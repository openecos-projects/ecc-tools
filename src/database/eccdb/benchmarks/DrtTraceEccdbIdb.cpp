// SPDX-License-Identifier: Apache-2.0
// Replays one recorded TritonRoute database trace against the EccDB service
// facade or the legacy iDB model. Import and token binding are deliberately
// outside drt_trace::replay's operation timers.

#include "DrtTrace.h"

#include "../adapters/tritonroute/IspdGuideReader.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "builder.h"
#include "def/DefDesignImporter.h"
#include "design/DesignStore.h"
#include "design/constraint/model/ConstraintComponents.h"
#include "design/guide/model/GuideComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/pin_access/model/PinAccessComponents.h"
#include "design/netlist/storage/DesignNetlistStorage.h"
#include "design/routing/storage/DesignRoutingStorage.h"
#include "design/via/component/ViaComponents.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/cell_master/model/MasterObsComponents.h"
#include "library/master_port/model/MasterPortComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "library/LibraryStore.h"
#include "service/query/PhysicalDesignService.h"
#include "service/context/RoutingContextService.h"
#include "service/routing/DesignViaService.h"
#include "service/routing/NetRouteService.h"
#include "tech/cut_layer/model/CutRuleComponents.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"
#include "tech/overlap_layer/model/OverlapLayerComponents.h"
#include "tech/routing_layer/model/RoutingRuleComponents.h"
#include "tech/TechStore.h"

namespace {
namespace json = boost::json;

[[noreturn]] void fail(std::string message)
{
  throw std::runtime_error(std::move(message));
}

[[nodiscard]] int32_t checkedDelta(int32_t upper, int32_t lower, std::string_view context)
{
  const int64_t value = static_cast<int64_t>(upper) - lower;
  if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
    fail(std::string(context) + " exceeds iDB's relative-coordinate range");
  }
  return static_cast<int32_t>(value);
}

[[nodiscard]] int32_t checkedOffset(int32_t origin, int32_t delta, std::string_view context)
{
  const int64_t value = static_cast<int64_t>(origin) + delta;
  if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
    fail(std::string(context) + " overflows int32");
  }
  return static_cast<int32_t>(value);
}

[[nodiscard]] int32_t checkedTraceInt(uint32_t value, std::string_view context)
{
  if (value > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
    fail(std::string(context) + " exceeds ODB's signed trace range");
  }
  return static_cast<int32_t>(value);
}

using TokenNames = std::vector<std::string>;

// iDB normalizes DEF escaped identifiers during import, while an ODB trace
// retains the spelling returned by OpenDB.  Keep that spelling for comparison
// and use this only as a fallback when resolving an iDB object by name.
[[nodiscard]] std::string unescapeDefIdentifier(std::string_view name)
{
  std::string result;
  result.reserve(name.size());
  bool escaped = false;
  for (const char character : name) {
    if (escaped) {
      result.push_back(character);
      escaped = false;
    } else if (character == '\\') {
      escaped = true;
    } else {
      result.push_back(character);
    }
  }
  if (escaped) result.push_back('\\');
  return result;
}

// A DRT run may derive a single X/Y GCELLGRID pair from guides before it
// begins reading ODB.  It is setup state rather than a timed trace event.
struct InitialGCellGrid
{
  int32_t x_start = 0;
  int32_t x_count = 0;
  int32_t x_step = 0;
  int32_t y_start = 0;
  int32_t y_count = 0;
  int32_t y_step = 0;
};

[[nodiscard]] std::optional<InitialGCellGrid> parseInitialGCellGrid(const json::value& value)
{
  if (value.is_null()) return std::nullopt;
  if (!value.is_array() || value.as_array().size() != 6u) {
    fail("trace header 'initial_gcell_grid' must be null or [xStart,xCount,xStep,yStart,yCount,yStep]");
  }
  const auto& fields = value.as_array();
  InitialGCellGrid result{.x_start = drt_trace::coordinate(fields[0]),
                          .x_count = drt_trace::coordinate(fields[1]),
                          .x_step = drt_trace::coordinate(fields[2]),
                          .y_start = drt_trace::coordinate(fields[3]),
                          .y_count = drt_trace::coordinate(fields[4]),
                          .y_step = drt_trace::coordinate(fields[5])};
  if (result.x_count <= 0 || result.x_step <= 0 || result.y_count <= 0 || result.y_step <= 0) {
    fail("trace header 'initial_gcell_grid' requires positive counts and steps");
  }
  return result;
}

[[nodiscard]] TokenNames tokenNames(const json::object& header, const char* field)
{
  const auto* value = header.if_contains(field);
  if (value == nullptr || !value->is_array()) {
    fail(std::string("trace header is missing array '") + field + "'");
  }

  TokenNames result;
  const auto& values = value->as_array();
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (!values[index].is_array()) {
      fail(std::string("trace header '") + field + "' entry " + std::to_string(index) + " is not an array");
    }
    const auto& entry = values[index].as_array();
    if (entry.size() != 2u) {
      fail(std::string("trace header '") + field + "' entry " + std::to_string(index) + " must contain token and name");
    }
    const int64_t raw_token = drt_trace::integer(entry[0]);
    if (raw_token < 0 || static_cast<uint64_t>(raw_token) > std::numeric_limits<uint32_t>::max()) {
      fail(std::string("trace header '") + field + "' token is outside uint32 range");
    }
    const std::string name = drt_trace::text(entry[1]);
    if (name.empty()) {
      fail(std::string("trace header '") + field + "' has an empty object name");
    }
    const auto token = static_cast<uint32_t>(raw_token);
    if (static_cast<uint64_t>(token) >= result.max_size()) {
      fail(std::string("trace header '") + field + "' token cannot index this platform's vector");
    }
    if (token >= result.size()) result.resize(static_cast<std::size_t>(token) + 1u);
    if (!result[token].empty()) {
      fail(std::string("trace header '") + field + "' duplicates token " + std::to_string(raw_token));
    }
    result[token] = name;
  }
  return result;
}

[[nodiscard]] std::string orientationName(eccdb::DesignOrientation orientation)
{
  switch (orientation) {
    case eccdb::DesignOrientation::kN: return "N";
    case eccdb::DesignOrientation::kS: return "S";
    case eccdb::DesignOrientation::kE: return "E";
    case eccdb::DesignOrientation::kW: return "W";
    case eccdb::DesignOrientation::kFN: return "FN";
    case eccdb::DesignOrientation::kFS: return "FS";
    case eccdb::DesignOrientation::kFE: return "FE";
    case eccdb::DesignOrientation::kFW: return "FW";
  }
  fail("EccDB instance has an invalid DEF orientation");
}

[[nodiscard]] std::string orientationName(idb::IdbOrient orientation)
{
  switch (orientation) {
    case idb::IdbOrient::kN_R0: return "N";
    case idb::IdbOrient::kS_R180: return "S";
    case idb::IdbOrient::kE_R270: return "E";
    case idb::IdbOrient::kW_R90: return "W";
    case idb::IdbOrient::kFN_MY: return "FN";
    case idb::IdbOrient::kFS_MX: return "FS";
    case idb::IdbOrient::kFE_MY90: return "FE";
    case idb::IdbOrient::kFW_MX90: return "FW";
    default: fail("iDB instance has an invalid DEF orientation");
  }
}

[[nodiscard]] const char* axisName(eccdb::DesignAxis axis)
{
  switch (axis) {
    case eccdb::DesignAxis::kX: return "X";
    case eccdb::DesignAxis::kY: return "Y";
  }
  fail("EccDB grid has an invalid DEF axis");
}

[[nodiscard]] const char* axisName(idb::IdbTrackDirection axis)
{
  switch (axis) {
    case idb::IdbTrackDirection::kDirectionX: return "X";
    case idb::IdbTrackDirection::kDirectionY: return "Y";
    default: fail("iDB grid has an invalid DEF axis");
  }
}

// ODB normalizes an omitted DEF USE to SIGNAL.  Preserve that semantic in the
// projections below instead of exposing EccDB/iDB's internal unspecified enum.
[[nodiscard]] const char* signalUseName(eccdb::DesignSignalUse use)
{
  switch (use) {
    case eccdb::DesignSignalUse::kNone:
    case eccdb::DesignSignalUse::kSignal: return "SIGNAL";
    case eccdb::DesignSignalUse::kAnalog: return "ANALOG";
    case eccdb::DesignSignalUse::kPower: return "POWER";
    case eccdb::DesignSignalUse::kGround: return "GROUND";
    case eccdb::DesignSignalUse::kClock: return "CLOCK";
    case eccdb::DesignSignalUse::kTieOff: return "TIEOFF";
    case eccdb::DesignSignalUse::kScan: return "SCAN";
    case eccdb::DesignSignalUse::kReset: return "RESET";
  }
  fail("EccDB net has an invalid signal USE");
}

[[nodiscard]] const char* signalUseName(idb::IdbConnectType use)
{
  switch (use) {
    case idb::IdbConnectType::kNone:
    case idb::IdbConnectType::kSignal: return "SIGNAL";
    case idb::IdbConnectType::kAnalog: return "ANALOG";
    case idb::IdbConnectType::kPower: return "POWER";
    case idb::IdbConnectType::kGround: return "GROUND";
    case idb::IdbConnectType::kClock: return "CLOCK";
    case idb::IdbConnectType::kTieOff: return "TIEOFF";
    case idb::IdbConnectType::kScan: return "SCAN";
    case idb::IdbConnectType::kReset: return "RESET";
    default: fail("iDB net has an invalid signal USE");
  }
}

[[nodiscard]] const char* specialWireTypeName(eccdb::DesignWireStatus status)
{
  switch (status) {
    case eccdb::DesignWireStatus::kRouted: return "ROUTED";
    case eccdb::DesignWireStatus::kFixed: return "FIXED";
    case eccdb::DesignWireStatus::kCover: return "COVER";
    case eccdb::DesignWireStatus::kShield: return "SHIELD";
    case eccdb::DesignWireStatus::kNoShield: return "NOSHIELD";
  }
  fail("EccDB special wire has an invalid status");
}

template <typename Flag>
[[nodiscard]] bool hasFlag(uint64_t flags, Flag flag)
{
  return (flags & static_cast<uint64_t>(flag)) != 0u;
}

[[nodiscard]] const char* routingDirectionName(eccdb::TechRoutingDirection direction)
{
  switch (direction) {
    case eccdb::TechRoutingDirection::kUnknown: return "NONE";
    case eccdb::TechRoutingDirection::kHorizontal: return "HORIZONTAL";
    case eccdb::TechRoutingDirection::kVertical: return "VERTICAL";
    case eccdb::TechRoutingDirection::kDiag45: return "DIAG45";
    case eccdb::TechRoutingDirection::kDiag135: return "DIAG135";
  }
  fail("EccDB routing layer has an invalid direction");
}

[[nodiscard]] const char* minStepTypeName(eccdb::TechRoutingMinStepType type)
{
  switch (type) {
    case eccdb::TechRoutingMinStepType::kNone: return "NONE";
    case eccdb::TechRoutingMinStepType::kInsideCorner: return "INSIDECORNER";
    case eccdb::TechRoutingMinStepType::kOutsideCorner: return "OUTSIDECORNER";
    case eccdb::TechRoutingMinStepType::kStep: return "STEP";
  }
  fail("EccDB MINSTEP has an invalid type");
}

[[nodiscard]] const char* cellMasterTypeName(eccdb::LibraryCellMasterType type)
{
  switch (type) {
    case eccdb::LibraryCellMasterType::kNone: return "NONE";
    case eccdb::LibraryCellMasterType::kCover: return "COVER";
    case eccdb::LibraryCellMasterType::kCoverBump: return "COVER BUMP";
    case eccdb::LibraryCellMasterType::kRing: return "RING";
    case eccdb::LibraryCellMasterType::kBlock: return "BLOCK";
    case eccdb::LibraryCellMasterType::kBlockBlackbox: return "BLOCK BLACKBOX";
    case eccdb::LibraryCellMasterType::kBlockSoft: return "BLOCK SOFT";
    case eccdb::LibraryCellMasterType::kPad: return "PAD";
    case eccdb::LibraryCellMasterType::kPadInput: return "PAD INPUT";
    case eccdb::LibraryCellMasterType::kPadOutput: return "PAD OUTPUT";
    case eccdb::LibraryCellMasterType::kPadInOut: return "PAD INOUT";
    case eccdb::LibraryCellMasterType::kPadPower: return "PAD POWER";
    case eccdb::LibraryCellMasterType::kPadSpacer: return "PAD SPACER";
    case eccdb::LibraryCellMasterType::kPadAreaIo: return "PAD AREAIO";
    case eccdb::LibraryCellMasterType::kCore: return "CORE";
    case eccdb::LibraryCellMasterType::kCoreFeedThru: return "CORE FEEDTHRU";
    case eccdb::LibraryCellMasterType::kCoreTieHigh: return "CORE TIEHIGH";
    case eccdb::LibraryCellMasterType::kCoreTieLow: return "CORE TIELOW";
    case eccdb::LibraryCellMasterType::kCoreSpacer: return "CORE SPACER";
    case eccdb::LibraryCellMasterType::kCoreAntennaCell: return "CORE ANTENNACELL";
    case eccdb::LibraryCellMasterType::kCoreWelltap: return "CORE WELLTAP";
    case eccdb::LibraryCellMasterType::kEndcap: return "ENDCAP";
    case eccdb::LibraryCellMasterType::kEndcapPre: return "ENDCAP PRE";
    case eccdb::LibraryCellMasterType::kEndcapPost: return "ENDCAP POST";
    case eccdb::LibraryCellMasterType::kEndcapTopLeft: return "ENDCAP TOPLEFT";
    case eccdb::LibraryCellMasterType::kEndcapTopRight: return "ENDCAP TOPRIGHT";
    case eccdb::LibraryCellMasterType::kEndcapBottomLeft: return "ENDCAP BOTTOMLEFT";
    case eccdb::LibraryCellMasterType::kEndcapBottomRight: return "ENDCAP BOTTOMRIGHT";
  }
  fail("EccDB library master has an invalid class");
}

[[nodiscard]] const char* masterTermUseName(eccdb::LibraryMasterTermUse use)
{
  switch (use) {
    case eccdb::LibraryMasterTermUse::kNone:
    case eccdb::LibraryMasterTermUse::kSignal: return "SIGNAL";
    case eccdb::LibraryMasterTermUse::kAnalog: return "ANALOG";
    case eccdb::LibraryMasterTermUse::kPower: return "POWER";
    case eccdb::LibraryMasterTermUse::kGround: return "GROUND";
    case eccdb::LibraryMasterTermUse::kClock: return "CLOCK";
    case eccdb::LibraryMasterTermUse::kTieOff: return "TIEOFF";
    case eccdb::LibraryMasterTermUse::kScan: return "SCAN";
    case eccdb::LibraryMasterTermUse::kReset: return "RESET";
  }
  fail("EccDB library master term has an invalid USE");
}

[[nodiscard]] const char* ioDirectionName(eccdb::DesignIoPinDirection direction)
{
  switch (direction) {
    case eccdb::DesignIoPinDirection::kNone: return "NONE";
    case eccdb::DesignIoPinDirection::kInput: return "INPUT";
    case eccdb::DesignIoPinDirection::kOutput: return "OUTPUT";
    case eccdb::DesignIoPinDirection::kInOut: return "INOUT";
    case eccdb::DesignIoPinDirection::kFeedThru: return "FEEDTHRU";
  }
  fail("EccDB IO pin has an invalid direction");
}

[[nodiscard]] const char* pinAccessTypeName(eccdb::DesignPinAccessType type)
{
  switch (type) {
    case eccdb::DesignPinAccessType::kOnGrid: return "OnGrid";
    case eccdb::DesignPinAccessType::kHalfGrid: return "HalfGrid";
    case eccdb::DesignPinAccessType::kCenter: return "Center";
    case eccdb::DesignPinAccessType::kEncOpt: return "EncOpt";
    case eccdb::DesignPinAccessType::kNearbyGrid: return "NearbyGrid";
  }
  fail("EccDB pin access point has an invalid access type");
}

[[nodiscard]] bool isKnownPinAccessType(eccdb::DesignPinAccessType type) noexcept
{
  switch (type) {
    case eccdb::DesignPinAccessType::kOnGrid:
    case eccdb::DesignPinAccessType::kHalfGrid:
    case eccdb::DesignPinAccessType::kCenter:
    case eccdb::DesignPinAccessType::kEncOpt:
    case eccdb::DesignPinAccessType::kNearbyGrid: return true;
  }
  return false;
}

[[nodiscard]] int32_t checkedProjectedCoordinate(int64_t value)
{
  if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
    fail("EccDB projected geometry coordinate overflows int32");
  }
  return static_cast<int32_t>(value);
}

// DEF PIN geometry is local to each PORT when that PORT carries placement.
// Match PhysicalDesignService::forEachIoPinGeometry while keeping PORT/BPin
// grouping intact for the ODB trace contract.
[[nodiscard]] eccdb::Point transformIoPoint(eccdb::Point point,
                                             eccdb::Point origin,
                                             eccdb::DesignOrientation orientation)
{
  const int64_t x = point.x;
  const int64_t y = point.y;
  int64_t transformed_x = 0;
  int64_t transformed_y = 0;
  switch (orientation) {
    case eccdb::DesignOrientation::kN: transformed_x = x; transformed_y = y; break;
    case eccdb::DesignOrientation::kS: transformed_x = -x; transformed_y = -y; break;
    case eccdb::DesignOrientation::kE: transformed_x = y; transformed_y = -x; break;
    case eccdb::DesignOrientation::kW: transformed_x = -y; transformed_y = x; break;
    case eccdb::DesignOrientation::kFN: transformed_x = -x; transformed_y = y; break;
    case eccdb::DesignOrientation::kFS: transformed_x = x; transformed_y = -y; break;
    case eccdb::DesignOrientation::kFE: transformed_x = -y; transformed_y = -x; break;
    case eccdb::DesignOrientation::kFW: transformed_x = y; transformed_y = x; break;
  }
  return {.x = checkedProjectedCoordinate(transformed_x + origin.x),
          .y = checkedProjectedCoordinate(transformed_y + origin.y)};
}

[[nodiscard]] eccdb::Rect transformIoRectangle(eccdb::Rect rectangle,
                                                eccdb::Point origin,
                                                eccdb::DesignOrientation orientation)
{
  const auto first = transformIoPoint({.x = rectangle.ll_x, .y = rectangle.ll_y}, origin, orientation);
  eccdb::Rect result{.ll_x = first.x, .ll_y = first.y, .ur_x = first.x, .ur_y = first.y};
  const auto update = [&result](eccdb::Point point) {
    result.ll_x = std::min(result.ll_x, point.x);
    result.ll_y = std::min(result.ll_y, point.y);
    result.ur_x = std::max(result.ur_x, point.x);
    result.ur_y = std::max(result.ur_y, point.y);
  };
  update(transformIoPoint({.x = rectangle.ll_x, .y = rectangle.ur_y}, origin, orientation));
  update(transformIoPoint({.x = rectangle.ur_x, .y = rectangle.ll_y}, origin, orientation));
  update(transformIoPoint({.x = rectangle.ur_x, .y = rectangle.ur_y}, origin, orientation));
  return result;
}

class EccdbBackend
{
 public:
  static constexpr bool retains_extensions = true;

  EccdbBackend(const drt_trace::Options& options, const json::object& header)
      : library_(technology_.techRegistry()),
        design_(technology_.techRegistry(), library_.libraryRegistry()),
        vias_(design_),
        physical_(technology_, library_, design_),
        routes_(design_, technology_),
        profile_wire_write_(options.profile_wire_write)
  {
    eccdb::LefTechImporter(technology_).import(options.lef);
    eccdb::LefLibraryImporter(technology_, library_).import(options.lef);
    eccdb::DefDesignImporter(design_).import(options.def);
    importGuides(options.guides);
    restoreInitialGCellGrid(header);
    bind(header);
  }

  // EccDB retains the complete technology-header projection.
  [[nodiscard]] bool supports(const drt_trace::Event& event) const
  {
    return event.op == "read_tech_header" || event.op == "read_tech_layers"
           || (event.op == "read_routing_layer_rules" && supportsRoutingLayerRules(event))
           || (event.op == "read_cut_layer_rules" && supportsCutLayerRules())
           || (event.op == "read_tech_vias" && supportsTechVias(event))
           || (event.op == "read_design_vias" && supportsDesignVias(event))
           || (event.op == "read_via_generate_rules" && supportsViaGenerateRules(event))
           || event.op == "read_ndrs"
           || (event.op == "read_library_masters" && supportsLibraryGeometry())
           || (event.op == "read_library_terms" && supportsLibraryGeometry())
           || (event.op == "read_library_mpin_geometry" && supportsLibraryGeometry())
           || (event.op == "read_library_obstructions" && supportsLibraryGeometry())
           || event.op == "read_die" || event.op == "scan_tracks" || event.op == "read_instance_pins"
           || event.op == "read_gcell_grid" || (event.op == "read_blockages" && supportsBlockages())
           || (event.op == "read_bterms" && supportsBterms())
           || (event.op == "read_guides" && supportsGuides(event))
           || (event.op == "read_special_wire" && supportsSpecialWire(event))
           || (event.op == "read_bterm_pin_access" && supportsBtermPinAccess(event))
           || (event.op == "read_master_pin_access" && supportsMasterPinAccess(event))
           || (event.op == "read_preferred_pin_access" && supportsPreferredPinAccess(event))
           || event.op == "scan_instances" || event.op == "scan_nets"
           || event.op == "read_instance" || event.op == "read_net_attributes"
           || event.op == "net_pins" || event.op == "read_wire"
           || event.op == "find_instance" || event.op == "find_pin" || event.op == "define_design_via"
           || event.op == "clear_wire" || event.op == "replace_wire"
           || (event.op == "append_track_assignment" && supportsAppendTrackAssignment(event))
           || (event.op == "replace_master_pin_access" && supportsReplaceMasterPinAccess(event))
           || (event.op == "replace_instance_pin_access" && supportsReplaceInstancePinAccess(event))
           || (event.op == "append_bterm_pin_access" && supportsAppendBtermPinAccess(event))
           || (event.op == "replace_guides" && supportsReplaceGuides(event))
           || (event.op == "append_bterm_stack_wire" && supportsAppendBtermStackWire(event));
  }

  [[nodiscard]] json::value execute(const drt_trace::Event& event)
  {
    if (event.op == "read_tech_header") return readTechHeader();
    if (event.op == "read_tech_layers") return readTechLayers();
    if (event.op == "read_routing_layer_rules") return readRoutingLayerRules();
    if (event.op == "read_cut_layer_rules") return readCutLayerRules();
    if (event.op == "read_tech_vias") return readTechVias();
    if (event.op == "read_design_vias") return readDesignVias();
    if (event.op == "read_via_generate_rules") return readViaGenerateRules();
    if (event.op == "read_ndrs") return readNdrs();
    if (event.op == "read_library_masters") return readLibraryMasters();
    if (event.op == "read_library_terms") return readLibraryTerms();
    if (event.op == "read_library_mpin_geometry") return readLibraryMPinGeometry();
    if (event.op == "read_library_obstructions") return readLibraryObstructions();
    if (event.op == "read_die") return readDie();
    if (event.op == "scan_tracks") return scanTracks();
    if (event.op == "read_instance_pins") return readInstancePins(event.object);
    if (event.op == "read_net_attributes") return readNetAttributes(event.object);
    if (event.op == "read_gcell_grid") return readGCellGrid();
    if (event.op == "read_blockages") return readBlockages();
    if (event.op == "read_bterms") return readBterms();
    if (event.op == "read_guides") return readGuides(event.name);
    if (event.op == "read_special_wire") return readSpecialWire(event.name);
    if (event.op == "read_bterm_pin_access") return readBtermPinAccess(event.name, event.ordinal);
    if (event.op == "read_master_pin_access") return readMasterPinAccess(event.master, event.term, *event.ordinal);
    if (event.op == "read_preferred_pin_access") return readPreferredPinAccess(event.instance, event.term);
    if (event.op == "scan_instances") return scanInstances();
    if (event.op == "scan_nets") return scanNets();
    if (event.op == "read_instance") return readInstance(event.object);
    if (event.op == "net_pins") return netPins(event.object);
    if (event.op == "read_wire") return drt_trace::shapeProjection(wire(event.object), false);
    if (event.op == "find_instance") return findInstance(event.name);
    if (event.op == "find_pin") return findPin(event.object, event.name);
    if (event.op == "define_design_via") {
      defineDesignVia(event);
      return viaDefinition(event.name);
    }
    if (event.op == "clear_wire") {
      static_cast<void>(routes_.replaceRouted(net(event.object), {}));
      return json::value(nullptr);
    }
    if (event.op == "replace_wire") {
      replaceWire(event.object, event.shapes);
      return json::value(nullptr);
    }
    if (event.op == "append_track_assignment") return appendTrackAssignment(event);
    if (event.op == "replace_master_pin_access") return replaceMasterPinAccess(event);
    if (event.op == "replace_instance_pin_access") return replaceInstancePinAccess(event);
    if (event.op == "append_bterm_pin_access") return appendBtermPinAccess(event);
    if (event.op == "replace_guides") return replaceGuides(event);
    if (event.op == "append_bterm_stack_wire") return appendBtermStackWire(event);
    fail("unsupported EccDB trace operation: " + event.op);
  }

