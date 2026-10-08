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
#include <type_traits>
#include <vector>

#include "design/DesignStore.h"
#include "design/constraint/model/ConstraintComponents.h"
#include "design/fill/model/FillComponents.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "design/geometry/DesignGeometryComponents.h"
#include "design/guide/model/GuideComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/via/component/ViaComponents.h"
#include "def/DefDesignImporter.h"
#include "geometry/DomainGeometryComponents.h"
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
  // EnTT exposes the sparse-set extent, not its private sparse-page pointer
  // table. This is therefore an upper-bound estimate of sparse entity slots.
  uint64_t sparse_entity_extent_estimate_bytes = 0;
  uint64_t wire_component_capacity_bytes = 0;
  uint64_t instance_component_capacity_bytes = 0;
  uint64_t instance_pin_component_capacity_bytes = 0;
  uint64_t instance_term_name_index_count = 0;
  // EnTT dense_map does not expose packed-vector capacity. These retain the
  // public lower bound (size), rather than assuming a private layout.
  uint64_t instance_term_name_index_capacity_lower_bound = 0;
  uint64_t instance_term_name_index_capacity_bytes_lower_bound = 0;
  uint64_t instance_term_name_index_bucket_count = 0;
  uint64_t instance_term_name_index_bucket_bytes_lower_bound = 0;
  uint64_t instance_term_id_vector_capacity_bytes = 0;
  uint64_t net_component_capacity_bytes = 0;
  uint64_t geometry_bounds_count = 0;
  uint64_t geometry_bounds_component_capacity_bytes = 0;
  uint64_t route_shape_count = 0;
  uint64_t wire_shape_id_capacity_bytes = 0;
  uint64_t wire_shape_id_buffer_count = 0;
  uint64_t wire_shape_id_usable_bytes = 0;
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

template <typename Component, typename Registry>
uint64_t componentCapacity(const Registry& registry)
{
  const auto* storage = registry.template storage<Component>();
  return storage == nullptr ? 0u : static_cast<uint64_t>(storage->capacity());
}

template <typename Component, typename Registry>
void addComponentStorage(const Registry& registry, MemoryBreakdown& result)
{
  const auto* storage = registry.template storage<Component>();
  if (storage == nullptr) {
    return;
  }

  const auto capacity = static_cast<uint64_t>(storage->capacity());
  using Entity = typename Registry::entity_type;
  result.component_payload_capacity_bytes += capacity * sizeof(Component);
  // EnTT does not expose the packed entity vector capacity separately from a
  // paged component storage. Using the component page capacity keeps this
  // diagnostic comparable across the design and geometry registries.
  result.packed_entity_capacity_estimate_bytes += capacity * sizeof(Entity);
  result.sparse_entity_extent_estimate_bytes += static_cast<uint64_t>(storage->extent()) * sizeof(Entity);
}

