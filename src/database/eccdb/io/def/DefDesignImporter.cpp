#include "def/DefDesignImporter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <deque>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "defiBlockage.hpp"
#include "defiComponent.hpp"
#include "defiFill.hpp"
#include "defiGroup.hpp"
#include "defiNet.hpp"
#include "defiNonDefault.hpp"
#include "defiPath.hpp"
#include "defiPinCap.hpp"
#include "defiRegion.hpp"
#include "defiRowTrack.hpp"
#include "defiVia.hpp"
#include "defrReader.hpp"
#include "design/constraint/model/ConstraintComponents.h"
#include "design/fill/model/FillComponents.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/non_default_rule/component/NonDefaultRuleComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/routing/pool/WireRoutingInput.h"
#include "design/via/component/ViaComponents.h"
#include "def/detail/DefParserMutex.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "library/site/model/SiteComponents.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/cut_layer/model/CutLayerComponents.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/via_master/model/ViaMasterComponents.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace eccdb {
namespace {

using namespace LefDefParser;

struct GroupContext
{
  std::string name;
  std::string region;
  std::vector<std::string> member_patterns;
};

// Direct DEF import context.  It contains only references to objects that
// have already been created in the EnTT database plus unresolved references
// whose DEF section may legally appear later in the file.  Parsed DEF records
// are never retained here.
struct DirectContext
{
  explicit DirectContext(DesignStore& database) : design(database) {}

  DesignStore& design;
  std::string design_name;
  int32_t database_units_per_micron = 0;
  std::string divider_character = "/";
  std::string bus_bit_characters = "[]";
  std::unordered_map<std::string, TechLayerId> layers;
  std::unordered_map<std::string, TechViaMasterId> tech_vias;
  std::unordered_map<std::string, TechViaRuleGenerateId> via_rules;
  std::unordered_map<std::string, TechNonDefaultRuleId> tech_ndrs;
  std::unordered_map<std::string, LibrarySiteId> sites;
  std::unordered_map<std::string, LibraryCellMasterId> masters;

