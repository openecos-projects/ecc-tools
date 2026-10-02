// SPDX-License-Identifier: Apache-2.0

// Service-level benchmark for the EccDB APIs consumed by the TritonRoute
// adapter.  LEF/DEF parsing and guide JSONL parsing happen before timing; the
// timed sections call only the public EccDB services.

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <boost/json.hpp>

#include "DbOnlyBenchCommon.h"
#include "def/DefDesignImporter.h"
#include "design/DesignStore.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/via/component/ViaComponents.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "library/LibraryStore.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "service/context/RoutingContextService.h"
#include "service/query/PhysicalDesignService.h"
#include "service/routing/NetRouteService.h"
#include "tech/TechStore.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/via_master/model/ViaMasterComponents.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Options
{
  std::filesystem::path lef;
  std::filesystem::path routed_def;
  std::filesystem::path fixture;
  std::filesystem::path guides;
  std::filesystem::path output;
  std::size_t repeat = 1;
  bool run_experiments = true;
  bool shadow_find_first = false;
  bool pins_only = false;
};

struct Counts
{
  uint64_t instances = 0;
  uint64_t regular_nets = 0;
  uint64_t instance_pins = 0;
  uint64_t connected_iterms = 0;
  uint64_t io_pins = 0;
  uint64_t guides = 0;
  uint64_t routed_nets = 0;
  uint64_t route_shapes = 0;
  uint64_t route_vias = 0;
};

struct Timing
{
  uint64_t scan_regular_nets_ns = 0;
  uint64_t find_instances_ns = 0;
  uint64_t find_nets_ns = 0;
  uint64_t find_pins_ns = 0;
  uint64_t find_pins_shadow_ns = 0;
  uint64_t scan_net_pins_ns = 0;
  uint64_t scan_net_pins_direct_registry_ns = 0;
  uint64_t scan_net_pins_cached_ns = 0;
  uint64_t scan_net_pins_shadow_registry_ns = 0;
  uint64_t scan_net_pins_shadow_shared_ns = 0;
  uint64_t scan_net_pins_shadow_inline_ns = 0;
  uint64_t scan_guides_ns = 0;
  uint64_t decode_routes_ns = 0;
  uint64_t replace_guides_ns = 0;
};

struct Run
{
  Timing timing;
  uint64_t checksum = 0;
};

struct ImportedDesign
{
  eccdb::TechStore technology;
  eccdb::LibraryStore library;
  eccdb::DesignStore design;

  ImportedDesign() : library(technology.techRegistry()), design(technology.techRegistry(), library.libraryRegistry()) {}
};

struct NetProbe
{
  eccdb::DesignNetId id;
  std::string_view name;
};

struct PinProbe
{
  eccdb::DesignInstanceId instance;
  eccdb::DesignInstancePinId expected;
  std::string_view instance_name;
  std::string_view term_name;
};

struct GuidePayload
{
  eccdb::DesignNetId net;
  std::string_view name;
  std::vector<eccdb::DesignRouteGuide> values;
  bool appeared_in_input = false;
};

struct QueryIndices
{
  std::vector<std::size_t> instances;
  std::vector<std::size_t> nets;
  std::vector<std::size_t> pins;
};

struct RouteSummary
{
  uint64_t routed_nets = 0;
  uint64_t shapes = 0;
  uint64_t vias = 0;
};

struct BenchmarkCache
{
  std::vector<eccdb::DesignInstanceId> instances;
  std::vector<std::string_view> instance_names;
  std::vector<NetProbe> nets;
  std::vector<PinProbe> pins;
  std::vector<std::size_t> instance_pin_offsets;
  std::vector<GuidePayload> guide_payloads;
  std::vector<NetProbe> routed_nets;
  QueryIndices query_indices;
  uint64_t net_pin_entries = 0;
};

// This is intentionally benchmark-local.  The library is immutable after
// import here, so a term pointer is valid for the process lifetime but is not
// part of EccDB storage or its persistence contract.
struct MasterTermPointerSlot
{
  uint32_t packed_id = 0;
  const eccdb::LibraryMasterTerm* term = nullptr;
};

struct MasterTermPointerTable
{
  std::vector<MasterTermPointerSlot> slots;
  uint64_t build_ns = 0;
  uint64_t payload_bytes = 0;
  uint64_t terms = 0;

  [[nodiscard]] const eccdb::LibraryMasterTerm& get(eccdb::LibraryMasterTermId id) const noexcept
  {
    return *slots[static_cast<std::size_t>(entt::to_entity(id.entity()))].term;
  }
};

// Benchmark-only shadow of the per-instance terminal map used by
// findInstancePin(). This preserves the nested dense-map lookup while avoiding
// a repeated EnTT component lookup.
struct BenchInstancePinLookupSlot
{
  const eccdb::DesignInstanceTerms* terms = nullptr;
};

struct BenchInstancePinLookupTable
{
  std::vector<BenchInstancePinLookupSlot> slots;
  uint64_t build_ns = 0;
  uint64_t payload_bytes = 0;
  uint64_t instances = 0;
  uint64_t probes = 0;

  [[nodiscard]] const BenchInstancePinLookupSlot& get(eccdb::DesignInstanceId id) const noexcept
  {
    return slots[static_cast<std::size_t>(entt::to_entity(id.entity()))];
  }
};

// Benchmark-only lookup table over opaque instance-pin IDs. It mirrors the
// adapter's one-time PinId -> record pointer table without changing the
// storage or binary archive contract.
struct BenchCachedPin
{
  const eccdb::DesignInstancePin* pin = nullptr;
  const eccdb::LibraryMasterTerm* term = nullptr;
};

struct BenchCachedPinTable
{
  std::vector<BenchCachedPin> slots;
  uint64_t build_ns = 0;
  uint64_t payload_bytes = 0;
  uint64_t pins = 0;

  [[nodiscard]] const BenchCachedPin& get(eccdb::DesignInstancePinId id) const noexcept
  {
    return slots[static_cast<std::size_t>(id.packed())];
  }
};

struct NetPinScanResult
{
  uint64_t checksum = UINT64_C(0x5);
  uint64_t instance_pins = 0;
  uint64_t io_pins = 0;
};

struct ViaLayers
{
  eccdb::TechRoutingLayerId bottom;
  eccdb::TechRoutingLayerId top;
};

volatile uint64_t g_sink = 0;

[[noreturn]] void fail(std::string message)
{
  throw std::runtime_error(std::move(message));
}

void require(bool condition, std::string_view message)
{
  if (!condition) fail(std::string(message));
}

[[nodiscard]] uint64_t elapsedNs(Clock::time_point begin)
{
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count());
}

[[nodiscard]] uint64_t foldChecksum(uint64_t seed, uint64_t value)
{
  return seed ^ (value + UINT64_C(0x9e3779b97f4a7c15) + (seed << 6u) + (seed >> 2u));
}