  // This is intentionally separate from execute().  Trace parsing, token
  // binding and JSON projection are replay plumbing; only the underlying
  // service/storage call belongs in NativeTimer.  Keep unsupported/complex
  // operations on execute() below without a timer rather than presenting an
  // adapter-inclusive number as a native measurement.
  [[nodiscard]] json::value executeNative(const drt_trace::Event& event, drt_trace::NativeTimer& timer)
  {
    // Preserve the existing diagnostic profile's adapter-stage counters when
    // explicitly requested. That mode is intentionally not a native-only
    // benchmark and therefore remains unmeasured here.
    if (profile_wire_write_ && event.op == "replace_wire") return execute(event);

    const auto item_count = event.expected.is_array() ? static_cast<uint64_t>(event.expected.as_array().size()) : 1u;

    if (event.op == "find_instance") {
      const auto found = timer.measure("find_instance", 1u, [&] { return physical_.findInstance(event.name); });
      if (!found) return json::value(nullptr);
      return json::value(design_.netlistStorage().instance(found).name);
    }
    if (event.op == "find_pin") {
      const auto instance_id = instance(event.object);
      const auto* found = timer.measure("find_pin", 1u, [&] {
        return physical_.findInstanceTermData(instance_id, event.name);
      });
      if (found == nullptr) return json::value(nullptr);
      return json::value(found->master_term_ptr->name);
    }
    if (event.op == "read_instance") {
      const auto instance_id = instance(event.object);
      struct Value
      {
        std::string_view name;
        std::string_view master;
        eccdb::Point origin;
        eccdb::DesignOrientation orientation = eccdb::DesignOrientation::kN;
      } value;
      timer.measure("read_instance", 1u, [&] {
        const auto& instance_value = design_.netlistStorage().instance(instance_id);
        const auto& master = library_.cellMasterStorage().cellMaster(instance_value.master);
        value = {.name = instance_value.name,
                 .master = master.name,
                 .origin = instance_value.origin,
                 .orientation = instance_value.orientation};
      });
      return json::array{std::string(value.name), std::string(value.master), value.origin.x, value.origin.y,
                         orientationName(value.orientation)};
    }
    if (event.op == "read_instance_pins") {
      const auto instance_id = instance(event.object);
      const auto& netlist = design_.netlistStorage();
      std::vector<std::string_view> names;
      names.reserve(static_cast<std::size_t>(item_count));
      timer.measure("read_instance_pins", item_count, [&] {
        const auto pins = netlist.instancePins(instance_id);
        for (const auto pin : pins) names.push_back(netlist.instanceTerm(pin).master_term_ptr->name);
      });
      json::array result;
      result.reserve(names.size());
      for (const auto name : names) result.emplace_back(std::string(name));
      return result;
    }
    if (event.op == "read_net_attributes") {
      const auto net_id = net(event.object);
      struct Value
      {
        eccdb::DesignSignalUse use = eccdb::DesignSignalUse::kNone;
        std::optional<std::string_view> rule;
        bool jumpers = false;
        bool abutment = false;
      } value;
      timer.measure("read_net_attributes", 1u, [&] {
        value.use = design_.netlistStorage().net(net_id).use;
        value.rule = physical_.nonDefaultRuleName(net_id);
        value.jumpers = physical_.netHasJumpers(net_id);
        value.abutment = physical_.isNetConnectedByAbutment(net_id);
      });
      return json::array{signalUseName(value.use),
                         value.rule ? json::value(std::string(*value.rule)) : json::value(nullptr),
                         value.jumpers,
                         value.abutment};
    }
    if (event.op == "scan_instances") {
      const auto& netlist = design_.netlistStorage();
      std::vector<std::string_view> names;
      const auto instance_count = netlist.instanceCount();
      names.reserve(instance_count);
      timer.measure("scan_instances", instance_count, [&] {
        netlist.forEachInstance([&](eccdb::DesignInstanceId,
                                    const eccdb::DesignInstance& instance_value,
                                    std::span<const eccdb::DesignInstancePinId>) {
          names.push_back(instance_value.name);
        });
      });
      json::array result;
      result.reserve(names.size());
      for (const auto name : names) result.emplace_back(std::string(name));
      return result;
    }
    if (event.op == "scan_nets") {
      std::vector<std::string_view> names;
      names.reserve(static_cast<std::size_t>(item_count));
      timer.measure("scan_nets", item_count, [&] {
        physical_.forEachRegularNet([&](eccdb::DesignNetId, const eccdb::DesignNet& value) { names.push_back(value.name); });
      });
      json::array result;
      result.reserve(names.size());
      for (const auto name : names) result.emplace_back(std::string(name));
      return result;
    }
    if (event.op == "net_pins") {
      const auto net_id = net(event.object);
      struct PinValue
      {
        bool instance = false;
        std::string_view first;
        std::string_view second;
      };
      std::vector<PinValue> values;
      values.reserve(static_cast<std::size_t>(item_count));
      eccdb::PhysicalNetPinVisitor visitor;
      visitor.instance_pin = [&](eccdb::DesignInstancePinId,
                                 const eccdb::DesignInstanceTerm&,
                                 const eccdb::DesignInstance& instance_value,
                                 const eccdb::LibraryMasterTerm& term) {
        values.push_back({.instance = true, .first = instance_value.name, .second = term.name});
      };
      visitor.io_pin = [&](eccdb::DesignIoPinId, const eccdb::DesignIoPin& pin) {
        values.push_back({.instance = false, .first = pin.name, .second = {}});
      };
      timer.measure("net_pins", item_count, [&] { physical_.forEachNetPin(net_id, visitor); });
      json::array result;
      result.reserve(values.size());
      for (const auto& value : values) {
        result.emplace_back(json::array{value.instance ? "I" : "B", std::string(value.first), std::string(value.second)});
      }
      return result;
    }
    if (event.op == "read_wire") {
      const auto net_id = net(event.object);
      struct RawShape
      {
        enum class Kind : uint8_t
        {
          kSegment,
          kVia,
          kPatch
        };

        Kind kind = Kind::kSegment;
        std::string_view layer;
        std::string_view via;
        int32_t x1 = 0;
        int32_t y1 = 0;
        int32_t x2 = 0;
        int32_t y2 = 0;
        std::optional<int32_t> begin_extension;
        std::optional<int32_t> end_extension;
        bool tech_via = true;
      };
      std::vector<RawShape> raw_shapes;
      raw_shapes.reserve(static_cast<std::size_t>(item_count));
      timer.measure("read_wire", item_count, [&] {
        const auto snapshot = routes_.snapshot(net_id);
        for (const auto& saved_wire : snapshot.wires) {
          for (const auto& shape : saved_wire.shapes) {
            std::visit(
                [&](const auto& value) {
                  using Value = std::remove_cvref_t<decltype(value)>;
                  if constexpr (std::is_same_v<Value, eccdb::RouteSegment>) {
                    raw_shapes.push_back({.kind = RawShape::Kind::kSegment,
                                          .layer = technology_.layerInfo(eccdb::TechLayerId{value.layer.entity()}).name,
                                          .x1 = value.begin.x,
                                          .y1 = value.begin.y,
                                          .x2 = value.end.x,
                                          .y2 = value.end.y,
                                          .begin_extension = value.begin_extension,
                                          .end_extension = value.end_extension});
                  } else if constexpr (std::is_same_v<Value, eccdb::RouteVia>) {
                    RawShape raw{.kind = RawShape::Kind::kVia,
                                 .x1 = value.origin.x,
                                 .y1 = value.origin.y,
                                 .x2 = value.origin.x,
                                 .y2 = value.origin.y};
                    std::visit(
                        [&](const auto definition) {
                          using Definition = std::remove_cvref_t<decltype(definition)>;
                          if constexpr (std::is_same_v<Definition, eccdb::TechViaMasterId>) {
                            const auto& vias = technology_.viaMasterStorage();
                            const auto& geometry = vias.geometry(definition);
                            raw.tech_via = true;
                            raw.via = vias.viaMaster(definition).name;
                            raw.layer = technology_.layerInfo(geometry.bottom_layer.layer()).name;
                          } else {
                            const auto& design_via = design_.routingStorage().via(definition);
                            raw.tech_via = false;
                            raw.via = design_via.name;
                            if ((design_via.flags & eccdb::DesignViaFlag::kGenerated) != 0u) {
                              raw.layer = technology_.layerInfo(eccdb::TechLayerId{design_via.generated.bottom_layer.entity()}).name;
                            } else {
                              eccdb::TechRoutingLayerId first_layer;
                              eccdb::TechRoutingLayerId second_layer;
                              const auto& registry = technology_.techRegistry().registry();
                              const auto append = [&](const eccdb::TechLayerId layer) {
                                const auto routing_layer = eccdb::TechRoutingLayerId{layer.entity()};
                                if (!registry.all_of<eccdb::TechRoutingLayer>(layer.entity())) return;
                                if (!first_layer) {
                                  first_layer = routing_layer;
                                } else if (routing_layer != first_layer) {
                                  second_layer = routing_layer;
                                }
                              };
                              for (const auto& rectangle : design_via.rectangles) append(rectangle.layer);
                              for (const auto& polygon : design_via.polygons) append(polygon.layer);
                              const auto bottom_layer = technology_.isBelow(eccdb::TechLayerId{second_layer.entity()},
                                                                           eccdb::TechLayerId{first_layer.entity()})
                                                            ? second_layer
                                                            : first_layer;
                              raw.layer = technology_.layerInfo(eccdb::TechLayerId{bottom_layer.entity()}).name;
                            }
                          }
                        },
                        value.definition);
                    raw_shapes.push_back(std::move(raw));
                  } else {
                    raw_shapes.push_back({.kind = RawShape::Kind::kPatch,
                                          .layer = technology_.layerInfo(eccdb::TechLayerId{value.layer.entity()}).name,
                                          .x1 = value.rectangle.ll_x,
                                          .y1 = value.rectangle.ll_y,
                                          .x2 = value.rectangle.ur_x,
                                          .y2 = value.rectangle.ur_y});
                  }
                },
                shape);
          }
        }
      });
      std::vector<drt_trace::Shape> shapes;
      shapes.reserve(raw_shapes.size());
      for (const auto& raw : raw_shapes) {
        drt_trace::Shape shape;
        shape.kind = raw.kind == RawShape::Kind::kSegment ? "segment"
                     : raw.kind == RawShape::Kind::kVia ? "via"
                                                           : "patch";
        shape.layer = raw.layer;
        shape.via = raw.via;
        shape.x1 = raw.x1;
        shape.y1 = raw.y1;
        shape.x2 = raw.x2;
        shape.y2 = raw.y2;
        shape.begin_extension = raw.begin_extension;
        shape.end_extension = raw.end_extension;
        shape.tech_via = raw.tech_via;
        shapes.push_back(std::move(shape));
      }
      return drt_trace::shapeProjection(shapes, false);
    }
    if (event.op == "read_guides") {
      const auto net_id = netByName(event.name);
      struct RawGuide
      {
        std::string_view layer;
        std::optional<std::string_view> via_layer;
        int32_t x1 = 0;
        int32_t y1 = 0;
        int32_t x2 = 0;
        int32_t y2 = 0;
        bool congested = false;
        bool jumper = false;
        bool connected_to_term = false;
      };
      std::vector<RawGuide> guides;
      guides.reserve(static_cast<std::size_t>(item_count));
      timer.measure("read_guides", item_count, [&] {
        const auto source = design_.guideStorage().guides(net_id);
        for (const auto& guide : source) {
          const auto& layer = technology_.layerInfo(eccdb::TechLayerId{guide.layer.entity()});
          if (isBackside(layer)) continue;
          const auto via_layer = eccdb::TechLayerId{guide.via_layer.entity()};
          std::optional<std::string_view> via_layer_name;
          if (via_layer) {
            const auto& via_layer_info = technology_.layerInfo(via_layer);
            if (isBackside(via_layer_info)) continue;
            via_layer_name = via_layer_info.name;
          }
          guides.push_back({.layer = layer.name,
                            .via_layer = via_layer_name,
                            .x1 = guide.box.ll_x,
                            .y1 = guide.box.ll_y,
                            .x2 = guide.box.ur_x,
                            .y2 = guide.box.ur_y,
                            .congested = hasFlag(guide.flags, eccdb::DesignRouteGuideFlag::kCongested),
                            .jumper = hasFlag(guide.flags, eccdb::DesignRouteGuideFlag::kJumper),
                            .connected_to_term = hasFlag(guide.flags, eccdb::DesignRouteGuideFlag::kConnectedToTerm)});
        }
      });
      json::array result;
      result.reserve(guides.size());
      for (const auto& guide : guides) {
        result.emplace_back(json::array{guide.layer,
                                        guide.via_layer ? json::value(*guide.via_layer) : json::value(nullptr),
                                        guide.x1,
                                        guide.y1,
                                        guide.x2,
                                        guide.y2,
                                        guide.congested,
                                        guide.jumper,
                                        guide.connected_to_term});
      }
      return result;
    }
    if (event.op == "clear_wire") {
      const auto net_id = net(event.object);
      timer.measure("clear_wire", 0u, [&] { static_cast<void>(routes_.replaceRouted(net_id, {})); });
      return json::value(nullptr);
    }
    if (event.op == "replace_wire") {
      const auto net_id = net(event.object);
      std::vector<eccdb::RouteShape> shapes;
      shapes.reserve(event.shapes.size());
      for (const auto& shape : event.shapes) shapes.push_back(routeShape(shape));
      timer.measure("replace_wire", static_cast<uint64_t>(shapes.size()), [&] {
        static_cast<void>(routes_.replaceRouted(net_id, std::span<const eccdb::RouteShape>{shapes}));
      });
      return json::value(nullptr);
    }
    if (event.op == "append_track_assignment") {
      const auto& payload = payloadObject(event);
      const auto* appended = payload.if_contains("append");
      if (appended == nullptr) fail("trace track-assignment payload has no append list");
      const auto net_id = netByName(event.name);
      auto added = parseTrackAssignments(*appended);
      std::vector<eccdb::DesignTrackAssignmentShape> replacement;
      const auto previous = design_.guideStorage().trackAssignments(net_id);
      replacement.reserve(added.size() + previous.size());
      for (auto it = added.rbegin(); it != added.rend(); ++it) replacement.push_back(std::move(*it));
      replacement.insert(replacement.end(), previous.begin(), previous.end());
      timer.measure("append_track_assignment", static_cast<uint64_t>(added.size()), [&] {
        eccdb::RoutingContextService context(design_);
        context.replaceTrackAssignments(net_id, std::move(replacement));
      });
      return trackAssignmentsValue(net_id);
    }
    if (event.op == "replace_guides") {
      const auto& payload = payloadObject(event);
      const auto* source = payload.if_contains("guides");
      if (source == nullptr) fail("trace guide payload has no guide list");
      const auto net_id = netByName(event.name);
      auto guides = parseGuides(*source);
      const auto count = static_cast<uint64_t>(guides.size());
      timer.measure("replace_guides", count, [&] {
        eccdb::RoutingContextService context(design_);
        context.replaceGuides(net_id, std::move(guides));
      });
      return readGuides(event.name);
    }
    if (event.op == "append_bterm_pin_access") {
      const auto& payload = payloadObject(event);
      const auto* ordinal = payload.if_contains("bpin_ordinal");
      const auto* appended = payload.if_contains("append");
      if (ordinal == nullptr || appended == nullptr) fail("trace BTerm pin-access payload is incomplete");
      const auto pin_id = physical_.findBlockTerm(event.name);
      if (!pin_id) fail("EccDB trace BTerm does not exist: " + event.name);
      const auto port_index = traceIndex(*ordinal, "BPin ordinal");
      const auto block_pins = physical_.blockPins(pin_id);
      if (port_index >= block_pins.size()) fail("trace BPin ordinal is out of range");
      auto points = parsePinAccessPoints(*appended);
      const auto count = static_cast<uint64_t>(points.size());
      timer.measure("append_bterm_pin_access", count, [&] {
        eccdb::RoutingContextService context(design_);
        context.appendBlockPinCandidatePoints(block_pins[port_index], 0u, std::move(points));
      });
      return readBtermPinAccess(event.name, port_index);
    }
    if (event.op == "replace_master_pin_access") {
      auto replacement = parseMasterPinAccessReplacement(event);
      const auto& master = library_.cellMasterStorage().cellMaster(replacement.master);
      struct ClearTarget { eccdb::LibraryMasterPortId port; uint32_t group = 0; };
      std::vector<ClearTarget> targets;
      for (const auto term : master.terms) {
        for (const auto port : physical_.masterPorts(term)) {
          for (const auto group : replacement.clear_groups) {
            if (design_.pinAccessStorage().hasMasterPortCandidateGroup(port, group)) targets.push_back({port, group});
          }
        }
      }
      const auto item_total = static_cast<uint64_t>(targets.size() + replacement.updates.size());
      timer.measure("replace_master_pin_access", item_total, [&] {
        eccdb::RoutingContextService context(design_);
        for (const auto& target : targets) {
          if (!context.clearMasterPortCandidateGroup(target.port, target.group)) {
            fail("EccDB cannot clear a referenced or selected master pin-access candidate group");
          }
        }
        for (auto& update : replacement.updates) {
          context.setMasterPortCandidateGroup(update.port, update.group_index, std::move(update.points));
        }
      });
      return masterPinAccessUpdateValue(replacement.updates);
    }
    if (event.op == "replace_instance_pin_access") {
      auto replacement = parseInstancePinAccessReplacement(event);
      struct PreparedTerm
      {
        eccdb::DesignInstancePinId pin;
        std::vector<eccdb::DesignPreferredPinAccessRef> preferences;
      };
      std::vector<PreparedTerm> terms;
      terms.reserve(replacement.terms.size());
      for (const auto& term : replacement.terms) {
        PreparedTerm prepared{.pin = term.pin};
        prepared.preferences.reserve(term.locators.size());
        for (const auto& locator : term.locators) {
          if (locator.preference) prepared.preferences.push_back(*locator.preference);
        }
        terms.push_back(std::move(prepared));
      }
      timer.measure("replace_instance_pin_access", static_cast<uint64_t>(replacement.terms.size()), [&] {
        eccdb::RoutingContextService context(design_);
        for (const auto& term : terms) static_cast<void>(context.clearPreferredInstancePinAccess(term.pin));
        context.setInstancePinAccessGroup(replacement.instance, replacement.group_index);
        for (auto& term : terms) {
          context.setPreferredInstancePinAccess(term.pin, std::move(term.preferences));
        }
      });
      return instancePinAccessUpdateValue(replacement);
    }
    if (event.op == "append_bterm_stack_wire") {
      const auto& payload = payloadObject(event);
      const auto* layer = payload.if_contains("layer");
      const auto* x = payload.if_contains("x");
      const auto* y = payload.if_contains("y");
      const auto* vias = payload.if_contains("vias");
      if (layer == nullptr || x == nullptr || y == nullptr || vias == nullptr || !vias->is_array()) {
        fail("trace BTerm stack-wire payload is incomplete");
      }
      const auto net_id = net(event.object);
      auto active_layer = routingLayer(drt_trace::text(*layer));
      const eccdb::Point origin{.x = drt_trace::coordinate(*x), .y = drt_trace::coordinate(*y)};
      std::vector<eccdb::RouteShape> shapes;
      const auto snapshot = routes_.snapshot(net_id);
      for (const auto& saved_wire : snapshot.wires) {
        if (saved_wire.status == eccdb::DesignWireStatus::kRouted) {
          shapes.insert(shapes.end(), saved_wire.shapes.begin(), saved_wire.shapes.end());
        }
      }
      for (const auto& via_name : vias->as_array()) {
        const auto via = technology_.viaMasterStorage().findViaMaster(drt_trace::text(via_name));
        if (!via) fail("trace BTerm stack wire references an unknown technology VIA");
        const auto layers = physical_.routingViaLayers(via, {});
        if (active_layer != layers.bottom && active_layer != layers.top) {
          fail("trace BTerm stack VIA does not touch the active routing layer");
        }
        shapes.emplace_back(eccdb::RouteVia{.definition = eccdb::RouteViaDefinition{via}, .origin = origin});
        active_layer = active_layer == layers.bottom ? layers.top : layers.bottom;
      }
      timer.measure("append_bterm_stack_wire", static_cast<uint64_t>(vias->as_array().size()), [&] {
        static_cast<void>(routes_.replaceRouted(net_id, std::span<const eccdb::RouteShape>{shapes}));
      });
      return drt_trace::shapeValues(wire(event.object), false);
    }

    return execute(event);
  }

  [[nodiscard]] std::vector<drt_trace::Shape> wire(uint32_t token) const
  {
    return wireSnapshot(routes_.snapshot(net(token)));
  }

  [[nodiscard]] std::vector<drt_trace::Shape> wireSnapshot(const eccdb::NetRouteSnapshot& snapshot) const
  {
    std::vector<drt_trace::Shape> result;
    for (const auto& saved_wire : snapshot.wires) {
      for (const auto& shape : saved_wire.shapes) {
        std::visit(
            [&](const auto& value) {
              using Value = std::remove_cvref_t<decltype(value)>;
              drt_trace::Shape out;
              if constexpr (std::is_same_v<Value, eccdb::RouteSegment>) {
                out.kind = "segment";
                out.layer = routingLayerName(value.layer);
                out.x1 = value.begin.x;
                out.y1 = value.begin.y;
                out.x2 = value.end.x;
                out.y2 = value.end.y;
                out.begin_extension = value.begin_extension;
                out.end_extension = value.end_extension;
              } else if constexpr (std::is_same_v<Value, eccdb::RouteVia>) {
                out.kind = "via";
                out.x1 = value.origin.x;
                out.y1 = value.origin.y;
                out.x2 = value.origin.x;
                out.y2 = value.origin.y;
                std::visit(
                    [&](const auto definition) {
                      using Definition = std::remove_cvref_t<decltype(definition)>;
                      if constexpr (std::is_same_v<Definition, eccdb::TechViaMasterId>) {
                        out.tech_via = true;
                        out.via = technology_.viaMasterStorage().viaMaster(definition).name;
                        out.layer = routingLayerName(viaBottom(definition));
                      } else {
                        out.tech_via = false;
                        out.via = design_.routingStorage().via(definition).name;
                        out.layer = routingLayerName(viaBottom(definition));
                      }
                    },
                    value.definition);
              } else {
                out.kind = "patch";
                out.layer = routingLayerName(value.layer);
                out.x1 = value.rectangle.ll_x;
                out.y1 = value.rectangle.ll_y;
                out.x2 = value.rectangle.ur_x;
                out.y2 = value.rectangle.ur_y;
              }
              result.push_back(std::move(out));
            },
            shape);
      }
    }
    return result;
  }

  [[nodiscard]] json::value viaDefinition(std::string_view name) const
  {
    const auto id = vias_.findVia(name);
    if (!id) {
      fail("missing EccDB design VIA after replay: " + std::string(name));
    }
    const auto& value = vias_.via(id);
    if (value.flags != 0u || !value.polygons.empty()) {
      fail("EccDB design VIA has unsupported non-fixed geometry: " + std::string(name));
    }

    std::vector<drt_trace::ViaRect> rectangles;
    rectangles.reserve(value.rectangles.size());
    for (const auto& rectangle : value.rectangles) {
      if (rectangle.mask != 0u || !rectangle.layer || !rectangle.rectangle.hasArea()) {
        fail("EccDB design VIA has unsupported rectangle geometry: " + std::string(name));
      }
      rectangles.push_back({.layer = technology_.layerInfo(rectangle.layer).name,
                            .x1 = rectangle.rectangle.ll_x,
                            .y1 = rectangle.rectangle.ll_y,
                            .x2 = rectangle.rectangle.ur_x,
                            .y2 = rectangle.rectangle.ur_y});
    }
    return drt_trace::viaValue(value.name, rectangles);
  }

  [[nodiscard]] json::object wireWriteProfile() const
  {
    const auto& profile = wire_write_profile_;
    return {{"schema", "eccdb_drt_trace_wire_write_profile_v1"},
            {"scope", "replace_wire only; every *_ns value is cumulative across whole-Wire operations; "
                      "adapter conversion includes vector reserve, adaptor_total_ns includes temporary vector destruction, "
                      "and parsing/setup/final validation are excluded; convert_shapes_ns is a retained zero compatibility "
                      "field because streaming conversion, validation, Shape entity/component insertion, and owner ShapeId "
                      "append are fused into append_shapes_ns; service phase values are not nested, while service_call_ns "
                      "and service_total_ns wrap them and must not be added to them"},
            {"timer", json::object{{"clock", "std::chrono::steady_clock"},
                                    {"unit", "nanoseconds"},
                                    {"aggregation", "cumulative_ns"},
                                    {"instrumented", true},
                                    {"comparison", "diagnostic timings are not directly comparable to an uninstrumented run"}}},
            {"calls", profile.calls},
            {"counts", json::object{{"input_shapes", profile.input_shapes},
                                     {"replaced_wires", profile.replaced_wires},
                                     {"replaced_shapes", profile.replaced_shapes}}},
            {"route_shape_bytes", static_cast<uint64_t>(sizeof(eccdb::RouteShape))},
            {"stored_shape_bytes", static_cast<uint64_t>(sizeof(eccdb::DesignRouteShape))},
            {"shape_id_bytes", static_cast<uint64_t>(sizeof(eccdb::DesignRouteShapeId))},
            {"adaptor_convert_ns", wire_write_adaptor_convert_ns_},
            {"net_resolution_ns", wire_write_net_resolution_ns_},
            {"service_call_ns", wire_write_service_call_ns_},
            {"adaptor_total_ns", wire_write_adaptor_total_ns_},
            {"service_phase_semantics",
             json::object{{"convert_shapes_ns", "compatibility field; zero for streaming writes"},
                          {"append_shapes_ns",
                           "fused RouteShape conversion, validation, Shape entity/component insertion, and owner ShapeId append"}}},
            {"service", json::object{{"select_wires_ns", profile.select_wires_ns},
                                       {"convert_shapes_ns", profile.convert_shapes_ns},
                                       {"create_wire_ns", profile.create_wire_ns},
                                       {"append_shapes_ns", profile.append_shapes_ns},
                                       {"destroy_old_ns", profile.destroy_old_ns},
                                       {"service_total_ns", profile.service_total_ns}}}};
  }

 private:
  void importGuides(const std::filesystem::path& path)
  {
    if (path.empty()) return;

    eccdb::RoutingContextService context(design_);
    eccdb::tritonroute::guide_input::readGuides(path, [&](const std::string& name, const auto& rectangles) {
      const auto net_id = design_.netlistStorage().findRegularNet(name);
      if (!net_id) {
        fail("unknown guide net " + name);
      }

      std::vector<eccdb::DesignRouteGuide> guides;
      guides.reserve(rectangles.size());
      for (const auto& rectangle : rectangles) {
        const auto layer = technology_.findLayer(rectangle.layer);
        if (!layer || physical_.layerKind(layer) != eccdb::GeometryLayerKind::kRouting) {
          fail("unknown guide routing layer " + rectangle.layer);
        }
        guides.push_back({.box = {rectangle.x1, rectangle.y1, rectangle.x2, rectangle.y2},
                          .layer = eccdb::TechRoutingLayerId{layer.entity()},
                          // The native GuideProcessor stores the guide's
                          // routing layer as its via layer as well.
                          .via_layer = eccdb::TechRoutingLayerId{layer.entity()}});
      }
      // dbGuide::create prepends; preserve the ODB/GRT observable order.
      std::reverse(guides.begin(), guides.end());
      context.replaceGuides(net_id, std::move(guides));
    });
  }

  [[nodiscard]] static const char* layerTypeName(const eccdb::TechStore& technology, eccdb::TechLayerId id)
  {
    const auto& registry = technology.techRegistry().registry();
    if (registry.all_of<eccdb::TechRoutingLayer>(id.entity())) return "ROUTING";
    if (registry.all_of<eccdb::TechCutLayer>(id.entity())) return "CUT";
    if (registry.all_of<eccdb::TechImplantLayer>(id.entity())) return "IMPLANT";
    if (registry.all_of<eccdb::TechMastersliceLayer>(id.entity())) return "MASTERSLICE";
    if (registry.all_of<eccdb::TechOverlapLayer>(id.entity())) return "OVERLAP";
    fail("EccDB technology layer has no ODB layer type");
  }

  [[nodiscard]] static bool isBackside(const eccdb::TechLayerInfo& info)
  {
    return hasFlag(info.flags, eccdb::TechLayerInfoFlag::kLef58Backside);
  }

