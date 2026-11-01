// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

// Native route payload reading. Parsing and shared-field validation are untimed.
#include <malloc.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <boost/json.hpp>

#include "builder.h"
#include "def/DefDesignImporter.h"
#include "design/DesignStore.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"

namespace {
using Clock = std::chrono::steady_clock;
volatile uint64_t sink = 0;

struct Options
{
  std::filesystem::path lef, def, output, snapshot;
  std::string source;
};

struct Geometry
{
  uint64_t wires = 0, paths = 0, points = 0, vias = 0, rectangles = 0;
  uint64_t checksum_sum = 0, checksum_xor = 0;
};

uint64_t hash(std::string_view value)
{
  uint64_t result = UINT64_C(14695981039346656037);
  for (const unsigned char ch : value) result = (result ^ ch) * UINT64_C(1099511628211);
  return result;
}

uint64_t mix(uint64_t state, uint64_t value)
{
  return state ^ (value + UINT64_C(0x9e3779b97f4a7c15) + (state << 6) + (state >> 2));
}

uint64_t rss(std::string_view key)
{
  std::ifstream input("/proc/self/status");
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream row(line);
    std::string name, unit;
    uint64_t value;
    if (row >> name >> value >> unit && name == key && unit == "kB") return value;
  }
  return 0;
}

boost::json::object json(const Geometry& value)
{
  return {{"wires", value.wires}, {"paths", value.paths}, {"points", value.points},
          {"vias", value.vias}, {"rectangles", value.rectangles},
          {"checksum_sum", value.checksum_sum}, {"checksum_xor", value.checksum_xor}};
}

// Same shared-field tuple on both backends. It deliberately omits attributes
// that iDB discards, including regular-wire flush extensions and masks.
struct Path
{
  bool special = false;
  int status = 0;
  std::string net, layer;
  std::string shape;
  int style = -1;
  int32_t width = 0;
  std::vector<eccdb::Point> points;
  struct Via { std::string name; eccdb::Point origin; };
  std::vector<Via> vias;
  std::vector<eccdb::Rect> rectangles;

  std::string canonical() const
  {
    if (points.empty() || points.size() > 2 || vias.size() > 1 || rectangles.size() > 1
        || (points.size() == 1 && vias.empty() && rectangles.empty())
        || (points.size() == 2 && (!vias.empty() || !rectangles.empty()))
        || (!vias.empty() && !rectangles.empty())) {
      throw std::runtime_error("wire comparison requires two-point lines or single-point VIA/RECT paths: " + net);
    }
    auto ordered = points;
    if (ordered.size() == 2) {
      if (ordered[0].x != ordered[1].x && ordered[0].y != ordered[1].y) {
        throw std::runtime_error("non-Manhattan path is outside the comparison: " + net);
      }
      if (ordered[0].x > ordered[1].x || (ordered[0].x == ordered[1].x && ordered[0].y > ordered[1].y)) {
        std::swap(ordered[0], ordered[1]);
      }
    }
    boost::json::array p, v, r;
    for (const auto& point : ordered) p.emplace_back(boost::json::array{point.x, point.y});
    for (const auto& via : vias) v.emplace_back(boost::json::array{via.name, via.origin.x, via.origin.y});
    for (const auto& rect : rectangles) r.emplace_back(boost::json::array{rect.ll_x, rect.ll_y, rect.ur_x, rect.ur_y});
    return boost::json::serialize(boost::json::array{special, status, net, layer, width, shape, style,
                                                   std::move(p), std::move(v), std::move(r)});
  }
};

int status(eccdb::DesignWireStatus value)
{
  switch (value) {
    case eccdb::DesignWireStatus::kRouted: return 0;
    case eccdb::DesignWireStatus::kFixed: return 1;
    case eccdb::DesignWireStatus::kCover: return 2;
    case eccdb::DesignWireStatus::kShield: return 3;
    case eccdb::DesignWireStatus::kNoShield: return 4;
  }
  throw std::runtime_error("unknown EccDB wire status");
}