  struct PendingConnection
  {
    DesignNetId net;
    bool special = false;
    std::string instance;
    std::string pin;
  };
  struct PendingPinNet
  {
    DesignIoPinId pin;
    std::string net;
  };
  struct PendingRegion
  {
    DesignInstanceId instance;
    std::string name;
  };
  struct PendingGroupRegion
  {
    DesignGroupId group;
    std::string name;
  };
  std::vector<PendingConnection> pending_connections;
  std::vector<PendingPinNet> pending_pin_nets;
  std::vector<PendingRegion> pending_regions;
  std::vector<PendingGroupRegion> pending_group_regions;
  std::optional<GroupContext> current_group;
  std::unordered_map<std::string, std::size_t> diagnostics;
  std::exception_ptr callback_failure;
  std::size_t must_join_index = 0;
};

bool isPinConnection(std::string_view value)
{
  return value.size() == 3u && std::toupper(static_cast<unsigned char>(value[0])) == 'P'
         && std::toupper(static_cast<unsigned char>(value[1])) == 'I'
         && std::toupper(static_cast<unsigned char>(value[2])) == 'N';
}

std::string upper(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
  return value;
}

std::string requiredText(const char* value, std::string_view field)
{
  if (value == nullptr || *value == '\0') {
    throw std::runtime_error(std::string(field) + " is required");
  }
  return value;
}

int32_t exactInt32(double value, const char* field)
{
  if (!std::isfinite(value) || std::floor(value) != value || value < static_cast<double>(std::numeric_limits<int32_t>::min())
      || value > static_cast<double>(std::numeric_limits<int32_t>::max())) {
    throw std::runtime_error(std::string(field) + " must be an int32 value");
  }
  return static_cast<int32_t>(value);
}

uint32_t exactUint32(double value, const char* field)
{
  if (!std::isfinite(value) || std::floor(value) != value || value < 0.0
      || value > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
    throw std::runtime_error(std::string(field) + " must be a uint32 value");
  }
  return static_cast<uint32_t>(value);
}

uint32_t exactSizeUint32(std::size_t value, const char* field)
{
  if (value > std::numeric_limits<uint32_t>::max()) {
    throw std::runtime_error(std::string(field) + " exceeds uint32_t");
  }
  return static_cast<uint32_t>(value);
}

DesignPropertyType propertyType(char parser_type, std::string_view value)
{
  if (parser_type == 'I') {
    return DesignPropertyType::kInteger;
  }
  if (parser_type == 'R') {
    return DesignPropertyType::kReal;
  }
  if (parser_type == 'S') {
    return DesignPropertyType::kString;
  }
  if (value.find_first_of(".eE") != std::string_view::npos) {
    return DesignPropertyType::kReal;
  }
  const std::size_t first_digit = !value.empty() && (value.front() == '+' || value.front() == '-') ? 1u : 0u;
  const bool integer_text
      = first_digit < value.size()
        && std::all_of(value.begin() + first_digit, value.end(), [](unsigned char character) { return std::isdigit(character) != 0; });
  return integer_text ? DesignPropertyType::kInteger : DesignPropertyType::kString;
}

DesignOrientation orientation(const char* value)
{
  const auto name = upper(requiredText(value, "DEF orientation"));
  if (name == "N") {
    return DesignOrientation::kN;
  }
  if (name == "S") {
    return DesignOrientation::kS;
  }
  if (name == "E") {
    return DesignOrientation::kE;
  }
  if (name == "W") {
    return DesignOrientation::kW;
  }
  if (name == "FN") {
    return DesignOrientation::kFN;
  }
  if (name == "FS") {
    return DesignOrientation::kFS;
  }
  if (name == "FE") {
    return DesignOrientation::kFE;
  }
  if (name == "FW") {
    return DesignOrientation::kFW;
  }
  throw std::runtime_error("unsupported DEF orientation: " + name);
}

DesignAxis axis(const char* value)
{
  const auto name = upper(requiredText(value, "DEF axis"));
  if (name == "X") {
    return DesignAxis::kX;
  }
  if (name == "Y") {
    return DesignAxis::kY;
  }
  throw std::runtime_error("unsupported DEF axis: " + name);
}

DesignPlacementStatus componentPlacement(const defiComponent& source)
{
  if (source.isPlaced()) {
    return DesignPlacementStatus::kPlaced;
  }
  if (source.isFixed()) {
    return DesignPlacementStatus::kFixed;
  }
  if (source.isCover()) {
    return DesignPlacementStatus::kCover;
  }
  return DesignPlacementStatus::kUnplaced;
}

DesignPlacementStatus pinPlacement(const defiPin& source)
{
  if (source.isPlaced()) {
    return DesignPlacementStatus::kPlaced;
  }
  if (source.isFixed()) {
    return DesignPlacementStatus::kFixed;
  }
  if (source.isCover()) {
    return DesignPlacementStatus::kCover;
  }
  return DesignPlacementStatus::kUnplaced;
}

DesignPlacementStatus portPlacement(const defiPinPort& source)
{
  if (source.isPlaced()) {
    return DesignPlacementStatus::kPlaced;
  }
  if (source.isFixed()) {
    return DesignPlacementStatus::kFixed;
  }
  if (source.isCover()) {
    return DesignPlacementStatus::kCover;
  }
  return DesignPlacementStatus::kUnplaced;
}

DesignInstanceSource instanceSource(const char* value)
{
  const auto name = upper(requiredText(value, "COMPONENT SOURCE"));
  if (name == "NETLIST") {
    return DesignInstanceSource::kNetlist;
  }
  if (name == "DIST") {
    return DesignInstanceSource::kDist;
  }
  if (name == "USER") {
    return DesignInstanceSource::kUser;
  }
  if (name == "TIMING") {
    return DesignInstanceSource::kTiming;
  }
  throw std::runtime_error("unsupported COMPONENT SOURCE: " + name);
}

DesignSignalUse signalUse(const char* value)
{
  const auto name = upper(requiredText(value, "DEF USE"));
  if (name == "SIGNAL" || name == "DATA") {
    return DesignSignalUse::kSignal;
  }
  if (name == "ANALOG") {
    return DesignSignalUse::kAnalog;
  }
  if (name == "POWER") {
    return DesignSignalUse::kPower;
  }
  if (name == "GROUND") {
    return DesignSignalUse::kGround;
  }
  if (name == "CLOCK") {
    return DesignSignalUse::kClock;
  }
  if (name == "TIEOFF") {
    return DesignSignalUse::kTieOff;
  }
  if (name == "SCAN") {
    return DesignSignalUse::kScan;
  }
  if (name == "RESET") {
    return DesignSignalUse::kReset;
  }
  throw std::runtime_error("unsupported DEF USE: " + name);
}

DesignIoPinDirection pinDirection(const char* value)
{
  const auto name = upper(requiredText(value, "PIN DIRECTION"));
  if (name == "INPUT") {
    return DesignIoPinDirection::kInput;
  }
  if (name == "OUTPUT") {
    return DesignIoPinDirection::kOutput;
  }
  if (name == "INOUT") {
    return DesignIoPinDirection::kInOut;
  }
  if (name == "FEEDTHRU") {
    return DesignIoPinDirection::kFeedThru;
  }
  throw std::runtime_error("unsupported PIN DIRECTION: " + name);
}

DesignNetPattern netPattern(const char* value)
{
  const auto name = upper(requiredText(value, "NET PATTERN"));
  if (name == "BALANCED") {
    return DesignNetPattern::kBalanced;
  }
  if (name == "STEINER") {
    return DesignNetPattern::kSteiner;
  }
  if (name == "TRUNK") {
    return DesignNetPattern::kTrunk;
  }
  if (name == "WIREDLOGIC") {
    return DesignNetPattern::kWiredLogic;
  }
  throw std::runtime_error("unsupported NET PATTERN: " + name);
}

DesignWireStatus wireStatus(const char* value)
{
  const auto name = upper(requiredText(value, "wire status"));
  if (name == "ROUTED") {
    return DesignWireStatus::kRouted;
  }
  if (name == "FIXED") {
    return DesignWireStatus::kFixed;
  }
  if (name == "COVER") {
    return DesignWireStatus::kCover;
  }
  if (name == "SHIELD") {
    return DesignWireStatus::kShield;
  }
  if (name == "NOSHIELD") {
    return DesignWireStatus::kNoShield;
  }
  throw std::runtime_error("unsupported wire status: " + name);
}

DesignRegionType regionType(const char* value)
{
  const auto name = upper(requiredText(value, "REGION TYPE"));
  if (name == "FENCE") {
    return DesignRegionType::kFence;
  }
  if (name == "GUIDE") {
    return DesignRegionType::kGuide;
  }
  throw std::runtime_error("unsupported REGION TYPE: " + name);
}

std::vector<Point> points(const defiPoints& source, std::string_view field, int minimum_count = 3)
{
  if (source.numPoints < minimum_count || source.x == nullptr || source.y == nullptr) {
    throw std::runtime_error(std::string(field) + " has too few points");
  }
  std::vector<Point> result;
  result.reserve(static_cast<std::size_t>(source.numPoints));
  for (int index = 0; index < source.numPoints; ++index) {
    result.push_back(Point{source.x[index], source.y[index]});
  }
  return result;
}

TechRoutingLayerId directRoutingLayer(const DirectContext& context, std::string_view name, std::string_view field);

void resolveWireVia(DirectContext& context, DesignWireVia& via, std::string_view name)
{
  const auto local_via = context.design.routingStorage().findVia(name);
  const auto tech_via = context.tech_vias.find(std::string{name});
  if (static_cast<bool>(local_via) == (tech_via != context.tech_vias.end())) {
    throw std::runtime_error("wire VIA name must resolve to exactly one VIA: " + std::string{name});
  }
  if (local_via) {
    via.design_via = local_via;
  } else {
    via.tech_via = tech_via->second;
  }
}

void parsePath(DirectContext& context, const defiPath& source, DesignWireRoutingInput& routing)
{
  const auto old_path_count = routing.path_records.size();
  const auto old_point_count = routing.points.size();
  const auto old_via_count = routing.vias.size();
  const auto old_rectangle_count = routing.rectangles.size();
  const auto old_extra_count = routing.extras.size();
  DesignWireInputPathRecord record;
  DesignWirePathExtra extra;
  std::optional<std::array<uint32_t, 3>> pending_via_mask;
  try {
    source.initTraverse();
    while (const auto token = source.next()) {
      switch (token) {
        case DEFIPATH_LAYER:
          if (record.layer) {
            throw std::runtime_error("one SI2 path contains multiple layers; expected NEW to create a new path");
          }
          record.layer = directRoutingLayer(context, requiredText(source.getLayer(), "wire path layer"), "NET wire");
          break;
        case DEFIPATH_WIDTH:
          record.flags |= DesignWirePathFlag::kHasWidth;
          extra.width = source.getWidth();
          break;
        case DEFIPATH_MASK:
          record.flags |= DesignWirePathFlag::kHasMask;
          extra.mask = static_cast<uint32_t>(source.getMask());
          break;
        case DEFIPATH_TAPER:
          record.flags |= DesignWirePathFlag::kTaper;
          break;
        case DEFIPATH_TAPERRULE:
          record.flags |= DesignWirePathFlag::kHasTaperRule;
          extra.taper_rule = requiredText(source.getTaperRule(), "wire TAPERRULE");
          break;
        case DEFIPATH_SHAPE:
          record.flags |= DesignWirePathFlag::kHasShape;
          extra.shape = requiredText(source.getShape(), "special wire SHAPE");
          break;
        case DEFIPATH_STYLE:
          record.flags |= DesignWirePathFlag::kHasStyle;
          extra.style = source.getStyle();
          break;
        case DEFIPATH_POINT: {
          int x = 0;
          int y = 0;
          source.getPoint(&x, &y);
          routing.points.push_back(DesignWirePoint{.position = {x, y}});
          break;
        }
        case DEFIPATH_FLUSHPOINT: {
          int x = 0;
          int y = 0;
          int extension = 0;
          source.getFlushPoint(&x, &y, &extension);
          routing.points.push_back(
              DesignWirePoint{.position = {x, y}, .flags = DesignWirePointFlag::kHasExtension, .extension = extension});
          break;
        }
        case DEFIPATH_VIRTUALPOINT: {
          int x = 0;
          int y = 0;
          source.getVirtualPoint(&x, &y);
          routing.points.push_back(DesignWirePoint{.position = {x, y}, .flags = DesignWirePointFlag::kVirtual});
          break;
        }
        case DEFIPATH_VIAMASK:
          pending_via_mask = std::array<uint32_t, 3>{static_cast<uint32_t>(source.getViaTopMask()),
                                                     static_cast<uint32_t>(source.getViaCutMask()),
                                                     static_cast<uint32_t>(source.getViaBottomMask())};
          break;
        case DEFIPATH_VIA: {
          if (routing.points.size() == old_point_count) {
            throw std::runtime_error("wire VIA requires a preceding point");
          }
          DesignWireVia via{.point_index = exactSizeUint32(routing.points.size() - old_point_count - 1u, "wire VIA point index")};
          resolveWireVia(context, via, requiredText(source.getVia(), "wire VIA"));
          if (pending_via_mask) {
            via.flags |= DesignWireViaFlag::kHasMask;
            via.top_mask = (*pending_via_mask)[0];
            via.cut_mask = (*pending_via_mask)[1];
            via.bottom_mask = (*pending_via_mask)[2];
            pending_via_mask.reset();
          }
          routing.vias.push_back(via);
          break;
        }
        case DEFIPATH_VIAROTATION:
          if (routing.vias.size() == old_via_count) {
            throw std::runtime_error("wire VIA orientation appears before VIA");
          }
          routing.vias.back().orientation = orientation(source.getViaRotationStr());
          break;
        case DEFIPATH_VIADATA: {
          if (routing.vias.size() == old_via_count) {
            throw std::runtime_error("wire VIA array appears before VIA");
          }
          int columns = 0;
          int rows = 0;
          int step_x = 0;
          int step_y = 0;
          source.getViaData(&columns, &rows, &step_x, &step_y);
          auto& via = routing.vias.back();
          via.flags |= DesignWireViaFlag::kHasArray;
          via.columns = static_cast<uint32_t>(columns);
          via.rows = static_cast<uint32_t>(rows);
          via.step_x = step_x;
          via.step_y = step_y;
          break;
        }
        case DEFIPATH_RECT: {
          if (routing.points.size() == old_point_count) {
            throw std::runtime_error("wire RECT requires a preceding point");
          }
          int ll_x = 0;
          int ll_y = 0;
          int ur_x = 0;
          int ur_y = 0;
          source.getViaRect(&ll_x, &ll_y, &ur_x, &ur_y);
          routing.rectangles.push_back(
              DesignWireRectangle{.point_index
                                  = exactSizeUint32(routing.points.size() - old_point_count - 1u, "wire RECT point index"),
                                  .delta = Rect{ll_x, ll_y, ur_x, ur_y}.normalized()});
          break;
        }
        default:
          throw std::runtime_error("unsupported SI2 wire path token");
      }
    }
    if (pending_via_mask) {
      throw std::runtime_error("wire VIA MASK is not followed by a VIA");
    }
    if (!record.layer) {
      throw std::runtime_error("wire path requires a layer");
    }

    record.point_end.value = exactSizeUint32(routing.points.size(), "wire point pool");
    record.via_end.value = exactSizeUint32(routing.vias.size(), "wire VIA pool");
    record.rectangle_end.value = exactSizeUint32(routing.rectangles.size(), "wire RECT pool");
    constexpr uint32_t kValueFlags = DesignWirePathFlag::kHasWidth | DesignWirePathFlag::kHasMask
                                     | DesignWirePathFlag::kHasTaperRule | DesignWirePathFlag::kHasShape
                                     | DesignWirePathFlag::kHasStyle;
    if ((record.flags & kValueFlags) != 0u) {
      record.extra_index = exactSizeUint32(routing.extras.size(), "wire path extra pool");
      routing.extras.push_back(std::move(extra));
    }
    routing.path_records.push_back(record);
  } catch (...) {
    routing.path_records.resize(old_path_count);
    routing.points.resize(old_point_count);
    routing.vias.resize(old_via_count);
    routing.rectangles.resize(old_rectangle_count);
    routing.extras.resize(old_extra_count);
    throw;
  }
}

struct ParsedWire
{
  DesignWire component;
  DesignWireRoutingInput routing;
};

ParsedWire parseWire(DirectContext& context, const defiWire& source)
{
  ParsedWire target{.component = DesignWire{.status = wireStatus(source.wireType())}};
  if (target.component.status == DesignWireStatus::kShield) {
    target.component.shield_net = requiredText(source.wireShieldNetName(), "SHIELD net name");
  }
  target.routing.reservePaths(static_cast<std::size_t>(source.numPaths()));
  for (int index = 0; index < source.numPaths(); ++index) {
    const auto* path = source.path(index);
    if (path == nullptr) {
      throw std::runtime_error("SI2 returned a null wire path");
    }
    parsePath(context, *path, target.routing);
  }
  return target;
}

struct FileCloser
{
  void operator()(FILE* file) const noexcept
  {
    if (file != nullptr) {
      static_cast<void>(std::fclose(file));
    }
  }
};

template <typename Id, typename Component>
std::unordered_map<std::string, Id> componentNameMap(const auto& registry)
{
  std::unordered_map<std::string, Id> result;
  const auto view = registry.template view<const Component>();
  for (const auto entity : view) {
    result.emplace(view.template get<const Component>(entity).name, Id{entity});
  }
  return result;
}

bool globMatches(std::string_view pattern, std::string_view value)
{
  std::size_t pattern_index = 0;
  std::size_t value_index = 0;
  std::size_t star = std::string_view::npos;
  std::size_t restart = 0;
  while (value_index < value.size()) {
    if (pattern_index < pattern.size() && (pattern[pattern_index] == '?' || pattern[pattern_index] == value[value_index])) {
      ++pattern_index;
      ++value_index;
    } else if (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
      star = pattern_index++;
      restart = value_index;
    } else if (star != std::string_view::npos) {
      pattern_index = star + 1u;
      value_index = ++restart;
    } else {
      return false;
    }
  }
  while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
    ++pattern_index;
  }
  return pattern_index == pattern.size();
}

