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

#include "NativeEccdbImporter.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <boost/polygon/polygon.hpp>
#include <entt/container/dense_map.hpp>

#include "db/obj/frAccess.h"
#include "db/obj/frBPin.h"
#include "db/obj/frBTerm.h"
#include "db/obj/frBlock.h"
#include "db/obj/frBlockage.h"
#include "db/obj/frBoundary.h"
#include "db/obj/frInst.h"
#include "db/obj/frInstBlockage.h"
#include "db/obj/frInstTerm.h"
#include "db/obj/frMPin.h"
#include "db/obj/frMTerm.h"
#include "db/obj/frMaster.h"
#include "db/obj/frNet.h"
#include "db/obj/frShape.h"
#include "db/obj/frTrackPattern.h"
#include "db/obj/frVia.h"
#include "db/tech/frLayer.h"
#include "db/tech/frTechObject.h"
#include "db/tech/frViaDef.h"
#include "design/DesignStore.h"
#include "design/constraint/model/ConstraintComponents.h"
#include "design/constraint/storage/DesignConstraintStorage.h"
#include "design/floorplan/storage/DesignFloorplanStorage.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/netlist/storage/DesignNetlistStorage.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/routing/storage/DesignRoutingStorage.h"
#include "design/routing/view/WireRoutingView.h"
#include "drt-global.h"
#include "frDesign.h"
#include "geometry/PolygonRectDecomposer.h"
#include "library/LibraryStore.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/cell_master/model/MasterObsComponents.h"
#include "library/master_port/model/MasterPortComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "odb/dbTypes.h"
#include "odb/geom.h"
#include "service/geometry/PhysicalDesignService.h"
#include "tech/TechStore.h"
#include "utl/Logger.h"