[[nodiscard]] uint64_t hashText(std::string_view value)
{
  uint64_t hash = UINT64_C(1469598103934665603);
  for (const unsigned char character : value) {
    hash ^= character;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

[[nodiscard]] uint64_t hashPoint(eccdb::Point point)
{
  return foldChecksum(static_cast<uint32_t>(point.x), static_cast<uint32_t>(point.y));
}

[[nodiscard]] uint64_t hashRect(eccdb::Rect rectangle)
{
  uint64_t value = foldChecksum(static_cast<uint32_t>(rectangle.ll_x), static_cast<uint32_t>(rectangle.ll_y));
  value = foldChecksum(value, static_cast<uint32_t>(rectangle.ur_x));
  return foldChecksum(value, static_cast<uint32_t>(rectangle.ur_y));
}

template <typename Function>
[[nodiscard]] uint64_t measure(std::string_view phase, Function&& function, uint64_t& checksum)
{
  const auto begin = Clock::now();
  const uint64_t value = std::forward<Function>(function)();
  const uint64_t elapsed = elapsedNs(begin);
  require(value != 0u, std::string(phase) + " checksum is zero");
  g_sink = g_sink ^ value;
  checksum = foldChecksum(checksum, value);
  return elapsed;
}

[[nodiscard]] boost::json::string_view jsonStringView(std::string_view value)
{
  return {value.data(), value.size()};
}

[[nodiscard]] const boost::json::value& jsonMember(const boost::json::object& object, std::string_view name,
                                                     std::string_view context)
{
  const auto found = object.find(jsonStringView(name));
  if (found == object.end()) fail(std::string(context) + ": missing '" + std::string(name) + "'");
  return found->value();
}

[[nodiscard]] const boost::json::object& jsonObject(const boost::json::value& value, std::string_view context)
{
  if (!value.is_object()) fail(std::string(context) + " must be an object");
  return value.as_object();
}

[[nodiscard]] const boost::json::array& jsonArray(const boost::json::value& value, std::string_view context)
{
  if (!value.is_array()) fail(std::string(context) + " must be an array");
  return value.as_array();
}

[[nodiscard]] std::string jsonString(const boost::json::value& value, std::string_view context)
{
  if (!value.is_string()) fail(std::string(context) + " must be a string");
  const auto& source = value.as_string();
  std::string result(source.data(), source.size());
  if (result.empty() || result.find('\0') != std::string::npos) {
    fail(std::string(context) + " must be a non-empty string without NUL bytes");
  }
  return result;
}

[[nodiscard]] uint64_t jsonUnsigned(const boost::json::value& value, std::string_view context)
{
  if (value.is_uint64()) return value.as_uint64();
  if (value.is_int64() && value.as_int64() >= 0) return static_cast<uint64_t>(value.as_int64());
  fail(std::string(context) + " must be a non-negative integer");
}

[[nodiscard]] int32_t jsonInt32(const boost::json::value& value, std::string_view context)
{
  int64_t result = 0;
  if (value.is_int64()) {
    result = value.as_int64();
  } else if (value.is_uint64() && value.as_uint64() <= static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
    result = static_cast<int64_t>(value.as_uint64());
  } else {
    fail(std::string(context) + " must be an int32 integer");
  }
  if (result < std::numeric_limits<int32_t>::min() || result > std::numeric_limits<int32_t>::max()) {
    fail(std::string(context) + " is outside int32 range");
  }
  return static_cast<int32_t>(result);
}

[[nodiscard]] std::string lineContext(std::size_t line)
{
  return "guides JSONL line " + std::to_string(line);
}

[[nodiscard]] eccdb::TechRoutingLayerId requireRoutingLayer(const eccdb::TechStore& technology, std::string_view name,
                                                             std::string_view context)
{
  const auto layer = technology.findLayer(name);
  const auto routing = eccdb::TechRoutingLayerId{layer.entity()};
  if (!layer || !technology.routingLayerStorage().contains(routing)) {
    fail(std::string(context) + ": '" + std::string(name) + "' is not a routing layer");
  }
  return routing;
}

[[nodiscard]] eccdb::DesignRouteGuide parseGuide(const boost::json::value& value, const eccdb::TechStore& technology,
                                                  std::string_view context)
{
  const auto& guide = jsonObject(value, context);
  if (guide.size() != 4u) fail(std::string(context) + " must have exactly box, layer, via_layer, and flags");
  const auto& box = jsonArray(jsonMember(guide, "box", context), std::string(context) + ".box");
  if (box.size() != 4u) fail(std::string(context) + ".box must have exactly four coordinates");
  const eccdb::Rect rectangle{.ll_x = jsonInt32(box[0], std::string(context) + ".box[0]"),
                              .ll_y = jsonInt32(box[1], std::string(context) + ".box[1]"),
                              .ur_x = jsonInt32(box[2], std::string(context) + ".box[2]"),
                              .ur_y = jsonInt32(box[3], std::string(context) + ".box[3]")};
  if (!rectangle.isValid() || !rectangle.hasArea()) fail(std::string(context) + ".box is not a positive-area rectangle");

  const std::string layer_name = jsonString(jsonMember(guide, "layer", context), std::string(context) + ".layer");
  const auto layer = requireRoutingLayer(technology, layer_name, std::string(context) + ".layer");

  const auto& via_value = jsonMember(guide, "via_layer", context);
  eccdb::TechRoutingLayerId via_layer;
  if (!via_value.is_null()) {
    const std::string via_name = jsonString(via_value, std::string(context) + ".via_layer");
    via_layer = requireRoutingLayer(technology, via_name, std::string(context) + ".via_layer");
  }

  const uint64_t flags = jsonUnsigned(jsonMember(guide, "flags", context), std::string(context) + ".flags");
  if (flags > std::numeric_limits<uint32_t>::max()) fail(std::string(context) + ".flags is outside uint32 range");
  return {.box = rectangle, .layer = layer, .via_layer = via_layer, .flags = static_cast<uint32_t>(flags)};
}

[[nodiscard]] std::vector<GuidePayload> loadGuides(const std::filesystem::path& path, const eccdb::TechStore& technology,
                                                    const std::vector<NetProbe>& nets)
{
  std::unordered_map<std::string, std::size_t> indices;
  indices.reserve(nets.size());
  std::vector<GuidePayload> result;
  result.reserve(nets.size());
  for (std::size_t index = 0; index < nets.size(); ++index) {
    const auto [unused, inserted] = indices.emplace(std::string(nets[index].name), index);
    require(inserted, "regular-net cache has duplicate names");
    result.push_back({.net = nets[index].id, .name = nets[index].name});
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) fail("cannot open guides JSONL: " + path.string());
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty()) fail(lineContext(line_number) + " is empty");
    boost::json::value document;
    try {
      document = boost::json::parse(line);
    } catch (const std::exception& error) {
      fail(lineContext(line_number) + " is invalid JSON: " + error.what());
    }
    const auto context = lineContext(line_number);
    const auto& object = jsonObject(document, context);
    if (object.size() != 2u) fail(context + " must have exactly net and guides");
    const std::string net_name = jsonString(jsonMember(object, "net", context), context + ".net");
    const auto found = indices.find(net_name);
    if (found == indices.end()) fail(context + ": unknown regular net '" + net_name + "'");
    auto& target = result[found->second];
    if (target.appeared_in_input) fail(context + ": duplicate regular net '" + net_name + "'");
    target.appeared_in_input = true;

    const auto& guides = jsonArray(jsonMember(object, "guides", context), context + ".guides");
    target.values.reserve(guides.size());
    for (std::size_t guide_index = 0; guide_index < guides.size(); ++guide_index) {
      target.values.push_back(parseGuide(guides[guide_index], technology,
                                         context + ".guides[" + std::to_string(guide_index) + "]"));
    }
  }
  if (input.bad()) fail("failed while reading guides JSONL: " + path.string());
  for (const auto& payload : result) {
    if (!payload.appeared_in_input) fail("guides JSONL is missing regular net '" + std::string(payload.name) + "'");
  }
  return result;
}

[[nodiscard]] bool sameGuide(const eccdb::DesignRouteGuide& lhs, const eccdb::DesignRouteGuide& rhs)
{
  return lhs.box == rhs.box && lhs.layer == rhs.layer && lhs.via_layer == rhs.via_layer && lhs.flags == rhs.flags;
}

void attachGuides(eccdb::RoutingContextService& service, const std::vector<GuidePayload>& payloads)
{
  for (const auto& payload : payloads) {
    service.replaceGuides(payload.net, payload.values);
  }
}

[[nodiscard]] uint64_t guideCount(const std::vector<GuidePayload>& payloads)
{
  uint64_t result = 0;
  for (const auto& payload : payloads) result += static_cast<uint64_t>(payload.values.size());
  return result;
}

void validateGuides(const eccdb::RoutingContextService& service, const std::vector<GuidePayload>& payloads, uint64_t expected_count)
{
  uint64_t actual_count = 0;
  for (const auto& payload : payloads) {
    const auto actual = service.guides(payload.net);
    require(actual.size() == payload.values.size(), "guide count mismatch for net '" + std::string(payload.name) + "'");
    require(service.hasGuides(payload.net) == !payload.values.empty(),
            "guide component presence mismatch for net '" + std::string(payload.name) + "'");
    for (std::size_t index = 0; index < actual.size(); ++index) {
      require(sameGuide(actual[index], payload.values[index]), "guide value mismatch for net '" + std::string(payload.name) + "'");
    }
    actual_count += static_cast<uint64_t>(actual.size());
  }
  require(actual_count == expected_count, "total guide count mismatch");
}

template <typename Sink>
void forEachInputShape(const eccdb::DesignRouteShape& shape, const eccdb::DesignRouteShapeAttributes* attributes,
                       std::string_view context, Sink&& sink)
{
  constexpr uint32_t kProjectionFlags = eccdb::DesignWirePathFlag::kTaper;
  if ((shape.flags & ~kProjectionFlags) != 0u) {
    fail(std::string(context) + ": route snapshot cannot represent Shape metadata");
  }
  const bool tapered = (shape.flags & eccdb::DesignWirePathFlag::kTaper) != 0u;
  std::visit(
      [&](const auto& geometry) {
        using Geometry = std::remove_cvref_t<decltype(geometry)>;
        if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteSegment>) {
          const auto extension = [](eccdb::DesignWirePoint point) -> std::optional<int32_t> {
            return (point.flags & eccdb::DesignWirePointFlag::kHasExtension) != 0u
                       ? std::optional<int32_t>{point.extension}
                       : std::optional<int32_t>{};
          };
          sink(eccdb::RouteShape{eccdb::RouteSegment{.layer = shape.layer,
                                                      .begin = geometry.begin.position,
                                                      .end = geometry.end.position,
                                                      .width = std::nullopt,
                                                      .begin_extension = extension(geometry.begin),
                                                      .end_extension = extension(geometry.end),
                                                      .tapered = tapered}});
        } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteVia>) {
          if (geometry.via.flags != 0u || geometry.via.top_mask != 0u || geometry.via.cut_mask != 0u
              || geometry.via.bottom_mask != 0u || geometry.via.rows != 1u || geometry.via.columns != 1u
              || geometry.via.step_x != 0 || geometry.via.step_y != 0) {
            fail(std::string(context) + ": route snapshot cannot represent VIA metadata");
          }
          const eccdb::RouteViaDefinition definition = geometry.via.tech_via
                                                           ? eccdb::RouteViaDefinition{geometry.via.tech_via}
                                                           : eccdb::RouteViaDefinition{geometry.via.design_via};
          sink(eccdb::RouteShape{eccdb::RouteVia{.definition = definition,
                                                  .origin = geometry.origin.position,
                                                  .orientation = geometry.via.orientation,
                                                  .tapered = tapered}});
        } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRoutePatch>) {
          sink(eccdb::RouteShape{eccdb::RoutePatch{.layer = shape.layer,
                                                    .rectangle = geometry.rectangle,
                                                    .tapered = tapered}});
        } else {
          fail(std::string(context) + ": route snapshot cannot represent a point-only Shape");
        }
      },
      shape.geometry);
}

