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

// Import-only half of the ISPD18 test6 OpenDB/EccDB comparison.  The Python
// runner prepares the repaired LEF once and supplies that exact path here and
// to OpenDB; keeping the repair outside this executable prevents either
// backend from silently measuring a different input.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "def/DefDesignImporter.h"
#include "design/DesignStore.h"
#include "design/constraint/model/ConstraintComponents.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/via/component/ViaComponents.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"
#include "tech/cut_layer/model/CutLayerComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/via_master/model/ViaMasterComponents.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Options
{
  std::filesystem::path lef;
  std::filesystem::path def;
  std::filesystem::path output;
};

struct Counts
{
  uint64_t layers = 0;
  uint64_t routing_layers = 0;
  uint64_t cut_layers = 0;
  uint64_t tech_vias = 0;
  uint64_t sites = 0;
  uint64_t masters = 0;
  uint64_t master_terms = 0;
  uint64_t rows = 0;
  // EccDB retains each DEF TRACKS statement.  A statement can name multiple
  // layers, so this is the representation-neutral count used against OpenDB.
  uint64_t track_grid_axis_patterns = 0;
  uint64_t track_grids = 0;
  uint64_t instances = 0;
  uint64_t instance_pins = 0;
  uint64_t io_pins = 0;
  uint64_t regular_nets = 0;
  uint64_t special_nets = 0;
  uint64_t design_vias = 0;
  uint64_t blockages = 0;
};

struct Measurement
{
  uint64_t lef_tech_elapsed_ns = 0;
  uint64_t lef_library_elapsed_ns = 0;
  uint64_t lef_elapsed_ns = 0;
  uint64_t def_elapsed_ns = 0;
  Counts counts;
};

// Registry::storage<T>() is nullable for an absent component in this EnTT
// version.  A view is both null-safe and keeps the count independent of the
// registry's internal sparse-set implementation.
template <typename Component, typename Registry>
[[nodiscard]] uint64_t countComponent(const Registry& registry)
{
  uint64_t result = 0;
  const auto view = registry.template view<const Component>();
  // Tag components such as DesignSpecialNet are invoked with no callback
  // arguments by EnTT, while value components receive an entity/value pair.
  view.each([&result](const auto&...) { ++result; });
  return result;
}

[[nodiscard]] uint64_t elapsedNs(Clock::time_point begin)
{
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count());
}

[[nodiscard]] Options parseOptions(int argc, char** argv)
{
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto value = [&](std::string_view name) -> std::filesystem::path {
      if (index + 1 >= argc) {
        throw std::runtime_error("missing value for " + std::string(name));
      }
      return argv[++index];
    };
    if (argument == "--lef") {
      options.lef = value(argument);
    } else if (argument == "--def") {
      options.def = value(argument);
    } else if (argument == "--output") {
      options.output = value(argument);
    } else if (argument == "--help" || argument == "-h") {
      std::cout << "usage: eccdb_ispd18_import_benchmark --lef REPAIRED_LEF --def DEF [--output JSON]\n";
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + std::string(argument));
    }
  }
  if (!std::filesystem::is_regular_file(options.lef)) {
    throw std::runtime_error("LEF is not a regular file: " + options.lef.string());
  }
  if (!std::filesystem::is_regular_file(options.def)) {
    throw std::runtime_error("DEF is not a regular file: " + options.def.string());
  }
  return options;
}

[[nodiscard]] Counts counts(const eccdb::TechStore& technology, const eccdb::LibraryStore& library,
                            const eccdb::DesignStore& design)
{
  Counts result;

  const auto& tech_registry = technology.techRegistry().registry();
  result.layers = countComponent<eccdb::TechLayerInfo>(tech_registry);
  result.routing_layers = countComponent<eccdb::TechRoutingLayer>(tech_registry);
  result.cut_layers = countComponent<eccdb::TechCutLayer>(tech_registry);
  result.tech_vias = countComponent<eccdb::TechViaMaster>(tech_registry);

  result.sites = library.siteStorage().siteCount();
  result.masters = library.cellMasterStorage().cellMasterCount();
  result.master_terms = library.masterTermStorage().masterTermCount();

  const auto& design_registry = design.designRegistry().registry();
  result.rows = countComponent<eccdb::DesignRow>(design_registry);
  const auto track_grids = design_registry.view<const eccdb::DesignTrackGrid>();
  track_grids.each([&result](const auto, const eccdb::DesignTrackGrid& grid) {
    ++result.track_grids;
    result.track_grid_axis_patterns += grid.layers.size();
  });
  result.instances = countComponent<eccdb::DesignInstance>(design_registry);
  result.instance_pins = design.netlistStorage().instancePinCount();
  result.io_pins = countComponent<eccdb::DesignIoPin>(design_registry);
  result.special_nets = countComponent<eccdb::DesignSpecialNet>(design_registry);
  const auto regular_nets = design_registry.view<const eccdb::DesignNet>(entt::exclude<eccdb::DesignSpecialNet>);
  regular_nets.each([&result](const auto, const eccdb::DesignNet&) { ++result.regular_nets; });
  result.design_vias = countComponent<eccdb::DesignVia>(design_registry);
  result.blockages = countComponent<eccdb::DesignBlockage>(design_registry);
  return result;
}

