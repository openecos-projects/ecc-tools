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

#include "geometry/GeometryOccurrenceService.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#include "context/DatabaseContext.h"
#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "service/geometry/PhysicalDesignService.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

constexpr uint64_t kNoId = std::numeric_limits<uint64_t>::max();

void includeBounds(GeometryOccurrenceHeader& header, Rect bounds)
{
  if (!header.has_bounds) {
    header.bounds = bounds;
    header.has_bounds = true;
    return;
  }
  header.bounds = header.bounds.united(bounds);
}

class OccurrencePayloadBuilder
{
 public:
  OccurrencePayloadBuilder(const PhysicalDesignService& physical, GeometryOccurrencePayloadBatch& batch)
      : _physical(physical), _batch(batch)
  {
  }

  [[nodiscard]] uint32_t appendHeader(GeometryOccurrenceHeader header)
  {
    if (_batch.headers.size() >= std::numeric_limits<uint32_t>::max()) {
      throw std::overflow_error("geometry occurrence header index exceeds uint32 range");
    }
    _batch.headers.push_back(std::move(header));
    return static_cast<uint32_t>(_batch.headers.size() - 1u);
  }

  [[nodiscard]] PhysicalGeometryVisitor visitor(uint32_t header_index)
  {
    return PhysicalGeometryVisitor{
        .rectangle = [this, header_index](const PhysicalLayerRect& rectangle) {
          appendRectangle(header_index, rectangle);
        },
        .polygon = [this, header_index](const PhysicalLayerPolygon& polygon) {
          appendPolygon(header_index, polygon);
        },
        .via = [this, header_index](const PhysicalViaPlacement& via) {
          appendVia(header_index, via);
        }};
  }

 private:
  void appendRectangle(uint32_t header_index, const PhysicalLayerRect& rectangle)
  {
    _batch.rectangles.push_back(GeometryPayloadRectangle{.header_index = header_index,
                                                         .layer = rectangle.layer.packed(),
                                                         .rectangle = rectangle.rectangle,
                                                         .flags = rectangle.flags,
                                                         .mask = rectangle.mask,
                                                         .spacing = rectangle.spacing,
                                                         .design_rule_width = rectangle.design_rule_width});
    includeBounds(_batch.headers[header_index], rectangle.rectangle);
  }

  void appendPolygon(uint32_t header_index, const PhysicalLayerPolygon& polygon)
  {
    if (_batch.polygon_points.size() > std::numeric_limits<uint32_t>::max()
        || polygon.points.size() > std::numeric_limits<uint32_t>::max() - _batch.polygon_points.size()) {
      throw std::overflow_error("geometry occurrence polygon point count exceeds uint32 range");
    }
    const auto point_offset = static_cast<uint32_t>(_batch.polygon_points.size());
    const auto point_count = static_cast<uint32_t>(polygon.points.size());
    _batch.polygon_points.insert(_batch.polygon_points.end(), polygon.points.begin(), polygon.points.end());
    _batch.polygons.push_back(GeometryPayloadPolygon{.header_index = header_index,
                                                      .layer = polygon.layer.packed(),
                                                      .point_offset = point_offset,
                                                      .point_count = point_count,
                                                      .flags = polygon.flags,
                                                      .mask = polygon.mask,
                                                      .spacing = polygon.spacing,
                                                      .design_rule_width = polygon.design_rule_width});
    if (polygon.points.empty()) return;

    auto bounds = Rect{.ll_x = polygon.points.front().x,
                       .ll_y = polygon.points.front().y,
                       .ur_x = polygon.points.front().x,
                       .ur_y = polygon.points.front().y};
    for (const auto point : polygon.points) {
      bounds.ll_x = std::min(bounds.ll_x, point.x);
      bounds.ll_y = std::min(bounds.ll_y, point.y);
      bounds.ur_x = std::max(bounds.ur_x, point.x);
      bounds.ur_y = std::max(bounds.ur_y, point.y);
    }
    includeBounds(_batch.headers[header_index], bounds);
  }

