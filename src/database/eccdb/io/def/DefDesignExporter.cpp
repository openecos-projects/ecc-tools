#include "def/DefDesignExporter.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "design/constraint/model/ConstraintComponents.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/non_default_rule/component/NonDefaultRuleComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/via/component/ViaComponents.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "library/site/model/SiteComponents.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"
#include "tech/via_master/model/ViaMasterComponents.h"

namespace eccdb {
namespace {

class DefTextOutput
{
  class SmallBuffer
  {
   public:
    template <std::size_t Size>
    void append(const char (&text)[Size])
    {
      append(text, Size - 1u);
    }

    void append(const char* data, std::size_t size)
    {
      if (size > static_cast<std::size_t>(_storage.data() + _storage.size() - _end)) {
        throw std::runtime_error("DEF routing token exceeds formatting buffer");
      }
      std::memcpy(_end, data, size);
      _end += size;
    }

    template <typename Value>
      requires std::is_integral_v<Value>
    void appendInteger(Value value)
    {
      const auto result = std::to_chars(_end, _storage.data() + _storage.size(), value);
      if (result.ec != std::errc{}) throw std::runtime_error("failed to format DEF routing integer");
      _end = result.ptr;
    }

    [[nodiscard]] const char* data() const noexcept { return _storage.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return static_cast<std::size_t>(_end - _storage.data()); }

   private:
    std::array<char, 128u> _storage{};
    char* _end = _storage.data();
  };

 public:
  explicit DefTextOutput(std::ostream& output) : _output(output) {}

  template <std::size_t Size>
  DefTextOutput& operator<<(const char (&text)[Size])
  {
    append(text, Size - 1u);
    return *this;
  }

  DefTextOutput& operator<<(const char* text)
  {
    append(text, std::strlen(text));
    return *this;
  }

  DefTextOutput& operator<<(std::string_view text)
  {
    append(text.data(), text.size());
    return *this;
  }

  DefTextOutput& operator<<(const std::string& text)
  {
    append(text.data(), text.size());
    return *this;
  }

  DefTextOutput& operator<<(char value)
  {
    append(&value, 1u);
    return *this;
  }

  template <typename Value>
    requires(std::is_integral_v<Value> && !std::is_same_v<std::remove_cv_t<Value>, char>)
  DefTextOutput& operator<<(Value value)
  {
    std::array<char, std::numeric_limits<std::make_unsigned_t<Value>>::digits10 + 4u> text{};
    const auto result = std::to_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{}) throw std::runtime_error("failed to format DEF integer");
    append(text.data(), static_cast<std::size_t>(result.ptr - text.data()));
    return *this;
  }

  template <typename Value>
    requires std::is_floating_point_v<Value>
  DefTextOutput& operator<<(Value value)
  {
    flush();
    _output << value;
    return *this;
  }

  void flush()
  {
    if (_size == 0u) return;
    _output.write(_buffer.data(), static_cast<std::streamsize>(_size));
    _size = 0u;
  }

  void writeRoutingPoint(Point point, const Point* previous, uint32_t flags, int32_t extension)
  {
    SmallBuffer text;
    if ((flags & DesignWirePointFlag::kVirtual) != 0u) text.append(" VIRTUAL");
    text.append(" ( ");
    if (previous != nullptr && point.x == previous->x) {
      text.append("* ");
      text.appendInteger(point.y);
    } else if (previous != nullptr && point.y == previous->y) {
      text.appendInteger(point.x);
      text.append(" *");
    } else {
      text.appendInteger(point.x);
      text.append(" ");
      text.appendInteger(point.y);
    }
    if ((flags & DesignWirePointFlag::kHasExtension) != 0u) {
      text.append(" ");
      text.appendInteger(extension);
    }
    text.append(" )");
    append(text.data(), text.size());
  }

  void writeRoutingRectangle(Rect rectangle)
  {
    SmallBuffer text;
    text.append(" RECT ( ");
    text.appendInteger(rectangle.ll_x);
    text.append(" ");
    text.appendInteger(rectangle.ll_y);
    text.append(" ");
    text.appendInteger(rectangle.ur_x);
    text.append(" ");
    text.appendInteger(rectangle.ur_y);
    text.append(" )");
    append(text.data(), text.size());
  }

  void writeRoutingViaMask(uint32_t top, uint32_t cut, uint32_t bottom)
  {
    SmallBuffer text;
    text.append(" MASK ");
    text.appendInteger(top);
    text.appendInteger(cut);
    text.appendInteger(bottom);
    append(text.data(), text.size());
  }

  void writeRoutingViaArray(uint32_t columns, uint32_t rows, int32_t step_x, int32_t step_y)
  {
    SmallBuffer text;
    text.append(" DO ");
    text.appendInteger(columns);
    text.append(" BY ");
    text.appendInteger(rows);
    text.append(" STEP ");
    text.appendInteger(step_x);
    text.append(" ");
    text.appendInteger(step_y);
    append(text.data(), text.size());
  }

 private:
  void append(const char* data, std::size_t size)
  {
    while (size != 0u) {
      if (_size == _buffer.size()) flush();
      const auto count = std::min(size, _buffer.size() - _size);
      std::memcpy(_buffer.data() + _size, data, count);
      data += count;
      size -= count;
      _size += count;
    }
  }

