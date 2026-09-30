// SPDX-License-Identifier: Apache-2.0

#include "NativeEccdbImporter.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/container/dense_map.hpp>

#include "db/obj/frShape.h"
#include "db/tech/frConstraint.h"
#include "db/tech/frLayer.h"
#include "db/tech/frTechObject.h"
#include "db/tech/frViaDef.h"
#include "db/tech/frViaRuleGenerate.h"
#include "design/DesignStore.h"
#include "design/non_default_rule/component/NonDefaultRuleComponents.h"
#include "design/via/component/ViaComponents.h"
#include "frDesign.h"
#include "geometry/PolygonRectDecomposer.h"
#include "io/io.h"
#include "odb/dbTypes.h"
#include "odb/geom.h"
#include "service/query/PhysicalDesignService.h"
#include "tech/TechStore.h"
#include "tech/cut_layer/model/CutLayerComponents.h"
#include "tech/cut_layer/model/CutRuleComponents.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/routing_layer/model/RoutingRuleComponents.h"
#include "tech/via_master/model/ViaMasterComponents.h"
#include "utl/Logger.h"

namespace drt::io {
namespace {

constexpr int kNativeImportErrorBase = 680;

struct ImportedLayers
{
  entt::dense_map<uint32_t, frLayerNum> numbers;
  frLayerNum next_number = 0;
  frLayerNum highest_routing = -1;
  bool previous_was_cut = false;
};

[[noreturn]] void fail(utl::Logger& logger, int offset, std::string_view message)
{
  logger.error(utl::DRT, kNativeImportErrorBase + offset, "{}", std::string{message});
}

[[nodiscard]] uint32_t entityKey(auto id)
{
  return static_cast<uint32_t>(id.packed());
}

[[nodiscard]] std::string layerMessage(std::string_view name, std::string_view detail)
{
  return "native EccDB technology importer: layer " + std::string{name} + " " + std::string{detail};
}

[[nodiscard]] std::string viaMessage(std::string_view name, std::string_view detail)
{
  return "native EccDB technology importer: via " + std::string{name} + " " + std::string{detail};
}

[[nodiscard]] std::string ndrMessage(std::string_view name, std::string_view detail)
{
  return "native EccDB technology importer: non-default rule " + std::string{name} + " " + std::string{detail};
}

[[nodiscard]] frCoord checkedCoord(int64_t value, utl::Logger& logger, std::string_view description)
{
  if (value < std::numeric_limits<frCoord>::min() || value > std::numeric_limits<frCoord>::max()) {
    fail(logger, 1, std::string{description} + " exceeds DRT's coordinate range");
  }
  return static_cast<frCoord>(value);
}

[[nodiscard]] frCoord checkedNonnegativeCoord(int64_t value, utl::Logger& logger, std::string_view description)
{
  if (value < 0) {
    fail(logger, 2, std::string{description} + " is negative");
  }
  return checkedCoord(value, logger, description);
}

[[nodiscard]] frUInt4 checkedUnsigned(int64_t value, utl::Logger& logger, std::string_view description)
{
  if (value < 0 || static_cast<uint64_t>(value) > std::numeric_limits<frUInt4>::max()) {
    fail(logger, 3, std::string{description} + " exceeds DRT's unsigned range");
  }
  return static_cast<frUInt4>(value);
}

[[nodiscard]] odb::Rect toOdb(eccdb::Rect rectangle)
{
  return {static_cast<int>(rectangle.ll_x),
          static_cast<int>(rectangle.ll_y),
          static_cast<int>(rectangle.ur_x),
          static_cast<int>(rectangle.ur_y)};
}

[[nodiscard]] odb::dbTechLayerDir toDirection(eccdb::TechRoutingDirection direction,
                                               utl::Logger& logger,
                                               std::string_view layer_name)
{
  switch (direction) {
    case eccdb::TechRoutingDirection::kHorizontal:
      return odb::dbTechLayerDir::HORIZONTAL;
    case eccdb::TechRoutingDirection::kVertical:
      return odb::dbTechLayerDir::VERTICAL;
    case eccdb::TechRoutingDirection::kUnknown:
    case eccdb::TechRoutingDirection::kDiag45:
    case eccdb::TechRoutingDirection::kDiag135:
      fail(logger, 4, layerMessage(layer_name, "has an unsupported ROUTING direction"));
  }
  fail(logger, 5, layerMessage(layer_name, "has an invalid ROUTING direction"));
}

void addFakeMasterslice(frTechObject& technology, ImportedLayers& layers)
{
  auto layer = std::make_unique<frLayer>();
  layer->setFakeMasterslice(true);
  layer->setLayerNum(layers.next_number++);
  technology.addLayer(std::move(layer));
}

void addFakeCut(frTechObject& technology, ImportedLayers& layers)
{
  auto layer = std::make_unique<frLayer>();
  layer->setFakeCut(true);
  layer->setLayerNum(layers.next_number++);
  technology.addLayer(std::move(layer));
}

[[nodiscard]] bool equalsIgnoreCase(std::string_view left, std::string_view right)
{
  return left.size() == right.size()
         && std::equal(left.begin(),
                       left.end(),
                       right.begin(),
                       [](char left_character, char right_character) {
                         return std::toupper(static_cast<unsigned char>(left_character))
                                == std::toupper(static_cast<unsigned char>(right_character));
                       });
}

void rejectUnrepresentedLayerProperties(const eccdb::TechStore& source,
                                        eccdb::TechLayerId layer,
                                        const eccdb::TechLayerInfo& info,
                                        bool has_corner_spacing,
                                        utl::Logger& logger)
{
  for (const auto& property : source.layerProperties(layer)) {
    // LEF properties are preserved verbatim for round-tripping.  Only this
    // family has an equivalent typed rule that is imported below.
    if (equalsIgnoreCase(property.name, "LEF58_CORNERSPACING") && has_corner_spacing) {
      continue;
    }
    fail(logger,
         6,
         layerMessage(info.name,
                      "has unsupported PROPERTY " + property.name + " that native TritonRoute cannot represent"));
  }
}

void validateLayerInfo(const eccdb::TechLayerInfo& info, utl::Logger& logger)
{
  if (info.name.empty()) {
    fail(logger, 7, "native EccDB technology importer received an unnamed layer");
  }
  if (info.mask_count == 0u) {
    fail(logger, 8, layerMessage(info.name, "has zero masks"));
  }
  if ((info.flags & eccdb::TechLayerInfoFlag::kLef58Backside) != 0u) {
    fail(logger, 9, layerMessage(info.name, "is a LEF58_BACKSIDE layer, which native TritonRoute does not route"));
  }
  if (info.lef58_type != eccdb::TechLef58LayerType::kNone) {
    fail(logger, 10, layerMessage(info.name, "has an unsupported LEF58 layer type"));
  }
}

void addRoutingBaseConstraints(frTechObject& technology,
                               frLayer& layer,
                               frCoord min_width,
                               bool has_area,
                               int64_t area,
                               utl::Logger& logger,
                               std::string_view layer_name)
{
  auto min_width_constraint = std::make_unique<frMinWidthConstraint>(min_width);
  layer.setMinWidthConstraint(min_width_constraint.get());
  technology.addUConstraint(std::move(min_width_constraint));

  auto recheck_constraint = std::make_unique<frRecheckConstraint>();
  layer.setRecheckConstraint(recheck_constraint.get());
  technology.addUConstraint(std::move(recheck_constraint));

  auto short_constraint = std::make_unique<frShortConstraint>();
  layer.setShortConstraint(short_constraint.get());
  technology.addUConstraint(std::move(short_constraint));

  auto off_grid_constraint = std::make_unique<frOffGridConstraint>();
  layer.setOffGridConstraint(off_grid_constraint.get());
  technology.addUConstraint(std::move(off_grid_constraint));

  auto non_sufficient_constraint = std::make_unique<frNonSufficientMetalConstraint>();
  layer.setNonSufficientMetalConstraint(non_sufficient_constraint.get());
  technology.addUConstraint(std::move(non_sufficient_constraint));

  if (has_area) {
    auto area_constraint = std::make_unique<frAreaConstraint>(
        checkedNonnegativeCoord(area, logger, layerMessage(layer_name, "AREA")));
    layer.setAreaConstraint(area_constraint.get());
    technology.addUConstraint(std::move(area_constraint));
  }
}

void addEndOfLineConstraints(const eccdb::TechRoutingLayerStorage& storage,
                             eccdb::TechRoutingLayerId source_layer,
                             frTechObject& technology,
                             frLayer& layer,
                             utl::Logger& logger,
                             std::string_view layer_name)
{
  constexpr uint32_t kSupportedFlags = eccdb::TechRoutingEndOfLineSpacingRuleFlag::kHasParallelEdge
                                       | eccdb::TechRoutingEndOfLineSpacingRuleFlag::kTwoEdges;
  for (const auto rule_id : storage.endOfLineSpacingRules(source_layer)) {
    const auto& rule = storage.rule(rule_id);
    if ((rule.flags & ~kSupportedFlags) != 0u
        || ((rule.flags & eccdb::TechRoutingEndOfLineSpacingRuleFlag::kTwoEdges) != 0u
            && (rule.flags & eccdb::TechRoutingEndOfLineSpacingRuleFlag::kHasParallelEdge) == 0u)) {
      fail(logger, 11, layerMessage(layer_name, "has an unsupported ENDOFLINE SPACING rule"));
    }

    auto constraint = std::make_unique<frSpacingEndOfLineConstraint>();
    constraint->setMinSpacing(checkedNonnegativeCoord(rule.min_spacing, logger, layerMessage(layer_name, "EOL spacing")));
    constraint->setEolWidth(checkedNonnegativeCoord(rule.eol_width, logger, layerMessage(layer_name, "EOL width")));
    constraint->setEolWithin(checkedNonnegativeCoord(rule.eol_within, logger, layerMessage(layer_name, "EOL within")));
    if ((rule.flags & eccdb::TechRoutingEndOfLineSpacingRuleFlag::kHasParallelEdge) != 0u) {
      constraint->setParSpace(
          checkedNonnegativeCoord(rule.parallel_space, logger, layerMessage(layer_name, "parallel-edge spacing")));
      constraint->setParWithin(
          checkedNonnegativeCoord(rule.parallel_within, logger, layerMessage(layer_name, "parallel-edge within")));
      constraint->setTwoEdges((rule.flags & eccdb::TechRoutingEndOfLineSpacingRuleFlag::kTwoEdges) != 0u);
    }
    auto* raw_constraint = constraint.get();
    technology.addUConstraint(std::move(constraint));
    layer.addEolSpacing(raw_constraint);
  }
}

void addBasicSpacingConstraints(const eccdb::TechRoutingLayerStorage& storage,
                                eccdb::TechRoutingLayerId source_layer,
                                frTechObject& technology,
                                frLayer& layer,
                                utl::Logger& logger,
                                std::string_view layer_name)
{
  for (const auto rule_id : storage.spacingRules(source_layer)) {
    const auto& rule = storage.rule(rule_id);
    if (rule.type != eccdb::TechRoutingSpacingType::kDefault) {
      fail(logger, 57, layerMessage(layer_name, "has an unsupported RANGE SPACING rule"));
    }

    frCollection<frCoord> row_values(1, 0);
    frCollection<frCoord> column_values(1, 0);
    frCollection<frCollection<frCoord>> table_values(
        1,
        {checkedNonnegativeCoord(rule.min_spacing, logger, layerMessage(layer_name, "SPACING"))});
    frString row_name("WIDTH");
    frString column_name("PARALLELRUNLENGTH");
    auto constraint = std::make_unique<frSpacingTablePrlConstraint>(
        fr2DLookupTbl<frCoord, frCoord, frCoord>(row_name, row_values, column_name, column_values, table_values));
    auto* raw_constraint = constraint.get();
    technology.addUConstraint(std::move(constraint));
    layer.setMinSpacing(raw_constraint);
  }
}

void addPrlConstraints(const eccdb::TechRoutingLayerStorage& storage,
                       eccdb::TechRoutingLayerId source_layer,
                       frTechObject& technology,
                       frLayer& layer,
                       utl::Logger& logger,
                       std::string_view layer_name)
{
  for (const auto rule_id : storage.prlSpacingTableRules(source_layer)) {
    const auto& rule = storage.rule(rule_id);
    if ((rule.flags & ~eccdb::TechRoutingPrlSpacingTableRuleFlag::kNonIncreasingWidths) != 0u
        || !rule.except_withins.empty() || !rule.influences.empty()) {
      fail(logger, 12, layerMessage(layer_name, "has an unsupported extended/LEF58 PRL SPACINGTABLE"));
    }
    if (rule.widths.empty() || rule.parallel_run_lengths.empty()) {
      fail(logger, 13, layerMessage(layer_name, "has an empty PRL SPACINGTABLE axis"));
    }
    if (rule.widths.size() > std::numeric_limits<std::size_t>::max() / rule.parallel_run_lengths.size()
        || rule.cells.size() != rule.widths.size() * rule.parallel_run_lengths.size()) {
      fail(logger, 14, layerMessage(layer_name, "has a malformed PRL SPACINGTABLE matrix"));
    }

    frCollection<frCoord> row_values;
    frCollection<frCoord> column_values;
    frCollection<frCollection<frCoord>> table_values;
    row_values.reserve(rule.widths.size());
    column_values.reserve(rule.parallel_run_lengths.size());
    table_values.resize(rule.widths.size());

    // Do not sort widths. OpenDB's DRT import uses the stored table order,
    // including legacy tables whose rows are intentionally non-monotonic.
    for (const auto width : rule.widths) {
      row_values.push_back(checkedNonnegativeCoord(width, logger, layerMessage(layer_name, "PRL width")));
    }
    for (const auto length : rule.parallel_run_lengths) {
      column_values.push_back(checkedNonnegativeCoord(length, logger, layerMessage(layer_name, "PRL length")));
    }
    for (std::size_t row = 0; row < rule.widths.size(); ++row) {
      auto& target_row = table_values[row];
      target_row.reserve(rule.parallel_run_lengths.size());
      for (std::size_t column = 0; column < rule.parallel_run_lengths.size(); ++column) {
        target_row.push_back(checkedNonnegativeCoord(
            rule.cells[row * rule.parallel_run_lengths.size() + column], logger, layerMessage(layer_name, "PRL spacing")));
      }
    }

    frString row_name("WIDTH");
    frString column_name("PARALLELRUNLENGTH");

    auto legacy_lookup = std::make_shared<fr2DLookupTbl<frCoord, frCoord, frCoord>>(
        row_name, row_values, column_name, column_values, table_values);
    auto legacy_constraint = std::make_unique<frSpacingTableConstraint>(legacy_lookup);
    layer.addConstraint(legacy_constraint.get());
    technology.addUConstraint(std::move(legacy_constraint));

    auto prl_constraint = std::make_unique<frSpacingTablePrlConstraint>(
        fr2DLookupTbl<frCoord, frCoord, frCoord>(row_name, row_values, column_name, column_values, table_values));
    auto* raw_constraint = prl_constraint.get();
    technology.addUConstraint(std::move(prl_constraint));
    layer.setMinSpacing(raw_constraint);
  }
}

void addLef58CornerSpacingConstraints(const eccdb::TechRoutingLayerStorage& storage,
                                      eccdb::TechRoutingLayerId source_layer,
                                      frTechObject& technology,
                                      frLayer& layer,
                                      utl::Logger& logger,
                                      std::string_view layer_name)
{
  constexpr uint32_t kSupportedFlags = eccdb::TechRoutingLef58CornerSpacingRuleFlag::kHasExceptEol
                                       | eccdb::TechRoutingLef58CornerSpacingRuleFlag::kCornerToCorner;
  for (const auto rule_id : storage.lef58CornerSpacingRules(source_layer)) {
    const auto& rule = storage.rule(rule_id);
    if ((rule.flags & ~kSupportedFlags) != 0u) {
      fail(logger, 59, layerMessage(layer_name, "has an unsupported LEF58_CORNERSPACING option"));
    }

    const auto widths_and_spacings = storage.lef58CornerSpacingWidths(rule_id);
    if (widths_and_spacings.empty()) {
      fail(logger, 60, layerMessage(layer_name, "has LEF58_CORNERSPACING without WIDTH entries"));
    }

    frCollection<frCoord> widths;
    frCollection<std::pair<frCoord, frCoord>> spacings;
    widths.reserve(widths_and_spacings.size());
    spacings.reserve(widths_and_spacings.size());
    for (const auto& entry : widths_and_spacings) {
      const auto width = checkedNonnegativeCoord(entry.width, logger, layerMessage(layer_name, "LEF58_CORNERSPACING WIDTH"));
      if (!widths.empty() && width <= widths.back()) {
        fail(logger, 61, layerMessage(layer_name, "has non-increasing LEF58_CORNERSPACING WIDTH entries"));
      }
      const auto spacing = checkedNonnegativeCoord(entry.spacing, logger, layerMessage(layer_name, "LEF58_CORNERSPACING SPACING"));
      widths.push_back(width);
      // EccDB's currently supported grammar has one SPACING value.  The ODB
      // parser expands that form to equal horizontal and vertical values.
      spacings.emplace_back(spacing, spacing);
    }

    frString width_name("WIDTH");
    auto constraint = std::make_unique<frLef58CornerSpacingConstraint>(
        fr1DLookupTbl<frCoord, std::pair<frCoord, frCoord>>(width_name, widths, spacings));
    switch (rule.type) {
      case eccdb::TechRoutingLef58CornerType::kConvex:
        constraint->setCornerType(frCornerTypeEnum::CONVEX);
        if ((rule.flags & eccdb::TechRoutingLef58CornerSpacingRuleFlag::kCornerToCorner) != 0u) {
          constraint->setCornerToCorner(true);
        }
        if ((rule.flags & eccdb::TechRoutingLef58CornerSpacingRuleFlag::kHasExceptEol) != 0u) {
          constraint->setEolWidth(
              checkedNonnegativeCoord(rule.except_eol, logger, layerMessage(layer_name, "LEF58_CORNERSPACING EXCEPTEOL")));
        }
        break;
      case eccdb::TechRoutingLef58CornerType::kConcave:
        // This mirrors Parser::setRoutingLayerProperties: the currently
        // represented concave form has no additional DRT attributes.
        constraint->setCornerType(frCornerTypeEnum::CONCAVE);
        break;
      default:
        fail(logger, 62, layerMessage(layer_name, "has an invalid LEF58_CORNERSPACING corner type"));
    }
    constraint->setSameXY(true);
    auto* raw_constraint = constraint.get();
    technology.addUConstraint(std::move(constraint));
    layer.addLef58CornerSpacingConstraint(raw_constraint);
  }
}

void importRoutingLayer(const eccdb::TechStore& source,
                        eccdb::TechLayerId source_id,
                        const eccdb::TechLayerInfo& info,
                        const eccdb::TechRoutingLayer& routing,
                        frTechObject& technology,
                        ImportedLayers& layers,
                        RouterConfiguration& configuration,
                        utl::Logger& logger)
{
  constexpr uint64_t kSupportedFlags = eccdb::TechRoutingLayerFlag::kHasWidth
                                       | eccdb::TechRoutingLayerFlag::kHasMinWidth
                                       | eccdb::TechRoutingLayerFlag::kHasPitchY
                                       | eccdb::TechRoutingLayerFlag::kHasOffsetY
                                       | eccdb::TechRoutingLayerFlag::kHasArea;
  if ((routing.flags & ~kSupportedFlags) != 0u) {
    fail(logger, 15, layerMessage(info.name, "has unsupported ROUTING scalar data"));
  }
  if ((routing.flags & eccdb::TechRoutingLayerFlag::kHasWidth) == 0u) {
    fail(logger, 16, layerMessage(info.name, "has no ROUTING WIDTH"));
  }
  if (routing.pitch_form != eccdb::TechRoutingAxisValueForm::kBothXY
      && routing.pitch_form != eccdb::TechRoutingAxisValueForm::kSeparateXY) {
    fail(logger, 17, layerMessage(info.name, "has no supported ROUTING PITCH"));
  }
  // DRT's ODB Parser does not copy LEF ROUTING OFFSET into frLayer.  Keep
  // the source value in EccDB for LEF round-tripping, but intentionally do
  // not make it a native-router import blocker.
  const bool has_pitch_y = (routing.flags & eccdb::TechRoutingLayerFlag::kHasPitchY) != 0u;
  if ((routing.pitch_form == eccdb::TechRoutingAxisValueForm::kSeparateXY) != has_pitch_y) {
    fail(logger, 19, layerMessage(info.name, "has inconsistent ROUTING PITCH data"));
  }

  const auto width = checkedUnsigned(routing.width, logger, layerMessage(info.name, "ROUTING WIDTH"));
  const auto pitch = checkedUnsigned(routing.pitch_x, logger, layerMessage(info.name, "ROUTING PITCH"));
  if (width == 0u || pitch == 0u) {
    fail(logger, 20, layerMessage(info.name, "has zero ROUTING WIDTH or PITCH"));
  }
  if (routing.pitch_form == eccdb::TechRoutingAxisValueForm::kSeparateXY) {
    static_cast<void>(checkedUnsigned(routing.pitch_y, logger, layerMessage(info.name, "ROUTING PITCH Y")));
  }

  const auto imported_min_width = (routing.flags & eccdb::TechRoutingLayerFlag::kHasMinWidth) != 0u
                                      ? checkedUnsigned(routing.min_width, logger, layerMessage(info.name, "ROUTING MINWIDTH"))
                                      : width;
  if (imported_min_width > width) {
    logger.warn(utl::DRT,
                210,
                "Layer {} minWidth is larger than width. Using width as minWidth.",
                info.name);
  }
  const auto min_width = std::min(imported_min_width, width);

  if (layers.next_number == 0) {
    addFakeMasterslice(technology, layers);
    addFakeCut(technology, layers);
  }

  auto native_layer = std::make_unique<frLayer>();
  native_layer->setNativeLayer(info.name,
                               odb::dbTechLayerType::ROUTING,
                               toDirection(routing.direction, logger, info.name),
                               pitch,
                               width,
                               checkedUnsigned(info.mask_count, logger, layerMessage(info.name, "MASK count")),
                               false);
  native_layer->setLayerNum(layers.next_number++);
  auto* raw_layer = native_layer.get();
  technology.addLayer(std::move(native_layer));

  raw_layer->setWidth(width);
  raw_layer->setMinWidth(min_width);
  addRoutingBaseConstraints(technology,
                            *raw_layer,
                            static_cast<frCoord>(min_width),
                            (routing.flags & eccdb::TechRoutingLayerFlag::kHasArea) != 0u,
                            routing.area,
                            logger,
                            info.name);

  const auto routing_id = eccdb::TechRoutingLayerId{source_id.entity()};
  const auto& storage = source.routingLayerStorage();
  const auto spacing_rules = storage.spacingRules(routing_id);
  const auto end_of_line_rules = storage.endOfLineSpacingRules(routing_id);
  const auto prl_rules = storage.prlSpacingTableRules(routing_id);
  const auto corner_spacing_rules = storage.lef58CornerSpacingRules(routing_id);
  if (storage.ruleCount(routing_id)
      != spacing_rules.size() + end_of_line_rules.size() + prl_rules.size() + corner_spacing_rules.size()) {
    fail(logger, 21, layerMessage(info.name, "has an unsupported ROUTING rule family"));
  }
  addBasicSpacingConstraints(storage, routing_id, technology, *raw_layer, logger, info.name);
  addEndOfLineConstraints(storage, routing_id, technology, *raw_layer, logger, info.name);
  addPrlConstraints(storage, routing_id, technology, *raw_layer, logger, info.name);
  addLef58CornerSpacingConstraints(storage, routing_id, technology, *raw_layer, logger, info.name);

  if (info.mask_count > 1u) {
    configuration.ALLOW_PIN_AS_FEEDTHROUGH = false;
  }
  layers.numbers.emplace(entityKey(source_id), raw_layer->getLayerNum());
  layers.highest_routing = raw_layer->getLayerNum();
  layers.previous_was_cut = false;
}

void importCutLayer(const eccdb::TechStore& source,
                    eccdb::TechLayerId source_id,
                    const eccdb::TechLayerInfo& info,
                    const eccdb::TechCutLayer& cut,
                    frTechObject& technology,
                    ImportedLayers& layers,
                    utl::Logger& logger)
{
  // ODB's DRT import consumes CUT WIDTH but does not project RESISTANCE.
  // Retain resistance in EccDB for round-tripping without rejecting it here.
  constexpr uint32_t kSupportedFlags = eccdb::TechCutLayerFlag::kHasWidth
                                       | eccdb::TechCutLayerFlag::kHasResistance;
  if ((cut.flags & ~kSupportedFlags) != 0u) {
    fail(logger, 22, layerMessage(info.name, "has unsupported CUT scalar data"));
  }
  const auto wrong_dir_width = (cut.flags & eccdb::TechCutLayerFlag::kHasWidth) != 0u
                                   ? checkedUnsigned(cut.width, logger, layerMessage(info.name, "CUT WIDTH"))
                                   : 0u;

  if (layers.next_number == 0) {
    addFakeMasterslice(technology, layers);
  }
  if (layers.previous_was_cut) {
    fail(logger, 23, layerMessage(info.name, "follows another CUT layer, which native TritonRoute cannot index"));
  }

  auto native_layer = std::make_unique<frLayer>();
  native_layer->setNativeLayer(info.name,
                               odb::dbTechLayerType::CUT,
                               odb::dbTechLayerDir::NONE,
                               0u,
                               wrong_dir_width,
                               checkedUnsigned(info.mask_count, logger, layerMessage(info.name, "MASK count")),
                               false);
  native_layer->setLayerNum(layers.next_number++);
  auto* raw_layer = native_layer.get();
  technology.addLayer(std::move(native_layer));

  // Keep width unset here. Parser::initCutLayerWidth() derives it from the
  // selected default via, exactly as the OpenDB parser does for CUT layers.
  auto short_constraint = std::make_unique<frShortConstraint>();
  raw_layer->addConstraint(short_constraint.get());
  raw_layer->setShortConstraint(short_constraint.get());
  technology.addUConstraint(std::move(short_constraint));

  const auto cut_id = eccdb::TechCutLayerId{source_id.entity()};
  const auto& storage = source.cutLayerStorage();
  const auto spacing_rules = storage.spacingRules(cut_id);
  if (storage.ruleCount(cut_id) != spacing_rules.size()) {
    fail(logger, 24, layerMessage(info.name, "has an unsupported CUT rule family"));
  }
  constexpr uint32_t kSupportedSpacingFlags = eccdb::TechCutSpacingRuleFlag::kHasAdjacentCuts
                                               | eccdb::TechCutSpacingRuleFlag::kSameNet
                                               | eccdb::TechCutSpacingRuleFlag::kCenterToCenter
                                               | eccdb::TechCutSpacingRuleFlag::kSameNetPgOnly
                                               | eccdb::TechCutSpacingRuleFlag::kHasSecondLayer
                                               | eccdb::TechCutSpacingRuleFlag::kStack
                                               | eccdb::TechCutSpacingRuleFlag::kExceptSamePgNet
                                               | eccdb::TechCutSpacingRuleFlag::kParallelOverlap
                                               | eccdb::TechCutSpacingRuleFlag::kHasCutArea;
  for (const auto rule_id : spacing_rules) {
    const auto& rule = storage.spacingRule(rule_id);
    if ((rule.flags & ~kSupportedSpacingFlags) != 0u) {
      fail(logger, 25, layerMessage(info.name, "has an unsupported CUT SPACING qualifier"));
    }

    const bool has_adjacent_cuts = (rule.flags & eccdb::TechCutSpacingRuleFlag::kHasAdjacentCuts) != 0u;
    if (has_adjacent_cuts && rule.adjacent_cut_count == 0u) {
      fail(logger, 67, layerMessage(info.name, "has ADJACENTCUTS with a zero cut count"));
    }
    if (has_adjacent_cuts && rule.adjacent_cut_count > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
      fail(logger, 69, layerMessage(info.name, "has ADJACENTCUTS outside DRT's integer range"));
    }
    const bool has_second_layer = (rule.flags & eccdb::TechCutSpacingRuleFlag::kHasSecondLayer) != 0u;
    if (has_second_layer && rule.second_layer_name.empty()) {
      fail(logger, 68, layerMessage(info.name, "has CUT SPACING LAYER with an empty layer name"));
    }

    // This follows Parser::addCutLayer() exactly. It reads SAMENET PGONLY,
    // but the current DRT path does not consume ADJACENTCUTS EXCEPTSAMEPGNET.
    // Preserve the latter in EccDB rather than incorrectly broadening the
    // frCutSpacingConstraint behavior.
    const bool except_same_pg_net = (rule.flags & eccdb::TechCutSpacingRuleFlag::kSameNetPgOnly) != 0u;
    const auto adjacent_cut_within = has_adjacent_cuts && rule.adjacent_cut_within != 0
                                         ? checkedNonnegativeCoord(rule.adjacent_cut_within,
                                                                   logger,
                                                                   layerMessage(info.name, "CUT ADJACENTCUTS WITHIN"))
                                         : -1;
    const auto cut_area = (rule.flags & eccdb::TechCutSpacingRuleFlag::kHasCutArea) != 0u && rule.cut_area != 0
                              ? checkedNonnegativeCoord(rule.cut_area, logger, layerMessage(info.name, "CUT AREA"))
                              : -1;
    auto constraint = std::make_unique<frCutSpacingConstraint>(
        checkedNonnegativeCoord(rule.spacing, logger, layerMessage(info.name, "CUT spacing")),
        (rule.flags & eccdb::TechCutSpacingRuleFlag::kCenterToCenter) != 0u,
        (rule.flags & eccdb::TechCutSpacingRuleFlag::kSameNet) != 0u,
        has_second_layer ? frString{rule.second_layer_name} : frString{},
        (rule.flags & eccdb::TechCutSpacingRuleFlag::kStack) != 0u,
        has_adjacent_cuts ? static_cast<int>(rule.adjacent_cut_count) : -1,
        adjacent_cut_within,
        except_same_pg_net,
        (rule.flags & eccdb::TechCutSpacingRuleFlag::kParallelOverlap) != 0u,
        cut_area);
    auto* raw_constraint = constraint.get();
    raw_layer->addCutSpacingConstraint(raw_constraint);
    technology.addUConstraint(std::move(constraint));
  }

  layers.numbers.emplace(entityKey(source_id), raw_layer->getLayerNum());
  layers.previous_was_cut = true;
}

[[nodiscard]] frLayerNum requireLayerNumber(const ImportedLayers& layers,
                                             eccdb::TechLayerId source_layer,
                                             utl::Logger& logger,
                                             std::string_view object)
{
  if (!source_layer) {
    fail(logger, 26, std::string{object} + " has no layer");
  }
  const auto found = layers.numbers.find(entityKey(source_layer));
  if (found == layers.numbers.end()) {
    fail(logger, 27, std::string{object} + " references a layer not imported by native TritonRoute");
  }
  return found->second;
}

void appendViaRects(const eccdb::GeometryPool& pool,
                    eccdb::GeometryHandle geometry,
                    frLayerNum layer_number,
                    std::string_view via_name,
                    std::string_view role,
                    utl::Logger& logger,
                    const auto& append)
{
  if (!geometry.valid()) {
    fail(logger, 28, viaMessage(via_name, std::string{role} + " geometry is missing"));
  }
  if (pool.empty(geometry)) {
    fail(logger, 30, viaMessage(via_name, std::string{role} + " geometry has no RECT shapes"));
  }

  const auto append_rectangle = [&](eccdb::Rect rectangle) {
    if (rectangle.ll_x >= rectangle.ur_x || rectangle.ll_y >= rectangle.ur_y) {
      fail(logger, 31, viaMessage(via_name, std::string{role} + " geometry has a degenerate RECT"));
    }
    auto figure = std::make_unique<frRect>();
    figure->setBBox(toOdb(rectangle));
    figure->setLayerNum(layer_number);
    append(std::move(figure));
  };

  for (const auto rectangle : pool.rectangles(geometry)) {
    append_rectangle(rectangle);
  }
  for (uint32_t polygon_index = 0; polygon_index < pool.polygonCount(geometry); ++polygon_index) {
    std::vector<eccdb::Rect> rectangles;
    try {
      rectangles = eccdb::decomposePolygonToRectangles(pool.polygonPoints(geometry, polygon_index));
    } catch (const std::exception& error) {
      fail(logger,
           29,
           viaMessage(via_name,
                      std::string{role} + " POLYGON cannot be decomposed to DRT rectangles: " + error.what()));
    }
    for (const auto rectangle : rectangles) {
      append_rectangle(rectangle);
    }
  }
}

void validateViaStack(const frTechObject& technology,
                      frLayerNum bottom,
                      frLayerNum cut,
                      frLayerNum top,
                      utl::Logger& logger,
                      std::string_view name)
{
  if (bottom + 1 != cut || cut + 1 != top || technology.getLayer(bottom)->getType() != odb::dbTechLayerType::ROUTING
      || technology.getLayer(cut)->getType() != odb::dbTechLayerType::CUT
      || technology.getLayer(top)->getType() != odb::dbTechLayerType::ROUTING) {
    fail(logger, 32, viaMessage(name, "does not connect consecutive ROUTING/CUT/ROUTING layers"));
  }
}

void importTechVias(const eccdb::PhysicalDesignService& physical,
                    frTechObject& technology,
                    const ImportedLayers& layers,
                    utl::Logger& logger)
{
  const auto& source = physical.tech();
  const auto& via_storage = source.viaMasterStorage();
  const auto& pool = source.geometryPool();
  physical.forEachTechVia([&](eccdb::TechViaMasterId id, const eccdb::TechViaMaster& via, const eccdb::TechViaGeometry& geometry) {
    if (!via_storage.contains(id)) {
      fail(logger, 33, viaMessage(via.name, "does not belong to the technology VIA store"));
    }
    // Parser::setTechVias() only consumes concrete geometry and DEFAULT.
    // Keep additional LEF VIA metadata in EccDB without making native DRT
    // import fail merely because frViaDef has no matching storage for it.
    if (geometry.bottom_layer.kind != eccdb::TechConductorLayerKind::kRouting
        || geometry.top_layer.kind != eccdb::TechConductorLayerKind::kRouting) {
      fail(logger, 35, viaMessage(via.name, "uses a non-ROUTING conductor layer"));
    }

    const auto bottom = requireLayerNumber(layers, geometry.bottom_layer.layer(), logger, viaMessage(via.name, "bottom"));
    const auto cut = requireLayerNumber(layers, eccdb::TechLayerId{geometry.cut_layer.entity()}, logger, viaMessage(via.name, "cut"));
    const auto top = requireLayerNumber(layers, geometry.top_layer.layer(), logger, viaMessage(via.name, "top"));
    validateViaStack(technology, bottom, cut, top, logger, via.name);

    auto definition = std::make_unique<frViaDef>(via.name);
    definition->setDefault((via.flags & eccdb::TechViaMasterFlag::kDefault) != 0u);
    appendViaRects(pool,
                   geometry.bottom_geometry,
                   bottom,
                   via.name,
                   "bottom",
                   logger,
                   [&definition](std::unique_ptr<frRect> figure) { definition->addLayer1Fig(std::move(figure)); });
    appendViaRects(pool,
                   geometry.cut_geometry,
                   cut,
                   via.name,
                   "cut",
                   logger,
                   [&definition](std::unique_ptr<frRect> figure) { definition->addCutFig(std::move(figure)); });
    appendViaRects(pool,
                   geometry.top_geometry,
                   top,
                   via.name,
                   "top",
                   logger,
                   [&definition](std::unique_ptr<frRect> figure) { definition->addLayer2Fig(std::move(figure)); });
    if (technology.addVia(std::move(definition)) == nullptr) {
      fail(logger, 36, viaMessage(via.name, "duplicates a different DRT VIA definition"));
    }
  });
}

void importTechViaRuleGenerates(const eccdb::PhysicalDesignService& physical,
                                frTechObject& technology,
                                const ImportedLayers& layers,
                                utl::Logger& logger)
{
  constexpr uint32_t kSupportedRuleFlags = eccdb::TechViaRuleGenerateFlag::kDefault;
  constexpr uint32_t kSupportedConductorFlags = eccdb::TechViaRuleGenerateRoutingLayerFlag::kHasDirection
                                                | eccdb::TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure
                                                | eccdb::TechViaRuleGenerateRoutingLayerFlag::kHasWidth
                                                | eccdb::TechViaRuleGenerateRoutingLayerFlag::kHasOverhang
                                                | eccdb::TechViaRuleGenerateRoutingLayerFlag::kHasMetalOverhang;
  constexpr uint32_t kSupportedCutFlags = eccdb::TechViaRuleGenerateCutLayerFlag::kHasRect
                                         | eccdb::TechViaRuleGenerateCutLayerFlag::kHasSpacing
                                         | eccdb::TechViaRuleGenerateCutLayerFlag::kHasResistance;

  physical.forEachTechViaRuleGenerate(
      [&](eccdb::TechViaRuleGenerateId,
          const eccdb::TechViaRuleGenerate& rule,
          const eccdb::TechViaRuleGenerateBottomLayer& bottom,
          const eccdb::TechViaRuleGenerateCutLayer& cut,
          const eccdb::TechViaRuleGenerateTopLayer& top) {
        if ((rule.flags & ~kSupportedRuleFlags) != 0u) {
          fail(logger, 63, viaMessage(rule.name, "has unknown VIARULE GENERATE flags"));
        }
        if ((bottom.flags & ~kSupportedConductorFlags) != 0u || (top.flags & ~kSupportedConductorFlags) != 0u
            || (cut.flags & ~kSupportedCutFlags) != 0u) {
          fail(logger, 64, viaMessage(rule.name, "has unknown VIARULE GENERATE clause flags"));
        }
        if (bottom.layer.kind != eccdb::TechConductorLayerKind::kRouting
            || top.layer.kind != eccdb::TechConductorLayerKind::kRouting) {
          fail(logger, 65, viaMessage(rule.name, "uses a non-ROUTING VIARULE GENERATE conductor layer"));
        }

        const auto bottom_layer = requireLayerNumber(layers, bottom.layer.layer(), logger, viaMessage(rule.name, "bottom"));
        const auto cut_layer = requireLayerNumber(layers, eccdb::TechLayerId{cut.layer.entity()}, logger, viaMessage(rule.name, "cut"));
        const auto top_layer = requireLayerNumber(layers, top.layer.layer(), logger, viaMessage(rule.name, "top"));
        validateViaStack(technology, bottom_layer, cut_layer, top_layer, logger, rule.name);

        auto definition = std::make_unique<frViaRuleGenerate>(rule.name);
        definition->setDefault(rule.isDefault());
        if ((bottom.flags & eccdb::TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure) != 0u) {
          definition->setLayer1Enc(
              odb::Point(checkedNonnegativeCoord(bottom.enclosure_overhang1,
                                                  logger,
                                                  viaMessage(rule.name, "bottom VIARULE GENERATE ENCLOSURE")),
                         checkedNonnegativeCoord(bottom.enclosure_overhang2,
                                                 logger,
                                                 viaMessage(rule.name, "bottom VIARULE GENERATE ENCLOSURE"))));
        }
        if ((top.flags & eccdb::TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure) != 0u) {
          definition->setLayer2Enc(
              odb::Point(checkedNonnegativeCoord(top.enclosure_overhang1,
                                                  logger,
                                                  viaMessage(rule.name, "top VIARULE GENERATE ENCLOSURE")),
                         checkedNonnegativeCoord(top.enclosure_overhang2,
                                                 logger,
                                                 viaMessage(rule.name, "top VIARULE GENERATE ENCLOSURE"))));
        }
        if ((cut.flags & eccdb::TechViaRuleGenerateCutLayerFlag::kHasRect) != 0u) {
          if (!cut.cut_rect.hasArea()) {
            fail(logger, 66, viaMessage(rule.name, "has a degenerate VIARULE GENERATE cut RECT"));
          }
          definition->setCutRect(toOdb(cut.cut_rect));
        }
        if ((cut.flags & eccdb::TechViaRuleGenerateCutLayerFlag::kHasSpacing) != 0u) {
          definition->setCutSpacing(
              odb::Point(checkedNonnegativeCoord(cut.spacing_x, logger, viaMessage(rule.name, "VIARULE GENERATE CUT SPACING")),
                         checkedNonnegativeCoord(cut.spacing_y, logger, viaMessage(rule.name, "VIARULE GENERATE CUT SPACING"))));
        }

        // DRT's ODB parser likewise retains only enclosure, cut RECT, and
        // cut SPACING. Direction, width, overhang, and cut resistance are
        // valid LEF data but have no frViaRuleGenerate representation.
        technology.addViaRuleGenerate(std::move(definition));
      });
}

void importDesignVias(const eccdb::PhysicalDesignService& physical,
                      frTechObject& technology,
                      const ImportedLayers& layers,
                      utl::Logger& logger)
{
  physical.forEachDesignVia([&](eccdb::DesignViaId id, const eccdb::DesignVia& via) {
    // Parser::setVias() materializes geometry but does not project DEF VIA
    // PATTERN or other non-geometric metadata into frViaDef.
    entt::dense_map<frLayerNum, std::vector<eccdb::Rect>> figures;
    const auto add_rectangle = [&](eccdb::TechLayerId source_layer, eccdb::Rect rectangle, uint32_t) {
      if (rectangle.ll_x >= rectangle.ur_x || rectangle.ll_y >= rectangle.ur_y) {
        fail(logger, 40, viaMessage(via.name, "contains a degenerate DEF VIA RECT"));
      }
      const auto layer = requireLayerNumber(layers, source_layer, logger, viaMessage(via.name, "geometry"));
      figures[layer].push_back(rectangle);
    };

    // OpenDB's DRT Parser materializes dbViaParams into a normal frViaDef.
    // The service applies the same centered cut-array and metal-offset
    // transform for generated VIAs, including ROWCOL, ORIGIN, and OFFSET.
    // Both parser paths hand DRT rectangle geometry; masks are intentionally
    // not retained by frRect/frViaDef, just as in Parser::setVias().
    physical.forEachDesignViaGeometry(
        id,
        eccdb::PhysicalGeometryVisitor{
            .rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
              add_rectangle(rectangle.layer, rectangle.rectangle, rectangle.mask);
            },
            .polygon = [&](const eccdb::PhysicalLayerPolygon& polygon) {
              std::vector<eccdb::Rect> rectangles;
              try {
                rectangles = eccdb::decomposePolygonToRectangles(polygon.points);
              } catch (const std::exception& error) {
                fail(logger,
                     38,
                     viaMessage(via.name, std::string{"POLYGON cannot be decomposed to DRT rectangles: "} + error.what()));
              }
              for (const auto rectangle : rectangles) {
                add_rectangle(polygon.layer, rectangle, polygon.mask);
              }
            }});
    if (figures.size() != 3u) {
      fail(logger, 41, viaMessage(via.name, "does not have exactly three static layer groups"));
    }

    frLayerNum bottom = std::numeric_limits<frLayerNum>::max();
    frLayerNum cut = std::numeric_limits<frLayerNum>::max();
    frLayerNum top = -1;
    for (const auto& [layer, rectangles] : figures) {
      if (rectangles.empty()) {
        fail(logger, 42, viaMessage(via.name, "has an empty static layer group"));
      }
      bottom = std::min(bottom, layer);
      top = std::max(top, layer);
    }
    cut = bottom + 1;
    if (figures.find(cut) == figures.end()) {
      fail(logger, 43, viaMessage(via.name, "has no CUT layer between its routing layers"));
    }
    validateViaStack(technology, bottom, cut, top, logger, via.name);

    auto definition = std::make_unique<frViaDef>(via.name);
    const auto append_group = [&](frLayerNum layer, const auto& append) {
      for (const auto rectangle : figures.at(layer)) {
        auto figure = std::make_unique<frRect>();
        figure->setBBox(toOdb(rectangle));
        figure->setLayerNum(layer);
        append(std::move(figure));
      }
    };
    append_group(bottom, [&definition](std::unique_ptr<frRect> figure) { definition->addLayer1Fig(std::move(figure)); });
    append_group(cut, [&definition](std::unique_ptr<frRect> figure) { definition->addCutFig(std::move(figure)); });
    append_group(top, [&definition](std::unique_ptr<frRect> figure) { definition->addLayer2Fig(std::move(figure)); });
    if (technology.addVia(std::move(definition)) == nullptr) {
      fail(logger, 44, viaMessage(via.name, "duplicates a different DRT VIA definition"));
    }
  });
}

[[nodiscard]] int requireRoutingIndex(const ImportedLayers& layers,
                                      const frTechObject& technology,
                                      eccdb::TechLayerId source_layer,
                                      utl::Logger& logger,
                                      std::string_view object)
{
  const auto layer_number = requireLayerNumber(layers, source_layer, logger, object);
  const auto* layer = technology.getLayer(layer_number);
  if (layer->getType() != odb::dbTechLayerType::ROUTING || layer_number < 2 || (layer_number % 2) != 0) {
    fail(logger, 70, std::string{object} + " does not reference an imported ROUTING layer");
  }
  return layer_number / 2 - 1;
}

[[nodiscard]] frViaDef* requireImportedVia(frTechObject& technology,
                                           std::string_view name,
                                           utl::Logger& logger,
                                           std::string_view owner)
{
  for (const auto& candidate : technology.getVias()) {
    if (candidate->getName() == name) {
      return candidate.get();
    }
  }
  fail(logger, 71, std::string{owner} + " references a VIA that was not projected to DRT: " + std::string{name});
}

[[nodiscard]] frViaRuleGenerate* requireImportedViaRule(frTechObject& technology,
                                                         std::string_view name,
                                                         utl::Logger& logger,
                                                         std::string_view owner)
{
  for (const auto& candidate : technology.getViaRuleGenerates()) {
    if (candidate->getName() == name) {
      return candidate.get();
    }
  }
  fail(logger, 72, std::string{owner} + " references a VIARULE GENERATE that was not projected to DRT: " + std::string{name});
}

[[nodiscard]] int requireViaBottomRoutingIndex(const eccdb::TechStore& source,
                                                const ImportedLayers& layers,
                                                const frTechObject& technology,
                                                eccdb::TechViaMasterId source_via,
                                                utl::Logger& logger,
                                                std::string_view owner)
{
  const auto& via_storage = source.viaMasterStorage();
  if (!via_storage.contains(source_via)) {
    fail(logger, 73, std::string{owner} + " references an unknown technology VIA");
  }
  const auto& geometry = via_storage.geometry(source_via);
  if (geometry.bottom_layer.kind != eccdb::TechConductorLayerKind::kRouting) {
    fail(logger, 74, std::string{owner} + " references a VIA without a ROUTING bottom layer");
  }
  return requireRoutingIndex(layers, technology, geometry.bottom_layer.layer(), logger, owner);
}

[[nodiscard]] int requireViaRuleBottomRoutingIndex(const eccdb::TechStore& source,
                                                    const ImportedLayers& layers,
                                                    const frTechObject& technology,
                                                    eccdb::TechViaRuleGenerateId source_rule,
                                                    utl::Logger& logger,
                                                    std::string_view owner)
{
  const auto& storage = source.viaRuleGenerateStorage();
  if (!storage.contains(source_rule)) {
    fail(logger, 75, std::string{owner} + " references an unknown VIARULE GENERATE");
  }
  const auto& bottom = storage.bottomLayer(source_rule);
  if (bottom.layer.kind != eccdb::TechConductorLayerKind::kRouting) {
    fail(logger, 76, std::string{owner} + " references a VIARULE GENERATE without a ROUTING bottom layer");
  }
  return requireRoutingIndex(layers, technology, bottom.layer.layer(), logger, owner);
}

void projectTechNdrLayerRule(const eccdb::TechNdrRoutingRule& source_rule,
                             const ImportedLayers& layers,
                             const frTechObject& technology,
                             frNonDefaultRule& destination,
                             utl::Logger& logger,
                             std::string_view rule_name)
{
  // Parser::createNDR() only projects WIDTH, SPACING, and WIREEXTENSION.
  // EccDB keeps DIAGWIDTH and electrical fields in its source model.
  const auto z = requireRoutingIndex(layers, technology, eccdb::TechLayerId{source_rule.layer.entity()}, logger,
                                     ndrMessage(rule_name, "LAYER"));
  destination.setWidth(checkedNonnegativeCoord(source_rule.width, logger, ndrMessage(rule_name, "WIDTH")), z);
  destination.setSpacing((source_rule.flags & eccdb::TechNdrRoutingRuleFlag::kHasSpacing) != 0u
                             ? checkedNonnegativeCoord(source_rule.spacing, logger, ndrMessage(rule_name, "SPACING"))
                             : 0,
                         z);
  destination.setWireExtension((source_rule.flags & eccdb::TechNdrRoutingRuleFlag::kHasWireExtension) != 0u
                                   ? checkedNonnegativeCoord(source_rule.wire_extension,
                                                              logger,
                                                              ndrMessage(rule_name, "WIREEXTENSION"))
                                   : 0,
                               z);
}

void projectDesignNdrLayerRule(const eccdb::DesignNdrLayerRule& source_rule,
                               const ImportedLayers& layers,
                               const frTechObject& technology,
                               frNonDefaultRule& destination,
                               utl::Logger& logger,
                               std::string_view rule_name)
{
  // DRT likewise ignores DEF DIAGWIDTH here; retain it in EccDB source only.
  const auto z = requireRoutingIndex(layers, technology, source_rule.layer, logger, ndrMessage(rule_name, "LAYER"));
  destination.setWidth(checkedNonnegativeCoord(source_rule.width, logger, ndrMessage(rule_name, "WIDTH")), z);
  destination.setSpacing((source_rule.flags & eccdb::DesignNdrLayerRuleFlag::kHasSpacing) != 0u
                             ? checkedNonnegativeCoord(source_rule.spacing, logger, ndrMessage(rule_name, "SPACING"))
                             : 0,
                         z);
  destination.setWireExtension((source_rule.flags & eccdb::DesignNdrLayerRuleFlag::kHasWireExtension) != 0u
                                   ? checkedNonnegativeCoord(source_rule.wire_extension,
                                                              logger,
                                                              ndrMessage(rule_name, "WIREEXTENSION"))
                                   : 0,
                               z);
}

void importTechNonDefaultRules(const eccdb::TechStore& source,
                               frTechObject& technology,
                               const ImportedLayers& layers,
                               utl::Logger& logger)
{
  const auto& ndr_storage = source.nonDefaultRuleStorage();
  const auto& via_storage = source.viaMasterStorage();
  const auto& via_rule_storage = source.viaRuleGenerateStorage();
  for (const auto id : ndr_storage.nonDefaultRules()) {
    const auto& rule = ndr_storage.nonDefaultRule(id);
    if (rule.name.empty()) {
      fail(logger, 79, ndrMessage(rule.name, "has an empty LEF NDR name"));
    }
    if (technology.getNondefaultRule(rule.name) != nullptr) {
      logger.warn(utl::DRT, 211, "Skipping NDR {} because another rule with the same name already exists.", rule.name);
      continue;
    }

    auto destination = std::make_unique<frNonDefaultRule>();
    destination->setName(rule.name.c_str());
    destination->setHardSpacing((rule.flags & eccdb::TechNonDefaultRuleFlag::kHardSpacing) != 0u);
    for (const auto& layer_rule : ndr_storage.routingRules(id)) {
      projectTechNdrLayerRule(layer_rule, layers, technology, *destination, logger, rule.name);
    }
    for (const auto source_via : ndr_storage.useVias(id)) {
      if (!via_storage.contains(source_via)) {
        fail(logger, 81, ndrMessage(rule.name, "references an unknown LEF USEVIA"));
      }
      const auto& via = via_storage.viaMaster(source_via);
      const auto z = requireViaBottomRoutingIndex(source, layers, technology, source_via, logger, ndrMessage(rule.name, "USEVIA"));
      destination->addVia(requireImportedVia(technology, via.name, logger, ndrMessage(rule.name, "USEVIA")), z);
    }
    for (const auto source_via_rule : ndr_storage.useViaRules(id)) {
      if (!via_rule_storage.contains(source_via_rule)) {
        fail(logger, 82, ndrMessage(rule.name, "references an unknown LEF USEVIARULE"));
      }
      const auto& via_rule = via_rule_storage.viaRuleGenerate(source_via_rule);
      const auto z = requireViaRuleBottomRoutingIndex(
          source, layers, technology, source_via_rule, logger, ndrMessage(rule.name, "USEVIARULE"));
      destination->addViaRule(requireImportedViaRule(technology, via_rule.name, logger, ndrMessage(rule.name, "USEVIARULE")), z);
    }
    technology.addNDR(std::move(destination));
  }
}

void importDesignNonDefaultRules(const eccdb::PhysicalDesignService& physical,
                                 frTechObject& technology,
                                 const ImportedLayers& layers,
                                 utl::Logger& logger)
{
  const auto& source = physical.tech();
  const auto& design_routing = physical.design().routingStorage();
  const auto& via_storage = source.viaMasterStorage();
  const auto& via_rule_storage = source.viaRuleGenerateStorage();
  for (const auto id : design_routing.nonDefaultRules()) {
    const auto& rule = design_routing.nonDefaultRule(id);
    if (rule.name.empty()) {
      fail(logger, 83, ndrMessage(rule.name, "has an empty DEF NDR name"));
    }
    if (technology.getNondefaultRule(rule.name) != nullptr) {
      logger.warn(utl::DRT, 212, "Skipping NDR {} because another rule with the same name already exists.", rule.name);
      continue;
    }

    auto destination = std::make_unique<frNonDefaultRule>();
    destination->setName(rule.name.c_str());
    destination->setHardSpacing((rule.flags & eccdb::DesignNonDefaultRuleFlag::kHardSpacing) != 0u);
    for (const auto& layer_rule : rule.layer_rules) {
      projectDesignNdrLayerRule(layer_rule, layers, technology, *destination, logger, rule.name);
    }
    for (const auto& via_reference : rule.vias) {
      if (via_reference.design_via) {
        fail(logger, 85, ndrMessage(rule.name, "references a DEF-local VIA; DRT's ODB NDR path only projects technology VIAs"));
      }
      if (!via_reference.tech_via || !via_storage.contains(via_reference.tech_via)) {
        fail(logger, 86, ndrMessage(rule.name, "references an unknown technology VIA"));
      }
      const auto& via = via_storage.viaMaster(via_reference.tech_via);
      const auto z = requireViaBottomRoutingIndex(
          source, layers, technology, via_reference.tech_via, logger, ndrMessage(rule.name, "USEVIA"));
      destination->addVia(requireImportedVia(technology, via.name, logger, ndrMessage(rule.name, "USEVIA")), z);
    }
    for (const auto source_via_rule : rule.via_rules) {
      if (!via_rule_storage.contains(source_via_rule)) {
        fail(logger, 87, ndrMessage(rule.name, "references an unknown DEF USEVIARULE"));
      }
      const auto& via_rule = via_rule_storage.viaRuleGenerate(source_via_rule);
      const auto z = requireViaRuleBottomRoutingIndex(
          source, layers, technology, source_via_rule, logger, ndrMessage(rule.name, "USEVIARULE"));
      destination->addViaRule(requireImportedViaRule(technology, via_rule.name, logger, ndrMessage(rule.name, "USEVIARULE")), z);
    }
    technology.addNDR(std::move(destination));
  }
}

void updateNdrSafeDistance(frTechObject& technology, RouterConfiguration& configuration)
{
  for (const auto& layer : technology.getLayers()) {
    if (layer->getType() != odb::dbTechLayerType::ROUTING) {
      continue;
    }
    configuration.MTSAFEDIST = std::max(configuration.MTSAFEDIST,
                                        technology.getMaxNondefaultSpacing(layer->getLayerNum() / 2 - 1));
  }
}

void validateGlobalTechnology(const eccdb::TechStore& source, frTechObject& technology, utl::Logger& logger)
{
  const auto& globals = source.globalStorage();
  if (!globals.hasUnits()) {
    fail(logger, 45, "native EccDB technology importer requires LEF DATABASE UNITS");
  }
  const auto& units = globals.getUnits();
  if ((units.flags & eccdb::TechGlobalUnitsFlag::kHasDatabaseUnitsPerMicron) == 0u) {
    fail(logger, 46, "native EccDB technology importer requires LEF DATABASE MICRONS");
  }
  if ((units.flags & ~eccdb::TechGlobalUnitsFlag::kHasDatabaseUnitsPerMicron) != 0u) {
    fail(logger, 47, "native EccDB technology importer cannot retain non-database LEF UNITS");
  }
  const auto dbu = checkedUnsigned(units.database_units_per_micron, logger, "LEF DATABASE MICRONS");
  if (dbu == 0u) {
    fail(logger, 48, "LEF DATABASE MICRONS is zero");
  }
  technology.setDBUPerUU(dbu);

  if (globals.hasManufacturingGrid()) {
    technology.setManufacturingGrid(
        checkedUnsigned(globals.getManufacturingGrid().value, logger, "LEF MANUFACTURINGGRID"));
  } else {
    technology.setManufacturingGrid(0u);
  }
  // Parser::readTechAndLibs() does not copy MAXVIASTACK or non-GENERATE
  // VIARULE data into frTechObject.  EccDB retains both source forms for
  // LEF round-tripping, but they must not block a parity import.
}

}  // namespace