namespace drt::io {
namespace {

static_assert(std::numeric_limits<int>::digits >= std::numeric_limits<int32_t>::digits,
              "EccDB coordinates require a 32-bit DRT coordinate type");

constexpr int kNativeImportErrorBase = 640;

[[noreturn]] void fail(utl::Logger& logger, int offset, std::string_view message)
{
  logger.error(utl::DRT, kNativeImportErrorBase + offset, "{}", std::string{message});
}

template <typename Id>
[[nodiscard]] uint32_t entityKey(Id id)
{
  return static_cast<uint32_t>(id.packed());
}

[[nodiscard]] uint64_t instanceTermKey(eccdb::DesignInstancePinId id)
{
  return static_cast<uint64_t>(id.packed());
}

// EnTT's view iteration follows dense storage order, which can differ after
// component insertion from the DEF/LEF object creation order. The packed ID is
// the stable source-order key for the append-only import data.
template <typename Id>
[[nodiscard]] std::vector<Id> orderedIds(std::vector<Id> ids)
{
  std::sort(ids.begin(), ids.end(), [](Id lhs, Id rhs) { return lhs.packed() < rhs.packed(); });
  return ids;
}

[[nodiscard]] odb::Point toOdb(eccdb::Point point)
{
  return {static_cast<int>(point.x), static_cast<int>(point.y)};
}

[[nodiscard]] odb::Rect toOdb(eccdb::Rect rectangle)
{
  return {static_cast<int>(rectangle.ll_x), static_cast<int>(rectangle.ll_y), static_cast<int>(rectangle.ur_x),
          static_cast<int>(rectangle.ur_y)};
}

[[nodiscard]] int checkedCoordinate(int64_t value, utl::Logger& logger, std::string_view description)
{
  if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
    fail(logger, 1, std::string{description} + " exceeds DRT's coordinate range");
  }
  return static_cast<int>(value);
}

[[nodiscard]] int checkedMasterExtent(uint32_t value, utl::Logger& logger, std::string_view description)
{
  if (value > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    fail(logger, 2, std::string{description} + " exceeds DRT's coordinate range");
  }
  return static_cast<int>(value);
}

[[nodiscard]] int checkedNegatedCoordinate(int64_t value, utl::Logger& logger, std::string_view description)
{
  if (value == std::numeric_limits<int64_t>::min()) {
    fail(logger, 2, std::string{description} + " cannot be negated");
  }
  return checkedCoordinate(-value, logger, description);
}

[[nodiscard]] int checkedPlacementUpper(uint32_t extent,
                                         int64_t origin,
                                         utl::Logger& logger,
                                         std::string_view description)
{
  const auto extent64 = static_cast<int64_t>(extent);
  if (origin < std::numeric_limits<int64_t>::min() + extent64) {
    fail(logger, 2, std::string{description} + " overflows");
  }
  return checkedCoordinate(extent64 - origin, logger, description);
}

[[nodiscard]] int checkedId(std::size_t value, utl::Logger& logger, std::string_view description)
{
  if (value > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    fail(logger, 3, std::string{description} + " exceeds DRT's object-id range");
  }
  return static_cast<int>(value);
}

[[nodiscard]] odb::dbOrientType toDbOrient(eccdb::DesignOrientation orientation, utl::Logger& logger)
{
  switch (orientation) {
    case eccdb::DesignOrientation::kN:
      return odb::dbOrientType::R0;
    case eccdb::DesignOrientation::kS:
      return odb::dbOrientType::R180;
    case eccdb::DesignOrientation::kE:
      return odb::dbOrientType::R270;
    case eccdb::DesignOrientation::kW:
      return odb::dbOrientType::R90;
    case eccdb::DesignOrientation::kFN:
      return odb::dbOrientType::MY;
    case eccdb::DesignOrientation::kFS:
      return odb::dbOrientType::MX;
    case eccdb::DesignOrientation::kFE:
      return odb::dbOrientType::MYR90;
    case eccdb::DesignOrientation::kFW:
      return odb::dbOrientType::MXR90;
  }
  fail(logger, 4, "EccDB has an invalid placement orientation");
}

[[nodiscard]] odb::dbSigType toDbSigType(eccdb::LibraryMasterTermUse use, utl::Logger& logger)
{
  switch (use) {
    case eccdb::LibraryMasterTermUse::kNone:
    case eccdb::LibraryMasterTermUse::kSignal:
      return odb::dbSigType::SIGNAL;
    case eccdb::LibraryMasterTermUse::kAnalog:
      return odb::dbSigType::ANALOG;
    case eccdb::LibraryMasterTermUse::kPower:
      return odb::dbSigType::POWER;
    case eccdb::LibraryMasterTermUse::kGround:
      return odb::dbSigType::GROUND;
    case eccdb::LibraryMasterTermUse::kClock:
      return odb::dbSigType::CLOCK;
    case eccdb::LibraryMasterTermUse::kTieOff:
      return odb::dbSigType::TIEOFF;
    case eccdb::LibraryMasterTermUse::kScan:
      return odb::dbSigType::SCAN;
    case eccdb::LibraryMasterTermUse::kReset:
      return odb::dbSigType::RESET;
  }
  fail(logger, 5, "EccDB has an invalid master terminal USE");
}

[[nodiscard]] odb::dbSigType toDbSigType(eccdb::DesignSignalUse use, utl::Logger& logger)
{
  switch (use) {
    case eccdb::DesignSignalUse::kNone:
    case eccdb::DesignSignalUse::kSignal:
      return odb::dbSigType::SIGNAL;
    case eccdb::DesignSignalUse::kAnalog:
      return odb::dbSigType::ANALOG;
    case eccdb::DesignSignalUse::kPower:
      return odb::dbSigType::POWER;
    case eccdb::DesignSignalUse::kGround:
      return odb::dbSigType::GROUND;
    case eccdb::DesignSignalUse::kClock:
      return odb::dbSigType::CLOCK;
    case eccdb::DesignSignalUse::kTieOff:
      return odb::dbSigType::TIEOFF;
    case eccdb::DesignSignalUse::kScan:
      return odb::dbSigType::SCAN;
    case eccdb::DesignSignalUse::kReset:
      return odb::dbSigType::RESET;
  }
  fail(logger, 6, "EccDB has an invalid design signal USE");
}

[[nodiscard]] odb::dbMasterType toDbMasterType(eccdb::LibraryCellMasterType type, utl::Logger& logger)
{
  switch (type) {
    case eccdb::LibraryCellMasterType::kNone:
    case eccdb::LibraryCellMasterType::kCore:
      return odb::dbMasterType::CORE;
    case eccdb::LibraryCellMasterType::kCover:
      return odb::dbMasterType::COVER;
    case eccdb::LibraryCellMasterType::kCoverBump:
      return odb::dbMasterType::COVER_BUMP;
    case eccdb::LibraryCellMasterType::kRing:
      return odb::dbMasterType::RING;
    case eccdb::LibraryCellMasterType::kBlock:
      return odb::dbMasterType::BLOCK;
    case eccdb::LibraryCellMasterType::kBlockBlackbox:
      return odb::dbMasterType::BLOCK_BLACKBOX;
    case eccdb::LibraryCellMasterType::kBlockSoft:
      return odb::dbMasterType::BLOCK_SOFT;
    case eccdb::LibraryCellMasterType::kPad:
      return odb::dbMasterType::PAD;
    case eccdb::LibraryCellMasterType::kPadInput:
      return odb::dbMasterType::PAD_INPUT;
    case eccdb::LibraryCellMasterType::kPadOutput:
      return odb::dbMasterType::PAD_OUTPUT;
    case eccdb::LibraryCellMasterType::kPadInOut:
      return odb::dbMasterType::PAD_INOUT;
    case eccdb::LibraryCellMasterType::kPadPower:
      return odb::dbMasterType::PAD_POWER;
    case eccdb::LibraryCellMasterType::kPadSpacer:
      return odb::dbMasterType::PAD_SPACER;
    case eccdb::LibraryCellMasterType::kPadAreaIo:
      return odb::dbMasterType::PAD_AREAIO;
    case eccdb::LibraryCellMasterType::kCoreFeedThru:
      return odb::dbMasterType::CORE_FEEDTHRU;
    case eccdb::LibraryCellMasterType::kCoreTieHigh:
      return odb::dbMasterType::CORE_TIEHIGH;
    case eccdb::LibraryCellMasterType::kCoreTieLow:
      return odb::dbMasterType::CORE_TIELOW;
    case eccdb::LibraryCellMasterType::kCoreSpacer:
      return odb::dbMasterType::CORE_SPACER;
    case eccdb::LibraryCellMasterType::kCoreAntennaCell:
      return odb::dbMasterType::CORE_ANTENNACELL;
    case eccdb::LibraryCellMasterType::kCoreWelltap:
      return odb::dbMasterType::CORE_WELLTAP;
    case eccdb::LibraryCellMasterType::kEndcap:
      return odb::dbMasterType::ENDCAP;
    case eccdb::LibraryCellMasterType::kEndcapPre:
      return odb::dbMasterType::ENDCAP_PRE;
    case eccdb::LibraryCellMasterType::kEndcapPost:
      return odb::dbMasterType::ENDCAP_POST;
    case eccdb::LibraryCellMasterType::kEndcapTopLeft:
      return odb::dbMasterType::ENDCAP_TOPLEFT;
    case eccdb::LibraryCellMasterType::kEndcapTopRight:
      return odb::dbMasterType::ENDCAP_TOPRIGHT;
    case eccdb::LibraryCellMasterType::kEndcapBottomLeft:
      return odb::dbMasterType::ENDCAP_BOTTOMLEFT;
    case eccdb::LibraryCellMasterType::kEndcapBottomRight:
      return odb::dbMasterType::ENDCAP_BOTTOMRIGHT;
  }
  fail(logger, 7, "EccDB has an invalid library master type");
}

[[nodiscard]] frLayerNum requireFrLayer(const eccdb::PhysicalDesignService& physical,
                                        const frDesign& design,
                                        eccdb::TechLayerId layer,
                                        utl::Logger& logger,
                                        std::string_view object)
{
  const auto kind = physical.layerKind(layer);
  if (kind != eccdb::GeometryLayerKind::kRouting && kind != eccdb::GeometryLayerKind::kCut) {
    fail(logger, 8, std::string{object} + " uses a non-routing/non-cut layer");
  }
  const auto& info = physical.layerInfo(layer);
  const auto* technology = design.getTech();
  if (technology == nullptr) {
    fail(logger, 9, "DRT technology must be loaded before EccDB geometry");
  }
  const auto* fr_layer = technology->getLayer(info.name);
  if (fr_layer == nullptr) {
    fail(logger, 10, std::string{object} + " references DRT-missing layer " + info.name);
  }
  const auto layer_number = fr_layer->getLayerNum();
  if (layer_number < 0 || static_cast<std::size_t>(layer_number) >= technology->getLayers().size()) {
    fail(logger, 10, std::string{object} + " references an invalid DRT layer number");
  }
  return layer_number;
}

template <typename Pin>
void appendPinRectangle(Pin& pin, frLayerNum layer, eccdb::Rect rectangle)
{
  auto figure = std::make_unique<frRect>();
  figure->setBBox(toOdb(rectangle));
  figure->addToPin(&pin);
  figure->setLayerNum(layer);
  std::unique_ptr<frPinFig> pin_figure(std::move(figure));
  pin.addPinFig(std::move(pin_figure));
}

[[nodiscard]] std::unique_ptr<frBlockage> makeBlockage(frLayerNum layer,
                                                        eccdb::Rect rectangle,
                                                        std::optional<int32_t> design_rule_width = std::nullopt)
{
  auto blockage = std::make_unique<frBlockage>();
  if (design_rule_width.has_value()) {
    blockage->setDesignRuleWidth(*design_rule_width);
  }
  auto pin = std::make_unique<frBPin>();
  pin->setId(0);
  appendPinRectangle(*pin, layer, rectangle);
  blockage->setPin(std::move(pin));
  return blockage;
}

template <typename Function>
void forEachGeometryRectangle(const eccdb::DomainGeometryStorage& storage,
                              std::span<const eccdb::GeometryShapeId> geometry,
                              Function&& function)
{
  for (const auto id : geometry) {
    const auto kind = storage.geometryRegistry().shape(id).kind;
    if (kind == eccdb::GeometryShapeKind::kRectangle) {
      function(storage.rectangle(id).rectangle);
    } else if (kind == eccdb::GeometryShapeKind::kPolygon) {
      for (const auto rectangle : eccdb::decomposePolygonToRectangles(storage.polygonPoints(id))) {
        function(rectangle);
      }
    }
  }
}

template <typename Function>
void forEachExpandedVia(const eccdb::PhysicalDesignService& physical,
                        eccdb::TechViaMasterId tech_via,
                        eccdb::DesignViaId design_via,
                        eccdb::Point origin,
                        eccdb::DesignOrientation orientation,
                        utl::Logger& logger,
                        Function&& function)
{
  if (static_cast<bool>(tech_via) == static_cast<bool>(design_via)) {
    fail(logger, 11, "EccDB VIA placement must reference exactly one VIA definition");
  }

  eccdb::PhysicalGeometryVisitor visitor;
  visitor.rectangle = [&function](const eccdb::PhysicalLayerRect& rectangle) {
    function(rectangle.layer, rectangle.rectangle, rectangle.mask);
  };
  visitor.polygon = [&function](const eccdb::PhysicalLayerPolygon& polygon) {
    for (const auto rectangle : eccdb::decomposePolygonToRectangles(polygon.points)) {
      function(polygon.layer, rectangle, polygon.mask);
    }
  };
  visitor.via = [&logger](const eccdb::PhysicalViaPlacement&) {
    fail(logger, 13, "nested VIA geometry is unsupported by native TritonRoute import");
  };
  if (tech_via) {
    physical.forEachTechViaGeometry(tech_via, origin, orientation, visitor);
  } else {
    physical.forEachDesignViaGeometry(design_via, origin, orientation, visitor);
  }
}

[[nodiscard]] const frViaDef* requireFrViaDefinition(const eccdb::PhysicalDesignService& physical,
                                                      const frDesign& design,
                                                      eccdb::TechViaMasterId tech_via,
                                                      eccdb::DesignViaId design_via,
                                                      utl::Logger& logger,
                                                      std::string_view object)
{
  const auto via_name = physical.viaName(tech_via, design_via);
  const auto* technology = design.getTech();
  if (technology == nullptr) {
    fail(logger, 63, "DRT technology must be loaded before EccDB VIA placement");
  }
  for (const auto& candidate : technology->getVias()) {
    if (candidate->getName() == via_name) {
      return candidate.get();
    }
  }
  fail(logger, 64, std::string{object} + " references DRT-missing VIA " + std::string{via_name});
}

[[nodiscard]] eccdb::Point checkedOffsetPoint(eccdb::Point origin,
                                               int64_t offset_x,
                                               int64_t offset_y,
                                               utl::Logger& logger,
                                               std::string_view object)
{
  return {.x = checkedCoordinate(static_cast<int64_t>(origin.x) + offset_x, logger, object),
          .y = checkedCoordinate(static_cast<int64_t>(origin.y) + offset_y, logger, object)};
}

[[nodiscard]] std::optional<int32_t> pointExtension(eccdb::DesignWirePoint point)
{
  if ((point.flags & eccdb::DesignWirePointFlag::kHasExtension) == 0u) {
    return std::nullopt;
  }
  return point.extension;
}

void setPathSegStyle(frPathSeg& segment,
                     frUInt4 width,
                     std::optional<int32_t> begin_extension,
                     std::optional<int32_t> end_extension,
                     bool truncated,
                     utl::Logger& logger,
                     std::string_view object)
{
  if (width <= 0) {
    fail(logger, 65, std::string{object} + " has a non-positive route width");
  }
  const auto make_style = [&](std::optional<int32_t> extension) {
    if (truncated) {
      return std::pair{frEndStyle{frcTruncateEndStyle}, frUInt4{0}};
    }
    if (extension.has_value()) {
      if (*extension < 0) {
        fail(logger, 66, std::string{object} + " has a negative route endpoint extension");
      }
      return std::pair{frEndStyle{frcVariableEndStyle}, static_cast<frUInt4>(*extension)};
    }
    return std::pair{frEndStyle{frcExtendEndStyle}, static_cast<frUInt4>(width / 2)};
  };
  const auto [begin_style, begin_extent] = make_style(begin_extension);
  const auto [end_style, end_extent] = make_style(end_extension);
  frSegStyle style;
  style.setWidth(width);
  style.setBeginStyle(begin_style, begin_extent);
  style.setEndStyle(end_style, end_extent);
  segment.setStyle(style);
}

void appendRouteSegment(frNet& net,
                        frLayerNum layer,
                        eccdb::Point begin,
                        eccdb::Point end,
                        frUInt4 width,
                        std::optional<int32_t> begin_extension,
                        std::optional<int32_t> end_extension,
                        bool truncated,
                        bool tapered,
                        utl::Logger& logger,
                        std::string_view object)
{
  const int64_t delta_x = static_cast<int64_t>(end.x) - begin.x;
  const int64_t delta_y = static_cast<int64_t>(end.y) - begin.y;
  if ((delta_x == 0 && delta_y == 0)
      || (delta_x != 0 && delta_y != 0 && std::llabs(delta_x) != std::llabs(delta_y))) {
    fail(logger, 67, std::string{object} + " contains a non-Manhattan/non-45-degree route segment");
  }

  // DRT's ODB reader normalizes its regular path endpoints before assigning
  // styles. Keeping that convention gives deterministic route snapshots.
  if (begin.x > end.x || begin.y > end.y) {
    std::swap(begin, end);
    std::swap(begin_extension, end_extension);
  }
  auto segment = std::make_unique<frPathSeg>();
  segment->setPoints(toOdb(begin), toOdb(end));
  segment->setLayerNum(layer);
  segment->setTapered(tapered);
  setPathSegStyle(*segment, width, begin_extension, end_extension, truncated, logger, object);
  net.addShape(std::move(segment));
}

void appendRoutePatch(frNet& net,
                      frLayerNum layer,
                      eccdb::Point anchor,
                      eccdb::Rect delta,
                      utl::Logger& logger,
                      std::string_view object)
{
  if (delta.ll_x >= delta.ur_x || delta.ll_y >= delta.ur_y) {
    fail(logger, 68, std::string{object} + " has a degenerate patch rectangle");
  }
  auto patch = std::make_unique<frPatchWire>();
  patch->setLayerNum(layer);
  patch->setOrigin(toOdb(anchor));
  patch->setOffsetBox(toOdb(delta));
  net.addPatchWire(std::move(patch));
}

[[nodiscard]] frUInt4 checkedRouteWidth(int64_t width, utl::Logger& logger, std::string_view object)
{
  if (width <= 0 || static_cast<uint64_t>(width) > std::numeric_limits<frUInt4>::max()) {
    fail(logger, 70, std::string{object} + " has a route width outside DRT's supported range");
  }
  return static_cast<frUInt4>(width);
}

[[nodiscard]] frLayer& requireFrRoutingLayer(const eccdb::PhysicalDesignService& physical,
                                              const frDesign& design,
                                              eccdb::TechRoutingLayerId layer,
                                              utl::Logger& logger,
                                              std::string_view object)
{
  const auto number = requireFrLayer(physical, design, eccdb::TechLayerId{layer.entity()}, logger, object);
  const auto* technology = design.getTech();
  if (technology == nullptr) {
    fail(logger, 71, "DRT technology must be loaded before EccDB route geometry");
  }
  auto* result = technology->getLayer(number);
  if (result == nullptr || !result->isRoutable()) {
    fail(logger, 72, std::string{object} + " references a DRT-missing routing layer");
  }
  return *result;
}

[[nodiscard]] frUInt4 implicitRouteWidth(const frLayer& layer, eccdb::Point begin, eccdb::Point end,
                                          utl::Logger& logger, std::string_view object)
{
  const auto width = (layer.isHorizontal() && begin.y != end.y) || (!layer.isHorizontal() && begin.x != end.x)
                         ? layer.getWrongDirWidth()
                         : layer.getWidth();
  return checkedRouteWidth(width, logger, object);
}

void appendRouteVia(const eccdb::PhysicalDesignService& physical,
                    const frDesign& design,
                    frNet& net,
                    eccdb::DesignWireVia via,
                    eccdb::Point origin,
                    eccdb::TechRoutingLayerId& active_layer,
                    utl::Logger& logger,
                    std::string_view object)
{
  if (via.orientation != eccdb::DesignOrientation::kN) {
    fail(logger, 73, std::string{object} + " has an oriented VIA, which DRT frVia cannot represent");
  }

  eccdb::PhysicalRoutingViaLayers layers;
  try {
    layers = physical.routingViaLayers(via.tech_via, via.design_via);
  } catch (const std::exception& error) {
    fail(logger, 74, std::string{object} + " has a non-projectable VIA: " + error.what());
  }
  if (active_layer == layers.bottom) {
    active_layer = layers.top;
  } else if (active_layer == layers.top) {
    active_layer = layers.bottom;
  } else {
    fail(logger, 75, std::string{object} + " VIA does not touch the active routing layer");
  }

  // ODB stores all array members as individual SBoxes in column-major order;
  // DRT likewise owns one frVia per concrete placement.
  const uint32_t columns = (via.flags & eccdb::DesignWireViaFlag::kHasArray) != 0u ? via.columns : 1u;
  const uint32_t rows = (via.flags & eccdb::DesignWireViaFlag::kHasArray) != 0u ? via.rows : 1u;
  const int64_t step_x = (via.flags & eccdb::DesignWireViaFlag::kHasArray) != 0u ? via.step_x : 0;
  const int64_t step_y = (via.flags & eccdb::DesignWireViaFlag::kHasArray) != 0u ? via.step_y : 0;
  const auto* definition = requireFrViaDefinition(physical, design, via.tech_via, via.design_via, logger, object);
  for (uint32_t column = 0; column < columns; ++column) {
    for (uint32_t row = 0; row < rows; ++row) {
      const auto placement = checkedOffsetPoint(origin,
                                                static_cast<int64_t>(column) * step_x,
                                                static_cast<int64_t>(row) * step_y,
                                                logger,
                                                object);
      net.addVia(std::make_unique<frVia>(definition, toOdb(placement)));
    }
  }
}

void appendWireShape(const eccdb::PhysicalDesignService& physical,
                    const frDesign& design,
                    frNet& net,
                    bool special,
                    const eccdb::DesignRouteShape& shape,
                    const eccdb::DesignRouteShapeAttributes* attributes,
                    bool& has_route_geometry,
                    utl::Logger& logger,
                    std::string_view object)
{
  constexpr uint32_t kUnsupportedFlags = eccdb::DesignWirePathFlag::kHasTaperRule | eccdb::DesignWirePathFlag::kHasStyle;
  if ((shape.flags & kUnsupportedFlags) != 0u) {
    fail(logger, 76, std::string{object} + " has a TAPERRULE or STYLE qualifier that DRT cannot represent");
  }
  const bool has_width = (shape.flags & eccdb::DesignWirePathFlag::kHasWidth) != 0u;
  if (has_width != special) {
    fail(logger, 77, std::string{object} + " has NET/SPECIALNET-incompatible route width metadata");
  }
  const auto require_physical_point = [&](eccdb::DesignWirePoint point) {
    if ((point.flags & eccdb::DesignWirePointFlag::kVirtual) != 0u) {
      // OpenDB's regular dbWire codec preserves VIRTUAL as a separate edge,
      // while frNet has no equivalent edge kind.  Treating it as copper would
      // invent physical geometry, so fail instead.
      fail(logger, 79, std::string{object} + " has a VIRTUAL route point that DRT frNet cannot represent");
    }
  };

  // ODB DRT consumes neither route-layer nor VIA mask coloring when it
  // materializes frPathSeg/frVia. Preserve the same projection rather than
  // rejecting an otherwise routable masked path.
  const bool tapered = (shape.flags & eccdb::DesignWirePathFlag::kTaper) != 0u;
  const bool truncated = special && (shape.flags & eccdb::DesignWirePathFlag::kHasShape) != 0u
                         && attributes != nullptr && !attributes->shape.empty();
  const auto& layer = requireFrRoutingLayer(physical, design, shape.layer, logger, object);
  std::visit([&](const auto& geometry) {
    using Geometry = std::decay_t<decltype(geometry)>;
    if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteSegment>) {
      require_physical_point(geometry.begin);
      require_physical_point(geometry.end);
      const auto width = special ? checkedRouteWidth(attributes == nullptr ? 0 : attributes->width, logger, object)
                                : implicitRouteWidth(layer, geometry.begin.position, geometry.end.position, logger, object);
      appendRouteSegment(net,
                         layer.getLayerNum(),
                         geometry.begin.position,
                         geometry.end.position,
                         width,
                         pointExtension(geometry.begin),
                         pointExtension(geometry.end),
                         truncated,
                         tapered,
                         logger,
                         object);
      has_route_geometry = true;
    } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRoutePatch>) {
      require_physical_point(geometry.anchor);
      // Patch geometry is absolute. DRT accepts an origin plus offset box;
      // using origin zero avoids reconstructing a path-relative rectangle.
      appendRoutePatch(net, layer.getLayerNum(), {}, geometry.rectangle, logger, object);
      has_route_geometry = true;
    } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteVia>) {
      require_physical_point(geometry.origin);
      auto active_layer = shape.layer;
      appendRouteVia(physical, design, net, geometry.via, geometry.origin.position, active_layer, logger, object);
      has_route_geometry = true;
    } else {
      require_physical_point(geometry.point);
    }
  }, shape.geometry);
}

