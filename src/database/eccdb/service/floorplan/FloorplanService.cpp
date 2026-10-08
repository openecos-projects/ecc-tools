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

#include "floorplan/FloorplanService.h"

#include <array>
#include <stdexcept>
#include <utility>
#include <vector>

#include "common/GeometryBounds.h"
#include "design/DesignStore.h"
#include "design/floorplan/storage/DesignFloorplanStorage.h"
#include "design/global/storage/DesignGlobalStorage.h"
#include "design/guide/storage/DesignGuideStorage.h"
#include "design/netlist/storage/DesignNetlistStorage.h"
#include "design/pin_access/storage/DesignPinAccessStorage.h"

namespace eccdb {
namespace {

[[nodiscard]] DesignBlockPinInput blockPinInput(const DesignNetlistStorage& netlist, DesignBlockPinId id)
{
  const auto& pin = netlist.blockPin(id);
  DesignBlockPinInput result{.flags = pin.flags,
                              .placement_status = pin.placement_status,
                              .origin = pin.origin,
                              .orientation = pin.orientation};
  result.rectangles.reserve(pin.rectangles.size());
  result.polygons.reserve(pin.polygons.size());
  result.vias.reserve(pin.vias.size());
  for (const auto shape : pin.rectangles) {
    result.rectangles.push_back(netlist.blockPinRectangle(shape));
  }
  for (const auto shape : pin.polygons) {
    result.polygons.push_back(netlist.blockPinPolygon(shape));
  }
  for (const auto shape : pin.vias) {
    result.vias.push_back(netlist.blockPinVia(shape));
  }
  return result;
}

}  // namespace

FloorplanService::FloorplanService(DatabaseContext& context) noexcept : _context(context) {}

DesignStore& FloorplanService::design() const noexcept
{
  return _context.design();
}

void FloorplanService::notifyAfter(DatabaseChangeDomain domains,
                                   std::span<const DesignInstanceId> instances,
                                   std::span<const DesignNetId> nets,
                                   std::span<const GeometryOwner> replacements) noexcept
{
  _context.notifyAfter(DatabaseChange{.domains = domains, .instances = instances, .nets = nets,
                                     .geometry_replacements = replacements});
}

void FloorplanService::invalidateInstanceBounds(DesignInstanceId instance) const
{
  auto& design_store = design();
  design_store.designRegistry().registry().get_or_emplace<GeometryBounds>(instance.entity()).invalidate();
  design_store.designRegistry().markGeometryChanged();
}

void FloorplanService::invalidateBlockTermBounds(DesignBlockTermId term) const
{
  design().designRegistry().registry().get_or_emplace<GeometryBounds>(term.entity()).invalidate();
}

void FloorplanService::invalidateBlockPinBounds(DesignBlockPinId pin) const
{
  design().designRegistry().registry().get_or_emplace<GeometryBounds>(pin.entity()).invalidate();
}

void FloorplanService::setDieArea(DesignDieAreaInput area)
{
  design().globalStorage().setDieArea(std::move(area));
  notifyAfter(DatabaseChangeDomain::kFloorplan | DatabaseChangeDomain::kGeometry);
}

void FloorplanService::setCoreArea(DesignCoreAreaInput area)
{
  design().floorplanStorage().setCoreArea(std::move(area));
  notifyAfter(DatabaseChangeDomain::kFloorplan | DatabaseChangeDomain::kGeometry);
}

DesignRowId FloorplanService::createRow(DesignRow row)
{
  const auto id = design().floorplanStorage().createRow(std::move(row));
  notifyAfter(DatabaseChangeDomain::kFloorplan);
  return id;
}

void FloorplanService::updateRow(DesignRowId id, DesignRow row)
{
  design().floorplanStorage().updateRow(id, std::move(row));
  notifyAfter(DatabaseChangeDomain::kFloorplan);
}

bool FloorplanService::destroyRow(DesignRowId id)
{
  const auto removed = design().floorplanStorage().destroyRow(id);
  if (removed) {
    notifyAfter(DatabaseChangeDomain::kFloorplan);
  }
  return removed;
}

std::vector<DesignRowId> FloorplanService::replaceRows(std::vector<DesignRow> rows)
{
  const auto result = design().floorplanStorage().replaceRows(std::move(rows));
  notifyAfter(DatabaseChangeDomain::kFloorplan);
  return result;
}

DesignTrackGridId FloorplanService::createTrackGrid(DesignTrackGrid grid)
{
  const auto assignments = trackAssignmentOwners();
  const auto id = design().floorplanStorage().createTrackGrid(std::move(grid));
  notifyTracksChanged(assignments);
  return id;
}

void FloorplanService::updateTrackGrid(DesignTrackGridId id, DesignTrackGrid grid)
{
  const auto assignments = trackAssignmentOwners();
  design().floorplanStorage().updateTrackGrid(id, std::move(grid));
  notifyTracksChanged(assignments);
}

bool FloorplanService::destroyTrackGrid(DesignTrackGridId id)
{
  if (!design().floorplanStorage().contains(id)) return false;
  const auto assignments = trackAssignmentOwners();
  const auto removed = design().floorplanStorage().destroyTrackGrid(id);
  if (removed) {
    notifyTracksChanged(assignments);
  }
  return removed;
}

std::vector<DesignTrackGridId> FloorplanService::replaceTrackGrids(std::vector<DesignTrackGrid> grids)
{
  const auto assignments = trackAssignmentOwners();
  auto& floorplan = design().floorplanStorage();
  const auto old_ids = floorplan.trackGrids();
  std::vector<DesignTrackGridId> staged;
  staged.reserve(grids.size());
  try {
    for (auto& grid : grids) {
      staged.push_back(floorplan.createTrackGrid(std::move(grid)));
    }
  } catch (...) {
    for (const auto id : staged) {
      static_cast<void>(floorplan.destroyTrackGrid(id));
    }
    throw;
  }
  for (const auto id : old_ids) {
    static_cast<void>(floorplan.destroyTrackGrid(id));
  }
  notifyTracksChanged(assignments);
  return staged;
}

std::vector<GeometryOwner> FloorplanService::trackAssignmentOwners() const
{
  const auto view = design().designRegistry().registry().view<const DesignNetTrackAssignments>();
  std::vector<GeometryOwner> owners;
  owners.reserve(view.size());
  for (const auto entity : view) owners.push_back(designTrackAssignmentOwner(DesignNetId{entity}));
  return owners;
}

void FloorplanService::notifyTracksChanged(std::span<const GeometryOwner> assignments) noexcept
{
  // Track-relative PA is invalid only after a successful logical commit.
  // A staged replacement that rolls back leaves both old tracks and PA intact.
  design().pinAccessStorage().invalidateAll();
  for (const auto owner : assignments) {
    design().guideStorage().clearTrackAssignments(DesignNetId{static_cast<DesignEntity>(owner.value)});
  }
  notifyAfter(DatabaseChangeDomain::kFloorplan | DatabaseChangeDomain::kPinAccess
              | DatabaseChangeDomain::kRouting | DatabaseChangeDomain::kGeometry, {}, {}, assignments);
}

DesignGCellGridId FloorplanService::createGCellGrid(DesignGCellGrid grid)
{
  const auto id = design().floorplanStorage().createGCellGrid(std::move(grid));
  notifyAfter(DatabaseChangeDomain::kFloorplan);
  return id;
}

void FloorplanService::updateGCellGrid(DesignGCellGridId id, DesignGCellGrid grid)
{
  design().floorplanStorage().updateGCellGrid(id, std::move(grid));
  notifyAfter(DatabaseChangeDomain::kFloorplan);
}

bool FloorplanService::destroyGCellGrid(DesignGCellGridId id)
{
  const auto removed = design().floorplanStorage().destroyGCellGrid(id);
  if (removed) {
    notifyAfter(DatabaseChangeDomain::kFloorplan);
  }
  return removed;
}

std::vector<DesignGCellGridId> FloorplanService::replaceGCellGrids(std::vector<DesignGCellGrid> grids)
{
  auto& floorplan = design().floorplanStorage();
  const auto old_ids = floorplan.gcellGrids();
  std::vector<DesignGCellGridId> staged;
  staged.reserve(grids.size());
  try {
    for (auto& grid : grids) {
      staged.push_back(floorplan.createGCellGrid(std::move(grid)));
    }
  } catch (...) {
    for (const auto id : staged) {
      static_cast<void>(floorplan.destroyGCellGrid(id));
    }
    throw;
  }
  for (const auto id : old_ids) {
    static_cast<void>(floorplan.destroyGCellGrid(id));
  }
  notifyAfter(DatabaseChangeDomain::kFloorplan);
  return staged;
}

void FloorplanService::placeBlockPin(DesignBlockPinId pin,
                                     Point origin,
                                     DesignOrientation orientation,
                                     DesignPlacementStatus placement_status)
{
  auto& netlist = design().netlistStorage();
  auto input = blockPinInput(netlist, pin);
  input.flags |= DesignIoPinPortFlag::kHasPlacement;
  input.origin = origin;
  input.orientation = orientation;
  input.placement_status = placement_status;
  updateBlockPinValue(pin, std::move(input));
}

DesignBlockPinId FloorplanService::createBlockPin(DesignBlockTermId term, DesignBlockPinInput pin)
{
  const auto id = design().netlistStorage().createBlockPin(term, std::move(pin));
  invalidateBlockTermBounds(term);
  notifyAfter(DatabaseChangeDomain::kFloorplan | DatabaseChangeDomain::kGeometry | DatabaseChangeDomain::kPinAccess);
  return id;
}

void FloorplanService::updateBlockPin(DesignBlockPinId pin, DesignBlockPinInput value)
{
  updateBlockPinValue(pin, std::move(value));
}

bool FloorplanService::destroyBlockPin(DesignBlockPinId pin)
{
  auto& netlist = design().netlistStorage();
  if (!netlist.contains(pin)) {
    return false;
  }
  const auto term = netlist.blockPin(pin).term;
  const std::array replacements{designBlockPinOwner(pin)};
  if (!netlist.destroyBlockPin(pin)) {
    throw std::logic_error("prepared block pin disappeared during physical-port deletion");
  }
  invalidateBlockTermBounds(term);
  notifyAfter(DatabaseChangeDomain::kFloorplan | DatabaseChangeDomain::kGeometry | DatabaseChangeDomain::kPinAccess,
              {}, {}, replacements);
  return true;
}

std::vector<DesignBlockPinId> FloorplanService::replaceSelectedBlockPinPorts(
    DesignBlockTermId term,
    std::span<const DesignBlockPinId> retired,
    std::vector<DesignBlockPinInput> replacements)
{
  return replaceBlockPinPorts(term, retired, std::move(replacements));
}

std::vector<DesignBlockPinId> FloorplanService::replaceAllBlockPinPorts(DesignBlockTermId term,
                                                                          std::vector<DesignBlockPinInput> replacements)
{
  const auto& netlist = design().netlistStorage();
  const auto ports = netlist.blockPins(term);
  const std::vector<DesignBlockPinId> retired{ports.begin(), ports.end()};
  return replaceBlockPinPorts(term, retired, std::move(replacements));
}

void FloorplanService::updateBlockPinValue(DesignBlockPinId pin, DesignBlockPinInput value)
{
  auto& netlist = design().netlistStorage();
  const auto term = netlist.blockPin(pin).term;
  netlist.updateBlockPin(pin, std::move(value));
  invalidateBlockPinBounds(pin);
  invalidateBlockTermBounds(term);
  const std::array replacements{designBlockPinOwner(pin)};
  notifyAfter(DatabaseChangeDomain::kFloorplan | DatabaseChangeDomain::kGeometry | DatabaseChangeDomain::kPinAccess,
              {}, {}, replacements);
}

std::vector<DesignBlockPinId> FloorplanService::replaceBlockPinPorts(DesignBlockTermId term,
                                                                       std::span<const DesignBlockPinId> retired,
                                                                       std::vector<DesignBlockPinInput> replacements)
{
  std::vector<GeometryOwner> owner_replacements;
  owner_replacements.reserve(retired.size());
  for (const auto pin : retired) {
    owner_replacements.push_back(designBlockPinOwner(pin));
  }

  auto ids = design().netlistStorage().replaceBlockPins(term, retired, std::move(replacements));
  invalidateBlockTermBounds(term);
  notifyAfter(DatabaseChangeDomain::kFloorplan | DatabaseChangeDomain::kGeometry | DatabaseChangeDomain::kPinAccess,
              {}, {}, owner_replacements);
  return ids;
}

void FloorplanService::setInstanceHalo(DesignInstanceId instance, DesignInstanceHalo halo, bool soft)
{
  auto& netlist = design().netlistStorage();
  auto value = netlist.instance(instance);
  value.halo = halo;
  value.flags |= DesignInstanceFlag::kHasHalo;
  if (soft) {
    value.flags |= DesignInstanceFlag::kHaloSoft;
  } else {
    value.flags &= ~DesignInstanceFlag::kHaloSoft;
  }
  netlist.updateInstance(instance, std::move(value));
  invalidateInstanceBounds(instance);
  const std::array instances{instance};
  notifyAfter(DatabaseChangeDomain::kInstances | DatabaseChangeDomain::kFloorplan | DatabaseChangeDomain::kGeometry, instances);
}

void FloorplanService::clearInstanceHalo(DesignInstanceId instance)
{
  auto& netlist = design().netlistStorage();
  auto value = netlist.instance(instance);
  value.halo = {};
  value.flags &= ~(DesignInstanceFlag::kHasHalo | DesignInstanceFlag::kHaloSoft);
  netlist.updateInstance(instance, std::move(value));
  invalidateInstanceBounds(instance);
  const std::array instances{instance};
  notifyAfter(DatabaseChangeDomain::kInstances | DatabaseChangeDomain::kFloorplan | DatabaseChangeDomain::kGeometry, instances);
}

}  // namespace eccdb