  std::ostream& _output;
  std::array<char, 64u * 1024u> _buffer{};
  std::size_t _size = 0u;
};

std::string_view orientationName(DesignOrientation orientation)
{
  switch (orientation) {
    case DesignOrientation::kN:
      return "N";
    case DesignOrientation::kS:
      return "S";
    case DesignOrientation::kE:
      return "E";
    case DesignOrientation::kW:
      return "W";
    case DesignOrientation::kFN:
      return "FN";
    case DesignOrientation::kFS:
      return "FS";
    case DesignOrientation::kFE:
      return "FE";
    case DesignOrientation::kFW:
      return "FW";
  }
  throw std::logic_error("invalid design orientation during DEF export");
}

std::string_view axisName(DesignAxis axis)
{
  return axis == DesignAxis::kX ? "X" : "Y";
}

void writeDefString(DefTextOutput& output, std::string_view value)
{
  output << '"';
  for (const char character : value) {
    if (character == '"' || character == '\\') {
      output << '\\';
    }
    output << character;
  }
  output << '"';
}

std::string_view placementName(DesignPlacementStatus status)
{
  switch (status) {
    case DesignPlacementStatus::kUnplaced:
      return "UNPLACED";
    case DesignPlacementStatus::kPlaced:
      return "PLACED";
    case DesignPlacementStatus::kFixed:
      return "FIXED";
    case DesignPlacementStatus::kCover:
      return "COVER";
  }
  throw std::logic_error("invalid placement status during DEF export");
}

std::string_view instanceSourceName(DesignInstanceSource source)
{
  switch (source) {
    case DesignInstanceSource::kNone:
      return {};
    case DesignInstanceSource::kNetlist:
      return "NETLIST";
    case DesignInstanceSource::kDist:
      return "DIST";
    case DesignInstanceSource::kUser:
      return "USER";
    case DesignInstanceSource::kTiming:
      return "TIMING";
  }
  throw std::logic_error("invalid instance source during DEF export");
}

std::string_view signalUseName(DesignSignalUse use)
{
  switch (use) {
    case DesignSignalUse::kNone:
      return {};
    case DesignSignalUse::kSignal:
      return "SIGNAL";
    case DesignSignalUse::kAnalog:
      return "ANALOG";
    case DesignSignalUse::kPower:
      return "POWER";
    case DesignSignalUse::kGround:
      return "GROUND";
    case DesignSignalUse::kClock:
      return "CLOCK";
    case DesignSignalUse::kTieOff:
      return "TIEOFF";
    case DesignSignalUse::kScan:
      return "SCAN";
    case DesignSignalUse::kReset:
      return "RESET";
  }
  throw std::logic_error("invalid signal use during DEF export");
}

std::string_view ioDirectionName(DesignIoPinDirection direction)
{
  switch (direction) {
    case DesignIoPinDirection::kNone:
      return {};
    case DesignIoPinDirection::kInput:
      return "INPUT";
    case DesignIoPinDirection::kOutput:
      return "OUTPUT";
    case DesignIoPinDirection::kInOut:
      return "INOUT";
    case DesignIoPinDirection::kFeedThru:
      return "FEEDTHRU";
  }
  throw std::logic_error("invalid IO direction during DEF export");
}

std::string_view netSourceName(DesignNetSource source)
{
  switch (source) {
    case DesignNetSource::kNone:
      return {};
    case DesignNetSource::kNetlist:
      return "NETLIST";
    case DesignNetSource::kDist:
      return "DIST";
    case DesignNetSource::kUser:
      return "USER";
    case DesignNetSource::kTiming:
      return "TIMING";
    case DesignNetSource::kTest:
      return "TEST";
  }
  throw std::logic_error("invalid net source during DEF export");
}

std::string_view netPatternName(DesignNetPattern pattern)
{
  switch (pattern) {
    case DesignNetPattern::kNone:
      return {};
    case DesignNetPattern::kBalanced:
      return "BALANCED";
    case DesignNetPattern::kSteiner:
      return "STEINER";
    case DesignNetPattern::kTrunk:
      return "TRUNK";
    case DesignNetPattern::kWiredLogic:
      return "WIREDLOGIC";
  }
  throw std::logic_error("invalid net pattern during DEF export");
}

std::string_view wireStatusName(DesignWireStatus status)
{
  switch (status) {
    case DesignWireStatus::kRouted:
      return "ROUTED";
    case DesignWireStatus::kFixed:
      return "FIXED";
    case DesignWireStatus::kCover:
      return "COVER";
    case DesignWireStatus::kShield:
      return "SHIELD";
    case DesignWireStatus::kNoShield:
      return "NOSHIELD";
  }
  throw std::logic_error("invalid wire status during DEF export");
}

class DefNameCache
{
  template <typename Value>
  struct IndexedEntry
  {
    uint64_t packed = std::numeric_limits<uint64_t>::max();
    const Value* value = nullptr;
  };

 public:
  explicit DefNameCache(const DesignStore& design)
  {
    const auto& tech = design.techRegistry().registry();
    const auto layers = tech.view<const TechLayerInfo>();
    for (const auto entity : layers) {
      const auto index = static_cast<std::size_t>(entt::to_entity(entity));
      if (_layers.size() <= index) _layers.resize(index + 1u);
      _layers[index] = IndexedEntry<TechLayerInfo>{.packed = entt::to_integral(entity),
                                                   .value = &layers.get<const TechLayerInfo>(entity)};
    }
    const auto tech_vias = tech.view<const TechViaMaster>();
    for (const auto entity : tech_vias) {
      const auto index = static_cast<std::size_t>(entt::to_entity(entity));
      if (_tech_vias.size() <= index) _tech_vias.resize(index + 1u);
      _tech_vias[index] = IndexedEntry<std::string>{.packed = entt::to_integral(entity),
                                                    .value = &tech_vias.get<const TechViaMaster>(entity).name};
    }

    const auto design_vias = design.designRegistry().registry().view<const DesignVia>();
    for (const auto entity : design_vias) {
      _design_vias.emplace(static_cast<uint64_t>(entt::to_integral(entity)),
                           &design_vias.get<const DesignVia>(entity).name);
    }
  }

  [[nodiscard]] const TechLayerInfo& layerInfo(TechLayerId id) const
  {
    if (!id) throw std::logic_error("Design references an invalid Tech layer during DEF export");
    const auto entity = id.entity();
    const auto index = static_cast<std::size_t>(entt::to_entity(entity));
    if (index >= _layers.size() || _layers[index].packed != entt::to_integral(entity)
        || _layers[index].value == nullptr) {
      throw std::logic_error("Design references an invalid Tech layer during DEF export");
    }
    return *_layers[index].value;
  }

  [[nodiscard]] std::string_view layerName(TechRoutingLayerId id) const
  {
    return layerInfo(TechLayerId{id.entity()}).name;
  }

