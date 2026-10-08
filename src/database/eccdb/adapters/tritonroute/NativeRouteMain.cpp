// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include <sys/resource.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "AbstractGraphicsFactory.h"
#include "NativeDatabaseSnapshot.h"
#include "NativeEccdbImporter.h"
#include "NativeRouteSnapshot.h"
#include "IspdGuideReader.h"
#include "OdbTraceRecorder.h"
#include "def/DefDesignExporter.h"
#include "def/DefDesignImporter.h"
#include "design/DesignStore.h"
#include "dr/AbstractDRGraphics.h"
#include "drt/TritonRoute.h"
#include "dst/Distributed.h"
#include "db/obj/frInstTerm.h"
#include "db/obj/frAccess.h"
#include "db/obj/frPin.h"
#include "frDesign.h"
#include "io/io.h"
#include "io/EccdbContextAdapter.h"
#include "io/EccdbRouteAdapter.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "library/LibraryStore.h"
#include "odb/db.h"
#include "odb/defin.h"
#include "odb/defout.h"
#include "odb/lefin.h"
#include "pa/AbstractPAGraphics.h"
#include "service/routing/RoutingService.h"
#include "service/geometry/PhysicalDesignService.h"
#include "stt/SteinerTreeBuilder.h"
#include "ta/AbstractTAGraphics.h"
#include "tech/TechStore.h"
#include "utl/Logger.h"
#include "utl/ServiceRegistry.h"

namespace {
using Clock = std::chrono::steady_clock;

class HeadlessGraphics final : public drt::AbstractGraphicsFactory
{
 public:
  void reset(drt::frDebugSettings*, drt::frDesign*, odb::dbDatabase*, utl::Logger*, drt::RouterConfiguration*) override {}
  bool guiActive() override { return false; }
  std::unique_ptr<drt::AbstractDRGraphics> makeUniqueDRGraphics() override { return nullptr; }
  std::unique_ptr<drt::AbstractTAGraphics> makeUniqueTAGraphics() override { return nullptr; }
  std::unique_ptr<drt::AbstractPAGraphics> makeUniquePAGraphics() override { return nullptr; }
};

struct Options
{
  std::string backend = "eccdb";
  std::filesystem::path lef;
  std::filesystem::path def;
  std::filesystem::path guides;
  std::filesystem::path output;
  std::filesystem::path trace;
  int threads = 1;
  int iterations = -1;
  int max_routing_level = 0;
  bool import_only = false;
  bool save_guide_updates = false;
  bool write_net_tracks = false;
};

int parseInteger(std::string_view text)
{
  int result = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
    throw std::invalid_argument("invalid integer argument");
  }
  return result;
}

Options parseOptions(int argc, char** argv)
{
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto value = [&]() -> std::string_view {
      if (++index == argc) throw std::invalid_argument("missing value for " + std::string(argument));
      return argv[index];
    };
    if (argument == "--backend") options.backend = value();
    else if (argument == "--lef") options.lef = value();
    else if (argument == "--def") options.def = value();
    else if (argument == "--guides") options.guides = value();
    else if (argument == "--output") options.output = value();
    else if (argument == "--trace") options.trace = value();
    else if (argument == "--threads") options.threads = parseInteger(value());
    else if (argument == "--iterations") options.iterations = parseInteger(value());
    else if (argument == "--max-routing-level") {
      options.max_routing_level = parseInteger(value());
      if (options.max_routing_level <= 0) {
        throw std::invalid_argument("--max-routing-level requires a positive level");
      }
    }
    else if (argument == "--import-only") options.import_only = true;
    else if (argument == "--save-guide-updates") options.save_guide_updates = true;
    else if (argument == "--write-net-tracks") options.write_net_tracks = true;
    else throw std::invalid_argument("unknown argument " + std::string(argument));
  }
  if (options.backend != "eccdb" && options.backend != "odb") throw std::invalid_argument("backend must be eccdb or odb");
  if (!options.trace.empty() && options.backend != "odb") {
    throw std::invalid_argument("--trace is supported only with --backend odb");
  }
  if (options.write_net_tracks && options.backend != "odb") {
    throw std::invalid_argument("--write-net-tracks requires --backend odb");
  }
  if (options.max_routing_level != 0 && options.backend != "odb") {
    throw std::invalid_argument("--max-routing-level requires --backend odb");
  }
  if (options.threads < 1 || options.iterations == 0 || options.iterations < -1) throw std::invalid_argument("invalid thread/iteration count");
  if (options.output.empty()) throw std::invalid_argument("--output directory is required");
  for (const auto& path : {options.lef, options.def}) {
    if (!std::filesystem::is_regular_file(path)) throw std::invalid_argument("input does not exist: " + path.string());
  }
  if (!options.import_only && !std::filesystem::is_regular_file(options.guides)) {
    throw std::invalid_argument("--guides is required for routing");
  }
  return options;
}

