// SPDX-License-Identifier: Apache-2.0

#include <malloc.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "design/DesignStore.h"
#include "design/constraint/model/ConstraintComponents.h"
#include "design/fill/model/FillComponents.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "def/DefDesignImporter.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"
#include "tech/common/TechLayerTypes.h"

namespace eccdb {
namespace {

struct Memory
{
  uint64_t allocator_in_use_kib = 0;
  uint64_t rss_kib = 0;
  uint64_t pss_kib = 0;
  uint64_t private_dirty_kib = 0;
  uint64_t anonymous_kib = 0;
  uint64_t peak_rss_kib = 0;
};

struct Counts
{
  uint64_t layers = 0;
  uint64_t sites = 0;
  uint64_t masters = 0;
  uint64_t master_terms = 0;
  uint64_t rows = 0;
  uint64_t track_grids = 0;
  uint64_t gcell_grids = 0;
  uint64_t instances = 0;
  uint64_t instance_pins = 0;
  uint64_t io_pins = 0;
  uint64_t regular_nets = 0;
  uint64_t special_nets = 0;
  uint64_t vias = 0;
  uint64_t wires = 0;
  uint64_t regions = 0;
  uint64_t groups = 0;
  uint64_t blockages = 0;
  uint64_t fills = 0;
};

struct MemoryBreakdown
{
  uint64_t component_payload_capacity_bytes = 0;
  uint64_t packed_entity_capacity_estimate_bytes = 0;
  uint64_t wire_component_capacity_bytes = 0;
  uint64_t instance_component_capacity_bytes = 0;
  uint64_t instance_pin_component_capacity_bytes = 0;
  uint64_t net_component_capacity_bytes = 0;
  uint64_t wire_path_count = 0;
  uint64_t wire_path_capacity = 0;
  uint64_t wire_path_capacity_bytes = 0;
  uint64_t wire_path_buffer_count = 0;
  uint64_t wire_path_usable_bytes = 0;
  uint64_t wire_path_extra_count = 0;
  uint64_t wire_path_extra_capacity = 0;
  uint64_t wire_path_extra_capacity_bytes = 0;
  uint64_t wire_path_extra_buffer_count = 0;
  uint64_t wire_path_extra_usable_bytes = 0;
  uint64_t wire_point_count = 0;
  uint64_t wire_point_capacity = 0;
  uint64_t wire_point_capacity_bytes = 0;
  uint64_t wire_point_buffer_count = 0;
  uint64_t wire_point_usable_bytes = 0;
  uint64_t wire_point_extra_count = 0;
  uint64_t wire_point_extra_capacity = 0;
  uint64_t wire_point_extra_capacity_bytes = 0;
  uint64_t wire_point_extra_buffer_count = 0;
  uint64_t wire_point_extra_usable_bytes = 0;
  uint64_t wire_via_count = 0;
  uint64_t wire_via_capacity = 0;
  uint64_t wire_via_capacity_bytes = 0;
  uint64_t wire_via_buffer_count = 0;
  uint64_t wire_via_usable_bytes = 0;
  uint64_t wire_via_extra_count = 0;
  uint64_t wire_via_extra_capacity = 0;
  uint64_t wire_via_extra_capacity_bytes = 0;
  uint64_t wire_via_extra_buffer_count = 0;
  uint64_t wire_via_extra_usable_bytes = 0;
  uint64_t wire_rectangle_count = 0;
  uint64_t wire_rectangle_capacity = 0;
  uint64_t wire_rectangle_capacity_bytes = 0;
  uint64_t wire_rectangle_buffer_count = 0;
  uint64_t wire_rectangle_usable_bytes = 0;
  uint64_t wire_nested_buffer_count = 0;
  uint64_t wire_nested_usable_bytes = 0;
  uint64_t wire_nested_usable_overhead_bytes = 0;
  uint64_t netlist_nested_capacity_bytes = 0;
  uint64_t routing_other_nested_capacity_bytes = 0;
  uint64_t other_nested_capacity_bytes = 0;
  uint64_t dynamic_string_capacity_estimate_bytes = 0;
  uint64_t attributed_capacity_bytes = 0;
  uint64_t unattributed_allocator_bytes = 0;
  uint64_t unattributed_after_wire_usable_bytes = 0;
};

uint64_t statusValue(const std::string& field)
{
  std::ifstream input("/proc/self/status");
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream fields(line);
    std::string name;
    uint64_t value = 0;
    std::string unit;
    if ((fields >> name >> value >> unit) && name == field && unit == "kB") {
      return value;
    }
  }
  throw std::runtime_error("missing process status field: " + field);
}

Memory readMemory()
{
  const auto allocator = mallinfo2();
  Memory result;
  result.allocator_in_use_kib = (static_cast<uint64_t>(allocator.uordblks) + static_cast<uint64_t>(allocator.hblkhd)) / 1024u;
  result.peak_rss_kib = statusValue("VmHWM:");

  std::ifstream input("/proc/self/smaps_rollup");
  if (!input) {
    throw std::runtime_error("cannot read /proc/self/smaps_rollup");
  }
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream fields(line);
    std::string name;
    uint64_t value = 0;
    std::string unit;
    if (!(fields >> name >> value >> unit) || unit != "kB") {
      continue;
    }
    if (name == "Rss:") {
      result.rss_kib = value;
    } else if (name == "Pss:") {
      result.pss_kib = value;
    } else if (name == "Private_Dirty:") {
      result.private_dirty_kib = value;
    } else if (name == "Anonymous:") {
      result.anonymous_kib = value;
    }
  }
  return result;
}