void appendSpecialRectangle(const eccdb::PhysicalDesignService& physical,
                            const frDesign& design,
                            frNet& net,
                            const eccdb::DesignGeometryRectangle& rectangle,
                            bool& has_route_geometry,
                            utl::Logger& logger)
{
  const int64_t dx = static_cast<int64_t>(rectangle.rectangle.ur_x) - rectangle.rectangle.ll_x;
  const int64_t dy = static_cast<int64_t>(rectangle.rectangle.ur_y) - rectangle.rectangle.ll_y;
  if (dx <= 0 || dy <= 0) {
    fail(logger, 80, "SPECIALNET RECT has no area");
  }

  eccdb::Point begin{.x = rectangle.rectangle.ll_x, .y = rectangle.rectangle.ll_y};
  eccdb::Point end{.x = rectangle.rectangle.ur_x, .y = rectangle.rectangle.ur_y};
  frUInt4 width = 0;
  const bool dx_even = (dx & 1) == 0;
  const bool dy_even = (dy & 1) == 0;
  if (dx_even && dy_even) {
    if (dy < dx) {
      width = checkedRouteWidth(dy, logger, "SPECIALNET RECT");
      begin.y = checkedCoordinate(static_cast<int64_t>(begin.y) + dy / 2, logger, "SPECIALNET RECT centerline");
      end.y = begin.y;
    } else {
      width = checkedRouteWidth(dx, logger, "SPECIALNET RECT");
      begin.x = checkedCoordinate(static_cast<int64_t>(begin.x) + dx / 2, logger, "SPECIALNET RECT centerline");
      end.x = begin.x;
    }
  } else if (dx_even) {
    width = checkedRouteWidth(dx, logger, "SPECIALNET RECT");
    begin.x = checkedCoordinate(static_cast<int64_t>(begin.x) + dx / 2, logger, "SPECIALNET RECT centerline");
    end.x = begin.x;
  } else if (dy_even) {
    width = checkedRouteWidth(dy, logger, "SPECIALNET RECT");
    begin.y = checkedCoordinate(static_cast<int64_t>(begin.y) + dy / 2, logger, "SPECIALNET RECT centerline");
    end.y = begin.y;
  } else {
    fail(logger, 81, "SPECIALNET RECT has odd dimensions in both directions and cannot form a DRT centerline");
  }

  const auto& layer = requireFrRoutingLayer(physical, design, rectangle.layer, logger, "SPECIALNET RECT");
  const bool truncated = (rectangle.flags & eccdb::DesignNetGeometryFlag::kHasShape) != 0u;
  // DRT's ODB import ignores the SBox mask and route-status metadata after
  // OpenDB has stored it. The same is true here: the physical rectangle and
  // SHAPE-derived endpoint behavior are retained, but no color/status field
  // exists on frPathSeg.
  appendRouteSegment(net, layer.getLayerNum(), begin, end, width, std::nullopt, std::nullopt, truncated, false, logger,
                     "SPECIALNET RECT");
  has_route_geometry = true;
}