void ensureEmptyTarget(const DesignStore& design)
{
  if (design.globalStorage().hasInfo() || design.globalStorage().hasDieArea() || design.floorplanStorage().hasCoreArea()
      || design.floorplanStorage().rowCount() != 0u || design.floorplanStorage().trackGridCount() != 0u
      || design.floorplanStorage().gcellGridCount() != 0u || design.netlistStorage().instanceCount() != 0u
      || design.netlistStorage().ioPinCount() != 0u || design.netlistStorage().netCount() != 0u || design.routingStorage().viaCount() != 0u
      || design.routingStorage().nonDefaultRuleCount() != 0u || design.routingStorage().wireCount() != 0u
      || design.constraintStorage().regionCount() != 0u
      || design.constraintStorage().groupCount() != 0u || design.constraintStorage().blockageCount() != 0u
      || design.fillStorage().fillCount() != 0u) {
    throw std::logic_error("direct DEF importer requires an empty DesignStore");
  }
}

template <typename Component>
void appendEntities(const DesignRegistry::registry_type& registry, std::vector<DesignEntity>& entities)
{
  const auto view = registry.view<const Component>();
  for (const auto entity : view) {
    entities.push_back(entity);
  }
}

void rollbackImport(DesignStore& design) noexcept
{
  try {
    auto& registry = design.designRegistry().registry();
    std::vector<DesignEntity> entities;
    appendEntities<DesignRow>(registry, entities);
    appendEntities<DesignTrackGrid>(registry, entities);
    appendEntities<DesignGCellGrid>(registry, entities);
    appendEntities<DesignVia>(registry, entities);
    appendEntities<DesignNonDefaultRule>(registry, entities);
    appendEntities<DesignInstance>(registry, entities);
    appendEntities<DesignInstancePin>(registry, entities);
    appendEntities<DesignIoPin>(registry, entities);
    appendEntities<DesignNet>(registry, entities);
    appendEntities<DesignWire>(registry, entities);
    appendEntities<DesignRegion>(registry, entities);
    appendEntities<DesignGroup>(registry, entities);
    appendEntities<DesignBlockage>(registry, entities);
    appendEntities<DesignFill>(registry, entities);
    std::sort(entities.begin(), entities.end(),
              [](DesignEntity lhs, DesignEntity rhs) { return entt::to_integral(lhs) < entt::to_integral(rhs); });
    entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
    for (const auto entity : entities) {
      if (entity != design.rootId().entity() && registry.valid(entity)) {
        registry.destroy(entity);
      }
    }
    if (registry.valid(design.rootId().entity())) {
      registry.remove<DesignInfo>(design.rootId().entity());
      registry.remove<DesignDieArea>(design.rootId().entity());
    }
    design.netlistStorage().rebuildNameIndexes();
    design.routingStorage().rebuildWireIndex();
    design.routingStorage().clearRoutingPool();
  } catch (...) {
  }
}

void directDiagnostic(DirectContext& context, std::string_view statement, std::size_t count = 1)
{
  if (count != 0u) context.diagnostics[std::string{statement}] += count;
}

template <typename Function>
int directGuardedCallback(defiUserData user_data, Function&& function) noexcept
{
  auto* context = static_cast<DirectContext*>(user_data);
  if (context == nullptr) {
    return 1;
  }
  try {
    function(*context);
    return 0;
  } catch (...) {
    try {
      throw;
    } catch (const std::exception& error) {
      std::fprintf(stderr, "[DEF import] direct callback failed: %s\n", error.what());
    } catch (...) {
      std::fprintf(stderr, "[DEF import] direct callback failed: unknown exception\n");
    }
    context->callback_failure = std::current_exception();
    return 1;
  }
}

void setDirectInfo(DirectContext& context)
{
  if (!context.design_name.empty() && context.database_units_per_micron > 0) {
    context.design.globalStorage().setInfo(DesignInfo{.name = context.design_name,
                                                       .database_units_per_micron = context.database_units_per_micron,
                                                       .divider_character = context.divider_character,
                                                       .bus_bit_characters = context.bus_bit_characters});
  }
}

TechRoutingLayerId directRoutingLayer(const DirectContext& context, std::string_view name, std::string_view field)
{
  const auto found = context.layers.find(std::string{name});
  if (found == context.layers.end()
      || !context.design.techRegistry().registry().all_of<TechRoutingLayer>(found->second.entity())) {
    throw std::runtime_error(std::string(field) + " references an unknown routing layer: " + std::string{name});
  }
  return TechRoutingLayerId{found->second.entity()};
}

void directConnectIoPin(DirectContext& context, DesignIoPinId pin_id, DesignNetId net_id, bool special)
{
  auto& netlist = context.design.netlistStorage();
  const auto& pin = netlist.ioPin(pin_id);
  const auto previous = special ? pin.special_net : pin.net;
  if (previous && previous != net_id) {
    netlist.disconnect(pin_id, previous);
    context.diagnostics["DUPLICATE NET CONNECTION"] += 1;
  }
  netlist.connect(pin_id, net_id);
}

void directConnectInstancePin(DirectContext& context, DesignInstancePinId pin_id, DesignNetId net_id, bool special)
{
  auto& netlist = context.design.netlistStorage();
  const auto& pin = netlist.instancePin(pin_id);
  const auto previous = special ? pin.special_net : pin.net;
  if (previous && previous != net_id) {
    netlist.disconnect(pin_id, previous);
    context.diagnostics["DUPLICATE NET CONNECTION"] += 1;
  }
  netlist.connect(pin_id, net_id);
}

void directConnect(DirectContext& context, DesignNetId net_id, bool special, std::string instance, std::string pin)
{
  auto& netlist = context.design.netlistStorage();
  if (isPinConnection(instance)) {
    const auto pin_name = instance == "PIN" ? pin : instance;
    const auto pin_id = netlist.findIoPin(pin_name);
    if (!pin_id) {
      context.pending_connections.push_back({.net = net_id, .special = special, .instance = std::move(instance), .pin = std::move(pin)});
      return;
    }
    directConnectIoPin(context, pin_id, net_id, special);
    return;
  }
  if (instance == "*") {
    if (!special) {
      throw std::runtime_error("wildcard COMPONENT connection is only valid for SPECIALNETS");
    }
    for (const auto instance_id : netlist.instances()) {
      const auto pin_id = netlist.findInstancePin(instance_id, pin);
      if (pin_id) {
        directConnectInstancePin(context, pin_id, net_id, special);
      }
    }
    return;
  }
  const auto instance_id = netlist.findInstance(instance);
  if (!instance_id) {
    context.pending_connections.push_back({.net = net_id, .special = special, .instance = std::move(instance), .pin = std::move(pin)});
    return;
  }
  const auto pin_id = netlist.findInstancePin(instance_id, pin);
  if (!pin_id) {
    throw std::runtime_error("NET references an unknown instance pin: " + instance + "/" + pin);
  }
  directConnectInstancePin(context, pin_id, net_id, special);
}