int status(idb::IdbWiringStatement value)
{
  switch (value) {
    case idb::IdbWiringStatement::kRouted: return 0;
    case idb::IdbWiringStatement::kFixed: return 1;
    case idb::IdbWiringStatement::kCover: return 2;
    case idb::IdbWiringStatement::kShield: return 3;
    case idb::IdbWiringStatement::kNoShield: return 4;
    default: throw std::runtime_error("unknown IDB wire status");
  }
}

struct Scan
{
  Geometry geometry;
  std::vector<std::string>* snapshot = nullptr;

  void path(const Path& value)
  {
    ++geometry.paths;
    geometry.points += value.points.size();
    geometry.vias += value.vias.size();
    geometry.rectangles += value.rectangles.size();
    const auto record = value.canonical();
    const auto fingerprint = hash(record);
    geometry.checksum_sum += fingerprint;
    geometry.checksum_xor ^= fingerprint;
    if (snapshot) snapshot->push_back(record);
  }
};

struct Entt
{
  eccdb::TechStore tech;
  eccdb::LibraryStore library{tech.techRegistry()};
  eccdb::DesignStore design{tech.techRegistry(), library.libraryRegistry()};

  explicit Entt(const Options& options)
  {
    eccdb::LefTechImporter(tech).import(options.lef);
    eccdb::LefLibraryImporter(tech, library).import(options.lef);
    eccdb::DefDesignImporter(design).import(options.def);
  }

  boost::json::object counts()
  {
    auto& netlist = design.netlistStorage();
    return {{"instances", netlist.instanceCount()}, {"regular_nets", netlist.regularNets().size()},
            {"special_nets", netlist.specialNets().size()}, {"instance_pins", netlist.instancePinCount()},
            {"io_pins", netlist.ioPinCount()}};
  }

  template<bool Validate>
  Geometry scan(bool special, std::vector<std::string>* snapshot = nullptr)
  {
    Scan out{{}, snapshot};
    auto& netlist = design.netlistStorage();
    auto& routing = design.routingStorage();
    uint64_t checksum = 0;
    auto visit_net = [&](eccdb::DesignNetId net, const eccdb::DesignNet& net_value) {
      if constexpr (Validate) {
        if (special && routing.netGeometry(net)) {
          throw std::runtime_error("standalone SPECIALNET RECT/POLYGON/VIA is outside the wire-path comparison");
        }
      }
      for (const auto wire : routing.wireIds(net)) {
        ++out.geometry.wires;
        routing.forEachShape(wire, [&](eccdb::DesignRouteShapeId, const eccdb::DesignRouteShape& shape,
                                       const eccdb::DesignRouteShapeAttributes* attributes) {
          const auto append_path = [&](Path value) {
            if constexpr (Validate) {
              out.path(value);
            } else {
              ++out.geometry.paths;
              out.geometry.points += value.points.size();
              out.geometry.vias += value.vias.size();
              out.geometry.rectangles += value.rectangles.size();
              for (const auto point : value.points) {
                checksum = mix(checksum, static_cast<uint32_t>(point.x));
                checksum = mix(checksum, static_cast<uint32_t>(point.y));
              }
              for (const auto& via : value.vias) {
                checksum = mix(checksum, static_cast<uint32_t>(via.origin.x));
                checksum = mix(checksum, static_cast<uint32_t>(via.origin.y));
              }
              for (const auto& rect : value.rectangles) {
                checksum = mix(checksum, static_cast<uint32_t>(rect.ll_x));
                checksum = mix(checksum, static_cast<uint32_t>(rect.ll_y));
                checksum = mix(checksum, static_cast<uint32_t>(rect.ur_x));
                checksum = mix(checksum, static_cast<uint32_t>(rect.ur_y));
              }
            }
          };
          Path value;
          value.special = special;
          value.status = status(routing.wire(wire).status);
          value.net = net_value.name;
          value.layer = tech.layerInfo(eccdb::TechLayerId{shape.layer.entity()}).name;
          if (special && attributes != nullptr) {
            value.width = attributes->width;
            value.shape = attributes->shape;
            if ((shape.flags & eccdb::DesignWirePathFlag::kHasStyle) != 0u) value.style = attributes->style;
          }
          std::visit(
              [&](const auto& geometry) {
                using Geometry = std::remove_cvref_t<decltype(geometry)>;
                if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteSegment>) {
                  value.points = {geometry.begin.position, geometry.end.position};
                } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteVia>) {
                  value.points = {geometry.origin.position};
                  const auto& via = geometry.via;
                  const auto name = via.tech_via ? tech.viaMasterStorage().viaMaster(via.tech_via).name
                                                 : routing.via(via.design_via).name;
                  value.vias.push_back({name, geometry.origin.position});
                } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRoutePatch>) {
                  value.points = {geometry.anchor.position};
                  value.rectangles.push_back(geometry.rectangle);
                } else {
                  value.points = {geometry.point.position};
                }
              },
              shape.geometry);
          append_path(std::move(value));
        });
      }
    };
    if (special) netlist.forEachSpecialNet(visit_net);
    else netlist.forEachRegularNet(visit_net);
    sink = checksum;
    return out.geometry;
  }
};