  void appendVia(uint32_t header_index, const PhysicalViaPlacement& via)
  {
    _batch.vias.push_back(GeometryPayloadVia{.header_index = header_index,
                                              .origin = via.origin,
                                              .tech_via = via.tech_via ? via.tech_via.packed() : kNoId,
                                              .design_via = via.design_via ? via.design_via.packed() : kNoId,
                                              .orientation = static_cast<uint8_t>(via.orientation),
                                              .flags = via.flags,
                                              .top_mask = via.top_mask,
                                              .cut_mask = via.cut_mask,
                                              .bottom_mask = via.bottom_mask,
                                              .exit_layer = kNoId});
    auto via_visitor = visitor(header_index);
    via_visitor.via = {};
    if (via.tech_via) {
      _physical.forEachTechViaGeometry(via.tech_via, via.origin, via.orientation, via_visitor);
    } else {
      _physical.forEachDesignViaGeometry(via.design_via, via.origin, via.orientation, via_visitor);
    }
  }

  const PhysicalDesignService& _physical;
  GeometryOccurrencePayloadBatch& _batch;
};

void appendPlacedInstanceTerm(const DesignNetlistStorage& netlist, const LibraryStore& library,
                              const PhysicalDesignService& physical, DesignInstanceTermId iterm_id,
                              OccurrencePayloadBuilder& builder)
{
  const auto& iterm = netlist.instancePin(iterm_id);
  const auto& term = library.masterTermStorage().masterTerm(iterm.master_term);
  for (const auto master_pin : term.ports) {
    const auto header = builder.appendHeader(
        GeometryOccurrenceHeader{.kind = GeometryOccurrenceKind::kPlacedInstanceMasterPin,
                                 .instance_term = iterm_id.packed(),
                                 .instance = iterm.instance.packed(),
                                 .master_pin = master_pin.packed()});
    physical.forEachPlacedInstanceMasterPinGeometry(iterm_id, master_pin, builder.visitor(header));
  }
}

}  // namespace

GeometryOccurrenceService::GeometryOccurrenceService(DatabaseContext& context)
{
  _technology = context.technology();
  _library = context.library();
  _design = &context.design();
  if (_technology == nullptr || _library == nullptr) {
    throw std::logic_error("geometry occurrence queries require an owning Database with technology and library storage");
  }
}

GeometryOccurrenceService::GeometryOccurrenceService(const TechStore& technology, const LibraryStore& library,
                                                     const DesignStore& design) noexcept
    : _technology(&technology), _library(&library), _design(&design)
{
}

GeometryOccurrencePayloadBatch GeometryOccurrenceService::fetchMasterPin(LibraryMasterPinId pin) const
{
  PhysicalDesignService physical{*_technology, *_library, *_design};
  GeometryOccurrencePayloadBatch result;
  OccurrencePayloadBuilder builder{physical, result};
  const auto header = builder.appendHeader(GeometryOccurrenceHeader{.kind = GeometryOccurrenceKind::kMasterPin,
                                                                      .master_pin = pin.packed()});
  physical.forEachMasterPinGeometry(pin, builder.visitor(header));
  return result;
}

GeometryOccurrencePayloadBatch GeometryOccurrenceService::fetchInstancePin(DesignInstanceTermId pin) const
{
  PhysicalDesignService physical{*_technology, *_library, *_design};
  const auto& netlist = _design->netlistStorage();

  GeometryOccurrencePayloadBatch result;
  OccurrencePayloadBuilder builder{physical, result};
  appendPlacedInstanceTerm(netlist, *_library, physical, pin, builder);
  return result;
}

GeometryOccurrencePayloadBatch GeometryOccurrenceService::fetchInstancePins(
    std::span<const DesignInstanceId> instances) const
{
  PhysicalDesignService physical{*_technology, *_library, *_design};
  const auto& netlist = _design->netlistStorage();

  GeometryOccurrencePayloadBatch result;
  // The instance count is a rough allocation hint; the actual PORT count can
  // differ. Avoid a second traversal solely to count PORTs.
  result.headers.reserve(instances.size());
  OccurrencePayloadBuilder builder{physical, result};
  for (const auto instance : instances) {
    for (const auto iterm : netlist.instanceTerms(instance)) {
      appendPlacedInstanceTerm(netlist, *_library, physical, iterm, builder);
    }
  }
  return result;
}

}  // namespace eccdb