void requireSameRouteShape(const eccdb::RouteShape& actual, const eccdb::RouteShape& expected, std::string_view context)
{
  require(actual.index() == expected.index(), std::string(context) + ": route primitive kind mismatch");
  std::visit(
      [&](const auto& actual_value, const auto& expected_value) {
        using Actual = std::remove_cvref_t<decltype(actual_value)>;
        using Expected = std::remove_cvref_t<decltype(expected_value)>;
        if constexpr (!std::is_same_v<Actual, Expected>) {
          fail(std::string(context) + ": route primitive variant mismatch");
        } else if constexpr (std::is_same_v<Actual, eccdb::RouteSegment>) {
          require(actual_value.layer == expected_value.layer && actual_value.begin == expected_value.begin
                      && actual_value.end == expected_value.end && actual_value.width == expected_value.width
                      && actual_value.begin_extension == expected_value.begin_extension
                      && actual_value.end_extension == expected_value.end_extension,
                  std::string(context) + ": route segment mismatch");
        } else if constexpr (std::is_same_v<Actual, eccdb::RouteVia>) {
          require(actual_value.definition == expected_value.definition && actual_value.origin == expected_value.origin
                      && actual_value.orientation == expected_value.orientation,
                  std::string(context) + ": route VIA mismatch");
        } else {
          require(actual_value.layer == expected_value.layer && actual_value.rectangle == expected_value.rectangle,
                  std::string(context) + ": route patch mismatch");
        }
      },
      actual, expected);
}

[[nodiscard]] RouteSummary validateRoutes(const eccdb::DesignStore& design, const eccdb::TechStore& technology,
                                          const eccdb::NetRouteService& service, const std::vector<NetProbe>& nets,
                                          std::vector<NetProbe>& routed_nets)
{
  const auto& routing = design.routingStorage();
  RouteSummary summary;
  routed_nets.clear();
  for (const auto& net : nets) {
    const auto input_wires = routing.wireIds(net.id);
    if (input_wires.empty()) continue;
    routed_nets.push_back(net);
    ++summary.routed_nets;
    const auto snapshot = service.snapshot(net.id);
    require(snapshot.net == net.id, "route snapshot net ID mismatch for '" + std::string(net.name) + "'");
    require(snapshot.wires.size() == input_wires.size(), "route snapshot wire count mismatch for '" + std::string(net.name) + "'");
    for (std::size_t wire_index = 0; wire_index < input_wires.size(); ++wire_index) {
      const auto wire_id = input_wires[wire_index];
      const auto& input_wire = routing.wire(wire_id);
      const auto& actual_wire = snapshot.wires[wire_index];
      require(actual_wire.wire == wire_id && actual_wire.status == input_wire.status && actual_wire.shield_net == input_wire.shield_net,
              "route snapshot wire metadata mismatch for '" + std::string(net.name) + "'");
      std::size_t actual_shape_index = 0;
      routing.forEachShape(
          wire_id, [&](eccdb::DesignRouteShapeId, const eccdb::DesignRouteShape& shape,
                       const eccdb::DesignRouteShapeAttributes* attributes) {
            const std::string path_context = "route snapshot for net '" + std::string(net.name) + "'";
            forEachInputShape(shape, attributes, path_context, [&](eccdb::RouteShape expected) {
              require(actual_shape_index < actual_wire.shapes.size(), path_context + ": missing route primitive");
              requireSameRouteShape(actual_wire.shapes[actual_shape_index], expected, path_context);
              ++actual_shape_index;
              ++summary.shapes;
              if (std::holds_alternative<eccdb::RouteVia>(expected)) ++summary.vias;
            });
          });
      require(actual_shape_index == actual_wire.shapes.size(), "route snapshot has an extra primitive for '" + std::string(net.name) + "'");
    }
  }
  return summary;
}

[[nodiscard]] Counts buildAndValidateCache(const DbOnlyFixture& fixture, ImportedDesign& imported,
                                           eccdb::PhysicalDesignService& physical, eccdb::RoutingContextService& context,
                                           eccdb::NetRouteService& routes, BenchmarkCache& cache,
                                           const std::filesystem::path& guide_path)
{
  const auto& netlist = imported.design.netlistStorage();
  Counts counts;
  counts.instances = netlist.instanceCount();
  counts.instance_pins = netlist.instancePinCount();
  counts.io_pins = netlist.ioPinCount();
  require(counts.instances == fixture.instances.size(), "imported instance count does not match fixture");

  cache.nets.clear();
  cache.nets.reserve(fixture.nets.size());
  std::unordered_set<uint64_t> seen_nets;
  seen_nets.reserve(fixture.nets.size());
  for (const auto& name : fixture.nets) {
    const auto id = physical.findRegularNet(name);
    require(static_cast<bool>(id), "missing imported regular net '" + name + "'");
    require(netlist.net(id).name == name, "regular net name mismatch");
    require(seen_nets.insert(static_cast<uint64_t>(id.packed())).second, "duplicate fixture regular-net ID");
    cache.nets.push_back({.id = id, .name = name});
  }
  uint64_t enumerated_regular_nets = 0;
  physical.forEachRegularNet([&](eccdb::DesignNetId id, const eccdb::DesignNet& net) {
    ++enumerated_regular_nets;
    require(!net.name.empty(), "imported regular net has empty name");
    require(physical.findRegularNet(net.name) == id, "regular-net name index mismatch");
  });
  counts.regular_nets = enumerated_regular_nets;
  require(counts.regular_nets == fixture.nets.size(), "imported regular-net count does not match fixture");
  require(cache.nets.size() == counts.regular_nets, "fixture regular-net cache count mismatch");

  cache.instances.clear();
  cache.instances.reserve(fixture.instances.size());
  cache.instance_names.clear();
  cache.instance_names.reserve(fixture.instances.size());
  cache.pins.clear();
  cache.instance_pin_offsets.clear();
  cache.instance_pin_offsets.reserve(fixture.instances.size() + 1u);
  cache.instance_pin_offsets.push_back(0u);
  for (const auto& source_instance : fixture.instances) {
    const auto instance = physical.findInstance(source_instance.name);
    require(static_cast<bool>(instance), "missing imported instance '" + source_instance.name + "'");
    const auto& stored_instance = netlist.instance(instance);
    const auto& source_master = fixture.masters[source_instance.master_index];
    const auto master = physical.findMaster(source_master.name);
    require(static_cast<bool>(master) && stored_instance.master == master, "instance master mismatch for '" + source_instance.name + "'");
    cache.instances.push_back(instance);
    cache.instance_names.push_back(source_instance.name);
    for (const auto& term_name : source_master.terms) {
      const auto master_term = physical.findMasterTerm(master, term_name);
      require(static_cast<bool>(master_term), "missing imported master term '" + term_name + "'");
      const auto pin = physical.findInstancePin(instance, term_name);
      require(static_cast<bool>(pin), "missing imported instance pin '" + source_instance.name + "/" + term_name + "'");
      const auto& stored_pin = netlist.instancePin(pin);
      require(stored_pin.instance == instance && stored_pin.master_term == master_term,
              "imported instance pin relationship mismatch for '" + source_instance.name + "/" + term_name + "'");
      cache.pins.push_back({.instance = instance, .expected = pin, .instance_name = source_instance.name, .term_name = term_name});
    }
    cache.instance_pin_offsets.push_back(cache.pins.size());
  }
  require(cache.instances.size() == counts.instances, "instance query cache count mismatch");
  require(cache.instance_names.size() == counts.instances, "instance-name query cache count mismatch");
  require(cache.pins.size() == counts.instance_pins, "instance-pin query cache count mismatch");

  uint64_t connected_iterms = 0;
  netlist.forEachInstancePin([&](eccdb::DesignInstancePinId, const eccdb::DesignInstancePin& pin) {
    if (pin.net || pin.special_net) ++connected_iterms;
  });
  counts.connected_iterms = connected_iterms;
  require(counts.connected_iterms == fixture.connections.size(), "connected instance-pin count does not match fixture");
  for (const auto& connection : fixture.connections) {
    require(connection.instance_index + 1u < cache.instance_pin_offsets.size(), "fixture connection instance index is outside cache");
    const auto pin_index = cache.instance_pin_offsets[connection.instance_index] + connection.term_index;
    require(pin_index < cache.instance_pin_offsets[connection.instance_index + 1u], "fixture connection term index is outside cache");
    const auto& pin_probe = cache.pins[pin_index];
    const auto& stored_pin = netlist.instancePin(pin_probe.expected);
    require(stored_pin.net == cache.nets[connection.net_index].id && !stored_pin.special_net,
            "fixture instance-pin connection differs after DEF import");
  }

  std::unordered_set<uint64_t> seen_io;
  seen_io.reserve(counts.io_pins);
  netlist.forEachIoPin([&](eccdb::DesignIoPinId id, const eccdb::DesignIoPin& pin) {
    require(!pin.name.empty(), "imported IO pin has empty name");
    require(physical.findIoPin(pin.name) == id, "IO pin name index mismatch");
    require(seen_io.insert(static_cast<uint64_t>(id.packed())).second, "duplicate imported IO pin ID");
  });
  require(seen_io.size() == counts.io_pins, "IO pin iteration count mismatch");

  cache.guide_payloads = loadGuides(guide_path, imported.technology, cache.nets);
  attachGuides(context, cache.guide_payloads);
  counts.guides = guideCount(cache.guide_payloads);
  validateGuides(context, cache.guide_payloads, counts.guides);

  cache.net_pin_entries = 0;
  physical.forEachRegularNet([&](eccdb::DesignNetId net, const eccdb::DesignNet&) {
    physical.forEachNetPin(net,
                           {.instance_pin = [&](eccdb::DesignInstancePinId pin, const eccdb::DesignInstancePin& value,
                                                const eccdb::DesignInstance&, const eccdb::LibraryMasterTerm&) {
                              require(value.net == net && !value.special_net, "regular net instance-pin relationship mismatch");
                              require(static_cast<bool>(pin), "regular net has invalid instance-pin ID");
                              ++cache.net_pin_entries;
                            },
                            .io_pin = [&](eccdb::DesignIoPinId pin, const eccdb::DesignIoPin& value) {
                              require(value.net == net && !value.special_net, "regular net IO-pin relationship mismatch");
                              require(static_cast<bool>(pin), "regular net has invalid IO-pin ID");
                              ++cache.net_pin_entries;
                            }});
  });
  require(cache.net_pin_entries >= counts.connected_iterms, "regular net-pin scan lost connected instance pins");

  const auto route_summary = validateRoutes(imported.design, imported.technology, routes, cache.nets, cache.routed_nets);
  counts.routed_nets = route_summary.routed_nets;
  counts.route_shapes = route_summary.shapes;
  counts.route_vias = route_summary.vias;
  require(counts.routed_nets == cache.routed_nets.size(), "routed-net cache count mismatch");

  cache.query_indices = {.instances = shuffledIndices(cache.instances.size(), UINT64_C(0x11)),
                         .nets = shuffledIndices(cache.nets.size(), UINT64_C(0x22)),
                         .pins = shuffledIndices(cache.pins.size(), UINT64_C(0x33))};
  return counts;
}

