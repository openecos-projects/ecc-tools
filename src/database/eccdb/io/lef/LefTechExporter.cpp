#include "lef/LefTechExporter.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "lef/LefExportFormat.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

template <typename Integer>
[[nodiscard]] bool hasFlag(Integer flags, Integer flag) noexcept
{
  return (flags & flag) != 0;
}

[[nodiscard]] std::string upper(std::string_view value)
{
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
  return result;
}

[[nodiscard]] bool hasProperty(const std::vector<TechProperty>& properties, std::string_view name)
{
  const auto expected = upper(name);
  return std::any_of(properties.begin(), properties.end(), [&](const TechProperty& property) { return upper(property.name) == expected; });
}

[[nodiscard]] const char* routingDirectionName(TechRoutingDirection direction)
{
  switch (direction) {
    case TechRoutingDirection::kHorizontal:
      return "HORIZONTAL";
    case TechRoutingDirection::kVertical:
      return "VERTICAL";
    case TechRoutingDirection::kDiag45:
      return "DIAG45";
    case TechRoutingDirection::kDiag135:
      return "DIAG135";
    case TechRoutingDirection::kUnknown:
      break;
  }
  throw std::invalid_argument("cannot export an unknown ROUTING direction");
}

[[nodiscard]] const char* routingDirectionName(RoutingDirection direction)
{
  switch (direction) {
    case RoutingDirection::kHorizontal:
      return "HORIZONTAL";
    case RoutingDirection::kVertical:
      return "VERTICAL";
    case RoutingDirection::kUnknown:
      break;
  }
  throw std::invalid_argument("cannot export an unknown VIARULE direction");
}

[[nodiscard]] const char* cutSideName(CutLayerSide side)
{
  switch (side) {
    case CutLayerSide::kAbove:
      return "ABOVE";
    case CutLayerSide::kBelow:
      return "BELOW";
    case CutLayerSide::kUnknown:
      break;
  }
  throw std::invalid_argument("cannot export an unknown CUT enclosure side");
}

[[nodiscard]] const char* cutDirectionName(CutDirection direction)
{
  switch (direction) {
    case CutDirection::kHorizontal:
      return "HORIZONTAL";
    case CutDirection::kVertical:
      return "VERTICAL";
    case CutDirection::kUnknown:
      return "";
  }
  return "";
}

[[nodiscard]] const char* cutClassEdgeName(CutClassEdge edge)
{
  switch (edge) {
    case CutClassEdge::kSide:
      return "SIDE";
    case CutClassEdge::kEnd:
      return "END";
    case CutClassEdge::kUnspecified:
      return "";
  }
  return "";
}

[[nodiscard]] const char* mastersliceTypeName(TechMastersliceType type)
{
  switch (type) {
    case TechMastersliceType::kNWell:
      return "NWELL";
    case TechMastersliceType::kPWell:
      return "PWELL";
    case TechMastersliceType::kAboveDieEdge:
      return "ABOVEDIEEDGE";
    case TechMastersliceType::kBelowDieEdge:
      return "BELOWDIEEDGE";
    case TechMastersliceType::kDiffusion:
      return "DIFFUSION";
    case TechMastersliceType::kTrimPoly:
      return "TRIMPOLY";
    case TechMastersliceType::kTrimMetal:
      return "TRIMMETAL";
    case TechMastersliceType::kRegion:
      return "REGION";
    case TechMastersliceType::kNone:
      break;
  }
  throw std::invalid_argument("MASTERSLICE has no LEF58 subtype");
}

[[nodiscard]] const char* lef58LayerTypeName(TechLef58LayerType type)
{
  switch (type) {
    case TechLef58LayerType::kPolyRouting: return "POLYROUTING";
    case TechLef58LayerType::kMimCap: return "MIMCAP";
    case TechLef58LayerType::kTsv: return "TSV";
    case TechLef58LayerType::kPassivation: return "PASSIVATION";
    case TechLef58LayerType::kTrimPoly: return "TRIMPOLY";
    case TechLef58LayerType::kNWell: return "NWELL";
    case TechLef58LayerType::kPWell: return "PWELL";
    case TechLef58LayerType::kBelowDieEdge: return "BELOWDIEEDGE";
    case TechLef58LayerType::kAboveDieEdge: return "ABOVEDIEEDGE";
    case TechLef58LayerType::kDiffusion: return "DIFFUSION";
    case TechLef58LayerType::kTrimMetal: return "TRIMMETAL";
    case TechLef58LayerType::kMeol: return "MEOL";
    case TechLef58LayerType::kPadMetal: return "PADMETAL";
    case TechLef58LayerType::kTsvMetal: return "TSVMETAL";
    case TechLef58LayerType::kStackedMimCap: return "STACKEDMIMCAP";
    case TechLef58LayerType::kSpecialCut: return "SPECIALCUT";
    case TechLef58LayerType::kWellDistance: return "WELLDISTANCE";
    case TechLef58LayerType::kCpode: return "CPODE";
    case TechLef58LayerType::kHighR: return "HIGHR";
    case TechLef58LayerType::kRegion: return "REGION";
    case TechLef58LayerType::kRcBlockage: return "RCBLOCKAGE";
    case TechLef58LayerType::kAbutFiller: return "ABUTFILLER";
    case TechLef58LayerType::kAbutLogic: return "ABUTLOGIC";
    case TechLef58LayerType::kNone: break;
  }
  throw std::invalid_argument("layer has no LEF58_TYPE");
}

[[nodiscard]] const char* routingMinStepTypeName(TechRoutingMinStepType type)
{
  switch (type) {
    case TechRoutingMinStepType::kInsideCorner:
      return "INSIDECORNER";
    case TechRoutingMinStepType::kOutsideCorner:
      return "OUTSIDECORNER";
    case TechRoutingMinStepType::kStep:
      return "STEP";
    case TechRoutingMinStepType::kNone:
      break;
  }
  throw std::invalid_argument("MINSTEP has no type");
}

[[nodiscard]] const char* routingMinimumCutOrientName(TechRoutingMinimumCutOrient orient)
{
  switch (orient) {
    case TechRoutingMinimumCutOrient::kFromAbove:
      return "FROMABOVE";
    case TechRoutingMinimumCutOrient::kFromBelow:
      return "FROMBELOW";
    case TechRoutingMinimumCutOrient::kNone:
      break;
  }
  throw std::invalid_argument("MINIMUMCUT has no orientation");
}

[[nodiscard]] const char* routingCurrentDensityTypeName(TechRoutingCurrentDensityType type)
{
  switch (type) {
    case TechRoutingCurrentDensityType::kPeak:
      return "PEAK";
    case TechRoutingCurrentDensityType::kAverage:
      return "AVERAGE";
    case TechRoutingCurrentDensityType::kRms:
      return "RMS";
    case TechRoutingCurrentDensityType::kUnknown:
      break;
  }
  throw std::invalid_argument("current-density rule has no type");
}

[[nodiscard]] const char* cutCurrentDensityTypeName(TechCutCurrentDensityType type)
{
  switch (type) {
    case TechCutCurrentDensityType::kPeak:
      return "PEAK";
    case TechCutCurrentDensityType::kAverage:
      return "AVERAGE";
    case TechCutCurrentDensityType::kRms:
      return "RMS";
    case TechCutCurrentDensityType::kUnknown:
      break;
  }
  throw std::invalid_argument("current-density rule has no type");
}

class TechLefWriter
{
 public:
  TechLefWriter(std::ostream& output, const TechStore& database, int32_t database_units_per_micron)
      : _output(output), _database(database), _database_units_per_micron(database_units_per_micron)
  {
  }

  void write()
  {
    rejectUnimplementedNonDefaultRules();

    _output << "VERSION 5.8 ;\n\n";
    writeUnits();
    writeManufacturingGrid();
    writeLayers();
    writeViaRuleGenerates();
    writeViaMasters();
    writeViaRules();
    writeNonDefaultRules();
    writeMaxViaStack();
    _output << "END LIBRARY\n";
  }

 private:
  [[nodiscard]] std::string distance(int64_t value) const { return lef_export_detail::distance(value, _database_units_per_micron); }

  [[nodiscard]] std::string area(int64_t value) const { return lef_export_detail::area(value, _database_units_per_micron); }