  [[nodiscard]] std::string_view techViaName(uint64_t reference) const
  {
    if (reference > std::numeric_limits<uint32_t>::max()) {
      throw std::logic_error("compact technology VIA reference exceeds uint32_t");
    }
    const auto entity = static_cast<TechEntity>(static_cast<uint32_t>(reference));
    const auto index = static_cast<std::size_t>(entt::to_entity(entity));
    if (index >= _tech_vias.size() || _tech_vias[index].packed != reference || _tech_vias[index].value == nullptr) {
      throw std::logic_error("Design references an invalid Tech VIA during DEF export");
    }
    return *_tech_vias[index].value;
  }

  [[nodiscard]] std::string_view designViaName(uint64_t reference) const
  {
    const auto found = _design_vias.find(reference);
    if (found == _design_vias.end()) {
      throw std::logic_error("Design references an invalid Design VIA during DEF export");
    }
    return *found->second;
  }

  [[nodiscard]] std::string_view viaName(TechViaMasterId id) const
  {
    if (!id) throw std::logic_error("Design references an invalid Tech VIA during DEF export");
    return techViaName(id.packed());
  }

  [[nodiscard]] std::string_view viaName(DesignViaId id) const
  {
    if (!id) throw std::logic_error("Design references an invalid Design VIA during DEF export");
    return designViaName(id.packed());
  }

  [[nodiscard]] std::string_view viaName(const DesignRoutingViaRecord& via) const
  {
    if (via.meta.reference_kind == DesignRoutingViaReferenceKind::kTech) return techViaName(via.reference);
    if (via.meta.reference_kind == DesignRoutingViaReferenceKind::kDesign) return designViaName(via.reference);
    throw std::logic_error("compact wire VIA reference kind is invalid");
  }