void validateReadQueries(const eccdb::PhysicalDesignService& physical, const eccdb::RoutingContextService& context,
                         const eccdb::NetRouteService& routes, const BenchmarkCache& cache, const Counts& counts)
{
  uint64_t regular_count = 0;
  std::unordered_set<uint64_t> regular_ids;
  regular_ids.reserve(cache.nets.size());
  physical.forEachRegularNet([&](eccdb::DesignNetId id, const eccdb::DesignNet& net) {
    ++regular_count;
    require(physical.findRegularNet(net.name) == id, "read validation regular-net lookup mismatch");
    regular_ids.insert(static_cast<uint64_t>(id.packed()));
  });
  require(regular_count == counts.regular_nets && regular_ids.size() == cache.nets.size(),
          "read validation regular-net scan count mismatch");
  for (const auto& net : cache.nets) require(regular_ids.contains(static_cast<uint64_t>(net.id.packed())), "fixture net absent from service scan");

  for (const auto instance : cache.instances) {
    const auto& value = physical.design().netlistStorage().instance(instance);
    require(physical.findInstance(value.name) == instance, "read validation instance lookup mismatch");
  }
  for (const auto& net : cache.nets) require(physical.findRegularNet(net.name) == net.id, "read validation net lookup mismatch");
  for (const auto& pin : cache.pins) {
    require(physical.findInstancePin(pin.instance, pin.term_name) == pin.expected,
            "read validation instance-pin lookup mismatch for '" + std::string(pin.instance_name) + "/" + std::string(pin.term_name) + "'");
  }

  uint64_t net_pin_count = 0;
  physical.forEachRegularNet([&](eccdb::DesignNetId net, const eccdb::DesignNet&) {
    physical.forEachNetPin(net,
                           {.instance_pin = [&](eccdb::DesignInstancePinId, const eccdb::DesignInstancePin& pin,
                                                const eccdb::DesignInstance&, const eccdb::LibraryMasterTerm&) {
                              require(pin.net == net && !pin.special_net, "read validation regular ITerm connection mismatch");
                              ++net_pin_count;
                            },
                            .io_pin = [&](eccdb::DesignIoPinId, const eccdb::DesignIoPin& pin) {
                              require(pin.net == net && !pin.special_net, "read validation regular IO pin connection mismatch");
                              ++net_pin_count;
                            }});
  });
  require(net_pin_count == cache.net_pin_entries, "read validation net-pin count mismatch");

  validateGuides(context, cache.guide_payloads, counts.guides);
  std::vector<NetProbe> route_validation_cache;
  const auto routes_summary = validateRoutes(physical.design(), physical.tech(), routes, cache.nets, route_validation_cache);
  require(routes_summary.routed_nets == counts.routed_nets && routes_summary.shapes == counts.route_shapes
              && routes_summary.vias == counts.route_vias,
          "read validation route primitive count mismatch");
  require(route_validation_cache.size() == cache.routed_nets.size(), "read validation routed-net cache count mismatch");
  for (std::size_t index = 0; index < route_validation_cache.size(); ++index) {
    require(route_validation_cache[index].id == cache.routed_nets[index].id,
            "read validation routed-net order mismatch");
  }
}

[[nodiscard]] MasterTermPointerTable buildMasterTermPointerTable(const eccdb::PhysicalDesignService& physical)
{
  MasterTermPointerTable table;
  const auto begin = Clock::now();
  const auto& registry = physical.library().libraryRegistry().registry();
  const auto view = registry.view<const eccdb::LibraryMasterTerm>();

  std::size_t max_entity = 0;
  bool has_terms = false;
  for (const auto entity : view) {
    has_terms = true;
    max_entity = std::max(max_entity, static_cast<std::size_t>(entt::to_entity(entity)));
  }
  if (has_terms) table.slots.resize(max_entity + 1u);

  view.each([&table](const auto entity, const eccdb::LibraryMasterTerm& term) {
    auto& slot = table.slots[static_cast<std::size_t>(entt::to_entity(entity))];
    slot.packed_id = static_cast<uint32_t>(entt::to_integral(entity));
    slot.term = &term;
    ++table.terms;
  });
  table.build_ns = elapsedNs(begin);
  table.payload_bytes = static_cast<uint64_t>(table.slots.size()) * sizeof(MasterTermPointerSlot);
  return table;
}

void validateMasterTermPointerTable(const eccdb::PhysicalDesignService& physical, const MasterTermPointerTable& table)
{
  const auto& registry = physical.library().libraryRegistry().registry();
  uint64_t populated_slots = 0;
  for (std::size_t index = 0; index < table.slots.size(); ++index) {
    const auto& slot = table.slots[index];
    if (slot.term == nullptr) continue;

    const auto entity = static_cast<eccdb::LibraryEntity>(slot.packed_id);
    require(static_cast<std::size_t>(entt::to_entity(entity)) == index, "master-term cache slot index mismatch");
    require(registry.valid(entity) && registry.all_of<eccdb::LibraryMasterTerm>(entity),
            "master-term cache refers to a missing component");
    require(&registry.get<const eccdb::LibraryMasterTerm>(entity) == slot.term, "master-term cache pointer mismatch");
    ++populated_slots;
  }
  require(populated_slots == table.terms, "master-term cache populated-slot count mismatch");

  uint64_t registry_terms = 0;
  const auto view = registry.view<const eccdb::LibraryMasterTerm>();
  for (const auto entity : view) {
    const auto index = static_cast<std::size_t>(entt::to_entity(entity));
    require(index < table.slots.size(), "master-term cache is missing an entity slot");
    const auto& slot = table.slots[index];
    require(slot.packed_id == static_cast<uint32_t>(entt::to_integral(entity)), "master-term cache packed ID mismatch");
    require(slot.term == &registry.get<const eccdb::LibraryMasterTerm>(entity), "master-term cache entity pointer mismatch");
    ++registry_terms;
  }
  require(registry_terms == table.terms, "master-term cache term count mismatch");

  physical.design().netlistStorage().forEachInstancePin([&](eccdb::DesignInstancePinId, const eccdb::DesignInstancePin& pin) {
    const auto index = static_cast<std::size_t>(entt::to_entity(pin.master_term.entity()));
    require(index < table.slots.size(), "instance pin master-term cache slot is absent");
    const auto& slot = table.slots[index];
    require(slot.packed_id == static_cast<uint32_t>(pin.master_term.packed()),
            "instance pin master-term cache generation mismatch");
    require(slot.term == &registry.get<const eccdb::LibraryMasterTerm>(pin.master_term.entity()),
            "instance pin master-term cache pointer mismatch");
  });
}

[[nodiscard]] BenchInstancePinLookupTable buildBenchInstancePinLookupTable(const eccdb::PhysicalDesignService& physical,
                                                                             const BenchmarkCache& cache)
{
  BenchInstancePinLookupTable table;
  const auto begin = Clock::now();
  const auto& design_registry = physical.design().designRegistry().registry();

  std::size_t max_entity = 0;
  bool has_instances = false;
  for (const auto instance : cache.instances) {
    has_instances = true;
    max_entity = std::max(max_entity, static_cast<std::size_t>(entt::to_entity(instance.entity())));
  }
  if (has_instances) table.slots.resize(max_entity + 1u);

  for (const auto instance_id : cache.instances) {
    const auto index = static_cast<std::size_t>(entt::to_entity(instance_id.entity()));
    auto& slot = table.slots[index];
    slot.terms = &design_registry.get<const eccdb::DesignInstanceTerms>(instance_id.entity());
    ++table.instances;
  }
  table.build_ns = elapsedNs(begin);
  table.payload_bytes = static_cast<uint64_t>(table.slots.size()) * sizeof(BenchInstancePinLookupSlot);
  table.probes = cache.pins.size();
  return table;
}