  [[nodiscard]] bool supportsRoutingLayerRules(const drt_trace::Event& event) const
  {
    // EccDB deliberately models the native SPACING and ENDOFLINE forms.  The
    // ODB v5.4 extensions below do not have an EccDB source component yet.
    if (!event.expected.is_array()) return false;
    for (const auto& layer_value : event.expected.as_array()) {
      if (!layer_value.is_array()) return false;
      const auto& layer = layer_value.as_array();
      if (layer.size() != 11u || !layer[5].is_array()) return false;
      for (const auto& rule_value : layer[5].as_array()) {
        if (!rule_value.is_array()) return false;
        const auto& rule = rule_value.as_array();
        if (rule.size() != 8u || !rule[3].is_bool() || rule[3].as_bool() || !rule[4].is_null()
            || !rule[5].is_bool() || rule[5].as_bool() || !rule[6].is_bool() || rule[6].as_bool()
            || !rule[7].is_bool() || rule[7].as_bool()) {
          return false;
        }
      }
    }
    const auto& storage = technology_.routingLayerStorage();
    for (const auto id : storage.registry().view<const eccdb::TechRoutingLayer>()) {
      const auto layer = eccdb::TechRoutingLayerId{id};
      if (storage.minStepRules(layer).size() > 1u || storage.prlSpacingTableRules(layer).size() > 1u
          || storage.twoWidthsSpacingTableRules(layer).size() > 1u) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool supportsCutLayerRules() const
  {
    const auto& storage = technology_.cutLayerStorage();
    for (const auto entity : storage.registry().view<const eccdb::TechCutLayer>()) {
      const auto id = eccdb::TechCutLayerId{entity};
      if (storage.orthogonalSpacingTableRules(id).size() > 1u) return false;
    }
    return true;
  }

  [[nodiscard]] json::value readTechLayers() const
  {
    json::array result;
    const auto& registry = technology_.techRegistry().registry();
    const auto& sequence = technology_.layerSequence();
    for (uint32_t index = 0; index < sequence.size(); ++index) {
      const auto layer = sequence[index];
      const auto& info = technology_.layerInfo(layer);
      if (isBackside(info)) continue;
      const auto routing = eccdb::TechRoutingLayerId{layer.entity()};
      const bool is_routing = registry.all_of<eccdb::TechRoutingLayer>(layer.entity());
      const auto direction = is_routing ? routingDirectionName(technology_.routingLayerStorage().routingLayer(routing).direction) : "NONE";
      const auto level = is_routing ? static_cast<int64_t>(technology_.routingLevel(routing).value_or(0u)) : 0;
      result.emplace_back(json::array{info.name, layerTypeName(technology_, layer), static_cast<int64_t>(index), level, direction});
    }

    // ODB assigns non-process layers after the physical layer sequence.  The
    // current EccDB overlap layer is the only such layer used by DRT.
    uint32_t extra_index = static_cast<uint32_t>(sequence.size());
    const auto view = registry.view<const eccdb::TechLayerInfo>();
    for (const auto entity : view) {
      const auto layer = eccdb::TechLayerId{entity};
      if (std::find(sequence.begin(), sequence.end(), layer) != sequence.end()) continue;
      const auto& info = view.get<const eccdb::TechLayerInfo>(entity);
      if (isBackside(info)) continue;
      const auto routing = eccdb::TechRoutingLayerId{entity};
      const bool is_routing = registry.all_of<eccdb::TechRoutingLayer>(entity);
      const auto direction = is_routing ? routingDirectionName(technology_.routingLayerStorage().routingLayer(routing).direction) : "NONE";
      const auto level = is_routing ? static_cast<int64_t>(technology_.routingLevel(routing).value_or(0u)) : 0;
      result.emplace_back(json::array{info.name, layerTypeName(technology_, layer), static_cast<int64_t>(extra_index++), level, direction});
    }
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] json::value readRoutingLayerRules() const
  {
    json::array result;
    const auto& storage = technology_.routingLayerStorage();
    physical_.forEachRoutingLayer([&](eccdb::TechRoutingLayerId id, uint32_t, uint32_t level,
                                      const eccdb::TechLayerInfo& info, const eccdb::TechRoutingLayer& layer) {
      if (isBackside(info) || level == 0u) return;

      json::array v54;
      for (const auto rule_id : storage.spacingRules(id)) {
        const auto& rule = storage.rule(rule_id);
        const json::value range = rule.type == eccdb::TechRoutingSpacingType::kRange
                                      ? json::value(json::array{rule.min_width, rule.max_width})
                                      : json::value(nullptr);
        v54.emplace_back(json::array{rule.min_spacing, range, nullptr, false, nullptr, false, false, false});
      }
      for (const auto rule_id : storage.endOfLineSpacingRules(id)) {
        const auto& rule = storage.rule(rule_id);
        const bool parallel = hasFlag(rule.flags, eccdb::TechRoutingEndOfLineSpacingRuleFlag::kHasParallelEdge);
        const auto eol = json::array{rule.eol_width,
                                     rule.eol_within,
                                     parallel,
                                     parallel ? json::value(rule.parallel_space) : json::value(0),
                                     parallel ? json::value(rule.parallel_within) : json::value(0),
                                     hasFlag(rule.flags, eccdb::TechRoutingEndOfLineSpacingRuleFlag::kTwoEdges)};
        v54.emplace_back(json::array{rule.min_spacing, nullptr, std::move(eol), false, nullptr, false, false, false});
      }

      json::array influences;
      for (const auto rule_id : storage.influenceSpacingTableRules(id)) {
        for (const auto& entry : storage.rule(rule_id).entries) {
          influences.emplace_back(json::array{entry.width, entry.within, entry.spacing});
        }
      }

      json::value min_step = nullptr;
      const auto min_step_rules = storage.minStepRules(id);
      if (!min_step_rules.empty()) {
        const auto& rule = storage.rule(min_step_rules.front());
        min_step = json::array{rule.min_step_length,
                               minStepTypeName(rule.type),
                               hasFlag(rule.flags, eccdb::TechRoutingMinStepRuleFlag::kHasMaxLengthSum)
                                   ? json::value(rule.max_length_sum)
                                   : json::value(nullptr),
                               hasFlag(rule.flags, eccdb::TechRoutingMinStepRuleFlag::kHasMaxEdges)
                                   ? json::value(rule.max_edges)
                                   : json::value(nullptr)};
      }

      json::value v55 = nullptr;
      const auto prl_rules = storage.prlSpacingTableRules(id);
      if (!prl_rules.empty()) {
        const auto& rule = storage.rule(prl_rules.front());
        json::array widths;
        json::array lengths;
        json::array table;
        for (const auto value : rule.widths) widths.emplace_back(value);
        for (const auto value : rule.parallel_run_lengths) lengths.emplace_back(value);
        const auto column_count = rule.parallel_run_lengths.size();
        for (std::size_t row = 0; row < rule.widths.size(); ++row) {
          json::array values;
          for (std::size_t column = 0; column < column_count; ++column) values.emplace_back(rule.cells[row * column_count + column]);
          table.emplace_back(std::move(values));
        }
        v55 = json::array{std::move(widths), std::move(lengths), std::move(table)};
      }

      json::value two_widths = nullptr;
      const auto two_width_rules = storage.twoWidthsSpacingTableRules(id);
      if (!two_width_rules.empty()) {
        const auto& rule = storage.rule(two_width_rules.front());
        json::array rows;
        json::array table;
        for (const auto& width : rule.widths) rows.emplace_back(json::array{width.width, width.has_prl, width.has_prl ? json::value(width.prl) : json::value(nullptr)});
        const auto count = rule.widths.size();
        for (std::size_t row = 0; row < count; ++row) {
          json::array values;
          for (std::size_t column = 0; column < count; ++column) values.emplace_back(rule.cells[row * count + column]);
          table.emplace_back(std::move(values));
        }
        two_widths = json::array{std::move(rows), std::move(table)};
      }

      json::array minimum_cuts;
      for (const auto rule_id : storage.minimumCutRules(id)) {
        const auto& rule = storage.rule(rule_id);
        const bool above = rule.orient == eccdb::TechRoutingMinimumCutOrient::kFromAbove;
        const bool below = rule.orient == eccdb::TechRoutingMinimumCutOrient::kFromBelow;
        const auto length = hasFlag(rule.flags, eccdb::TechRoutingMinimumCutRuleFlag::kHasLength)
                                ? json::value(json::array{rule.length, rule.length_distance})
                                : json::value(nullptr);
        minimum_cuts.emplace_back(json::array{rule.num_cuts,
                                              rule.width,
                                              hasFlag(rule.flags, eccdb::TechRoutingMinimumCutRuleFlag::kHasWithinCutDistance)
                                                  ? json::value(rule.within_cut_distance)
                                                  : json::value(nullptr),
                                              above,
                                              below,
                                              length});
      }

      json::array minimum_enclosures;
      for (const auto rule_id : storage.minEncloseAreaRules(id)) {
        const auto& rule = storage.rule(rule_id);
        minimum_enclosures.emplace_back(json::array{rule.area, rule.width >= 0 ? json::value(rule.width) : json::value(nullptr)});
      }

      const auto min_width = hasFlag(layer.flags, eccdb::TechRoutingLayerFlag::kHasMinWidth) ? layer.min_width : layer.width;
      result.emplace_back(json::array{info.name,
                                      layer.width,
                                      min_width,
                                      hasFlag(layer.flags, eccdb::TechRoutingLayerFlag::kHasArea) ? json::value(layer.area)
                                                                                                   : json::value(nullptr),
                                      std::move(min_step),
                                      drt_trace::canonical(std::move(v54)),
                                      drt_trace::canonical(std::move(influences)),
                                      std::move(v55),
                                      std::move(two_widths),
                                      drt_trace::canonical(std::move(minimum_cuts)),
                                      drt_trace::canonical(std::move(minimum_enclosures))});
    });
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] json::value readCutLayerRules() const
  {
    json::array result;
    const auto& sequence = technology_.layerSequence();
    const auto& storage = technology_.cutLayerStorage();
    for (std::size_t index = 0; index < sequence.size(); ++index) {
      const auto layer = sequence[index];
      const auto cut = eccdb::TechCutLayerId{layer.entity()};
      if (!storage.contains(cut)) continue;
      const auto& info = technology_.layerInfo(layer);
      if (isBackside(info) || info.lef58_type == eccdb::TechLef58LayerType::kMimCap) continue;
      if (index > 0u && storage.contains(eccdb::TechCutLayerId{sequence[index - 1u].entity()})) continue;

      json::array rules;
      for (const auto rule_id : storage.spacingRules(cut)) {
        const auto& rule = storage.spacingRule(rule_id);
        const bool adjacent = hasFlag(rule.flags, eccdb::TechCutSpacingRuleFlag::kHasAdjacentCuts);
        rules.emplace_back(json::array{rule.spacing,
                                       hasFlag(rule.flags, eccdb::TechCutSpacingRuleFlag::kHasCutArea) ? json::value(rule.cut_area)
                                                                                                        : json::value(0),
                                       hasFlag(rule.flags, eccdb::TechCutSpacingRuleFlag::kCenterToCenter),
                                       hasFlag(rule.flags, eccdb::TechCutSpacingRuleFlag::kSameNet),
                                       hasFlag(rule.flags, eccdb::TechCutSpacingRuleFlag::kStack),
                                       hasFlag(rule.flags, eccdb::TechCutSpacingRuleFlag::kSameNetPgOnly),
                                       hasFlag(rule.flags, eccdb::TechCutSpacingRuleFlag::kParallelOverlap),
                                       hasFlag(rule.flags, eccdb::TechCutSpacingRuleFlag::kHasSecondLayer)
                                           ? json::value(rule.second_layer_name)
                                           : json::value(nullptr),
                                       adjacent ? json::value(json::array{rule.adjacent_cut_count,
                                                                          rule.adjacent_cut_within,
                                                                          rule.spacing,
                                                                          hasFlag(rule.flags, eccdb::TechCutSpacingRuleFlag::kExceptSamePgNet)})
                                                : json::value(nullptr)});
      }

      json::value orthogonal = nullptr;
      const auto tables = storage.orthogonalSpacingTableRules(cut);
      if (!tables.empty()) {
        json::array entries;
        for (const auto& item : storage.orthogonalSpacingTableRule(tables.front()).items) {
          entries.emplace_back(json::array{item.within, item.spacing});
        }
        orthogonal = drt_trace::canonical(std::move(entries));
      }
      result.emplace_back(json::array{info.name, drt_trace::canonical(std::move(rules)), std::move(orthogonal)});
    }
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] std::string layerName(eccdb::TechLayerId layer) const
  {
    if (!layer || !technology_.contains(layer)) fail("EccDB projection references an invalid technology layer");
    return technology_.layerInfo(layer).name;
  }

  [[nodiscard]] std::string conductorLayerName(eccdb::TechConductorLayerRef layer) const
  {
    if (!layer) fail("EccDB projection references an empty VIA conductor layer");
    return layerName(layer.layer());
  }

  [[nodiscard]] static bool expectedViasUseOnlyKnownKinds(const drt_trace::Event& event, bool design)
  {
    if (!event.expected.is_array()) return false;
    for (const auto& value : event.expected.as_array()) {
      if (!value.is_array()) return false;
      const auto& via = value.as_array();
      if (via.size() != 6u || !via[1].is_bool() || !via[2].is_string()) return false;
      const auto kind = std::string_view(via[2].as_string().data(), via[2].as_string().size());
      if (kind != "rectangles" && kind != "generated") return false;
      // EccDB's DesignVia deliberately has no DEFAULT bit. A false value is
      // exact; a true value must be reported unsupported instead of invented.
      if (design && via[1].as_bool()) return false;
    }
    return true;
  }

  [[nodiscard]] bool supportsTechVias(const drt_trace::Event& event) const
  {
    if (!expectedViasUseOnlyKnownKinds(event, false)) return false;
    const auto& vias = technology_.viaMasterStorage();
    for (const auto id : vias.viaMasters()) {
      const auto& geometry = vias.geometry(id);
      if (technology_.geometryPool().polygonCount(geometry.bottom_geometry) != 0u
          || technology_.geometryPool().polygonCount(geometry.cut_geometry) != 0u
          || technology_.geometryPool().polygonCount(geometry.top_geometry) != 0u) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool supportsDesignVias(const drt_trace::Event& event) const
  {
    if (!expectedViasUseOnlyKnownKinds(event, true)) return false;
    for (const auto id : design_.routingStorage().vias()) {
      if (!design_.routingStorage().via(id).polygons.empty()) return false;
    }
    return true;
  }

  [[nodiscard]] bool supportsViaGenerateRules(const drt_trace::Event& event) const
  {
    if (!event.expected.is_array()) return false;
    // CUT VIARULE clauses have no direction field in EccDB, which agrees with
    // ODB's normal NONE value. Refuse a future contract that requires more.
    for (const auto& value : event.expected.as_array()) {
      if (!value.is_array()) return false;
      const auto& rule = value.as_array();
      if (rule.size() != 3u || !rule[2].is_array()) return false;
      for (const auto& layer_value : rule[2].as_array()) {
        if (!layer_value.is_array()) return false;
        const auto& layer = layer_value.as_array();
        if (layer.size() != 9u || !layer[0].is_string() || !layer[1].is_string()) return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool supportsLibraryGeometry() const
  {
    const auto& geometry = library_.geometryPool();
    const auto& masters = library_.cellMasterStorage();
    const auto& ports = library_.masterPortStorage();
    for (const auto master_id : masters.cellMasters()) {
      const auto& master = masters.cellMaster(master_id);
      for (const auto term_id : master.terms) {
        const auto& term = library_.masterTermStorage().masterTerm(term_id);
        for (const auto port_id : term.ports) {
          const auto& port = ports.masterPort(port_id);
          for (const auto& clause : port.layer_clauses) {
            if (geometry.polygonCount(clause.geometry) != 0u) return false;
          }
        }
      }
      if (!masters.hasObs(master_id)) continue;
      const auto& obs = masters.obs(master_id);
      if (!obs.vias.empty()) return false;
      for (const auto& clause : obs.layer_clauses) {
        if (geometry.polygonCount(clause.geometry) != 0u) return false;
      }
    }
    return true;
  }

  void appendTechViaRectangles(json::array& output, eccdb::TechViaMasterId id) const
  {
    const auto& vias = technology_.viaMasterStorage();
    const auto& geometry = vias.geometry(id);
    const auto append = [&](eccdb::TechLayerId layer, eccdb::GeometryHandle handle) {
      for (const auto rect : technology_.geometryPool().rectangles(handle)) {
        output.emplace_back(json::array{layerName(layer), rect.ll_x, rect.ll_y, rect.ur_x, rect.ur_y});
      }
    };
    append(geometry.bottom_layer.layer(), geometry.bottom_geometry);
    append(eccdb::TechLayerId{geometry.cut_layer.entity()}, geometry.cut_geometry);
    append(geometry.top_layer.layer(), geometry.top_geometry);
  }

  [[nodiscard]] json::value techViaParams(eccdb::TechViaMasterId id) const
  {
    const auto& vias = technology_.viaMasterStorage();
    const auto& geometry = vias.geometry(id);
    const auto& generated = vias.generatedViaMaster(id);
    return json::array{layerName(geometry.bottom_layer.layer()),
                       layerName(eccdb::TechLayerId{geometry.cut_layer.entity()}),
                       layerName(geometry.top_layer.layer()),
                       generated.cut_size_x,
                       generated.cut_size_y,
                       generated.cut_spacing_x,
                       generated.cut_spacing_y,
                       generated.top_enclosure_x,
                       generated.top_enclosure_y,
                       generated.bottom_enclosure_x,
                       generated.bottom_enclosure_y,
                       generated.row_count,
                       generated.column_count,
                       generated.origin_x,
                       generated.origin_y,
                       generated.top_offset_x,
                       generated.top_offset_y,
                       generated.bottom_offset_x,
                       generated.bottom_offset_y};
  }

  [[nodiscard]] json::value techViaValue(eccdb::TechViaMasterId id) const
  {
    const auto& vias = technology_.viaMasterStorage();
    const auto& master = vias.viaMaster(id);
    json::array rectangles;
    appendTechViaRectangles(rectangles, id);
    if (!vias.hasGeneratedViaMaster(id)) {
      return json::array{master.name,
                         hasFlag(master.flags, eccdb::TechViaMasterFlag::kDefault),
                         "rectangles",
                         nullptr,
                         nullptr,
                         drt_trace::canonical(std::move(rectangles))};
    }
    const auto& generated = vias.generatedViaMaster(id);
    if (!generated.via_rule_generate || !technology_.viaRuleGenerateStorage().contains(generated.via_rule_generate)) {
      fail("EccDB generated technology VIA has no valid VIARULE");
    }
    return json::array{master.name,
                       hasFlag(master.flags, eccdb::TechViaMasterFlag::kDefault),
                       "generated",
                       technology_.viaRuleGenerateStorage().viaRuleGenerate(generated.via_rule_generate).name,
                       techViaParams(id),
                       drt_trace::canonical(std::move(rectangles))};
  }

  [[nodiscard]] json::value readTechVias() const
  {
    json::array result;
    for (const auto id : technology_.viaMasterStorage().viaMasters()) result.emplace_back(techViaValue(id));
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] json::value designViaParams(const eccdb::DesignGeneratedVia& generated) const
  {
    return json::array{layerName(eccdb::TechLayerId{generated.bottom_layer.entity()}),
                       layerName(eccdb::TechLayerId{generated.cut_layer.entity()}),
                       layerName(eccdb::TechLayerId{generated.top_layer.entity()}),
                       generated.cut_size_x,
                       generated.cut_size_y,
                       generated.cut_spacing_x,
                       generated.cut_spacing_y,
                       generated.top_enclosure_x,
                       generated.top_enclosure_y,
                       generated.bottom_enclosure_x,
                       generated.bottom_enclosure_y,
                       generated.row_count,
                       generated.column_count,
                       generated.origin.x,
                       generated.origin.y,
                       generated.top_offset.x,
                       generated.top_offset.y,
                       generated.bottom_offset.x,
                       generated.bottom_offset.y};
  }

  [[nodiscard]] json::value designViaValue(eccdb::DesignViaId id) const
  {
    const auto& value = design_.routingStorage().via(id);
    json::array rectangles;
    // Generated DEF VIAs retain their rule parameters rather than duplicating
    // their materialized boxes.  The physical service expands them with the
    // same centered cut-array semantics as OpenDB's dbVia boxes.
    physical_.forEachDesignViaGeometry(id, {.rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
                                              rectangles.emplace_back(json::array{layerName(rectangle.layer),
                                                                                  rectangle.rectangle.ll_x,
                                                                                  rectangle.rectangle.ll_y,
                                                                                  rectangle.rectangle.ur_x,
                                                                                  rectangle.rectangle.ur_y});
                                            }});
    const bool generated = hasFlag(value.flags, eccdb::DesignViaFlag::kGenerated);
    if (!generated) {
      return json::array{value.name, false, "rectangles", nullptr, nullptr, drt_trace::canonical(std::move(rectangles))};
    }
    if (!value.generated.via_rule || !technology_.viaRuleGenerateStorage().contains(value.generated.via_rule)) {
      fail("EccDB generated design VIA has no valid VIARULE");
    }
    return json::array{value.name,
                       false,
                       "generated",
                       technology_.viaRuleGenerateStorage().viaRuleGenerate(value.generated.via_rule).name,
                       designViaParams(value.generated),
                       drt_trace::canonical(std::move(rectangles))};
  }

  [[nodiscard]] json::value readDesignVias() const
  {
    json::array result;
    for (const auto id : design_.routingStorage().vias()) result.emplace_back(designViaValue(id));
    return drt_trace::canonical(std::move(result));
  }

  template <typename RoutingClause>
  [[nodiscard]] json::value viaGenerateRoutingLayerValue(const RoutingClause& clause) const
  {
    return json::array{conductorLayerName(clause.layer),
                       routingDirectionName(clause.direction),
                       hasFlag(clause.flags, eccdb::TechViaRuleGenerateRoutingLayerFlag::kHasWidth)
                           ? json::value(json::array{clause.min_width, clause.max_width})
                           : json::value(nullptr),
                       hasFlag(clause.flags, eccdb::TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure)
                           ? json::value(json::array{clause.enclosure_overhang1, clause.enclosure_overhang2})
                           : json::value(nullptr),
                       hasFlag(clause.flags, eccdb::TechViaRuleGenerateRoutingLayerFlag::kHasOverhang)
                           ? json::value(clause.overhang)
                           : json::value(nullptr),
                       hasFlag(clause.flags, eccdb::TechViaRuleGenerateRoutingLayerFlag::kHasMetalOverhang)
                           ? json::value(clause.metal_overhang)
                           : json::value(nullptr),
                       nullptr,
                       nullptr,
                       nullptr};
  }

  [[nodiscard]] json::value readViaGenerateRules() const
  {
    json::array result;
    const auto& storage = technology_.viaRuleGenerateStorage();
    for (const auto id : storage.viaRuleGenerates()) {
      const auto& rule = storage.viaRuleGenerate(id);
      const auto& bottom = storage.bottomLayer(id);
      const auto& cut = storage.cutLayer(id);
      const auto& top = storage.topLayer(id);
      const auto cut_rect = hasFlag(cut.flags, eccdb::TechViaRuleGenerateCutLayerFlag::kHasRect)
                                ? json::value(json::array{cut.cut_rect.ll_x, cut.cut_rect.ll_y, cut.cut_rect.ur_x, cut.cut_rect.ur_y})
                                : json::value(nullptr);
      const auto cut_spacing = hasFlag(cut.flags, eccdb::TechViaRuleGenerateCutLayerFlag::kHasSpacing)
                                   ? json::value(json::array{cut.spacing_x, cut.spacing_y})
                                   : json::value(nullptr);
      const auto cut_resistance = hasFlag(cut.flags, eccdb::TechViaRuleGenerateCutLayerFlag::kHasResistance)
                                      ? json::value(cut.resistance_per_cut)
                                      : json::value(nullptr);
      json::array layers;
      layers.emplace_back(viaGenerateRoutingLayerValue(bottom));
      layers.emplace_back(json::array{layerName(eccdb::TechLayerId{cut.layer.entity()}),
                                      "NONE",
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      std::move(cut_rect),
                                      std::move(cut_spacing),
                                      std::move(cut_resistance)});
      layers.emplace_back(viaGenerateRoutingLayerValue(top));
      result.emplace_back(json::array{rule.name,
                                      rule.isDefault(),
                                      drt_trace::canonical(std::move(layers))});
    }
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] json::value ndrValue(std::string_view origin, const eccdb::TechNonDefaultRule& rule,
                                      const std::vector<eccdb::TechNdrRoutingRule>& layer_rules,
                                      const std::vector<eccdb::TechViaMasterId>& vias,
                                      const std::vector<eccdb::TechViaRuleGenerateId>& via_rules) const
  {
    json::array projected_layers;
    for (const auto& layer_rule : layer_rules) {
      const auto layer = eccdb::TechLayerId{layer_rule.layer.entity()};
      if (isBackside(technology_.layerInfo(layer))) continue;
      projected_layers.emplace_back(json::array{layerName(layer),
                                                layer_rule.width,
                                                hasFlag(layer_rule.flags, eccdb::TechNdrRoutingRuleFlag::kHasSpacing)
                                                    ? json::value(layer_rule.spacing)
                                                    : json::value(0),
                                                hasFlag(layer_rule.flags, eccdb::TechNdrRoutingRuleFlag::kHasWireExtension)
                                                    ? json::value(layer_rule.wire_extension)
                                                    : json::value(0)});
    }
    json::array projected_vias;
    for (const auto via : vias) {
      const auto& geometry = technology_.viaMasterStorage().geometry(via);
      if (isBackside(technology_.layerInfo(geometry.bottom_layer.layer()))
          || isBackside(technology_.layerInfo(geometry.top_layer.layer()))) {
        continue;
      }
      projected_vias.emplace_back(technology_.viaMasterStorage().viaMaster(via).name);
    }
    json::array projected_via_rules;
    for (const auto via_rule : via_rules) {
      const auto& storage = technology_.viaRuleGenerateStorage();
      const auto& bottom = storage.bottomLayer(via_rule);
      const auto& top = storage.topLayer(via_rule);
      if (isBackside(technology_.layerInfo(bottom.layer.layer())) || isBackside(technology_.layerInfo(top.layer.layer()))) continue;
      projected_via_rules.emplace_back(storage.viaRuleGenerate(via_rule).name);
    }
    return json::array{std::string(origin),
                       rule.name,
                       hasFlag(rule.flags, eccdb::TechNonDefaultRuleFlag::kHardSpacing),
                       drt_trace::canonical(std::move(projected_layers)),
                       drt_trace::canonical(std::move(projected_vias)),
                       drt_trace::canonical(std::move(projected_via_rules))};
  }

  [[nodiscard]] json::value readNdrs() const
  {
    json::array result;
    const auto& tech_ndr = technology_.nonDefaultRuleStorage();
    for (const auto id : tech_ndr.nonDefaultRules()) {
      result.emplace_back(ndrValue("tech",
                                   tech_ndr.nonDefaultRule(id),
                                   tech_ndr.routingRules(id),
                                   tech_ndr.useVias(id),
                                   tech_ndr.useViaRules(id)));
    }
    for (const auto id : design_.routingStorage().nonDefaultRules()) {
      const auto& rule = design_.routingStorage().nonDefaultRule(id);
      json::array projected_layers;
      for (const auto& layer_rule : rule.layer_rules) {
        const auto layer = eccdb::TechLayerId{layer_rule.layer.entity()};
        if (isBackside(technology_.layerInfo(layer))) continue;
        projected_layers.emplace_back(json::array{layerName(layer),
                                                  layer_rule.width,
                                                  hasFlag(layer_rule.flags, eccdb::DesignNdrLayerRuleFlag::kHasSpacing)
                                                      ? json::value(layer_rule.spacing)
                                                      : json::value(0),
                                                  hasFlag(layer_rule.flags, eccdb::DesignNdrLayerRuleFlag::kHasWireExtension)
                                                      ? json::value(layer_rule.wire_extension)
                                                      : json::value(0)});
      }
      json::array projected_vias;
      for (const auto& via : rule.vias) {
        if (via.tech_via) projected_vias.emplace_back(technology_.viaMasterStorage().viaMaster(via.tech_via).name);
        else if (via.design_via) projected_vias.emplace_back(design_.routingStorage().via(via.design_via).name);
        else fail("EccDB design NDR has an empty VIA reference");
      }
      json::array projected_via_rules;
      for (const auto via_rule : rule.via_rules) {
        projected_via_rules.emplace_back(technology_.viaRuleGenerateStorage().viaRuleGenerate(via_rule).name);
      }
      result.emplace_back(json::array{"block",
                                      rule.name,
                                      hasFlag(rule.flags, eccdb::DesignNonDefaultRuleFlag::kHardSpacing),
                                      drt_trace::canonical(std::move(projected_layers)),
                                      drt_trace::canonical(std::move(projected_vias)),
                                      drt_trace::canonical(std::move(projected_via_rules))});
    }
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] json::value readLibraryMasters() const
  {
    json::array result;
    const auto& masters = library_.cellMasterStorage();
    for (const auto id : masters.cellMasters()) {
      const auto& master = masters.cellMaster(id);
      result.emplace_back(json::array{master.name,
                                      cellMasterTypeName(master.type),
                                      master.origin_x,
                                      master.origin_y,
                                      master.width,
                                      master.height});
    }
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] json::value readLibraryTerms() const
  {
    json::array result;
    const auto& masters = library_.cellMasterStorage();
    const auto& terms = library_.masterTermStorage();
    for (const auto master_id : masters.cellMasters()) {
      const auto& master = masters.cellMaster(master_id);
      for (const auto term_id : master.terms) {
        const auto& term = terms.masterTerm(term_id);
        result.emplace_back(json::array{master.name,
                                        term.name,
                                        masterTermUseName(term.use),
                                        static_cast<int64_t>(term.ports.size())});
      }
    }
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] json::value readLibraryMPinGeometry() const
  {
    json::array result;
    const auto& masters = library_.cellMasterStorage();
    const auto& terms = library_.masterTermStorage();
    const auto& ports = library_.masterPortStorage();
    for (const auto master_id : masters.cellMasters()) {
      const auto& master = masters.cellMaster(master_id);
      for (const auto term_id : master.terms) {
        const auto& term = terms.masterTerm(term_id);
        for (std::size_t ordinal = 0; ordinal < term.ports.size(); ++ordinal) {
          const auto& port = ports.masterPort(term.ports[ordinal]);
          json::array geometry;
          for (const auto& clause : port.layer_clauses) {
            for (const auto rect : library_.geometryPool().rectangles(clause.geometry)) {
              geometry.emplace_back(json::array{"rect", layerName(clause.layer), rect.ll_x, rect.ll_y, rect.ur_x, rect.ur_y});
            }
          }
          for (const auto& via : port.vias) {
            if (!technology_.viaMasterStorage().contains(via.via)) fail("EccDB library PORT references an invalid VIA");
            geometry.emplace_back(json::array{"tech_via",
                                              technology_.viaMasterStorage().viaMaster(via.via).name,
                                              via.origin.x,
                                              via.origin.y});
          }
          result.emplace_back(json::array{master.name,
                                          term.name,
                                          static_cast<int64_t>(ordinal),
                                          drt_trace::canonical(std::move(geometry))});
        }
      }
    }
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] json::value readLibraryObstructions() const
  {
    json::array result;
    const auto& masters = library_.cellMasterStorage();
    for (const auto master_id : masters.cellMasters()) {
      const auto& master = masters.cellMaster(master_id);
      if (!masters.hasObs(master_id)) continue;
      const auto& obs = masters.obs(master_id);
      for (const auto& clause : obs.layer_clauses) {
        const auto width = hasFlag(clause.flags, eccdb::LibraryObsLayerFlag::kHasDesignRuleWidth)
                               ? clause.design_rule_width
                               // OpenDB exposes an omitted OBS DESIGNRULEWIDTH
                               // as -1, not zero.
                               : -1;
        for (const auto rect : library_.geometryPool().rectangles(clause.geometry)) {
          result.emplace_back(json::array{master.name,
                                          layerName(clause.layer),
                                          rect.ll_x,
                                          rect.ll_y,
                                          rect.ur_x,
                                          rect.ur_y,
                                          width});
        }
      }
    }
    return drt_trace::canonical(std::move(result));
  }

  void defineDesignVia(const drt_trace::Event& event)
  {
    if (event.name.empty() || event.via_rectangles.empty()) {
      fail("trace design VIA has no name or geometry");
    }
    if (vias_.findVia(event.name)) {
      fail("trace design VIA already exists in EccDB: " + event.name);
    }

    eccdb::DesignVia value{.name = event.name};
    value.rectangles.reserve(event.via_rectangles.size());
    for (const auto& source : event.via_rectangles) {
      if (source.layer.empty() || source.x1 >= source.x2 || source.y1 >= source.y2) {
        fail("trace design VIA has invalid rectangle geometry: " + event.name);
      }
      const auto layer = technology_.findLayer(source.layer);
      if (!layer) {
        fail("trace design VIA references unknown EccDB layer '" + source.layer + "'");
      }
      const auto kind = physical_.layerKind(layer);
      if (kind != eccdb::GeometryLayerKind::kRouting && kind != eccdb::GeometryLayerKind::kCut) {
        fail("trace design VIA layer is not an EccDB routing or cut layer: '" + source.layer + "'");
      }
      value.rectangles.push_back({.layer = layer, .rectangle = {source.x1, source.y1, source.x2, source.y2}});
    }

    // ODB keeps DEFAULT as block-VIA selection metadata. EccDB's DesignVia
    // deliberately models geometry only, which is the replay comparison scope.
    static_cast<void>(event.via_default);
    static_cast<void>(vias_.createVia(std::move(value)));
  }

  void bind(const json::object& header)
  {
    const auto instance_names = tokenNames(header, "instances");
    instances_.resize(instance_names.size());
    for (std::size_t token = 0; token < instance_names.size(); ++token) {
      const auto& name = instance_names[token];
      if (name.empty()) continue;
      const auto id = physical_.findInstance(name);
      if (!id) {
        fail("EccDB token binding cannot find instance '" + name + "'");
      }
      instances_[token] = id;
    }
    const auto net_names = tokenNames(header, "nets");
    nets_.resize(net_names.size());
    for (std::size_t token = 0; token < net_names.size(); ++token) {
      const auto& name = net_names[token];
      if (name.empty()) continue;
      const auto id = physical_.findRegularNet(name);
      if (!id) {
        fail("EccDB token binding cannot find ordinary net '" + name + "'");
      }
      nets_[token] = id;
    }
  }

  void restoreInitialGCellGrid(const json::object& header)
  {
    const auto* value = header.if_contains("initial_gcell_grid");
    if (value == nullptr) return;

    auto& floorplan = design_.floorplanStorage();
    for (const auto id : floorplan.gcellGrids()) static_cast<void>(floorplan.destroyGCellGrid(id));
    const auto grid = parseInitialGCellGrid(*value);
    if (!grid) return;

    static_cast<void>(floorplan.createGCellGrid(
        {.axis = eccdb::DesignAxis::kX,
         .start = grid->x_start,
         .line_count = static_cast<uint32_t>(grid->x_count),
         .step = grid->x_step}));
    static_cast<void>(floorplan.createGCellGrid(
        {.axis = eccdb::DesignAxis::kY,
         .start = grid->y_start,
         .line_count = static_cast<uint32_t>(grid->y_count),
         .step = grid->y_step}));
  }

  [[nodiscard]] eccdb::DesignInstanceId instance(uint32_t token) const
  {
    if (token >= instances_.size() || !instances_[token]) {
      fail("unknown EccDB instance token " + std::to_string(token));
    }
    return instances_[token];
  }

  [[nodiscard]] eccdb::DesignNetId net(uint32_t token) const
  {
    if (token >= nets_.size() || !nets_[token]) {
      fail("unknown EccDB net token " + std::to_string(token));
    }
    return nets_[token];
  }

  [[nodiscard]] eccdb::TechRoutingLayerId routingLayer(std::string_view name) const
  {
    const auto layer = technology_.findLayer(name);
    const auto routing = eccdb::TechRoutingLayerId{layer.entity()};
    if (!layer || !technology_.routingLayerStorage().contains(routing)) {
      fail("trace route layer '" + std::string(name) + "' is not an EccDB routing layer");
    }
    return routing;
  }

  [[nodiscard]] std::string routingLayerName(eccdb::TechRoutingLayerId layer) const
  {
    if (!layer || !technology_.routingLayerStorage().contains(layer)) {
      fail("EccDB route primitive references an invalid routing layer");
    }
    return technology_.layerInfo(eccdb::TechLayerId{layer.entity()}).name;
  }

  [[nodiscard]] eccdb::TechRoutingLayerId viaBottom(eccdb::TechViaMasterId via) const
  {
    const auto& vias = technology_.viaMasterStorage();
    if (!via || !vias.contains(via)) {
      fail("EccDB route VIA references an invalid technology VIA");
    }
    const auto& geometry = vias.geometry(via);
    if (geometry.bottom_layer.kind != eccdb::TechConductorLayerKind::kRouting) {
      fail("EccDB technology VIA has no routing bottom layer");
    }
    const auto bottom = eccdb::TechRoutingLayerId{geometry.bottom_layer.entity};
    if (!technology_.routingLayerStorage().contains(bottom)) {
      fail("EccDB technology VIA has an invalid routing bottom layer");
    }
    return bottom;
  }

  [[nodiscard]] eccdb::TechRoutingLayerId viaBottom(eccdb::DesignViaId via) const
  {
    const auto& routing = design_.routingStorage();
    if (!via || !routing.contains(via)) {
      fail("EccDB route VIA references an invalid design VIA");
    }
    const auto& definition = routing.via(via);
    if ((definition.flags & eccdb::DesignViaFlag::kGenerated) != 0u) {
      const auto bottom = definition.generated.bottom_layer;
      if (!bottom || !technology_.routingLayerStorage().contains(bottom)) {
        fail("EccDB generated design VIA has an invalid routing bottom layer");
      }
      return bottom;
    }

    std::vector<eccdb::TechRoutingLayerId> layers;
    const auto append = [&](eccdb::TechLayerId layer) {
      const auto routing_layer = eccdb::TechRoutingLayerId{layer.entity()};
      if (technology_.routingLayerStorage().contains(routing_layer)
          && std::find(layers.begin(), layers.end(), routing_layer) == layers.end()) {
        layers.push_back(routing_layer);
      }
    };
    for (const auto& rectangle : definition.rectangles) append(rectangle.layer);
    for (const auto& polygon : definition.polygons) append(polygon.layer);
    if (layers.size() != 2u) {
      fail("EccDB fixed design VIA does not have exactly two routing layers");
    }
    if (layers[0] == layers[1]) {
      fail("EccDB design VIA uses the same routing layer on both sides");
    }
    if (technology_.isBelow(eccdb::TechLayerId{layers[1].entity()}, eccdb::TechLayerId{layers[0].entity()})) {
      std::swap(layers[0], layers[1]);
    }
    return layers.front();
  }

  [[nodiscard]] eccdb::TechRoutingLayerId viaBottom(const eccdb::RouteViaDefinition& definition) const
  {
    return std::visit([&](const auto via) { return viaBottom(via); }, definition);
  }

  [[nodiscard]] eccdb::RouteShape routeShape(const drt_trace::Shape& shape) const
  {
    if (shape.kind == "segment") {
      return eccdb::RouteSegment{.layer = routingLayer(shape.layer),
                                 .begin = {.x = shape.x1, .y = shape.y1},
                                 .end = {.x = shape.x2, .y = shape.y2},
                                 .width = std::nullopt,
                                 .begin_extension = shape.begin_extension,
                                 .end_extension = shape.end_extension};
    }
    if (shape.kind == "via") {
      if (shape.via.empty() || shape.x1 != shape.x2 || shape.y1 != shape.y2 || shape.begin_extension || shape.end_extension) {
        fail("trace VIA carries unsupported metadata");
      }
      eccdb::RouteViaDefinition definition;
      if (shape.tech_via) {
        const auto via = technology_.viaMasterStorage().findViaMaster(shape.via);
        if (!via) {
          fail("trace references unknown EccDB technology VIA '" + shape.via + "'");
        }
        definition = via;
      } else {
        const auto via = design_.routingStorage().findVia(shape.via);
        if (!via) {
          fail("trace references unknown EccDB design VIA '" + shape.via + "'");
        }
        definition = via;
      }
      if (routingLayerName(viaBottom(definition)) != shape.layer) {
        fail("trace VIA layer is not the VIA's canonical EccDB bottom routing layer");
      }
      return eccdb::RouteVia{.definition = std::move(definition),
                             .origin = {.x = shape.x1, .y = shape.y1},
                             .orientation = eccdb::DesignOrientation::kN};
    }
    if (shape.kind == "patch") {
      return eccdb::RoutePatch{.layer = routingLayer(shape.layer),
                               .rectangle = {.ll_x = shape.x1, .ll_y = shape.y1, .ur_x = shape.x2, .ur_y = shape.y2}};
    }
    fail("unsupported EccDB route shape '" + shape.kind + "'");
  }

  void replaceWire(uint32_t token, const std::vector<drt_trace::Shape>& shapes)
  {
    if (!profile_wire_write_) {
      std::vector<eccdb::RouteShape> converted;
      converted.reserve(shapes.size());
      for (const auto& shape : shapes) converted.push_back(routeShape(shape));
      static_cast<void>(routes_.replaceRouted(net(token), std::span<const eccdb::RouteShape>{converted}));
      return;
    }

    using Clock = std::chrono::steady_clock;
    const auto total_begin = Clock::now();
    {
      const auto convert_begin = Clock::now();
      std::vector<eccdb::RouteShape> converted;
      converted.reserve(shapes.size());
      for (const auto& shape : shapes) converted.push_back(routeShape(shape));
      wire_write_adaptor_convert_ns_ +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - convert_begin).count();

      const auto net_resolution_begin = Clock::now();
      const auto net_id = net(token);
      wire_write_net_resolution_ns_ +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - net_resolution_begin).count();

      const auto service_begin = Clock::now();
      static_cast<void>(routes_.replaceRouted(net_id, std::span<const eccdb::RouteShape>{converted}, &wire_write_profile_));
      wire_write_service_call_ns_ +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - service_begin).count();
    }
    wire_write_adaptor_total_ns_ +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - total_begin).count();
  }

  [[nodiscard]] bool supportsBlockages() const
  {
    bool supported = true;
    physical_.forEachRoutingBlockage([&](eccdb::DesignBlockageId, const eccdb::DesignBlockage& blockage) {
      const auto layer = eccdb::TechLayerId{blockage.layer.entity()};
      supported = supported && blockage.polygons.empty() && layer && technology_.contains(layer);
    });
    return supported;
  }

  [[nodiscard]] bool supportsBterms() const
  {
    const auto& netlist = design_.netlistStorage();
    bool supported = true;
    netlist.forEachIoPin([&](eccdb::DesignIoPinId term_id, const eccdb::DesignIoPin&) {
      for (const auto block_pin_id : netlist.blockPins(term_id)) {
        const auto& block_pin = netlist.blockPin(block_pin_id);
        if (!block_pin.polygons.empty() || !block_pin.vias.empty()) {
          supported = false;
          return;
        }
        for (const auto& rectangle : block_pin.rectangles) {
          supported = supported && rectangle.layer && technology_.contains(rectangle.layer);
        }
      }
    });
    return supported;
  }

  [[nodiscard]] bool supportsGuides(const drt_trace::Event& event) const
  {
    if (event.name.empty()) return false;
    auto net_id = physical_.findRegularNet(event.name);
    if (!net_id) net_id = design_.netlistStorage().findSpecialNet(event.name);
    if (!net_id) return false;
    for (const auto& guide : design_.guideStorage().guides(net_id)) {
      const auto layer = eccdb::TechLayerId{guide.layer.entity()};
      const auto via_layer = eccdb::TechLayerId{guide.via_layer.entity()};
      if (!layer || !technology_.routingLayerStorage().contains(guide.layer)
          || (via_layer && !technology_.routingLayerStorage().contains(guide.via_layer))) {
        return false;
      }
    }
    return true;
  }

  // Trace replay consumes the canonical Shape entities directly.  A Shape is
  // already one ordered routing primitive, so no path projection or point
  // index inference is needed here.
  [[nodiscard]] bool supportsSpecialWireShape(const eccdb::DesignRouteShape& shape) const
  {
    constexpr uint32_t kSupportedShapeFlags = eccdb::DesignWirePathFlag::kHasWidth | eccdb::DesignWirePathFlag::kHasMask
                                              | eccdb::DesignWirePathFlag::kHasShape;
    if (!shape.layer || !technology_.routingLayerStorage().contains(shape.layer)
        || isBackside(technology_.layerInfo(eccdb::TechLayerId{shape.layer.entity()}))
        || (shape.flags & ~kSupportedShapeFlags) != 0u) {
      return false;
    }
    return std::visit(
        [&](const auto& geometry) {
          using Geometry = std::remove_cvref_t<decltype(geometry)>;
          if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteSegment>) {
            if ((shape.flags & eccdb::DesignWirePathFlag::kHasWidth) == 0u) return false;
            if (geometry.begin.flags != 0u || geometry.end.flags != 0u || geometry.begin.extension != 0
                || geometry.end.extension != 0) {
              return false;
            }
            return (geometry.begin.position.x == geometry.end.position.x)
                   != (geometry.begin.position.y == geometry.end.position.y);
          } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteVia>) {
            constexpr uint32_t kKnownViaFlags = eccdb::DesignWireViaFlag::kHasMask | eccdb::DesignWireViaFlag::kHasArray;
            const auto& via = geometry.via;
            const bool has_tech = static_cast<bool>(via.tech_via);
            const bool has_design = static_cast<bool>(via.design_via);
            if (geometry.origin.flags != 0u || geometry.origin.extension != 0 || has_tech == has_design
                || via.orientation != eccdb::DesignOrientation::kN || (via.flags & ~kKnownViaFlags) != 0u
                || via.rows == 0u || via.columns == 0u
                || ((via.flags & eccdb::DesignWireViaFlag::kHasArray) == 0u
                    && (via.rows != 1u || via.columns != 1u || via.step_x != 0 || via.step_y != 0))) {
              return false;
            }
            try {
              const auto layers = physical_.routingViaLayers(via.tech_via, via.design_via);
              return !isBackside(technology_.layerInfo(eccdb::TechLayerId{layers.bottom.entity()}))
                     && !isBackside(technology_.layerInfo(eccdb::TechLayerId{layers.top.entity()}));
            } catch (const std::exception&) {
              return false;
            }
          } else {
            // SPECIALNET trace replay currently models SBoxes as paths/Vias;
            // standalone rectangles and syntax-only points have no dbSWire
            // equivalent in this trace operation.
            return false;
          }
        },
        shape.geometry);
  }