  void writeUnits()
  {
    const auto& units = _database.globalStorage().getUnits();
    _output << "UNITS\n";
    if (hasFlag(units.flags, TechGlobalUnitsFlag::kHasNanoseconds)) {
      _output << "  TIME NANOSECONDS " << units.nanoseconds << " ;\n";
    }
    if (hasFlag(units.flags, TechGlobalUnitsFlag::kHasPicofarads)) {
      _output << "  CAPACITANCE PICOFARADS " << units.picofarads << " ;\n";
    }
    if (hasFlag(units.flags, TechGlobalUnitsFlag::kHasOhms)) {
      _output << "  RESISTANCE OHMS " << units.ohms << " ;\n";
    }
    if (hasFlag(units.flags, TechGlobalUnitsFlag::kHasMilliwatts)) {
      _output << "  POWER MILLIWATTS " << units.milliwatts << " ;\n";
    }
    if (hasFlag(units.flags, TechGlobalUnitsFlag::kHasMilliamps)) {
      _output << "  CURRENT MILLIAMPS " << units.milliamps << " ;\n";
    }
    if (hasFlag(units.flags, TechGlobalUnitsFlag::kHasVolts)) {
      _output << "  VOLTAGE VOLTS " << units.volts << " ;\n";
    }
    _output << "  DATABASE MICRONS " << units.database_units_per_micron << " ;\n";
    if (hasFlag(units.flags, TechGlobalUnitsFlag::kHasMegahertz)) {
      _output << "  FREQUENCY MEGAHERTZ " << units.megahertz << " ;\n";
    }
    _output << "END UNITS\n\n";
  }

  void writeManufacturingGrid()
  {
    if (!_database.globalStorage().hasManufacturingGrid()) {
      return;
    }
    _output << "MANUFACTURINGGRID " << distance(_database.globalStorage().getManufacturingGrid().value) << " ;\n\n";
  }

  void writeMaxViaStack()
  {
    if (!_database.globalStorage().hasMaxViaStack()) {
      return;
    }
    const auto& stack = _database.globalStorage().getMaxViaStack();
    if (hasFlag(stack.flags, TechMaxViaStackFlag::kNoSingle)) {
      throw std::logic_error("TechMaxViaStack::kNoSingle has no direct LEF representation");
    }
    _output << "MAXVIASTACK " << stack.max_stack_count;
    if (hasFlag(stack.flags, TechMaxViaStackFlag::kHasRange)) {
      _output << " RANGE " << layerName(TechLayerId{stack.bottom_layer.entity()}) << ' '
              << layerName(TechLayerId{stack.top_layer.entity()});
    }
    _output << " ;\n\n";
  }

  void writeLayers()
  {
    for (const auto layer : _database.layerSequence()) {
      writeLayer(layer);
    }

    const auto& registry = _database.techRegistry().registry();
    std::vector<TechOverlapLayerId> overlaps;
    const auto overlap_view = registry.view<const TechLayerInfo, const TechOverlapLayer>();
    overlaps.reserve(overlap_view.size_hint());
    for (const auto entity : overlap_view) {
      overlaps.emplace_back(entity);
    }
    std::sort(overlaps.begin(), overlaps.end(), [&](TechOverlapLayerId lhs, TechOverlapLayerId rhs) {
      return layerName(TechLayerId{lhs.entity()}) < layerName(TechLayerId{rhs.entity()});
    });
    for (const auto overlap : overlaps) {
      writeLayer(TechLayerId{overlap.entity()});
    }

    const auto layer_view = registry.view<const TechLayerInfo>();
    for (const auto entity : layer_view) {
      const auto id = TechLayerId{entity};
      const bool in_sequence = std::any_of(_database.layerSequence().begin(), _database.layerSequence().end(),
                                           [&](TechLayerId sequence_id) { return sequence_id == id; });
      if (!in_sequence && !registry.all_of<TechOverlapLayer>(entity)) {
        throw std::logic_error("a non-OVERLAP technology layer is absent from TechLayerSequence");
      }
    }
  }

  void writeLayer(TechLayerId id)
  {
    const auto& registry = _database.techRegistry().registry();
    const auto& info = _database.layerInfo(id);
    _output << "LAYER " << info.name << "\n";

    if (registry.all_of<TechRoutingLayer>(id.entity())) {
      writeRoutingLayer(TechRoutingLayerId{id.entity()}, info);
    } else if (registry.all_of<TechCutLayer>(id.entity())) {
      writeCutLayer(TechCutLayerId{id.entity()}, info);
    } else if (registry.all_of<TechImplantLayer>(id.entity())) {
      writeImplantLayer(TechImplantLayerId{id.entity()}, info);
    } else if (registry.all_of<TechMastersliceLayer>(id.entity())) {
      writeMastersliceLayer(TechMastersliceLayerId{id.entity()}, info);
    } else if (registry.all_of<TechOverlapLayer>(id.entity())) {
      writeOverlapLayer(info);
    } else {
      throw std::logic_error("technology layer has no recognized layer component");
    }

    _output << "END " << info.name << "\n\n";
  }

  void writeViaRuleGenerates()
  {
    auto rules = _database.viaRuleGenerateStorage().viaRuleGenerates();
    std::sort(rules.begin(), rules.end(), [&](TechViaRuleGenerateId lhs, TechViaRuleGenerateId rhs) {
      return _database.viaRuleGenerateStorage().viaRuleGenerate(lhs).name < _database.viaRuleGenerateStorage().viaRuleGenerate(rhs).name;
    });
    for (const auto id : rules) {
      writeViaRuleGenerate(id);
    }
  }

  void writeViaRuleGenerate(TechViaRuleGenerateId id)
  {
    const auto& storage = _database.viaRuleGenerateStorage();
    const auto& rule = storage.viaRuleGenerate(id);
    const auto& bottom = storage.bottomLayer(id);
    const auto& cut = storage.cutLayer(id);
    const auto& top = storage.topLayer(id);
    _output << "VIARULE " << rule.name << " GENERATE";
    if (rule.isDefault()) {
      _output << " DEFAULT";
    }
    _output << "\n";
    writeViaGenerateRoutingClause(bottom);
    writeViaGenerateRoutingClause(top);
    _output << "  LAYER " << layerName(TechLayerId{cut.layer.entity()}) << " ;\n";
    if (hasFlag(cut.flags, TechViaRuleGenerateCutLayerFlag::kHasRect)) {
      writeRect(cut.cut_rect);
    }
    if (hasFlag(cut.flags, TechViaRuleGenerateCutLayerFlag::kHasSpacing)) {
      _output << "  SPACING " << distance(cut.spacing_x) << " BY " << distance(cut.spacing_y) << " ;\n";
    }
    if (hasFlag(cut.flags, TechViaRuleGenerateCutLayerFlag::kHasResistance)) {
      _output << "  RESISTANCE " << lef_export_detail::number(cut.resistance_per_cut) << " ;\n";
    }
    writeStoredProperties(rule.properties);
    _output << "END " << rule.name << "\n\n";
  }

  void writeViaGenerateRoutingClause(const TechViaRuleGenerateBottomLayer& layer)
  {
    _output << "  LAYER " << conductorLayerName(layer.layer) << " ;\n";
    writeViaGenerateRoutingFields(layer.flags, layer.direction, layer.enclosure_overhang1, layer.enclosure_overhang2, layer.min_width,
                                  layer.max_width, layer.overhang, layer.metal_overhang);
  }

  void writeViaGenerateRoutingClause(const TechViaRuleGenerateTopLayer& layer)
  {
    _output << "  LAYER " << conductorLayerName(layer.layer) << " ;\n";
    writeViaGenerateRoutingFields(layer.flags, layer.direction, layer.enclosure_overhang1, layer.enclosure_overhang2, layer.min_width,
                                  layer.max_width, layer.overhang, layer.metal_overhang);
  }

  void writeViaGenerateRoutingFields(uint32_t flags, TechRoutingDirection direction, int32_t enclosure_overhang1,
                                     int32_t enclosure_overhang2, int32_t min_width, int32_t max_width, int32_t overhang,
                                     int32_t metal_overhang)
  {
    if (hasFlag(flags, TechViaRuleGenerateRoutingLayerFlag::kHasDirection)) {
      _output << "  DIRECTION " << routingDirectionName(direction) << " ;\n";
    }
    if (hasFlag(flags, TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure)) {
      _output << "  ENCLOSURE " << distance(enclosure_overhang1) << ' ' << distance(enclosure_overhang2) << " ;\n";
    }
    if (hasFlag(flags, TechViaRuleGenerateRoutingLayerFlag::kHasWidth)) {
      _output << "  WIDTH " << distance(min_width) << " TO " << distance(max_width) << " ;\n";
    }
    if (hasFlag(flags, TechViaRuleGenerateRoutingLayerFlag::kHasOverhang)) {
      _output << "  OVERHANG " << distance(overhang) << " ;\n";
    }
    if (hasFlag(flags, TechViaRuleGenerateRoutingLayerFlag::kHasMetalOverhang)) {
      _output << "  METALOVERHANG " << distance(metal_overhang) << " ;\n";
    }
  }

