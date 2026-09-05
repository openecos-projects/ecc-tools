#include "lef/LefTechImporter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <exception>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "lef/detail/LefParserMutex.h"
#include "lef/detail/LefTechLayerProperties.h"
#include "lef/detail/LefTechLayerRules.h"
#include "lef/detail/LefTechObjects.h"
#include "lefiLayer.hpp"
#include "lefiMisc.hpp"
#include "lefiNonDefault.hpp"
#include "lefiUnits.hpp"
#include "lefiVia.hpp"
#include "lefiViaRule.hpp"
#include "lefrReader.hpp"
#include "property_parser/lef58_property/layer_property_parser.h"

namespace eccdb {

namespace layer_property = idb::layer_property;

namespace {

struct StagedMaxViaStack
{
  uint32_t max_stack_count = 0;
  std::optional<std::pair<std::string, std::string>> range;
};

struct StagedRoutingLayer
{
  TechRoutingDirection direction = TechRoutingDirection::kUnknown;
  TechRoutingAxisValueForm pitch_form = TechRoutingAxisValueForm::kNone;
  TechRoutingAxisValueForm offset_form = TechRoutingAxisValueForm::kNone;
  bool poly_routing = false;
  std::optional<double> width;
  std::optional<double> min_width;
  std::optional<double> max_width;
  std::optional<double> diag_width;
  std::optional<double> diag_spacing;
  std::optional<std::pair<double, double>> pitch;
  std::optional<std::pair<double, double>> offset;
  std::optional<double> wire_extension;
  std::optional<double> thickness;
  std::optional<double> height;
  std::optional<double> shrinkage;
  std::optional<double> cap_multiplier;
  std::optional<double> fill_active_spacing;
  std::optional<double> area;
  std::optional<double> resistance;
  std::optional<double> capacitance;
  std::optional<double> edge_capacitance;
  std::optional<double> min_density;
  std::optional<double> max_density;
  std::optional<std::pair<double, double>> density_check_window;
  std::optional<double> density_check_step;
  std::optional<std::array<double, 3>> protrusion;
  lef_detail::StagedRoutingRules rules;
};

struct StagedCutLayer
{
  std::optional<double> width;
  std::optional<double> resistance_per_cut;
  lef_detail::StagedCutRules rules;
};

struct StagedImplantSpacing
{
  double min_spacing = 0.0;
  std::optional<std::string> other_layer;
};

struct StagedImplantLayer
{
  std::optional<double> min_width;
  std::vector<StagedImplantSpacing> spacing_rules;
};

struct StagedMastersliceLayer
{
  TechMastersliceType subtype = TechMastersliceType::kNone;
};

struct StagedOverlapLayer
{
};

using StagedLayerPayload = std::variant<StagedRoutingLayer, StagedCutLayer, StagedImplantLayer, StagedMastersliceLayer, StagedOverlapLayer>;

struct StagedLayer
{
  TechLayerInfo info;
  StagedLayerPayload payload;
  std::vector<TechProperty> properties;
};

struct Staging
{
  std::optional<TechGlobalUnits> units;
  std::optional<double> manufacturing_grid;
  std::optional<StagedMaxViaStack> max_via_stack;
  std::vector<StagedLayer> layers;
  lef_detail::StagedTechObjects objects;
  std::unordered_set<std::string> layer_names;
  std::exception_ptr callback_failure;
};

struct PreparedImplantSpacing
{
  int32_t min_spacing = 0;
  std::optional<std::string> other_layer;
};

struct PreparedImplantLayer
{
  TechImplantLayer component;
  std::vector<PreparedImplantSpacing> spacing_rules;
};

struct PreparedRoutingLayer
{
  TechRoutingLayer component;
  lef_detail::PreparedRoutingRules rules;
  lef_detail::PreparedRoutingLef58Properties lef58_properties;
};

struct PreparedCutLayer
{
  TechCutLayer component;
  lef_detail::PreparedCutRules rules;
  lef_detail::PreparedCutLef58Properties lef58_properties;
};

struct PreparedMastersliceLayer
{
  TechMastersliceLayer component;
  std::optional<lef_detail::PreparedTrimmedMetalRule> trimmed_metal_rule;
};

using PreparedLayerPayload
    = std::variant<PreparedRoutingLayer, PreparedCutLayer, PreparedImplantLayer, PreparedMastersliceLayer, TechOverlapLayer>;

struct PreparedLayer
{
  TechLayerInfo info;
  PreparedLayerPayload payload;
  std::vector<TechProperty> properties;
};

struct PreparedImport
{
  std::optional<TechGlobalUnits> units;
  std::optional<int32_t> manufacturing_grid;
  std::optional<StagedMaxViaStack> max_via_stack;
  std::vector<PreparedLayer> layers;
  lef_detail::PreparedTechObjects objects;
};

std::string upper(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
  return value;
}

TechLef58LayerType lef58LayerType(std::string value)
{
  value = upper(std::move(value));
  if (value == "POLYROUTING") return TechLef58LayerType::kPolyRouting;
  if (value == "MIMCAP") return TechLef58LayerType::kMimCap;
  if (value == "TSV") return TechLef58LayerType::kTsv;
  if (value == "PASSIVATION") return TechLef58LayerType::kPassivation;
  if (value == "TRIMPOLY") return TechLef58LayerType::kTrimPoly;
  if (value == "NWELL") return TechLef58LayerType::kNWell;
  if (value == "PWELL") return TechLef58LayerType::kPWell;
  if (value == "BELOWDIEEDGE") return TechLef58LayerType::kBelowDieEdge;
  if (value == "ABOVEDIEEDGE") return TechLef58LayerType::kAboveDieEdge;
  if (value == "DIFFUSION") return TechLef58LayerType::kDiffusion;
  if (value == "TRIMMETAL") return TechLef58LayerType::kTrimMetal;
  if (value == "MEOL") return TechLef58LayerType::kMeol;
  if (value == "PADMETAL") return TechLef58LayerType::kPadMetal;
  if (value == "TSVMETAL") return TechLef58LayerType::kTsvMetal;
  if (value == "STACKEDMIMCAP") return TechLef58LayerType::kStackedMimCap;
  if (value == "SPECIALCUT") return TechLef58LayerType::kSpecialCut;
  if (value == "WELLDISTANCE") return TechLef58LayerType::kWellDistance;
  if (value == "CPODE") return TechLef58LayerType::kCpode;
  if (value == "HIGHR") return TechLef58LayerType::kHighR;
  if (value == "REGION") return TechLef58LayerType::kRegion;
  if (value == "RCBLOCKAGE") return TechLef58LayerType::kRcBlockage;
  if (value == "ABUTFILLER") return TechLef58LayerType::kAbutFiller;
  if (value == "ABUTLOGIC") return TechLef58LayerType::kAbutLogic;
  return TechLef58LayerType::kNone;
}

TechMastersliceType mastersliceType(TechLef58LayerType type)
{
  switch (type) {
    case TechLef58LayerType::kNWell: return TechMastersliceType::kNWell;
    case TechLef58LayerType::kPWell: return TechMastersliceType::kPWell;
    case TechLef58LayerType::kAboveDieEdge: return TechMastersliceType::kAboveDieEdge;
    case TechLef58LayerType::kBelowDieEdge: return TechMastersliceType::kBelowDieEdge;
    case TechLef58LayerType::kDiffusion: return TechMastersliceType::kDiffusion;
    case TechLef58LayerType::kTrimPoly: return TechMastersliceType::kTrimPoly;
    case TechLef58LayerType::kTrimMetal: return TechMastersliceType::kTrimMetal;
    case TechLef58LayerType::kRegion: return TechMastersliceType::kRegion;
    default: return TechMastersliceType::kNone;
  }
}

std::string numberText(double value)
{
  std::ostringstream stream;
  stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
  return stream.str();
}

int32_t checkedPositiveInteger(double value, const char* field)
{
  if (!std::isfinite(value) || value <= 0.0) {
    throw std::runtime_error(std::string(field) + " must be positive");
  }
  const auto rounded = std::llround(value);
  if (rounded <= 0 || rounded > std::numeric_limits<int32_t>::max()) {
    throw std::runtime_error(std::string(field) + " is outside int32 range");
  }
  return static_cast<int32_t>(rounded);
}

bool sameUnits(const TechGlobalUnits& lhs, const TechGlobalUnits& rhs) noexcept
{
  return lhs.flags == rhs.flags && lhs.nanoseconds == rhs.nanoseconds && lhs.picofarads == rhs.picofarads && lhs.ohms == rhs.ohms
         && lhs.milliwatts == rhs.milliwatts && lhs.milliamps == rhs.milliamps && lhs.volts == rhs.volts
         && lhs.database_units_per_micron == rhs.database_units_per_micron && lhs.megahertz == rhs.megahertz;
}

bool sameMaxViaStack(const StagedMaxViaStack& lhs, const StagedMaxViaStack& rhs) noexcept
{
  return lhs.max_stack_count == rhs.max_stack_count && lhs.range == rhs.range;
}

void mergeUnits(Staging& staging, TechGlobalUnits units)
{
  if (staging.units && !sameUnits(*staging.units, units)) {
    throw std::runtime_error("conflicting LEF UNITS statements");
  }
  staging.units = std::move(units);
}

void mergeManufacturingGrid(Staging& staging, double value)
{
  if (!std::isfinite(value) || value <= 0.0) {
    throw std::runtime_error("LEF MANUFACTURINGGRID must be positive");
  }
  if (staging.manufacturing_grid && *staging.manufacturing_grid != value) {
    throw std::runtime_error("conflicting LEF MANUFACTURINGGRID statements");
  }
  staging.manufacturing_grid = value;
}

void mergeMaxViaStack(Staging& staging, StagedMaxViaStack value)
{
  if (value.max_stack_count == 0) {
    throw std::runtime_error("LEF MAXVIASTACK must be positive");
  }
  if (staging.max_via_stack && !sameMaxViaStack(*staging.max_via_stack, value)) {
    throw std::runtime_error("conflicting LEF MAXVIASTACK statements");
  }
  staging.max_via_stack = std::move(value);
}

void appendLayer(Staging& staging, StagedLayer layer)
{
  if (layer.info.name.empty()) {
    throw std::runtime_error("LEF LAYER name is empty");
  }
  if (!staging.layer_names.emplace(layer.info.name).second) {
    throw std::runtime_error("duplicate LEF LAYER name: " + layer.info.name);
  }
  staging.layers.push_back(std::move(layer));
}

TechRoutingDirection routingDirection(const char* value)
{
  const auto direction = upper(value == nullptr ? std::string{} : std::string(value));
  if (direction == "HORIZONTAL") {
    return TechRoutingDirection::kHorizontal;
  }
  if (direction == "VERTICAL") {
    return TechRoutingDirection::kVertical;
  }
  if (direction == "DIAG45") {
    return TechRoutingDirection::kDiag45;
  }
  if (direction == "DIAG135") {
    return TechRoutingDirection::kDiag135;
  }
  return TechRoutingDirection::kUnknown;
}

std::vector<TechProperty> layerProperties(const lefiLayer& source)
{
  std::vector<TechProperty> properties;
  properties.reserve(static_cast<size_t>(source.numProps()));
  for (int index = 0; index < source.numProps(); ++index) {
    const auto* name = source.propName(index);
    if (name == nullptr || *name == '\0') {
      throw std::runtime_error("LEF LAYER property name is empty");
    }
    const auto* text = source.propValue(index);
    properties.push_back(TechProperty{.name = name, .value = text == nullptr ? numberText(source.propNumber(index)) : std::string(text)});
  }
  return properties;
}

StagedRoutingLayer stageRoutingLayer(lefiLayer& source)
{
  StagedRoutingLayer result;
  if (source.hasDirection()) {
    result.direction = routingDirection(source.direction());
  }
  if (source.hasPitch()) {
    result.pitch_form = TechRoutingAxisValueForm::kBothXY;
    result.pitch = std::pair{source.pitch(), source.pitch()};
  } else if (source.hasXYPitch()) {
    result.pitch_form = TechRoutingAxisValueForm::kSeparateXY;
    result.pitch = std::pair{source.pitchX(), source.pitchY()};
  }
  if (source.hasOffset()) {
    result.offset_form = TechRoutingAxisValueForm::kBothXY;
    result.offset = std::pair{source.offset(), source.offset()};
  } else if (source.hasXYOffset()) {
    result.offset_form = TechRoutingAxisValueForm::kSeparateXY;
    result.offset = std::pair{source.offsetX(), source.offsetY()};
  }
  if (source.hasWidth()) {
    result.width = source.width();
  }
  if (source.hasMinwidth()) {
    result.min_width = source.minwidth();
  }
  if (source.hasMaxwidth()) {
    result.max_width = source.maxwidth();
  }
  if (source.hasDiagWidth()) {
    result.diag_width = source.diagWidth();
  }
  if (source.hasDiagSpacing()) {
    result.diag_spacing = source.diagSpacing();
  }
  if (source.hasWireExtension()) {
    result.wire_extension = source.wireExtension();
  }
  if (source.hasThickness()) {
    result.thickness = source.thickness();
  }
  if (source.hasHeight()) {
    result.height = source.height();
  }
  if (source.hasShrinkage()) {
    result.shrinkage = source.shrinkage();
  }
  if (source.hasCapMultiplier()) {
    result.cap_multiplier = source.capMultiplier();
  }
  if (source.hasFillActiveSpacing()) {
    result.fill_active_spacing = source.fillActiveSpacing();
  }
  if (source.hasArea()) {
    result.area = source.area();
  }
  if (source.hasResistance()) {
    result.resistance = source.resistance();
  }
  if (source.hasCapacitance()) {
    result.capacitance = source.capacitance();
  }
  if (source.hasEdgeCap()) {
    result.edge_capacitance = source.edgeCap();
  }
  if (source.hasMinimumDensity()) {
    result.min_density = source.minimumDensity();
  }
  if (source.hasMaximumDensity()) {
    result.max_density = source.maximumDensity();
  }
  if (source.hasDensityCheckWindow()) {
    result.density_check_window = std::pair{source.densityCheckWindowLength(), source.densityCheckWindowWidth()};
  }
  if (source.hasDensityCheckStep()) {
    result.density_check_step = source.densityCheckStep();
  }
  if (source.hasProtrusion()) {
    result.protrusion = std::array{source.protrusionWidth1(), source.protrusionLength(), source.protrusionWidth2()};
  }
  result.poly_routing = source.hasLayerType() && upper(source.layerType()) == "POLYROUTING";
  result.rules = lef_detail::stageRoutingRules(source);
  return result;
}

StagedCutLayer stageCutLayer(lefiLayer& source)
{
  StagedCutLayer result;
  if (source.hasWidth()) {
    result.width = source.width();
  }
  if (source.hasResistancePerCut()) {
    result.resistance_per_cut = source.resistancePerCut();
  }
  result.rules = lef_detail::stageCutRules(source);
  return result;
}

StagedImplantLayer stageImplantLayer(const lefiLayer& source)
{
  StagedImplantLayer result;
  if (source.hasWidth()) {
    result.min_width = source.width();
  }
  result.spacing_rules.reserve(static_cast<size_t>(source.numSpacing()));
  for (int index = 0; index < source.numSpacing(); ++index) {
    StagedImplantSpacing rule{.min_spacing = source.spacing(index)};
    if (source.hasSpacingName(index)) {
      const auto* name = source.spacingName(index);
      if (name == nullptr || *name == '\0') {
        throw std::runtime_error("LEF IMPLANT SPACING peer layer name is empty");
      }
      rule.other_layer = name;
    }
    result.spacing_rules.push_back(std::move(rule));
  }
  return result;
}

StagedLayer stageLayer(lefiLayer& source)
{
  if (source.need58PropsProcessing()) {
    source.parseLEF58Layer();
  }
  if (!source.hasType() || source.type() == nullptr) {
    throw std::runtime_error("LEF LAYER has no TYPE: " + std::string(source.name() == nullptr ? "" : source.name()));
  }

  TechLayerInfo info{.name = source.name() == nullptr ? std::string{} : std::string(source.name())};
  if (source.hasMask()) {
    if (source.mask() <= 0) {
      throw std::runtime_error("LEF LAYER MASK must be positive: " + info.name);
    }
    info.mask_count = static_cast<uint32_t>(source.mask());
  }
  if (source.hasLayerType() && source.layerType() != nullptr) {
    info.lef58_type = lef58LayerType(source.layerType());
  }
  auto properties = layerProperties(source);
  for (const auto& property : properties) {
    const auto property_name = upper(property.name);
    if (property_name == "LEF58_TYPE") {
      std::string parsed_type;
      if (!layer_property::parse_lef58_type(property.value.begin(), property.value.end(), parsed_type)) {
        throw std::runtime_error("failed to parse LEF58_TYPE property");
      }
      const auto type = lef58LayerType(parsed_type);
      if (type == TechLef58LayerType::kNone) throw std::runtime_error("unsupported LEF58_TYPE " + parsed_type);
      if (info.lef58_type != TechLef58LayerType::kNone && info.lef58_type != type)
        throw std::runtime_error("conflicting LEF58_TYPE values on layer " + info.name);
      info.lef58_type = type;
    } else if (property_name == "LEF58_BACKSIDE") {
      if (!layer_property::parse_lef58_backside(property.value.begin(), property.value.end())) {
        throw std::runtime_error("failed to parse LEF58_BACKSIDE property");
      }
      info.flags |= TechLayerInfoFlag::kLef58Backside;
    }
  }

  const auto type = upper(source.type());
  StagedLayerPayload payload;
  if (type == "ROUTING") {
    payload = stageRoutingLayer(source);
    if (info.lef58_type == TechLef58LayerType::kPolyRouting) std::get<StagedRoutingLayer>(payload).poly_routing = true;
  } else if (type == "CUT") {
    payload = stageCutLayer(source);
  } else if (type == "IMPLANT") {
    payload = stageImplantLayer(source);
  } else if (type == "MASTERSLICE") {
    payload = StagedMastersliceLayer{.subtype = mastersliceType(info.lef58_type)};
  } else if (type == "OVERLAP") {
    payload = StagedOverlapLayer{};
  } else {
    throw std::runtime_error("unsupported LEF LAYER TYPE " + type + ": " + info.name);
  }
  return StagedLayer{.info = std::move(info), .payload = std::move(payload), .properties = std::move(properties)};
}

template <typename Function>
int guardedCallback(lefiUserData user_data, Function&& function) noexcept
{
  auto* staging = static_cast<Staging*>(user_data);
  if (staging == nullptr || staging->callback_failure) {
    return PARSE_ERROR;
  }
  try {
    function(*staging);
    return PARSE_OK;
  } catch (...) {
    staging->callback_failure = std::current_exception();
    return PARSE_ERROR;
  }
}

int unitsCallback(lefrCallbackType_e type, lefiUnits* source, lefiUserData user_data) noexcept
{
  return guardedCallback(user_data, [&](Staging& staging) {
    if (type != lefrUnitsCbkType || source == nullptr) {
      throw std::runtime_error("invalid SI2 UNITS callback");
    }
    TechGlobalUnits units;
    if (source->hasTime()) {
      units.flags |= TechGlobalUnitsFlag::kHasNanoseconds;
      units.nanoseconds = checkedPositiveInteger(source->time(), "LEF TIME NANOSECONDS");
    }
    if (source->hasCapacitance()) {
      units.flags |= TechGlobalUnitsFlag::kHasPicofarads;
      units.picofarads = checkedPositiveInteger(source->capacitance(), "LEF CAPACITANCE PICOFARADS");
    }
    if (source->hasResistance()) {
      units.flags |= TechGlobalUnitsFlag::kHasOhms;
      units.ohms = checkedPositiveInteger(source->resistance(), "LEF RESISTANCE OHMS");
    }
    if (source->hasPower()) {
      units.flags |= TechGlobalUnitsFlag::kHasMilliwatts;
      units.milliwatts = checkedPositiveInteger(source->power(), "LEF POWER MILLIWATTS");
    }
    if (source->hasCurrent()) {
      units.flags |= TechGlobalUnitsFlag::kHasMilliamps;
      units.milliamps = checkedPositiveInteger(source->current(), "LEF CURRENT MILLIAMPS");
    }
    if (source->hasVoltage()) {
      units.flags |= TechGlobalUnitsFlag::kHasVolts;
      units.volts = checkedPositiveInteger(source->voltage(), "LEF VOLTAGE VOLTS");
    }
    if (source->hasDatabase()) {
      units.flags |= TechGlobalUnitsFlag::kHasDatabaseUnitsPerMicron;
      units.database_units_per_micron = checkedPositiveInteger(source->databaseNumber(), "LEF DATABASE MICRONS");
    }
    if (source->hasFrequency()) {
      units.flags |= TechGlobalUnitsFlag::kHasMegahertz;
      units.megahertz = checkedPositiveInteger(source->frequency(), "LEF FREQUENCY MEGAHERTZ");
    }
    mergeUnits(staging, units);
  });
}

int manufacturingCallback(lefrCallbackType_e type, double value, lefiUserData user_data) noexcept
{
  return guardedCallback(user_data, [&](Staging& staging) {
    if (type != lefrManufacturingCbkType) {
      throw std::runtime_error("invalid SI2 MANUFACTURINGGRID callback");
    }
    mergeManufacturingGrid(staging, value);
  });
}

int maxViaStackCallback(lefrCallbackType_e type, lefiMaxStackVia* source, lefiUserData user_data) noexcept
{
  return guardedCallback(user_data, [&](Staging& staging) {
    if (type != lefrMaxStackViaCbkType || source == nullptr) {
      throw std::runtime_error("invalid SI2 MAXVIASTACK callback");
    }
    if (source->maxStackVia() <= 0) {
      throw std::runtime_error("LEF MAXVIASTACK must be positive");
    }
    StagedMaxViaStack stack{.max_stack_count = static_cast<uint32_t>(source->maxStackVia())};
    if (source->hasMaxStackViaRange()) {
      const auto* bottom = source->maxStackViaBottomLayer();
      const auto* top = source->maxStackViaTopLayer();
      if (bottom == nullptr || top == nullptr || *bottom == '\0' || *top == '\0') {
        throw std::runtime_error("LEF MAXVIASTACK RANGE requires two layer names");
      }
      stack.range = std::pair{std::string(bottom), std::string(top)};
    }
    mergeMaxViaStack(staging, std::move(stack));
  });
}

int layerCallback(lefrCallbackType_e type, lefiLayer* source, lefiUserData user_data) noexcept
{
  return guardedCallback(user_data, [&](Staging& staging) {
    if (type != lefrLayerCbkType || source == nullptr) {
      throw std::runtime_error("invalid SI2 LAYER callback");
    }
    appendLayer(staging, stageLayer(*source));
  });
}

int viaCallback(lefrCallbackType_e type, lefiVia* source, lefiUserData user_data) noexcept
{
  return guardedCallback(user_data, [&](Staging& staging) {
    if (type != lefrViaCbkType || source == nullptr) {
      throw std::runtime_error("invalid SI2 VIA callback");
    }
    staging.objects.vias.push_back(lef_detail::stageVia(*source));
  });
}

int viaRuleCallback(lefrCallbackType_e type, lefiViaRule* source, lefiUserData user_data) noexcept
{
  return guardedCallback(user_data, [&](Staging& staging) {
    if (type != lefrViaRuleCbkType || source == nullptr) {
      throw std::runtime_error("invalid SI2 VIARULE callback");
    }
    staging.objects.via_rules.push_back(lef_detail::stageViaRule(*source));
  });
}

int nonDefaultCallback(lefrCallbackType_e type, lefiNonDefault* source, lefiUserData user_data) noexcept
{
  return guardedCallback(user_data, [&](Staging& staging) {
    if (type != lefrNonDefaultCbkType || source == nullptr) {
      throw std::runtime_error("invalid SI2 NONDEFAULTRULE callback");
    }
    staging.objects.non_default_rules.push_back(lef_detail::stageNonDefaultRule(*source));
  });
}

int spacingCallback(lefrCallbackType_e type, lefiSpacing*, lefiUserData user_data) noexcept
{
  return guardedCallback(user_data, [&](Staging&) {
    if (type != lefrSpacingCbkType) {
      throw std::runtime_error("invalid SI2 SPACING callback");
    }
  });
}

class ParserSession
{
 public:
  ParserSession()
  {
    if (lefrInit() != 0) {
      throw std::runtime_error("failed to initialize SI2 LEF parser session");
    }
    lefrSetUnitsCbk(unitsCallback);
    lefrSetManufacturingCbk(manufacturingCallback);
    lefrSetMaxStackViaCbk(maxViaStackCallback);
    lefrSetLayerCbk(layerCallback);
    lefrSetViaCbk(viaCallback);
    lefrSetViaRuleCbk(viaRuleCallback);
    lefrSetNonDefaultCbk(nonDefaultCallback);
    // SI2 only materializes pre-5.6 NONDEFAULTRULE SAMENET rows when this
    // callback is installed, although the callback itself is not invoked for
    // rows owned by an NDR.
    lefrSetSpacingCbk(spacingCallback);
  }