  [[nodiscard]] bool supportsSpecialWire(const drt_trace::Event& event) const
  {
    try {
      if (event.name.empty()) return false;
      const auto net_id = design_.netlistStorage().findSpecialNet(event.name);
      // Standalone SPECIALNET RECT/POLYGON/VIA clauses are modeled, but are
      // not ordered together with DesignWire paths in the flat representation.
      if (!net_id || design_.routingStorage().netGeometry(net_id) != nullptr) return false;
      bool supported = true;
      physical_.forEachNetWire(net_id, [&](eccdb::DesignWireId wire_id, const eccdb::DesignWire&) {
        design_.routingStorage().forEachShape(
            wire_id, [&](eccdb::DesignRouteShapeId, const eccdb::DesignRouteShape& shape,
                         const eccdb::DesignRouteShapeAttributes*) {
              if (supported) supported = supportsSpecialWireShape(shape);
        });
      });
      return supported;
    } catch (const std::exception&) {
      return false;
    }
  }

  [[nodiscard]] bool supportsBtermPinAccess(std::string_view name, std::optional<uint32_t> ordinal) const
  {
    if (name.empty()) return false;
    const auto pin_id = physical_.findBlockTerm(name);
    if (!pin_id) return false;
    const auto block_pins = physical_.blockPins(pin_id);
    // Pre-v3 traces had no BPin ordinal and are only unambiguous when there
    // is exactly one DEF PORT. New traces always carry the zero-based ordinal.
    if (!ordinal && block_pins.size() != 1u) return false;
    const uint32_t port_index = ordinal.value_or(0u);
    if (port_index >= block_pins.size()) return false;
    const auto groups = design_.pinAccessStorage().blockPinCandidateGroups(block_pins[port_index]);
    // dbBPin owns one flat access-point sequence. EccDB represents it with
    // candidate group zero, so any other group would not be an exact ODB
    // projection.
    return std::all_of(groups.begin(), groups.end(), [this](const auto& group) {
      return group.group_index == 0u && supportsPinAccessPoints(group.points);
    });
  }

  [[nodiscard]] bool supportsBtermPinAccess(const drt_trace::Event& event) const
  {
    return supportsBtermPinAccess(event.name, event.ordinal);
  }

  [[nodiscard]] bool supportsMasterPinAccess(const drt_trace::Event& event) const
  {
    if (event.master.empty() || event.term.empty() || !event.ordinal) return false;
    const auto master = physical_.findMaster(event.master);
    if (!master) return false;
    const auto term = physical_.findMasterTerm(master, event.term);
    if (!term) return false;
    const auto ports = physical_.masterPorts(term);
    if (*event.ordinal >= ports.size()) return false;
    return supportsCandidateGroups(design_.pinAccessStorage().masterPortCandidateGroups(ports[*event.ordinal]));
  }

  [[nodiscard]] bool supportsPreferredPinAccess(const drt_trace::Event& event) const
  {
    if (event.instance.empty() || event.term.empty()) return false;
    const auto instance = physical_.findInstance(event.instance);
    if (!instance) return false;
    const auto pin = physical_.findInstancePin(instance, event.term);
    if (!pin) return false;
    const auto& netlist = design_.netlistStorage();
    const auto& instance_pin = netlist.instancePin(pin);
    const auto& term = library_.masterTermStorage().masterTerm(instance_pin.master_term);
    for (const auto& preference : design_.pinAccessStorage().preferredInstancePinAccess(pin)) {
      if (std::find(term.ports.begin(), term.ports.end(), preference.master_port) == term.ports.end()) return false;
      const auto groups = design_.pinAccessStorage().masterPortCandidateGroups(preference.master_port);
      const auto group = std::find_if(groups.begin(), groups.end(), [&](const auto& candidate) {
        return candidate.group_index == preference.group_index;
      });
      if (group == groups.end() || preference.point_index >= group->points.size()
          || !supportsPinAccessPoint(group->points[preference.point_index])) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool supportsPinAccessPoint(const eccdb::DesignPinAccessPoint& point) const
  {
    constexpr uint32_t kKnownDirections = eccdb::DesignPinAccessDirection::kAll;
    if (!point.layer || !technology_.routingLayerStorage().contains(point.layer)
        || (point.access_directions & ~kKnownDirections) != 0u
        || !isKnownPinAccessType(point.low_type) || !isKnownPinAccessType(point.high_type)) {
      return false;
    }
    for (const auto& via : point.vias) {
      const bool has_tech = static_cast<bool>(via.tech_via);
      const bool has_design = static_cast<bool>(via.design_via);
      if (via.cut_count == 0u || has_tech == has_design
          || (has_tech && !technology_.viaMasterStorage().contains(via.tech_via))
          || (has_design && !design_.routingStorage().contains(via.design_via))) {
        return false;
      }
    }
    return std::all_of(point.path_segments.begin(), point.path_segments.end(), [](const auto& segment) {
      constexpr uint32_t kFlags = eccdb::DesignPinAccessPathSegmentFlag::kBeginTruncated
                                  | eccdb::DesignPinAccessPathSegmentFlag::kEndTruncated;
      return segment.rectangle.isValid() && (segment.rectangle.width() != 0 || segment.rectangle.height() != 0)
             && (segment.flags & ~kFlags) == 0u;
    });
  }

  [[nodiscard]] bool supportsPinAccessPoints(const std::vector<eccdb::DesignPinAccessPoint>& points) const
  {
    return std::all_of(points.begin(), points.end(), [this](const auto& point) { return supportsPinAccessPoint(point); });
  }

  [[nodiscard]] bool supportsCandidateGroups(std::span<const eccdb::DesignPinAccessCandidateGroup> groups) const
  {
    for (std::size_t index = 0; index < groups.size(); ++index) {
      if (!supportsPinAccessPoints(groups[index].points)
          || std::any_of(groups.begin(), groups.begin() + index, [&](const auto& prior) {
               return prior.group_index == groups[index].group_index;
             })) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] json::value readBlockages() const
  {
    json::array result;
    physical_.forEachRoutingBlockage([&](eccdb::DesignBlockageId id, const eccdb::DesignBlockage&) {
      physical_.forEachRoutingBlockageGeometry(id, {.rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
                                                       if (isBackside(technology_.layerInfo(rectangle.layer))) return;
                                                       const auto& box = rectangle.rectangle;
                                                       result.emplace_back(
                                                           json::array{layerName(rectangle.layer), box.ll_x, box.ll_y, box.ur_x, box.ur_y});
                                                     }});
    });
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] json::value readBterms() const
  {
    const auto& netlist = design_.netlistStorage();
    json::array result;
    netlist.forEachIoPin([&](eccdb::DesignIoPinId term_id, const eccdb::DesignIoPin& pin) {
      json::array ports;
      for (const auto block_pin_id : netlist.blockPins(term_id)) {
        const auto& block_pin = netlist.blockPin(block_pin_id);
        json::array boxes;
        const bool has_placement = hasFlag(block_pin.flags, eccdb::DesignIoPinPortFlag::kHasPlacement);
        const auto origin = has_placement ? block_pin.origin : eccdb::Point{};
        const auto orientation = has_placement ? block_pin.orientation : eccdb::DesignOrientation::kN;
        for (const auto& rectangle : block_pin.rectangles) {
          if (isBackside(technology_.layerInfo(rectangle.layer))) continue;
          const auto box = transformIoRectangle(rectangle.rectangle, origin, orientation);
          boxes.emplace_back(json::array{layerName(rectangle.layer), box.ll_x, box.ll_y, box.ur_x, box.ur_y});
        }
        ports.emplace_back(drt_trace::canonical(std::move(boxes)));
      }
      const auto net_id = hasFlag(pin.flags, eccdb::DesignIoPinFlag::kSpecial) ? pin.special_net : pin.net;
      const json::value net_name = net_id ? json::value(netlist.net(net_id).name) : json::value(nullptr);
      result.emplace_back(json::array{pin.name,
                                      signalUseName(pin.use),
                                      ioDirectionName(pin.direction),
                                      net_name,
                                      drt_trace::canonical(std::move(ports))});
    });
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] json::value readGuides(std::string_view name) const
  {
    auto net_id = physical_.findRegularNet(name);
    if (!net_id) net_id = design_.netlistStorage().findSpecialNet(name);
    if (!net_id) fail("EccDB trace guide net does not exist: " + std::string(name));
    return guideValues(design_.guideStorage().guides(net_id));
  }

  [[nodiscard]] json::value guideValues(std::span<const eccdb::DesignRouteGuide> guides) const
  {
    json::array result;
    result.reserve(guides.size());
    for (const auto& guide : guides) {
      if (isBackside(technology_.layerInfo(eccdb::TechLayerId{guide.layer.entity()}))) continue;
      const auto via_layer = eccdb::TechLayerId{guide.via_layer.entity()};
      if (via_layer && isBackside(technology_.layerInfo(via_layer))) {
        fail("EccDB guide uses a backside via layer");
      }
      result.emplace_back(json::array{routingLayerName(guide.layer),
                                      via_layer ? json::value(routingLayerName(guide.via_layer)) : json::value(nullptr),
                                      guide.box.ll_x,
                                      guide.box.ll_y,
                                      guide.box.ur_x,
                                      guide.box.ur_y,
                                      hasFlag(guide.flags, eccdb::DesignRouteGuideFlag::kCongested),
                                      hasFlag(guide.flags, eccdb::DesignRouteGuideFlag::kJumper),
                                      hasFlag(guide.flags, eccdb::DesignRouteGuideFlag::kConnectedToTerm)});
    }
    return result;
  }

  void appendSpecialWireVia(json::array& shapes, const eccdb::DesignWireVia& via, eccdb::Point origin) const
  {
    const auto name = physical_.viaName(via.tech_via, via.design_via);
    if (name.empty()) fail("EccDB special wire VIA has no definition name");
    // ODB's definSNet expands a VIA array with X as the outer loop and Y as
    // the inner loop. Match that order because dbSWire exposes it directly.
    for (uint32_t column = 0; column < via.columns; ++column) {
      const auto x = checkedProjectedCoordinate(static_cast<int64_t>(origin.x)
                                                + static_cast<int64_t>(column) * via.step_x);
      for (uint32_t row = 0; row < via.rows; ++row) {
        const auto y = checkedProjectedCoordinate(static_cast<int64_t>(origin.y)
                                                  + static_cast<int64_t>(row) * via.step_y);
        shapes.emplace_back(json::array{"via", via.tech_via ? "tech" : "design", std::string(name), x, y});
      }
    }
  }

  void appendSpecialWireShape(json::array& shapes, const eccdb::DesignRouteShape& shape,
                              const eccdb::DesignRouteShapeAttributes* attributes) const
  {
    const std::string shape_name = attributes == nullptr || attributes->shape.empty() ? "NONE" : attributes->shape;
    const int32_t width = attributes == nullptr ? 0 : attributes->width;
    std::visit(
        [&](const auto& geometry) {
          using Geometry = std::remove_cvref_t<decltype(geometry)>;
          if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteSegment>) {
            const auto first = geometry.begin.position;
            const auto second = geometry.end.position;
            if (first.x == second.x) {
              shapes.emplace_back(json::array{"path", routingLayerName(shape.layer), shape_name, first.x,
                                              std::min(first.y, second.y), first.x, std::max(first.y, second.y), width});
            } else {
              shapes.emplace_back(json::array{"path", routingLayerName(shape.layer), shape_name,
                                              std::min(first.x, second.x), first.y, std::max(first.x, second.x), first.y, width});
            }
          } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteVia>) {
            appendSpecialWireVia(shapes, geometry.via, geometry.origin.position);
          }
        },
        shape.geometry);
  }

