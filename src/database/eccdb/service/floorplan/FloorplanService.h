// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <span>
#include <string>
#include <vector>

#include "context/DatabaseContext.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "design/netlist/model/NetlistComponents.h"

namespace eccdb {

// Owns floorplan mutations that cross the global, floorplan, netlist,
// geometry-cache, and pin-access stores. Read-only floorplan traversal remains
// on DesignFloorplanStorage.
class FloorplanService
{
 public:
  explicit FloorplanService(DatabaseContext& context) noexcept;

  void setDieArea(DesignDieAreaInput area);
  void setCoreArea(DesignCoreAreaInput area);

  [[nodiscard]] DesignRowId createRow(DesignRow row);
  void updateRow(DesignRowId id, DesignRow row);
  [[nodiscard]] bool destroyRow(DesignRowId id);
  // Preserves IDs for matching row names. Input validation is staged before
  // the installed rows are changed; returned IDs follow the input order.
  [[nodiscard]] std::vector<DesignRowId> replaceRows(std::vector<DesignRow> rows);

  [[nodiscard]] DesignTrackGridId createTrackGrid(DesignTrackGrid grid);
  void updateTrackGrid(DesignTrackGridId id, DesignTrackGrid grid);
  [[nodiscard]] bool destroyTrackGrid(DesignTrackGridId id);
  // Track and gcell statements have no stable DEF name, so successful
  // replacement creates a new collection and retires the old IDs.
  [[nodiscard]] std::vector<DesignTrackGridId> replaceTrackGrids(std::vector<DesignTrackGrid> grids);

  [[nodiscard]] DesignGCellGridId createGCellGrid(DesignGCellGrid grid);
  void updateGCellGrid(DesignGCellGridId id, DesignGCellGrid grid);
  [[nodiscard]] bool destroyGCellGrid(DesignGCellGridId id);
  [[nodiscard]] std::vector<DesignGCellGridId> replaceGCellGrids(std::vector<DesignGCellGrid> grids);

  // Replaces a physical dbBPin in place while retaining its ID and all shape
  // payload metadata. The pin-access candidates derived from the old position
  // are invalidated by the native BlockPin update.
  void placeBlockPin(DesignBlockPinId pin,
                     Point origin,
                     DesignOrientation orientation,
                     DesignPlacementStatus placement_status);

  // Physical dbBPin edits preserve the owning dbBTerm and its Net
  // relationship. A block term can therefore remain logically connected while
  // its PORT geometry is created, edited, or removed.
  [[nodiscard]] DesignBlockPinId createBlockPin(DesignBlockTermId term, DesignBlockPinInput pin);
  void updateBlockPin(DesignBlockPinId pin, DesignBlockPinInput value);
  [[nodiscard]] bool destroyBlockPin(DesignBlockPinId pin);

  // Retires only the selected physical ports. Unselected ports retain their
  // IDs and relative order. New port IDs are returned in input order.
  [[nodiscard]] std::vector<DesignBlockPinId> replaceSelectedBlockPinPorts(
      DesignBlockTermId term,
      std::span<const DesignBlockPinId> retired,
      std::vector<DesignBlockPinInput> replacements);
  // Replaces every physical PORT beneath one surviving BTerm. This supports
  // a deliberate implicit-to-explicit PORT conversion without rebuilding or
  // disconnecting the logical BTerm.
  [[nodiscard]] std::vector<DesignBlockPinId> replaceAllBlockPinPorts(DesignBlockTermId term,
                                                                        std::vector<DesignBlockPinInput> replacements);

  void setInstanceHalo(DesignInstanceId instance, DesignInstanceHalo halo, bool soft);
  void clearInstanceHalo(DesignInstanceId instance);

  // Routing halos are floorplan constraints. Updating one preserves the
  // instance, its master and every Term/Net relationship.
  void setInstanceRouteHalo(DesignInstanceId instance, DesignInstanceRouteHalo halo);
  void clearInstanceRouteHalo(DesignInstanceId instance);

 private:
  [[nodiscard]] DesignStore& design() const noexcept;
  void notifyAfter(DatabaseChangeDomain domains,
                   std::span<const DesignInstanceId> instances = {},
                   std::span<const DesignNetId> nets = {},
                   std::span<const GeometryOwner> replacements = {}) noexcept;
  void invalidateInstanceBounds(DesignInstanceId instance) const;
  void invalidateBlockTermBounds(DesignBlockTermId term) const;
  void invalidateBlockPinBounds(DesignBlockPinId pin) const;
  void updateBlockPinValue(DesignBlockPinId pin, DesignBlockPinInput value);
  [[nodiscard]] std::vector<DesignBlockPinId> replaceBlockPinPorts(DesignBlockTermId term,
                                                                     std::span<const DesignBlockPinId> retired,
                                                                     std::vector<DesignBlockPinInput> replacements);
  [[nodiscard]] std::vector<GeometryOwner> trackAssignmentOwners() const;
  void notifyTracksChanged(std::span<const GeometryOwner> assignments) noexcept;

  DatabaseContext& _context;
};

}  // namespace eccdb