struct Idb
{
  idb::IdbBuilder builder;
  idb::IdbDesign* design = nullptr;

  explicit Idb(const Options& options)
  {
    std::vector<std::string> files{options.lef.string()};
    if (!builder.buildLef(files, false)) throw std::runtime_error("IDB LEF import failed");
    const auto service = builder.buildDef(options.def.string());
    if (!service || !(design = service->get_design())) throw std::runtime_error("IDB DEF import failed");
  }

  boost::json::object counts()
  {
    uint64_t pins = 0;
    for (const auto instance : design->get_instance_list()->get_instance_list()) pins += instance->get_pin_list()->get_pin_num();
    return {{"instances", design->get_instance_list()->get_instance_list().size()},
            {"regular_nets", design->get_net_list()->get_num()},
            {"special_nets", design->get_special_net_list()->get_num()},
            {"instance_pins", pins}, {"io_pins", design->get_io_pin_list()->get_pin_num()}};
  }

  template<bool Validate, class Nets>
  Geometry scanNets(Nets& nets, bool special, std::vector<std::string>* snapshot)
  {
    Scan out{{}, snapshot};
    uint64_t checksum = 0;
    for (const auto net : nets) {
      for (const auto wire : net->get_wire_list()->get_wire_list()) {
        ++out.geometry.wires;
        for (const auto segment : wire->get_segment_list()) {
          constexpr bool is_special = std::is_same_v<std::remove_pointer_t<std::remove_cv_t<decltype(segment)>>,
                                                    idb::IdbSpecialWireSegment>;
          if constexpr (Validate) {
            Path value;
            value.special = special;
            if constexpr (is_special) value.status = status(wire->get_wire_state());
            else value.status = status(wire->get_wire_statement());
            value.net = net->get_net_name();
            value.layer = segment->get_layer()->get_name();
            if constexpr (is_special) {
              value.width = segment->get_route_width();
              value.style = segment->get_style();
              if (segment->get_shape_type() != idb::IdbWireShapeType::kNone) {
                static idb::IdbConnectProperty properties;
                value.shape = properties.get_wire_shape_name(segment->get_shape_type());
              }
            }
            for (const auto point : segment->get_point_list()) value.points.push_back({point->get_x(), point->get_y()});
            auto via = [&](idb::IdbVia* item) {
              value.vias.push_back({item->get_name(), {item->get_coordinate()->get_x(), item->get_coordinate()->get_y()}});
            };
            if constexpr (is_special) {
              if (segment->is_via()) via(segment->get_via());
            } else {
              for (const auto item : segment->get_via_list()) via(item);
            }
            if (segment->is_rect()) {
              const auto rect = segment->get_delta_rect();
              if (value.points.size() != 1) throw std::runtime_error("RECT path must have a single anchor");
              const int32_t x = special ? 0 : value.points[0].x;
              const int32_t y = special ? 0 : value.points[0].y;
              value.rectangles.push_back({x + rect->get_low_x(), y + rect->get_low_y(),
                                         x + rect->get_high_x(), y + rect->get_high_y()});
            }
            out.path(value);
          } else {
            ++out.geometry.paths;
            out.geometry.points += segment->get_point_list().size();
            for (const auto point : segment->get_point_list()) {
              checksum = mix(checksum, static_cast<uint32_t>(point->get_x()));
              checksum = mix(checksum, static_cast<uint32_t>(point->get_y()));
            }
            auto via = [&](idb::IdbVia* item) {
              ++out.geometry.vias;
              checksum = mix(checksum, static_cast<uint32_t>(item->get_coordinate()->get_x()));
              checksum = mix(checksum, static_cast<uint32_t>(item->get_coordinate()->get_y()));
            };
            if constexpr (is_special) {
              if (segment->is_via()) via(segment->get_via());
            } else {
              for (const auto item : segment->get_via_list()) via(item);
            }
            if (segment->is_rect()) {
              const auto rect = segment->get_delta_rect();
              ++out.geometry.rectangles;
              checksum = mix(checksum, static_cast<uint32_t>(rect->get_low_x()));
              checksum = mix(checksum, static_cast<uint32_t>(rect->get_low_y()));
              checksum = mix(checksum, static_cast<uint32_t>(rect->get_high_x()));
              checksum = mix(checksum, static_cast<uint32_t>(rect->get_high_y()));
            }
          }
        }
      }
    }
    sink = checksum;
    return out.geometry;
  }