struct EccdbStores
{
  eccdb::TechStore tech;
  eccdb::LibraryStore library{tech.techRegistry()};
  eccdb::DesignStore design{tech.techRegistry(), library.libraryRegistry()};
  eccdb::DatabaseContext context{tech, library, design};
};

using GuideGridStatistics = eccdb::tritonroute::guide_input::GridStatistics;
using eccdb::tritonroute::guide_input::readGuides;

std::array<eccdb::DesignGCellGrid, 2> guideGrid(const GuideGridStatistics& statistics, const odb::Rect& die)
{
  const auto most_used_size = [](const auto& counts) {
    int size = 0;
    std::size_t highest_count = 0;
    for (const auto& [candidate, count] : counts) {
      if (count > highest_count) { size = candidate; highest_count = count; }
    }
    if (size <= 0) throw std::runtime_error("cannot derive GCell grid from empty guides");
    return size;
  };
  const int width = most_used_size(statistics.widths);
  const int height = most_used_size(statistics.heights);
  // Match GlobalRouter::computeGCellGridPatternFromGuides, not DRT's fallback
  // inference, which may choose a different clipped boundary cell.
  const int origin_x = statistics.min_x
                       - static_cast<int>(std::floor((static_cast<double>(statistics.min_x) - die.xMin()) / width)) * width;
  const int origin_y = statistics.min_y
                       - static_cast<int>(std::floor((static_cast<double>(statistics.min_y) - die.yMin()) / height)) * height;
  return {{{.axis = eccdb::DesignAxis::kX, .start = origin_x,
            .line_count = static_cast<uint32_t>(die.dx() / width), .step = width},
           {.axis = eccdb::DesignAxis::kY, .start = origin_y,
            .line_count = static_cast<uint32_t>(die.dy() / height), .step = height}}};
}

void importGuides(EccdbStores& stores, const std::filesystem::path& path)
{
  const eccdb::PhysicalDesignService physical(stores.tech, stores.library, stores.design);
  eccdb::RoutingService context(stores.context);
  const auto grid = readGuides(path, [&](const auto& name, const auto& rectangles) {
    const auto net = stores.design.netlistStorage().findRegularNet(name);
    if (!net) throw std::runtime_error("unknown guide net " + name);
    std::vector<eccdb::DesignRouteGuide> guides;
    guides.reserve(rectangles.size());
    for (const auto& rectangle : rectangles) {
      const auto layer = stores.tech.findLayer(rectangle.layer);
      if (!layer || physical.layerKind(layer) != eccdb::GeometryLayerKind::kRouting) {
        throw std::runtime_error("unknown guide routing layer " + rectangle.layer);
      }
      guides.push_back({.box = {rectangle.x1, rectangle.y1, rectangle.x2, rectangle.y2},
                        .layer = eccdb::TechRoutingLayerId{layer.entity()}, .via_layer = {}});
    }
    // GRT's read_guides creates dbGuides by head insertion without reversing.
    // Preserve that order because guide patching uses first-match tie breaks.
    std::reverse(guides.begin(), guides.end());
    context.replaceGuides(net, std::move(guides));
  });
  const auto bounds = physical.dieBounds();
  const odb::Rect die(bounds.ll_x, bounds.ll_y, bounds.ur_x, bounds.ur_y);
  auto& floorplan = stores.design.floorplanStorage();
  for (const auto id : floorplan.gcellGrids()) static_cast<void>(floorplan.destroyGCellGrid(id));
  for (const auto& pattern : guideGrid(grid, die)) static_cast<void>(floorplan.createGCellGrid(pattern));
}