 private:
  std::vector<IndexedEntry<TechLayerInfo>> _layers;
  std::vector<IndexedEntry<std::string>> _tech_vias;
  std::unordered_map<uint64_t, const std::string*> _design_vias;
};

std::string_view nonDefaultRuleName(const DesignStore& design, TechNonDefaultRuleId id)
{
  const auto& registry = design.techRegistry().registry();
  if (!id || !registry.valid(id.entity()) || !registry.all_of<TechNonDefaultRule>(id.entity())) {
    throw std::logic_error("Design references an invalid Tech NONDEFAULTRULE during DEF export");
  }
  return registry.get<const TechNonDefaultRule>(id.entity()).name;
}

std::string_view nonDefaultRuleName(const DesignStore& design, DesignNonDefaultRuleId id)
{
  return design.routingStorage().nonDefaultRule(id).name;
}

std::string_view nonDefaultRuleName(const DesignStore& design, const DesignNet& net)
{
  if (net.design_non_default_rule) {
    return nonDefaultRuleName(design, net.design_non_default_rule);
  }
  return nonDefaultRuleName(design, net.non_default_rule);
}

std::string_view viaName(const DefNameCache& names, const DesignNdrViaRef& via)
{
  if (via.tech_via) {
    return names.viaName(via.tech_via);
  }
  return names.viaName(via.design_via);
}

std::string_view viaName(const DefNameCache& names, const DesignNetVia& via)
{
  if (via.tech_via) {
    return names.viaName(via.tech_via);
  }
  return names.viaName(via.design_via);
}

std::string_view pinViaName(const DefNameCache& names, const DesignPinVia& via)
{
  if (via.tech_via) {
    return names.viaName(via.tech_via);
  }
  return names.viaName(via.design_via);
}

void writePolygon(DefTextOutput& output, const std::vector<Point>& polygon)
{
  for (const auto point : polygon) {
    output << " ( " << point.x << ' ' << point.y << " )";
  }
}

void writePinShapeOptions(DefTextOutput& output, uint32_t flags, uint32_t mask, int32_t spacing, int32_t design_rule_width)
{
  if ((flags & DesignPinShapeFlag::kHasMask) != 0u) {
    output << " MASK " << mask;
  }
  if ((flags & DesignPinShapeFlag::kHasSpacing) != 0u) {
    output << " SPACING " << spacing;
  } else if ((flags & DesignPinShapeFlag::kHasDesignRuleWidth) != 0u) {
    output << " DESIGNRULEWIDTH " << design_rule_width;
  }
}

void writePinPort(DefTextOutput& output, const DefNameCache& names, const DesignIoPinPort& port)
{
  if ((port.flags & DesignIoPinPortFlag::kExplicit) != 0u) {
    output << "\n  + PORT";
  }
  for (const auto& rectangle : port.rectangles) {
    output << "\n    + LAYER " << names.layerInfo(rectangle.layer).name;
    writePinShapeOptions(output, rectangle.flags, rectangle.mask, rectangle.spacing, rectangle.design_rule_width);
    output << " ( " << rectangle.rectangle.ll_x << ' ' << rectangle.rectangle.ll_y << " ) ( " << rectangle.rectangle.ur_x << ' '
           << rectangle.rectangle.ur_y << " )";
  }
  for (const auto& polygon : port.polygons) {
    output << "\n    + POLYGON " << names.layerInfo(polygon.layer).name;
    writePinShapeOptions(output, polygon.flags, polygon.mask, polygon.spacing, polygon.design_rule_width);
    writePolygon(output, polygon.points);
  }
  for (const auto& via : port.vias) {
    output << "\n    + VIA " << pinViaName(names, via);
    if ((via.flags & DesignPinViaFlag::kHasMask) != 0u) {
      output << " MASK " << via.top_mask << via.cut_mask << via.bottom_mask;
    }
    output << " ( " << via.origin.x << ' ' << via.origin.y << " )";
  }
  if ((port.flags & DesignIoPinPortFlag::kHasPlacement) != 0u) {
    output << "\n    + " << placementName(port.placement_status);
    if (port.placement_status != DesignPlacementStatus::kUnplaced) {
      output << " ( " << port.origin.x << ' ' << port.origin.y << " ) " << orientationName(port.orientation);
    }
  }
}

void writeNetGeometryPrefix(DefTextOutput& output, DesignWireStatus status, std::string_view shield_net, uint32_t flags,
                            std::string_view shape)
{
  output << "\n  + " << wireStatusName(status);
  if (status == DesignWireStatus::kShield) {
    output << ' ' << shield_net;
  }
  if ((flags & DesignNetGeometryFlag::kHasShape) != 0u) {
    output << " + SHAPE " << shape;
  }
}

void writeNetGeometry(DefTextOutput& output, const DefNameCache& names, const DesignNetGeometry& geometry)
{
  for (const auto& rectangle : geometry.rectangles) {
    writeNetGeometryPrefix(output, rectangle.route_status, rectangle.shield_net, rectangle.flags, rectangle.shape);
    if ((rectangle.flags & DesignNetGeometryFlag::kHasMask) != 0u) {
      output << " + MASK " << rectangle.mask;
    }
    output << " + RECT " << names.layerName(rectangle.layer) << " ( " << rectangle.rectangle.ll_x << ' ' << rectangle.rectangle.ll_y
           << " ) ( " << rectangle.rectangle.ur_x << ' ' << rectangle.rectangle.ur_y << " )";
  }
  for (const auto& polygon : geometry.polygons) {
    writeNetGeometryPrefix(output, polygon.route_status, polygon.shield_net, polygon.flags, polygon.shape);
    if ((polygon.flags & DesignNetGeometryFlag::kHasMask) != 0u) {
      output << " + MASK " << polygon.mask;
    }
    output << " + POLYGON " << names.layerName(polygon.layer);
    writePolygon(output, polygon.points);
  }
  for (const auto& via : geometry.vias) {
    writeNetGeometryPrefix(output, via.route_status, via.shield_net, via.flags, via.shape);
    if ((via.flags & DesignNetGeometryFlag::kHasMask) != 0u) {
      output << " + MASK " << via.top_mask << via.cut_mask << via.bottom_mask;
    }
    output << " + VIA " << viaName(names, via) << ' ' << orientationName(via.orientation);
    for (const auto origin : via.origins) {
      output << " ( " << origin.x << ' ' << origin.y << " )";
    }
  }
}

void writeNetOptions(DefTextOutput& output, const DefNameCache& names, const DesignNetOptions& options)
{
  if ((options.flags & DesignNetOptionsFlag::kHasOriginal) != 0u) {
    output << "\n  + ORIGINAL " << options.original;
  }
  if ((options.flags & DesignNetOptionsFlag::kHasPattern) != 0u) {
    output << "\n  + PATTERN " << netPatternName(options.pattern);
  }
  if ((options.flags & DesignNetOptionsFlag::kHasEstimatedCapacitance) != 0u) {
    output << "\n  + ESTCAP " << options.estimated_capacitance;
  }
  if ((options.flags & DesignNetOptionsFlag::kHasFrequency) != 0u) {
    output << "\n  + FREQUENCY " << options.frequency;
  }
  if ((options.flags & DesignNetOptionsFlag::kHasXTalk) != 0u) {
    output << "\n  + XTALK " << options.xtalk;
  }
  if ((options.flags & DesignNetOptionsFlag::kHasStyle) != 0u) {
    output << "\n  + STYLE " << options.style;
  }
  if ((options.flags & DesignNetOptionsFlag::kHasVoltage) != 0u) {
    output << "\n  + VOLTAGE " << options.voltage;
  }
  for (const auto& rule : options.spacing_rules) {
    output << "\n  + SPACING " << names.layerName(rule.layer) << ' ' << rule.spacing;
    if ((rule.flags & DesignNetSpacingRuleFlag::kHasRange) != 0u) {
      output << " RANGE " << rule.range_left << ' ' << rule.range_right;
    }
  }
}

void writePath(DefTextOutput& output, const DefNameCache& names, DesignRoutingCompactPathView path, bool first, bool special)
{
  output << (first ? " " : " NEW ") << names.layerName(path.layer);
  const bool has_width = (path.flags & DesignWirePathFlag::kHasWidth) != 0u;
  if (special != has_width) {
    throw std::logic_error("SPECIALNET paths require WIDTH and regular NET paths must use layer/NDR width");
  }
  if (has_width) {
    output << ' ' << (path.extra == nullptr ? 0 : path.extra->width);
  }
  if (!special && (path.flags & DesignWirePathFlag::kTaper) != 0u) {
    output << " TAPER";
  }
  if (!special && (path.flags & DesignWirePathFlag::kHasTaperRule) != 0u) {
    output << " TAPERRULE " << (path.extra == nullptr ? std::string_view{} : path.extra->taper_rule);
  }
  if ((path.flags & DesignWirePathFlag::kHasShape) != 0u) {
    if (!special) {
      throw std::logic_error("regular NET path cannot contain SPECIALNET SHAPE");
    }
    output << " + SHAPE " << (path.extra == nullptr ? std::string_view{} : path.extra->shape);
  }
  if ((path.flags & DesignWirePathFlag::kHasStyle) != 0u) {
    output << (special ? " + STYLE " : " STYLE ") << (path.extra == nullptr ? 0 : path.extra->style);
  }

  const bool rectangles_ordered
      = std::is_sorted(path.rectangles.begin(), path.rectangles.end(), [](const auto& left, const auto& right) {
          return left.point_index < right.point_index;
        });
  const bool vias_ordered = std::is_sorted(path.vias.begin(), path.vias.end(), [](const auto& left, const auto& right) {
    return left.point_index < right.point_index;
  });
  std::size_t rectangle_cursor = 0u;
  std::size_t via_cursor = 0u;
  std::size_t point_extra_cursor = 0u;
  std::size_t via_extra_cursor = 0u;

  const auto write_rectangle = [&](const DesignWireRectangle& rectangle) { output.writeRoutingRectangle(rectangle.delta); };
  const auto write_via = [&](std::size_t local_index, bool sequential) {
    const auto& via = path.vias[local_index];
    const auto global_index = static_cast<uint64_t>(path.via_global_begin) + local_index;
    const DesignRoutingViaExtraEntry* extra = nullptr;
    if (sequential) {
      while (via_extra_cursor < path.via_extras.size() && path.via_extras[via_extra_cursor].via_index < global_index) {
        ++via_extra_cursor;
      }
      if (via_extra_cursor < path.via_extras.size() && path.via_extras[via_extra_cursor].via_index == global_index) {
        extra = &path.via_extras[via_extra_cursor++];
      }
    } else {
      const auto found = std::lower_bound(path.via_extras.begin(), path.via_extras.end(), global_index,
                                          [](const auto& entry, uint64_t index) { return entry.via_index < index; });
      if (found != path.via_extras.end() && found->via_index == global_index) extra = &*found;
    }
    if ((via.meta.flags & DesignWireViaFlag::kHasMask) != 0u) {
      if (extra == nullptr) throw std::logic_error("compact wire VIA mask extra is missing");
      output.writeRoutingViaMask(extra->top_mask, extra->cut_mask, extra->bottom_mask);
    }
    output << ' ' << names.viaName(via);
    const auto orientation = static_cast<DesignOrientation>(via.meta.orientation);
    if (orientation != DesignOrientation::kN) {
      output << ' ' << orientationName(orientation);
    }
    if ((via.meta.flags & DesignWireViaFlag::kHasArray) != 0u) {
      if (extra == nullptr) throw std::logic_error("compact wire VIA array extra is missing");
      output.writeRoutingViaArray(extra->columns, extra->rows, extra->step_x, extra->step_y);
    }
  };

  for (std::size_t point_index = 0; point_index < path.points.size(); ++point_index) {
    const auto global_index = static_cast<uint64_t>(path.point_global_begin) + point_index;
    while (point_extra_cursor < path.point_extras.size()
           && path.point_extras[point_extra_cursor].point_index < global_index) {
      ++point_extra_cursor;
    }
    uint32_t point_flags = 0u;
    int32_t extension = 0;
    if (point_extra_cursor < path.point_extras.size()
        && path.point_extras[point_extra_cursor].point_index == global_index) {
      point_flags = path.point_extras[point_extra_cursor].flags;
      extension = path.point_extras[point_extra_cursor].extension;
      ++point_extra_cursor;
    }
    const auto point = path.points[point_index].position;
    const Point* previous = point_index == 0u ? nullptr : &path.points[point_index - 1u].position;
    output.writeRoutingPoint(point, previous, point_flags, extension);

    if (point_index == 0u) {
      if ((path.flags & DesignWirePathFlag::kHasMask) != 0u) {
        output << " MASK " << (path.extra == nullptr ? 0u : path.extra->mask);
      }
      if (special && (path.flags & DesignWirePathFlag::kTaper) != 0u) {
        output << " TAPER";
      }
      if (special && (path.flags & DesignWirePathFlag::kHasTaperRule) != 0u) {
        output << " TAPERRULE " << (path.extra == nullptr ? std::string_view{} : path.extra->taper_rule);
      }
    }

    if (rectangles_ordered) {
      while (rectangle_cursor < path.rectangles.size() && path.rectangles[rectangle_cursor].point_index == point_index) {
        write_rectangle(path.rectangles[rectangle_cursor++]);
      }
    } else {
      for (const auto& rectangle : path.rectangles) {
        if (rectangle.point_index == point_index) write_rectangle(rectangle);
      }
    }
    if (vias_ordered) {
      while (via_cursor < path.vias.size() && path.vias[via_cursor].point_index == point_index) {
        write_via(via_cursor++, true);
      }
    } else {
      for (std::size_t index = 0; index < path.vias.size(); ++index) {
        if (path.vias[index].point_index == point_index) write_via(index, false);
      }
    }
  }
}

void writeNetSection(DefTextOutput& output, const DesignStore& design, const DefNameCache& names,
                     const std::vector<DesignNetId>& nets, std::string_view section)
{
  if (nets.empty()) {
    return;
  }
  const auto& netlist = design.netlistStorage();
  const auto& routing = design.routingStorage();
  const auto& library = design.libraryRegistry().registry();
  const bool special = section == "SPECIALNETS";
  output << section << ' ' << nets.size() << " ;\n";
  for (const auto id : nets) {
    const auto& net = netlist.net(id);
    const auto instance_pins = netlist.instancePins(id);
    const auto io_pins = netlist.ioPins(id);
    const bool must_join = (net.flags & DesignNetFlag::kMustJoin) != 0u;
    std::size_t first_instance_pin = 0;
    if (must_join) {
      if (special || instance_pins.size() != 1u || !io_pins.empty()) {
        throw std::logic_error("DEF MUSTJOIN requires exactly one component pin and no top-level IO pins");
      }
      const auto& pin = netlist.instancePin(instance_pins.front());
      const auto& instance = netlist.instance(pin.instance);
      const auto& term = library.get<const LibraryMasterTerm>(pin.master_term.entity());
      output << "- MUSTJOIN ( " << instance.name << ' ' << term.name << " )";
      first_instance_pin = instance_pins.size();
    } else {
      output << "- " << net.name;
    }
    for (std::size_t index = first_instance_pin; index < instance_pins.size(); ++index) {
      const auto pin_id = instance_pins[index];
      const auto& pin = netlist.instancePin(pin_id);
      const auto& instance = netlist.instance(pin.instance);
      const auto& term = library.get<const LibraryMasterTerm>(pin.master_term.entity());
      output << "\n  ( " << instance.name << ' ' << term.name << " )";
    }
    for (const auto pin_id : io_pins) {
      output << "\n  ( PIN " << netlist.ioPin(pin_id).name << " )";
    }
    if (const auto use = signalUseName(net.use); !use.empty()) {
      output << "\n  + USE " << use;
    }
    if (const auto source = netSourceName(net.source); !source.empty()) {
      output << "\n  + SOURCE " << source;
    }
    if ((net.flags & DesignNetFlag::kHasWeight) != 0u) {
      output << "\n  + WEIGHT " << net.weight;
    }
    if ((net.flags & DesignNetFlag::kFixedBump) != 0u) {
      output << "\n  + FIXEDBUMP";
    }
    if ((net.flags & DesignNetFlag::kHasNonDefaultRule) != 0u) {
      output << "\n  + NONDEFAULTRULE " << nonDefaultRuleName(design, net);
    }
    if (const auto* options = netlist.netOptions(id); options != nullptr) {
      writeNetOptions(output, names, *options);
    }
    if (const auto* geometry = routing.netGeometry(id); geometry != nullptr) {
      writeNetGeometry(output, names, *geometry);
    }
    for (const auto wire_id : routing.wireIds(id)) {
      const auto& wire = routing.wire(wire_id);
      output << "\n  + " << wireStatusName(wire.status);
      if (!wire.shield_net.empty()) {
        output << ' ' << wire.shield_net;
      }
      bool first = true;
      routing.forEachCompactPath(wire_id, [&](DesignRoutingCompactPathView path) {
        writePath(output, names, path, first, special);
        first = false;
      });
    }
    output << "\n  ;\n";
  }
  output << "END " << section << "\n\n";
}

}  // namespace