  ~ParserSession() { static_cast<void>(lefrClear()); }

  ParserSession(const ParserSession&) = delete;
  ParserSession& operator=(const ParserSession&) = delete;
};

struct FileCloser
{
  void operator()(FILE* file) const noexcept
  {
    if (file != nullptr) {
      static_cast<void>(std::fclose(file));
    }
  }
};

void parseFile(const std::filesystem::path& path, Staging& staging)
{
  if (path.empty()) {
    throw std::invalid_argument("LEF path must not be empty");
  }
  ParserSession session;
  std::unique_ptr<FILE, FileCloser> file(std::fopen(path.c_str(), "r"));
  if (!file) {
    throw std::runtime_error("cannot open LEF file: " + path.string());
  }

  staging.callback_failure = nullptr;
  const auto result = lefrRead(file.get(), path.c_str(), &staging);
  if (staging.callback_failure) {
    std::rethrow_exception(staging.callback_failure);
  }
  if (result != 0) {
    throw std::runtime_error("SI2 failed to parse LEF file: " + path.string());
  }
}

int32_t databaseUnitsPerMicron(const Staging& staging)
{
  if (!staging.units || (staging.units->flags & TechGlobalUnitsFlag::kHasDatabaseUnitsPerMicron) == 0u) {
    throw std::runtime_error("LEF distance data requires UNITS DATABASE MICRONS");
  }
  return staging.units->database_units_per_micron;
}

int32_t toDatabaseUnits(double value, const Staging& staging, const char* field)
{
  if (!std::isfinite(value) || value < 0.0) {
    throw std::runtime_error(std::string(field) + " must be non-negative");
  }
  const auto scaled = value * static_cast<double>(databaseUnitsPerMicron(staging));
  if (!std::isfinite(scaled) || scaled > static_cast<double>(std::numeric_limits<int32_t>::max())) {
    throw std::runtime_error(std::string(field) + " is outside int32 DBU range");
  }
  return static_cast<int32_t>(std::llround(scaled));
}

int64_t toDatabaseArea(double value, const Staging& staging, const char* field)
{
  if (!std::isfinite(value) || value < 0.0) {
    throw std::runtime_error(std::string(field) + " must be non-negative");
  }
  const auto units = static_cast<long double>(databaseUnitsPerMicron(staging));
  const auto scaled = value * units * units;
  if (!std::isfinite(scaled) || scaled > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
    throw std::runtime_error(std::string(field) + " is outside int64 DBU area range");
  }
  return static_cast<int64_t>(std::llround(scaled));
}

TechRoutingLayer prepareRoutingLayer(const StagedRoutingLayer& source, const Staging& staging)
{
  TechRoutingLayer target;
  target.direction = source.direction;
  target.pitch_form = source.pitch_form;
  target.offset_form = source.offset_form;
  if (source.width) {
    target.flags |= TechRoutingLayerFlag::kHasWidth;
    target.width = toDatabaseUnits(*source.width, staging, "ROUTING WIDTH");
  }
  if (source.min_width) {
    target.flags |= TechRoutingLayerFlag::kHasMinWidth;
    target.min_width = toDatabaseUnits(*source.min_width, staging, "ROUTING MINWIDTH");
  }
  if (source.max_width) {
    target.flags |= TechRoutingLayerFlag::kHasMaxWidth;
    target.max_width = toDatabaseUnits(*source.max_width, staging, "ROUTING MAXWIDTH");
  }
  if (source.diag_width) {
    target.flags |= TechRoutingLayerFlag::kHasDiagWidth;
    target.diag_width = toDatabaseUnits(*source.diag_width, staging, "ROUTING DIAGWIDTH");
  }
  if (source.diag_spacing) {
    target.flags |= TechRoutingLayerFlag::kHasDiagSpacing;
    target.diag_spacing = toDatabaseUnits(*source.diag_spacing, staging, "ROUTING DIAGSPACING");
  }
  if (source.pitch) {
    target.pitch_x = toDatabaseUnits(source.pitch->first, staging, "ROUTING PITCH X");
    target.pitch_y = toDatabaseUnits(source.pitch->second, staging, "ROUTING PITCH Y");
    if (source.pitch_form == TechRoutingAxisValueForm::kSeparateXY) {
      target.flags |= TechRoutingLayerFlag::kHasPitchY;
    }
  }
  if (source.offset) {
    target.offset_x = toDatabaseUnits(source.offset->first, staging, "ROUTING OFFSET X");
    target.offset_y = toDatabaseUnits(source.offset->second, staging, "ROUTING OFFSET Y");
    if (source.offset_form == TechRoutingAxisValueForm::kSeparateXY) {
      target.flags |= TechRoutingLayerFlag::kHasOffsetY;
    }
  }
  if (source.wire_extension) {
    target.flags |= TechRoutingLayerFlag::kHasWireExtension;
    target.wire_extension = toDatabaseUnits(*source.wire_extension, staging, "ROUTING WIREEXTENSION");
  }
  if (source.thickness) {
    target.flags |= TechRoutingLayerFlag::kHasThickness;
    target.thickness = toDatabaseUnits(*source.thickness, staging, "ROUTING THICKNESS");
  }
  if (source.height) {
    target.flags |= TechRoutingLayerFlag::kHasHeight;
    target.height = toDatabaseUnits(*source.height, staging, "ROUTING HEIGHT");
  }
  if (source.shrinkage) {
    target.flags |= TechRoutingLayerFlag::kHasShrinkage;
    target.shrinkage = toDatabaseUnits(*source.shrinkage, staging, "ROUTING SHRINKAGE");
  }
  if (source.cap_multiplier) {
    target.flags |= TechRoutingLayerFlag::kHasCapMultiplier;
    target.cap_multiplier = *source.cap_multiplier;
  }
  if (source.fill_active_spacing) {
    target.flags |= TechRoutingLayerFlag::kHasFillActiveSpacing;
    target.fill_active_spacing = toDatabaseUnits(*source.fill_active_spacing, staging, "ROUTING FILLACTIVESPACING");
  }
  if (source.area) {
    target.flags |= TechRoutingLayerFlag::kHasArea;
    target.area = toDatabaseArea(*source.area, staging, "ROUTING AREA");
  }
  if (source.resistance) {
    target.flags |= TechRoutingLayerFlag::kHasResistance;
    target.resistance = *source.resistance;
  }
  if (source.capacitance) {
    target.flags |= TechRoutingLayerFlag::kHasCapacitance;
    target.capacitance = *source.capacitance;
  }
  if (source.edge_capacitance) {
    target.flags |= TechRoutingLayerFlag::kHasEdgeCapacitance;
    target.edge_capacitance = *source.edge_capacitance;
  }
  if (source.min_density) {
    target.flags |= TechRoutingLayerFlag::kHasMinDensity;
    target.min_density = *source.min_density;
  }
  if (source.max_density) {
    target.flags |= TechRoutingLayerFlag::kHasMaxDensity;
    target.max_density = *source.max_density;
  }
  if (source.density_check_window) {
    target.flags |= TechRoutingLayerFlag::kHasDensityCheckWindow;
    target.density_check_length = toDatabaseUnits(source.density_check_window->first, staging, "ROUTING DENSITYCHECKWINDOW length");
    target.density_check_width = toDatabaseUnits(source.density_check_window->second, staging, "ROUTING DENSITYCHECKWINDOW width");
  }
  if (source.density_check_step) {
    target.flags |= TechRoutingLayerFlag::kHasDensityCheckStep;
    target.density_check_step = toDatabaseUnits(*source.density_check_step, staging, "ROUTING DENSITYCHECKSTEP");
  }
  if (source.protrusion) {
    target.flags |= TechRoutingLayerFlag::kHasProtrusion;
    target.protrusion_width1 = toDatabaseUnits((*source.protrusion)[0], staging, "ROUTING PROTRUSION width1");
    target.protrusion_length = toDatabaseUnits((*source.protrusion)[1], staging, "ROUTING PROTRUSION length");
    target.protrusion_width2 = toDatabaseUnits((*source.protrusion)[2], staging, "ROUTING PROTRUSION width2");
  }
  if (source.poly_routing) {
    target.flags |= TechRoutingLayerFlag::kPolyRouting;
  }
  return target;
}

TechCutLayer prepareCutLayer(const StagedCutLayer& source, const Staging& staging)
{
  TechCutLayer target;
  if (source.width) {
    target.flags |= TechCutLayerFlag::kHasWidth;
    target.width = toDatabaseUnits(*source.width, staging, "CUT WIDTH");
  }
  if (source.resistance_per_cut) {
    target.flags |= TechCutLayerFlag::kHasResistance;
    target.resistance_per_cut = *source.resistance_per_cut;
  }
  return target;
}

PreparedImplantLayer prepareImplantLayer(const StagedImplantLayer& source, const Staging& staging)
{
  PreparedImplantLayer target;
  if (source.min_width) {
    target.component.flags |= TechImplantLayerFlag::kHasMinWidth;
    target.component.min_width = toDatabaseUnits(*source.min_width, staging, "IMPLANT WIDTH");
    if (target.component.min_width <= 0) {
      throw std::runtime_error("IMPLANT WIDTH rounds to zero DBU");
    }
  }
  target.spacing_rules.reserve(source.spacing_rules.size());
  for (const auto& source_rule : source.spacing_rules) {
    const auto spacing = toDatabaseUnits(source_rule.min_spacing, staging, "IMPLANT SPACING");
    if (spacing <= 0) {
      throw std::runtime_error("IMPLANT SPACING rounds to zero DBU");
    }
    target.spacing_rules.push_back(PreparedImplantSpacing{.min_spacing = spacing, .other_layer = source_rule.other_layer});
  }
  return target;
}

PreparedImport prepareImport(const Staging& staging)
{
  PreparedImport prepared{.units = staging.units, .max_via_stack = staging.max_via_stack};
  if (staging.manufacturing_grid) {
    prepared.manufacturing_grid = toDatabaseUnits(*staging.manufacturing_grid, staging, "MANUFACTURINGGRID");
    if (*prepared.manufacturing_grid <= 0) {
      throw std::runtime_error("MANUFACTURINGGRID rounds to zero DBU");
    }
  }

  prepared.layers.reserve(staging.layers.size());
  for (const auto& layer : staging.layers) {
    PreparedLayerPayload payload;
    if (const auto* routing = std::get_if<StagedRoutingLayer>(&layer.payload)) {
      payload = PreparedRoutingLayer{
          .component = prepareRoutingLayer(*routing, staging),
          .rules = lef_detail::prepareRoutingRules(routing->rules, databaseUnitsPerMicron(staging)),
          .lef58_properties = lef_detail::prepareRoutingLef58Properties(layer.properties, databaseUnitsPerMicron(staging))};
    } else if (const auto* cut = std::get_if<StagedCutLayer>(&layer.payload)) {
      payload
          = PreparedCutLayer{.component = prepareCutLayer(*cut, staging),
                             .rules = lef_detail::prepareCutRules(cut->rules, databaseUnitsPerMicron(staging)),
                             .lef58_properties = lef_detail::prepareCutLef58Properties(layer.properties, databaseUnitsPerMicron(staging))};
    } else if (const auto* implant = std::get_if<StagedImplantLayer>(&layer.payload)) {
      payload = prepareImplantLayer(*implant, staging);
    } else if (const auto* masterslice = std::get_if<StagedMastersliceLayer>(&layer.payload)) {
      payload = PreparedMastersliceLayer{.component = TechMastersliceLayer{.subtype = masterslice->subtype},
                                         .trimmed_metal_rule = lef_detail::prepareMastersliceLef58Properties(layer.properties)};
    } else {
      payload = TechOverlapLayer{};
    }
    prepared.layers.push_back(PreparedLayer{.info = layer.info, .payload = std::move(payload), .properties = layer.properties});
  }

  std::unordered_map<std::string, size_t> layer_indexes;
  for (size_t index = 0; index < prepared.layers.size(); ++index) {
    layer_indexes.emplace(prepared.layers[index].info.name, index);
  }
  for (const auto& layer : prepared.layers) {
    const auto* implant = std::get_if<PreparedImplantLayer>(&layer.payload);
    if (implant == nullptr) {
      continue;
    }
    for (const auto& rule : implant->spacing_rules) {
      if (!rule.other_layer) {
        continue;
      }
      const auto peer = layer_indexes.find(*rule.other_layer);
      if (peer == layer_indexes.end() || !std::holds_alternative<PreparedImplantLayer>(prepared.layers[peer->second].payload)) {
        throw std::runtime_error("IMPLANT SPACING references a missing or non-IMPLANT layer: " + *rule.other_layer);
      }
      if (prepared.layers[peer->second].info.name == layer.info.name) {
        throw std::runtime_error("IMPLANT SPACING peer layer must differ from its owner: " + layer.info.name);
      }
    }
  }
  if (prepared.max_via_stack && prepared.max_via_stack->range) {
    const auto& [bottom_name, top_name] = *prepared.max_via_stack->range;
    const auto bottom = layer_indexes.find(bottom_name);
    const auto top = layer_indexes.find(top_name);
    if (bottom == layer_indexes.end() || top == layer_indexes.end()
        || !std::holds_alternative<PreparedRoutingLayer>(prepared.layers[bottom->second].payload)
        || !std::holds_alternative<PreparedRoutingLayer>(prepared.layers[top->second].payload)) {
      throw std::runtime_error("MAXVIASTACK RANGE requires two ROUTING layers");
    }
    if (bottom->second >= top->second) {
      throw std::runtime_error("MAXVIASTACK bottom ROUTING layer must precede its top layer");
    }
  }
  prepared.objects = lef_detail::prepareTechObjects(staging.objects, databaseUnitsPerMicron(staging));
  return prepared;
}

void ensureEmptyTarget(const TechStore& database)
{
  const auto& registry = database.techRegistry().registry();
  if (!database.contains(database.techRootId()) || registry.storage<TechEntity>()->free_list() != 1u || !database.layerSequence().empty()
      || database.globalStorage().hasUnits() || database.globalStorage().hasManufacturingGrid() || database.globalStorage().hasMaxViaStack()
      || database.geometryPool().rectangleCount() != 0u || database.geometryPool().polygonCount() != 0u
      || database.geometryPool().pointCount() != 0u) {
    throw std::logic_error("direct LEF importer requires a new empty TechStore");
  }
}

void rollbackImport(TechStore& database, GeometryPoolCheckpoint geometry_checkpoint) noexcept
{
  auto& registry = database.techRegistry().registry();
  const auto root = database.techRootId().entity();
  std::vector<TechEntity> entities;
  entities.reserve(registry.storage<TechEntity>().size());
  for (const auto [entity] : registry.storage<TechEntity>().each()) {
    if (entity != root) {
      entities.push_back(entity);
    }
  }
  for (const auto entity : entities) {
    registry.destroy(entity);
  }
  registry.get<TechLayerSequence>(root).layers.clear();
  database.globalStorage().removeMaxViaStack();
  database.globalStorage().removeManufacturingGrid();
  database.globalStorage().removeUnits();
  try {
    database.geometryPool().rollback(geometry_checkpoint);
  } catch (...) {
  }
}

void commitImport(TechStore& database, const PreparedImport& prepared)
{
  std::unordered_map<std::string, TechLayerId> layer_ids;
  const auto geometry_checkpoint = database.geometryPool().checkpoint();
  try {
    if (prepared.units) {
      database.globalStorage().setUnits(*prepared.units);
    }
    if (prepared.manufacturing_grid) {
      database.globalStorage().setManufacturingGrid(*prepared.manufacturing_grid);
    }

    for (const auto& layer : prepared.layers) {
      TechLayerId id;
      if (const auto* routing = std::get_if<PreparedRoutingLayer>(&layer.payload)) {
        const auto routing_id = database.createRoutingLayer(layer.info, routing->component);
        id = TechLayerId{routing_id.entity()};
        lef_detail::commitRoutingRules(database.routingLayerStorage(), routing_id, routing->rules);
        lef_detail::commitRoutingLef58Properties(database.routingLayerStorage(), routing_id, routing->lef58_properties);
      } else if (const auto* cut = std::get_if<PreparedCutLayer>(&layer.payload)) {
        const auto cut_id = database.createCutLayer(layer.info, cut->component);
        id = TechLayerId{cut_id.entity()};
        lef_detail::commitCutRules(database.cutLayerStorage(), cut_id, cut->rules);
        lef_detail::commitCutLef58Properties(database.cutLayerStorage(), cut_id, cut->lef58_properties);
      } else if (const auto* implant = std::get_if<PreparedImplantLayer>(&layer.payload)) {
        id = TechLayerId{database.createImplantLayer(layer.info, implant->component).entity()};
      } else if (const auto* masterslice = std::get_if<PreparedMastersliceLayer>(&layer.payload)) {
        id = TechLayerId{database.createMastersliceLayer(layer.info, masterslice->component).entity()};
      } else {
        id = TechLayerId{database.createOverlapLayer(layer.info).entity()};
      }
      layer_ids.emplace(layer.info.name, id);
      for (const auto& property : layer.properties) {
        database.appendLayerProperty(id, property);
      }
    }

    for (const auto& layer : prepared.layers) {
      const auto* implant = std::get_if<PreparedImplantLayer>(&layer.payload);
      if (implant == nullptr) {
        continue;
      }
      const auto owner = TechImplantLayerId{layer_ids.at(layer.info.name).entity()};
      for (const auto& source_rule : implant->spacing_rules) {
        TechImplantSpacingRule rule{.min_spacing = source_rule.min_spacing};
        if (source_rule.other_layer) {
          rule.flags |= TechImplantSpacingRuleFlag::kHasOtherLayer;
          rule.other_layer = TechImplantLayerId{layer_ids.at(*source_rule.other_layer).entity()};
        }
        database.implantLayerStorage().addSpacingRule(owner, rule);
      }
    }

    for (const auto& layer : prepared.layers) {
      const auto* masterslice = std::get_if<PreparedMastersliceLayer>(&layer.payload);
      if (masterslice != nullptr && masterslice->trimmed_metal_rule) {
        lef_detail::commitTrimmedMetalRule(database, TechMastersliceLayerId{layer_ids.at(layer.info.name).entity()},
                                           *masterslice->trimmed_metal_rule);
      }
    }

    lef_detail::commitTechObjects(database, prepared.objects);

    if (prepared.max_via_stack) {
      TechMaxViaStack stack{.max_stack_count = prepared.max_via_stack->max_stack_count};
      if (prepared.max_via_stack->range) {
        stack.flags |= TechMaxViaStackFlag::kHasRange;
        stack.bottom_layer = TechRoutingLayerId{layer_ids.at(prepared.max_via_stack->range->first).entity()};
        stack.top_layer = TechRoutingLayerId{layer_ids.at(prepared.max_via_stack->range->second).entity()};
      }
      database.globalStorage().setMaxViaStack(stack);
    }
  } catch (...) {
    rollbackImport(database, geometry_checkpoint);
    throw;
  }
}

}  // namespace

void LefTechImporter::import(const std::filesystem::path& file)
{
  const std::array files{file};
  import(std::span<const std::filesystem::path>(files));
}

void LefTechImporter::import(std::span<const std::filesystem::path> files)
{
  if (_used) {
    throw std::logic_error("direct LEF technology importer is one-shot");
  }
  _used = true;
  if (files.empty()) {
    throw std::invalid_argument("direct LEF technology importer requires at least one file");
  }
  ensureEmptyTarget(_database);

  Staging staging;
  {
    const std::scoped_lock lock(lef_detail::parserMutex());
    for (const auto& file : files) {
      parseFile(file, staging);
    }
  }
  const auto prepared = prepareImport(staging);
  commitImport(_database, prepared);
}

}  // namespace eccdb