MemoryBreakdown memoryBreakdown(const DesignStore& design, uint64_t allocator_in_use_bytes)
{
  MemoryBreakdown result;
  const auto& registry = design.designRegistry().registry();
  const auto& geometry_registry = design.geometryRegistry().registry();

  result.wire_component_capacity_bytes = componentCapacity<DesignWire>(registry) * sizeof(DesignWire);
  result.instance_component_capacity_bytes = componentCapacity<DesignInstance>(registry) * sizeof(DesignInstance);
  result.instance_pin_component_capacity_bytes = componentCapacity<DesignInstanceTerm>(registry) * sizeof(DesignInstanceTerm);
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
  addComponentStorage<DesignInstanceTerm>(registry, result);
  addComponentStorage<DesignIoPin>(registry, result);
  addComponentStorage<DesignBlockPin>(registry, result);
  addComponentStorage<DesignNet>(registry, result);
  addComponentStorage<DesignNetInstancePins>(registry, result);
  addComponentStorage<DesignNetIoPins>(registry, result);
  addComponentStorage<DesignSpecialNet>(registry, result);
  addComponentStorage<DesignNetOptions>(registry, result);
  addComponentStorage<DesignVia>(registry, result);
  addComponentStorage<DesignNonDefaultRule>(registry, result);
  addComponentStorage<DesignWire>(registry, result);
  addComponentStorage<DesignNetGeometry>(registry, result);
  addComponentStorage<DesignNetRouteGuides>(registry, result);
  addComponentStorage<DesignNetTrackAssignments>(registry, result);
  addComponentStorage<DesignRegion>(registry, result);
  addComponentStorage<DesignGroup>(registry, result);
  addComponentStorage<DesignBlockage>(registry, result);
  addComponentStorage<DesignFill>(registry, result);

  // Geometry is in an independent EnTT registry. Count every canonical
  // component store here, rather than treating its component objects as
  // nested vectors under Wire or Net.
  addComponentStorage<GeometryShape>(geometry_registry, result);
  result.geometry_bounds_count = geometry_registry.storage<GeometryBounds>() == nullptr
                                     ? 0u
                                     : geometry_registry.storage<GeometryBounds>()->size();
  result.geometry_bounds_component_capacity_bytes
      = componentCapacity<GeometryBounds>(geometry_registry) * sizeof(GeometryBounds);
  addComponentStorage<GeometryBounds>(geometry_registry, result);
  addComponentStorage<DesignRouteShape>(geometry_registry, result);
  addComponentStorage<DesignRouteShapeAttributes>(geometry_registry, result);
  addComponentStorage<DesignGeometryRectangle>(geometry_registry, result);
  addComponentStorage<DesignGeometryPolygon>(geometry_registry, result);
  addComponentStorage<DesignGeometryNetVia>(geometry_registry, result);
  addComponentStorage<GeometryRectangle>(geometry_registry, result);
  addComponentStorage<GeometryPolygon>(geometry_registry, result);
  addComponentStorage<GeometryBoundary>(geometry_registry, result);
  addComponentStorage<DesignPinRectangle>(geometry_registry, result);
  addComponentStorage<DesignPinPolygon>(geometry_registry, result);
  addComponentStorage<DesignPinVia>(geometry_registry, result);
  addComponentStorage<DesignViaRectangle>(geometry_registry, result);
  addComponentStorage<DesignViaPolygon>(geometry_registry, result);
  addComponentStorage<DesignRouteGuide>(geometry_registry, result);
  addComponentStorage<DesignTrackAssignmentShape>(geometry_registry, result);

  for (const auto entity : registry.view<const DesignWire>()) {
    const auto& wire = registry.get<const DesignWire>(entity);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(wire.shield_net);
  }

  const auto routing_vectors = design.routingStorage().routingStatistics();
  result.route_shape_count = routing_vectors.shapes.count;
  result.wire_shape_id_capacity_bytes = routing_vectors.wire_shape_ids.capacity_bytes;
  result.wire_shape_id_buffer_count = routing_vectors.wire_shape_ids.buffer_count;
  result.wire_shape_id_usable_bytes = routing_vectors.wire_shape_ids.allocation_usable_bytes;
  result.wire_path_count = routing_vectors.paths.count;
  result.wire_path_capacity = routing_vectors.paths.capacity;
  result.wire_path_capacity_bytes = routing_vectors.paths.capacity_bytes;
  result.wire_path_buffer_count = routing_vectors.paths.buffer_count;
  result.wire_path_usable_bytes = routing_vectors.paths.allocation_usable_bytes;
  result.wire_path_extra_count = routing_vectors.path_extras.count;
  result.wire_path_extra_capacity = routing_vectors.path_extras.capacity;
  // Shape attributes are already counted by addComponentStorage above.
  result.wire_path_extra_capacity_bytes = 0;
  result.wire_path_extra_buffer_count = routing_vectors.path_extras.buffer_count;
  result.wire_path_extra_usable_bytes = routing_vectors.path_extras.allocation_usable_bytes;
  result.dynamic_string_capacity_estimate_bytes += routing_vectors.dynamic_string_capacity_bytes;
  result.wire_point_count = routing_vectors.points.count;
  result.wire_point_capacity = routing_vectors.points.capacity;
  result.wire_point_capacity_bytes = routing_vectors.points.capacity_bytes;
  result.wire_point_buffer_count = routing_vectors.points.buffer_count;
  result.wire_point_usable_bytes = routing_vectors.points.allocation_usable_bytes;
  result.wire_via_count = routing_vectors.vias.count;
  result.wire_via_capacity = routing_vectors.vias.capacity;
  result.wire_via_capacity_bytes = routing_vectors.vias.capacity_bytes;
  result.wire_via_buffer_count = routing_vectors.vias.buffer_count;
  result.wire_via_usable_bytes = routing_vectors.vias.allocation_usable_bytes;
  result.wire_rectangle_count = routing_vectors.rectangles.count;
  result.wire_rectangle_capacity = routing_vectors.rectangles.capacity;
  result.wire_rectangle_capacity_bytes = routing_vectors.rectangles.capacity_bytes;
  result.wire_rectangle_buffer_count = routing_vectors.rectangles.buffer_count;
  result.wire_rectangle_usable_bytes = routing_vectors.rectangles.allocation_usable_bytes;

  result.wire_nested_buffer_count = result.wire_shape_id_buffer_count + result.wire_path_buffer_count + result.wire_path_extra_buffer_count + result.wire_point_buffer_count
                                    + result.wire_point_extra_buffer_count + result.wire_via_buffer_count
                                    + result.wire_via_extra_buffer_count + result.wire_rectangle_buffer_count;
  result.wire_nested_usable_bytes = result.wire_shape_id_usable_bytes + result.wire_path_usable_bytes + result.wire_path_extra_usable_bytes + result.wire_point_usable_bytes
                                    + result.wire_point_extra_usable_bytes + result.wire_via_usable_bytes
                                    + result.wire_via_extra_usable_bytes + result.wire_rectangle_usable_bytes;
  const auto wire_nested_capacity_bytes = result.wire_shape_id_capacity_bytes + result.wire_path_capacity_bytes + result.wire_path_extra_capacity_bytes
                                          + result.wire_point_capacity_bytes + result.wire_point_extra_capacity_bytes
                                          + result.wire_via_capacity_bytes + result.wire_via_extra_capacity_bytes
                                          + result.wire_rectangle_capacity_bytes;
  result.wire_nested_usable_overhead_bytes
      = result.wire_nested_usable_bytes > wire_nested_capacity_bytes ? result.wire_nested_usable_bytes - wire_nested_capacity_bytes
                                                                     : 0u;

  for (const auto entity : registry.view<const DesignInstance>()) {
    const auto& instance = registry.get<const DesignInstance>(entity);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(instance.name);
    result.netlist_nested_capacity_bytes += instance.region_bounds.capacity() * sizeof(GeometryShapeId);
  }
  for (const auto entity : registry.view<const DesignInstancePins>()) {
    const auto& pins = registry.get<const DesignInstancePins>(entity);
    using InstanceTermNameIndex = std::remove_cvref_t<decltype(pins.names)>;
    const auto name_capacity_lower_bound = static_cast<uint64_t>(pins.names.size());
    const auto bucket_count = static_cast<uint64_t>(pins.names.bucket_count());
    const auto name_capacity_bytes_lower_bound
        = name_capacity_lower_bound * (sizeof(std::size_t) + sizeof(typename InstanceTermNameIndex::value_type));
    const auto bucket_bytes_lower_bound = bucket_count * sizeof(std::size_t);
    const auto id_vector_capacity_bytes = static_cast<uint64_t>(pins.values.capacity()) * sizeof(DesignInstancePinId);
    result.instance_term_name_index_count += pins.names.size();
    result.instance_term_name_index_capacity_lower_bound += name_capacity_lower_bound;
    result.instance_term_name_index_capacity_bytes_lower_bound += name_capacity_bytes_lower_bound;
    result.instance_term_name_index_bucket_count += bucket_count;
    result.instance_term_name_index_bucket_bytes_lower_bound += bucket_bytes_lower_bound;
    result.instance_term_id_vector_capacity_bytes += id_vector_capacity_bytes;
    result.netlist_nested_capacity_bytes
        += name_capacity_bytes_lower_bound + bucket_bytes_lower_bound + id_vector_capacity_bytes;
    for (const auto& [name, unused] : pins.names) {
      static_cast<void>(unused);
      result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(name);
    }
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
    result.netlist_nested_capacity_bytes += pin.pins.capacity() * sizeof(DesignBlockPinId);
  }
  for (const auto entity : registry.view<const DesignBlockPin>()) {
    const auto& port = registry.get<const DesignBlockPin>(entity);
    result.netlist_nested_capacity_bytes += port.rectangles.capacity() * sizeof(GeometryShapeId);
    result.netlist_nested_capacity_bytes += port.polygons.capacity() * sizeof(GeometryShapeId);
    result.netlist_nested_capacity_bytes += port.vias.capacity() * sizeof(GeometryShapeId);
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
    result.routing_other_nested_capacity_bytes += via.rectangles.capacity() * sizeof(GeometryShapeId);
    result.routing_other_nested_capacity_bytes += via.polygons.capacity() * sizeof(GeometryShapeId);
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
    result.routing_other_nested_capacity_bytes += geometry.shapes.capacity() * sizeof(GeometryShapeId);
  }
  for (const auto entity : registry.view<const DesignNetRouteGuides>()) {
    result.routing_other_nested_capacity_bytes
        += registry.get<const DesignNetRouteGuides>(entity).values.capacity() * sizeof(GeometryShapeId);
  }
  for (const auto entity : registry.view<const DesignNetTrackAssignments>()) {
    result.routing_other_nested_capacity_bytes
        += registry.get<const DesignNetTrackAssignments>(entity).values.capacity() * sizeof(GeometryShapeId);
  }

  // Route and SPECIALNET geometry payloads live in the design's independent
  // GeometryRegistry. Their component pages and entity indexes were counted
  // above. Keep only true nested allocations here. Route attribute strings
  // are already included by routingStatistics().
  for (const auto entity : geometry_registry.view<const DesignGeometryRectangle>()) {
    const auto& rectangle = geometry_registry.get<const DesignGeometryRectangle>(entity);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(rectangle.shield_net);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(rectangle.shape);
  }
  for (const auto entity : geometry_registry.view<const DesignGeometryPolygon>()) {
    const auto& polygon = geometry_registry.get<const DesignGeometryPolygon>(entity);
    result.routing_other_nested_capacity_bytes += polygon.points.capacity() * sizeof(Point);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(polygon.shield_net);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(polygon.shape);
  }
  for (const auto entity : geometry_registry.view<const DesignGeometryNetVia>()) {
    const auto& via = geometry_registry.get<const DesignGeometryNetVia>(entity);
    result.routing_other_nested_capacity_bytes += via.origins.capacity() * sizeof(Point);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(via.shield_net);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(via.shape);
  }
  for (const auto entity : geometry_registry.view<const GeometryPolygon>()) {
    result.other_nested_capacity_bytes += geometry_registry.get<const GeometryPolygon>(entity).points.capacity() * sizeof(Point);
  }
  for (const auto entity : geometry_registry.view<const GeometryBoundary>()) {
    result.other_nested_capacity_bytes += geometry_registry.get<const GeometryBoundary>(entity).points.capacity() * sizeof(Point);
  }
  for (const auto entity : geometry_registry.view<const DesignPinPolygon>()) {
    result.netlist_nested_capacity_bytes += geometry_registry.get<const DesignPinPolygon>(entity).points.capacity() * sizeof(Point);
  }
  for (const auto entity : geometry_registry.view<const DesignViaPolygon>()) {
    result.routing_other_nested_capacity_bytes += geometry_registry.get<const DesignViaPolygon>(entity).points.capacity() * sizeof(Point);
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
    result.other_nested_capacity_bytes += region.rectangles.capacity() * sizeof(GeometryShapeId);
  }
  for (const auto entity : registry.view<const DesignGroup>()) {
    const auto& group = registry.get<const DesignGroup>(entity);
    result.dynamic_string_capacity_estimate_bytes += dynamicStringCapacityBytes(group.name);
    result.other_nested_capacity_bytes += group.instances.capacity() * sizeof(DesignInstanceId);
  }
  for (const auto entity : registry.view<const DesignBlockage>()) {
    const auto& blockage = registry.get<const DesignBlockage>(entity);
    result.other_nested_capacity_bytes += blockage.rectangles.capacity() * sizeof(GeometryShapeId);
    result.other_nested_capacity_bytes += blockage.polygons.capacity() * sizeof(GeometryShapeId);
  }
  for (const auto entity : registry.view<const DesignFill>()) {
    result.other_nested_capacity_bytes += registry.get<const DesignFill>(entity).rectangles.capacity() * sizeof(GeometryShapeId);
  }

  result.attributed_capacity_bytes
      = result.component_payload_capacity_bytes + result.packed_entity_capacity_estimate_bytes
        + result.sparse_entity_extent_estimate_bytes + result.wire_shape_id_capacity_bytes + result.wire_path_capacity_bytes
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
            << ",\"sparse_entity_extent_estimate_bytes\":" << value.sparse_entity_extent_estimate_bytes
            << ",\"wire_component_capacity_bytes\":" << value.wire_component_capacity_bytes
            << ",\"instance_component_capacity_bytes\":" << value.instance_component_capacity_bytes
            << ",\"instance_pin_component_capacity_bytes\":" << value.instance_pin_component_capacity_bytes
            << ",\"instance_term_name_index_count\":" << value.instance_term_name_index_count
            << ",\"instance_term_name_index_capacity_lower_bound\":" << value.instance_term_name_index_capacity_lower_bound
            << ",\"instance_term_name_index_capacity_bytes_lower_bound\":"
            << value.instance_term_name_index_capacity_bytes_lower_bound
            << ",\"instance_term_name_index_bucket_count\":" << value.instance_term_name_index_bucket_count
            << ",\"instance_term_name_index_bucket_bytes_lower_bound\":"
            << value.instance_term_name_index_bucket_bytes_lower_bound
            << ",\"instance_term_id_vector_capacity_bytes\":" << value.instance_term_id_vector_capacity_bytes
            << ",\"net_component_capacity_bytes\":" << value.net_component_capacity_bytes
            << ",\"geometry_bounds_count\":" << value.geometry_bounds_count
            << ",\"geometry_bounds_component_capacity_bytes\":"
            << value.geometry_bounds_component_capacity_bytes
            << ",\"route_shape_count\":" << value.route_shape_count
            << ",\"wire_shape_id_capacity_bytes\":" << value.wire_shape_id_capacity_bytes
            << ",\"wire_shape_id_buffer_count\":" << value.wire_shape_id_buffer_count
            << ",\"wire_shape_id_usable_bytes\":" << value.wire_shape_id_usable_bytes
            << ",\"wire_path_count\":" << value.wire_path_count << ",\"wire_path_capacity\":" << value.wire_path_capacity
            << ",\"wire_path_capacity_bytes\":" << value.wire_path_capacity_bytes
            << ",\"wire_path_buffer_count\":" << value.wire_path_buffer_count
            << ",\"wire_path_usable_bytes\":" << value.wire_path_usable_bytes
            << ",\"wire_path_record_size_bytes\":0"
            << ",\"route_shape_component_size_bytes\":" << sizeof(DesignRouteShape)
            << ",\"wire_path_extra_count\":" << value.wire_path_extra_count
            << ",\"wire_path_extra_capacity\":" << value.wire_path_extra_capacity
            << ",\"wire_path_extra_capacity_bytes\":" << value.wire_path_extra_capacity_bytes
            << ",\"wire_path_extra_buffer_count\":" << value.wire_path_extra_buffer_count
            << ",\"wire_path_extra_usable_bytes\":" << value.wire_path_extra_usable_bytes
            << ",\"wire_point_count\":" << value.wire_point_count << ",\"wire_point_capacity\":" << value.wire_point_capacity
            << ",\"wire_point_capacity_bytes\":" << value.wire_point_capacity_bytes
            << ",\"wire_point_record_size_bytes\":" << sizeof(DesignWirePoint)
            << ",\"wire_point_buffer_count\":" << value.wire_point_buffer_count
            << ",\"wire_point_usable_bytes\":" << value.wire_point_usable_bytes
            << ",\"wire_point_extra_count\":" << value.wire_point_extra_count
            << ",\"wire_point_extra_capacity\":" << value.wire_point_extra_capacity
            << ",\"wire_point_extra_capacity_bytes\":" << value.wire_point_extra_capacity_bytes
            << ",\"wire_point_extra_buffer_count\":" << value.wire_point_extra_buffer_count
            << ",\"wire_point_extra_usable_bytes\":" << value.wire_point_extra_usable_bytes
            << ",\"wire_via_count\":" << value.wire_via_count << ",\"wire_via_capacity\":" << value.wire_via_capacity
            << ",\"wire_via_capacity_bytes\":" << value.wire_via_capacity_bytes
            << ",\"wire_via_record_size_bytes\":" << sizeof(DesignWireVia)
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