void appendSpecialStandaloneGeometry(const eccdb::PhysicalDesignService& physical,
                                     const frDesign& design,
                                     frNet& net,
                                     eccdb::DesignNetId net_id,
                                     bool& has_route_geometry,
                                     utl::Logger& logger)
{
  eccdb::PhysicalNetGeometryVisitor visitor;
  visitor.rectangle = [&](const eccdb::DesignGeometryRectangle& rectangle) {
    appendSpecialRectangle(physical, design, net, rectangle, has_route_geometry, logger);
  };
  visitor.polygon = [&](const eccdb::DesignGeometryPolygon&) {
    // OpenDB's DRT reader rejects SPECIALNET polygons before creating a
    // dbSBox.  Decomposing one here would create a different route model.
    fail(logger, 82, "SPECIALNET POLYGON is unsupported by the ODB-compatible DRT projection");
  };
  visitor.via = [&](const eccdb::DesignGeometryNetVia& via) {
    if (via.orientation != eccdb::DesignOrientation::kN) {
      fail(logger, 83, "SPECIALNET standalone VIA has an orientation DRT frVia cannot represent");
    }
    if ((via.flags & eccdb::DesignNetGeometryFlag::kHasShape) != 0u) {
      fail(logger, 84, "SPECIALNET standalone VIA SHAPE is unsupported by DRT frVia");
    }
    const auto* definition = requireFrViaDefinition(physical, design, via.tech_via, via.design_via, logger,
                                                     "SPECIALNET standalone VIA");
    // As with path VIAs, DRT's ODB projection does not retain color masks.
    for (const auto origin : via.origins) {
      net.addVia(std::make_unique<frVia>(definition, toOdb(origin)));
      has_route_geometry = true;
    }
  };
  physical.forEachNetGeometry(net_id, visitor);
}

struct PinShape
{
  odb::Rect rectangle;
  frMPin* pin = nullptr;
};

using PinShapesByLayer = std::vector<std::vector<PinShape>>;

void appendMasterPinRectangle(frMPin& pin,
                              PinShapesByLayer& shapes,
                              frLayerNum layer,
                              eccdb::Rect rectangle,
                              bool remember)
{
  appendPinRectangle(pin, layer, rectangle);
  if (remember) {
    shapes.at(static_cast<std::size_t>(layer)).push_back(PinShape{.rectangle = toOdb(rectangle), .pin = &pin});
  }
}

void appendMasterPort(const eccdb::PhysicalDesignService& physical,
                      const frDesign& design,
                      const eccdb::LibraryMasterPort& port,
                      frMPin& pin,
                      PinShapesByLayer& shapes,
                      utl::Logger& logger)
{
  const auto& library = physical.library();
  for (const auto& clause : port.layer_clauses) {
    const auto layer = requireFrLayer(physical, design, clause.layer, logger, "library PORT geometry");
    forEachGeometryRectangle(library.geometryStorage(), clause.geometry, [&](const eccdb::Rect rectangle) {
      appendMasterPinRectangle(pin, shapes, layer, rectangle, true);
    });
  }
  for (const auto shape : port.vias) {
    const auto& via = library.geometryRegistry().get<eccdb::LibraryViaPlacement>(shape);
    forEachExpandedVia(physical, via.via, {}, via.origin, eccdb::DesignOrientation::kN, logger,
                        [&](eccdb::TechLayerId layer_id, eccdb::Rect rectangle, uint32_t mask) {
                          // ODB retains LEF VIA masks, but DRT's frPinFig has
                          // no color field.  Match its geometry projection.
                          static_cast<void>(mask);
                          const auto layer = requireFrLayer(physical, design, layer_id, logger, "library PORT VIA geometry");
                          appendMasterPinRectangle(pin, shapes, layer, rectangle, true);
                        });
  }
}

void appendMasterBlockage(frMaster& master,
                          frLayerNum layer,
                          eccdb::Rect rectangle,
                          std::optional<int32_t> design_rule_width,
                          int& next_blockage_id)
{
  auto blockage = makeBlockage(layer, rectangle, design_rule_width);
  blockage->setId(next_blockage_id++);
  master.addBlockage(std::move(blockage));
}

[[nodiscard]] bool moveCutObstructionIntoPin(frLayerNum layer,
                                              eccdb::Rect rectangle,
                                              PinShapesByLayer& shapes)
{
  const auto target_layer = static_cast<std::size_t>(layer) + 1u;
  if (target_layer >= shapes.size()) {
    return false;
  }

  frMPin* owner = nullptr;
  const auto box = toOdb(rectangle);
  for (const auto& candidate : shapes[target_layer]) {
    if (!candidate.rectangle.intersects(box)) {
      continue;
    }
    if (owner == nullptr) {
      owner = candidate.pin;
    } else if (owner != candidate.pin) {
      return false;
    }
  }
  if (owner == nullptr) {
    return false;
  }
  appendPinRectangle(*owner, layer, rectangle);
  return true;
}