void importGuides(odb::dbDatabase& database, const std::filesystem::path& path)
{
  auto* block = database.getChip()->getBlock();
  const auto grid = readGuides(path, [&](const auto& name, const auto& rectangles) {
    auto* net = block->findNet(name.c_str());
    if (net == nullptr) throw std::runtime_error("unknown guide net " + name);
    for (const auto& rectangle : rectangles) {
      auto* layer = database.getTech()->findLayer(rectangle.layer.c_str());
      if (layer == nullptr) throw std::runtime_error("unknown guide layer " + rectangle.layer);
      if (odb::dbGuide::create(net, layer, layer, {rectangle.x1, rectangle.y1, rectangle.x2, rectangle.y2}, false) == nullptr) {
        throw std::runtime_error("cannot create OpenDB guide");
      }
    }
  });
  auto* database_grid = block->getGCellGrid();
  if (database_grid == nullptr) database_grid = odb::dbGCellGrid::create(block);
  else database_grid->resetGrid();
  for (const auto& pattern : guideGrid(grid, block->getDieArea())) {
    if (pattern.axis == eccdb::DesignAxis::kX) database_grid->addGridPatternX(pattern.start, pattern.line_count, pattern.step);
    else database_grid->addGridPatternY(pattern.start, pattern.line_count, pattern.step);
  }
}

uint64_t rssKiB()
{
  std::ifstream input("/proc/self/status");
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream fields(line);
    std::string key;
    uint64_t value = 0;
    if ((fields >> key >> value) && key == "VmRSS:") return value;
  }
  return 0;
}

double elapsedMs(Clock::time_point begin)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

void verifyNative(const drt::TritonRoute& router)
{
  if (router.getDb() != nullptr) throw std::logic_error("native routing retained an OpenDB database");
  const auto* design = router.getDesign();
  for (const auto& layer : design->getTech()->getLayers()) {
    if (layer->getDbLayer() != nullptr) throw std::logic_error("native routing retained an OpenDB layer");
  }
  const auto verify_access = [](const drt::frAccessPoint* access) {
    if (access != nullptr && access->getDbAccessPoint() != nullptr) {
      throw std::logic_error("native routing retained an OpenDB access point");
    }
  };
  const auto verify_pin = [&](const drt::frPin& pin) {
    for (int index = 0; index < pin.getNumPinAccess(); ++index) {
      const auto* group = pin.getPinAccess(index);
      if (group == nullptr) throw std::logic_error("native pin access group is missing");
      for (const auto& access : group->getAccessPoints()) verify_access(access.get());
    }
  };
  for (const auto& master : design->getMasters()) {
    for (const auto& term : master->getTerms()) {
      for (const auto& pin : term->getPins()) verify_pin(*pin);
    }
  }
  for (const auto& term : design->getTopBlock()->getTerms()) {
    for (const auto& pin : term->getPins()) verify_pin(*pin);
  }
  for (const auto& instance : design->getTopBlock()->getInsts()) {
    if (instance->getDBInst() != nullptr) throw std::logic_error("native routing retained an OpenDB instance");
    for (const auto& pin : instance->getInstTerms()) {
      if (pin->getInst() != instance.get()) throw std::logic_error("native pin owner is inconsistent");
      for (const auto* access : pin->getAccessPoints()) verify_access(access);
    }
  }
}