  void writeViaMasters()
  {
    const auto& storage = _database.viaMasterStorage();
    auto fixed = storage.fixedViaMasters();
    auto generated = storage.generatedViaMasters();
    const auto compare
        = [&](TechViaMasterId lhs, TechViaMasterId rhs) { return storage.viaMaster(lhs).name < storage.viaMaster(rhs).name; };
    std::sort(fixed.begin(), fixed.end(), compare);
    std::sort(generated.begin(), generated.end(), compare);
    for (const auto id : fixed) {
      writeViaMaster(id);
    }
    for (const auto id : generated) {
      writeViaMaster(id);
    }
  }

  void writeViaMaster(TechViaMasterId id)
  {
    const auto& storage = _database.viaMasterStorage();
    const auto& master = storage.viaMaster(id);
    _output << "VIA " << master.name;
    if (hasFlag(master.flags, TechViaMasterFlag::kDefault)) {
      _output << " DEFAULT";
    }
    _output << "\n";
    writeViaDefinition(master, storage.geometry(id), storage.hasGeneratedViaMaster(id) ? &storage.generatedViaMaster(id) : nullptr);
  }

  void writeViaDefinition(const TechViaMaster& master, const TechViaGeometry& geometry, const TechGeneratedViaMaster* generated)
  {
    if (hasFlag(master.flags, TechViaMasterFlag::kTopOfStackOnly)) {
      _output << "  TOPOFSTACKONLY ;\n";
    }
    if (hasFlag(master.flags, TechViaMasterFlag::kHasResistance)) {
      _output << "  RESISTANCE " << lef_export_detail::number(master.resistance) << " ;\n";
    }
    if (generated != nullptr) {
      writeGeneratedViaFormula(geometry, *generated);
    } else {
      writeFixedViaGeometry(geometry);
    }
    writeStoredProperties(master.properties);
    _output << "END " << master.name << "\n\n";
  }

  void writeGeneratedViaFormula(const TechViaGeometry& geometry, const TechGeneratedViaMaster& generated)
  {
    if (!generated.via_rule_generate) {
      throw std::logic_error("generated VIA has no VIARULE GENERATE reference");
    }
    const auto& rule = _database.viaRuleGenerateStorage().viaRuleGenerate(generated.via_rule_generate);
    _output << "  VIARULE " << rule.name << " ;\n";
    _output << "  CUTSIZE " << distance(generated.cut_size_x) << ' ' << distance(generated.cut_size_y) << " ;\n";
    _output << "  LAYERS " << conductorLayerName(geometry.bottom_layer) << ' ' << layerName(TechLayerId{geometry.cut_layer.entity()}) << ' '
            << conductorLayerName(geometry.top_layer) << " ;\n";
    _output << "  CUTSPACING " << distance(generated.cut_spacing_x) << ' ' << distance(generated.cut_spacing_y) << " ;\n";
    _output << "  ENCLOSURE " << distance(generated.bottom_enclosure_x) << ' ' << distance(generated.bottom_enclosure_y) << ' '
            << distance(generated.top_enclosure_x) << ' ' << distance(generated.top_enclosure_y) << " ;\n";
    if (hasFlag(generated.flags, TechGeneratedViaMasterFlag::kHasRowCol)) {
      _output << "  ROWCOL " << generated.row_count << ' ' << generated.column_count << " ;\n";
    }
    if (hasFlag(generated.flags, TechGeneratedViaMasterFlag::kHasOrigin)) {
      _output << "  ORIGIN " << distance(generated.origin_x) << ' ' << distance(generated.origin_y) << " ;\n";
    }
    if (hasFlag(generated.flags, TechGeneratedViaMasterFlag::kHasOffset)) {
      _output << "  OFFSET " << distance(generated.bottom_offset_x) << ' ' << distance(generated.bottom_offset_y) << ' '
              << distance(generated.top_offset_x) << ' ' << distance(generated.top_offset_y) << " ;\n";
    }
    if (hasFlag(generated.flags, TechGeneratedViaMasterFlag::kHasPattern)) {
      if (generated.pattern.empty()) {
        throw std::logic_error("generated VIA PATTERN flag has no pattern");
      }
      _output << "  PATTERN " << generated.pattern << " ;\n";
    }
  }

  void writeFixedViaGeometry(const TechViaGeometry& geometry)
  {
    writeViaGeometryLayer(conductorLayerName(geometry.bottom_layer), geometry.bottom_geometry);
    writeViaGeometryLayer(layerName(TechLayerId{geometry.cut_layer.entity()}), geometry.cut_geometry);
    writeViaGeometryLayer(conductorLayerName(geometry.top_layer), geometry.top_geometry);
  }

  void writeViaGeometryLayer(std::string_view layer, GeometryHandle geometry)
  {
    _output << "  LAYER " << layer << " ;\n";
    for (const auto rect : _database.geometryPool().rectangles(geometry)) {
      writeRect(rect);
    }
    for (uint32_t index = 0; index < _database.geometryPool().polygonCount(geometry); ++index) {
      writePolygon(_database.geometryPool().polygonPoints(geometry, index));
    }
  }

  void writeRect(Rect rect)
  {
    _output << "  RECT " << distance(rect.ll_x) << ' ' << distance(rect.ll_y) << ' ' << distance(rect.ur_x) << ' ' << distance(rect.ur_y)
            << " ;\n";
  }

  void writePolygon(std::span<const Point> points)
  {
    if (points.size() < 3u) {
      throw std::logic_error("VIA polygon has fewer than three points");
    }
    _output << "  POLYGON";
    for (const auto point : points) {
      _output << ' ' << distance(point.x) << ' ' << distance(point.y);
    }
    _output << " ;\n";
  }

  void writeViaRules()
  {
    auto rules = _database.viaRuleStorage().viaRules();
    std::sort(rules.begin(), rules.end(), [&](TechViaRuleId lhs, TechViaRuleId rhs) {
      return _database.viaRuleStorage().viaRule(lhs).name < _database.viaRuleStorage().viaRule(rhs).name;
    });
    for (const auto id : rules) {
      const auto& storage = _database.viaRuleStorage();
      const auto& rule = storage.viaRule(id);
      _output << "VIARULE " << rule.name << "\n";
      writeOrdinaryViaRuleLayer(storage.lowerLayer(id));
      writeOrdinaryViaRuleLayer(storage.upperLayer(id));
      for (const auto via : storage.candidates(id)) {
        _output << "  VIA " << _database.viaMasterStorage().viaMaster(via).name << " ;\n";
      }
      for (const auto& property : storage.properties(id)) {
        writeProperty(property.name, property.value);
      }
      _output << "END " << rule.name << "\n\n";
    }
  }

  void writeOrdinaryViaRuleLayer(const TechViaRuleLowerLayer& layer)
  {
    _output << "  LAYER " << layerName(TechLayerId{layer.layer.entity()}) << " ;\n";
    _output << "  DIRECTION " << routingDirectionName(layer.direction) << " ;\n";
    if (hasFlag(layer.flags, TechViaRuleLayerFlag::kHasWidth)) {
      _output << "  WIDTH " << distance(layer.min_width) << " TO " << distance(layer.max_width) << " ;\n";
    }
  }

  void writeOrdinaryViaRuleLayer(const TechViaRuleUpperLayer& layer)
  {
    _output << "  LAYER " << layerName(TechLayerId{layer.layer.entity()}) << " ;\n";
    _output << "  DIRECTION " << routingDirectionName(layer.direction) << " ;\n";
    if (hasFlag(layer.flags, TechViaRuleLayerFlag::kHasWidth)) {
      _output << "  WIDTH " << distance(layer.min_width) << " TO " << distance(layer.max_width) << " ;\n";
    }
  }