void validateBenchInstancePinLookupTable(const eccdb::PhysicalDesignService& physical, const BenchmarkCache& cache,
                                         const BenchInstancePinLookupTable& table)
{
  const auto& design_registry = physical.design().designRegistry().registry();
  require(table.instances == cache.instances.size(), "shadow instance-pin lookup instance count mismatch");
  require(table.probes == cache.pins.size(), "shadow instance-pin lookup probe count mismatch");

  uint64_t populated_slots = 0;
  for (const auto instance_id : cache.instances) {
    const auto index = static_cast<std::size_t>(entt::to_entity(instance_id.entity()));
    require(index < table.slots.size(), "shadow instance-pin lookup table is missing an instance slot");
    const auto& slot = table.slots[index];
    require(slot.terms == &design_registry.get<const eccdb::DesignInstanceTerms>(instance_id.entity()),
            "shadow instance-pin lookup terms pointer mismatch");
    ++populated_slots;
  }
  require(populated_slots == table.instances, "shadow instance-pin lookup populated-slot count mismatch");

  for (const auto& probe : cache.pins) {
    const auto& slot = table.get(probe.instance);
    require(slot.terms != nullptr, "shadow instance-pin lookup has a null component pointer");
    const auto found = slot.terms->records.find(std::string{probe.term_name});
    require(found != slot.terms->records.end(), "shadow instance-pin lookup is missing a master-term name");
    require(found->second.id == probe.expected,
            "shadow instance-pin lookup result differs from the service result");
  }
}

[[nodiscard]] BenchCachedPinTable buildBenchCachedPinTable(const eccdb::PhysicalDesignService& physical)
{
  BenchCachedPinTable table;
  const auto begin = Clock::now();
  const auto& netlist = physical.design().netlistStorage();
  const auto& library_registry = physical.library().libraryRegistry().registry();

  netlist.forEachInstancePin([&](eccdb::DesignInstancePinId id, const eccdb::DesignInstancePin& pin) {
    const auto index = static_cast<std::size_t>(id.packed());
    if (index >= table.slots.size()) {
      table.slots.resize(index + 1u);
    }
    auto& cached = table.slots[index];
    cached.pin = &pin;
    cached.term = &library_registry.get<const eccdb::LibraryMasterTerm>(pin.master_term.entity());
    ++table.pins;
  });
  table.build_ns = elapsedNs(begin);
  table.payload_bytes = static_cast<uint64_t>(table.slots.size()) * sizeof(BenchCachedPin);
  return table;
}

void validateBenchCachedPinTable(const eccdb::PhysicalDesignService& physical, const BenchCachedPinTable& table)
{
  const auto& netlist = physical.design().netlistStorage();
  const auto& library_registry = physical.library().libraryRegistry().registry();

  uint64_t source_pins = 0;
  netlist.forEachInstancePin([&](eccdb::DesignInstancePinId id, const eccdb::DesignInstancePin& original) {
    const auto index = static_cast<std::size_t>(id.packed());
    require(index < table.slots.size(), "shadow pin table is missing a pin slot");
    const auto& cached = table.slots[index];
    require(cached.pin == &original && cached.term != nullptr, "shadow pin table record pointer mismatch");
    require(cached.term == &library_registry.get<const eccdb::LibraryMasterTerm>(original.master_term.entity()),
            "shadow pin term pointer mismatch");
    ++source_pins;
  });
  require(source_pins == table.pins, "shadow pin table pin count mismatch");

  uint64_t populated_slots = 0;
  for (const auto& cached : table.slots) {
    if (cached.term != nullptr) ++populated_slots;
  }
  require(populated_slots == table.pins, "shadow pin table populated-slot count mismatch");
}

[[nodiscard]] uint64_t scanRegularNets(const eccdb::PhysicalDesignService& physical)
{
  uint64_t checksum = UINT64_C(0x1);
  physical.forEachRegularNet([&](eccdb::DesignNetId id, const eccdb::DesignNet& net) {
    checksum = foldChecksum(checksum, static_cast<uint64_t>(id.packed()) ^ hashText(net.name));
  });
  return checksum;
}

[[nodiscard]] uint64_t findInstances(const eccdb::PhysicalDesignService& physical, const BenchmarkCache& cache)
{
  uint64_t checksum = UINT64_C(0x2);
  for (const auto index : cache.query_indices.instances) {
    const auto id = physical.findInstance(cache.instance_names[index]);
    checksum = foldChecksum(checksum, static_cast<uint64_t>(id.packed()));
  }
  return checksum;
}

[[nodiscard]] uint64_t findNets(const eccdb::PhysicalDesignService& physical, const BenchmarkCache& cache)
{
  uint64_t checksum = UINT64_C(0x3);
  for (const auto index : cache.query_indices.nets) {
    const auto id = physical.findRegularNet(cache.nets[index].name);
    checksum = foldChecksum(checksum, static_cast<uint64_t>(id.packed()));
  }
  return checksum;
}

[[nodiscard]] uint64_t findPins(const eccdb::PhysicalDesignService& physical, const BenchmarkCache& cache)
{
  uint64_t checksum = UINT64_C(0x4);
  for (const auto index : cache.query_indices.pins) {
    const auto& probe = cache.pins[index];
    const auto id = physical.findInstancePin(probe.instance, probe.term_name);
    checksum = foldChecksum(checksum, static_cast<uint64_t>(id.packed()));
  }
  return checksum;
}

[[nodiscard]] uint64_t findPinsShadow(const BenchInstancePinLookupTable& table, const BenchmarkCache& cache)
{
  uint64_t checksum = UINT64_C(0x4);
  for (const auto index : cache.query_indices.pins) {
    const auto& probe = cache.pins[index];
    const auto& slot = table.get(probe.instance);
    const auto found = slot.terms->records.find(std::string{probe.term_name});
    const auto id = found == slot.terms->records.end() ? eccdb::DesignInstancePinId{} : found->second.id;
    checksum = foldChecksum(checksum, static_cast<uint64_t>(id.packed()));
  }
  return checksum;
}

template <typename ResolveTerm>
[[nodiscard]] NetPinScanResult scanNetPinsWithTermLookup(const eccdb::PhysicalDesignService& physical, ResolveTerm&& resolve_term)
{
  const auto& design_registry = physical.design().designRegistry().registry();
  const auto& netlist = physical.design().netlistStorage();
  NetPinScanResult result;
  physical.forEachRegularNet([&](eccdb::DesignNetId net, const eccdb::DesignNet&) {
    // Keep the same std::function callback boundary as PhysicalDesignService.
    // The two benchmark paths below differ only in resolve_term().
    eccdb::PhysicalNetPinVisitor visitor{
        .instance_pin = [&](eccdb::DesignInstancePinId pin, const eccdb::DesignInstancePin&, const eccdb::DesignInstance& instance,
                            const eccdb::LibraryMasterTerm& term) {
          result.checksum = foldChecksum(result.checksum, static_cast<uint64_t>(net.packed()) ^ static_cast<uint64_t>(pin.packed())
                                                              ^ hashText(instance.name) ^ hashText(term.name));
          ++result.instance_pins;
        },
        .io_pin = [&](eccdb::DesignIoPinId pin, const eccdb::DesignIoPin& value) {
          result.checksum = foldChecksum(result.checksum, static_cast<uint64_t>(net.packed()) ^ static_cast<uint64_t>(pin.packed())
                                                              ^ hashText(value.name));
          ++result.io_pins;
        }};

    if (visitor.instance_pin) {
      if (const auto* connections = design_registry.try_get<const eccdb::DesignNetInstancePins>(net.entity()); connections != nullptr) {
        for (const auto pin_id : connections->values) {
          const auto& pin = netlist.instancePin(pin_id);
          const auto& instance = design_registry.get<const eccdb::DesignInstance>(pin.instance.entity());
          visitor.instance_pin(pin_id, pin, instance, resolve_term(pin.master_term));
        }
      }
    }
    if (visitor.io_pin) {
      if (const auto* connections = design_registry.try_get<const eccdb::DesignNetIoPins>(net.entity()); connections != nullptr) {
        for (const auto pin_id : connections->values) {
          visitor.io_pin(pin_id, design_registry.get<const eccdb::DesignIoPin>(pin_id.entity()));
        }
      }
    }
  });
  return result;
}

// Preserve the historical service-facing benchmark path.  The direct paths
// below exist only to isolate the cost of resolving LibraryMasterTerm.
[[nodiscard]] uint64_t scanNetPins(const eccdb::PhysicalDesignService& physical)
{
  uint64_t checksum = UINT64_C(0x5);
  physical.forEachRegularNet([&](eccdb::DesignNetId net, const eccdb::DesignNet&) {
    physical.forEachNetPin(net,
                           {.instance_pin = [&](eccdb::DesignInstancePinId pin, const eccdb::DesignInstancePin&,
                                                const eccdb::DesignInstance& instance, const eccdb::LibraryMasterTerm& term) {
                              checksum = foldChecksum(checksum, static_cast<uint64_t>(net.packed())
                                                                     ^ static_cast<uint64_t>(pin.packed()) ^ hashText(instance.name)
                                                                     ^ hashText(term.name));
                            },
                            .io_pin = [&](eccdb::DesignIoPinId pin, const eccdb::DesignIoPin& value) {
                              checksum = foldChecksum(checksum, static_cast<uint64_t>(net.packed())
                                                                     ^ static_cast<uint64_t>(pin.packed()) ^ hashText(value.name));
                            }});
  });
  return checksum;
}