  [[nodiscard]] json::value readSpecialWire(std::string_view name) const
  {
    const auto net_id = design_.netlistStorage().findSpecialNet(name);
    if (!net_id) fail("EccDB trace special net does not exist: " + std::string(name));
    json::array result;
    physical_.forEachNetWire(net_id, [&](eccdb::DesignWireId wire_id, const eccdb::DesignWire& wire) {
      json::array shapes;
      design_.routingStorage().forEachShape(
          wire_id, [&](eccdb::DesignRouteShapeId, const eccdb::DesignRouteShape& shape,
                       const eccdb::DesignRouteShapeAttributes* attributes) {
            appendSpecialWireShape(shapes, shape, attributes);
          });
      result.emplace_back(json::array{specialWireTypeName(wire.status), std::move(shapes)});
    });
    return result;
  }

  [[nodiscard]] json::value pinAccessValue(const eccdb::DesignPinAccessPoint& point) const
  {
    json::array directions;
    constexpr std::pair<uint32_t, const char*> direction_names[] = {
        {eccdb::DesignPinAccessDirection::kNorth, "N"}, {eccdb::DesignPinAccessDirection::kEast, "E"},
        {eccdb::DesignPinAccessDirection::kSouth, "S"}, {eccdb::DesignPinAccessDirection::kWest, "W"},
        {eccdb::DesignPinAccessDirection::kUp, "U"},    {eccdb::DesignPinAccessDirection::kDown, "D"}};
    for (const auto& [direction, name] : direction_names) {
      if ((point.access_directions & direction) != 0u) directions.emplace_back(name);
    }

    uint32_t max_cut_count = 0u;
    for (const auto& via : point.vias) max_cut_count = std::max(max_cut_count, via.cut_count);
    json::array via_groups;
    for (uint32_t cut_count = 1u; cut_count <= max_cut_count; ++cut_count) {
      json::array vias;
      for (const auto& via : point.vias) {
        if (via.cut_count != cut_count) continue;
        if (via.tech_via) {
          vias.emplace_back(json::array{"tech", technology_.viaMasterStorage().viaMaster(via.tech_via).name});
        } else {
          vias.emplace_back(json::array{"design", design_.routingStorage().via(via.design_via).name});
        }
      }
      via_groups.emplace_back(json::array{static_cast<int64_t>(cut_count), std::move(vias)});
    }

    json::array segments;
    for (const auto& segment : point.path_segments) {
      const auto& box = segment.rectangle;
      segments.emplace_back(json::array{box.ll_x,
                                        box.ll_y,
                                        box.ur_x,
                                        box.ur_y,
                                        hasFlag(segment.flags, eccdb::DesignPinAccessPathSegmentFlag::kBeginTruncated),
                                        hasFlag(segment.flags, eccdb::DesignPinAccessPathSegmentFlag::kEndTruncated)});
    }
    return json::array{point.point.x,
                       point.point.y,
                       routingLayerName(point.layer),
                       std::move(directions),
                       pinAccessTypeName(point.low_type),
                       pinAccessTypeName(point.high_type),
                       std::move(via_groups),
                       std::move(segments)};
  }

  [[nodiscard]] json::array candidateGroupsValue(std::span<const eccdb::DesignPinAccessCandidateGroup> groups) const
  {
    if (groups.empty()) return {};
    uint32_t max_group = 0u;
    for (const auto& group : groups) max_group = std::max(max_group, group.group_index);
    json::array result;
    result.reserve(static_cast<std::size_t>(max_group) + 1u);
    for (uint32_t group_index = 0u; group_index <= max_group; ++group_index) {
      const auto found = std::find_if(groups.begin(), groups.end(), [group_index](const auto& group) {
        return group.group_index == group_index;
      });
      json::array points;
      if (found != groups.end()) {
        points.reserve(found->points.size());
        for (const auto& point : found->points) points.emplace_back(pinAccessValue(point));
      }
      result.emplace_back(json::array{static_cast<int64_t>(group_index), std::move(points)});
    }
    return result;
  }

  [[nodiscard]] uint32_t masterPortOrdinal(eccdb::LibraryMasterTermId term, eccdb::LibraryMasterPortId port) const
  {
    const auto ports = physical_.masterPorts(term);
    const auto found = std::find(ports.begin(), ports.end(), port);
    if (found == ports.end()) fail("EccDB pin-access reference does not belong to its master terminal");
    return static_cast<uint32_t>(std::distance(ports.begin(), found));
  }

  [[nodiscard]] uint32_t traceIndex(const json::value& value, std::string_view context) const
  {
    const auto raw = drt_trace::integer(value);
    if (raw < 0 || static_cast<uint64_t>(raw) > std::numeric_limits<uint32_t>::max()) {
      fail("trace " + std::string(context) + " is outside uint32 range");
    }
    return static_cast<uint32_t>(raw);
  }

  [[nodiscard]] uint32_t parseAccessDirections(const json::value& value) const
  {
    if (!value.is_array()) fail("trace pin-access directions are not an array");
    uint32_t result = 0u;
    for (const auto& entry : value.as_array()) {
      const auto name = drt_trace::text(entry);
      uint32_t direction = 0u;
      if (name == "N") direction = eccdb::DesignPinAccessDirection::kNorth;
      else if (name == "E") direction = eccdb::DesignPinAccessDirection::kEast;
      else if (name == "S") direction = eccdb::DesignPinAccessDirection::kSouth;
      else if (name == "W") direction = eccdb::DesignPinAccessDirection::kWest;
      else if (name == "U") direction = eccdb::DesignPinAccessDirection::kUp;
      else if (name == "D") direction = eccdb::DesignPinAccessDirection::kDown;
      else fail("trace pin-access direction is unsupported: " + name);
      if ((result & direction) != 0u) fail("trace pin-access direction is duplicated: " + name);
      result |= direction;
    }
    return result;
  }

  [[nodiscard]] eccdb::DesignPinAccessType parsePinAccessType(const json::value& value) const
  {
    const auto name = drt_trace::text(value);
    if (name == "OnGrid") return eccdb::DesignPinAccessType::kOnGrid;
    if (name == "HalfGrid") return eccdb::DesignPinAccessType::kHalfGrid;
    if (name == "Center") return eccdb::DesignPinAccessType::kCenter;
    if (name == "EncOpt") return eccdb::DesignPinAccessType::kEncOpt;
    if (name == "NearbyGrid") return eccdb::DesignPinAccessType::kNearbyGrid;
    fail("trace pin-access type is unsupported: " + name);
  }

  [[nodiscard]] eccdb::DesignPinAccessPoint parsePinAccess(const json::value& value) const
  {
    if (!value.is_array() || value.as_array().size() != 8u) {
      fail("trace pin-access point must be an 8-field array");
    }
    const auto& fields = value.as_array();
    eccdb::DesignPinAccessPoint result{.point = {.x = drt_trace::coordinate(fields[0]), .y = drt_trace::coordinate(fields[1])},
                                        .layer = routingLayer(drt_trace::text(fields[2])),
                                        .access_directions = parseAccessDirections(fields[3]),
                                        .low_type = parsePinAccessType(fields[4]),
                                        .high_type = parsePinAccessType(fields[5])};
    if (!fields[6].is_array() || !fields[7].is_array()) fail("trace pin-access VIA or segment list is not an array");
    for (const auto& group_value : fields[6].as_array()) {
      if (!group_value.is_array() || group_value.as_array().size() != 2u || !group_value.as_array()[1].is_array()) {
        fail("trace pin-access VIA group is malformed");
      }
      const auto& group = group_value.as_array();
      const uint32_t cut_count = traceIndex(group[0], "pin-access VIA cut count");
      if (cut_count == 0u) fail("trace pin-access VIA cut count is zero");
      for (const auto& via_value : group[1].as_array()) {
        if (!via_value.is_array() || via_value.as_array().size() != 2u) fail("trace pin-access VIA is malformed");
        const auto& via = via_value.as_array();
        const auto kind = drt_trace::text(via[0]);
        const auto name = drt_trace::text(via[1]);
        eccdb::DesignPinAccessVia converted{.cut_count = cut_count};
        if (kind == "tech") {
          converted.tech_via = technology_.viaMasterStorage().findViaMaster(name);
          if (!converted.tech_via) fail("trace pin-access references unknown technology VIA " + name);
        } else if (kind == "design") {
          converted.design_via = design_.routingStorage().findVia(name);
          if (!converted.design_via) fail("trace pin-access references unknown design VIA " + name);
        } else {
          fail("trace pin-access VIA kind is unsupported: " + kind);
        }
        result.vias.push_back(std::move(converted));
      }
    }
    for (const auto& segment_value : fields[7].as_array()) {
      if (!segment_value.is_array() || segment_value.as_array().size() != 6u) fail("trace pin-access segment is malformed");
      const auto& segment = segment_value.as_array();
      if (!segment[4].is_bool() || !segment[5].is_bool()) fail("trace pin-access segment flags are not boolean");
      uint32_t flags = 0u;
      if (segment[4].as_bool()) flags |= eccdb::DesignPinAccessPathSegmentFlag::kBeginTruncated;
      if (segment[5].as_bool()) flags |= eccdb::DesignPinAccessPathSegmentFlag::kEndTruncated;
      result.path_segments.push_back({.rectangle = {.ll_x = drt_trace::coordinate(segment[0]),
                                                    .ll_y = drt_trace::coordinate(segment[1]),
                                                    .ur_x = drt_trace::coordinate(segment[2]),
                                                    .ur_y = drt_trace::coordinate(segment[3])},
                                      .flags = flags});
    }
    return result;
  }

  [[nodiscard]] std::vector<eccdb::DesignPinAccessPoint> parsePinAccessPoints(const json::value& value) const
  {
    if (!value.is_array()) fail("trace pin-access point list is not an array");
    std::vector<eccdb::DesignPinAccessPoint> result;
    result.reserve(value.as_array().size());
    for (const auto& point : value.as_array()) result.push_back(parsePinAccess(point));
    return result;
  }

  [[nodiscard]] const json::object& payloadObject(const drt_trace::Event& event) const
  {
    if (!event.payload.is_object()) fail("trace " + event.op + " payload is not an object");
    return event.payload.as_object();
  }

  [[nodiscard]] eccdb::DesignNetId netByName(std::string_view name) const
  {
    auto net_id = physical_.findRegularNet(name);
    if (!net_id) net_id = design_.netlistStorage().findSpecialNet(name);
    if (!net_id) fail("EccDB trace net does not exist: " + std::string(name));
    return net_id;
  }

  [[nodiscard]] json::value trackAssignmentsValue(eccdb::DesignNetId net_id) const
  {
    json::array result;
    for (const auto& assignment : design_.guideStorage().trackAssignments(net_id)) {
      const auto& box = assignment.box;
      result.emplace_back(json::array{routingLayerName(assignment.layer), box.ll_x, box.ll_y, box.ur_x, box.ur_y});
    }
    return result;
  }

  [[nodiscard]] std::vector<eccdb::DesignTrackAssignmentShape> parseTrackAssignments(const json::value& value) const
  {
    if (!value.is_array()) fail("trace track-assignment list is not an array");
    std::vector<eccdb::DesignTrackAssignmentShape> result;
    result.reserve(value.as_array().size());
    for (const auto& entry_value : value.as_array()) {
      if (!entry_value.is_array() || entry_value.as_array().size() != 5u) {
        fail("trace track assignment must be [layer,x1,y1,x2,y2]");
      }
      const auto& entry = entry_value.as_array();
      const eccdb::Rect box{.ll_x = drt_trace::coordinate(entry[1]),
                            .ll_y = drt_trace::coordinate(entry[2]),
                            .ur_x = drt_trace::coordinate(entry[3]),
                            .ur_y = drt_trace::coordinate(entry[4])};
      if (!box.isValid()) fail("trace track assignment has invalid geometry");
      result.push_back({.layer = routingLayer(drt_trace::text(entry[0])), .box = box});
    }
    return result;
  }

  [[nodiscard]] std::vector<eccdb::DesignRouteGuide> parseGuides(const json::value& value) const
  {
    if (!value.is_array()) fail("trace guide list is not an array");
    std::vector<eccdb::DesignRouteGuide> result;
    result.reserve(value.as_array().size());
    for (const auto& entry_value : value.as_array()) {
      if (!entry_value.is_array() || entry_value.as_array().size() != 5u) {
        fail("trace guide must be [layer,x1,y1,x2,y2]");
      }
      const auto& entry = entry_value.as_array();
      const eccdb::Rect box{.ll_x = drt_trace::coordinate(entry[1]),
                            .ll_y = drt_trace::coordinate(entry[2]),
                            .ur_x = drt_trace::coordinate(entry[3]),
                            .ur_y = drt_trace::coordinate(entry[4])};
      if (!box.isValid()) fail("trace guide has invalid geometry");
      const auto layer = routingLayer(drt_trace::text(entry[0]));
      result.push_back({.box = box, .layer = layer, .via_layer = layer});
    }
    return result;
  }

  [[nodiscard]] bool supportsAppendTrackAssignment(const drt_trace::Event& event) const
  {
    try {
      const auto& payload = payloadObject(event);
      if (event.name.empty() || payload.if_contains("append") == nullptr) return false;
      static_cast<void>(netByName(event.name));
      static_cast<void>(parseTrackAssignments(*payload.if_contains("append")));
      return true;
    } catch (const std::exception&) {
      return false;
    }
  }

  [[nodiscard]] bool supportsReplaceGuides(const drt_trace::Event& event) const
  {
    try {
      const auto& payload = payloadObject(event);
      if (event.name.empty() || payload.if_contains("guides") == nullptr) return false;
      static_cast<void>(netByName(event.name));
      static_cast<void>(parseGuides(*payload.if_contains("guides")));
      return true;
    } catch (const std::exception&) {
      return false;
    }
  }

  [[nodiscard]] bool supportsAppendBtermPinAccess(const drt_trace::Event& event) const
  {
    try {
      const auto& payload = payloadObject(event);
      const auto* ordinal = payload.if_contains("bpin_ordinal");
      const auto* appended = payload.if_contains("append");
      if (event.name.empty() || ordinal == nullptr || appended == nullptr) return false;
      const auto pin_id = physical_.findBlockTerm(event.name);
      if (!pin_id) return false;
      const auto port_index = traceIndex(*ordinal, "BPin ordinal");
      if (port_index >= design_.netlistStorage().blockPins(pin_id).size() || !supportsBtermPinAccess(event.name, port_index)) return false;
      return supportsPinAccessPoints(parsePinAccessPoints(*appended));
    } catch (const std::exception&) {
      return false;
    }
  }

  [[nodiscard]] json::value appendTrackAssignment(const drt_trace::Event& event)
  {
    const auto& payload = payloadObject(event);
    const auto* appended = payload.if_contains("append");
    if (appended == nullptr) fail("trace track-assignment payload has no append list");
    const auto net_id = netByName(event.name);
    auto added = parseTrackAssignments(*appended);
    std::vector<eccdb::DesignTrackAssignmentShape> replacement;
    const auto previous = design_.guideStorage().trackAssignments(net_id);
    replacement.reserve(added.size() + previous.size());
    // dbNetTrack::create pushes each appended item at the list head.
    for (auto it = added.rbegin(); it != added.rend(); ++it) replacement.push_back(std::move(*it));
    replacement.insert(replacement.end(), previous.begin(), previous.end());
    eccdb::RoutingContextService context(design_);
    context.replaceTrackAssignments(net_id, std::move(replacement));
    return trackAssignmentsValue(net_id);
  }

  [[nodiscard]] json::value replaceGuides(const drt_trace::Event& event)
  {
    const auto& payload = payloadObject(event);
    const auto* guides = payload.if_contains("guides");
    if (guides == nullptr) fail("trace guide payload has no guide list");
    const auto net_id = netByName(event.name);
    eccdb::RoutingContextService context(design_);
    context.replaceGuides(net_id, parseGuides(*guides));
    return readGuides(event.name);
  }

  [[nodiscard]] json::value appendBtermPinAccess(const drt_trace::Event& event)
  {
    const auto& payload = payloadObject(event);
    const auto* ordinal = payload.if_contains("bpin_ordinal");
    const auto* appended = payload.if_contains("append");
    if (ordinal == nullptr || appended == nullptr) fail("trace BTerm pin-access payload is incomplete");
    const auto pin_id = physical_.findBlockTerm(event.name);
    if (!pin_id) fail("EccDB trace BTerm does not exist: " + event.name);
    const auto port_index = traceIndex(*ordinal, "BPin ordinal");
    const auto block_pins = physical_.blockPins(pin_id);
    if (port_index >= block_pins.size()) fail("trace BPin ordinal is out of range");
    eccdb::RoutingContextService context(design_);
    context.appendBlockPinCandidatePoints(block_pins[port_index], 0u, parsePinAccessPoints(*appended));
    return readBtermPinAccess(event.name, port_index);
  }

  struct MasterPinAccessUpdate
  {
    std::string term_name;
    eccdb::LibraryMasterPortId port;
    uint32_t port_ordinal = 0u;
    uint32_t group_index = 0u;
    std::vector<eccdb::DesignPinAccessPoint> points;
  };

  struct MasterPinAccessReplacement
  {
    eccdb::LibraryCellMasterId master;
    std::vector<uint32_t> clear_groups;
    std::vector<MasterPinAccessUpdate> updates;
  };

  [[nodiscard]] bool canClearMasterPortCandidateGroup(eccdb::LibraryMasterPortId port, uint32_t group_index) const
  {
    const auto& pin_access = design_.pinAccessStorage();
    if (!pin_access.hasMasterPortCandidateGroup(port, group_index)) return true;

    const auto& netlist = design_.netlistStorage();
    bool clearable = true;
    netlist.forEachInstance([&](eccdb::DesignInstanceId instance_id,
                                const eccdb::DesignInstance&,
                                std::span<const eccdb::DesignInstancePinId> pins) {
      if (!clearable) return;
      const auto selected = pin_access.instancePinAccessGroup(instance_id);
      for (const auto pin : pins) {
        for (const auto& preference : pin_access.preferredInstancePinAccess(pin)) {
          if (preference.master_port == port && preference.group_index == group_index) {
            clearable = false;
            return;
          }
        }
        const auto& instance_pin = netlist.instancePin(pin);
        const auto ports = physical_.masterPorts(instance_pin.master_term);
        if (selected && *selected == group_index && std::find(ports.begin(), ports.end(), port) != ports.end()) {
          clearable = false;
          return;
        }
      }
    });
    return clearable;
  }

  [[nodiscard]] MasterPinAccessReplacement parseMasterPinAccessReplacement(const drt_trace::Event& event) const
  {
    if (event.master.empty()) fail("trace master pin-access update has no master name");
    const auto master = physical_.findMaster(event.master);
    if (!master) fail("trace master pin-access update references unknown master " + event.master);
    const auto& payload = payloadObject(event);
    const auto* clear = payload.if_contains("clear_groups");
    const auto* create = payload.if_contains("create");
    if (clear == nullptr || create == nullptr || !clear->is_array() || !create->is_array()) {
      fail("trace master pin-access update payload is incomplete");
    }

    MasterPinAccessReplacement result{.master = master};
    result.clear_groups.reserve(clear->as_array().size());
    for (const auto& group_value : clear->as_array()) {
      const auto group = traceIndex(group_value, "master pin-access group");
      if (std::find(result.clear_groups.begin(), result.clear_groups.end(), group) != result.clear_groups.end()) {
        fail("trace master pin-access clear group is duplicated");
      }
      result.clear_groups.push_back(group);
    }
    result.updates.reserve(create->as_array().size());
    for (const auto& entry_value : create->as_array()) {
      if (!entry_value.is_array() || entry_value.as_array().size() != 4u) {
        fail("trace master pin-access create entry is malformed");
      }
      const auto& entry = entry_value.as_array();
      const auto term_name = drt_trace::text(entry[0]);
      const auto term = physical_.findMasterTerm(master, term_name);
      if (!term) fail("trace master pin-access update references unknown terminal " + term_name);
      const auto ports = physical_.masterPorts(term);
      const auto ordinal = traceIndex(entry[1], "master port ordinal");
      if (ordinal >= ports.size()) fail("trace master pin-access port ordinal is out of range");
      const auto group = traceIndex(entry[2], "master pin-access group");
      MasterPinAccessUpdate update{.term_name = term_name,
                                    .port = ports[ordinal],
                                    .port_ordinal = ordinal,
                                    .group_index = group,
                                    .points = parsePinAccessPoints(entry[3])};
      if (std::any_of(result.updates.begin(), result.updates.end(), [&](const auto& prior) {
            return prior.port == update.port && prior.group_index == update.group_index;
          })) {
        fail("trace master pin-access update repeats a port/group pair");
      }
      result.updates.push_back(std::move(update));
    }
    return result;
  }

  [[nodiscard]] bool supportsReplaceMasterPinAccess(const drt_trace::Event& event) const
  {
    try {
      const auto replacement = parseMasterPinAccessReplacement(event);
      const auto& master = library_.cellMasterStorage().cellMaster(replacement.master);
      for (const auto term : master.terms) {
        for (const auto port : physical_.masterPorts(term)) {
          for (const auto group : replacement.clear_groups) {
            if (!canClearMasterPortCandidateGroup(port, group)) return false;
          }
        }
      }
      return std::all_of(replacement.updates.begin(), replacement.updates.end(), [this](const auto& update) {
        return supportsPinAccessPoints(update.points);
      });
    } catch (const std::exception&) {
      return false;
    }
  }

  [[nodiscard]] json::value masterPinAccessUpdateValue(const std::vector<MasterPinAccessUpdate>& updates) const
  {
    json::array result;
    for (const auto& update : updates) {
      const auto& group = design_.pinAccessStorage().masterPortCandidateGroup(update.port, update.group_index);
      json::array points;
      points.reserve(group.points.size());
      for (const auto& point : group.points) points.emplace_back(pinAccessValue(point));
      result.emplace_back(json::array{update.term_name,
                                      static_cast<int64_t>(update.port_ordinal),
                                      static_cast<int64_t>(update.group_index),
                                      std::move(points)});
    }
    return result;
  }

  [[nodiscard]] json::value replaceMasterPinAccess(const drt_trace::Event& event)
  {
    auto replacement = parseMasterPinAccessReplacement(event);
    eccdb::RoutingContextService context(design_);
    const auto& master = library_.cellMasterStorage().cellMaster(replacement.master);
    for (const auto term : master.terms) {
      for (const auto port : physical_.masterPorts(term)) {
        for (const auto group : replacement.clear_groups) {
          if (design_.pinAccessStorage().hasMasterPortCandidateGroup(port, group)
              && !context.clearMasterPortCandidateGroup(port, group)) {
            fail("EccDB cannot clear a referenced or selected master pin-access candidate group");
          }
        }
      }
    }
    for (auto& update : replacement.updates) {
      context.setMasterPortCandidateGroup(update.port, update.group_index, std::move(update.points));
    }
    return masterPinAccessUpdateValue(replacement.updates);
  }

  struct InstancePinAccessLocator
  {
    uint32_t port_ordinal = 0u;
    std::optional<eccdb::DesignPreferredPinAccessRef> preference;
  };

  struct InstancePinAccessTermUpdate
  {
    std::string term_name;
    eccdb::DesignInstancePinId pin;
    std::vector<InstancePinAccessLocator> locators;
  };

  struct InstancePinAccessReplacement
  {
    eccdb::DesignInstanceId instance;
    uint32_t group_index = 0u;
    std::vector<InstancePinAccessTermUpdate> terms;
  };

  [[nodiscard]] bool instanceHasPinAccessCandidateGroup(eccdb::DesignInstanceId instance_id, uint32_t group_index) const
  {
    const auto& netlist = design_.netlistStorage();
    for (const auto pin : netlist.instancePins(instance_id)) {
      const auto& instance_pin = netlist.instancePin(pin);
      for (const auto port : physical_.masterPorts(instance_pin.master_term)) {
        if (design_.pinAccessStorage().hasMasterPortCandidateGroup(port, group_index)) return true;
      }
    }
    return false;
  }