  void writeNonDefaultRules()
  {
    const auto& storage = _database.nonDefaultRuleStorage();
    auto rules = storage.nonDefaultRules();
    std::sort(rules.begin(), rules.end(), [&](TechNonDefaultRuleId lhs, TechNonDefaultRuleId rhs) {
      return storage.nonDefaultRule(lhs).name < storage.nonDefaultRule(rhs).name;
    });
    for (const auto id : rules) {
      writeNonDefaultRule(id);
    }
  }

  void writeNonDefaultRule(TechNonDefaultRuleId id)
  {
    const auto& storage = _database.nonDefaultRuleStorage();
    const auto& rule = storage.nonDefaultRule(id);
    _output << "NONDEFAULTRULE " << rule.name << "\n";
    if (rule.isHardSpacing()) {
      _output << "  HARDSPACING ;\n";
    }

    for (const auto& routing_rule : storage.routingRules(id)) {
      writeNdrRoutingRule(routing_rule);
    }
    for (const auto via : storage.viaDefinitions(id)) {
      writeNdrViaDefinition(via);
    }
    for (const auto use_via : storage.useVias(id)) {
      _output << "  USEVIA " << _database.viaMasterStorage().viaMaster(use_via).name << " ;\n";
    }
    for (const auto use_via_rule : storage.useViaRules(id)) {
      _output << "  USEVIARULE " << _database.viaRuleGenerateStorage().viaRuleGenerate(use_via_rule).name << " ;\n";
    }
    for (const auto& cut_rule : storage.minCutsRules(id)) {
      _output << "  MINCUTS " << layerName(TechLayerId{cut_rule.layer.entity()}) << ' ' << cut_rule.cut_count << " ;\n";
    }
    for (const auto& property : storage.properties(id)) {
      writeProperty(property.name, property.value);
    }
    _output << "END " << rule.name << "\n\n";
  }

  void writeNdrRoutingRule(const TechNdrRoutingRule& rule)
  {
    const auto layer = layerName(TechLayerId{rule.layer.entity()});
    _output << "  LAYER " << layer << "\n";
    _output << "    WIDTH " << distance(rule.width) << " ;\n";
    if (hasFlag(rule.flags, TechNdrRoutingRuleFlag::kHasDiagWidth)) {
      _output << "    DIAGWIDTH " << distance(rule.diag_width) << " ;\n";
    }
    if (hasFlag(rule.flags, TechNdrRoutingRuleFlag::kHasSpacing)) {
      _output << "    SPACING " << distance(rule.spacing) << " ;\n";
    }
    if (hasFlag(rule.flags, TechNdrRoutingRuleFlag::kHasWireExtension)) {
      _output << "    WIREEXTENSION " << distance(rule.wire_extension) << " ;\n";
    }
    if (hasFlag(rule.flags, TechNdrRoutingRuleFlag::kHasResistance)) {
      _output << "    RESISTANCE RPERSQ " << lef_export_detail::number(rule.resistance) << " ;\n";
    }
    if (hasFlag(rule.flags, TechNdrRoutingRuleFlag::kHasCapacitance)) {
      _output << "    CAPACITANCE CPERSQDIST " << lef_export_detail::number(rule.capacitance) << " ;\n";
    }
    if (hasFlag(rule.flags, TechNdrRoutingRuleFlag::kHasEdgeCapacitance)) {
      _output << "    EDGECAPACITANCE " << lef_export_detail::number(rule.edge_capacitance) << " ;\n";
    }
    _output << "  END " << layer << "\n";
  }

  void writeNdrViaDefinition(TechViaMasterId id)
  {
    const auto& storage = _database.nonDefaultRuleStorage();
    const auto& master = storage.viaDefinition(id);
    _output << "  VIA " << master.name;
    if (hasFlag(master.flags, TechViaMasterFlag::kDefault)) {
      _output << " DEFAULT";
    }
    _output << "\n";
    writeViaDefinition(master, storage.viaDefinitionGeometry(id), storage.generatedViaDefinition(id));
  }

  void writeLayerPrefix(const TechLayerInfo& info, std::string_view type)
  {
    _output << "  TYPE " << type << " ;\n";
    if (info.mask_count != 1u) {
      _output << "  MASK " << info.mask_count << " ;\n";
    }
  }

  void writeRoutingLayer(TechRoutingLayerId id, const TechLayerInfo& info)
  {
    const auto& storage = _database.routingLayerStorage();
    const auto& layer = storage.routingLayer(id);
    const auto& properties = _database.layerProperties(TechLayerId{id.entity()});
    writeLayerPrefix(info, "ROUTING");

    if (layer.direction != TechRoutingDirection::kUnknown) {
      _output << "  DIRECTION " << routingDirectionName(layer.direction) << " ;\n";
    }
    writeRoutingAxes(layer);
    writeRoutingScalars(layer);
    writeRoutingNativeRules(id, properties);
    requireRoutingLef58Properties(id, properties);
    writeLayerProperties(info, properties);
  }

  void writeRoutingAxes(const TechRoutingLayer& layer)
  {
    if (layer.pitch_form == TechRoutingAxisValueForm::kBothXY) {
      _output << "  PITCH " << distance(layer.pitch_x) << " ;\n";
    } else if (layer.pitch_form == TechRoutingAxisValueForm::kSeparateXY) {
      _output << "  PITCH " << distance(layer.pitch_x) << ' ' << distance(layer.pitch_y) << " ;\n";
    }

    if (layer.offset_form == TechRoutingAxisValueForm::kBothXY) {
      _output << "  OFFSET " << distance(layer.offset_x) << " ;\n";
    } else if (layer.offset_form == TechRoutingAxisValueForm::kSeparateXY) {
      _output << "  OFFSET " << distance(layer.offset_x) << ' ' << distance(layer.offset_y) << " ;\n";
    }
  }