void appendMasterObstructions(const eccdb::PhysicalDesignService& physical,
                              const frDesign& design,
                              eccdb::LibraryCellMasterId master_id,
                              frMaster& master,
                              PinShapesByLayer& pin_shapes,
                              utl::Logger& logger)
{
  const auto& library = physical.library();
  const auto& masters = library.cellMasterStorage();
  if (!masters.hasObs(master_id)) {
    return;
  }

  namespace gtl = boost::polygon;
  std::vector<gtl::polygon_90_set_data<frCoord>> unioned(design.getTech()->getLayers().size());
  int next_blockage_id = 0;

  const auto append_rectangle = [&](eccdb::TechLayerId layer_id,
                                    eccdb::Rect rectangle,
                                    std::optional<int32_t> design_rule_width) {
    const auto layer = requireFrLayer(physical, design, layer_id, logger, "library OBS geometry");
    if (physical.layerKind(layer_id) == eccdb::GeometryLayerKind::kCut
        && moveCutObstructionIntoPin(layer, rectangle, pin_shapes)) {
      return;
    }
    if (design_rule_width.has_value()) {
      appendMasterBlockage(master, layer, rectangle, design_rule_width, next_blockage_id);
      return;
    }
    using gtl::operators::operator+=;
    unioned.at(static_cast<std::size_t>(layer)) += gtl::rectangle_data<frCoord>(rectangle.ll_x, rectangle.ll_y, rectangle.ur_x,
                                                                                rectangle.ur_y);
  };

  const auto& obstruction = masters.obs(master_id);
  for (const auto& clause : obstruction.layer_clauses) {
    constexpr uint32_t kSupportedFlags = eccdb::LibraryObsLayerFlag::kHasDesignRuleWidth;
    if ((clause.flags & ~kSupportedFlags) != 0u) {
      fail(logger, 15, "native TritonRoute import cannot represent an OBS spacing, path-width, or EXCEPTPGNET qualifier");
    }
    const auto width = (clause.flags & eccdb::LibraryObsLayerFlag::kHasDesignRuleWidth) != 0u
                           ? std::optional<int32_t>{clause.design_rule_width}
                           : std::nullopt;
    forEachGeometryRectangle(library.geometryStorage(), clause.geometry,
                             [&](const eccdb::Rect rectangle) { append_rectangle(clause.layer, rectangle, width); });
  }
  for (const auto shape : obstruction.vias) {
    const auto& via = library.geometryRegistry().get<eccdb::LibraryViaPlacement>(shape);
    forEachExpandedVia(physical, via.via, {}, via.origin, eccdb::DesignOrientation::kN, logger,
                        [&](eccdb::TechLayerId layer_id, eccdb::Rect rectangle, uint32_t mask) {
                          static_cast<void>(mask);
                          append_rectangle(layer_id, rectangle, std::nullopt);
                        });
  }

  for (std::size_t layer = 0; layer < unioned.size(); ++layer) {
    std::vector<gtl::polygon_90_with_holes_data<frCoord>> polygons;
    unioned[layer].get(polygons);
    for (const auto& polygon : polygons) {
      std::vector<gtl::rectangle_data<frCoord>> rectangles;
      gtl::get_max_rectangles(rectangles, polygon);
      for (const auto& rectangle : rectangles) {
        appendMasterBlockage(master,
                             checkedId(layer, logger, "master OBS layer number"),
                             eccdb::Rect{.ll_x = gtl::xl(rectangle),
                                         .ll_y = gtl::yl(rectangle),
                                         .ur_x = gtl::xh(rectangle),
                                         .ur_y = gtl::yh(rectangle)},
                             std::nullopt,
                             next_blockage_id);
      }
    }
  }
}

[[nodiscard]] std::unique_ptr<frMaster> buildMaster(const eccdb::PhysicalDesignService& physical,
                                                     const frDesign& design,
                                                     eccdb::LibraryCellMasterId master_id,
                                                     const eccdb::LibraryCellMaster& source,
                                                     int master_id_in_drt,
                                                     utl::Logger& logger)
{
  if (source.name.empty()) {
    fail(logger, 17, "EccDB library master has an empty name");
  }

  auto master = std::make_unique<frMaster>(source.name);
  master->setId(master_id_in_drt);
  master->setMasterType(toDbMasterType(source.type, logger));

  const auto width = checkedMasterExtent(source.width, logger, "library master width");
  const auto height = checkedMasterExtent(source.height, logger, "library master height");
  // OpenDB's placement boundary starts at (0, 0), then applies the LEF
  // origin translation. frInst's native DB transform cache uses this box to
  // recover the raw ODB placement transform for rotations and mirrors.
  const auto lower_x = checkedNegatedCoordinate(source.origin_x, logger, "library master origin x");
  const auto lower_y = checkedNegatedCoordinate(source.origin_y, logger, "library master origin y");
  const auto upper_x = checkedPlacementUpper(source.width, source.origin_x, logger, "library master upper x");
  const auto upper_y = checkedPlacementUpper(source.height, source.origin_y, logger, "library master upper y");
  if (width == 0 || height == 0 || upper_x <= lower_x || upper_y <= lower_y) {
    fail(logger, 18, "EccDB library master has an invalid boundary");
  }
  frBoundary boundary;
  boundary.setPoints({odb::Point{lower_x, lower_y}, odb::Point{upper_x, lower_y}, odb::Point{upper_x, upper_y},
                      odb::Point{lower_x, upper_y}});
  master->setBoundaries({boundary});

  PinShapesByLayer pin_shapes(design.getTech()->getLayers().size());
  const auto& library = physical.library();
  const auto& terms = library.masterTermStorage();
  const auto& ports = library.masterPortStorage();
  for (std::size_t term_index = 0; term_index < source.terms.size(); ++term_index) {
    const auto term_id = source.terms[term_index];
    const auto& source_term = terms.masterTerm(term_id);
    if (source_term.master != master_id || source_term.name.empty()) {
      fail(logger, 19, "EccDB library master contains an invalid terminal relation");
    }
    auto term = std::make_unique<frMTerm>(source_term.name);
    term->setId(checkedId(term_index, logger, "master terminal id"));
    term->setType(toDbSigType(source_term.use, logger));
    for (std::size_t port_index = 0; port_index < source_term.ports.size(); ++port_index) {
      const auto port_id = source_term.ports[port_index];
      const auto& source_port = ports.masterPort(port_id);
      if (source_port.term != term_id) {
        fail(logger, 20, "EccDB library master terminal contains an invalid PORT relation");
      }
      auto pin = std::make_unique<frMPin>();
      pin->setId(checkedId(port_index, logger, "master terminal PORT id"));
      appendMasterPort(physical, design, source_port, *pin, pin_shapes, logger);
      term->addPin(std::move(pin));
    }
    master->addTerm(std::move(term));
  }

  appendMasterObstructions(physical, design, master_id, *master, pin_shapes, logger);
  return master;
}

void setDieArea(const eccdb::PhysicalDesignService& physical, frBlock& block, utl::Logger& logger)
{
  if (!physical.hasDesignInfo()) {
    fail(logger, 21, "EccDB design information is absent");
  }
  if (!physical.hasDieArea()) {
    fail(logger, 22, "EccDB design DIEAREA is absent");
  }
  const auto dbu = physical.databaseUnitsPerMicron();
  if (dbu <= 0) {
    fail(logger, 23, "EccDB database units per micron must be positive");
  }
  const auto die_boundary = physical.dieBoundary();
  const auto bounds = physical.dieBounds();
  uint8_t corners = 0;
  for (const auto point : die_boundary) {
    if ((point.x != bounds.ll_x && point.x != bounds.ur_x) || (point.y != bounds.ll_y && point.y != bounds.ur_y)) {
      continue;
    }
    const auto corner = static_cast<uint8_t>((point.x == bounds.ur_x ? 1u : 0u) | (point.y == bounds.ur_y ? 2u : 0u));
    corners |= static_cast<uint8_t>(1u << corner);
  }
  const bool rectangle = die_boundary.size() == 2u || (die_boundary.size() == 4u && corners == 0x0fu);
  if (!rectangle) {
    fail(logger, 24, "rectilinear DIEAREA polygons are unsupported by native TritonRoute import");
  }
  frBoundary boundary;
  boundary.setPoints({odb::Point{bounds.ll_x, bounds.ll_y}, odb::Point{bounds.ur_x, bounds.ur_y},
                      odb::Point{bounds.ur_x, bounds.ll_y}, odb::Point{bounds.ll_x, bounds.ur_y}});
  block.setDBUPerUU(static_cast<frUInt4>(dbu));
  block.setBoundaries({boundary});
}

void setTracks(const eccdb::PhysicalDesignService& physical, const frDesign& design, frBlock& block, utl::Logger& logger)
{
  const auto& floorplan = physical.design().floorplanStorage();
  for (const auto id : orderedIds(floorplan.trackGrids())) {
    const auto& grid = floorplan.trackGrid(id);
    if (grid.flags != 0u || grid.layers.empty() || grid.track_count == 0u || grid.step <= 0) {
      fail(logger, 25, "EccDB TRACKS statement has unsupported qualifiers or invalid dimensions");
    }
    const bool horizontal = grid.axis == eccdb::DesignAxis::kX;
    if (!horizontal && grid.axis != eccdb::DesignAxis::kY) {
      fail(logger, 26, "EccDB TRACKS statement has an invalid axis");
    }
    for (const auto layer_id : grid.layers) {
      const auto layer = requireFrLayer(physical, design, eccdb::TechLayerId{layer_id.entity()}, logger, "TRACKS");
      block.addTrackPattern(std::make_unique<frTrackPattern>(horizontal,
                                                              grid.start,
                                                              static_cast<frUInt4>(grid.track_count),
                                                              static_cast<frUInt4>(grid.step),
                                                              layer));
    }
  }
}