void DefDesignExporter::write(std::ostream& stream) const
{
  if (!stream) {
    throw std::invalid_argument("DEF output stream is not writable");
  }
  DefTextOutput output(stream);
  const DefNameCache names(_design);
  const auto& global = _design.globalStorage();
  if (!global.hasInfo() || !global.hasDieArea()) {
    throw std::logic_error("DEF export requires DesignInfo and DieArea");
  }
  const auto& info = global.info();
  output << "VERSION 5.8 ;\n";
  output << "DIVIDERCHAR \"" << info.divider_character << "\" ;\n";
  output << "BUSBITCHARS \"" << info.bus_bit_characters << "\" ;\n";
  output << "DESIGN " << info.name << " ;\n";
  output << "UNITS DISTANCE MICRONS " << info.database_units_per_micron << " ;\n\n";

  output << "DIEAREA";
  for (const auto point : global.dieArea().boundary) {
    output << " ( " << point.x << ' ' << point.y << " )";
  }
  output << " ;\n\n";

  const auto& floorplan = _design.floorplanStorage();
  const auto& library = _design.libraryRegistry().registry();
  for (const auto id : floorplan.rows()) {
    const auto& row = floorplan.row(id);
    const auto& site = library.get<const LibrarySite>(row.site.entity());
    output << "ROW " << row.name << ' ' << site.name << ' ' << row.origin.x << ' ' << row.origin.y << ' '
           << orientationName(row.orientation);
    if ((row.flags & DesignRowFlag::kHasDo) != 0u) {
      output << " DO " << row.repeat_count_x << " BY " << row.repeat_count_y;
      if ((row.flags & DesignRowFlag::kHasStep) != 0u) {
        output << " STEP " << row.step_x << ' ' << row.step_y;
      }
    }
    if (!row.properties.empty()) {
      output << " + PROPERTY";
      for (const auto& property : row.properties) {
        output << ' ' << property.name << ' ';
        if (property.type == DesignPropertyType::kString) {
          writeDefString(output, property.value);
        } else {
          output << property.value;
        }
      }
    }
    output << " ;\n";
  }
  auto track_grids = floorplan.trackGrids();
  const auto track_key = [&](DesignTrackGridId id) {
    const auto& grid = floorplan.trackGrid(id);
    std::string layers;
    for (const auto layer : grid.layers) {
      layers.append(names.layerName(layer));
      layers.push_back('\0');
    }
    return std::tuple{std::move(layers), static_cast<uint8_t>(grid.axis), grid.start, grid.track_count, grid.step, grid.flags, grid.mask};
  };
  std::sort(track_grids.begin(), track_grids.end(), [&](DesignTrackGridId lhs, DesignTrackGridId rhs) {
    return track_key(lhs) < track_key(rhs);
  });
  for (const auto id : track_grids) {
    const auto& grid = floorplan.trackGrid(id);
    output << "TRACKS " << axisName(grid.axis) << ' ' << grid.start << " DO " << grid.track_count << " STEP " << grid.step;
    if ((grid.flags & DesignTrackGridFlag::kHasMask) != 0u) {
      output << " MASK " << grid.mask;
      if ((grid.flags & DesignTrackGridFlag::kSameMask) != 0u) {
        output << " SAMEMASK";
      }
    }
    if (!grid.layers.empty()) {
      output << " LAYER";
      for (const auto layer : grid.layers) {
        output << ' ' << names.layerName(layer);
      }
    }
    output << " ;\n";
  }
  auto gcell_grids = floorplan.gcellGrids();
  std::sort(gcell_grids.begin(), gcell_grids.end(), [&](DesignGCellGridId lhs, DesignGCellGridId rhs) {
    const auto& left = floorplan.gcellGrid(lhs);
    const auto& right = floorplan.gcellGrid(rhs);
    return std::tie(left.axis, left.start, left.line_count, left.step) < std::tie(right.axis, right.start, right.line_count, right.step);
  });
  for (const auto id : gcell_grids) {
    const auto& grid = floorplan.gcellGrid(id);
    output << "GCELLGRID " << axisName(grid.axis) << ' ' << grid.start << " DO " << grid.line_count << " STEP " << grid.step << " ;\n";
  }
  output << '\n';

  const auto& routing = _design.routingStorage();
  const auto vias = routing.vias();
  if (!vias.empty()) {
    output << "VIAS " << vias.size() << " ;\n";
    for (const auto id : vias) {
      const auto& via = routing.via(id);
      output << "- " << via.name;
      if ((via.flags & DesignViaFlag::kHasPatternName) != 0u) {
        output << "\n  + PATTERNNAME " << via.pattern_name;
      }
      if ((via.flags & DesignViaFlag::kGenerated) != 0u) {
        const auto& formula = via.generated;
        const auto& rule = _design.techRegistry().registry().get<const TechViaRuleGenerate>(formula.via_rule.entity());
        output << "\n  + VIARULE " << rule.name;
        output << "\n  + CUTSIZE " << formula.cut_size_x << ' ' << formula.cut_size_y;
        output << "\n  + LAYERS " << names.layerName(formula.bottom_layer) << ' '
               << names.layerInfo(TechLayerId{formula.cut_layer.entity()}).name << ' ' << names.layerName(formula.top_layer);
        output << "\n  + CUTSPACING " << formula.cut_spacing_x << ' ' << formula.cut_spacing_y;
        output << "\n  + ENCLOSURE " << formula.bottom_enclosure_x << ' ' << formula.bottom_enclosure_y << ' ' << formula.top_enclosure_x
               << ' ' << formula.top_enclosure_y;
        if ((formula.flags & DesignGeneratedViaFlag::kHasRowCol) != 0u) {
          output << "\n  + ROWCOL " << formula.row_count << ' ' << formula.column_count;
        }
        if ((formula.flags & DesignGeneratedViaFlag::kHasCutPattern) != 0u) {
          output << "\n  + PATTERN " << formula.cut_pattern;
        }
        if ((formula.flags & DesignGeneratedViaFlag::kHasOrigin) != 0u) {
          output << "\n  + ORIGIN " << formula.origin.x << ' ' << formula.origin.y;
        }
        if ((formula.flags & DesignGeneratedViaFlag::kHasOffset) != 0u) {
          output << "\n  + OFFSET " << formula.bottom_offset.x << ' ' << formula.bottom_offset.y << ' ' << formula.top_offset.x << ' '
                 << formula.top_offset.y;
        }
      } else {
        for (const auto& rectangle : via.rectangles) {
          output << "\n  + RECT " << names.layerInfo(rectangle.layer).name;
          if (rectangle.mask != 0u) {
            output << " + MASK " << rectangle.mask;
          }
          output << " ( " << rectangle.rectangle.ll_x << ' ' << rectangle.rectangle.ll_y << " ) ( " << rectangle.rectangle.ur_x << ' '
                 << rectangle.rectangle.ur_y << " )";
        }
        for (const auto& polygon : via.polygons) {
          output << "\n  + POLYGON " << names.layerInfo(polygon.layer).name;
          if (polygon.mask != 0u) {
            output << " + MASK " << polygon.mask;
          }
          writePolygon(output, polygon.points);
        }
      }
      output << "\n  ;\n";
    }
    output << "END VIAS\n\n";
  }

  const auto non_default_rules = routing.nonDefaultRules();
  if (!non_default_rules.empty()) {
    output << "NONDEFAULTRULES " << non_default_rules.size() << " ;\n";
    for (const auto id : non_default_rules) {
      const auto& rule = routing.nonDefaultRule(id);
      output << "- " << rule.name;
      if ((rule.flags & DesignNonDefaultRuleFlag::kHardSpacing) != 0u) {
        output << "\n  + HARDSPACING";
      }
      for (const auto& layer_rule : rule.layer_rules) {
        output << "\n  + LAYER " << names.layerInfo(layer_rule.layer).name << " WIDTH " << layer_rule.width;
        if ((layer_rule.flags & DesignNdrLayerRuleFlag::kHasDiagWidth) != 0u) {
          output << " DIAGWIDTH " << layer_rule.diag_width;
        }
        if ((layer_rule.flags & DesignNdrLayerRuleFlag::kHasSpacing) != 0u) {
          output << " SPACING " << layer_rule.spacing;
        }
        if ((layer_rule.flags & DesignNdrLayerRuleFlag::kHasWireExtension) != 0u) {
          output << " WIREEXT " << layer_rule.wire_extension;
        }
      }
      for (const auto& via : rule.vias) {
        output << "\n  + VIA " << viaName(names, via);
      }
      for (const auto via_rule : rule.via_rules) {
        output << "\n  + VIARULE " << _design.techRegistry().registry().get<const TechViaRuleGenerate>(via_rule.entity()).name;
      }
      for (const auto& min_cuts : rule.min_cuts) {
        output << "\n  + MINCUTS " << names.layerInfo(TechLayerId{min_cuts.layer.entity()}).name << ' ' << min_cuts.cut_count;
      }
      for (const auto& property : rule.properties) {
        output << "\n  + PROPERTY " << property.name << ' ';
        if (property.type == DesignPropertyType::kString) {
          writeDefString(output, property.value);
        } else {
          output << property.value;
        }
      }
      output << "\n  ;\n";
    }
    output << "END NONDEFAULTRULES\n\n";
  }

  const auto& netlist = _design.netlistStorage();
  const auto instances = netlist.instances();
  if (!instances.empty()) {
    output << "COMPONENTS " << instances.size() << " ;\n";
    for (const auto id : instances) {
      const auto& instance = netlist.instance(id);
      const auto& master = library.get<const LibraryCellMaster>(instance.master.entity());
      output << "- " << instance.name << ' ' << master.name;
      if (const auto source = instanceSourceName(instance.source); !source.empty()) {
        output << "\n  + SOURCE " << source;
      }
      if ((instance.flags & DesignInstanceFlag::kHasWeight) != 0u) {
        output << "\n  + WEIGHT " << instance.weight;
      }
      if ((instance.flags & DesignInstanceFlag::kHasRegion) != 0u) {
        output << "\n  + REGION " << _design.constraintStorage().region(instance.region).name;
      }
      if ((instance.flags & DesignInstanceFlag::kHasRegionBounds) != 0u) {
        output << "\n  + REGION";
        for (const auto rectangle : instance.region_bounds) {
          output << " ( " << rectangle.ll_x << ' ' << rectangle.ll_y << " ) ( " << rectangle.ur_x << ' ' << rectangle.ur_y << " )";
        }
      }
      if ((instance.flags & DesignInstanceFlag::kHasHalo) != 0u) {
        output << "\n  + HALO";
        if ((instance.flags & DesignInstanceFlag::kHaloSoft) != 0u) {
          output << " SOFT";
        }
        output << ' ' << instance.halo.left << ' ' << instance.halo.bottom << ' ' << instance.halo.right << ' ' << instance.halo.top;
      }
      if ((instance.flags & DesignInstanceFlag::kHasRouteHalo) != 0u) {
        output << "\n  + ROUTEHALO " << instance.route_halo.distance << ' ' << names.layerName(instance.route_halo.min_layer) << ' '
               << names.layerName(instance.route_halo.max_layer);
      }
      output << "\n  + " << placementName(instance.placement_status);
      if (instance.placement_status != DesignPlacementStatus::kUnplaced) {
        output << " ( " << instance.origin.x << ' ' << instance.origin.y << " ) " << orientationName(instance.orientation);
      }
      output << "\n  ;\n";
    }
    output << "END COMPONENTS\n\n";
  }

  const auto io_pins = netlist.ioPins();
  if (!io_pins.empty()) {
    output << "PINS " << io_pins.size() << " ;\n";
    for (const auto id : io_pins) {
      const auto& pin = netlist.ioPin(id);
      output << "- " << pin.name;
      const auto pin_net = pin.net ? pin.net : pin.special_net;
      if (!pin_net) {
        throw std::logic_error("DEF PIN must reference a regular or special NET");
      }
      output << "\n  + NET " << netlist.net(pin_net).name;
      if ((pin.flags & DesignIoPinFlag::kSpecial) != 0u) {
        output << "\n  + SPECIAL";
      }
      if (const auto direction = ioDirectionName(pin.direction); !direction.empty()) {
        output << "\n  + DIRECTION " << direction;
      }
      if (const auto use = signalUseName(pin.use); !use.empty()) {
        output << "\n  + USE " << use;
      }
      for (const auto& port : pin.ports) {
        writePinPort(output, names, port);
      }
      output << "\n  ;\n";
    }
    output << "END PINS\n\n";
  }

  const auto& constraints = _design.constraintStorage();
  const auto blockages = constraints.blockages();
  if (!blockages.empty()) {
    output << "BLOCKAGES " << blockages.size() << " ;\n";
    for (const auto id : blockages) {
      const auto& blockage = constraints.blockage(id);
      output << "- ";
      if (blockage.kind == DesignBlockageKind::kRouting) {
        output << "LAYER " << names.layerName(blockage.layer);
      } else {
        output << "PLACEMENT";
      }
      if ((blockage.flags & DesignBlockageFlag::kSoft) != 0u) {
        output << " + SOFT";
      }
      if ((blockage.flags & DesignBlockageFlag::kPushdown) != 0u) {
        output << " + PUSHDOWN";
      }
      if ((blockage.flags & DesignBlockageFlag::kHasPartial) != 0u) {
        output << " + PARTIAL " << blockage.partial;
      }
      if ((blockage.flags & DesignBlockageFlag::kHasComponent) != 0u) {
        output << " + COMPONENT " << netlist.instance(blockage.component).name;
      }
      if ((blockage.flags & DesignBlockageFlag::kSlots) != 0u) {
        output << " + SLOTS";
      }
      if ((blockage.flags & DesignBlockageFlag::kFills) != 0u) {
        output << " + FILLS";
      }
      if ((blockage.flags & DesignBlockageFlag::kExceptPgNet) != 0u) {
        output << " + EXCEPTPGNET";
      }
      if ((blockage.flags & DesignBlockageFlag::kHasSpacing) != 0u) {
        output << " + SPACING " << blockage.spacing;
      }
      if ((blockage.flags & DesignBlockageFlag::kHasDesignRuleWidth) != 0u) {
        output << " + DESIGNRULEWIDTH " << blockage.design_rule_width;
      }
      if ((blockage.flags & DesignBlockageFlag::kHasMask) != 0u) {
        output << " + MASK " << blockage.mask;
      }
      for (const auto rectangle : blockage.rectangles) {
        output << " RECT ( " << rectangle.ll_x << ' ' << rectangle.ll_y << " ) ( " << rectangle.ur_x << ' ' << rectangle.ur_y << " )";
      }
      for (const auto& polygon : blockage.polygons) {
        output << " POLYGON";
        writePolygon(output, polygon);
      }
      output << " ;\n";
    }
    output << "END BLOCKAGES\n\n";
  }

  const auto& fill_storage = _design.fillStorage();
  const auto fills = fill_storage.fills();
  if (!fills.empty()) {
    output << "FILLS " << fills.size() << " ;\n";
    for (const auto id : fills) {
      const auto& fill = fill_storage.fill(id);
      output << "- LAYER " << names.layerInfo(fill.layer).name;
      if ((fill.flags & DesignFillFlag::kHasMask) != 0u) {
        output << " + MASK " << fill.mask;
      }
      if ((fill.flags & DesignFillFlag::kOpc) != 0u) {
        output << " + OPC";
      }
      for (const auto rectangle : fill.rectangles) {
        output << "\n  RECT ( " << rectangle.ll_x << ' ' << rectangle.ll_y << " ) ( " << rectangle.ur_x << ' ' << rectangle.ur_y << " )";
      }
      output << " ;\n";
    }
    output << "END FILLS\n\n";
  }

  const auto regions = constraints.regions();
  if (!regions.empty()) {
    output << "REGIONS " << regions.size() << " ;\n";
    for (const auto id : regions) {
      const auto& region = constraints.region(id);
      output << "- " << region.name;
      for (const auto rectangle : region.rectangles) {
        output << " ( " << rectangle.ll_x << ' ' << rectangle.ll_y << " ) ( " << rectangle.ur_x << ' ' << rectangle.ur_y << " )";
      }
      output << " + TYPE " << (region.type == DesignRegionType::kFence ? "FENCE" : "GUIDE") << " ;\n";
    }
    output << "END REGIONS\n\n";
  }

  const auto groups = constraints.groups();
  if (!groups.empty()) {
    output << "GROUPS " << groups.size() << " ;\n";
    for (const auto id : groups) {
      const auto& group = constraints.group(id);
      output << "- " << group.name;
      for (const auto instance : group.instances) {
        output << ' ' << netlist.instance(instance).name;
      }
      if ((group.flags & DesignGroupFlag::kHasRegion) != 0u) {
        output << "\n  + REGION " << constraints.region(group.region).name;
      }
      output << " ;\n";
    }
    output << "END GROUPS\n\n";
  }

  writeNetSection(output, _design, names, netlist.specialNets(), "SPECIALNETS");
  writeNetSection(output, _design, names, netlist.regularNets(), "NETS");
  output << "END DESIGN\n";
  output.flush();
  if (!stream) {
    throw std::runtime_error("failed to write DEF output stream");
  }
}

std::string DefDesignExporter::exportText() const
{
  std::ostringstream output;
  write(output);
  return std::move(output).str();
}

void DefDesignExporter::write(const std::filesystem::path& file) const
{
  if (file.empty()) {
    throw std::invalid_argument("DEF output path must not be empty");
  }
  std::ofstream output(file, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("cannot open DEF output file: " + file.string());
  }
  try {
    write(output);
    output.flush();
  } catch (const std::runtime_error&) {
    throw std::runtime_error("failed to write DEF output file: " + file.string());
  }
  if (!output) {
    throw std::runtime_error("failed to write DEF output file: " + file.string());
  }
}

}  // namespace eccdb