void readEccdbTechnology(const eccdb::PhysicalDesignService& physical,
                         frDesign& design,
                         RouterConfiguration& configuration,
                         utl::Logger& logger)
{
  auto* technology = design.getTech();
  if (technology == nullptr) {
    fail(logger, 53, "native EccDB technology import requires an initialized DRT technology object");
  }
  if (!technology->getLayers().empty() || !technology->getVias().empty()) {
    fail(logger, 54, "native EccDB technology import requires an empty DRT technology object");
  }

  const auto& source = physical.tech();
  validateGlobalTechnology(source, *technology, logger);

  ImportedLayers layers;
  const auto& registry = source.techRegistry().registry();
  for (const auto source_id : source.layerSequence()) {
    const bool routing = registry.all_of<eccdb::TechRoutingLayer>(source_id.entity());
    const bool cut = registry.all_of<eccdb::TechCutLayer>(source_id.entity());
    // Parser::setLayers() deliberately keeps only ROUTING and CUT layers in
    // frTechObject.  EccDB retains OVERLAP and other LEF layer classes for
    // round-tripping, but they are outside TritonRoute's routing stack.
    if (!routing && !cut) {
      continue;
    }
    if (routing && cut) {
      const auto& info = source.layerInfo(source_id);
      fail(logger, 55, layerMessage(info.name, "is not exactly one of ROUTING or CUT"));
    }
    const auto& info = source.layerInfo(source_id);
    validateLayerInfo(info, logger);
    if (routing) {
      const auto routing_id = eccdb::TechRoutingLayerId{source_id.entity()};
      rejectUnrepresentedLayerProperties(
          source, source_id, info, !source.routingLayerStorage().lef58CornerSpacingRules(routing_id).empty(), logger);
      importRoutingLayer(source,
                         source_id,
                         info,
                         source.routingLayerStorage().routingLayer(eccdb::TechRoutingLayerId{source_id.entity()}),
                         *technology,
                         layers,
                         configuration,
                         logger);
    } else {
      rejectUnrepresentedLayerProperties(source, source_id, info, false, logger);
      importCutLayer(source,
                     source_id,
                     info,
                     source.cutLayerStorage().cutLayer(eccdb::TechCutLayerId{source_id.entity()}),
                     *technology,
                     layers,
                     logger);
    }
  }

  if (layers.highest_routing < 0) {
    fail(logger, 56, "native EccDB technology import found no ROUTING layer");
  }
  if (configuration.TOP_ROUTING_LAYER == std::numeric_limits<int>::max()) {
    configuration.TOP_ROUTING_LAYER = layers.highest_routing;
  }

  importTechVias(physical, *technology, layers, logger);
  importTechViaRuleGenerates(physical, *technology, layers, logger);
  importDesignVias(physical, *technology, layers, logger);
  importTechNonDefaultRules(source, *technology, layers, logger);
  importDesignNonDefaultRules(physical, *technology, layers, logger);
  updateNdrSafeDistance(*technology, configuration);

  // This is the same public, value-backed selection path used by the ODB
  // parser. Standard Parser::postProcess() later repeats it and fills CUT
  // widths plus constraint layer indices after design import.
  Parser(nullptr, &design, &logger, &configuration).initDefaultVias();
}

}  // namespace drt::io