[[nodiscard]] NetPinScanResult scanNetPinsDirectRegistry(const eccdb::PhysicalDesignService& physical)
{
  const auto& library_registry = physical.library().libraryRegistry().registry();
  return scanNetPinsWithTermLookup(physical, [&library_registry](eccdb::LibraryMasterTermId id) -> const eccdb::LibraryMasterTerm& {
    return library_registry.get<const eccdb::LibraryMasterTerm>(id.entity());
  });
}

[[nodiscard]] NetPinScanResult scanNetPinsCached(const eccdb::PhysicalDesignService& physical, const MasterTermPointerTable& table)
{
  return scanNetPinsWithTermLookup(physical,
                                   [&table](eccdb::LibraryMasterTermId id) -> const eccdb::LibraryMasterTerm& { return table.get(id); });
}

template <typename ResolveTerm>
[[nodiscard]] NetPinScanResult scanNetPinsWithShadowTermLookup(const eccdb::PhysicalDesignService& physical,
                                                                 const BenchCachedPinTable& shadow_pins,
                                                                 ResolveTerm&& resolve_term)
{
  const auto& design_registry = physical.design().designRegistry().registry();
  NetPinScanResult result;
  physical.forEachRegularNet([&](eccdb::DesignNetId net, const eccdb::DesignNet&) {
    // Match the direct benchmark's callback and IO path.  Shadow variants
    // differ only in resolve_term() for the same cached pin record.
    eccdb::PhysicalNetPinVisitor visitor{
        .instance_pin = [&](eccdb::DesignInstancePinId pin, const eccdb::DesignInstancePin&, const eccdb::DesignInstance& instance,
                            const eccdb::LibraryMasterTerm& term) {
          result.checksum = foldChecksum(result.checksum, static_cast<uint64_t>(net.packed()) ^ static_cast<uint64_t>(pin.packed())
                                                              ^ hashText(instance.name) ^ hashText(term.name));
          ++result.instance_pins;
        },
        .io_pin = [&](eccdb::DesignIoPinId pin, const eccdb::DesignIoPin& value) {
          result.checksum = foldChecksum(result.checksum, static_cast<uint64_t>(net.packed()) ^ static_cast<uint64_t>(pin.packed())
                                                              ^ hashText(value.name));
          ++result.io_pins;
        }};

    if (visitor.instance_pin) {
      if (const auto* connections = design_registry.try_get<const eccdb::DesignNetInstancePins>(net.entity()); connections != nullptr) {
        for (const auto pin_id : connections->values) {
          const auto& cached_pin = shadow_pins.get(pin_id);
          const auto& instance = design_registry.get<const eccdb::DesignInstance>(cached_pin.pin->instance.entity());
          visitor.instance_pin(pin_id, *cached_pin.pin, instance, resolve_term(cached_pin));
        }
      }
    }
    if (visitor.io_pin) {
      if (const auto* connections = design_registry.try_get<const eccdb::DesignNetIoPins>(net.entity()); connections != nullptr) {
        for (const auto pin_id : connections->values) {
          visitor.io_pin(pin_id, design_registry.get<const eccdb::DesignIoPin>(pin_id.entity()));
        }
      }
    }
  });
  return result;
}

[[nodiscard]] NetPinScanResult scanNetPinsShadowRegistry(const eccdb::PhysicalDesignService& physical,
                                                          const BenchCachedPinTable& shadow_pins)
{
  const auto& library_registry = physical.library().libraryRegistry().registry();
  return scanNetPinsWithShadowTermLookup(
      physical, shadow_pins, [&library_registry](const BenchCachedPin& pin) -> const eccdb::LibraryMasterTerm& {
        return library_registry.get<const eccdb::LibraryMasterTerm>(pin.pin->master_term.entity());
      });
}

[[nodiscard]] NetPinScanResult scanNetPinsShadowShared(const eccdb::PhysicalDesignService& physical,
                                                        const BenchCachedPinTable& shadow_pins,
                                                        const MasterTermPointerTable& master_term_table)
{
  return scanNetPinsWithShadowTermLookup(
      physical, shadow_pins,
      [&master_term_table](const BenchCachedPin& pin) -> const eccdb::LibraryMasterTerm& { return master_term_table.get(pin.pin->master_term); });
}

[[nodiscard]] NetPinScanResult scanNetPinsShadowInline(const eccdb::PhysicalDesignService& physical,
                                                        const BenchCachedPinTable& shadow_pins)
{
  return scanNetPinsWithShadowTermLookup(physical, shadow_pins,
                                         [](const BenchCachedPin& pin) -> const eccdb::LibraryMasterTerm& { return *pin.term; });
}

void validateCachedNetPinScan(const eccdb::PhysicalDesignService& physical, const MasterTermPointerTable& table,
                              uint64_t expected_net_pin_entries)
{
  const auto service_checksum = scanNetPins(physical);
  const auto baseline = scanNetPinsDirectRegistry(physical);
  const auto cached = scanNetPinsCached(physical, table);
  require(service_checksum == baseline.checksum && cached.checksum == baseline.checksum, "cached net-pin scan checksum mismatch");
  require(baseline.instance_pins + baseline.io_pins == expected_net_pin_entries,
          "direct registry net-pin scan count mismatch");
  require(cached.instance_pins == baseline.instance_pins && cached.io_pins == baseline.io_pins,
          "cached net-pin scan count mismatch");
  require(cached.instance_pins + cached.io_pins == expected_net_pin_entries, "cached net-pin total count mismatch");
}

void validateShadowNetPinScans(const eccdb::PhysicalDesignService& physical, const BenchCachedPinTable& shadow_pins,
                               const MasterTermPointerTable& master_term_table, uint64_t expected_net_pin_entries)
{
  const auto service_checksum = scanNetPins(physical);
  const auto registry = scanNetPinsShadowRegistry(physical, shadow_pins);
  const auto shared = scanNetPinsShadowShared(physical, shadow_pins, master_term_table);
  const auto inline_term = scanNetPinsShadowInline(physical, shadow_pins);
  require(registry.checksum == service_checksum && shared.checksum == registry.checksum && inline_term.checksum == registry.checksum,
          "shadow net-pin scan checksum mismatch");
  require(registry.instance_pins + registry.io_pins == expected_net_pin_entries,
          "shadow registry net-pin scan count mismatch");
  require(shared.instance_pins == registry.instance_pins && shared.io_pins == registry.io_pins,
          "shadow shared net-pin scan count mismatch");
  require(inline_term.instance_pins == registry.instance_pins && inline_term.io_pins == registry.io_pins,
          "shadow inline net-pin scan count mismatch");
}

[[nodiscard]] uint64_t scanGuides(const eccdb::RoutingContextService& context, const BenchmarkCache& cache)
{
  uint64_t checksum = UINT64_C(0x6);
  for (const auto& payload : cache.guide_payloads) {
    for (const auto& guide : context.guides(payload.net)) {
      uint64_t value = static_cast<uint64_t>(payload.net.packed()) ^ hashRect(guide.box);
      value = foldChecksum(value, static_cast<uint64_t>(guide.layer.packed()));
      value = foldChecksum(value, static_cast<uint64_t>(guide.via_layer.packed()));
      checksum = foldChecksum(checksum, value ^ guide.flags);
    }
  }
  return checksum;
}

[[nodiscard]] uint64_t decodeRoutes(const eccdb::NetRouteService& routes, const BenchmarkCache& cache)
{
  uint64_t checksum = UINT64_C(0x7);
  for (const auto& net : cache.routed_nets) {
    const auto snapshot = routes.snapshot(net.id);
    checksum = foldChecksum(checksum, static_cast<uint64_t>(snapshot.net.packed()));
    for (const auto& wire : snapshot.wires) {
      checksum = foldChecksum(checksum, static_cast<uint64_t>(wire.wire.packed()) ^ static_cast<uint64_t>(wire.status));
      for (const auto& shape : wire.shapes) {
        const uint64_t value = std::visit(
            [](const auto& primitive) -> uint64_t {
              using Primitive = std::remove_cvref_t<decltype(primitive)>;
              if constexpr (std::is_same_v<Primitive, eccdb::RouteSegment>) {
                uint64_t result = foldChecksum(static_cast<uint64_t>(primitive.layer.packed()), hashPoint(primitive.begin));
                result = foldChecksum(result, hashPoint(primitive.end));
                result = foldChecksum(result, primitive.begin_extension.value_or(0));
                return foldChecksum(result, primitive.end_extension.value_or(0));
              } else if constexpr (std::is_same_v<Primitive, eccdb::RouteVia>) {
                const uint64_t definition = std::visit([](const auto id) { return static_cast<uint64_t>(id.packed()); }, primitive.definition);
                return foldChecksum(definition, hashPoint(primitive.origin) ^ static_cast<uint64_t>(primitive.orientation));
              } else {
                return foldChecksum(static_cast<uint64_t>(primitive.layer.packed()), hashRect(primitive.rectangle));
              }
            },
            shape);
        checksum = foldChecksum(checksum, value);
      }
    }
  }
  return checksum;
}