int run(const Options& options)
{
  std::filesystem::create_directories(options.output);
  utl::Logger logger;
  utl::ServiceRegistry services(&logger);
  dst::Distributed distributed(&logger);
  stt::SteinerTreeBuilder steiner(&logger);
  std::unique_ptr<EccdbStores> eccdb;
  using OdbPtr = std::unique_ptr<odb::dbDatabase, void (*)(odb::dbDatabase*)>;
  OdbPtr odb(nullptr, odb::dbDatabase::destroy);
  const auto load_begin = Clock::now();
  if (options.backend == "eccdb") {
    eccdb = std::make_unique<EccdbStores>();
    eccdb::LefTechImporter(eccdb->tech).import(options.lef);
    eccdb::LefLibraryImporter(eccdb->tech, eccdb->library).import(options.lef);
    eccdb::DefDesignImporter(eccdb->design).import(options.def);
    if (!options.guides.empty()) importGuides(*eccdb, options.guides);
  } else {
    odb.reset(odb::dbDatabase::create());
    odb->setLogger(&logger);
    odb::lefin lef_reader(odb.get(), &logger, false);
    if (lef_reader.createTechAndLib("native_comparison_tech", "native_comparison_lib", options.lef.c_str()) == nullptr) {
      throw std::runtime_error("OpenDB LEF import failed");
    }
    auto* chip = odb::dbChip::create(odb.get(), odb->getTech());
    std::vector<odb::dbLib*> libraries;
    for (auto* library : odb->getLibs()) libraries.push_back(library);
    odb::defin def_reader(odb.get(), &logger);
    def_reader.readChip(libraries, options.def.c_str(), chip);
    if (chip->getBlock() == nullptr) throw std::runtime_error("OpenDB DEF import failed");
    // Standard ISPD scripts allow Metal1 through the highest routing layer.
    // dbBlock defaults to routing level 2, unlike DRT's native level-1 default.
    chip->getBlock()->setMinRoutingLayer(1);
    if (options.max_routing_level != 0) {
      chip->getBlock()->setMaxRoutingLayer(options.max_routing_level);
    }
    if (!options.guides.empty()) importGuides(*odb, options.guides);
  }
  const auto load_ms = elapsedMs(load_begin);
  const auto loaded_rss = rssKiB();
  drt::TritonRoute router(odb.get(), &logger, &services, &distributed, &steiner);
  router.initGraphics(std::make_unique<HeadlessGraphics>());
  drt::ParamStruct parameters;
  parameters.outputDrcFile = (options.output / "routing.drc.rpt").string();
  parameters.outputGuideCoverageFile = (options.output / "guide-coverage.csv").string();
  parameters.enableViaGen = true;
  parameters.drouteEndIter = options.iterations;
  parameters.verbose = 0;
  parameters.doPa = true;
  parameters.saveGuideUpdates = options.save_guide_updates;
  parameters.num_threads = options.threads;
  router.setParams(parameters);
  router.setDebugWriteNetTracks(options.write_net_tracks);
  if (eccdb) router.attachEccdb(eccdb->context);

  std::unique_ptr<eccdb::tritonroute::OdbTraceRecorder> trace;
  if (!options.trace.empty()) {
    trace = std::make_unique<eccdb::tritonroute::OdbTraceRecorder>(
        options.trace,
        odb->getChip()->getBlock(),
        eccdb::tritonroute::OdbTraceInputPaths{.lef = options.lef,
                                                .def = options.def,
                                                .guides = options.guides});
  }
  const auto routing_begin = Clock::now();
  if (options.import_only) {
    if (eccdb) {
      const eccdb::PhysicalDesignService physical(eccdb->tech, eccdb->library, eccdb->design);
      drt::io::readEccdbTechnology(physical, *router.getDesign(), *router.getRouterConfiguration(), logger);
      drt::io::readEccdbLibrary(physical, *router.getDesign(), logger);
      drt::io::readEccdbDesign(physical, *router.getDesign(), *router.getRouterConfiguration(), logger);
      drt::io::EccdbRouteAdapter(eccdb->context, *router.getDesign()).readRoutes();
      drt::io::EccdbContextAdapter(eccdb->context, *router.getDesign()).readPinAccess();
    } else {
      drt::io::Parser parser(odb.get(), router.getDesign(), &logger, router.getRouterConfiguration());
      parser.readTechAndLibs(odb.get());
      parser.readDesign(odb.get());
    }
  } else if (router.main() != 0) {
    throw std::runtime_error("TritonRoute failed");
  }
  if (trace) trace->finish();
  const auto routing_ms = elapsedMs(routing_begin);
  const auto routed_rss = rssKiB();
  if (eccdb) verifyNative(router);
  const auto write_begin = Clock::now();
  if (eccdb) eccdb::DefDesignExporter(eccdb->design).write(options.output / "output.def");
  else {
    odb::DefOut writer(&logger);
    if (!writer.writeBlock(odb->getChip()->getBlock(), (options.output / "output.def").c_str())) {
      throw std::runtime_error("OpenDB DEF export failed");
    }
  }
  const auto write_ms = elapsedMs(write_begin);
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  std::ofstream metrics(options.output / "metrics.json");
  metrics << "{\"backend\":\"" << options.backend << "\",\"native\":" << (eccdb ? "true" : "false")
          << ",\"import_only\":" << (options.import_only ? "true" : "false")
          << ",\"load_including_parsers_ms\":" << load_ms << ",\"drt_ms\":" << routing_ms
          << ",\"def_write_ms\":" << write_ms << ",\"rss_loaded_kib\":" << loaded_rss
          << ",\"rss_drt_kib\":" << routed_rss << ",\"peak_before_snapshots_kib\":" << usage.ru_maxrss
          << ",\"drc\":" << (options.import_only ? -1 : router.getNumDRVs())
          << ",\"instances\":" << router.getDesign()->getTopBlock()->getInsts().size()
          << ",\"nets\":" << router.getDesign()->getTopBlock()->getNets().size()
          << ",\"special_nets\":" << router.getDesign()->getTopBlock()->getSNets().size()
          << ",\"vias\":" << router.getDesign()->getTech()->getVias().size()
          << ",\"via_rules\":" << router.getDesign()->getTech()->getViaRuleGenerates().size()
          << ",\"ndrs\":" << router.getDesign()->getTech()->getNondefaultRules().size()
          << ",\"routing_configuration\":{\"bottom_layer\":" << router.getRouterConfiguration()->BOTTOM_ROUTING_LAYER
          << ",\"top_layer\":" << router.getRouterConfiguration()->TOP_ROUTING_LAYER
          << ",\"via_access_layer\":" << router.getRouterConfiguration()->VIA_ACCESS_LAYERNUM
          << ",\"use_min_spacing_obs\":" << router.getRouterConfiguration()->USEMINSPACING_OBS
          << ",\"allow_pin_feedthrough\":" << router.getRouterConfiguration()->ALLOW_PIN_AS_FEEDTHROUGH
          << "}}\n";
  if (!metrics) throw std::runtime_error("cannot write metrics");
  if (eccdb) {
    const eccdb::PhysicalDesignService physical(eccdb->tech, eccdb->library, eccdb->design);
    drt::io::writeNativePlacementSnapshot(physical, options.output / "placement.txt");
  } else {
    drt::io::writeNativePlacementSnapshot(*odb->getChip()->getBlock(), options.output / "placement.txt");
  }
  drt::io::writeNativeRouteSnapshot(*router.getDesign(), options.output / "snapshot.txt", true);
  drt::io::writeNativeRouteSnapshot(*router.getDesign(), options.output / "design.txt", true, false);
  return 0;
}
}  // namespace

int main(int argc, char** argv)
{
  try { return run(parseOptions(argc, argv)); }
  catch (const std::exception& error) {
    std::cerr << "native route failed: " << error.what() << '\n';
    return 1;
  }
}