Memory settledMemory()
{
  static_cast<void>(malloc_trim(0));
  return readMemory();
}

uint64_t elapsedMilliseconds(std::chrono::steady_clock::time_point start)
{
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

Counts countDatabase(const TechStore& technology, const LibraryStore& library, const DesignStore& design)
{
  Counts result;
  const auto* layer_storage = technology.techRegistry().registry().storage<TechLayerInfo>();
  result.layers = layer_storage == nullptr ? 0u : layer_storage->size();
  result.sites = library.siteStorage().siteCount();
  result.masters = library.cellMasterStorage().cellMasterCount();
  result.master_terms = library.masterTermStorage().masterTermCount();
  result.rows = design.floorplanStorage().rowCount();
  result.track_grids = design.floorplanStorage().trackGridCount();
  result.gcell_grids = design.floorplanStorage().gcellGridCount();
  result.instances = design.netlistStorage().instanceCount();
  result.instance_pins = design.netlistStorage().instancePinCount();
  result.io_pins = design.netlistStorage().ioPinCount();
  const auto* special_net_storage = design.designRegistry().registry().storage<DesignSpecialNet>();
  result.special_nets = special_net_storage == nullptr ? 0u : special_net_storage->size();
  result.regular_nets = design.netlistStorage().netCount() - result.special_nets;
  result.vias = design.routingStorage().viaCount();
  result.wires = design.routingStorage().wireCount();
  result.regions = design.constraintStorage().regionCount();
  result.groups = design.constraintStorage().groupCount();
  result.blockages = design.constraintStorage().blockageCount();
  result.fills = design.fillStorage().fillCount();
  return result;
}

uint64_t dynamicStringCapacityBytes(const std::string& value)
{
  const auto data = reinterpret_cast<uintptr_t>(value.data());
  const auto object = reinterpret_cast<uintptr_t>(&value);
  return data >= object && data < object + sizeof(value) ? 0u : static_cast<uint64_t>(value.capacity()) + 1u;
}

template <typename Component>
uint64_t componentCapacity(const DesignRegistry::registry_type& registry)
{
  const auto* storage = registry.storage<Component>();
  return storage == nullptr ? 0u : static_cast<uint64_t>(storage->capacity());
}

template <typename Component>
void addComponentStorage(const DesignRegistry::registry_type& registry, MemoryBreakdown& result)
{
  const auto capacity = componentCapacity<Component>(registry);
  result.component_payload_capacity_bytes += capacity * sizeof(Component);
  result.packed_entity_capacity_estimate_bytes += capacity * sizeof(DesignEntity);
}

MemoryBreakdown memoryBreakdown(const DesignStore& design, uint64_t allocator_in_use_bytes)
{
  MemoryBreakdown result;
  const auto& registry = design.designRegistry().registry();

  result.wire_component_capacity_bytes = componentCapacity<DesignWire>(registry) * sizeof(DesignWire);
  result.instance_component_capacity_bytes = componentCapacity<DesignInstance>(registry) * sizeof(DesignInstance);
  result.instance_pin_component_capacity_bytes = componentCapacity<DesignInstancePin>(registry) * sizeof(DesignInstancePin);
  result.net_component_capacity_bytes = componentCapacity<DesignNet>(registry) * sizeof(DesignNet);

  addComponentStorage<DesignRoot>(registry, result);
  addComponentStorage<DesignInfo>(registry, result);
  addComponentStorage<DesignDieArea>(registry, result);
  addComponentStorage<DesignCoreArea>(registry, result);
  addComponentStorage<DesignRow>(registry, result);
  addComponentStorage<DesignTrackGrid>(registry, result);
  addComponentStorage<DesignGCellGrid>(registry, result);
  addComponentStorage<DesignInstance>(registry, result);
  addComponentStorage<DesignInstancePins>(registry, result);
  addComponentStorage<DesignInstancePin>(registry, result);
  addComponentStorage<DesignIoPin>(registry, result);
  addComponentStorage<DesignNet>(registry, result);
  addComponentStorage<DesignNetInstancePins>(registry, result);
  addComponentStorage<DesignNetIoPins>(registry, result);
  addComponentStorage<DesignSpecialNet>(registry, result);
  addComponentStorage<DesignNetOptions>(registry, result);
  addComponentStorage<DesignVia>(registry, result);
  addComponentStorage<DesignNonDefaultRule>(registry, result);
  addComponentStorage<DesignWire>(registry, result);
  addComponentStorage<DesignNetGeometry>(registry, result);
  addComponentStorage<DesignRegion>(registry, result);
  addComponentStorage<DesignGroup>(registry, result);
  addComponentStorage<DesignBlockage>(registry, result);
  addComponentStorage<DesignFill>(registry, result);

  for (const auto entity : registry.view<const DesignWire>()) {
    const auto& wire = registry.get<const DesignWire>(entity);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(wire.shield_net);
  }

  const auto routing_pool = design.routingStorage().routingPoolStatistics();
  result.wire_path_count = routing_pool.paths.count;
  result.wire_path_capacity = routing_pool.paths.capacity;
  result.wire_path_capacity_bytes = routing_pool.paths.capacity_bytes;
  result.wire_path_buffer_count = routing_pool.paths.capacity == 0u ? 0u : 1u;
  result.wire_path_usable_bytes = routing_pool.paths.allocation_usable_bytes;
  result.wire_path_extra_count = routing_pool.path_extras.count;
  result.wire_path_extra_capacity = routing_pool.path_extras.capacity;
  result.wire_path_extra_capacity_bytes = routing_pool.path_extras.capacity_bytes;
  result.wire_path_extra_buffer_count = routing_pool.path_extras.capacity == 0u ? 0u : 1u;
  result.wire_path_extra_usable_bytes = routing_pool.path_extras.allocation_usable_bytes;
  result.dynamic_string_capacity_estimate_bytes += routing_pool.dynamic_string_capacity_bytes;
  result.wire_point_count = routing_pool.points.count;
  result.wire_point_capacity = routing_pool.points.capacity;
  result.wire_point_capacity_bytes = routing_pool.points.capacity_bytes;
  result.wire_point_buffer_count = routing_pool.points.capacity == 0u ? 0u : 1u;
  result.wire_point_usable_bytes = routing_pool.points.allocation_usable_bytes;
  result.wire_point_extra_count = routing_pool.point_extras.count;
  result.wire_point_extra_capacity = routing_pool.point_extras.capacity;
  result.wire_point_extra_capacity_bytes = routing_pool.point_extras.capacity_bytes;
  result.wire_point_extra_buffer_count = routing_pool.point_extras.capacity == 0u ? 0u : 1u;
  result.wire_point_extra_usable_bytes = routing_pool.point_extras.allocation_usable_bytes;
  result.wire_via_count = routing_pool.vias.count;
  result.wire_via_capacity = routing_pool.vias.capacity;
  result.wire_via_capacity_bytes = routing_pool.vias.capacity_bytes;
  result.wire_via_buffer_count = routing_pool.vias.capacity == 0u ? 0u : 1u;
  result.wire_via_usable_bytes = routing_pool.vias.allocation_usable_bytes;
  result.wire_via_extra_count = routing_pool.via_extras.count;
  result.wire_via_extra_capacity = routing_pool.via_extras.capacity;
  result.wire_via_extra_capacity_bytes = routing_pool.via_extras.capacity_bytes;
  result.wire_via_extra_buffer_count = routing_pool.via_extras.capacity == 0u ? 0u : 1u;
  result.wire_via_extra_usable_bytes = routing_pool.via_extras.allocation_usable_bytes;
  result.wire_rectangle_count = routing_pool.rectangles.count;
  result.wire_rectangle_capacity = routing_pool.rectangles.capacity;
  result.wire_rectangle_capacity_bytes = routing_pool.rectangles.capacity_bytes;
  result.wire_rectangle_buffer_count = routing_pool.rectangles.capacity == 0u ? 0u : 1u;
  result.wire_rectangle_usable_bytes = routing_pool.rectangles.allocation_usable_bytes;

  result.wire_nested_buffer_count = result.wire_path_buffer_count + result.wire_path_extra_buffer_count + result.wire_point_buffer_count
                                    + result.wire_point_extra_buffer_count + result.wire_via_buffer_count
                                    + result.wire_via_extra_buffer_count + result.wire_rectangle_buffer_count;
  result.wire_nested_usable_bytes = result.wire_path_usable_bytes + result.wire_path_extra_usable_bytes + result.wire_point_usable_bytes
                                    + result.wire_point_extra_usable_bytes + result.wire_via_usable_bytes
                                    + result.wire_via_extra_usable_bytes + result.wire_rectangle_usable_bytes;
  const auto wire_nested_capacity_bytes = result.wire_path_capacity_bytes + result.wire_path_extra_capacity_bytes
                                          + result.wire_point_capacity_bytes + result.wire_point_extra_capacity_bytes
                                          + result.wire_via_capacity_bytes + result.wire_via_extra_capacity_bytes
                                          + result.wire_rectangle_capacity_bytes;
  result.wire_nested_usable_overhead_bytes
      = result.wire_nested_usable_bytes > wire_nested_capacity_bytes ? result.wire_nested_usable_bytes - wire_nested_capacity_bytes
                                                                     : 0u;

  for (const auto entity : registry.view<const DesignInstance>()) {
    const auto& instance = registry.get<const DesignInstance>(entity);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(instance.name);
    result.netlist_nested_capacity_bytes += instance.region_bounds.capacity() * sizeof(Rect);
  }
  for (const auto entity : registry.view<const DesignInstancePins>()) {
    result.netlist_nested_capacity_bytes
        += registry.get<const DesignInstancePins>(entity).values.capacity() * sizeof(DesignInstancePinId);
  }
  for (const auto entity : registry.view<const DesignNetInstancePins>()) {
    result.netlist_nested_capacity_bytes
        += registry.get<const DesignNetInstancePins>(entity).values.capacity() * sizeof(DesignInstancePinId);
  }
  for (const auto entity : registry.view<const DesignNetIoPins>()) {
    result.netlist_nested_capacity_bytes += registry.get<const DesignNetIoPins>(entity).values.capacity() * sizeof(DesignIoPinId);
  }
  for (const auto entity : registry.view<const DesignIoPin>()) {
    const auto& pin = registry.get<const DesignIoPin>(entity);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(pin.name);
    result.netlist_nested_capacity_bytes += pin.ports.capacity() * sizeof(DesignIoPinPort);
    for (const auto& port : pin.ports) {
      result.netlist_nested_capacity_bytes += port.rectangles.capacity() * sizeof(DesignPinRectangle);
      result.netlist_nested_capacity_bytes += port.polygons.capacity() * sizeof(DesignPinPolygon);
      result.netlist_nested_capacity_bytes += port.vias.capacity() * sizeof(DesignPinVia);
      for (const auto& polygon : port.polygons) {
        result.netlist_nested_capacity_bytes += polygon.points.capacity() * sizeof(Point);
      }
    }
  }
  for (const auto entity : registry.view<const DesignNet>()) {
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(registry.get<const DesignNet>(entity).name);
  }
  for (const auto entity : registry.view<const DesignNetOptions>()) {
    const auto& options = registry.get<const DesignNetOptions>(entity);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(options.original);
    result.netlist_nested_capacity_bytes += options.spacing_rules.capacity() * sizeof(DesignNetSpacingRule);
  }

  for (const auto entity : registry.view<const DesignVia>()) {
    const auto& via = registry.get<const DesignVia>(entity);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(via.name);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(via.pattern_name);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(via.generated.cut_pattern);
    result.routing_other_nested_capacity_bytes += via.rectangles.capacity() * sizeof(DesignViaRectangle);
    result.routing_other_nested_capacity_bytes += via.polygons.capacity() * sizeof(DesignViaPolygon);
    for (const auto& polygon : via.polygons) {
      result.routing_other_nested_capacity_bytes += polygon.points.capacity() * sizeof(Point);
    }
  }
  for (const auto entity : registry.view<const DesignNonDefaultRule>()) {
    const auto& rule = registry.get<const DesignNonDefaultRule>(entity);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(rule.name);
    result.routing_other_nested_capacity_bytes += rule.layer_rules.capacity() * sizeof(DesignNdrLayerRule);
    result.routing_other_nested_capacity_bytes += rule.vias.capacity() * sizeof(DesignNdrViaRef);
    result.routing_other_nested_capacity_bytes += rule.via_rules.capacity() * sizeof(TechViaRuleGenerateId);
    result.routing_other_nested_capacity_bytes += rule.min_cuts.capacity() * sizeof(DesignNdrMinCutsRule);
    result.routing_other_nested_capacity_bytes += rule.properties.capacity() * sizeof(DesignProperty);
    for (const auto& property : rule.properties) {
      result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(property.name);
      result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(property.value);
    }
  }
  for (const auto entity : registry.view<const DesignNetGeometry>()) {
    const auto& geometry = registry.get<const DesignNetGeometry>(entity);
    result.routing_other_nested_capacity_bytes += geometry.rectangles.capacity() * sizeof(DesignNetRectangle);
    result.routing_other_nested_capacity_bytes += geometry.polygons.capacity() * sizeof(DesignNetPolygon);
    result.routing_other_nested_capacity_bytes += geometry.vias.capacity() * sizeof(DesignNetVia);
    for (const auto& rectangle : geometry.rectangles) {
      result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(rectangle.shield_net);
      result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(rectangle.shape);
    }
    for (const auto& polygon : geometry.polygons) {
      result.routing_other_nested_capacity_bytes += polygon.points.capacity() * sizeof(Point);
      result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(polygon.shield_net);
      result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(polygon.shape);
    }
    for (const auto& via : geometry.vias) {
      result.routing_other_nested_capacity_bytes += via.origins.capacity() * sizeof(Point);
      result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(via.shield_net);
      result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(via.shape);
    }
  }

  for (const auto entity : registry.view<const DesignRow>()) {
    const auto& row = registry.get<const DesignRow>(entity);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(row.name);
    result.other_nested_capacity_bytes += row.properties.capacity() * sizeof(DesignProperty);
    for (const auto& property : row.properties) {
      result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(property.name);
      result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(property.value);
    }
  }
  for (const auto entity : registry.view<const DesignTrackGrid>()) {
    result.other_nested_capacity_bytes += registry.get<const DesignTrackGrid>(entity).layers.capacity() * sizeof(TechRoutingLayerId);
  }
  for (const auto entity : registry.view<const DesignRegion>()) {
    const auto& region = registry.get<const DesignRegion>(entity);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(region.name);
    result.other_nested_capacity_bytes += region.rectangles.capacity() * sizeof(Rect);
  }
  for (const auto entity : registry.view<const DesignGroup>()) {
    const auto& group = registry.get<const DesignGroup>(entity);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(group.name);
    result.other_nested_capacity_bytes += group.instances.capacity() * sizeof(DesignInstanceId);
  }
  for (const auto entity : registry.view<const DesignBlockage>()) {
    const auto& blockage = registry.get<const DesignBlockage>(entity);
    result.other_nested_capacity_bytes += blockage.rectangles.capacity() * sizeof(Rect);
    result.other_nested_capacity_bytes += blockage.polygons.capacity() * sizeof(std::vector<Point>);
    for (const auto& polygon : blockage.polygons) {
      result.other_nested_capacity_bytes += polygon.capacity() * sizeof(Point);
    }
  }
  for (const auto entity : registry.view<const DesignFill>()) {
    result.other_nested_capacity_bytes += registry.get<const DesignFill>(entity).rectangles.capacity() * sizeof(Rect);
  }

  result.attributed_capacity_bytes
      = result.component_payload_capacity_bytes + result.packed_entity_capacity_estimate_bytes + result.wire_path_capacity_bytes
        + result.wire_path_extra_capacity_bytes + result.wire_point_capacity_bytes + result.wire_via_capacity_bytes
        + result.wire_point_extra_capacity_bytes + result.wire_via_extra_capacity_bytes + result.wire_rectangle_capacity_bytes
        + result.netlist_nested_capacity_bytes + result.routing_other_nested_capacity_bytes + result.other_nested_capacity_bytes
        + result.dynamic_string_capacity_estimate_bytes;
  result.unattributed_allocator_bytes
      = allocator_in_use_bytes > result.attributed_capacity_bytes ? allocator_in_use_bytes - result.attributed_capacity_bytes : 0u;
  result.unattributed_after_wire_usable_bytes
      = result.unattributed_allocator_bytes > result.wire_nested_usable_overhead_bytes
            ? result.unattributed_allocator_bytes - result.wire_nested_usable_overhead_bytes
            : 0u;
  return result;
}

void printMemory(const Memory& value)
{
  std::cout << "{\"allocator_in_use_kib\":" << value.allocator_in_use_kib << ",\"rss_kib\":" << value.rss_kib
            << ",\"pss_kib\":" << value.pss_kib << ",\"private_dirty_kib\":" << value.private_dirty_kib
            << ",\"anonymous_kib\":" << value.anonymous_kib << ",\"peak_rss_kib\":" << value.peak_rss_kib << "}";
}

void printCounts(const Counts& value)
{
  std::cout << "{\"layers\":" << value.layers << ",\"sites\":" << value.sites << ",\"masters\":" << value.masters
            << ",\"master_terms\":" << value.master_terms << ",\"rows\":" << value.rows
            << ",\"track_grids\":" << value.track_grids << ",\"gcell_grids\":" << value.gcell_grids
            << ",\"instances\":" << value.instances << ",\"instance_pins\":" << value.instance_pins
            << ",\"io_pins\":" << value.io_pins << ",\"regular_nets\":" << value.regular_nets
            << ",\"special_nets\":" << value.special_nets << ",\"vias\":" << value.vias << ",\"wires\":" << value.wires
            << ",\"regions\":" << value.regions << ",\"groups\":" << value.groups << ",\"blockages\":" << value.blockages
            << ",\"fills\":" << value.fills << "}";
}

void printMemoryBreakdown(const MemoryBreakdown& value)
{
  std::cout << "{\"component_payload_capacity_bytes\":" << value.component_payload_capacity_bytes
            << ",\"packed_entity_capacity_estimate_bytes\":" << value.packed_entity_capacity_estimate_bytes
            << ",\"wire_component_capacity_bytes\":" << value.wire_component_capacity_bytes
            << ",\"instance_component_capacity_bytes\":" << value.instance_component_capacity_bytes
            << ",\"instance_pin_component_capacity_bytes\":" << value.instance_pin_component_capacity_bytes
            << ",\"net_component_capacity_bytes\":" << value.net_component_capacity_bytes
            << ",\"wire_path_count\":" << value.wire_path_count << ",\"wire_path_capacity\":" << value.wire_path_capacity
            << ",\"wire_path_capacity_bytes\":" << value.wire_path_capacity_bytes
            << ",\"wire_path_buffer_count\":" << value.wire_path_buffer_count
            << ",\"wire_path_usable_bytes\":" << value.wire_path_usable_bytes
            << ",\"wire_path_record_size_bytes\":" << sizeof(DesignRoutingPathRecord)
            << ",\"wire_path_extra_count\":" << value.wire_path_extra_count
            << ",\"wire_path_extra_capacity\":" << value.wire_path_extra_capacity
            << ",\"wire_path_extra_capacity_bytes\":" << value.wire_path_extra_capacity_bytes
            << ",\"wire_path_extra_buffer_count\":" << value.wire_path_extra_buffer_count
            << ",\"wire_path_extra_usable_bytes\":" << value.wire_path_extra_usable_bytes
            << ",\"wire_point_count\":" << value.wire_point_count << ",\"wire_point_capacity\":" << value.wire_point_capacity
            << ",\"wire_point_capacity_bytes\":" << value.wire_point_capacity_bytes
            << ",\"wire_point_record_size_bytes\":" << sizeof(DesignRoutingPointRecord)
            << ",\"wire_point_buffer_count\":" << value.wire_point_buffer_count
            << ",\"wire_point_usable_bytes\":" << value.wire_point_usable_bytes
            << ",\"wire_point_extra_count\":" << value.wire_point_extra_count
            << ",\"wire_point_extra_capacity\":" << value.wire_point_extra_capacity
            << ",\"wire_point_extra_capacity_bytes\":" << value.wire_point_extra_capacity_bytes
            << ",\"wire_point_extra_buffer_count\":" << value.wire_point_extra_buffer_count
            << ",\"wire_point_extra_usable_bytes\":" << value.wire_point_extra_usable_bytes
            << ",\"wire_via_count\":" << value.wire_via_count << ",\"wire_via_capacity\":" << value.wire_via_capacity
            << ",\"wire_via_capacity_bytes\":" << value.wire_via_capacity_bytes
            << ",\"wire_via_record_size_bytes\":" << sizeof(DesignRoutingViaRecord)
            << ",\"wire_via_buffer_count\":" << value.wire_via_buffer_count
            << ",\"wire_via_usable_bytes\":" << value.wire_via_usable_bytes
            << ",\"wire_via_extra_count\":" << value.wire_via_extra_count
            << ",\"wire_via_extra_capacity\":" << value.wire_via_extra_capacity
            << ",\"wire_via_extra_capacity_bytes\":" << value.wire_via_extra_capacity_bytes
            << ",\"wire_via_extra_buffer_count\":" << value.wire_via_extra_buffer_count
            << ",\"wire_via_extra_usable_bytes\":" << value.wire_via_extra_usable_bytes
            << ",\"wire_rectangle_count\":" << value.wire_rectangle_count
            << ",\"wire_rectangle_capacity\":" << value.wire_rectangle_capacity
            << ",\"wire_rectangle_capacity_bytes\":" << value.wire_rectangle_capacity_bytes
            << ",\"wire_rectangle_buffer_count\":" << value.wire_rectangle_buffer_count
            << ",\"wire_rectangle_usable_bytes\":" << value.wire_rectangle_usable_bytes
            << ",\"wire_nested_buffer_count\":" << value.wire_nested_buffer_count
            << ",\"wire_nested_usable_bytes\":" << value.wire_nested_usable_bytes
            << ",\"wire_nested_usable_overhead_bytes\":" << value.wire_nested_usable_overhead_bytes
            << ",\"netlist_nested_capacity_bytes\":" << value.netlist_nested_capacity_bytes
            << ",\"routing_other_nested_capacity_bytes\":" << value.routing_other_nested_capacity_bytes
            << ",\"other_nested_capacity_bytes\":" << value.other_nested_capacity_bytes
            << ",\"dynamic_string_capacity_estimate_bytes\":" << value.dynamic_string_capacity_estimate_bytes
            << ",\"attributed_capacity_bytes\":" << value.attributed_capacity_bytes
            << ",\"unattributed_allocator_bytes\":" << value.unattributed_allocator_bytes
            << ",\"unattributed_after_wire_usable_bytes\":" << value.unattributed_after_wire_usable_bytes << "}";
}

}  // namespace
}  // namespace eccdb