void directResolvePending(DirectContext& context)
{
  auto& netlist = context.design.netlistStorage();
  for (const auto& pending : context.pending_regions) {
    const auto region = context.design.constraintStorage().findRegion(pending.name);
    if (!region) {
      throw std::runtime_error("COMPONENT references an unknown REGION: " + pending.name);
    }
    auto instance = netlist.instance(pending.instance);
    instance.flags |= DesignInstanceFlag::kHasRegion;
    instance.region = region;
    netlist.updateInstance(pending.instance, std::move(instance));
  }
  for (const auto& pending : context.pending_group_regions) {
    const auto region = context.design.constraintStorage().findRegion(pending.name);
    if (!region) {
      throw std::runtime_error("GROUP references an unknown REGION: " + pending.name);
    }
    auto group = context.design.constraintStorage().group(pending.group);
    group.flags |= DesignGroupFlag::kHasRegion;
    group.region = region;
    context.design.constraintStorage().updateGroup(pending.group, std::move(group));
  }
  for (const auto& pending : context.pending_pin_nets) {
    const auto regular = netlist.findRegularNet(pending.net);
    const auto special = netlist.findSpecialNet(pending.net);
    if (!regular && !special) {
      throw std::runtime_error("PIN references an unknown NET: " + pending.net);
    }
    const bool use_special = (netlist.ioPin(pending.pin).flags & DesignIoPinFlag::kSpecial) != 0u && special;
    directConnectIoPin(context, pending.pin, use_special ? special : (regular ? regular : special), use_special || !regular);
  }
  for (const auto& pending : context.pending_connections) {
    if (!pending.instance.empty() && pending.instance != "*" && !isPinConnection(pending.instance)
        && !netlist.findInstance(pending.instance)) {
      throw std::runtime_error("NET references an unknown COMPONENT: " + pending.instance);
    }
    if (isPinConnection(pending.instance) && !netlist.findIoPin(pending.pin)) {
      // OpenDB materializes an undeclared top-level PIN referenced by NETS.
      // Keep the same permissive behavior so normalized DEF can be imported
      // back without inventing geometry or placement data.
      DesignIoPin synthetic_pin{.name = pending.pin,
                                .direction = DesignIoPinDirection::kInput,
                                .use = DesignSignalUse::kSignal};
      if (pending.special) synthetic_pin.flags |= DesignIoPinFlag::kSpecial;
      const auto synthetic_id = netlist.createIoPin(std::move(synthetic_pin));
      directConnectIoPin(context, synthetic_id, pending.net, pending.special);
      continue;
    }
    directConnect(context, pending.net, pending.special, pending.instance, pending.pin);
  }
  context.pending_regions.clear();
  context.pending_group_regions.clear();
  context.pending_pin_nets.clear();
  context.pending_connections.clear();
}

int directDesignCallback(defrCallbackType_e type, const char* value, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrDesignStartCbkType) throw std::runtime_error("invalid SI2 DESIGN callback");
    context.design_name = requiredText(value, "DESIGN name");
    setDirectInfo(context);
  });
}

int directDividerCallback(defrCallbackType_e type, const char* value, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrDividerCbkType) throw std::runtime_error("invalid SI2 DIVIDERCHAR callback");
    context.divider_character = requiredText(value, "DIVIDERCHAR");
    setDirectInfo(context);
  });
}

int directBusBitCallback(defrCallbackType_e type, const char* value, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrBusBitCbkType) throw std::runtime_error("invalid SI2 BUSBITCHARS callback");
    context.bus_bit_characters = requiredText(value, "BUSBITCHARS");
    setDirectInfo(context);
  });
}

int directUnitsCallback(defrCallbackType_e type, double value, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrUnitsCbkType) throw std::runtime_error("invalid SI2 UNITS callback");
    context.database_units_per_micron = exactInt32(value, "DEF database units");
    setDirectInfo(context);
  });
}

int directDieAreaCallback(defrCallbackType_e type, defiBox* source, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrDieAreaCbkType || source == nullptr) throw std::runtime_error("invalid SI2 DIEAREA callback");
    const auto raw = source->getPoint();
    if (raw.numPoints < 2 || raw.x == nullptr || raw.y == nullptr) throw std::runtime_error("DIEAREA requires at least two points");
    DesignDieArea area;
    area.boundary.reserve(static_cast<std::size_t>(raw.numPoints));
    for (int index = 0; index < raw.numPoints; ++index) area.boundary.push_back(Point{raw.x[index], raw.y[index]});
    context.design.globalStorage().setDieArea(std::move(area));
  });
}

int directRowCallback(defrCallbackType_e type, defiRow* source, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrRowCbkType || source == nullptr) throw std::runtime_error("invalid SI2 ROW callback");
    const auto site_name = requiredText(source->macro(), "ROW site");
    const auto site = context.sites.find(site_name);
    if (site == context.sites.end()) throw std::runtime_error("ROW references an unknown Library SITE: " + site_name);
    DesignRow row{.name = requiredText(source->name(), "ROW name"), .site = site->second,
                  .origin = {exactInt32(source->x(), "ROW x"), exactInt32(source->y(), "ROW y")},
                  .orientation = orientation(source->orientStr())};
    if (source->hasDo()) {
      row.flags |= DesignRowFlag::kHasDo;
      row.repeat_count_x = exactUint32(source->xNum(), "ROW DO count");
      row.repeat_count_y = exactUint32(source->yNum(), "ROW BY count");
      if (source->hasDoStep()) {
        row.flags |= DesignRowFlag::kHasStep;
        row.step_x = exactInt32(source->xStep(), "ROW STEP x");
        row.step_y = exactInt32(source->yStep(), "ROW STEP y");
      }
    }
    row.properties.reserve(static_cast<std::size_t>(source->numProps()));
    for (int index = 0; index < source->numProps(); ++index) {
      const auto name = requiredText(source->propName(index), "ROW PROPERTY name");
      const auto* raw_value = source->propValue(index);
      if (raw_value == nullptr) throw std::runtime_error("ROW PROPERTY value is required");
      const std::string value = raw_value;
      row.properties.push_back(DesignProperty{.name = name, .value = value, .type = propertyType(source->propType(index), value)});
    }
    static_cast<void>(context.design.floorplanStorage().createRow(std::move(row)));
  });
}

int directTrackCallback(defrCallbackType_e type, defiTrack* source, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrTrackCbkType || source == nullptr) throw std::runtime_error("invalid SI2 TRACKS callback");
    DesignTrackGrid grid{.axis = axis(source->macro()),
                         .start = exactInt32(source->x(), "TRACKS start"),
                         .track_count = exactUint32(source->xNum(), "TRACKS count"),
                         .step = exactInt32(source->xStep(), "TRACKS step")};
    if (source->firstTrackMask() != 0) {
      grid.flags |= DesignTrackGridFlag::kHasMask;
      grid.mask = static_cast<uint32_t>(source->firstTrackMask());
      if (source->sameMask()) grid.flags |= DesignTrackGridFlag::kSameMask;
    }
    grid.layers.reserve(static_cast<std::size_t>(source->numLayers()));
    for (int index = 0; index < source->numLayers(); ++index) {
      grid.layers.push_back(directRoutingLayer(context, requiredText(source->layer(index), "TRACKS layer"), "TRACKS"));
    }
    static_cast<void>(context.design.floorplanStorage().createTrackGrid(std::move(grid)));
  });
}

int directGcellCallback(defrCallbackType_e type, defiGcellGrid* source, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrGcellGridCbkType || source == nullptr) throw std::runtime_error("invalid SI2 GCELLGRID callback");
    DesignGCellGrid grid{.axis = axis(source->macro()),
                         .start = source->x(),
                         .line_count = exactUint32(source->xNum(), "GCELLGRID count"),
                         .step = exactInt32(source->xStep(), "GCELLGRID step")};
    static_cast<void>(context.design.floorplanStorage().createGCellGrid(std::move(grid)));
  });
}

int directViaCallback(defrCallbackType_e type, defiVia* source, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrViaCbkType || source == nullptr) throw std::runtime_error("invalid SI2 VIA callback");
    DesignVia target{.name = requiredText(source->name(), "VIA name")};
    if (context.design.routingStorage().findVia(target.name)) throw std::runtime_error("duplicate DEF VIA: " + target.name);
    std::string via_rule_name;
    std::string bottom_layer;
    std::string cut_layer;
    std::string top_layer;
    if (source->hasViaRule()) {
      target.flags |= DesignViaFlag::kGenerated;
      char* via_rule = nullptr;
      char* bottom = nullptr;
      char* cut = nullptr;
      char* top = nullptr;
      source->viaRule(&via_rule, &target.generated.cut_size_x, &target.generated.cut_size_y, &bottom, &cut, &top,
                      &target.generated.cut_spacing_x, &target.generated.cut_spacing_y, &target.generated.bottom_enclosure_x,
                      &target.generated.bottom_enclosure_y, &target.generated.top_enclosure_x,
                      &target.generated.top_enclosure_y);
      via_rule_name = requiredText(via_rule, "generated VIA VIARULE");
      bottom_layer = requiredText(bottom, "generated VIA bottom layer");
      cut_layer = requiredText(cut, "generated VIA cut layer");
      top_layer = requiredText(top, "generated VIA top layer");
      if (source->hasRowCol()) {
        int rows = 0;
        int columns = 0;
        source->rowCol(&rows, &columns);
        target.generated.flags |= DesignGeneratedViaFlag::kHasRowCol;
        target.generated.row_count = exactUint32(rows, "generated VIA ROWCOL rows");
        target.generated.column_count = exactUint32(columns, "generated VIA ROWCOL columns");
      }
      if (source->hasOrigin()) {
        target.generated.flags |= DesignGeneratedViaFlag::kHasOrigin;
        source->origin(&target.generated.origin.x, &target.generated.origin.y);
      }
      if (source->hasOffset()) {
        target.generated.flags |= DesignGeneratedViaFlag::kHasOffset;
        source->offset(&target.generated.bottom_offset.x, &target.generated.bottom_offset.y,
                       &target.generated.top_offset.x, &target.generated.top_offset.y);
      }
      if (source->hasCutPattern()) {
        target.generated.flags |= DesignGeneratedViaFlag::kHasCutPattern;
        target.generated.cut_pattern = requiredText(source->cutPattern(), "generated VIA PATTERN");
      }
    }
    target.rectangles.reserve(static_cast<std::size_t>(source->numLayers()));
    for (int index = 0; index < source->numLayers(); ++index) {
      char* layer = nullptr;
      int ll_x = 0;
      int ll_y = 0;
      int ur_x = 0;
      int ur_y = 0;
      source->layer(index, &layer, &ll_x, &ll_y, &ur_x, &ur_y);
      DesignViaRectangle rectangle{.layer = context.layers.at(requiredText(layer, "VIA RECT layer")),
                                   .rectangle = Rect{ll_x, ll_y, ur_x, ur_y}.normalized()};
      if (source->hasRectMask(index)) rectangle.mask = static_cast<uint32_t>(source->rectMask(index));
      target.rectangles.push_back(std::move(rectangle));
    }
    target.polygons.reserve(static_cast<std::size_t>(source->numPolygons()));
    for (int index = 0; index < source->numPolygons(); ++index) {
      DesignViaPolygon polygon{.layer = context.layers.at(requiredText(source->polygonName(index), "VIA POLYGON layer")),
                               .points = points(source->getPolygon(index), "VIA POLYGON")};
      if (source->hasPolyMask(index)) polygon.mask = static_cast<uint32_t>(source->polyMask(index));
      target.polygons.push_back(std::move(polygon));
    }
    if (source->hasPattern()) {
      target.flags |= DesignViaFlag::kHasPatternName;
      target.pattern_name = requiredText(source->pattern(), "VIA PATTERNNAME");
    }
    if ((target.flags & DesignViaFlag::kGenerated) != 0u) {
      target.generated.via_rule = context.via_rules.at(via_rule_name);
      target.generated.bottom_layer = TechRoutingLayerId{context.layers.at(bottom_layer).entity()};
      target.generated.cut_layer = TechCutLayerId{context.layers.at(cut_layer).entity()};
      target.generated.top_layer = TechRoutingLayerId{context.layers.at(top_layer).entity()};
    }
    static_cast<void>(context.design.routingStorage().createVia(std::move(target)));
  });
}