[[nodiscard]] Measurement import(const Options& options)
{
  eccdb::TechStore technology;
  const auto lef_begin = Clock::now();
  eccdb::LefTechImporter technology_importer(technology);
  technology_importer.import(options.lef);
  const uint64_t lef_tech_elapsed_ns = elapsedNs(lef_begin);

  eccdb::LibraryStore library(technology.techRegistry());
  const auto library_begin = Clock::now();
  eccdb::LefLibraryImporter library_importer(technology, library);
  library_importer.import(options.lef);
  const uint64_t lef_library_elapsed_ns = elapsedNs(library_begin);

  eccdb::DesignStore design(technology.techRegistry(), library.libraryRegistry());
  const auto def_begin = Clock::now();
  eccdb::DefDesignImporter design_importer(design);
  design_importer.import(options.def);
  const uint64_t def_elapsed_ns = elapsedNs(def_begin);

  return Measurement{.lef_tech_elapsed_ns = lef_tech_elapsed_ns,
                     .lef_library_elapsed_ns = lef_library_elapsed_ns,
                     .lef_elapsed_ns = lef_tech_elapsed_ns + lef_library_elapsed_ns,
                     .def_elapsed_ns = def_elapsed_ns,
                     .counts = counts(technology, library, design)};
}

void writeJsonString(std::ostream& output, std::string_view value)
{
  output << '"';
  for (const char character : value) {
    switch (character) {
      case '"':
        output << "\\\"";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        output << character;
        break;
    }
  }
  output << '"';
}

void writeCounts(std::ostream& output, const Counts& value)
{
  output << "{\"layers\":" << value.layers << ",\"routing_layers\":" << value.routing_layers
         << ",\"cut_layers\":" << value.cut_layers << ",\"tech_vias\":" << value.tech_vias
         << ",\"sites\":" << value.sites << ",\"masters\":" << value.masters << ",\"master_terms\":"
         << value.master_terms << ",\"rows\":" << value.rows << ",\"track_grids\":" << value.track_grids
         << ",\"track_grid_axis_patterns\":" << value.track_grid_axis_patterns
         << ",\"instances\":" << value.instances << ",\"instance_pins\":" << value.instance_pins
         << ",\"io_pins\":" << value.io_pins << ",\"regular_nets\":" << value.regular_nets
         << ",\"special_nets\":" << value.special_nets << ",\"design_vias\":" << value.design_vias
         << ",\"blockages\":" << value.blockages << '}';
}

void writeJson(std::ostream& output, const Options& options, const Measurement& measurement)
{
  output << "{\"schema\":\"eccdb_import_measurement_v1\",\"backend\":\"eccdb\",\"lef_passes\":2,\"lef\":";
  writeJsonString(output, options.lef.string());
  output << ",\"def\":";
  writeJsonString(output, options.def.string());
  output << ",\"lef_tech_elapsed_ns\":" << measurement.lef_tech_elapsed_ns
         << ",\"lef_library_elapsed_ns\":" << measurement.lef_library_elapsed_ns
         << ",\"lef_elapsed_ns\":" << measurement.lef_elapsed_ns << ",\"def_elapsed_ns\":" << measurement.def_elapsed_ns
         << ",\"total_elapsed_ns\":" << measurement.lef_elapsed_ns + measurement.def_elapsed_ns << ",\"counts\":";
  writeCounts(output, measurement.counts);
  output << "}\n";
}

void run(int argc, char** argv)
{
  const Options options = parseOptions(argc, argv);
  const Measurement measurement = import(options);

  if (options.output.empty()) {
    writeJson(std::cout, options, measurement);
    return;
  }

  if (const auto parent = options.output.parent_path(); !parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream output(options.output, std::ios::out | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("cannot open output: " + options.output.string());
  }
  writeJson(output, options, measurement);
  if (!output) {
    throw std::runtime_error("cannot write output: " + options.output.string());
  }
}

}  // namespace

int main(int argc, char** argv)
{
  try {
    run(argc, argv);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "eccdb_ispd18_import_benchmark: " << error.what() << '\n';
    return 1;
  }
}