[[nodiscard]] uint64_t replaceGuides(eccdb::RoutingContextService& context, const BenchmarkCache& cache)
{
  uint64_t checksum = UINT64_C(0x8);
  for (const auto& payload : cache.guide_payloads) {
    context.replaceGuides(payload.net, payload.values);
    checksum = foldChecksum(checksum, static_cast<uint64_t>(payload.net.packed()) ^ static_cast<uint64_t>(payload.values.size()));
  }
  return checksum;
}

[[nodiscard]] Run runOnce(eccdb::PhysicalDesignService& physical, eccdb::RoutingContextService& context,
                          eccdb::NetRouteService& routes, const BenchmarkCache& cache, const Counts& counts,
                          const MasterTermPointerTable& master_term_table,
                          const BenchInstancePinLookupTable& shadow_pin_lookup, const BenchCachedPinTable& shadow_pins,
                          bool run_experiments, bool shadow_find_first, bool direct_registry_net_pin_scan_first,
                          std::size_t shadow_scan_order, bool pins_only)
{
  Run result;
  if (pins_only) {
    result.timing.find_pins_ns = measure("find_pins", [&] { return findPins(physical, cache); }, result.checksum);
    result.timing.scan_net_pins_ns = measure("scan_net_pins", [&] { return scanNetPins(physical); }, result.checksum);
  } else {
    result.timing.scan_regular_nets_ns = measure("scan_regular_nets", [&] { return scanRegularNets(physical); }, result.checksum);
    result.timing.find_instances_ns = measure("find_instances", [&] { return findInstances(physical, cache); }, result.checksum);
    result.timing.find_nets_ns = measure("find_nets", [&] { return findNets(physical, cache); }, result.checksum);
    uint64_t find_pins_checksum = 0;
    uint64_t shadow_find_pins_checksum = 0;
    const auto measure_find_pins = [&] {
      result.timing.find_pins_ns = measure("find_pins", [&] { return findPins(physical, cache); }, find_pins_checksum);
    };
    const auto measure_shadow_find_pins = [&] {
      result.timing.find_pins_shadow_ns =
          measure("find_pins_shadow", [&] { return findPinsShadow(shadow_pin_lookup, cache); }, shadow_find_pins_checksum);
    };
    if (run_experiments && shadow_find_first) measure_shadow_find_pins();
    measure_find_pins();
    if (run_experiments && !shadow_find_first) measure_shadow_find_pins();
    result.checksum = foldChecksum(result.checksum, find_pins_checksum);
    if (run_experiments) {
      require(shadow_find_pins_checksum == find_pins_checksum, "timed shadow instance-pin lookup checksum mismatch");
      result.checksum = foldChecksum(result.checksum, shadow_find_pins_checksum);
    }
    result.timing.scan_net_pins_ns = measure("scan_net_pins", [&] { return scanNetPins(physical); }, result.checksum);

    if (run_experiments) {
      // Rotate the experimental lookup paths to reduce a systematic warm-cache advantage.
      uint64_t direct_registry_net_pin_checksum = 0;
      uint64_t cached_net_pin_checksum = 0;
      const auto measure_direct_registry = [&] {
        result.timing.scan_net_pins_direct_registry_ns = measure(
            "scan_net_pins_direct_registry", [&] { return scanNetPinsDirectRegistry(physical).checksum; }, direct_registry_net_pin_checksum);
      };
      const auto measure_cached = [&] {
        result.timing.scan_net_pins_cached_ns = measure(
            "scan_net_pins_cached", [&] { return scanNetPinsCached(physical, master_term_table).checksum; }, cached_net_pin_checksum);
      };
      if (direct_registry_net_pin_scan_first) {
        measure_direct_registry();
        measure_cached();
      } else {
        measure_cached();
        measure_direct_registry();
      }
      require(direct_registry_net_pin_checksum == cached_net_pin_checksum, "timed cached net-pin scan checksum mismatch");
      result.checksum = foldChecksum(result.checksum, direct_registry_net_pin_checksum);
      result.checksum = foldChecksum(result.checksum, cached_net_pin_checksum);

      uint64_t shadow_registry_checksum = 0;
      uint64_t shadow_shared_checksum = 0;
      uint64_t shadow_inline_checksum = 0;
      const auto measure_shadow_registry = [&] {
        result.timing.scan_net_pins_shadow_registry_ns =
            measure("scan_net_pins_shadow_registry", [&] { return scanNetPinsShadowRegistry(physical, shadow_pins).checksum; },
                    shadow_registry_checksum);
      };
      const auto measure_shadow_shared = [&] {
        result.timing.scan_net_pins_shadow_shared_ns = measure(
            "scan_net_pins_shadow_shared", [&] { return scanNetPinsShadowShared(physical, shadow_pins, master_term_table).checksum; },
            shadow_shared_checksum);
      };
      const auto measure_shadow_inline = [&] {
        result.timing.scan_net_pins_shadow_inline_ns =
            measure("scan_net_pins_shadow_inline", [&] { return scanNetPinsShadowInline(physical, shadow_pins).checksum; },
                    shadow_inline_checksum);
      };
      switch (shadow_scan_order % 3u) {
        case 0u:
          measure_shadow_registry();
          measure_shadow_shared();
          measure_shadow_inline();
          break;
        case 1u:
          measure_shadow_shared();
          measure_shadow_inline();
          measure_shadow_registry();
          break;
        default:
          measure_shadow_inline();
          measure_shadow_registry();
          measure_shadow_shared();
          break;
      }
      require(shadow_registry_checksum == shadow_shared_checksum && shadow_inline_checksum == shadow_registry_checksum,
              "timed shadow net-pin scan checksum mismatch");
      result.checksum = foldChecksum(result.checksum, shadow_registry_checksum);
      result.checksum = foldChecksum(result.checksum, shadow_shared_checksum);
      result.checksum = foldChecksum(result.checksum, shadow_inline_checksum);
    }

    result.timing.scan_guides_ns = measure("scan_guides", [&] { return scanGuides(context, cache); }, result.checksum);
    result.timing.decode_routes_ns = measure("decode_routes", [&] { return decodeRoutes(routes, cache); }, result.checksum);
    result.timing.replace_guides_ns = measure("replace_guides", [&] { return replaceGuides(context, cache); }, result.checksum);
  }
  validateGuides(context, cache.guide_payloads, counts.guides);
  require(result.checksum != 0u, "combined run checksum is zero");
  return result;
}

[[nodiscard]] uint64_t median(const std::vector<Run>& runs, uint64_t Timing::*field)
{
  std::vector<uint64_t> values;
  values.reserve(runs.size());
  for (const auto& run : runs) values.push_back(run.timing.*field);
  std::sort(values.begin(), values.end());
  return values.at(values.size() / 2u);
}

[[nodiscard]] Timing medianTiming(const std::vector<Run>& runs)
{
  return {.scan_regular_nets_ns = median(runs, &Timing::scan_regular_nets_ns),
          .find_instances_ns = median(runs, &Timing::find_instances_ns),
          .find_nets_ns = median(runs, &Timing::find_nets_ns),
          .find_pins_ns = median(runs, &Timing::find_pins_ns),
          .find_pins_shadow_ns = median(runs, &Timing::find_pins_shadow_ns),
          .scan_net_pins_ns = median(runs, &Timing::scan_net_pins_ns),
          .scan_net_pins_direct_registry_ns = median(runs, &Timing::scan_net_pins_direct_registry_ns),
          .scan_net_pins_cached_ns = median(runs, &Timing::scan_net_pins_cached_ns),
          .scan_net_pins_shadow_registry_ns = median(runs, &Timing::scan_net_pins_shadow_registry_ns),
          .scan_net_pins_shadow_shared_ns = median(runs, &Timing::scan_net_pins_shadow_shared_ns),
          .scan_net_pins_shadow_inline_ns = median(runs, &Timing::scan_net_pins_shadow_inline_ns),
          .scan_guides_ns = median(runs, &Timing::scan_guides_ns),
          .decode_routes_ns = median(runs, &Timing::decode_routes_ns),
          .replace_guides_ns = median(runs, &Timing::replace_guides_ns)};
}

void writeTiming(std::ostream& output, const Timing& timing)
{
  output << "\"scan_regular_nets_ns\":" << timing.scan_regular_nets_ns << ",\"find_instances_ns\":" << timing.find_instances_ns
         << ",\"find_nets_ns\":" << timing.find_nets_ns << ",\"find_pins_ns\":" << timing.find_pins_ns
         << ",\"find_pins_shadow_ns\":" << timing.find_pins_shadow_ns
         << ",\"scan_net_pins_ns\":" << timing.scan_net_pins_ns << ",\"scan_net_pins_direct_registry_ns\":"
         << timing.scan_net_pins_direct_registry_ns << ",\"scan_net_pins_cached_ns\":"
         << timing.scan_net_pins_cached_ns << ",\"scan_net_pins_shadow_registry_ns\":"
         << timing.scan_net_pins_shadow_registry_ns << ",\"scan_net_pins_shadow_shared_ns\":"
         << timing.scan_net_pins_shadow_shared_ns << ",\"scan_net_pins_shadow_inline_ns\":"
         << timing.scan_net_pins_shadow_inline_ns << ",\"scan_guides_ns\":" << timing.scan_guides_ns
         << ",\"decode_routes_ns\":" << timing.decode_routes_ns << ",\"replace_guides_ns\":" << timing.replace_guides_ns;
}