int directNdrCallback(defrCallbackType_e type, defiNonDefault* source, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrNonDefaultCbkType || source == nullptr) throw std::runtime_error("invalid SI2 NONDEFAULTRULE callback");
    DesignNonDefaultRule target{.name = requiredText(source->name(), "NONDEFAULTRULE name")};
    if (context.design.routingStorage().findNonDefaultRule(target.name)) throw std::runtime_error("duplicate DEF NONDEFAULTRULE: " + target.name);
    if (source->hasHardspacing()) target.flags |= DesignNonDefaultRuleFlag::kHardSpacing;
    target.layer_rules.reserve(static_cast<std::size_t>(source->numLayers()));
    for (int index = 0; index < source->numLayers(); ++index) {
      DesignNdrLayerRule rule{.layer = context.layers.at(requiredText(source->layerName(index), "NONDEFAULTRULE LAYER")),
                              .width = source->layerWidthVal(index)};
      if (source->hasLayerDiagWidth(index)) { rule.flags |= DesignNdrLayerRuleFlag::kHasDiagWidth; rule.diag_width = source->layerDiagWidthVal(index); }
      if (source->hasLayerSpacing(index)) { rule.flags |= DesignNdrLayerRuleFlag::kHasSpacing; rule.spacing = source->layerSpacingVal(index); }
      if (source->hasLayerWireExt(index)) { rule.flags |= DesignNdrLayerRuleFlag::kHasWireExtension; rule.wire_extension = source->layerWireExtVal(index); }
      target.layer_rules.push_back(std::move(rule));
    }
    target.vias.reserve(static_cast<std::size_t>(source->numVias()));
    for (int index = 0; index < source->numVias(); ++index) {
      const auto name = requiredText(source->viaName(index), "NONDEFAULTRULE VIA");
      const auto tech = context.tech_vias.find(name); const auto design = context.design.routingStorage().findVia(name);
      if ((tech != context.tech_vias.end()) == static_cast<bool>(design)) throw std::runtime_error("NONDEFAULTRULE VIA must resolve to exactly one VIA: " + name);
      target.vias.push_back(tech != context.tech_vias.end() ? DesignNdrViaRef{.tech_via = tech->second} : DesignNdrViaRef{.design_via = design});
    }
    target.via_rules.reserve(static_cast<std::size_t>(source->numViaRules()));
    for (int index = 0; index < source->numViaRules(); ++index) {
      target.via_rules.push_back(context.via_rules.at(requiredText(source->viaRuleName(index), "NONDEFAULTRULE VIARULE")));
    }
    target.min_cuts.reserve(static_cast<std::size_t>(source->numMinCuts()));
    for (int index = 0; index < source->numMinCuts(); ++index) {
      target.min_cuts.push_back(DesignNdrMinCutsRule{.layer = TechCutLayerId{context.layers.at(requiredText(source->cutLayerName(index), "NONDEFAULTRULE MINCUTS layer")).entity()},
                                                     .cut_count = exactUint32(source->numCuts(index), "MINCUTS count")});
    }
    target.properties.reserve(static_cast<std::size_t>(source->numProps()));
    for (int index = 0; index < source->numProps(); ++index) {
      const std::string value = source->propValue(index) == nullptr ? std::string{} : source->propValue(index);
      target.properties.push_back(DesignProperty{.name = requiredText(source->propName(index), "NONDEFAULTRULE PROPERTY name"),
                                                  .value = value,
                                                  .type = propertyType(source->propType(index), value)});
    }
    static_cast<void>(context.design.routingStorage().createNonDefaultRule(std::move(target)));
  });
}

int directComponentCallback(defrCallbackType_e type, defiComponent* source, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrComponentCbkType || source == nullptr) throw std::runtime_error("invalid SI2 COMPONENT callback");
    const auto name = requiredText(source->id(), "COMPONENT name");
    if (context.design.netlistStorage().findInstance(name)) throw std::runtime_error("duplicate DEF COMPONENT: " + name);
    const auto master_name = requiredText(source->name(), "COMPONENT master");
    DesignInstance instance{.name = name, .master = context.masters.at(master_name), .placement_status = componentPlacement(*source)};
    if (source->hasSource()) instance.source = instanceSource(source->source());
    if (instance.placement_status != DesignPlacementStatus::kUnplaced) {
      instance.origin = {source->placementX(), source->placementY()};
      instance.orientation = orientation(source->placementOrientStr());
    }
    if (source->hasWeight()) { instance.flags |= DesignInstanceFlag::kHasWeight; instance.weight = source->weight(); }
    std::string region_name;
    if (source->hasRegionName() && source->hasRegionBounds()) throw std::runtime_error("COMPONENT cannot use named and inline regions together: " + name);
    if (source->hasRegionName()) {
      instance.flags |= DesignInstanceFlag::kHasRegion;
      region_name = requiredText(source->regionName(), "COMPONENT REGION");
      if (const auto found = context.design.constraintStorage().findRegion(region_name); found) instance.region = found;
      else instance.flags &= ~DesignInstanceFlag::kHasRegion;
    }
    if (source->hasRegionBounds()) {
      int count = 0; int* ll_x = nullptr; int* ll_y = nullptr; int* ur_x = nullptr; int* ur_y = nullptr;
      source->regionBounds(&count, &ll_x, &ll_y, &ur_x, &ur_y);
      if (count <= 0 || ll_x == nullptr || ll_y == nullptr || ur_x == nullptr || ur_y == nullptr) throw std::runtime_error("COMPONENT has invalid inline REGION bounds: " + name);
      instance.flags |= DesignInstanceFlag::kHasRegionBounds;
      instance.region_bounds.reserve(static_cast<std::size_t>(count));
      for (int index = 0; index < count; ++index) instance.region_bounds.push_back(Rect{ll_x[index], ll_y[index], ur_x[index], ur_y[index]}.normalized());
    }
    if (source->hasHalo()) {
      instance.flags |= DesignInstanceFlag::kHasHalo;
      if (source->hasHaloSoft()) instance.flags |= DesignInstanceFlag::kHaloSoft;
      source->haloEdges(&instance.halo.left, &instance.halo.bottom, &instance.halo.right, &instance.halo.top);
    }
    if (source->hasRouteHalo()) {
      instance.flags |= DesignInstanceFlag::kHasRouteHalo;
      instance.route_halo.distance = source->haloDist();
      instance.route_halo.min_layer = directRoutingLayer(context, requiredText(source->minLayer(), "COMPONENT ROUTEHALO minimum layer"), "COMPONENT ROUTEHALO");
      instance.route_halo.max_layer = directRoutingLayer(context, requiredText(source->maxLayer(), "COMPONENT ROUTEHALO maximum layer"), "COMPONENT ROUTEHALO");
    }
    directDiagnostic(context, "COMPONENT EEQ/GENERATE/FOREIGN", static_cast<std::size_t>(source->hasEEQ() + source->hasGenerate() + source->hasForeignName()));
    directDiagnostic(context, "COMPONENT PROPERTY", static_cast<std::size_t>(source->numProps()));
    directDiagnostic(context, "COMPONENT MASKSHIFT", static_cast<std::size_t>(source->maskShiftSize()));
    const auto id = context.design.netlistStorage().createInstance(std::move(instance));
    if (!region_name.empty() && !context.design.constraintStorage().findRegion(region_name)) context.pending_regions.push_back({.instance = id, .name = region_name});
  });
}