  void writeRoutingScalars(const TechRoutingLayer& layer)
  {
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasWidth)) {
      _output << "  WIDTH " << distance(layer.width) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasMinWidth)) {
      _output << "  MINWIDTH " << distance(layer.min_width) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasMaxWidth)) {
      _output << "  MAXWIDTH " << distance(layer.max_width) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasDiagWidth)) {
      _output << "  DIAGWIDTH " << distance(layer.diag_width) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasDiagSpacing)) {
      _output << "  DIAGSPACING " << distance(layer.diag_spacing) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasWireExtension)) {
      _output << "  WIREEXTENSION " << distance(layer.wire_extension) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasThickness)) {
      _output << "  THICKNESS " << distance(layer.thickness) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasHeight)) {
      _output << "  HEIGHT " << distance(layer.height) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasShrinkage)) {
      _output << "  SHRINKAGE " << distance(layer.shrinkage) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasCapMultiplier)) {
      _output << "  CAPMULTIPLIER " << lef_export_detail::number(layer.cap_multiplier) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasFillActiveSpacing)) {
      _output << "  FILLACTIVESPACING " << distance(layer.fill_active_spacing) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasArea)) {
      _output << "  AREA " << area(layer.area) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasResistance)) {
      _output << "  RESISTANCE RPERSQ " << lef_export_detail::number(layer.resistance) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasCapacitance)) {
      _output << "  CAPACITANCE CPERSQDIST " << lef_export_detail::number(layer.capacitance) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasEdgeCapacitance)) {
      _output << "  EDGECAPACITANCE " << lef_export_detail::number(layer.edge_capacitance) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasMinDensity)) {
      _output << "  MINIMUMDENSITY " << lef_export_detail::number(layer.min_density) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasMaxDensity)) {
      _output << "  MAXIMUMDENSITY " << lef_export_detail::number(layer.max_density) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasDensityCheckWindow)) {
      _output << "  DENSITYCHECKWINDOW " << distance(layer.density_check_length) << ' ' << distance(layer.density_check_width) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasDensityCheckStep)) {
      _output << "  DENSITYCHECKSTEP " << distance(layer.density_check_step) << " ;\n";
    }
    if (hasFlag(layer.flags, TechRoutingLayerFlag::kHasProtrusion)) {
      _output << "  PROTRUSION " << distance(layer.protrusion_width1) << ' ' << distance(layer.protrusion_length) << ' '
              << distance(layer.protrusion_width2) << " ;\n";
    }

    constexpr uint64_t unsupported
        = TechRoutingLayerFlag::kHasDensity | TechRoutingLayerFlag::kHasMinCut | TechRoutingLayerFlag::kHasPowerSegmentWidth;
    if (hasFlag(layer.flags, unsupported)) {
      throw std::logic_error("ROUTING scalar field is not representable by the canonical LEF exporter yet");
    }
  }

  void writeRoutingNativeRules(TechRoutingLayerId owner, const std::vector<TechProperty>& properties)
  {
    const auto& storage = _database.routingLayerStorage();
    const auto notch_ids = storage.spacingNotchLengthRules(owner);
    for (const auto id : storage.spacingRules(owner)) {
      const auto& rule = storage.rule(id);
      _output << "  SPACING " << distance(rule.min_spacing);
      if (rule.type == TechRoutingSpacingType::kRange) {
        _output << " RANGE " << distance(rule.min_width) << ' ' << distance(rule.max_width);
      }
      _output << " ;\n";
    }
    for (const auto id : storage.endOfLineSpacingRules(owner)) {
      const auto& rule = storage.rule(id);
      _output << "  SPACING " << distance(rule.min_spacing) << " ENDOFLINE " << distance(rule.eol_width) << " WITHIN "
              << distance(rule.eol_within);
      if (hasFlag(rule.flags, TechRoutingEndOfLineSpacingRuleFlag::kHasParallelEdge)) {
        _output << " PARALLELEDGE " << distance(rule.parallel_space) << " WITHIN " << distance(rule.parallel_within);
        if (hasFlag(rule.flags, TechRoutingEndOfLineSpacingRuleFlag::kTwoEdges)) {
          _output << " TWOEDGES";
        }
      }
      _output << " ;\n";
    }
    for (const auto id : notch_ids) {
      const auto& rule = storage.rule(id);
      _output << "  SPACING " << distance(rule.min_spacing) << " NOTCHLENGTH " << distance(rule.notch_length) << " ;\n";
    }
    for (const auto id : storage.minEncloseAreaRules(owner)) {
      const auto& rule = storage.rule(id);
      _output << "  MINENCLOSEDAREA " << area(rule.area);
      if (rule.width >= 0) {
        _output << " WIDTH " << distance(rule.width);
      }
      _output << " ;\n";
    }
    for (const auto id : storage.minStepRules(owner)) {
      const auto& rule = storage.rule(id);
      _output << "  MINSTEP " << distance(rule.min_step_length);
      if (rule.type != TechRoutingMinStepType::kNone) {
        _output << ' ' << routingMinStepTypeName(rule.type);
      }
      if (hasFlag(rule.flags, TechRoutingMinStepRuleFlag::kHasMaxLengthSum)) {
        _output << " LENGTHSUM " << distance(rule.max_length_sum);
      }
      if (hasFlag(rule.flags, TechRoutingMinStepRuleFlag::kHasMaxEdges)) {
        _output << " MAXEDGES " << rule.max_edges;
      }
      _output << " ;\n";
    }
    for (const auto id : storage.minimumCutRules(owner)) {
      const auto& rule = storage.rule(id);
      _output << "  MINIMUMCUT " << rule.num_cuts << " WIDTH " << distance(rule.width);
      if (hasFlag(rule.flags, TechRoutingMinimumCutRuleFlag::kHasWithinCutDistance)) {
        _output << " WITHIN " << distance(rule.within_cut_distance);
      }
      if (rule.orient != TechRoutingMinimumCutOrient::kNone) {
        _output << ' ' << routingMinimumCutOrientName(rule.orient);
      }
      if (hasFlag(rule.flags, TechRoutingMinimumCutRuleFlag::kHasLength)) {
        _output << " LENGTH " << distance(rule.length) << " WITHIN " << distance(rule.length_distance);
      }
      _output << " ;\n";
    }
    // LEF58_SPACINGTABLE is preserved as raw PROPERTY text. The current model
    // does not retain an origin bit for a PRL row, so mixed native and LEF58
    // tables on one layer are intentionally rejected instead of guessing.
    const bool has_lef58_spacing_table = hasProperty(properties, "LEF58_SPACINGTABLE");
    const bool has_native_tables = !storage.prlSpacingTableRules(owner).empty() || !storage.influenceSpacingTableRules(owner).empty()
                                   || !storage.twoWidthsSpacingTableRules(owner).empty();
    if (has_lef58_spacing_table && has_native_tables) {
      throw std::logic_error("cannot distinguish native and LEF58 SPACINGTABLE rows on one ROUTING layer");
    }
    if (!has_lef58_spacing_table) {
      for (const auto id : storage.prlSpacingTableRules(owner)) {
        writePrlSpacingTable(storage.rule(id));
      }
      for (const auto id : storage.influenceSpacingTableRules(owner)) {
        writeInfluenceSpacingTable(storage.rule(id));
      }
      for (const auto id : storage.twoWidthsSpacingTableRules(owner)) {
        writeTwoWidthsSpacingTable(storage.rule(id));
      }
    }
    for (const auto id : storage.currentDensityRules(owner)) {
      writeRoutingCurrentDensity(storage.rule(id));
    }
  }

  void writeRoutingCurrentDensity(const TechRoutingCurrentDensityRule& rule)
  {
    _output << "  " << (rule.signal == TechRoutingCurrentDensitySignal::kAc ? "ACCURRENTDENSITY" : "DCCURRENTDENSITY") << ' '
            << routingCurrentDensityTypeName(rule.type);
    if (rule.signal == TechRoutingCurrentDensitySignal::kDc && rule.type != TechRoutingCurrentDensityType::kAverage) {
      throw std::logic_error("DCCURRENTDENSITY requires AVERAGE type");
    }
    if (hasFlag(rule.flags, TechRoutingCurrentDensityRuleFlag::kHasScalar)) {
      _output << ' ' << lef_export_detail::number(rule.scalar) << " ;\n";
      return;
    }
    _output << "\n";
    if (!rule.frequencies.empty()) {
      _output << "    FREQUENCY";
      for (const auto frequency : rule.frequencies) {
        _output << ' ' << lef_export_detail::number(frequency);
      }
      _output << " ;\n";
    }
    if (!rule.widths.empty()) {
      _output << "    WIDTH";
      for (const auto width : rule.widths) {
        _output << ' ' << distance(width);
      }
      _output << " ;\n";
    }
    if (rule.table_entries.empty()) {
      throw std::logic_error("ROUTING CURRENTDENSITY table has no entries");
    }
    _output << "    TABLEENTRIES";
    for (const auto entry : rule.table_entries) {
      _output << ' ' << lef_export_detail::number(entry);
    }
    _output << " ;\n";
  }

  void writePrlSpacingTable(const TechRoutingPrlSpacingTableRule& rule)
  {
    if (rule.flags != 0u || !rule.except_withins.empty() || !rule.influences.empty()) {
      throw std::logic_error("extended ROUTING PRL SPACINGTABLE requires LEF58 PROPERTY preservation");
    }
    _output << "  SPACINGTABLE\n    PARALLELRUNLENGTH";
    for (const auto length : rule.parallel_run_lengths) {
      _output << ' ' << distance(length);
    }
    _output << "\n";
    const auto length_count = rule.parallel_run_lengths.size();
    for (size_t row = 0; row < rule.widths.size(); ++row) {
      _output << "      WIDTH " << distance(rule.widths[row]);
      for (size_t column = 0; column < length_count; ++column) {
        _output << ' ' << distance(rule.cells[row * length_count + column]);
      }
      _output << "\n";
    }
    _output << "    ;\n";
  }

  void writeInfluenceSpacingTable(const TechRoutingInfluenceSpacingTableRule& rule)
  {
    _output << "  SPACINGTABLE\n    INFLUENCE\n";
    for (const auto& entry : rule.entries) {
      _output << "      WIDTH " << distance(entry.width) << " WITHIN " << distance(entry.within) << " SPACING " << distance(entry.spacing)
              << "\n";
    }
    _output << "    ;\n";
  }

  void writeTwoWidthsSpacingTable(const TechRoutingTwoWidthsSpacingTableRule& rule)
  {
    _output << "  SPACINGTABLE\n    TWOWIDTHS\n";
    const auto width_count = rule.widths.size();
    for (size_t row = 0; row < width_count; ++row) {
      const auto& axis = rule.widths[row];
      _output << "      WIDTH " << distance(axis.width);
      if (axis.has_prl) {
        _output << " PRL " << distance(axis.prl);
      }
      for (size_t column = 0; column < width_count; ++column) {
        _output << ' ' << distance(rule.cells[row * width_count + column]);
      }
      _output << "\n";
    }
    _output << "    ;\n";
  }

  void requireRoutingLef58Properties(TechRoutingLayerId owner, const std::vector<TechProperty>& properties) const
  {
    const auto& storage = _database.routingLayerStorage();
    const auto& layer = storage.routingLayer(owner);
    if ((hasFlag(layer.flags, TechRoutingLayerFlag::kLef58RectOnly)
         || hasFlag(layer.flags, TechRoutingLayerFlag::kLef58RectOnlyExceptNonCorePins))
        && !hasProperty(properties, "LEF58_RECTONLY")) {
      throw std::logic_error("LEF58_RECTONLY component has no preserved PROPERTY text");
    }
    if ((hasFlag(layer.flags, TechRoutingLayerFlag::kLef58RightWayOnGridOnly)
         || hasFlag(layer.flags, TechRoutingLayerFlag::kLef58RightWayOnGridOnlyCheckMask))
        && !hasProperty(properties, "LEF58_RIGHTWAYONGRIDONLY")) {
      throw std::logic_error("LEF58_RIGHTWAYONGRIDONLY component has no preserved PROPERTY text");
    }
    if (!storage.lef58AreaRules(owner).empty() && !hasProperty(properties, "LEF58_AREA")) {
      throw std::logic_error("LEF58_AREA components have no preserved PROPERTY text");
    }
    if (!storage.lef58CornerFillSpacingRules(owner).empty() && !hasProperty(properties, "LEF58_CORNERFILLSPACING")) {
      throw std::logic_error("LEF58_CORNERFILLSPACING components have no preserved PROPERTY text");
    }
    if (!storage.lef58CornerSpacingRules(owner).empty() && !hasProperty(properties, "LEF58_CORNERSPACING")) {
      throw std::logic_error("LEF58_CORNERSPACING components have no preserved PROPERTY text");
    }
    if (!storage.lef58MinimumCutRules(owner).empty() && !hasProperty(properties, "LEF58_MINIMUMCUT")) {
      throw std::logic_error("LEF58_MINIMUMCUT components have no preserved PROPERTY text");
    }
    if (!storage.lef58MinStepRules(owner).empty() && !hasProperty(properties, "LEF58_MINSTEP")) {
      throw std::logic_error("LEF58_MINSTEP components have no preserved PROPERTY text");
    }
    if (!storage.lef58WidthTableRules(owner).empty() && !hasProperty(properties, "LEF58_WIDTHTABLE")) {
      throw std::logic_error("LEF58_WIDTHTABLE components have no preserved PROPERTY text");
    }
    if ((!storage.lef58SpacingEolRules(owner).empty() || !storage.lef58SpacingNotchLengthRules(owner).empty())
        && !hasProperty(properties, "LEF58_SPACING")) {
      throw std::logic_error("LEF58_SPACING components have no preserved PROPERTY text");
    }
    if (!storage.lef58SpacingTableJogToJogRules(owner).empty() && !hasProperty(properties, "LEF58_SPACINGTABLE")) {
      throw std::logic_error("LEF58_SPACINGTABLE components have no preserved PROPERTY text");
    }
  }

  void writeCutLayer(TechCutLayerId id, const TechLayerInfo& info)
  {
    const auto& storage = _database.cutLayerStorage();
    const auto& layer = storage.cutLayer(id);
    const auto& properties = _database.layerProperties(TechLayerId{id.entity()});
    writeLayerPrefix(info, "CUT");
    if (hasFlag(layer.flags, TechCutLayerFlag::kHasWidth)) {
      _output << "  WIDTH " << distance(layer.width) << " ;\n";
    }
    if (hasFlag(layer.flags, TechCutLayerFlag::kHasResistance)) {
      _output << "  RESISTANCE " << lef_export_detail::number(layer.resistance_per_cut) << " ;\n";
    }
    writeCutNativeRules(id);
    requireCutLef58Properties(id, properties);
    writeSynthesizedCutLef58Properties(id, properties);
    writeLayerProperties(info, properties);
  }

  void writeCutNativeRules(TechCutLayerId owner)
  {
    const auto& storage = _database.cutLayerStorage();
    for (const auto id : storage.spacingRules(owner)) {
      const auto& rule = storage.spacingRule(id);
      _output << "  SPACING " << distance(rule.spacing);
      if (hasFlag(rule.flags, TechCutSpacingRuleFlag::kCenterToCenter)) {
        _output << " CENTERTOCENTER";
      }
      if (hasFlag(rule.flags, TechCutSpacingRuleFlag::kSameNet)) {
        _output << " SAMENET";
        if (hasFlag(rule.flags, TechCutSpacingRuleFlag::kSameNetPgOnly)) {
          _output << " PGONLY";
        }
      }
      if (hasFlag(rule.flags, TechCutSpacingRuleFlag::kHasSecondLayer)) {
        if (rule.second_layer_name.empty()) {
          throw std::logic_error("CUT SPACING LAYER has an empty layer name");
        }
        _output << " LAYER " << rule.second_layer_name;
        if (hasFlag(rule.flags, TechCutSpacingRuleFlag::kStack)) {
          _output << " STACK";
        }
      }
      if (hasFlag(rule.flags, TechCutSpacingRuleFlag::kHasAdjacentCuts)) {
        _output << " ADJACENTCUTS " << rule.adjacent_cut_count << " WITHIN " << distance(rule.adjacent_cut_within);
        if (hasFlag(rule.flags, TechCutSpacingRuleFlag::kExceptSamePgNet)) {
          _output << " EXCEPTSAMEPGNET";
        }
      }
      if (hasFlag(rule.flags, TechCutSpacingRuleFlag::kParallelOverlap)) {
        _output << " PARALLELOVERLAP";
      }
      if (hasFlag(rule.flags, TechCutSpacingRuleFlag::kHasCutArea)) {
        _output << " AREA " << area(rule.cut_area);
      }
      _output << " ;\n";
    }
    for (const auto id : storage.enclosureRules(owner)) {
      const auto& rule = storage.enclosureRule(id);
      _output << "  ENCLOSURE";
      if (rule.side != CutLayerSide::kUnknown) {
        _output << ' ' << cutSideName(rule.side);
      }
      _output << ' ' << distance(rule.overhang1) << ' ' << distance(rule.overhang2);
      if (hasFlag(rule.flags, TechCutEnclosureRuleFlag::kHasMinWidth)) {
        _output << " WIDTH " << distance(rule.min_width);
        if (hasFlag(rule.flags, TechCutEnclosureRuleFlag::kExceptExtraCut)) {
          if (!hasFlag(rule.flags, TechCutEnclosureRuleFlag::kHasCutWithin)) {
            throw std::logic_error("CUT ENCLOSURE EXCEPTEXTRACUT has no cut-within distance");
          }
          _output << " EXCEPTEXTRACUT " << distance(rule.cut_within);
        }
      } else if (hasFlag(rule.flags, TechCutEnclosureRuleFlag::kHasMinLength)) {
        _output << " LENGTH " << distance(rule.min_length);
      }
      _output << " ;\n";
    }
    if (const auto id = storage.arraySpacingRule(owner); id) {
      const auto& rule = storage.arraySpacingRule(id);
      _output << "  ARRAYSPACING";
      if (hasFlag(rule.flags, TechCutArraySpacingRuleFlag::kLongArray)) {
        _output << " LONGARRAY";
      }
      if (hasFlag(rule.flags, TechCutArraySpacingRuleFlag::kHasViaWidth)) {
        _output << " WIDTH " << distance(rule.via_width);
      }
      _output << "\n    CUTSPACING " << distance(rule.cut_spacing) << "\n";
      for (const auto& item : rule.items) {
        _output << "    ARRAYCUTS " << item.array_cut_count << " SPACING " << distance(item.spacing) << "\n";
      }
      _output << "  ;\n";
    }
    for (const auto id : storage.orthogonalSpacingTableRules(owner)) {
      const auto& rule = storage.orthogonalSpacingTableRule(id);
      if (hasFlag(rule.flags, TechCutOrthogonalSpacingTableRuleFlag::kLef58Property)) {
        continue;
      }
      _output << "  SPACINGTABLE ORTHOGONAL\n";
      for (const auto& item : rule.items) {
        _output << "    WITHIN " << distance(item.within) << " SPACING " << distance(item.spacing) << "\n";
      }
      _output << "  ;\n";
    }
    for (const auto id : storage.currentDensityRules(owner)) {
      writeCutCurrentDensity(storage.currentDensityRule(id));
    }
  }

  void writeCutCurrentDensity(const TechCutCurrentDensityRule& rule)
  {
    _output << "  " << (rule.signal == TechCutCurrentDensitySignal::kAc ? "ACCURRENTDENSITY" : "DCCURRENTDENSITY") << ' '
            << cutCurrentDensityTypeName(rule.type);
    if (rule.signal == TechCutCurrentDensitySignal::kDc && rule.type != TechCutCurrentDensityType::kAverage) {
      throw std::logic_error("DCCURRENTDENSITY requires AVERAGE type");
    }
    if (hasFlag(rule.flags, TechCutCurrentDensityRuleFlag::kHasScalar)) {
      _output << ' ' << lef_export_detail::number(rule.scalar) << " ;\n";
      return;
    }
    _output << "\n";
    if (!rule.frequencies.empty()) {
      _output << "    FREQUENCY";
      for (const auto frequency : rule.frequencies) {
        _output << ' ' << lef_export_detail::number(frequency);
      }
      _output << " ;\n";
    }
    if (!rule.cut_areas.empty()) {
      _output << "    CUTAREA";
      for (const auto cut_area : rule.cut_areas) {
        _output << ' ' << area(cut_area);
      }
      _output << " ;\n";
    }
    if (rule.table_entries.empty()) {
      throw std::logic_error("CUT CURRENTDENSITY table has no entries");
    }
    _output << "    TABLEENTRIES";
    for (const auto entry : rule.table_entries) {
      _output << ' ' << lef_export_detail::number(entry);
    }
    _output << " ;\n";
  }

  void requireCutLef58Properties(TechCutLayerId owner, const std::vector<TechProperty>& properties) const
  {
    const auto& storage = _database.cutLayerStorage();
    if (!storage.lef58CutClassRules(owner).empty() && !hasProperty(properties, "LEF58_CUTCLASS")) {
      throw std::logic_error("LEF58_CUTCLASS components have no preserved PROPERTY text");
    }
    if (!storage.lef58EnclosureEdgeRules(owner).empty() && !hasProperty(properties, "LEF58_ENCLOSUREEDGE")) {
      throw std::logic_error("LEF58_ENCLOSUREEDGE components have no preserved PROPERTY text");
    }
    if (storage.lef58EolEnclosureRule(owner) && !hasProperty(properties, "LEF58_EOLENCLOSURE")) {
      throw std::logic_error("LEF58_EOLENCLOSURE component has no preserved PROPERTY text");
    }
    if (storage.lef58EolSpacingRule(owner) && !hasProperty(properties, "LEF58_EOLSPACING")) {
      throw std::logic_error("LEF58_EOLSPACING component has no preserved PROPERTY text");
    }
  }

  void writeSynthesizedCutLef58Properties(TechCutLayerId owner, const std::vector<TechProperty>& properties)
  {
    const auto& storage = _database.cutLayerStorage();
    if (!hasProperty(properties, "LEF58_ENCLOSURE")) {
      for (const auto id : storage.lef58EnclosureRules(owner)) {
        const auto& rule = storage.lef58EnclosureRule(id);
        std::ostringstream value;
        value << "ENCLOSURE";
        if (!rule.cutclass_name.empty()) {
          value << " CUTCLASS " << rule.cutclass_name;
        }
        if (rule.side != CutLayerSide::kUnknown) {
          value << ' ' << cutSideName(rule.side);
        }
        if (hasFlag(rule.flags, TechCutLef58EnclosureRuleFlag::kHasOverhang1)) {
          value << ' ' << distance(rule.overhang1);
        }
        if (hasFlag(rule.flags, TechCutLef58EnclosureRuleFlag::kHasOverhang2)) {
          if (!hasFlag(rule.flags, TechCutLef58EnclosureRuleFlag::kHasOverhang1)) {
            throw std::logic_error("LEF58 ENCLOSURE overhang2 has no overhang1");
          }
          value << ' ' << distance(rule.overhang2);
        }
        if (hasFlag(rule.flags, TechCutLef58EnclosureRuleFlag::kHasEndOverhang1)) {
          value << " END " << distance(rule.end_overhang1);
        }
        if (hasFlag(rule.flags, TechCutLef58EnclosureRuleFlag::kHasSideOverhang2)) {
          value << " SIDE " << distance(rule.side_overhang2);
        }
        if (hasFlag(rule.flags, TechCutLef58EnclosureRuleFlag::kHasMinWidth)) {
          value << " WIDTH " << distance(rule.min_width);
        }
        if (hasFlag(rule.flags, TechCutLef58EnclosureRuleFlag::kIncludeAbutted)) {
          value << " INCLUDEABUTTED";
        }
        if (hasFlag(rule.flags, TechCutLef58EnclosureRuleFlag::kExceptExtraCut)) {
          if (!hasFlag(rule.flags, TechCutLef58EnclosureRuleFlag::kHasCutWithin)) {
            throw std::logic_error("LEF58 ENCLOSURE EXCEPTEXTRACUT has no cut-within distance");
          }
          value << " EXCEPTEXTRACUT " << distance(rule.cut_within);
          const bool prl = hasFlag(rule.flags, TechCutLef58EnclosureRuleFlag::kPrl);
          const bool no_shared_edge = hasFlag(rule.flags, TechCutLef58EnclosureRuleFlag::kNoSharedEdge);
          if (prl && no_shared_edge) {
            throw std::logic_error("LEF58 ENCLOSURE cannot be both PRL and NOSHAREDEDGE");
          }
          if (prl) {
            value << " PRL";
          } else if (no_shared_edge) {
            value << " NOSHAREDEDGE";
          }
        }
        if (hasFlag(rule.flags, TechCutLef58EnclosureRuleFlag::kHasMinLength)) {
          value << " LENGTH " << distance(rule.min_length);
        }
        if (hasFlag(rule.flags, TechCutLef58EnclosureRuleFlag::kHasRedundantCut)) {
          value << " REDUNDANTCUT " << distance(rule.redundant_cut_within);
        }
        value << " ;";
        writeProperty("LEF58_ENCLOSURE", value.str());
      }
    }

    if (!hasProperty(properties, "LEF58_SPACINGTABLE")) {
      for (const auto id : storage.orthogonalSpacingTableRules(owner)) {
        const auto& rule = storage.orthogonalSpacingTableRule(id);
        if (!hasFlag(rule.flags, TechCutOrthogonalSpacingTableRuleFlag::kLef58Property)) {
          continue;
        }
        std::ostringstream value;
        value << "SPACINGTABLE ORTHOGONAL";
        for (const auto& item : rule.items) {
          value << " WITHIN " << distance(item.within) << " SPACING " << distance(item.spacing);
        }
        value << " ;";
        writeProperty("LEF58_SPACINGTABLE", value.str());
      }
      for (const auto id : storage.lef58SpacingTableRules(owner)) {
        const auto& rule = storage.lef58SpacingTableRule(id);
        std::ostringstream value;
        value << "SPACINGTABLE";
        if (hasFlag(rule.flags, TechCutLef58SpacingTableRuleFlag::kHasDefault))
          value << " DEFAULT " << distance(rule.default_spacing);
        if (hasFlag(rule.flags, TechCutLef58SpacingTableRuleFlag::kSameMask)) value << " SAMEMASK";
        if (hasFlag(rule.flags, TechCutLef58SpacingTableRuleFlag::kSameNet)) value << " SAMENET";
        if (hasFlag(rule.flags, TechCutLef58SpacingTableRuleFlag::kSameMetal)) value << " SAMEMETAL";
        if (hasFlag(rule.flags, TechCutLef58SpacingTableRuleFlag::kSameVia)) value << " SAMEVIA";
        if (hasFlag(rule.flags, TechCutLef58SpacingTableRuleFlag::kHasSecondLayer)) {
          value << " LAYER " << rule.second_layer_name;
          if (hasFlag(rule.flags, TechCutLef58SpacingTableRuleFlag::kNoStack)) value << " NOSTACK";
          if (hasFlag(rule.flags, TechCutLef58SpacingTableRuleFlag::kPrlForAlignedCut)) {
            value << " PRLFORALIGNEDCUT";
            for (const auto& pair : rule.prl_for_aligned_cut) value << ' ' << pair.from << " TO " << pair.to;
          }
        }
        if (hasFlag(rule.flags, TechCutLef58SpacingTableRuleFlag::kHasPrl)) {
          value << " PRL " << distance(rule.prl);
          if (rule.prl_direction != CutDirection::kUnknown) value << ' ' << cutDirectionName(rule.prl_direction);
          if (hasFlag(rule.flags, TechCutLef58SpacingTableRuleFlag::kMaxXY)) value << " MAXXY";
          for (const auto& entry : rule.prl_entries)
            value << ' ' << entry.from << " TO " << entry.to << ' ' << distance(entry.prl);
        }
        value << " CUTCLASS";
        for (std::size_t column = 0; column < rule.cutclass1_names.size(); ++column) {
          value << ' ' << rule.cutclass1_names[column];
          if (!rule.cutclass1_edges.empty() && rule.cutclass1_edges[column] != CutClassEdge::kUnspecified)
            value << ' ' << cutClassEdgeName(rule.cutclass1_edges[column]);
        }
        for (std::size_t row = 0; row < rule.cutclass2_names.size(); ++row) {
          value << '\n' << rule.cutclass2_names[row];
          if (!rule.cutclass2_edges.empty() && rule.cutclass2_edges[row] != CutClassEdge::kUnspecified)
            value << ' ' << cutClassEdgeName(rule.cutclass2_edges[row]);
          for (std::size_t column = 0; column < rule.cutclass1_names.size(); ++column) {
            const auto& cell = rule.cells[row * rule.cutclass1_names.size() + column];
            if (cell.has_cut_spacing1)
              value << ' ' << distance(cell.cut_spacing1);
            else
              value << " -";
            if (cell.has_cut_spacing2)
              value << ' ' << distance(cell.cut_spacing2);
            else
              value << " -";
          }
        }
        value << " ;";
        writeProperty("LEF58_SPACINGTABLE", value.str());
      }
    }
  }

  void writeImplantLayer(TechImplantLayerId id, const TechLayerInfo& info)
  {
    const auto& storage = _database.implantLayerStorage();
    const auto& layer = storage.implantLayer(id);
    writeLayerPrefix(info, "IMPLANT");
    if (hasFlag(layer.flags, TechImplantLayerFlag::kHasMinWidth)) {
      _output << "  WIDTH " << distance(layer.min_width) << " ;\n";
    }
    for (const auto& rule : storage.spacingRules(id)) {
      _output << "  SPACING " << distance(rule.min_spacing);
      if (hasFlag(rule.flags, TechImplantSpacingRuleFlag::kHasOtherLayer)) {
        _output << " LAYER " << layerName(TechLayerId{rule.other_layer.entity()});
      }
      _output << " ;\n";
    }
    writeLayerProperties(info, _database.layerProperties(TechLayerId{id.entity()}));
  }

  void writeMastersliceLayer(TechMastersliceLayerId id, const TechLayerInfo& info)
  {
    const auto& storage = _database.mastersliceLayerStorage();
    const auto& layer = storage.mastersliceLayer(id);
    const auto& properties = _database.layerProperties(TechLayerId{id.entity()});
    writeLayerPrefix(info, "MASTERSLICE");
    if (info.lef58_type == TechLef58LayerType::kNone && layer.subtype != TechMastersliceType::kNone
        && !hasProperty(properties, "LEF58_TYPE")) {
      writeProperty("LEF58_TYPE", std::string("TYPE ") + mastersliceTypeName(layer.subtype) + " ;");
    }
    if (storage.hasTrimmedMetalRule(id) && !hasProperty(properties, "LEF58_TRIMMEDMETAL")) {
      const auto& rule = storage.trimmedMetalRule(id);
      std::string value = "TRIMMEDMETAL " + layerName(TechLayerId{rule.metal_layer.entity()});
      if (hasFlag(rule.flags, TechTrimmedMetalRuleFlag::kHasMask)) {
        value += " MASK " + std::to_string(rule.mask);
      }
      writeProperty("LEF58_TRIMMEDMETAL", value + " ;");
    }
    writeLayerProperties(info, properties);
  }

  void writeOverlapLayer(const TechLayerInfo& info)
  {
    writeLayerPrefix(info, "OVERLAP");
    const auto id = _database.findLayer(info.name);
    writeLayerProperties(info, _database.layerProperties(id));
  }

  void writeLayerProperties(const TechLayerInfo& info, const std::vector<TechProperty>& properties)
  {
    if (info.lef58_type != TechLef58LayerType::kNone && !hasProperty(properties, "LEF58_TYPE")) {
      writeProperty("LEF58_TYPE", std::string("TYPE ") + lef58LayerTypeName(info.lef58_type) + " ;");
    }
    if (hasFlag(info.flags, TechLayerInfoFlag::kLef58Backside) && !hasProperty(properties, "LEF58_BACKSIDE")) {
      writeProperty("LEF58_BACKSIDE", "BACKSIDE ;");
    }
    writeStoredProperties(properties);
  }

  void writeStoredProperties(const std::vector<TechProperty>& properties)
  {
    for (const auto& property : properties) {
      writeProperty(property.name, property.value);
    }
  }

  void writeProperty(std::string_view name, std::string_view value)
  {
    _output << "  PROPERTY " << name << ' ';
    lef_export_detail::writeQuoted(_output, value);
    _output << " ;\n";
  }

  [[nodiscard]] const std::string& layerName(TechLayerId id) const { return _database.layerInfo(id).name; }

  [[nodiscard]] const std::string& conductorLayerName(TechConductorLayerRef layer) const
  {
    if (!layer) {
      throw std::logic_error("VIA geometry has no conductor layer");
    }
    const auto& registry = _database.techRegistry().registry();
    switch (layer.kind) {
      case TechConductorLayerKind::kRouting:
        if (!registry.all_of<TechRoutingLayer>(layer.entity)) {
          throw std::logic_error("VIA routing conductor reference has the wrong layer type");
        }
        break;
      case TechConductorLayerKind::kMasterslice:
        if (!registry.all_of<TechMastersliceLayer>(layer.entity)) {
          throw std::logic_error("VIA masterslice conductor reference has the wrong layer type");
        }
        break;
      case TechConductorLayerKind::kNone:
        throw std::logic_error("VIA geometry has no conductor layer type");
    }
    return layerName(layer.layer());
  }

  void rejectUnimplementedNonDefaultRules() const
  {
    const auto& storage = _database.nonDefaultRuleStorage();
    for (const auto id : storage.nonDefaultRules()) {
      if (!storage.sameNetSpacingRules(id).empty()) {
        throw std::logic_error("LEF 5.8 canonical export cannot preserve obsolete NONDEFAULTRULE SAMENET spacing");
      }
      for (const auto& routing_rule : storage.routingRules(id)) {
        const auto flags = routing_rule.flags;
        if (hasFlag(flags, TechNdrRoutingRuleFlag::kHasResistance) || hasFlag(flags, TechNdrRoutingRuleFlag::kHasCapacitance)
            || hasFlag(flags, TechNdrRoutingRuleFlag::kHasEdgeCapacitance)) {
          throw std::logic_error("LEF 5.8 canonical export cannot preserve obsolete NONDEFAULTRULE electrical fields");
        }
      }
    }
  }

  std::ostream& _output;
  const TechStore& _database;
  int32_t _database_units_per_micron;
};

[[nodiscard]] int32_t requireDatabaseUnits(const TechStore& database)
{
  if (!database.globalStorage().hasUnits()) {
    throw std::logic_error("LEF export requires UNITS DATABASE MICRONS");
  }
  const auto& units = database.globalStorage().getUnits();
  if (!hasFlag(units.flags, TechGlobalUnitsFlag::kHasDatabaseUnitsPerMicron) || units.database_units_per_micron <= 0) {
    throw std::logic_error("LEF export requires UNITS DATABASE MICRONS");
  }
  return units.database_units_per_micron;
}

}  // namespace

void LefTechExporter::write(std::ostream& output, const TechStore& database)
{
  TechLefWriter{output, database, requireDatabaseUnits(database)}.write();
  if (!output) {
    throw std::runtime_error("failed to write canonical technology LEF");
  }
}

void LefTechExporter::write(const std::filesystem::path& path, const TechStore& database)
{
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("cannot open LEF export file: " + path.string());
  }
  write(output, database);
}

}  // namespace eccdb