int main(int argc, char** argv)
{
  if (argc != 3) {
    std::cerr << "usage: eccdb_entt_memory_probe <lef> <def>\n";
    return 2;
  }

  try {
    const std::filesystem::path lef = argv[1];
    const std::filesystem::path def = argv[2];
    const auto baseline = eccdb::settledMemory();

    const auto lef_start = std::chrono::steady_clock::now();
    eccdb::TechStore technology;
    eccdb::LefTechImporter(technology).import(lef);
    eccdb::LibraryStore library(technology.techRegistry());
    eccdb::LefLibraryImporter(technology, library).import(lef);
    const auto lef_milliseconds = eccdb::elapsedMilliseconds(lef_start);
    const auto after_lef = eccdb::settledMemory();

    const auto def_start = std::chrono::steady_clock::now();
    eccdb::DesignStore design(technology.techRegistry(), library.libraryRegistry());
    eccdb::DefDesignImporter importer(design);
    importer.import(def);
    const auto def_milliseconds = eccdb::elapsedMilliseconds(def_start);
    const auto after_def = eccdb::settledMemory();
    const auto counts = eccdb::countDatabase(technology, library, design);
    const auto breakdown = eccdb::memoryBreakdown(design, after_def.allocator_in_use_kib * 1024u);

    std::cout << "ENTT_MEMORY_JSON={\"baseline\":";
    eccdb::printMemory(baseline);
    std::cout << ",\"after_lef\":";
    eccdb::printMemory(after_lef);
    std::cout << ",\"after_def\":";
    eccdb::printMemory(after_def);
    std::cout << ",\"lef_milliseconds\":" << lef_milliseconds << ",\"def_milliseconds\":" << def_milliseconds
              << ",\"diagnostics_count\":" << importer.diagnostics().size() << ",\"counts\":";
    eccdb::printCounts(counts);
    std::cout << ",\"memory_breakdown\":";
    eccdb::printMemoryBreakdown(breakdown);
    std::cout << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "EnTT memory probe failed: " << error.what() << '\n';
    return 1;
  }
}