void setRoutingBlockages(const eccdb::PhysicalDesignService& physical, const frDesign& design, frBlock& block, utl::Logger& logger)
{
  const auto& constraints = physical.design().constraintStorage();
  for (const auto id : orderedIds(constraints.blockages())) {
    const auto& blockage = constraints.blockage(id);
    if (blockage.kind != eccdb::DesignBlockageKind::kRouting) {
      continue;
    }
    constexpr uint32_t kSupportedFlags = eccdb::DesignBlockageFlag::kHasLayer | eccdb::DesignBlockageFlag::kHasMask;
    if ((blockage.flags & ~kSupportedFlags) != 0u) {
      fail(logger, 27, "native TritonRoute import cannot represent qualified routing BLOCKAGE geometry");
    }
    eccdb::PhysicalGeometryVisitor visitor;
    visitor.rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
      if (rectangle.spacing != 0 || rectangle.design_rule_width != 0) {
        fail(logger, 28, "native TritonRoute import cannot represent routing BLOCKAGE shape attributes");
      }
      const auto layer = requireFrLayer(physical, design, rectangle.layer, logger, "routing BLOCKAGE");
      block.addBlockage(makeBlockage(layer, rectangle.rectangle));
    };
    visitor.polygon = [&](const eccdb::PhysicalLayerPolygon& polygon) {
      if (polygon.spacing != 0 || polygon.design_rule_width != 0) {
        fail(logger, 29, "native TritonRoute import cannot represent routing BLOCKAGE polygon attributes");
      }
      const auto layer = requireFrLayer(physical, design, polygon.layer, logger, "routing BLOCKAGE");
      for (const auto rectangle : eccdb::decomposePolygonToRectangles(polygon.points)) {
        block.addBlockage(makeBlockage(layer, rectangle));
      }
    };
    visitor.via = [&](const eccdb::PhysicalViaPlacement&) {
      fail(logger, 30, "routing BLOCKAGE VIA geometry is unsupported by native TritonRoute import");
    };
    physical.forEachRoutingBlockageGeometry(id, visitor);
  }
}

struct ImportedObjects
{
  entt::dense_map<uint64_t, frInstTerm*> instance_terms;
  entt::dense_map<uint32_t, frBTerm*> io_terms;
  entt::dense_map<uint32_t, frNet*> nets;
};

void setInstances(const eccdb::PhysicalDesignService& physical,
                  frDesign& design,
                  frBlock& block,
                  ImportedObjects& objects,
                  utl::Logger& logger)
{
  const auto& netlist = physical.design().netlistStorage();
  const auto& library = physical.library();
  const auto& masters = library.cellMasterStorage();
  for (const auto instance_id : orderedIds(netlist.instances())) {
    const auto& source = netlist.instance(instance_id);
    const auto source_pins = netlist.instancePins(instance_id);
    if (source.name.empty() || block.findInst(source.name) != nullptr) {
      fail(logger, 31, "EccDB design contains a duplicate or empty instance name");
    }
    const auto& source_master = masters.cellMaster(source.master);
    auto* master = design.findMaster(source_master.name);
    if (master == nullptr) {
      fail(logger, 32, "EccDB instance references a library master absent from DRT");
    }
    if (source_pins.size() != source_master.terms.size() || master->getTerms().size() != source_master.terms.size()) {
      fail(logger, 33, "EccDB instance terminal order differs from its DRT master");
    }

    auto instance = std::make_unique<frInst>(source.name, master, nullptr);
    instance->setOrigin(toOdb(source.origin));
    instance->setOrient(toDbOrient(source.orientation, logger));
    for (std::size_t index = 0; index < source_pins.size(); ++index) {
      const auto pin_id = source_pins[index];
      const auto& source_pin = netlist.instancePin(pin_id);
      if (source_pin.instance != instance_id || source_pin.master_term != source_master.terms[index]) {
        fail(logger, 34, "EccDB instance-pin order is corrupt");
      }
      auto term = std::make_unique<frInstTerm>(instance.get(), master->getTerms()[index].get());
      term->setIndexInOwner(checkedId(index, logger, "instance terminal index"));
      term->setAPSize(checkedId(master->getTerms()[index]->getPins().size(), logger, "instance terminal PORT count"));
      const auto [unused, inserted] = objects.instance_terms.emplace(instanceTermKey(pin_id), term.get());
      if (!inserted) {
        fail(logger, 35, "EccDB instance-pin id is duplicated");
      }
      instance->addInstTerm(std::move(term));
    }
    for (const auto& blockage : master->getBlockages()) {
      instance->addInstBlockage(std::make_unique<frInstBlockage>(instance.get(), blockage.get()));
    }
    block.addInst(std::move(instance));
  }
}

void setIoTerms(const eccdb::PhysicalDesignService& physical,
                const frDesign& design,
                frBlock& block,
                ImportedObjects& objects,
                utl::Logger& logger)
{
  const auto& netlist = physical.design().netlistStorage();
  for (const auto pin_id : orderedIds(netlist.blockTerms())) {
    const auto& source = netlist.blockTerm(pin_id);
    if (source.name.empty() || block.getTerm(source.name) != nullptr) {
      fail(logger, 36, "EccDB design contains a duplicate or empty IO pin name");
    }
    auto term = std::make_unique<frBTerm>(source.name);
    const auto type = toDbSigType(source.use, logger);
    term->setType(type);
    const auto block_pins = physical.blockPins(pin_id);
    // Match io::Parser::setBTerms: DRT rejects signal BTerms with more than
    // one dbBPin, while POWER/GROUND BTerms retain one frBPin whose geometry
    // is the union of every source dbBPin. EccDB keeps those BPin entities
    // separate; this is only a DRT projection boundary.
    if (!type.isSupply() && block_pins.size() > 1u) {
      fail(logger, 37, "native TritonRoute import does not support multiple BPin objects on a signal IO terminal");
    }

    auto pin = std::make_unique<frBPin>();
    pin->setId(0);
    eccdb::PhysicalGeometryVisitor visitor;
    visitor.rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
      constexpr uint32_t kUnsupportedShapeFlags = eccdb::DesignPinShapeFlag::kHasSpacing | eccdb::DesignPinShapeFlag::kHasDesignRuleWidth;
      if ((rectangle.flags & kUnsupportedShapeFlags) != 0u || rectangle.spacing != 0 || rectangle.design_rule_width != 0) {
        fail(logger, 38, "native TritonRoute import cannot represent IO-pin shape attributes");
      }
      appendPinRectangle(*pin, requireFrLayer(physical, design, rectangle.layer, logger, "IO pin geometry"), rectangle.rectangle);
    };
    visitor.polygon = [&](const eccdb::PhysicalLayerPolygon& polygon) {
      constexpr uint32_t kUnsupportedShapeFlags = eccdb::DesignPinShapeFlag::kHasSpacing | eccdb::DesignPinShapeFlag::kHasDesignRuleWidth;
      if ((polygon.flags & kUnsupportedShapeFlags) != 0u || polygon.spacing != 0 || polygon.design_rule_width != 0) {
        fail(logger, 39, "native TritonRoute import cannot represent IO-pin polygon attributes");
      }
      const auto layer = requireFrLayer(physical, design, polygon.layer, logger, "IO pin geometry");
      for (const auto rectangle : eccdb::decomposePolygonToRectangles(polygon.points)) {
        appendPinRectangle(*pin, layer, rectangle);
      }
    };
    visitor.via = [&](const eccdb::PhysicalViaPlacement& via) {
      if ((via.flags & ~eccdb::DesignPinViaFlag::kHasMask) != 0u) {
        fail(logger, 40, "native TritonRoute import cannot represent unknown IO-pin VIA attributes");
      }
      forEachExpandedVia(physical, via.tech_via, via.design_via, via.origin, via.orientation, logger,
                          [&](eccdb::TechLayerId layer_id, eccdb::Rect rectangle, uint32_t mask) {
                            static_cast<void>(mask);
                            appendPinRectangle(*pin,
                                               requireFrLayer(physical, design, layer_id, logger, "IO pin VIA geometry"),
                                               rectangle);
                          });
    };
    if (type.isSupply()) {
      physical.forEachBlockPin(pin_id, [&](eccdb::DesignBlockPinId block_pin, const eccdb::DesignBlockPin&) {
        physical.forEachBlockPinGeometry(block_pin, visitor);
      });
    } else if (!block_pins.empty()) {
      physical.forEachBlockPinGeometry(block_pins.front(), visitor);
    }
    pin->addPinAccess(std::make_unique<frPinAccess>());
    term->addPin(std::move(pin));
    auto* raw_term = term.get();
    const auto [unused, inserted] = objects.io_terms.emplace(entityKey(pin_id), raw_term);
    if (!inserted) {
      fail(logger, 42, "EccDB IO pin id is duplicated");
    }
    block.addTerm(std::move(term));
  }
}