void directSetPinPlacement(DesignIoPinPort& port, DesignPlacementStatus status, Point origin, DesignOrientation orient)
{
  port.flags |= DesignIoPinPortFlag::kHasPlacement;
  port.placement_status = status;
  port.origin = origin;
  port.orientation = orient;
}

template <typename Source>
void directParsePinGeometry(const Source& source, DesignIoPinPort& port, DirectContext& context, std::string_view field)
{
  for (int index = 0; index < source.numLayer(); ++index) {
    int ll_x = 0;
    int ll_y = 0;
    int ur_x = 0;
    int ur_y = 0;
    source.bounds(index, &ll_x, &ll_y, &ur_x, &ur_y);
    const auto layer_name = requiredText(source.layer(index), std::string(field) + " LAYER");
    DesignPinRectangle rectangle{.layer = context.layers.at(layer_name),
                                 .rectangle = Rect{ll_x, ll_y, ur_x, ur_y}.normalized()};
    if (source.layerMask(index) != 0) { rectangle.flags |= DesignPinShapeFlag::kHasMask; rectangle.mask = static_cast<uint32_t>(source.layerMask(index)); }
    if (source.hasLayerSpacing(index)) { rectangle.flags |= DesignPinShapeFlag::kHasSpacing; rectangle.spacing = source.layerSpacing(index); }
    if (source.hasLayerDesignRuleWidth(index)) { rectangle.flags |= DesignPinShapeFlag::kHasDesignRuleWidth; rectangle.design_rule_width = source.layerDesignRuleWidth(index); }
    port.rectangles.push_back(std::move(rectangle));
  }
  for (int index = 0; index < source.numPolygons(); ++index) {
    DesignPinPolygon polygon{.layer = context.layers.at(requiredText(source.polygonName(index), std::string(field) + " POLYGON layer")),
                             .points = points(source.getPolygon(index), std::string(field) + " POLYGON")};
    if (source.polygonMask(index) != 0) { polygon.flags |= DesignPinShapeFlag::kHasMask; polygon.mask = static_cast<uint32_t>(source.polygonMask(index)); }
    if (source.hasPolygonSpacing(index)) { polygon.flags |= DesignPinShapeFlag::kHasSpacing; polygon.spacing = source.polygonSpacing(index); }
    if (source.hasPolygonDesignRuleWidth(index)) { polygon.flags |= DesignPinShapeFlag::kHasDesignRuleWidth; polygon.design_rule_width = source.polygonDesignRuleWidth(index); }
    port.polygons.push_back(std::move(polygon));
  }
  for (int index = 0; index < source.numVias(); ++index) {
    const auto via_name = requiredText(source.viaName(index), std::string(field) + " VIA");
    const auto technology = context.tech_vias.find(via_name);
    const auto local = context.design.routingStorage().findVia(via_name);
    if ((technology != context.tech_vias.end()) == static_cast<bool>(local)) throw std::runtime_error("PIN VIA name must resolve to exactly one Design or Tech VIA: " + via_name);
    DesignPinVia via{.origin = {source.viaPtX(index), source.viaPtY(index)}};
    via.top_mask = static_cast<uint32_t>(source.viaTopMask(index));
    via.cut_mask = static_cast<uint32_t>(source.viaCutMask(index));
    via.bottom_mask = static_cast<uint32_t>(source.viaBottomMask(index));
    if (via.top_mask != 0u || via.cut_mask != 0u || via.bottom_mask != 0u) via.flags |= DesignPinViaFlag::kHasMask;
    if (technology != context.tech_vias.end()) via.tech_via = technology->second;
    else via.design_via = local;
    port.vias.push_back(std::move(via));
  }
}

int directPinCallback(defrCallbackType_e type, defiPin* source, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrPinCbkType || source == nullptr) throw std::runtime_error("invalid SI2 PIN callback");
    const auto name = requiredText(source->pinName(), "PIN name");
    if (context.design.netlistStorage().findIoPin(name)) throw std::runtime_error("duplicate DEF PIN: " + name);
    const auto net = requiredText(source->netName(), "PIN NET");
    DesignIoPin pin{.name = name};
    if (source->hasDirection()) pin.direction = pinDirection(source->direction());
    if (source->hasUse()) pin.use = signalUse(source->use());
    if (source->hasSpecial()) pin.flags |= DesignIoPinFlag::kSpecial;
    std::vector<DesignIoPinPort> ports;
    DesignIoPinPort implicit_port;
    if (source->hasPlacement()) directSetPinPlacement(implicit_port, pinPlacement(*source), Point{source->placementX(), source->placementY()}, orientation(source->orientStr()));
    directParsePinGeometry(*source, implicit_port, context, "PIN");
    if ((implicit_port.flags & DesignIoPinPortFlag::kHasPlacement) != 0u || !implicit_port.rectangles.empty() || !implicit_port.polygons.empty() || !implicit_port.vias.empty()) ports.push_back(std::move(implicit_port));
    for (int port_index = 0; port_index < source->numPorts(); ++port_index) {
      const auto* port_source = source->pinPort(port_index);
      if (port_source == nullptr) throw std::runtime_error("SI2 returned a null PIN PORT");
      DesignIoPinPort port{.flags = DesignIoPinPortFlag::kExplicit};
      if (port_source->hasPlacement()) directSetPinPlacement(port, portPlacement(*port_source), Point{port_source->placementX(), port_source->placementY()}, orientation(port_source->orientStr()));
      directParsePinGeometry(*port_source, port, context, "PIN PORT");
      ports.push_back(std::move(port));
    }
    pin.ports = std::move(ports);
    directDiagnostic(context, "PIN NETEXPR/SENSITIVITY", static_cast<std::size_t>(source->hasNetExpr() + source->hasSupplySensitivity() + source->hasGroundSensitivity()));
    const auto id = context.design.netlistStorage().createIoPin(std::move(pin));
    context.pending_pin_nets.push_back({.pin = id, .net = net});
  });
}