  [[nodiscard]] bool coversEveryInstancePin(const InstancePinAccessReplacement& replacement) const
  {
    const auto all_pins = design_.netlistStorage().instancePins(replacement.instance);
    if (replacement.terms.size() != all_pins.size()) return false;
    return std::all_of(all_pins.begin(), all_pins.end(), [&](const auto pin) {
      return std::count_if(replacement.terms.begin(), replacement.terms.end(),
                           [pin](const auto& term) { return term.pin == pin; }) == 1;
    });
  }

  [[nodiscard]] InstancePinAccessReplacement parseInstancePinAccessReplacement(const drt_trace::Event& event) const
  {
    if (event.instance.empty()) fail("trace instance pin-access update has no instance name");
    const auto instance = physical_.findInstance(event.instance);
    if (!instance) fail("trace instance pin-access update references unknown instance " + event.instance);
    const auto& payload = payloadObject(event);
    const auto* group = payload.if_contains("pin_access_index");
    const auto* terms = payload.if_contains("terms");
    if (group == nullptr || terms == nullptr || !terms->is_array()) {
      fail("trace instance pin-access update payload is incomplete");
    }
    InstancePinAccessReplacement result{.instance = instance, .group_index = traceIndex(*group, "instance pin-access group")};
    result.terms.reserve(terms->as_array().size());
    for (const auto& term_value : terms->as_array()) {
      if (!term_value.is_array() || term_value.as_array().size() != 2u || !term_value.as_array()[1].is_array()) {
        fail("trace instance pin-access terminal entry is malformed");
      }
      const auto& entry = term_value.as_array();
      const auto term_name = drt_trace::text(entry[0]);
      const auto pin = physical_.findInstancePin(instance, term_name);
      if (!pin) fail("trace instance pin-access update references unknown terminal " + term_name);
      const auto& instance_pin = design_.netlistStorage().instancePin(pin);
      const auto ports = physical_.masterPorts(instance_pin.master_term);
      InstancePinAccessTermUpdate update{.term_name = term_name, .pin = pin};
      update.locators.reserve(entry[1].as_array().size());
      for (const auto& locator_value : entry[1].as_array()) {
        if (!locator_value.is_array() || locator_value.as_array().size() != 2u) {
          fail("trace instance pin-access locator is malformed");
        }
        const auto& locator = locator_value.as_array();
        const auto ordinal = traceIndex(locator[0], "instance master port ordinal");
        if (ordinal >= ports.size()
            || std::any_of(update.locators.begin(), update.locators.end(), [ordinal](const auto& prior) {
                 return prior.port_ordinal == ordinal;
               })) {
          fail("trace instance pin-access port ordinal is invalid or duplicated");
        }
        std::optional<eccdb::DesignPreferredPinAccessRef> preference;
        if (!locator[1].is_null()) {
          if (!locator[1].is_array() || locator[1].as_array().size() != 2u) {
            fail("trace instance pin-access source locator is malformed");
          }
          const auto& source = locator[1].as_array();
          preference = eccdb::DesignPreferredPinAccessRef{.master_port = ports[ordinal],
                                                            .group_index = traceIndex(source[0], "preferred source group"),
                                                            .point_index = traceIndex(source[1], "preferred source point")};
        }
        update.locators.push_back({.port_ordinal = ordinal, .preference = preference});
      }
      if (update.locators.size() != ports.size()) {
        fail("trace instance pin-access update does not describe every master port");
      }
      if (std::any_of(result.terms.begin(), result.terms.end(), [pin](const auto& prior) { return prior.pin == pin; })) {
        fail("trace instance pin-access update repeats an instance terminal");
      }
      result.terms.push_back(std::move(update));
    }
    if (!coversEveryInstancePin(result)) {
      fail("trace instance pin-access update does not describe every instance terminal exactly once");
    }
    return result;
  }

  [[nodiscard]] bool supportsReplaceInstancePinAccess(const drt_trace::Event& event) const
  {
    try {
      const auto replacement = parseInstancePinAccessReplacement(event);
      if (!instanceHasPinAccessCandidateGroup(replacement.instance, replacement.group_index)) return false;
      for (const auto& term : replacement.terms) {
        for (const auto& locator : term.locators) {
          if (!locator.preference) continue;
          const auto& preference = *locator.preference;
          if (preference.group_index != replacement.group_index) return false;
          const auto groups = design_.pinAccessStorage().masterPortCandidateGroups(preference.master_port);
          const auto found = std::find_if(groups.begin(), groups.end(), [&](const auto& group) {
            return group.group_index == preference.group_index;
          });
          if (found == groups.end() || preference.point_index >= found->points.size()
              || !supportsPinAccessPoint(found->points[preference.point_index])) {
            return false;
          }
        }
      }
      return true;
    } catch (const std::exception&) {
      return false;
    }
  }

  [[nodiscard]] json::value instancePinAccessUpdateValue(const InstancePinAccessReplacement& replacement) const
  {
    json::array result;
    for (const auto& term : replacement.terms) {
      json::array values;
      values.reserve(term.locators.size());
      for (const auto& locator : term.locators) {
        if (!locator.preference) {
          values.emplace_back(json::array{static_cast<int64_t>(locator.port_ordinal), nullptr});
          continue;
        }
        const auto& preference = *locator.preference;
        const auto& group = design_.pinAccessStorage().masterPortCandidateGroup(preference.master_port, preference.group_index);
        if (preference.point_index >= group.points.size()) fail("EccDB preferred pin-access point index is out of range");
        values.emplace_back(json::array{static_cast<int64_t>(locator.port_ordinal), pinAccessValue(group.points[preference.point_index])});
      }
      result.emplace_back(json::array{term.term_name, std::move(values)});
    }
    return result;
  }

  [[nodiscard]] json::value replaceInstancePinAccess(const drt_trace::Event& event)
  {
    auto replacement = parseInstancePinAccessReplacement(event);
    eccdb::RoutingContextService context(design_);
    for (const auto& term : replacement.terms) static_cast<void>(context.clearPreferredInstancePinAccess(term.pin));
    context.setInstancePinAccessGroup(replacement.instance, replacement.group_index);
    for (const auto& term : replacement.terms) {
      std::vector<eccdb::DesignPreferredPinAccessRef> preferences;
      for (const auto& locator : term.locators) {
        if (locator.preference) preferences.push_back(*locator.preference);
      }
      context.setPreferredInstancePinAccess(term.pin, std::move(preferences));
    }
    return instancePinAccessUpdateValue(replacement);
  }

  [[nodiscard]] bool supportsAppendBtermStackWire(const drt_trace::Event& event) const
  {
    try {
      const auto& payload = payloadObject(event);
      const auto* layer = payload.if_contains("layer");
      const auto* x = payload.if_contains("x");
      const auto* y = payload.if_contains("y");
      const auto* vias = payload.if_contains("vias");
      if (layer == nullptr || x == nullptr || y == nullptr || vias == nullptr || !vias->is_array()) return false;
      static_cast<void>(net(event.object));
      static_cast<void>(routingLayer(drt_trace::text(*layer)));
      static_cast<void>(drt_trace::coordinate(*x));
      static_cast<void>(drt_trace::coordinate(*y));
      auto active_layer = routingLayer(drt_trace::text(*layer));
      for (const auto& via_name : vias->as_array()) {
        if (!technology_.viaMasterStorage().findViaMaster(drt_trace::text(via_name))) return false;
        const auto via = technology_.viaMasterStorage().findViaMaster(drt_trace::text(via_name));
        const auto layers = physical_.routingViaLayers(via, {});
        if (active_layer != layers.bottom && active_layer != layers.top) return false;
        active_layer = active_layer == layers.bottom ? layers.top : layers.bottom;
      }
      static_cast<void>(routes_.snapshot(net(event.object)));
      return true;
    } catch (const std::exception&) {
      return false;
    }
  }

  [[nodiscard]] json::value appendBtermStackWire(const drt_trace::Event& event)
  {
    const auto& payload = payloadObject(event);
    const auto* layer = payload.if_contains("layer");
    const auto* x = payload.if_contains("x");
    const auto* y = payload.if_contains("y");
    const auto* vias = payload.if_contains("vias");
    if (layer == nullptr || x == nullptr || y == nullptr || vias == nullptr || !vias->is_array()) {
      fail("trace BTerm stack-wire payload is incomplete");
    }
    const auto net_id = net(event.object);
    const auto first_layer = routingLayer(drt_trace::text(*layer));
    std::vector<eccdb::RouteShape> shapes;
    const auto snapshot = routes_.snapshot(net_id);
    for (const auto& saved_wire : snapshot.wires) {
      if (saved_wire.status == eccdb::DesignWireStatus::kRouted) {
        shapes.insert(shapes.end(), saved_wire.shapes.begin(), saved_wire.shapes.end());
      }
    }
    const eccdb::Point origin{.x = drt_trace::coordinate(*x), .y = drt_trace::coordinate(*y)};
    auto active_layer = first_layer;
    for (const auto& via_name : vias->as_array()) {
      const auto via = technology_.viaMasterStorage().findViaMaster(drt_trace::text(via_name));
      if (!via) fail("trace BTerm stack wire references an unknown technology VIA");
      const auto layers = physical_.routingViaLayers(via, {});
      if (active_layer != layers.bottom && active_layer != layers.top) {
        fail("trace BTerm stack VIA does not touch the active routing layer");
      }
      shapes.emplace_back(eccdb::RouteVia{.definition = eccdb::RouteViaDefinition{via}, .origin = origin});
      active_layer = active_layer == layers.bottom ? layers.top : layers.bottom;
    }
    static_cast<void>(routes_.replaceRouted(net_id, std::span<const eccdb::RouteShape>{shapes}));
    return drt_trace::shapeValues(wire(event.object), false);
  }

  [[nodiscard]] json::value readBtermPinAccess(std::string_view name, std::optional<uint32_t> ordinal) const
  {
    const auto pin_id = physical_.findBlockTerm(name);
    if (!pin_id) fail("EccDB trace BTerm does not exist: " + std::string(name));
    const auto block_pins = physical_.blockPins(pin_id);
    if (!ordinal && block_pins.size() != 1u) fail("legacy EccDB trace BTerm has ambiguous BPin ordinal");
    const uint32_t port_index = ordinal.value_or(0u);
    if (port_index >= block_pins.size()) fail("EccDB trace BPin ordinal is out of range");
    const auto groups = design_.pinAccessStorage().blockPinCandidateGroups(block_pins[port_index]);
    const auto group = std::find_if(groups.begin(), groups.end(), [](const auto& candidate) {
      return candidate.group_index == 0u;
    });
    json::array result;
    if (group == groups.end()) return result;
    result.reserve(group->points.size());
    for (const auto& point : group->points) result.emplace_back(pinAccessValue(point));
    return result;
  }

  [[nodiscard]] json::value readMasterPinAccess(std::string_view master_name,
                                                 std::string_view term_name,
                                                 uint32_t port_ordinal) const
  {
    const auto master = physical_.findMaster(master_name);
    if (!master) fail("EccDB trace master does not exist: " + std::string(master_name));
    const auto term = physical_.findMasterTerm(master, term_name);
    if (!term) fail("EccDB trace master term does not exist: " + std::string(term_name));
    const auto ports = physical_.masterPorts(term);
    if (port_ordinal >= ports.size()) fail("EccDB trace master port ordinal is out of range");
    return candidateGroupsValue(design_.pinAccessStorage().masterPortCandidateGroups(ports[port_ordinal]));
  }

  [[nodiscard]] json::value readPreferredPinAccess(std::string_view instance_name, std::string_view term_name) const
  {
    const auto instance = physical_.findInstance(instance_name);
    if (!instance) fail("EccDB trace instance does not exist: " + std::string(instance_name));
    const auto pin = physical_.findInstancePin(instance, term_name);
    if (!pin) fail("EccDB trace instance pin does not exist: " + std::string(term_name));
    const auto& instance_pin = design_.netlistStorage().instancePin(pin);
    std::vector<std::pair<uint32_t, json::value>> ordered;
    for (const auto& preference : design_.pinAccessStorage().preferredInstancePinAccess(pin)) {
      const auto& group = design_.pinAccessStorage().masterPortCandidateGroup(preference.master_port, preference.group_index);
      if (preference.point_index >= group.points.size()) fail("EccDB preferred pin-access point index is out of range");
      ordered.emplace_back(masterPortOrdinal(instance_pin.master_term, preference.master_port),
                           json::value(json::array{static_cast<int64_t>(masterPortOrdinal(instance_pin.master_term,
                                                                                           preference.master_port)),
                                                   pinAccessValue(group.points[preference.point_index])}));
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    json::array result;
    result.reserve(ordered.size());
    for (auto& [_, value] : ordered) result.emplace_back(std::move(value));
    return result;
  }

  [[nodiscard]] json::value readTechHeader() const
  {
    const auto& globals = technology_.globalStorage();
    if (!globals.hasUnits()) {
      fail("EccDB import did not retain LEF units");
    }
    const auto& units = globals.getUnits();
    if ((units.flags & eccdb::TechGlobalUnitsFlag::kHasDatabaseUnitsPerMicron) == 0u) {
      fail("EccDB LEF units do not include DATABASE MICRONS");
    }
    const int32_t manufacturing_grid = globals.hasManufacturingGrid() ? globals.getManufacturingGrid().value : 0;
    // OpenDB defaults USEMINSPACING OBS to ON when LEF omits the statement.
    const bool use_min_spacing_obs = globals.hasUseMinSpacingObs() ? globals.getUseMinSpacingObs() : true;
    return json::array{units.database_units_per_micron, manufacturing_grid, use_min_spacing_obs};
  }

  [[nodiscard]] json::value readDie() const
  {
    if (!physical_.hasDesignInfo() || !physical_.hasDieArea()) {
      fail("EccDB import did not retain DEF design information or DIEAREA");
    }
    const auto bounds = physical_.dieBounds();
    return json::array{physical_.databaseUnitsPerMicron(), bounds.ll_x, bounds.ll_y, bounds.ur_x, bounds.ur_y};
  }

  [[nodiscard]] json::value scanTracks() const
  {
    json::array result;
    physical_.forEachTrackGrid([&](eccdb::DesignTrackGridId, const eccdb::DesignTrackGrid& grid) {
      const auto count = checkedTraceInt(grid.track_count, "EccDB TRACKS count");
      for (const auto layer : grid.layers) {
        result.emplace_back(json::array{physical_.layerInfo(eccdb::TechLayerId{layer.entity()}).name,
                                        axisName(grid.axis),
                                        grid.start,
                                        count,
                                        grid.step});
      }
    });
    return result;
  }

  [[nodiscard]] json::value readInstancePins(uint32_t token) const
  {
    const auto instance_id = instance(token);
    const auto& netlist = physical_.design().netlistStorage();
    json::array result;
    for (const auto pin : netlist.instancePins(instance_id)) {
      const auto& value = netlist.instanceTerm(pin);
      result.emplace_back(value.master_term_ptr->name);
    }
    return result;
  }

  [[nodiscard]] json::value readNetAttributes(uint32_t token) const
  {
    const auto net_id = net(token);
    const auto& value = physical_.design().netlistStorage().net(net_id);
    const auto rule = physical_.nonDefaultRuleName(net_id);
    return json::array{signalUseName(value.use),
                       rule ? json::value(std::string(*rule)) : json::value(nullptr),
                       physical_.netHasJumpers(net_id),
                       physical_.isNetConnectedByAbutment(net_id)};
  }

  [[nodiscard]] json::value readGCellGrid() const
  {
    const eccdb::DesignGCellGrid* x_grid = nullptr;
    const eccdb::DesignGCellGrid* y_grid = nullptr;
    bool multiple_patterns = false;
    physical_.forEachGCellGrid([&](eccdb::DesignGCellGridId, const eccdb::DesignGCellGrid& grid) {
      const auto*& destination = grid.axis == eccdb::DesignAxis::kX ? x_grid : y_grid;
      if (destination != nullptr) {
        // ODB's trace projection is null unless it has exactly one pattern
        // for each axis. Mirror that schema rather than discarding patterns.
        multiple_patterns = true;
        return;
      }
      destination = &grid;
    });
    if (multiple_patterns || x_grid == nullptr || y_grid == nullptr) {
      return json::value(nullptr);
    }
    return json::array{x_grid->start,
                       checkedTraceInt(x_grid->line_count, "EccDB GCELLGRID X count"),
                       x_grid->step,
                       y_grid->start,
                       checkedTraceInt(y_grid->line_count, "EccDB GCELLGRID Y count"),
                       y_grid->step};
  }

  [[nodiscard]] json::value scanInstances() const
  {
    json::array result;
    const auto& netlist = physical_.design().netlistStorage();
    for (const auto id : netlist.instances()) result.emplace_back(netlist.instance(id).name);
    return result;
  }

  [[nodiscard]] json::value scanNets() const
  {
    json::array result;
    physical_.forEachRegularNet([&](eccdb::DesignNetId, const eccdb::DesignNet& value) { result.emplace_back(value.name); });
    return result;
  }

  [[nodiscard]] json::value readInstance(uint32_t token) const
  {
    const auto& netlist = physical_.design().netlistStorage();
    const auto& value = netlist.instance(instance(token));
    const auto& master = physical_.library().cellMasterStorage().cellMaster(value.master);
    return json::array{value.name, master.name, value.origin.x, value.origin.y, orientationName(value.orientation)};
  }

  [[nodiscard]] json::value netPins(uint32_t token) const
  {
    json::array result;
    eccdb::PhysicalNetPinVisitor visitor;
    visitor.instance_pin = [&](eccdb::DesignInstancePinId,
                                const eccdb::DesignInstancePin&,
                                const eccdb::DesignInstance& instance_value,
                                const eccdb::LibraryMasterTerm& term) {
      result.emplace_back(json::array{"I", instance_value.name, term.name});
    };
    visitor.io_pin = [&](eccdb::DesignIoPinId, const eccdb::DesignIoPin& pin) {
      result.emplace_back(json::array{"B", pin.name, ""});
    };
    physical_.forEachNetPin(net(token), visitor);
    return result;
  }

  [[nodiscard]] json::value findInstance(std::string_view name) const
  {
    const auto found = physical_.findInstance(name);
    if (!found) return json::value(nullptr);
    return json::value(physical_.design().netlistStorage().instance(found).name);
  }

  [[nodiscard]] json::value findPin(uint32_t token, std::string_view name) const
  {
    const auto* pin = physical_.findInstanceTermData(instance(token), name);
    if (pin == nullptr) return json::value(nullptr);
    return json::value(pin->master_term_ptr->name);
  }

  eccdb::TechStore technology_;
  eccdb::LibraryStore library_;
  eccdb::DesignStore design_;
  eccdb::DesignViaService vias_;
  eccdb::PhysicalDesignService physical_;
  eccdb::NetRouteService routes_;
  bool profile_wire_write_ = false;
  eccdb::NetRouteWriteProfile wire_write_profile_;
  uint64_t wire_write_adaptor_convert_ns_ = 0;
  uint64_t wire_write_net_resolution_ns_ = 0;
  uint64_t wire_write_service_call_ns_ = 0;
  uint64_t wire_write_adaptor_total_ns_ = 0;
  std::vector<eccdb::DesignInstanceId> instances_;
  std::vector<eccdb::DesignNetId> nets_;
};

class IdbBackend
{
 public:
  static constexpr bool retains_extensions = false;

  IdbBackend(const drt_trace::Options& options, const json::object& header)
  {
    std::vector<std::string> lefs{options.lef.string()};
    if (!builder_.buildLef(lefs, false)) {
      fail("iDB LEF import failed");
    }
    const auto service = builder_.buildDef(options.def.string());
    if (service == nullptr || (design_ = service->get_design()) == nullptr) {
      fail("iDB DEF import failed");
    }
    restoreInitialGCellGrid(header);
    bind(header);
  }

  // iDB has DBU, manufacturing-grid, die, track, terminal and GCell data,
  // but does not retain the full ODB contracts for USEMINSPACING OBS or the
  // ordinary-net NDR/jumper/abutment tuple.
  [[nodiscard]] bool supports(const drt_trace::Event& event) const
  {
    // An unsupported mutation leaves this net's later route state unknown.
    // A later full replacement re-establishes an exact iDB projection.
    if (event.op == "append_bterm_stack_wire") {
      unavailable_wire_state_.insert(event.object);
      return false;
    }
    if (event.op == "read_wire" && unavailable_wire_state_.contains(event.object)) return false;
    return event.op == "read_die" || event.op == "scan_tracks" || event.op == "read_instance_pins"
           || event.op == "read_gcell_grid" || event.op == "scan_instances" || event.op == "scan_nets"
           || event.op == "read_instance" || event.op == "net_pins" || event.op == "read_wire"
           || event.op == "find_instance" || event.op == "find_pin" || event.op == "define_design_via"
           || event.op == "clear_wire" || event.op == "replace_wire"
           || (event.op == "read_tech_layers" && supportsTechLayers())
           || (event.op == "read_tech_vias" && supportsVias(layout()->get_via_list()))
           || (event.op == "read_design_vias" && supportsVias(design_->get_via_list()))
           || (event.op == "read_library_masters" && supportsLibraryMasters())
           || (event.op == "read_library_terms" && supportsLibraryTerms())
           || (event.op == "read_library_mpin_geometry" && supportsLibraryMPinGeometry())
           || (event.op == "read_blockages" && supportsBlockages())
           || (event.op == "read_bterms" && supportsBterms(event));
  }

  [[nodiscard]] json::value execute(const drt_trace::Event& event)
  {
    if (event.op == "read_tech_header") return readTechHeader();
    if (event.op == "read_tech_layers") return readTechLayers();
    if (event.op == "read_tech_vias") return readTechVias();
    if (event.op == "read_design_vias") return readDesignVias();
    if (event.op == "read_library_masters") return readLibraryMasters();
    if (event.op == "read_library_terms") return readLibraryTerms();
    if (event.op == "read_library_mpin_geometry") return readLibraryMPinGeometry();
    if (event.op == "read_die") return readDie();
    if (event.op == "scan_tracks") return scanTracks();
    if (event.op == "read_instance_pins") return readInstancePins(event.object);
    if (event.op == "read_net_attributes") return readNetAttributes(event.object);
    if (event.op == "read_gcell_grid") return readGCellGrid();
    if (event.op == "read_blockages") return readBlockages();
    if (event.op == "read_bterms") return readBterms();
    if (event.op == "scan_instances") return scanInstances();
    if (event.op == "scan_nets") return scanNets();
    if (event.op == "read_instance") return readInstance(event.object);
    if (event.op == "net_pins") return netPins(event.object);
    if (event.op == "read_wire") return drt_trace::shapeProjection(wire(event.object), false);
    if (event.op == "find_instance") return findInstance(event.name);
    if (event.op == "find_pin") return findPin(event.object, event.name);
    if (event.op == "define_design_via") {
      defineDesignVia(event);
      return viaDefinition(event.name);
    }
    if (event.op == "clear_wire") {
      net(event.object)->clear_wire_list();
      unavailable_wire_state_.erase(event.object);
      return json::value(nullptr);
    }
    if (event.op == "replace_wire") {
      replaceWire(net(event.object), event.shapes);
      unavailable_wire_state_.erase(event.object);
      return json::value(nullptr);
    }
    fail("unsupported iDB trace operation: " + event.op);
  }

  // Keep the iDB projection code out of the native timer for the same reason
  // as EccDB: the trace's JSON vocabulary is not an iDB API cost.  The timer
  // covers list lookup/traversal and native mutation only.
  [[nodiscard]] json::value executeNative(const drt_trace::Event& event, drt_trace::NativeTimer& timer)
  {
    const auto item_count = event.expected.is_array() ? static_cast<uint64_t>(event.expected.as_array().size()) : 1u;

    if (event.op == "find_instance") {
      auto* instances = design_->get_instance_list();
      if (instances == nullptr) fail("iDB import did not create instance storage");
      const auto name = unescapeDefIdentifier(event.name);
      auto* value = timer.measure("find_instance", 1u, [&] { return instances->find_instance(name); });
      return value == nullptr ? json::value(nullptr) : json::value(traceName(value));
    }
    if (event.op == "find_pin") {
      auto* value = instance(event.object);
      const std::string term_name = event.name;
      struct PinValue
      {
        idb::IdbPin* pin = nullptr;
      } raw;
      timer.measure("find_pin", 1u, [&] {
        raw.pin = value->get_pin(term_name);
        if (raw.pin == nullptr) raw.pin = value->get_pin_by_term(term_name);
      });
      if (raw.pin == nullptr) return json::value(nullptr);
      return json::value(raw.pin->get_term_name());
    }
    if (event.op == "read_instance") {
      const auto* value = instance(event.object);
      struct Value
      {
        idb::IdbInstance* instance = nullptr;
        std::string_view instance_name;
        std::string_view master_name;
        int32_t x = 0;
        int32_t y = 0;
        idb::IdbOrient orient = idb::IdbOrient::kN_R0;
      } raw;
      timer.measure("read_instance", 1u, [&] {
        raw.instance = const_cast<idb::IdbInstance*>(value);
        raw.instance_name = raw.instance->get_name();
        raw.master_name = raw.instance->get_cell_master()->get_name();
        auto* coordinate = raw.instance->get_coordinate();
        raw.x = coordinate->get_x();
        raw.y = coordinate->get_y();
        raw.orient = raw.instance->get_orient();
      });
      return json::array{traceName(raw.instance, raw.instance_name), std::string(raw.master_name), raw.x, raw.y,
                         orientationName(raw.orient)};
    }
    if (event.op == "read_instance_pins") {
      auto* value = instance(event.object);
      std::vector<std::string> terms;
      terms.reserve(static_cast<std::size_t>(item_count));
      timer.measure("read_instance_pins", item_count, [&] {
        for (auto* pin : value->get_pin_list()->get_pin_list()) terms.push_back(pin->get_term_name());
      });
      json::array result;
      result.reserve(terms.size());
      for (const auto& term : terms) result.emplace_back(term);
      return result;
    }
    if (event.op == "scan_instances") {
      auto* source = design_->get_instance_list();
      if (source == nullptr) fail("iDB import did not create instance storage");
      struct InstanceValue
      {
        idb::IdbInstance* instance = nullptr;
        std::string_view name;
      };
      std::vector<InstanceValue> values;
      values.reserve(static_cast<std::size_t>(item_count));
      timer.measure("scan_instances", item_count, [&] {
        for (auto* value : source->get_instance_list()) values.push_back({value, value->get_name()});
      });
      json::array result;
      result.reserve(values.size());
      for (const auto& value : values) result.emplace_back(traceName(value.instance, value.name));
      return result;
    }
    if (event.op == "scan_nets") {
      auto* source = design_->get_net_list();
      if (source == nullptr) fail("iDB import did not create ordinary-net storage");
      struct NetValue
      {
        idb::IdbNet* net = nullptr;
        std::string name;
      };
      std::vector<NetValue> values;
      values.reserve(static_cast<std::size_t>(item_count));
      timer.measure("scan_nets", item_count, [&] {
        for (auto* value : source->get_net_list()) values.push_back({value, value->get_net_name()});
      });
      json::array result;
      result.reserve(values.size());
      for (const auto& value : values) result.emplace_back(traceName(value.net, value.name));
      return result;
    }
    if (event.op == "net_pins") {
      auto* value = net(event.object);
      struct PinValue
      {
        bool instance = false;
        idb::IdbInstance* instance_value = nullptr;
        std::string_view instance_name;
        std::string term_name;
        std::string pin_name;
      };
      std::vector<PinValue> values;
      values.reserve(static_cast<std::size_t>(item_count));
      timer.measure("net_pins", item_count, [&] {
        for (auto* pin : value->get_instance_pin_list()->get_pin_list()) {
          auto* instance_value = pin->get_instance();
          values.push_back({.instance = true,
                            .instance_value = instance_value,
                            .instance_name = instance_value->get_name(),
                            .term_name = pin->get_term_name()});
        }
        for (auto* pin : value->get_io_pins()->get_pin_list()) {
          values.push_back({.instance = false, .pin_name = pin->get_pin_name()});
        }
      });
      json::array result;
      result.reserve(values.size());
      for (const auto& entry : values) {
        if (entry.instance) {
          result.emplace_back(json::array{"I", traceName(entry.instance_value, entry.instance_name), entry.term_name});
        } else {
          result.emplace_back(json::array{"B", entry.pin_name, ""});
        }
      }
      return result;
    }
    if (event.op == "read_wire") {
      auto* value = net(event.object);
      std::vector<NativeWireShape> shapes;
      timer.measure("read_wire", item_count, [&] { shapes = readWireNative(value); });
      return drt_trace::shapeProjection(projectWireNative(shapes), false);
    }
    if (event.op == "clear_wire") {
      auto* value = net(event.object);
      timer.measure("clear_wire", 0u, [&] { value->clear_wire_list(); });
      unavailable_wire_state_.erase(event.object);
      return json::value(nullptr);
    }
    if (event.op == "replace_wire") {
      auto* value = net(event.object);
      auto shapes = prepareWire(event.shapes);
      const auto count = static_cast<uint64_t>(shapes.size());
      timer.measure("replace_wire", count, [&] {
        replaceWireNative(value, std::span<const NativeWireShape>{shapes.data(), shapes.size()});
      });
      unavailable_wire_state_.erase(event.object);
      return json::value(nullptr);
    }

    return execute(event);
  }