  template<bool Validate>
  Geometry scan(bool special, std::vector<std::string>* snapshot = nullptr)
  {
    return special ? scanNets<Validate>(design->get_special_net_list()->get_net_list(), true, snapshot)
                   : scanNets<Validate>(design->get_net_list()->get_net_list(), false, snapshot);
  }
};

template<class Backend>
boost::json::object run(const Options& options)
{
  Backend db(options);
  const auto counts = db.counts();
  malloc_trim(0);
  const auto source_rss = rss("VmRSS:");
  const auto source_peak = rss("VmHWM:");
  boost::json::object timings;
  for (const auto special : {false, true}) {
    const auto start = Clock::now();
    const auto result = db.template scan<false>(special);
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    timings[special ? "read_special_wire_ns" : "read_regular_wire_ns"] = ns;
    sink = result.points + result.vias + result.rectangles;
  }
  std::vector<std::string> snapshot;
  auto* destination = options.snapshot.empty() ? nullptr : &snapshot;
  const auto regular = db.template scan<true>(false, destination);
  const auto special = db.template scan<true>(true, destination);
  if (destination) {
    std::sort(snapshot.begin(), snapshot.end());
    std::ofstream output(options.snapshot);
    if (!output) throw std::runtime_error("cannot open snapshot");
    for (const auto& row : snapshot) output << row << '\n';
  }
  return {{"schema", "idb_wire_benchmark_v1"}, {"backend", options.source == "idb" ? "idb" : "eccdb"},
          {"scope", "native wire-path coordinate/via/rectangle reading; parser and shared-field validation excluded"},
          {"counts", counts}, {"runs", boost::json::array{timings}},
          {"geometry", boost::json::object{{"regular", json(regular)}, {"special", json(special)}}},
          {"memory", boost::json::object{{"rss_after_load_kib", source_rss}, {"peak_rss_kib", source_peak}}}};
}

Options parse(int argc, char** argv)
{
  Options result;
  for (int i = 1; i < argc; ++i) {
    const std::string key = argv[i];
    if (key == "--help") {
      std::cout << "usage: eccdb_idb_comparison_benchmark --lef FILE --def FILE --source idb|entt --output JSON [--snapshot JSONL]\n";
      std::exit(0);
    }
    if (i + 1 == argc) throw std::runtime_error("missing value for " + key);
    const std::string value = argv[++i];
    if (key == "--lef") result.lef = value;
    else if (key == "--def") result.def = value;
    else if (key == "--output") result.output = value;
    else if (key == "--snapshot") result.snapshot = value;
    else if (key == "--source") result.source = value;
    else throw std::runtime_error("unknown argument " + key);
  }
  if (!std::filesystem::is_regular_file(result.lef) || !std::filesystem::is_regular_file(result.def)
      || result.output.empty() || (result.source != "entt" && result.source != "idb")) {
    throw std::runtime_error("LEF, DEF, output and source are required");
  }
  return result;
}
}  // namespace

int main(int argc, char** argv)
{
  try {
    const auto options = parse(argc, argv);
    const auto result = options.source == "idb" ? run<Idb>(options) : run<Entt>(options);
    std::ofstream output(options.output);
    if (!output) throw std::runtime_error("cannot open output");
    output << boost::json::serialize(result) << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