void validateNet(const eccdb::PhysicalDesignService& physical,
                 eccdb::DesignNetId id,
                 const eccdb::DesignNet& source,
                 bool special,
                 utl::Logger& logger)
{
  constexpr uint32_t kSupportedFlags = eccdb::DesignNetFlag::kHasNonDefaultRule;
  if (source.name.empty() || (source.flags & ~kSupportedFlags) != 0u) {
    fail(logger, 43, "native TritonRoute import cannot represent an empty-name, weighted, FIXEDBUMP, or MUSTJOIN net");
  }
  const auto ndr_name = physical.nonDefaultRuleName(id);
  if (((source.flags & eccdb::DesignNetFlag::kHasNonDefaultRule) != 0u) != ndr_name.has_value()) {
    fail(logger, 44, "EccDB NET non-default-rule flag and reference are inconsistent");
  }
  if (physical.design().netlistStorage().netOptions(id) != nullptr) {
    fail(logger, 45, "native TritonRoute import does not support NET option payloads");
  }
  bool has_standalone_geometry = false;
  eccdb::PhysicalNetGeometryVisitor geometry;
  geometry.rectangle = [&](const eccdb::DesignGeometryRectangle&) { has_standalone_geometry = true; };
  geometry.polygon = [&](const eccdb::DesignGeometryPolygon&) { has_standalone_geometry = true; };
  geometry.via = [&](const eccdb::DesignGeometryNetVia&) { has_standalone_geometry = true; };
  physical.forEachNetGeometry(id, geometry);
  if (!special && has_standalone_geometry) {
    fail(logger, 46, "standalone routing geometry is only valid on a SPECIALNET");
  }
  const auto type = toDbSigType(source.use, logger);
  if (!special && (type == odb::dbSigType::POWER || type == odb::dbSigType::GROUND)) {
    fail(logger, 47, "POWER and GROUND nets must be represented as SPECIALNETS");
  }
}

[[nodiscard]] std::vector<eccdb::DesignNetId> orderedRegularNetIds(const eccdb::DesignNetlistStorage& netlist)
{
  const auto ids = orderedIds(netlist.nets());
  std::vector<eccdb::DesignNetId> result;
  entt::dense_map<uint32_t, bool> seen;
  result.reserve(ids.size());
  seen.reserve(ids.size());

  // OpenDB creates each PIN's regular net while parsing the PINS section,
  // before the later NETS statements. EccDB defers that association, so
  // rebuild the observable dbNet table order here.
  for (const auto pin_id : orderedIds(netlist.ioPins())) {
    const auto net = netlist.ioPin(pin_id).net;
    if (net && !netlist.isSpecialNet(net) && seen.emplace(entityKey(net), true).second) {
      result.push_back(net);
    }
  }
  for (const auto id : ids) {
    if (!netlist.isSpecialNet(id) && seen.emplace(entityKey(id), true).second) {
      result.push_back(id);
    }
  }
  return result;
}

[[nodiscard]] std::vector<eccdb::DesignNetId> orderedSpecialNetIds(const eccdb::DesignNetlistStorage& netlist)
{
  return orderedIds(netlist.specialNets());
}

[[nodiscard]] std::vector<eccdb::DesignNetId> orderedAllNetIds(const eccdb::DesignNetlistStorage& netlist)
{
  auto result = orderedRegularNetIds(netlist);
  const auto special = orderedSpecialNetIds(netlist);
  result.insert(result.end(), special.begin(), special.end());
  return result;
}

void attachNonDefaultRule(const eccdb::PhysicalDesignService& physical,
                          frDesign& design,
                          eccdb::DesignNetId id,
                          frNet& net,
                          utl::Logger& logger)
{
  const auto name = physical.nonDefaultRuleName(id);
  if (!name.has_value()) {
    return;
  }
  auto* technology = design.getTech();
  if (technology == nullptr) {
    fail(logger, 85, "DRT technology must be loaded before EccDB NET non-default rules");
  }
  auto* rule = technology->getNondefaultRule(std::string{*name});
  if (rule == nullptr) {
    fail(logger, 86, "EccDB NET references a non-default rule absent from DRT technology: " + std::string{*name});
  }
  net.updateNondefaultRule(rule);
}

void validateWireStatus(const eccdb::DesignWire& wire, bool special, utl::Logger& logger)
{
  switch (wire.status) {
    case eccdb::DesignWireStatus::kRouted:
    case eccdb::DesignWireStatus::kFixed:
      return;
    case eccdb::DesignWireStatus::kCover:
    case eccdb::DesignWireStatus::kShield:
    case eccdb::DesignWireStatus::kNoShield:
      if (special) {
        // DRT's ODB import projects every SPECIALNET SBox to geometry and
        // retains neither the SBox route status nor its SHIELD target.
        // Keeping the geometry is therefore the compatible projection.
        return;
      }
      fail(logger, 87, "regular NET routing must be ROUTED or FIXED for DRT");
  }
  fail(logger, 88, "EccDB wire has an invalid routing status");
}

void appendNetRouting(const eccdb::PhysicalDesignService& physical,
                      const frDesign& design,
                      eccdb::DesignNetId id,
                      bool special,
                      frNet& net,
                      utl::Logger& logger)
{
  bool has_routed_wire = false;
  bool has_fixed_wire = false;
  physical.forEachNetWire(id, [&](eccdb::DesignWireId, const eccdb::DesignWire& wire) {
    if (wire.net != id) {
      fail(logger, 89, "EccDB route wire belongs to a different NET");
    }
    validateWireStatus(wire, special, logger);
    has_routed_wire |= wire.status == eccdb::DesignWireStatus::kRouted;
    has_fixed_wire |= wire.status == eccdb::DesignWireStatus::kFixed;
  });
  if (!special && has_routed_wire && has_fixed_wire) {
    fail(logger, 90, "DRT cannot preserve a regular NET with both ROUTED and FIXED wires");
  }

  bool has_route_geometry = false;
  physical.forEachNetWireShape(id, [&](eccdb::DesignWireId, const eccdb::DesignWire&,
                                     eccdb::DesignRouteShapeId, const eccdb::DesignRouteShape& shape,
                                     const eccdb::DesignRouteShapeAttributes* attributes) {
    appendWireShape(physical, design, net, special, shape, attributes, has_route_geometry, logger,
                   special ? "SPECIALNET route" : "NET route");
  });
  if (special) {
    appendSpecialStandaloneGeometry(physical, design, net, id, has_route_geometry, logger);
  }
  if (!special) {
    net.setFixed(has_fixed_wire);
  }
  net.setHasInitialRouting(has_route_geometry);
}

void setNets(const eccdb::PhysicalDesignService& physical,
             frDesign& design,
             frBlock& block,
             RouterConfiguration& configuration,
             ImportedObjects& objects,
             utl::Logger& logger)
{
  const auto& netlist = physical.design().netlistStorage();
  const auto create = [&](eccdb::DesignNetId id, bool special) {
    const auto& source = netlist.net(id);
    validateNet(physical, id, source, special, logger);
    if (block.findNet(source.name) != nullptr) {
      fail(logger, 91, "EccDB design contains duplicate NET or SPECIALNET names");
    }
    auto net = std::make_unique<frNet>(source.name, &configuration);
    net->setType(toDbSigType(source.use, logger));
    if (source.use == eccdb::DesignSignalUse::kClock) {
      net->updateIsClock(true);
    }
    attachNonDefaultRule(physical, design, id, *net, logger);
    if (special) {
      net->setIsSpecial(true);
    }
    auto* raw_net = net.get();
    const auto [unused, inserted] = objects.nets.emplace(entityKey(id), raw_net);
    if (!inserted) {
      fail(logger, 92, "EccDB NET id is duplicated");
    }
    if (special) {
      block.addSNet(std::move(net));
    } else {
      block.addNet(std::move(net));
    }
  };

  for (const auto id : orderedRegularNetIds(netlist)) {
    create(id, false);
  }
  for (const auto id : orderedSpecialNetIds(netlist)) {
    create(id, true);
  }
  // EccdbRouteAdapter owns ordinary NET route projection and runs immediately
  // after this design import.  Keeping that work out of this phase avoids
  // decoding the same route twice and lets the route adapter handle the full
  // ordinary-wire vocabulary.  SPECIALNET geometry has no equivalent route
  // adapter yet, so it is materialized directly here.
  for (const auto id : orderedSpecialNetIds(netlist)) {
    const auto found = objects.nets.find(entityKey(id));
    if (found == objects.nets.end()) {
      fail(logger, 93, "EccDB SPECIALNET was not materialized before route import");
    }
    appendNetRouting(physical, design, id, true, *found->second, logger);
  }
}

using IoPinsByNet = entt::dense_map<uint32_t, std::vector<eccdb::DesignIoPinId>>;

[[nodiscard]] eccdb::DesignNetId connectedNet(eccdb::DesignNetId regular,
                                               eccdb::DesignNetId special,
                                               utl::Logger& logger,
                                               std::string_view object)
{
  if (regular && special) {
    fail(logger, 94, std::string{object} + " is connected to both a regular NET and a SPECIALNET");
  }
  return regular ? regular : special;
}