  [[nodiscard]] std::vector<drt_trace::Shape> wire(uint32_t token) const
  {
    return readWire(net(token));
  }

  [[nodiscard]] json::value viaDefinition(std::string_view name) const
  {
    auto* values = design_->get_via_list();
    if (values == nullptr) {
      fail("iDB import did not create design VIA storage");
    }
    auto* via = values->find_via(std::string(name));
    if (via == nullptr) {
      fail("missing iDB design VIA after replay: " + std::string(name));
    }
    auto* master = via->get_instance();
    if (master == nullptr || !master->is_fix()) {
      fail("iDB design VIA has unsupported non-fixed geometry: " + std::string(name));
    }

    std::vector<drt_trace::ViaRect> rectangles;
    for (auto* fixed : master->get_master_fixed_list()) {
      if (fixed == nullptr || fixed->get_layer() == nullptr) {
        fail("iDB design VIA has an invalid fixed layer: " + std::string(name));
      }
      for (auto* rectangle : fixed->get_rect_list()) {
        if (rectangle == nullptr || rectangle->get_low_x() >= rectangle->get_high_x()
            || rectangle->get_low_y() >= rectangle->get_high_y()) {
          fail("iDB design VIA has an invalid fixed rectangle: " + std::string(name));
        }
        rectangles.push_back({.layer = fixed->get_layer()->get_name(),
                              .x1 = rectangle->get_low_x(),
                              .y1 = rectangle->get_low_y(),
                              .x2 = rectangle->get_high_x(),
                              .y2 = rectangle->get_high_y()});
      }
    }
    return drt_trace::viaValue(via->get_name(), rectangles);
  }

 private:
  struct ViaPrototype
  {
    idb::IdbVia* via = nullptr;
    bool tech_via = false;
  };

  struct NativeWireShape
  {
    enum class Kind : uint8_t
    {
      kSegment,
      kVia,
      kPatch
    };

    Kind kind = Kind::kSegment;
    idb::IdbLayer* layer = nullptr;
    idb::IdbVia* via = nullptr;
    std::string layer_name;
    std::string via_name;
    bool tech_via = true;
    int32_t x1 = 0;
    int32_t y1 = 0;
    int32_t x2 = 0;
    int32_t y2 = 0;
    int32_t delta_x = 0;
    int32_t delta_y = 0;
  };

  [[nodiscard]] std::vector<NativeWireShape> prepareWire(const std::vector<drt_trace::Shape>& source) const
  {
    std::vector<NativeWireShape> result;
    result.reserve(source.size());
    for (const auto& shape : source) {
      if (shape.kind == "segment") {
        if (shape.x1 != shape.x2 && shape.y1 != shape.y2) fail("trace segment is not Manhattan");
        auto* layer = routingLayer(shape.layer);
        result.push_back({.kind = NativeWireShape::Kind::kSegment,
                          .layer = layer,
                          .layer_name = layer->get_name(),
                          .x1 = shape.x1,
                          .y1 = shape.y1,
                          .x2 = shape.x2,
                          .y2 = shape.y2});
      } else if (shape.kind == "via") {
        if (shape.via.empty() || shape.x1 != shape.x2 || shape.y1 != shape.y2 || shape.begin_extension || shape.end_extension) {
          fail("trace VIA carries unsupported metadata");
        }
        const auto source_via = viaPrototype(shape.via, shape.tech_via);
        const auto bottom = viaBottomLayer(source_via.via);
        if (bottom != shape.layer) fail("trace VIA layer is not the VIA's canonical iDB bottom routing layer");
        auto* layer = routingLayer(bottom);
        result.push_back({.kind = NativeWireShape::Kind::kVia,
                          .layer = layer,
                          .via = source_via.via,
                          .layer_name = layer->get_name(),
                          .tech_via = source_via.tech_via,
                          .x1 = shape.x1,
                          .y1 = shape.y1,
                          .x2 = shape.x2,
                          .y2 = shape.y2});
      } else if (shape.kind == "patch") {
        if (shape.x2 < shape.x1 || shape.y2 < shape.y1) fail("trace patch has an invalid rectangle");
        auto* layer = routingLayer(shape.layer);
        result.push_back({.kind = NativeWireShape::Kind::kPatch,
                          .layer = layer,
                          .layer_name = layer->get_name(),
                          .x1 = shape.x1,
                          .y1 = shape.y1,
                          .x2 = shape.x2,
                          .y2 = shape.y2,
                          .delta_x = checkedDelta(shape.x2, shape.x1, "trace patch width"),
                          .delta_y = checkedDelta(shape.y2, shape.y1, "trace patch height")});
      } else {
        fail("unsupported iDB route shape '" + shape.kind + "'");
      }
    }
    return result;
  }

  void replaceWireNative(idb::IdbNet* value, std::span<const NativeWireShape> shapes) const
  {
    value->clear_wire_list();
    if (shapes.empty()) return;

    auto* wire = value->get_wire_list()->add_wire();
    wire->set_wire_state(idb::IdbWiringStatement::kRouted);
    for (const auto& shape : shapes) {
      auto* segment = wire->add_segment();
      segment->set_layer_name(shape.layer_name);
      segment->set_layer(shape.layer);
      if (shape.kind == NativeWireShape::Kind::kSegment) {
        segment->add_point(shape.x1, shape.y1);
        segment->add_point(shape.x2, shape.y2);
      } else if (shape.kind == NativeWireShape::Kind::kVia) {
        segment->add_point(shape.x1, shape.y1);
        segment->set_is_via(true);
        auto* copy = segment->copy_via(shape.via);
        copy->set_coordinate(shape.x1, shape.y1);
      } else {
        segment->add_point(shape.x1, shape.y1);
        segment->set_is_rect(true);
        segment->set_delta_rect(0, 0, shape.delta_x, shape.delta_y);
      }
    }
  }

  [[nodiscard]] std::vector<NativeWireShape> readWireNative(idb::IdbNet* value) const
  {
    std::vector<NativeWireShape> result;
    for (auto* wire : value->get_wire_list()->get_wire_list()) {
      for (auto* segment : wire->get_segment_list()) {
        const auto& points = segment->get_point_list();
        if (segment->is_rect()) {
          auto* rectangle = segment->get_delta_rect();
          result.push_back({.kind = NativeWireShape::Kind::kPatch,
                            .layer = segment->get_layer(),
                            .layer_name = segment->get_layer()->get_name(),
                            .x1 = points.front()->get_x(),
                            .y1 = points.front()->get_y(),
                            .x2 = checkedOffset(points.front()->get_x(), rectangle->get_high_x(), "iDB RECT high x"),
                            .y2 = checkedOffset(points.front()->get_y(), rectangle->get_high_y(), "iDB RECT high y")});
        } else if (segment->is_via() || !segment->get_via_list().empty()) {
          auto* via = segment->get_via_list().front();
          const auto source = sourceOf(via);
          auto coordinate = via->get_coordinate();
          auto bottom = const_cast<idb::IdbVia*>(via)->get_bottom_layer_shape();
          result.push_back({.kind = NativeWireShape::Kind::kVia,
                            .layer = bottom.get_layer(),
                            .via = via,
                            .layer_name = bottom.get_layer()->get_name(),
                            .via_name = via->get_name(),
                            .tech_via = source.tech_via,
                            .x1 = coordinate->get_x(),
                            .y1 = coordinate->get_y(),
                            .x2 = coordinate->get_x(),
                            .y2 = coordinate->get_y()});
        } else {
          result.push_back({.kind = NativeWireShape::Kind::kSegment,
                            .layer = segment->get_layer(),
                            .layer_name = segment->get_layer()->get_name(),
                            .x1 = points[0]->get_x(),
                            .y1 = points[0]->get_y(),
                            .x2 = points[1]->get_x(),
                            .y2 = points[1]->get_y()});
        }
      }
    }
    return result;
  }

  [[nodiscard]] std::vector<drt_trace::Shape> projectWireNative(std::span<const NativeWireShape> source) const
  {
    std::vector<drt_trace::Shape> result;
    result.reserve(source.size());
    for (const auto& shape : source) {
      if (shape.kind == NativeWireShape::Kind::kSegment) {
        result.push_back({.kind = "segment", .layer = shape.layer_name, .x1 = shape.x1, .y1 = shape.y1,
                          .x2 = shape.x2, .y2 = shape.y2});
      } else if (shape.kind == NativeWireShape::Kind::kVia) {
        result.push_back({.kind = "via", .layer = shape.layer_name, .via = shape.via_name,
                          .x1 = shape.x1, .y1 = shape.y1, .x2 = shape.x2, .y2 = shape.y2, .tech_via = shape.tech_via});
      } else {
        result.push_back({.kind = "patch", .layer = shape.layer_name, .x1 = shape.x1, .y1 = shape.y1,
                          .x2 = shape.x2, .y2 = shape.y2});
      }
    }
    return result;
  }

  void bind(const json::object& header)
  {
    const auto* instances = design_->get_instance_list();
    const auto* nets = design_->get_net_list();
    if (instances == nullptr || nets == nullptr) {
      fail("iDB import did not create instance or ordinary-net storage");
    }
    const auto instance_names = tokenNames(header, "instances");
    instances_.resize(instance_names.size());
    for (std::size_t token = 0; token < instance_names.size(); ++token) {
      const auto& name = instance_names[token];
      if (name.empty()) continue;
      auto* value = design_->get_instance_list()->find_instance(name);
      if (value == nullptr) {
        const auto normalized = unescapeDefIdentifier(name);
        if (normalized != name) value = design_->get_instance_list()->find_instance(normalized);
      }
      if (value == nullptr) {
        fail("iDB token binding cannot find instance '" + name + "'");
      }
      instances_[token] = value;
      instance_trace_names_.emplace(value, name);
    }
    const auto net_names = tokenNames(header, "nets");
    nets_.resize(net_names.size());
    for (std::size_t token = 0; token < net_names.size(); ++token) {
      const auto& name = net_names[token];
      if (name.empty()) continue;
      auto* value = design_->get_net_list()->find_net(name);
      if (value == nullptr) {
        const auto normalized = unescapeDefIdentifier(name);
        if (normalized != name) value = design_->get_net_list()->find_net(normalized);
      }
      if (value == nullptr) {
        fail("iDB token binding cannot find ordinary net '" + name + "'");
      }
      nets_[token] = value;
      net_trace_names_.emplace(value, name);
    }
  }

  void restoreInitialGCellGrid(const json::object& header)
  {
    const auto* value = header.if_contains("initial_gcell_grid");
    if (value == nullptr) return;

    auto* grids = layout()->get_gcell_grid_list();
    if (grids == nullptr) fail("iDB import did not create GCELLGRID storage");
    grids->clear();
    const auto grid = parseInitialGCellGrid(*value);
    if (!grid) return;

    const auto add = [&](idb::IdbTrackDirection direction, int32_t start, int32_t count, int32_t step) {
      auto* pattern = grids->add_gcell_grid();
      if (pattern == nullptr) fail("cannot create iDB GCELLGRID pattern");
      pattern->set_direction(direction);
      pattern->set_start(start);
      pattern->set_num(count);
      pattern->set_space(step);
    };
    add(idb::IdbTrackDirection::kDirectionX, grid->x_start, grid->x_count, grid->x_step);
    add(idb::IdbTrackDirection::kDirectionY, grid->y_start, grid->y_count, grid->y_step);
  }

  [[nodiscard]] idb::IdbInstance* instance(uint32_t token) const
  {
    if (token >= instances_.size() || instances_[token] == nullptr) {
      fail("unknown iDB instance token " + std::to_string(token));
    }
    return instances_[token];
  }

  [[nodiscard]] idb::IdbNet* net(uint32_t token) const
  {
    if (token >= nets_.size() || nets_[token] == nullptr) {
      fail("unknown iDB net token " + std::to_string(token));
    }
    return nets_[token];
  }

  [[nodiscard]] std::string_view traceName(idb::IdbInstance* value) const
  {
    if (const auto found = instance_trace_names_.find(value); found != instance_trace_names_.end()) return found->second;
    return value->get_name();
  }

  [[nodiscard]] std::string_view traceName(idb::IdbInstance* value, std::string_view native_name) const
  {
    if (const auto found = instance_trace_names_.find(value); found != instance_trace_names_.end()) return found->second;
    return native_name;
  }

  [[nodiscard]] std::string_view traceName(idb::IdbNet* value) const
  {
    if (const auto found = net_trace_names_.find(value); found != net_trace_names_.end()) return found->second;
    return value->get_net_name();
  }

  [[nodiscard]] std::string_view traceName(idb::IdbNet* value, std::string_view native_name) const
  {
    if (const auto found = net_trace_names_.find(value); found != net_trace_names_.end()) return found->second;
    return native_name;
  }

  [[nodiscard]] idb::IdbLayout* layout() const
  {
    auto* value = design_->get_layout();
    if (value == nullptr || value->get_layers() == nullptr || value->get_via_list() == nullptr) {
      fail("iDB import did not retain LEF layout layers and VIAs");
    }
    return value;
  }

  [[nodiscard]] idb::IdbLayer* routingLayer(std::string_view name) const
  {
    auto* value = layout()->get_layers()->find_layer(std::string(name));
    if (value == nullptr || !value->is_routing()) {
      fail("trace route layer '" + std::string(name) + "' is not an iDB routing layer");
    }
    return value;
  }

  [[nodiscard]] idb::IdbLayer* viaLayer(std::string_view name) const
  {
    auto* value = layout()->get_layers()->find_layer(std::string(name));
    if (value == nullptr || (!value->is_routing() && !value->is_cut())) {
      fail("trace design VIA layer '" + std::string(name) + "' is not an iDB routing or cut layer");
    }
    return value;
  }

  void defineDesignVia(const drt_trace::Event& event) const
  {
    if (event.name.empty() || event.via_rectangles.empty()) {
      fail("trace design VIA has no name or geometry");
    }
    auto* values = design_->get_via_list();
    if (values == nullptr) {
      fail("iDB import did not create design VIA storage");
    }
    if (values->find_via(event.name) != nullptr) {
      fail("trace design VIA already exists in iDB: " + event.name);
    }

    struct ResolvedRectangle
    {
      const drt_trace::ViaRect* rectangle = nullptr;
      idb::IdbLayer* layer = nullptr;
    };
    std::vector<ResolvedRectangle> rectangles;
    rectangles.reserve(event.via_rectangles.size());
    std::vector<idb::IdbLayer*> layers;
    for (const auto& rectangle : event.via_rectangles) {
      if (rectangle.layer.empty() || rectangle.x1 >= rectangle.x2 || rectangle.y1 >= rectangle.y2) {
        fail("trace design VIA has invalid rectangle geometry: " + event.name);
      }
      auto* layer = viaLayer(rectangle.layer);
      rectangles.push_back({.rectangle = &rectangle, .layer = layer});
      if (std::find(layers.begin(), layers.end(), layer) == layers.end()) {
        layers.push_back(layer);
      }
    }

    const auto routing_count = static_cast<std::size_t>(std::count_if(
        layers.begin(), layers.end(), [](idb::IdbLayer* layer) { return layer->is_routing(); }));
    const auto cut_count = static_cast<std::size_t>(std::count_if(
        layers.begin(), layers.end(), [](idb::IdbLayer* layer) { return layer->is_cut(); }));
    if (layers.size() != 3u || routing_count != 2u || cut_count != 1u) {
      fail("trace fixed design VIA must have exactly two routing layers and one cut layer: " + event.name);
    }

    auto* via = values->add_via(event.name);
    if (via == nullptr || via->get_name() != event.name) {
      fail("iDB could not create trace design VIA: " + event.name);
    }
    auto* master = via->get_instance();
    if (master == nullptr) {
      fail("iDB could not create trace design VIA master: " + event.name);
    }
    master->set_name(event.name);
    master->set_type_fixed();
    master->set_default(event.via_default);

    std::vector<std::pair<idb::IdbLayer*, idb::IdbViaMasterFixed*>> fixed_layers;
    fixed_layers.reserve(layers.size());
    int32_t cut_ll_x = std::numeric_limits<int32_t>::max();
    int32_t cut_ll_y = std::numeric_limits<int32_t>::max();
    int32_t cut_ur_x = std::numeric_limits<int32_t>::min();
    int32_t cut_ur_y = std::numeric_limits<int32_t>::min();
    for (const auto& source : rectangles) {
      auto fixed = std::find_if(fixed_layers.begin(), fixed_layers.end(), [&](const auto& entry) {
        return entry.first == source.layer;
      });
      if (fixed == fixed_layers.end()) {
        auto* value = master->add_fixed(source.layer->get_name());
        if (value == nullptr) {
          fail("iDB could not create trace design VIA layer: " + event.name);
        }
        value->set_layer(source.layer);
        fixed = fixed_layers.emplace(fixed_layers.end(), source.layer, value);
      }
      fixed->second->add_rect(source.rectangle->x1, source.rectangle->y1, source.rectangle->x2, source.rectangle->y2);
      if (source.layer->is_cut()) {
        cut_ll_x = std::min(cut_ll_x, source.rectangle->x1);
        cut_ll_y = std::min(cut_ll_y, source.rectangle->y1);
        cut_ur_x = std::max(cut_ur_x, source.rectangle->x2);
        cut_ur_y = std::max(cut_ur_y, source.rectangle->y2);
      }
    }
    master->set_cut_row_col(1, 1);
    master->set_cut_rect(cut_ll_x, cut_ll_y, cut_ur_x, cut_ur_y);
    master->set_via_shape();
    if (master->is_default() != event.via_default) {
      fail("iDB design VIA default flag mismatch: " + event.name);
    }
  }

  [[nodiscard]] ViaPrototype viaPrototype(std::string_view name, bool tech_via) const
  {
    idb::IdbVias* values = tech_via ? layout()->get_via_list() : design_->get_via_list();
    if (values == nullptr) {
      fail("iDB has no source VIA list for trace VIA '" + std::string(name) + "'");
    }
    auto* via = values->find_via(std::string(name));
    if (via == nullptr) {
      fail("trace references unknown iDB " + std::string(tech_via ? "technology" : "design") + " VIA '" + std::string(name)
           + "'");
    }
    return {.via = via, .tech_via = tech_via};
  }

  [[nodiscard]] ViaPrototype sourceOf(const idb::IdbVia* value) const
  {
    if (value == nullptr || value->get_name().empty()) {
      fail("iDB routed VIA has no name");
    }
    // copy_via() preserves the source master's pointer. Use it to distinguish
    // same-named DEF and LEF definitions without an auxiliary shadow table.
    auto* master = const_cast<idb::IdbVia*>(value)->get_instance();
    if (master == nullptr || master->get_name().empty()) {
      fail("iDB routed VIA has no master geometry");
    }
    if (auto* list = design_->get_via_list(); list != nullptr) {
      if (auto* source = list->find_via(value->get_name()); source != nullptr && source->get_instance() == master) {
        return {.via = source, .tech_via = false};
      }
    }
    if (auto* source = layout()->get_via_list()->find_via(value->get_name()); source != nullptr && source->get_instance() == master) {
      return {.via = source, .tech_via = true};
    }
    fail("iDB routed VIA is not backed by an original DEF or LEF VIA definition");
  }

  [[nodiscard]] std::string viaBottomLayer(const idb::IdbVia* value) const
  {
    if (value == nullptr) {
      fail("iDB routed VIA is null");
    }
    auto shape = const_cast<idb::IdbVia*>(value)->get_bottom_layer_shape();
    auto* layer = shape.get_layer();
    if (layer == nullptr || !layer->is_routing()) {
      fail("iDB routed VIA has no routing bottom layer");
    }
    return layer->get_name();
  }

  [[nodiscard]] std::vector<drt_trace::Shape> readWire(idb::IdbNet* value) const
  {
    if (value == nullptr || value->get_wire_list() == nullptr) {
      fail("iDB ordinary net has no wire list");
    }
    std::vector<drt_trace::Shape> result;
    for (auto* wire : value->get_wire_list()->get_wire_list()) {
      if (wire == nullptr) {
        fail("iDB ordinary net has a null wire");
      }
      if (wire->get_wire_statement() != idb::IdbWiringStatement::kRouted) {
        fail("iDB trace replay only supports initial ROUTED ordinary wires");
      }
      if (!wire->get_shiled_name().empty()) {
        fail("iDB trace replay cannot project ordinary-wire shield metadata");
      }
      for (auto* segment : wire->get_segment_list()) {
        if (segment == nullptr || segment->get_layer() == nullptr || !segment->get_layer()->is_routing()) {
          fail("iDB routed segment has no routing layer");
        }

        std::vector<eccdb::Point> points;
        const auto& source_points = segment->get_point_list();
        points.reserve(source_points.size());
        for (auto* point : source_points) {
          if (point == nullptr) {
            fail("iDB routed segment has a null point");
          }
          if (segment->is_virtual(point)) {
            fail("iDB trace replay cannot project VIRTUAL wire points");
          }
          points.push_back({.x = point->get_x(), .y = point->get_y()});
        }

        const auto& vias = segment->get_via_list();
        if (segment->is_rect()) {
          if (segment->is_via() || !vias.empty() || points.size() != 1u || segment->get_delta_rect() == nullptr) {
            fail("iDB trace replay supports only one-point RECT segments");
          }
          auto* rectangle = segment->get_delta_rect();
          result.push_back({.kind = "patch",
                            .layer = segment->get_layer()->get_name(),
                            .x1 = checkedOffset(points[0].x, rectangle->get_low_x(), "iDB RECT low x"),
                            .y1 = checkedOffset(points[0].y, rectangle->get_low_y(), "iDB RECT low y"),
                            .x2 = checkedOffset(points[0].x, rectangle->get_high_x(), "iDB RECT high x"),
                            .y2 = checkedOffset(points[0].y, rectangle->get_high_y(), "iDB RECT high y")});
          continue;
        }

        if (segment->is_via() || !vias.empty()) {
          if (!segment->is_via() || vias.size() != 1u || points.size() != 1u) {
            fail("iDB trace replay supports only one-point, one-VIA segments");
          }
          auto* via = vias.front();
          if (via == nullptr || via->get_coordinate() == nullptr) {
            fail("iDB routed segment has an invalid VIA");
          }
          const auto source = sourceOf(via);
          auto* coordinate = via->get_coordinate();
          if (coordinate->get_x() != points[0].x || coordinate->get_y() != points[0].y) {
            fail("iDB routed VIA origin differs from its segment point");
          }
          const std::string bottom = viaBottomLayer(via);
          if (segment->get_layer()->get_name() != bottom) {
            fail("iDB routed VIA segment does not use the VIA's canonical bottom routing layer");
          }
          result.push_back({.kind = "via",
                            .layer = bottom,
                            .via = via->get_name(),
                            .x1 = coordinate->get_x(),
                            .y1 = coordinate->get_y(),
                            .x2 = coordinate->get_x(),
                            .y2 = coordinate->get_y(),
                            .tech_via = source.tech_via});
          continue;
        }

        if (points.size() != 2u || (points[0].x != points[1].x && points[0].y != points[1].y)) {
          fail("iDB trace replay supports only two-point Manhattan line segments");
        }
        result.push_back({.kind = "segment",
                          .layer = segment->get_layer()->get_name(),
                          .x1 = points[0].x,
                          .y1 = points[0].y,
                          .x2 = points[1].x,
                          .y2 = points[1].y});
      }
    }
    return result;
  }

  void replaceWire(idb::IdbNet* value, const std::vector<drt_trace::Shape>& shapes) const
  {
    if (value == nullptr || value->get_wire_list() == nullptr) {
      fail("iDB ordinary net has no wire list");
    }
    value->clear_wire_list();
    if (shapes.empty()) return;

    auto* wire = value->get_wire_list()->add_wire();
    if (wire == nullptr) {
      fail("iDB could not allocate a routed wire");
    }
    wire->set_wire_state(idb::IdbWiringStatement::kRouted);
    for (const auto& shape : shapes) {
      auto* segment = wire->add_segment();
      if (segment == nullptr) {
        fail("iDB could not allocate a routed segment");
      }
      if (shape.kind == "segment") {
        auto* layer = routingLayer(shape.layer);
        if (shape.x1 != shape.x2 && shape.y1 != shape.y2) {
          fail("trace segment is not Manhattan");
        }
        segment->set_layer_name(layer->get_name());
        segment->set_layer(layer);
        segment->add_point(shape.x1, shape.y1);
        segment->add_point(shape.x2, shape.y2);
      } else if (shape.kind == "via") {
        if (shape.via.empty() || shape.x1 != shape.x2 || shape.y1 != shape.y2 || shape.begin_extension || shape.end_extension) {
          fail("trace VIA carries unsupported metadata");
        }
        const auto source = viaPrototype(shape.via, shape.tech_via);
        const std::string bottom = viaBottomLayer(source.via);
        if (bottom != shape.layer) {
          fail("trace VIA layer is not the VIA's canonical iDB bottom routing layer");
        }
        auto* layer = routingLayer(bottom);
        segment->set_layer_name(layer->get_name());
        segment->set_layer(layer);
        segment->add_point(shape.x1, shape.y1);
        segment->set_is_via(true);
        auto* copy = segment->copy_via(source.via);
        if (copy == nullptr) {
          fail("iDB could not clone trace VIA");
        }
        copy->set_coordinate(shape.x1, shape.y1);
      } else if (shape.kind == "patch") {
        auto* layer = routingLayer(shape.layer);
        if (shape.x2 < shape.x1 || shape.y2 < shape.y1) {
          fail("trace patch has an invalid rectangle");
        }
        segment->set_layer_name(layer->get_name());
        segment->set_layer(layer);
        segment->add_point(shape.x1, shape.y1);
        segment->set_is_rect(true);
        segment->set_delta_rect(0, 0, checkedDelta(shape.x2, shape.x1, "trace patch width"),
                                checkedDelta(shape.y2, shape.y1, "trace patch height"));
      } else {
        fail("unsupported iDB route shape '" + shape.kind + "'");
      }
    }
  }