int directNetCallback(defrCallbackType_e type, defiNet* source, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if ((type != defrNetCbkType && type != defrSNetCbkType) || source == nullptr) {
      throw std::runtime_error("invalid SI2 NET callback");
    }
    const bool special = type == defrSNetCbkType;
    const bool must_join = source->numConnections() > 0 && source->pinIsMustJoin(0);
    DesignNet target;
    std::string net_name;
    if (must_join) {
      if (special || source->numConnections() != 1) {
        throw std::runtime_error("DEF MUSTJOIN requires exactly one regular-net component pin reference");
      }
      target.flags |= DesignNetFlag::kMustJoin;
      do {
        net_name = "__idb_mustjoin_" + std::to_string(context.must_join_index++);
      } while (context.design.netlistStorage().findRegularNet(net_name));
    } else {
      net_name = requiredText(source->name(), "NET name");
      const auto& netlist = context.design.netlistStorage();
      if (special ? netlist.findSpecialNet(net_name) : netlist.findRegularNet(net_name)) throw std::runtime_error("duplicate DEF NET/SPECIALNET: " + net_name);
    }
    target.name = net_name;
    if (source->hasUse()) target.use = signalUse(source->use());
    if (source->hasWeight()) { target.flags |= DesignNetFlag::kHasWeight; target.weight = source->weight(); }
    if (source->hasFixedbump()) target.flags |= DesignNetFlag::kFixedBump;
    std::string ndr_name;
    if (source->hasNonDefaultRule()) { target.flags |= DesignNetFlag::kHasNonDefaultRule; ndr_name = requiredText(source->nonDefaultRule(), "NET NONDEFAULTRULE"); }
    if (!ndr_name.empty()) {
      const auto local_ndr = context.design.routingStorage().findNonDefaultRule(ndr_name);
      const auto tech_ndr = context.tech_ndrs.find(ndr_name);
      if (local_ndr) target.design_non_default_rule = local_ndr;
      else if (tech_ndr != context.tech_ndrs.end()) target.non_default_rule = tech_ndr->second;
      else throw std::runtime_error("NET references an unknown NONDEFAULTRULE: " + ndr_name);
    }
    auto& netlist = context.design.netlistStorage();
    const auto net_id = special ? netlist.createSpecialNet(std::move(target)) : netlist.createNet(std::move(target));
    // Do not attach PINS-declared members before NETS connections. iDB only
    // calls add_io_pin from NETS, so get_driving_pin() walks NETS order.
    // Leftover PINS + NET names are still resolved in directResolvePending.
    for (int index = 0; index < source->numConnections(); ++index) {
      if (source->pinIsMustJoin(index) != (must_join && index == 0)) throw std::runtime_error("invalid DEF MUSTJOIN connection layout");
      directConnect(context, net_id, special, requiredText(source->instance(index), "NET connection instance"),
                    requiredText(source->pin(index), "NET connection pin"));
      directDiagnostic(context, "NET SYNTHESIZED", static_cast<std::size_t>(source->pinIsSynthesized(index)));
    }
    DesignNetOptions options;
    if (source->hasOriginal()) { options.flags |= DesignNetOptionsFlag::kHasOriginal; options.original = requiredText(source->original(), "NET ORIGINAL"); }
    if (source->hasPattern()) { options.flags |= DesignNetOptionsFlag::kHasPattern; options.pattern = netPattern(source->pattern()); }
    if (source->hasCap()) { options.flags |= DesignNetOptionsFlag::kHasEstimatedCapacitance; options.estimated_capacitance = source->cap(); }
    if (source->hasFrequency()) { options.flags |= DesignNetOptionsFlag::kHasFrequency; options.frequency = source->frequency(); }
    if (source->hasXTalk()) { options.flags |= DesignNetOptionsFlag::kHasXTalk; options.xtalk = source->XTalk(); }
    if (source->hasStyle()) { options.flags |= DesignNetOptionsFlag::kHasStyle; options.style = source->style(); }
    if (source->hasVoltage()) { options.flags |= DesignNetOptionsFlag::kHasVoltage; options.voltage = exactInt32(source->voltage(), "SPECIALNET VOLTAGE"); }
    for (int index = 0; index < source->numSpacingRules(); ++index) {
      char* layer = nullptr; double spacing = 0.0; double range_left = 0.0; double range_right = 0.0;
      source->spacingRule(index, &layer, &spacing, &range_left, &range_right);
      DesignNetSpacingRule rule{.spacing = exactInt32(spacing, "SPECIALNET SPACING")};
      if (range_left != spacing || range_right != spacing) { rule.flags |= DesignNetSpacingRuleFlag::kHasRange; rule.range_left = exactInt32(range_left, "SPECIALNET SPACING RANGE left"); rule.range_right = exactInt32(range_right, "SPECIALNET SPACING RANGE right"); }
      rule.layer = directRoutingLayer(context, requiredText(layer, "SPECIALNET SPACING layer"), "SPECIALNET SPACING");
      options.spacing_rules.push_back(std::move(rule));
    }
    if (options.flags != 0u || !options.spacing_rules.empty()) netlist.setNetOptions(net_id, std::move(options));
    DesignNetGeometry geometry;
    geometry.rectangles.reserve(static_cast<std::size_t>(source->numRectangles()));
    for (int index = 0; index < source->numRectangles(); ++index) {
      DesignNetRectangle rectangle{.rectangle = Rect{source->xl(index), source->yl(index), source->xh(index), source->yh(index)}.normalized(), .route_status = wireStatus(source->rectRouteStatus(index))};
      if (source->rectMask(index) != 0) { rectangle.flags |= DesignNetGeometryFlag::kHasMask; rectangle.mask = static_cast<uint32_t>(source->rectMask(index)); }
      if (const auto* shape = source->rectShapeType(index); shape != nullptr && *shape != '\0') { rectangle.flags |= DesignNetGeometryFlag::kHasShape; rectangle.shape = shape; }
      if (rectangle.route_status == DesignWireStatus::kShield) rectangle.shield_net = requiredText(source->rectRouteStatusShieldName(index), "SPECIALNET RECT SHIELD net");
      rectangle.layer = directRoutingLayer(context, requiredText(source->rectName(index), "SPECIALNET RECT layer"), "NET RECT");
      geometry.rectangles.push_back(std::move(rectangle));
    }
    geometry.polygons.reserve(static_cast<std::size_t>(source->numPolygons()));
    for (int index = 0; index < source->numPolygons(); ++index) {
      DesignNetPolygon polygon{.points = points(source->getPolygon(index), "SPECIALNET POLYGON"), .route_status = wireStatus(source->polyRouteStatus(index))};
      if (source->polyMask(index) != 0) { polygon.flags |= DesignNetGeometryFlag::kHasMask; polygon.mask = static_cast<uint32_t>(source->polyMask(index)); }
      if (const auto* shape = source->polyShapeType(index); shape != nullptr && *shape != '\0') { polygon.flags |= DesignNetGeometryFlag::kHasShape; polygon.shape = shape; }
      if (polygon.route_status == DesignWireStatus::kShield) polygon.shield_net = requiredText(source->polyRouteStatusShieldName(index), "SPECIALNET POLYGON SHIELD net");
      polygon.layer = directRoutingLayer(context, requiredText(source->polygonName(index), "SPECIALNET POLYGON layer"), "NET POLYGON");
      geometry.polygons.push_back(std::move(polygon));
    }
    geometry.vias.reserve(static_cast<std::size_t>(source->numViaSpecs()));
    for (int index = 0; index < source->numViaSpecs(); ++index) {
      DesignNetVia via{.origins = points(source->getViaPts(index), "SPECIALNET VIA", 1), .orientation = orientation(source->viaOrientStr(index)), .route_status = wireStatus(source->viaRouteStatus(index))};
      via.top_mask = static_cast<uint32_t>(source->topMaskNum(index)); via.cut_mask = static_cast<uint32_t>(source->cutMaskNum(index)); via.bottom_mask = static_cast<uint32_t>(source->bottomMaskNum(index));
      if (via.top_mask != 0u || via.cut_mask != 0u || via.bottom_mask != 0u) via.flags |= DesignNetGeometryFlag::kHasMask;
      if (const auto* shape = source->viaShapeType(index); shape != nullptr && *shape != '\0') { via.flags |= DesignNetGeometryFlag::kHasShape; via.shape = shape; }
      if (via.route_status == DesignWireStatus::kShield) via.shield_net = requiredText(source->viaRouteStatusShieldName(index), "SPECIALNET VIA SHIELD net");
      const auto via_name = requiredText(source->viaName(index), "SPECIALNET VIA name");
      const auto local_via = context.design.routingStorage().findVia(via_name); const auto tech_via = context.tech_vias.find(via_name);
      if (static_cast<bool>(local_via) == (tech_via != context.tech_vias.end())) throw std::runtime_error("NET VIA name must resolve to exactly one VIA: " + via_name);
      if (local_via) via.design_via = local_via; else via.tech_via = tech_via->second;
      geometry.vias.push_back(std::move(via));
    }
    if (!geometry.rectangles.empty() || !geometry.polygons.empty() || !geometry.vias.empty()) context.design.routingStorage().setNetGeometry(net_id, std::move(geometry));
    for (int index = 0; index < source->numWires(); ++index) {
      const auto* wire_source = source->wire(index);
      if (wire_source == nullptr) throw std::runtime_error("SI2 returned a null NET wire");
      auto wire = parseWire(context, *wire_source);
      wire.component.net = net_id;
      static_cast<void>(context.design.routingStorage().createWireTrusted(std::move(wire.component), std::move(wire.routing)));
    }
    directDiagnostic(context, "NET PROPERTY", static_cast<std::size_t>(source->numProps()));
    directDiagnostic(context, "NET SUBNET", static_cast<std::size_t>(source->numSubnets()));
    directDiagnostic(context, "NET VPIN", static_cast<std::size_t>(source->numVpins()));
    directDiagnostic(context, "NET obsolete WIDTH", static_cast<std::size_t>(source->numWidthRules()));
    directDiagnostic(context, "NET legacy SHIELD/NOSHIELD", static_cast<std::size_t>(source->numShields() + source->numNoShields() + source->numShieldNets()));
  });
}

int directRegionCallback(defrCallbackType_e type, defiRegion* source, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrRegionCbkType || source == nullptr) throw std::runtime_error("invalid SI2 REGION callback");
    DesignRegion target{.name = requiredText(source->name(), "REGION name")};
    if (context.design.constraintStorage().findRegion(target.name)) throw std::runtime_error("duplicate DEF REGION: " + target.name);
    if (source->hasType()) target.type = regionType(source->type());
    target.rectangles.reserve(static_cast<std::size_t>(source->numRectangles()));
    for (int index = 0; index < source->numRectangles(); ++index) target.rectangles.push_back(Rect{source->xl(index), source->yl(index), source->xh(index), source->yh(index)}.normalized());
    directDiagnostic(context, "REGION PROPERTY", static_cast<std::size_t>(source->numProps()));
    const auto name = target.name;
    const auto id = context.design.constraintStorage().createRegion(std::move(target));
    static_cast<void>(id);
  });
}

int directGroupNameCallback(defrCallbackType_e type, const char* value, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrGroupNameCbkType || context.current_group) throw std::runtime_error("invalid SI2 GROUP name callback");
    const std::string name = requiredText(value, "GROUP name");
    if (context.design.constraintStorage().findGroup(name)) throw std::runtime_error("duplicate DEF GROUP: " + name);
    context.current_group = GroupContext{.name = name};
  });
}

int directGroupMemberCallback(defrCallbackType_e type, const char* value, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrGroupMemberCbkType || !context.current_group) throw std::runtime_error("invalid SI2 GROUP member callback");
    context.current_group->member_patterns.push_back(requiredText(value, "GROUP member pattern"));
  });
}

int directGroupCallback(defrCallbackType_e type, defiGroup* source, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrGroupCbkType || source == nullptr || !context.current_group) throw std::runtime_error("invalid SI2 GROUP callback");
    if (source->hasRegionName()) context.current_group->region = requiredText(source->regionName(), "GROUP REGION");
    directDiagnostic(context, "GROUP obsolete REGION box", static_cast<std::size_t>(source->hasRegionBox()));
    directDiagnostic(context, "GROUP SOFT/property",
                     static_cast<std::size_t>(source->hasMaxX() + source->hasMaxY() + source->hasPerim() + source->numProps()));
    DesignGroup group{.name = context.current_group->name};
    const auto region_name = context.current_group->region;
    if (!region_name.empty()) {
      const auto region = context.design.constraintStorage().findRegion(region_name);
      if (region) {
        group.flags |= DesignGroupFlag::kHasRegion;
        group.region = region;
      }
    }
    std::unordered_set<uint32_t> added;
    for (const auto& pattern : context.current_group->member_patterns) {
      for (const auto id : context.design.netlistStorage().instances()) {
        if (globMatches(pattern, context.design.netlistStorage().instance(id).name) && added.emplace(id.packed()).second) group.instances.push_back(id);
      }
    }
    const auto group_id = context.design.constraintStorage().createGroup(std::move(group));
    if (!region_name.empty() && !context.design.constraintStorage().findRegion(region_name)) {
      context.pending_group_regions.push_back({.group = group_id, .name = region_name});
    }
    context.current_group.reset();
  });
}