void writeMeasuredFields(std::ostream& output, bool pins_only, bool run_experiments)
{
  output << "\"measured_fields\":[";
  if (pins_only) {
    output << "\"find_pins_ns\",\"scan_net_pins_ns\"";
  } else {
    output << "\"scan_regular_nets_ns\",\"find_instances_ns\",\"find_nets_ns\",\"find_pins_ns\",\"scan_net_pins_ns\","
              "\"scan_guides_ns\",\"decode_routes_ns\",\"replace_guides_ns\"";
    if (run_experiments) {
      output << ",\"find_pins_shadow_ns\",\"scan_net_pins_direct_registry_ns\",\"scan_net_pins_cached_ns\",\"scan_net_pins_shadow_registry_ns\",\"scan_net_pins_shadow_shared_ns\",\"scan_net_pins_shadow_inline_ns\"";
    }
  }
  output << ']';
}

void writeJson(std::ostream& output, const Counts& counts, const MasterTermPointerTable& master_term_table,
               const BenchInstancePinLookupTable& shadow_pin_lookup, const BenchCachedPinTable& shadow_pins,
               const std::vector<Run>& runs, bool run_experiments, bool pins_only)
{
  output << "{\"schema\":\"drt_service_benchmark_v1\",\"backend\":\"eccdb\",\"scope\":\"native EccDB service APIs used by the "
            "TritonRoute adapter; "
         << (run_experiments ? "find_pins_shadow_ns, scan_net_pins_direct_registry_ns, scan_net_pins_cached_ns, and "
                               "scan_net_pins_shadow_* are benchmark-only "
                               "internal-loop experiments, not service API timings; "
                             : "pin-cache experiments are disabled; ")
         << "LEF/DEF and JSONL parsing, database construction, guide attachment, "
            "validation, and output are excluded from timed phases\",\"counts\":{\"instances\":"
         << counts.instances << ",\"regular_nets\":" << counts.regular_nets << ",\"instance_pins\":" << counts.instance_pins
         << ",\"connected_iterms\":" << counts.connected_iterms << ",\"io_pins\":" << counts.io_pins
         << ",\"guides\":" << counts.guides << ",\"routed_nets\":" << counts.routed_nets
         << ",\"route_shapes\":" << counts.route_shapes << ",\"route_vias\":" << counts.route_vias
         << "},\"master_term_pointer_table\":{\"build_ns\":" << master_term_table.build_ns << ",\"payload_bytes\":"
         << master_term_table.payload_bytes << ",\"slots\":" << master_term_table.slots.size() << ",\"terms\":"
         << master_term_table.terms << "},\"shadow_instance_pin_lookup_table\":{\"build_ns\":" << shadow_pin_lookup.build_ns
         << ",\"payload_bytes\":" << shadow_pin_lookup.payload_bytes << ",\"slots\":" << shadow_pin_lookup.slots.size()
         << ",\"instances\":" << shadow_pin_lookup.instances << ",\"probes\":" << shadow_pin_lookup.probes
         << "},\"shadow_pin_table\":{\"build_ns\":" << shadow_pins.build_ns << ",\"payload_bytes\":"
         << shadow_pins.payload_bytes << ",\"slots\":" << shadow_pins.slots.size() << ",\"pins\":" << shadow_pins.pins
         << "},";
  writeMeasuredFields(output, pins_only, run_experiments);
  output << ",\"runs\":[";
  for (std::size_t index = 0; index < runs.size(); ++index) {
    if (index != 0u) output << ',';
    output << '{';
    writeTiming(output, runs[index].timing);
    output << '}';
  }
  output << "],\"median\":{";
  writeTiming(output, medianTiming(runs));
  output << "},\"checksums\":[";
  for (std::size_t index = 0; index < runs.size(); ++index) {
    if (index != 0u) output << ',';
    output << runs[index].checksum;
  }
  output << "]}\n";
}

[[nodiscard]] std::size_t parseRepeat(std::string_view value)
{
  uint64_t parsed = 0;
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (error != std::errc{} || end != value.data() + value.size() || parsed == 0u
      || parsed > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()) || (parsed % 2u) == 0u) {
    fail("--repeat must be a positive odd integer");
  }
  return static_cast<std::size_t>(parsed);
}

void requireRegularFile(const std::filesystem::path& path, std::string_view option)
{
  if (!std::filesystem::is_regular_file(path)) fail(std::string(option) + " is not a regular file: " + path.string());
}

[[nodiscard]] Options parseOptions(int argc, char** argv)
{
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto value = [&](std::string_view name) -> std::string_view {
      if (index + 1 >= argc) fail("missing value for " + std::string(name));
      return argv[++index];
    };
    if (argument == "--lef") {
      options.lef = std::string(value(argument));
    } else if (argument == "--routed-def") {
      options.routed_def = std::string(value(argument));
    } else if (argument == "--fixture") {
      options.fixture = std::string(value(argument));
    } else if (argument == "--guides") {
      options.guides = std::string(value(argument));
    } else if (argument == "--repeat") {
      options.repeat = parseRepeat(value(argument));
    } else if (argument == "--no-experiments") {
      options.run_experiments = false;
    } else if (argument == "--shadow-find-first") {
      options.shadow_find_first = true;
    } else if (argument == "--pins-only") {
      options.pins_only = true;
    } else if (argument == "--output") {
      options.output = std::string(value(argument));
    } else if (argument == "--help" || argument == "-h") {
      std::cout << "usage: eccdb_drt_service_benchmark --lef FILE --routed-def FILE --fixture FILE --guides FILE "
                   "[--repeat ODD_INT] [--pins-only] [--no-experiments] [--shadow-find-first] [--output JSON]\n";
      std::exit(0);
    } else {
      fail("unknown argument: " + std::string(argument));
    }
  }
  requireRegularFile(options.lef, "--lef");
  requireRegularFile(options.routed_def, "--routed-def");
  requireRegularFile(options.fixture, "--fixture");
  requireRegularFile(options.guides, "--guides");
  return options;
}

void run(const Options& options)
{
  const DbOnlyFixture fixture = DbOnlyFixture::load(options.fixture);
  ImportedDesign imported;
  eccdb::LefTechImporter(imported.technology).import(options.lef);
  eccdb::LefLibraryImporter(imported.technology, imported.library).import(options.lef);
  eccdb::DefDesignImporter(imported.design).import(options.routed_def);

  eccdb::PhysicalDesignService physical(imported.technology, imported.library, imported.design);
  eccdb::RoutingContextService context(imported.design);
  eccdb::NetRouteService routes(imported.design, imported.technology);
  BenchmarkCache cache;
  const Counts counts = buildAndValidateCache(fixture, imported, physical, context, routes, cache, options.guides);
  // All read-only data is validated once outside the timers.  The only timed
  // mutation is replaceGuides(), which is revalidated after every run.
  validateReadQueries(physical, context, routes, cache, counts);
  const bool run_experiments = options.run_experiments && !options.pins_only;
  MasterTermPointerTable master_term_table;
  BenchInstancePinLookupTable shadow_pin_lookup;
  BenchCachedPinTable shadow_pins;
  if (run_experiments) {
    shadow_pin_lookup = buildBenchInstancePinLookupTable(physical, cache);
    validateBenchInstancePinLookupTable(physical, cache, shadow_pin_lookup);
    require(findPinsShadow(shadow_pin_lookup, cache) == findPins(physical, cache),
            "shadow instance-pin lookup checksum mismatch");
    master_term_table = buildMasterTermPointerTable(physical);
    validateMasterTermPointerTable(physical, master_term_table);
    validateCachedNetPinScan(physical, master_term_table, cache.net_pin_entries);
    shadow_pins = buildBenchCachedPinTable(physical);
    validateBenchCachedPinTable(physical, shadow_pins);
    validateShadowNetPinScans(physical, shadow_pins, master_term_table, cache.net_pin_entries);
  }

  std::vector<Run> runs;
  runs.reserve(options.repeat);
  for (std::size_t index = 0; index < options.repeat; ++index) {
    runs.push_back(runOnce(physical, context, routes, cache, counts, master_term_table, shadow_pin_lookup, shadow_pins,
                           run_experiments, options.shadow_find_first, (index % 2u) == 0u, index, options.pins_only));
  }

  if (options.output.empty()) {
    writeJson(std::cout, counts, master_term_table, shadow_pin_lookup, shadow_pins, runs, run_experiments, options.pins_only);
    return;
  }
  if (const auto parent = options.output.parent_path(); !parent.empty()) {
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error) fail("cannot create output directory: " + parent.string());
  }
  std::ofstream output(options.output, std::ios::out | std::ios::trunc);
  if (!output) fail("cannot open output: " + options.output.string());
  writeJson(output, counts, master_term_table, shadow_pin_lookup, shadow_pins, runs, run_experiments, options.pins_only);
  if (!output) fail("cannot write output: " + options.output.string());
}

}  // namespace

int main(int argc, char** argv)
{
  try {
    run(parseOptions(argc, argv));
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "eccdb_drt_service_benchmark: " << error.what() << '\n';
    return 1;
  }
}