  [[nodiscard]] json::value readTechHeader() const
  {
    auto* units = layout()->get_units();
    if (units == nullptr) {
      fail("iDB import did not retain LEF units");
    }
    // iDB has no USEMINSPACING OBS model. supports() excludes this partial
    // projection from comparisons rather than claiming its default is ODB's.
    return json::array{units->get_micron_dbu(), layout()->get_munufacture_grid(), false};
  }

  [[nodiscard]] bool supportsTechLayers() const
  {
    auto* layers = layout()->get_layers();
    if (layers == nullptr) return false;
    auto* properties = idb::IdbEnum::GetInstance()->get_layer_property();
    if (properties == nullptr) return false;
    for (auto* layer : layers->get_layers()) {
      if (layer == nullptr || layer->get_name().empty() || properties->get_name(layer->get_type()).empty()) return false;
      if (!layer->is_routing()) continue;
      auto* routing = dynamic_cast<idb::IdbLayerRouting*>(layer);
      if (routing == nullptr || layer->get_id() < 0 || properties->get_direction_str(routing->get_direction()).empty()) return false;
    }
    return true;
  }

  [[nodiscard]] json::value readTechLayers() const
  {
    json::array result;
    auto* layers = layout()->get_layers();
    auto* properties = idb::IdbEnum::GetInstance()->get_layer_property();
    if (layers == nullptr || properties == nullptr) fail("iDB import did not retain LEF layer metadata");
    for (auto* layer : layers->get_layers()) {
      if (layer == nullptr) fail("iDB layer list contains null");
      const bool routing_layer = layer->is_routing();
      std::string direction = "NONE";
      int64_t routing_level = 0;
      if (routing_layer) {
        auto* routing = dynamic_cast<idb::IdbLayerRouting*>(layer);
        if (routing == nullptr || layer->get_id() < 0) fail("iDB routing layer has no routing identity");
        direction = properties->get_direction_str(routing->get_direction());
        routing_level = static_cast<int64_t>(layer->get_id()) + 1;
      }
      result.emplace_back(json::array{layer->get_name(),
                                      properties->get_name(layer->get_type()),
                                      static_cast<int64_t>(layer->get_order()),
                                      routing_level,
                                      direction});
    }
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] bool supportsViaRectangles(idb::IdbViaMasterFixed* fixed) const
  {
    if (fixed == nullptr || fixed->get_layer() == nullptr) return false;
    for (auto* rectangle : fixed->get_rect_list()) {
      if (rectangle == nullptr || rectangle->get_low_x() > rectangle->get_high_x()
          || rectangle->get_low_y() > rectangle->get_high_y()) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool supportsViaShape(idb::IdbLayerShape* shape, idb::IdbLayer* expected_layer) const
  {
    if (shape == nullptr || expected_layer == nullptr || shape->get_layer() != expected_layer) return false;
    for (auto* rectangle : shape->get_rect_list()) {
      if (rectangle == nullptr || rectangle->get_low_x() > rectangle->get_high_x()
          || rectangle->get_low_y() > rectangle->get_high_y()) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool supportsVia(idb::IdbVia* via) const
  {
    if (via == nullptr || via->get_name().empty()) return false;
    auto* master = via->get_instance();
    if (master == nullptr) return false;
    if (master->is_fix()) {
      for (auto* fixed : master->get_master_fixed_list()) {
        if (!supportsViaRectangles(fixed)) return false;
      }
      return true;
    }
    if (!master->is_generate()) return false;

    auto* generated = master->get_master_generate();
    if (generated == nullptr || generated->get_rule_name().empty() || generated->get_rule_generate() == nullptr
        || generated->get_layer_bottom() == nullptr || generated->get_layer_cut() == nullptr || generated->get_layer_top() == nullptr
        || generated->get_cut_size_x() <= 0 || generated->get_cut_size_y() <= 0 || generated->get_cut_rows() <= 0
        || generated->get_cut_cols() <= 0) {
      return false;
    }
    return supportsViaShape(master->get_bottom_layer_shape(), generated->get_layer_bottom())
           && supportsViaShape(master->get_cut_layer_shape(), generated->get_layer_cut())
           && supportsViaShape(master->get_top_layer_shape(), generated->get_layer_top());
  }

  [[nodiscard]] bool supportsVias(idb::IdbVias* vias) const
  {
    if (vias == nullptr) return false;
    for (auto* via : vias->get_via_list()) {
      if (!supportsVia(via)) return false;
    }
    return true;
  }

  void appendViaRectangles(json::array& result, idb::IdbViaMasterFixed* fixed) const
  {
    if (fixed == nullptr || fixed->get_layer() == nullptr) fail("iDB fixed VIA has no layer");
    for (auto* rectangle : fixed->get_rect_list()) {
      if (rectangle == nullptr) fail("iDB fixed VIA has a null rectangle");
      result.emplace_back(json::array{fixed->get_layer()->get_name(), rectangle->get_low_x(), rectangle->get_low_y(),
                                      rectangle->get_high_x(), rectangle->get_high_y()});
    }
  }

  void appendViaShape(json::array& result, idb::IdbLayerShape* shape) const
  {
    if (shape == nullptr || shape->get_layer() == nullptr) fail("iDB generated VIA has no layer shape");
    for (auto* rectangle : shape->get_rect_list()) {
      if (rectangle == nullptr) fail("iDB generated VIA has a null rectangle");
      result.emplace_back(json::array{shape->get_layer()->get_name(), rectangle->get_low_x(), rectangle->get_low_y(),
                                      rectangle->get_high_x(), rectangle->get_high_y()});
    }
  }

  [[nodiscard]] json::value idbViaParams(idb::IdbViaMasterGenerate* generated) const
  {
    if (generated == nullptr || generated->get_layer_bottom() == nullptr || generated->get_layer_cut() == nullptr
        || generated->get_layer_top() == nullptr) {
      fail("iDB generated VIA has incomplete layer parameters");
    }
    return json::array{generated->get_layer_bottom()->get_name(),
                       generated->get_layer_cut()->get_name(),
                       generated->get_layer_top()->get_name(),
                       generated->get_cut_size_x(),
                       generated->get_cut_size_y(),
                       generated->get_cut_spcing_x(),
                       generated->get_cut_spcing_y(),
                       generated->get_enclosure_top_x(),
                       generated->get_enclosure_top_y(),
                       generated->get_enclosure_bottom_x(),
                       generated->get_enclosure_bottom_y(),
                       generated->get_cut_rows(),
                       generated->get_cut_cols(),
                       generated->get_original_offset_x(),
                       generated->get_original_offset_y(),
                       generated->get_offset_top_x(),
                       generated->get_offset_top_y(),
                       generated->get_offset_bottom_x(),
                       generated->get_offset_bottom_y()};
  }

  [[nodiscard]] json::value idbViaValue(idb::IdbVia* via) const
  {
    if (via == nullptr || via->get_name().empty()) fail("iDB VIA is null or unnamed");
    auto* master = via->get_instance();
    if (master == nullptr) fail("iDB VIA has no master geometry");
    json::array rectangles;
    if (master->is_fix()) {
      for (auto* fixed : master->get_master_fixed_list()) appendViaRectangles(rectangles, fixed);
      return json::array{via->get_name(),
                         master->is_default(),
                         "rectangles",
                         nullptr,
                         nullptr,
                         drt_trace::canonical(std::move(rectangles))};
    }
    if (!master->is_generate()) fail("iDB VIA has neither fixed nor generated geometry");
    auto* generated = master->get_master_generate();
    if (generated == nullptr || generated->get_rule_name().empty()) fail("iDB generated VIA has no VIARULE name");
    appendViaShape(rectangles, master->get_bottom_layer_shape());
    appendViaShape(rectangles, master->get_cut_layer_shape());
    appendViaShape(rectangles, master->get_top_layer_shape());
    return json::array{via->get_name(),
                       master->is_default(),
                       "generated",
                       generated->get_rule_name(),
                       idbViaParams(generated),
                       drt_trace::canonical(std::move(rectangles))};
  }

  [[nodiscard]] json::value readTechVias() const
  {
    json::array result;
    auto* vias = layout()->get_via_list();
    if (vias == nullptr) fail("iDB import did not retain LEF VIA storage");
    for (auto* via : vias->get_via_list()) result.emplace_back(idbViaValue(via));
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] json::value readDesignVias() const
  {
    json::array result;
    auto* vias = design_->get_via_list();
    if (vias == nullptr) fail("iDB import did not retain DEF VIA storage");
    for (auto* via : vias->get_via_list()) result.emplace_back(idbViaValue(via));
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] bool supportsLibraryMasters() const
  {
    auto* masters = layout()->get_cell_master_list();
    auto* properties = idb::IdbEnum::GetInstance()->get_cell_property();
    if (masters == nullptr || properties == nullptr) return false;
    for (auto* master : masters->get_cell_master()) {
      if (master == nullptr || master->get_name().empty() || properties->get_name(master->get_type()).empty()) return false;
    }
    return true;
  }

  [[nodiscard]] json::value readLibraryMasters() const
  {
    json::array result;
    auto* masters = layout()->get_cell_master_list();
    auto* properties = idb::IdbEnum::GetInstance()->get_cell_property();
    if (masters == nullptr || properties == nullptr) fail("iDB import did not retain LEF master metadata");
    for (auto* master : masters->get_cell_master()) {
      if (master == nullptr) fail("iDB master list contains null");
      result.emplace_back(json::array{master->get_name(),
                                      properties->get_name(master->get_type()),
                                      master->get_origin_x(),
                                      master->get_origin_y(),
                                      master->get_width(),
                                      master->get_height()});
    }
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] bool supportsLibraryTerms() const
  {
    auto* masters = layout()->get_cell_master_list();
    auto* properties = idb::IdbEnum::GetInstance()->get_connect_property();
    if (masters == nullptr || properties == nullptr) return false;
    for (auto* master : masters->get_cell_master()) {
      if (master == nullptr) return false;
      for (auto* term : master->get_term_list()) {
        if (term == nullptr || term->get_name().empty() || properties->get_type_name(term->get_type()).empty()) return false;
      }
    }
    return true;
  }

  [[nodiscard]] json::value readLibraryTerms() const
  {
    json::array result;
    auto* masters = layout()->get_cell_master_list();
    auto* properties = idb::IdbEnum::GetInstance()->get_connect_property();
    if (masters == nullptr || properties == nullptr) fail("iDB import did not retain LEF master terminals");
    for (auto* master : masters->get_cell_master()) {
      if (master == nullptr) fail("iDB master list contains null");
      for (auto* term : master->get_term_list()) {
        if (term == nullptr) fail("iDB master has a null terminal");
        result.emplace_back(json::array{master->get_name(),
                                        term->get_name(),
                                        properties->get_type_name(term->get_type()),
                                        static_cast<int64_t>(term->get_port_list().size())});
      }
    }
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] bool isTechPortVia(idb::IdbVia* via) const
  {
    if (via == nullptr || via->get_name().empty() || via->get_coordinate() == nullptr) return false;
    auto* source = layout()->get_via_list()->find_via(via->get_name());
    return source != nullptr && source->get_instance() == via->get_instance();
  }

  [[nodiscard]] bool supportsLibraryMPinGeometry() const
  {
    auto* masters = layout()->get_cell_master_list();
    if (masters == nullptr) return false;
    for (auto* master : masters->get_cell_master()) {
      if (master == nullptr) return false;
      for (auto* term : master->get_term_list()) {
        if (term == nullptr) return false;
        for (auto* port : term->get_port_list()) {
          if (port == nullptr) return false;
          for (auto* shape : port->get_layer_shape()) {
            if (shape == nullptr || !shape->is_rect() || shape->get_layer() == nullptr) return false;
            for (auto* rectangle : shape->get_rect_list()) {
              if (rectangle == nullptr || rectangle->get_low_x() > rectangle->get_high_x()
                  || rectangle->get_low_y() > rectangle->get_high_y()) {
                return false;
              }
            }
          }
          for (auto* via : port->get_via_list()) {
            if (!isTechPortVia(via)) return false;
          }
        }
      }
    }
    return true;
  }

  [[nodiscard]] json::value readLibraryMPinGeometry() const
  {
    json::array result;
    auto* masters = layout()->get_cell_master_list();
    if (masters == nullptr) fail("iDB import did not retain LEF master PORT geometry");
    for (auto* master : masters->get_cell_master()) {
      if (master == nullptr) fail("iDB master list contains null");
      for (auto* term : master->get_term_list()) {
        if (term == nullptr) fail("iDB master has a null terminal");
        const auto& ports = term->get_port_list();
        for (std::size_t ordinal = 0; ordinal < ports.size(); ++ordinal) {
          auto* port = ports[ordinal];
          if (port == nullptr) fail("iDB master terminal has a null PORT");
          json::array geometry;
          for (auto* shape : port->get_layer_shape()) {
            if (shape == nullptr || shape->get_layer() == nullptr || !shape->is_rect()) {
              fail("iDB master PORT has unsupported non-RECT geometry");
            }
            for (auto* rectangle : shape->get_rect_list()) {
              if (rectangle == nullptr) fail("iDB master PORT has a null rectangle");
              geometry.emplace_back(json::array{"rect", shape->get_layer()->get_name(), rectangle->get_low_x(),
                                                rectangle->get_low_y(), rectangle->get_high_x(), rectangle->get_high_y()});
            }
          }
          for (auto* via : port->get_via_list()) {
            if (!isTechPortVia(via)) fail("iDB master PORT has a non-technology VIA");
            auto* origin = via->get_coordinate();
            geometry.emplace_back(json::array{"tech_via", via->get_name(), origin->get_x(), origin->get_y()});
          }
          result.emplace_back(json::array{master->get_name(),
                                          term->get_name(),
                                          static_cast<int64_t>(ordinal),
                                          drt_trace::canonical(std::move(geometry))});
        }
      }
    }
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] bool supportsBlockages() const
  {
    auto* blockages = design_->get_blockage_list();
    if (blockages == nullptr) return false;
    for (auto* blockage : blockages->get_blockage_list()) {
      if (blockage == nullptr) return false;
      if (!blockage->is_routing_blockage()) continue;
      auto* routing = dynamic_cast<idb::IdbRoutingBlockage*>(blockage);
      if (routing == nullptr || routing->get_layer() == nullptr) return false;
      for (auto* rectangle : routing->get_rect_list()) {
        if (rectangle == nullptr || rectangle->get_low_x() > rectangle->get_high_x()
            || rectangle->get_low_y() > rectangle->get_high_y()) {
          return false;
        }
      }
    }
    return true;
  }

  [[nodiscard]] json::value readBlockages() const
  {
    json::array result;
    auto* blockages = design_->get_blockage_list();
    if (blockages == nullptr) fail("iDB import did not retain DEF blockage storage");
    for (auto* blockage : blockages->get_blockage_list()) {
      if (blockage == nullptr) fail("iDB blockage list contains null");
      if (!blockage->is_routing_blockage()) continue;
      auto* routing = dynamic_cast<idb::IdbRoutingBlockage*>(blockage);
      if (routing == nullptr || routing->get_layer() == nullptr) fail("iDB routing blockage has no routing layer");
      for (auto* rectangle : routing->get_rect_list()) {
        if (rectangle == nullptr) fail("iDB routing blockage has a null rectangle");
        result.emplace_back(json::array{routing->get_layer()->get_name(), rectangle->get_low_x(), rectangle->get_low_y(),
                                        rectangle->get_high_x(), rectangle->get_high_y()});
      }
    }
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] bool supportsBterms(const drt_trace::Event& event) const
  {
    if (!event.expected.is_array()) return false;
    // iDB retains transformed DEF port boxes on IdbPin, but does not retain
    // the grouping of multiple BPin objects under one BTerm.
    for (const auto& expected : event.expected.as_array()) {
      if (!expected.is_array() || expected.as_array().size() != 5u || !expected.as_array()[4].is_array()
          || expected.as_array()[4].as_array().size() != 1u) {
        return false;
      }
    }
    auto* pins = design_->get_io_pin_list();
    auto* properties = idb::IdbEnum::GetInstance()->get_connect_property();
    if (pins == nullptr || properties == nullptr) return false;
    for (auto* pin : pins->get_pin_list()) {
      if (pin == nullptr || pin->get_term() == nullptr || pin->get_pin_name().empty()) return false;
      auto* term = pin->get_term();
      if (properties->get_type_name(term->get_type()).empty() || properties->get_direction_name(term->get_direction()).empty()) return false;
      for (auto* shape : pin->get_port_box_list()) {
        if (shape == nullptr || !shape->is_rect() || shape->get_layer() == nullptr) return false;
        for (auto* rectangle : shape->get_rect_list()) {
          if (rectangle == nullptr || rectangle->get_low_x() > rectangle->get_high_x()
              || rectangle->get_low_y() > rectangle->get_high_y()) {
            return false;
          }
        }
      }
    }
    return true;
  }

  [[nodiscard]] json::value readBterms() const
  {
    json::array result;
    auto* pins = design_->get_io_pin_list();
    auto* properties = idb::IdbEnum::GetInstance()->get_connect_property();
    if (pins == nullptr || properties == nullptr) fail("iDB import did not retain DEF BTerms");
    for (auto* pin : pins->get_pin_list()) {
      if (pin == nullptr || pin->get_term() == nullptr) fail("iDB BTerm has no terminal definition");
      auto* term = pin->get_term();
      json::array boxes;
      for (auto* shape : pin->get_port_box_list()) {
        if (shape == nullptr || shape->get_layer() == nullptr || !shape->is_rect()) {
          fail("iDB BTerm has unsupported non-RECT PORT geometry");
        }
        for (auto* rectangle : shape->get_rect_list()) {
          if (rectangle == nullptr) fail("iDB BTerm PORT has a null rectangle");
          boxes.emplace_back(json::array{shape->get_layer()->get_name(), rectangle->get_low_x(), rectangle->get_low_y(),
                                         rectangle->get_high_x(), rectangle->get_high_y()});
        }
      }
      json::array ports;
      ports.emplace_back(drt_trace::canonical(std::move(boxes)));
      const json::value net_name = pin->get_net_name().empty() ? json::value(nullptr) : json::value(pin->get_net_name());
      result.emplace_back(json::array{pin->get_pin_name(),
                                      properties->get_type_name(term->get_type()),
                                      properties->get_direction_name(term->get_direction()),
                                      net_name,
                                      drt_trace::canonical(std::move(ports))});
    }
    return drt_trace::canonical(std::move(result));
  }

  [[nodiscard]] json::value readDie() const
  {
    auto* units = design_->get_units();
    auto* die = layout()->get_die();
    auto* bounds = die == nullptr ? nullptr : die->get_bounding_box();
    if (units == nullptr || bounds == nullptr) {
      fail("iDB import did not retain DEF units or DIEAREA");
    }
    return json::array{units->get_micron_dbu(),
                       bounds->get_low_x(),
                       bounds->get_low_y(),
                       bounds->get_high_x(),
                       bounds->get_high_y()};
  }

  [[nodiscard]] json::value scanTracks() const
  {
    auto* grids = layout()->get_track_grid_list();
    if (grids == nullptr) {
      fail("iDB import did not retain DEF TRACKS storage");
    }
    json::array result;
    for (auto* grid : grids->get_track_grid_list()) {
      if (grid == nullptr || grid->get_track() == nullptr) {
        fail("iDB track grid has no track definition");
      }
      const auto* track = grid->get_track();
      const auto count = checkedTraceInt(grid->get_track_num(), "iDB TRACKS count");
      const auto start = checkedTraceInt(track->get_start(), "iDB TRACKS start");
      const auto step = checkedTraceInt(track->get_pitch(), "iDB TRACKS step");
      for (auto* layer : grid->get_layer_list()) {
        if (layer == nullptr) {
          fail("iDB track grid references a null layer");
        }
        // EccDB's DesignTrackGrid and ODB's dbTrackGrid projection are both
        // routing-layer only. iDB's DEF reader may retain a non-routing name
        // in the source clause, so it must not leak into this common contract.
        if (!layer->is_routing()) {
          continue;
        }
        result.emplace_back(json::array{layer->get_name(), axisName(track->get_direction()), start, count, step});
      }
    }
    return result;
  }

  [[nodiscard]] json::value readInstancePins(uint32_t token) const
  {
    auto* pins = instance(token)->get_pin_list();
    if (pins == nullptr) {
      fail("iDB instance has no terminal list");
    }
    json::array result;
    for (auto* pin : pins->get_pin_list()) {
      if (pin == nullptr || pin->get_term() == nullptr) {
        fail("iDB instance pin has no library master terminal");
      }
      result.emplace_back(pin->get_term_name());
    }
    return result;
  }

  [[nodiscard]] json::value readNetAttributes(uint32_t token) const
  {
    auto* value = net(token);
    // iDB has signal USE but not a retained NETS NONDEFAULTRULE, ODB's
    // schema-gated jumper flag, or isConnectedByAbutment. supports() marks
    // this deliberately partial result unsupported.
    return json::array{signalUseName(value->get_connect_type()), json::value(nullptr), false, false};
  }

  [[nodiscard]] json::value readGCellGrid() const
  {
    auto* grids = layout()->get_gcell_grid_list();
    if (grids == nullptr) {
      fail("iDB import did not retain DEF GCELLGRID storage");
    }
    const idb::IdbGCellGrid* x_grid = nullptr;
    const idb::IdbGCellGrid* y_grid = nullptr;
    bool multiple_patterns = false;
    for (auto* grid : grids->get_gcell_grid_list()) {
      if (grid == nullptr) {
        fail("iDB GCell grid list contains null");
      }
      const auto*& destination = grid->get_direction() == idb::IdbTrackDirection::kDirectionX ? x_grid : y_grid;
      if (destination != nullptr) {
        multiple_patterns = true;
        continue;
      }
      destination = grid;
    }
    if (multiple_patterns || x_grid == nullptr || y_grid == nullptr) {
      return json::value(nullptr);
    }
    return json::array{x_grid->get_start(),
                       x_grid->get_num(),
                       x_grid->get_space(),
                       y_grid->get_start(),
                       y_grid->get_num(),
                       y_grid->get_space()};
  }

  [[nodiscard]] json::value scanInstances() const
  {
    if (design_->get_instance_list() == nullptr) {
      fail("iDB import did not create instance storage");
    }
    json::array result;
    for (auto* value : design_->get_instance_list()->get_instance_list()) {
      if (value == nullptr) fail("iDB instance list contains null");
      result.emplace_back(traceName(value));
    }
    return result;
  }

  [[nodiscard]] json::value scanNets() const
  {
    if (design_->get_net_list() == nullptr) {
      fail("iDB import did not create ordinary-net storage");
    }
    json::array result;
    for (auto* value : design_->get_net_list()->get_net_list()) {
      if (value == nullptr) fail("iDB ordinary-net list contains null");
      result.emplace_back(traceName(value));
    }
    return result;
  }

  [[nodiscard]] json::value readInstance(uint32_t token) const
  {
    auto* value = instance(token);
    auto* master = value->get_cell_master();
    auto* coordinate = value->get_coordinate();
    if (master == nullptr || coordinate == nullptr) {
      fail("iDB instance is missing its master or DEF placement coordinate");
    }
    return json::array{traceName(value), master->get_name(), coordinate->get_x(), coordinate->get_y(),
                       orientationName(value->get_orient())};
  }

  [[nodiscard]] json::value netPins(uint32_t token) const
  {
    auto* value = net(token);
    if (value->get_instance_pin_list() == nullptr || value->get_io_pins() == nullptr) {
      fail("iDB ordinary net is missing pin-reference storage");
    }
    json::array result;
    for (auto* pin : value->get_instance_pin_list()->get_pin_list()) {
      if (pin == nullptr || pin->get_instance() == nullptr || pin->get_term() == nullptr) {
        fail("iDB ordinary net has an invalid instance pin");
      }
      result.emplace_back(json::array{"I", traceName(pin->get_instance()), pin->get_term_name()});
    }
    for (auto* pin : value->get_io_pins()->get_pin_list()) {
      if (pin == nullptr) {
        fail("iDB ordinary net has an invalid IO pin");
      }
      result.emplace_back(json::array{"B", pin->get_pin_name(), ""});
    }
    return result;
  }

  [[nodiscard]] json::value findInstance(std::string_view name) const
  {
    if (design_->get_instance_list() == nullptr) {
      fail("iDB import did not create instance storage");
    }
    auto* value = design_->get_instance_list()->find_instance(std::string(name));
    if (value == nullptr) {
      const auto normalized = unescapeDefIdentifier(name);
      if (normalized != name) value = design_->get_instance_list()->find_instance(normalized);
    }
    if (value == nullptr) return json::value(nullptr);
    return json::value(traceName(value));
  }

  [[nodiscard]] json::value findPin(uint32_t token, std::string_view name) const
  {
    auto* value = instance(token);
    auto* pin = value->get_pin(std::string(name));
    if (pin == nullptr) pin = value->get_pin_by_term(std::string(name));
    if (pin == nullptr) return json::value(nullptr);
    if (pin->get_term() == nullptr) {
      fail("iDB instance pin has no master terminal");
    }
    return json::value(pin->get_term_name());
  }

  idb::IdbBuilder builder_;
  idb::IdbDesign* design_ = nullptr;
  std::vector<idb::IdbInstance*> instances_;
  std::vector<idb::IdbNet*> nets_;
  std::unordered_map<const idb::IdbInstance*, std::string> instance_trace_names_;
  std::unordered_map<const idb::IdbNet*, std::string> net_trace_names_;
  mutable std::unordered_set<uint32_t> unavailable_wire_state_;
};
}  // namespace

int main(int argc, char** argv)
{
  try {
    auto options = drt_trace::options(argc, argv);
    drt_trace::TraceReader trace(options.trace);
    if (options.backend.empty()) options.backend = "eccdb";

    if (options.backend == "eccdb") {
      EccdbBackend backend(options, trace.header());
      drt_trace::replayStream(backend, trace, options);
    } else if (options.backend == "idb") {
      if (options.profile_wire_write) {
        fail("--profile-wire-write is supported only with --backend eccdb");
      }
      IdbBackend backend(options, trace.header());
      drt_trace::replayStream(backend, trace, options);
    } else {
      fail("--backend must be eccdb or idb");
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "drt trace replay: " << error.what() << '\n';
    return 1;
  }
}