int directBlockageCallback(defrCallbackType_e type, defiBlockage* source, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrBlockageCbkType || source == nullptr) throw std::runtime_error("invalid SI2 BLOCKAGE callback");
    DesignBlockage target;
    std::string layer_name;
    std::string component_name;
    if (source->hasLayer()) {
      target.kind = DesignBlockageKind::kRouting;
      target.flags |= DesignBlockageFlag::kHasLayer;
      layer_name = requiredText(source->layerName(), "routing BLOCKAGE layer");
      target.layer = directRoutingLayer(context, layer_name, "BLOCKAGE");
    } else if (source->hasPlacement()) {
      target.kind = DesignBlockageKind::kPlacement;
    } else throw std::runtime_error("BLOCKAGE must be LAYER or PLACEMENT");
    if (source->hasSoft()) target.flags |= DesignBlockageFlag::kSoft;
    if (source->hasPushdown()) target.flags |= DesignBlockageFlag::kPushdown;
    if (source->hasPartial()) { target.flags |= DesignBlockageFlag::kHasPartial; target.partial = source->placementMaxDensity(); }
    if (source->hasComponent()) {
      target.flags |= DesignBlockageFlag::kHasComponent;
      component_name = requiredText(source->hasLayer() ? source->layerComponentName() : source->placementComponentName(), "BLOCKAGE COMPONENT");
      const auto instance = context.design.netlistStorage().findInstance(component_name);
      if (!instance) throw std::runtime_error("BLOCKAGE references an unknown COMPONENT: " + component_name);
      target.component = instance;
    }
    if (source->hasSlots()) target.flags |= DesignBlockageFlag::kSlots;
    if (source->hasFills()) target.flags |= DesignBlockageFlag::kFills;
    if (source->hasExceptpgnet()) target.flags |= DesignBlockageFlag::kExceptPgNet;
    if (source->hasSpacing()) { target.flags |= DesignBlockageFlag::kHasSpacing; target.spacing = source->minSpacing(); }
    if (source->hasDesignRuleWidth()) { target.flags |= DesignBlockageFlag::kHasDesignRuleWidth; target.design_rule_width = source->designRuleWidth(); }
    if (source->hasMask()) { target.flags |= DesignBlockageFlag::kHasMask; target.mask = static_cast<uint32_t>(source->mask()); }
    target.rectangles.reserve(static_cast<std::size_t>(source->numRectangles()));
    for (int index = 0; index < source->numRectangles(); ++index) target.rectangles.push_back(Rect{source->xl(index), source->yl(index), source->xh(index), source->yh(index)}.normalized());
    target.polygons.reserve(static_cast<std::size_t>(source->numPolygons()));
    for (int index = 0; index < source->numPolygons(); ++index) target.polygons.push_back(points(source->getPolygon(index), "BLOCKAGE POLYGON"));
    static_cast<void>(context.design.constraintStorage().createBlockage(std::move(target)));
  });
}

int directFillCallback(defrCallbackType_e type, defiFill* source, defiUserData user_data) noexcept
{
  return directGuardedCallback(user_data, [&](DirectContext& context) {
    if (type != defrFillCbkType || source == nullptr) throw std::runtime_error("invalid SI2 FILL callback");
    if (source->hasVia()) { directDiagnostic(context, "FILL VIA"); return; }
    if (!source->hasLayer()) throw std::runtime_error("DEF FILL must specify LAYER or VIA");
    directDiagnostic(context, "FILL POLYGON", static_cast<std::size_t>(source->numPolygons()));
    if (source->numRectangles() == 0) return;
    const auto layer_name = requiredText(source->layerName(), "FILL LAYER");
    DesignFill target{.layer = context.layers.at(layer_name)};
    if (source->hasLayerOpc()) target.flags |= DesignFillFlag::kOpc;
    if (source->layerMask() != 0) { if (source->layerMask() < 0) throw std::runtime_error("FILL MASK must be non-negative"); target.flags |= DesignFillFlag::kHasMask; target.mask = static_cast<uint32_t>(source->layerMask()); }
    target.rectangles.reserve(static_cast<std::size_t>(source->numRectangles()));
    for (int index = 0; index < source->numRectangles(); ++index) target.rectangles.push_back(Rect{source->xl(index), source->yl(index), source->xh(index), source->yh(index)}.normalized());
    static_cast<void>(context.design.fillStorage().createFill(std::move(target)));
  });
}

class DirectParserSession
{
 public:
  DirectParserSession()
  {
    if (defrInit() != 0) throw std::runtime_error("failed to initialize SI2 DEF parser session");
    defrSetDesignCbk(directDesignCallback);
    defrSetDividerCbk(directDividerCallback);
    defrSetBusBitCbk(directBusBitCallback);
    defrSetUnitsCbk(directUnitsCallback);
    defrSetDieAreaCbk(directDieAreaCallback);
    defrSetRowCbk(directRowCallback);
    defrSetTrackCbk(directTrackCallback);
    defrSetGcellGridCbk(directGcellCallback);
    defrSetViaCbk(directViaCallback);
    defrSetNonDefaultCbk(directNdrCallback);
    defrSetComponentCbk(directComponentCallback);
    defrSetPinCbk(directPinCallback);
    defrSetNetCbk(directNetCallback);
    defrSetSNetCbk(directNetCallback);
    defrSetRegionCbk(directRegionCallback);
    defrSetGroupNameCbk(directGroupNameCallback);
    defrSetGroupMemberCbk(directGroupMemberCallback);
    defrSetGroupCbk(directGroupCallback);
    defrSetBlockageCbk(directBlockageCallback);
    defrSetFillCbk(directFillCallback);
    defrSetAddPathToNet();
  }

  ~DirectParserSession() { static_cast<void>(defrClear()); }
  DirectParserSession(const DirectParserSession&) = delete;
  DirectParserSession& operator=(const DirectParserSession&) = delete;
};

void parseDirectFile(const std::filesystem::path& path, DirectContext& context)
{
  if (path.empty()) throw std::invalid_argument("DEF path must not be empty");
  const auto& tech = context.design.techRegistry().registry();
  const auto& library = context.design.libraryRegistry().registry();
  context.layers = componentNameMap<TechLayerId, TechLayerInfo>(tech);
  context.tech_vias = componentNameMap<TechViaMasterId, TechViaMaster>(tech);
  context.via_rules = componentNameMap<TechViaRuleGenerateId, TechViaRuleGenerate>(tech);
  context.tech_ndrs = componentNameMap<TechNonDefaultRuleId, TechNonDefaultRule>(tech);
  context.sites = componentNameMap<LibrarySiteId, LibrarySite>(library);
  context.masters = componentNameMap<LibraryCellMasterId, LibraryCellMaster>(library);

  DirectParserSession session;
  std::unique_ptr<FILE, FileCloser> file(std::fopen(path.c_str(), "r"));
  if (!file) throw std::runtime_error("cannot open DEF file: " + path.string());
  context.callback_failure = nullptr;
  const auto result = defrRead(file.get(), path.c_str(), &context, 1);
  if (context.callback_failure) std::rethrow_exception(context.callback_failure);
  if (result != 0) throw std::runtime_error("SI2 failed to parse DEF file: " + path.string());
  if (context.current_group) throw std::runtime_error("SI2 completed with an unterminated GROUP");
  if (context.design_name.empty() || context.database_units_per_micron <= 0) throw std::runtime_error("DEF requires DESIGN and positive UNITS statements");
  if (!context.design.globalStorage().hasDieArea()) throw std::runtime_error("DEF requires DIEAREA for Design V1 import");
  directResolvePending(context);
  if (!context.pending_pin_nets.empty() || !context.pending_connections.empty() || !context.pending_regions.empty()
      || !context.pending_group_regions.empty()) {
    throw std::runtime_error("DEF contains unresolved references");
  }
}

std::vector<DefDesignImportDiagnostic> buildDiagnostics(const DirectContext& context)
{
  std::vector<DefDesignImportDiagnostic> result;
  result.reserve(context.diagnostics.size());
  for (const auto& [statement, count] : context.diagnostics) {
    result.push_back(DefDesignImportDiagnostic{.statement = statement, .occurrence_count = count});
  }
  std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) { return lhs.statement < rhs.statement; });
  return result;
}

}  // namespace

void DefDesignImporter::import(const std::filesystem::path& file)
{
  if (_used) {
    throw std::logic_error("direct DEF importer is one-shot");
  }
  _used = true;
  ensureEmptyTarget(_design);

  DirectContext context(_design);
  {
    const std::scoped_lock lock(def_detail::parserMutex());
    try {
      parseDirectFile(file, context);
      _design.routingStorage().shrinkRoutingPoolToFit();
    } catch (...) {
      try {
        throw;
      } catch (const std::exception& error) {
        std::fprintf(stderr, "[DEF import] failed: %s\n", error.what());
      } catch (...) {
        std::fprintf(stderr, "[DEF import] failed: unknown exception\n");
      }
      rollbackImport(_design);
      throw;
    }
  }
  _diagnostics = buildDiagnostics(context);
}

}  // namespace eccdb