[[nodiscard]] IoPinsByNet indexIoPinsByNet(const eccdb::DesignNetlistStorage& netlist, utl::Logger& logger)
{
  IoPinsByNet result;
  result.reserve(netlist.netCount());
  for (const auto pin_id : orderedIds(netlist.ioPins())) {
    const auto& pin = netlist.ioPin(pin_id);
    const auto net = connectedNet(pin.net, pin.special_net, logger, "IO pin");
    if (!net) {
      continue;
    }
    const auto [found, unused] = result.emplace(entityKey(net), std::vector<eccdb::DesignIoPinId>{});
    found->second.push_back(pin_id);
  }
  return result;
}

void connectNets(const eccdb::PhysicalDesignService& physical, ImportedObjects& objects, utl::Logger& logger)
{
  const auto& netlist = physical.design().netlistStorage();
  const auto io_pins_by_net = indexIoPinsByNet(netlist, logger);
  for (const auto net_id : orderedAllNetIds(netlist)) {
    const auto net_found = objects.nets.find(entityKey(net_id));
    if (net_found == objects.nets.end()) {
      fail(logger, 95, "EccDB NET was not materialized before connectivity import");
    }
    auto* net = net_found->second;
    std::vector<eccdb::DesignInstancePinId> instance_pins;
    std::vector<eccdb::DesignIoPinId> reverse_io_pins;
    eccdb::PhysicalNetPinVisitor visitor;
    visitor.instance_pin = [&](eccdb::DesignInstancePinId pin_id,
                                const eccdb::DesignInstancePin& pin,
                                const eccdb::DesignInstance&,
                                const eccdb::LibraryMasterTerm& term) {
      if (connectedNet(pin.net, pin.special_net, logger, "instance pin") != net_id) {
        fail(logger, 96, "EccDB NET reverse instance-pin relation is inconsistent");
      }
      if (toDbSigType(term.use, logger).isSupply() && !net->getType().isSupply()) {
        fail(logger, 97, "a supply instance pin cannot connect to a non-supply NET");
      }
      instance_pins.push_back(pin_id);
    };
    visitor.io_pin = [&](eccdb::DesignIoPinId pin_id, const eccdb::DesignIoPin& pin) {
      if (connectedNet(pin.net, pin.special_net, logger, "IO pin") != net_id) {
        fail(logger, 98, "EccDB NET reverse IO-pin relation is inconsistent");
      }
      reverse_io_pins.push_back(pin_id);
    };
    physical.forEachNetPin(net_id, visitor);

    std::vector<eccdb::DesignIoPinId> expected_io_pins;
    if (const auto io_found = io_pins_by_net.find(entityKey(net_id)); io_found != io_pins_by_net.end()) {
      expected_io_pins = io_found->second;
    }
    const auto by_id = [](auto left, auto right) { return left.packed() < right.packed(); };
    auto reverse_io_membership = reverse_io_pins;
    auto expected_io_membership = expected_io_pins;
    std::sort(reverse_io_membership.begin(), reverse_io_membership.end(), by_id);
    std::sort(expected_io_membership.begin(), expected_io_membership.end(), by_id);
    if (reverse_io_membership != expected_io_membership) {
      fail(logger, 99, "EccDB NET reverse IO-pin membership is inconsistent");
    }

    // dbBTerm creation during PINS prepends to OpenDB's net list. EccDB's
    // net-local relation may also contain later NETS connections, so preserve
    // the global PINS order first, then traverse it backwards.
    if (!expected_io_pins.empty()) {
      for (auto pin = expected_io_pins.rbegin(); pin != expected_io_pins.rend(); ++pin) {
        const auto found = objects.io_terms.find(entityKey(*pin));
        if (found == objects.io_terms.end() || found->second->getNet() != nullptr) {
          fail(logger, 100, "EccDB NET references an invalid or multiply-connected IO pin");
        }
        if (found->second->getType().isSupply() && !net->getType().isSupply()) {
          fail(logger, 101, "a supply IO pin cannot connect to a non-supply NET");
        }
        found->second->addToNet(net);
        net->addBTerm(found->second);
      }
    }
    for (const auto pin_id : instance_pins) {
      const auto found = objects.instance_terms.find(instanceTermKey(pin_id));
      if (found == objects.instance_terms.end() || found->second->getNet() != nullptr) {
        fail(logger, 102, "EccDB NET references an invalid or multiply-connected instance pin");
      }
      found->second->addToNet(net);
      net->addInstTerm(found->second);
    }
  }

  for (const auto instance_id : orderedIds(netlist.instances())) {
    for (const auto pin_id : netlist.instancePins(instance_id)) {
      const auto& pin = netlist.instancePin(pin_id);
      const auto owner = connectedNet(pin.net, pin.special_net, logger, "instance pin");
      if (!owner) {
        continue;
      }
      const auto term = objects.instance_terms.find(instanceTermKey(pin_id));
      const auto net = objects.nets.find(entityKey(owner));
      if (term == objects.instance_terms.end() || net == objects.nets.end() || term->second->getNet() != net->second) {
        fail(logger, 103, "EccDB NET reverse instance-pin connectivity is inconsistent");
      }
    }
  }
  for (const auto pin_id : orderedIds(netlist.ioPins())) {
    const auto& pin = netlist.ioPin(pin_id);
    const auto owner = connectedNet(pin.net, pin.special_net, logger, "IO pin");
    if (!owner) {
      continue;
    }
    const auto term = objects.io_terms.find(entityKey(pin_id));
    const auto net = objects.nets.find(entityKey(owner));
    if (term == objects.io_terms.end() || net == objects.nets.end() || term->second->getNet() != net->second) {
      fail(logger, 104, "EccDB NET reverse IO-pin connectivity is inconsistent");
    }
  }
}

void addFakeNets(frBlock& block, RouterConfiguration& configuration)
{
  auto vss = std::make_unique<frNet>("frFakeVSS", &configuration);
  vss->setType(odb::dbSigType::GROUND);
  vss->setIsFake(true);
  block.addFakeSNet(std::move(vss));

  auto vdd = std::make_unique<frNet>("frFakeVDD", &configuration);
  vdd->setType(odb::dbSigType::POWER);
  vdd->setIsFake(true);
  block.addFakeSNet(std::move(vdd));
}

}  // namespace

void readEccdbLibrary(const eccdb::PhysicalDesignService& physical, frDesign& design, utl::Logger& logger)
{
  if (design.getTech() == nullptr || design.getTech()->getLayers().empty()) {
    fail(logger, 57, "DRT technology must be loaded before EccDB library import");
  }
  if (!design.getMasters().empty()) {
    fail(logger, 58, "DRT library import requires an empty master table");
  }

  std::vector<std::unique_ptr<frMaster>> imported;
  const auto& storage = physical.library().cellMasterStorage();
  const auto ids = orderedIds(storage.cellMasters());
  entt::dense_map<std::string, bool> imported_names;
  imported.reserve(ids.size());
  imported_names.reserve(ids.size());
  for (std::size_t index = 0; index < ids.size(); ++index) {
    const auto id = ids[index];
    const auto& source = storage.cellMaster(id);
    if (!imported_names.emplace(source.name, true).second || design.findMaster(source.name) != nullptr) {
      fail(logger, 59, "EccDB library contains a master name already present in DRT");
    }
    imported.push_back(buildMaster(physical, design, id, source, checkedId(index + 1u, logger, "master id"), logger));
  }
  for (auto& master : imported) {
    design.addMaster(std::move(master));
  }
}

void readEccdbDesign(const eccdb::PhysicalDesignService& physical,
                     frDesign& design,
                     RouterConfiguration& configuration,
                     utl::Logger& logger)
{
  if (design.getTopBlock() != nullptr) {
    fail(logger, 60, "DRT design import requires an empty top block");
  }
  if (design.getTech() == nullptr || design.getTech()->getLayers().empty()) {
    fail(logger, 61, "DRT technology must be loaded before EccDB design import");
  }
  if (!physical.hasDesignInfo()) {
    fail(logger, 62, "EccDB design information is absent");
  }

  auto block = std::make_unique<frBlock>(physical.design().globalStorage().info().name);
  setDieArea(physical, *block, logger);
  setTracks(physical, design, *block, logger);

  ImportedObjects objects;
  objects.instance_terms.reserve(static_cast<std::size_t>(physical.design().netlistStorage().instancePinCount()));
  objects.io_terms.reserve(static_cast<std::size_t>(physical.design().netlistStorage().ioPinCount()));
  objects.nets.reserve(static_cast<std::size_t>(physical.design().netlistStorage().netCount()));
  setInstances(physical, design, *block, objects, logger);
  setRoutingBlockages(physical, design, *block, logger);
  setIoTerms(physical, design, *block, objects, logger);
  setNets(physical, design, *block, configuration, objects, logger);
  connectNets(physical, objects, logger);
  block->setId(0);
  addFakeNets(*block, configuration);
  design.setTopBlock(std::move(block));
}

}  // namespace drt::io
