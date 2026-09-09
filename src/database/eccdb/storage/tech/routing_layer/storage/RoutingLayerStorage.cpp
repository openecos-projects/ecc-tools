#include "tech/routing_layer/storage/RoutingLayerStorage.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace eccdb {
namespace {

template <typename Value>
void requireStrictlyIncreasing(const std::vector<Value>& values, const char* message)
{
  if (values.empty()) {
    throw std::invalid_argument(message);
  }
  for (size_t index = 1; index < values.size(); ++index) {
    if (values[index - 1] >= values[index]) {
      throw std::invalid_argument(message);
    }
  }
}

size_t checkedMatrixCellCount(size_t rows, size_t columns, const char* message)
{
  if (rows == 0 || columns == 0 || rows > std::numeric_limits<size_t>::max() / columns) {
    throw std::invalid_argument(message);
  }
  return rows * columns;
}

size_t lastStrictlyLowerIndex(std::span<const int32_t> thresholds, int32_t value)
{
  const auto first_not_lower = std::lower_bound(thresholds.begin(), thresholds.end(), value);
  return first_not_lower == thresholds.begin() ? 0u : static_cast<size_t>(first_not_lower - thresholds.begin() - 1);
}

size_t selectTwoWidthAxis(std::span<const TechRoutingTwoWidthsSpacingTableWidth> axes, int32_t width, int32_t parallel_run_length)
{
  size_t selected = 0;
  for (size_t index = 0; index < axes.size(); ++index) {
    const auto& axis = axes[index];
    if (width > axis.width && (!axis.has_prl || parallel_run_length > axis.prl)) {
      selected = index;
    }
  }
  return selected;
}

struct InterpolationBracket
{
  size_t low = 0;
  size_t high = 0;
  double fraction = 0.0;
};

template <typename Axis, typename Value>
InterpolationBracket interpolationBracket(std::span<const Axis> axis, Value value, const char* range_message)
{
  if (axis.empty() || axis.size() == 1) {
    return {};
  }

  const auto coordinate = static_cast<double>(value);
  if (coordinate < static_cast<double>(axis.front()) || coordinate > static_cast<double>(axis.back())) {
    throw std::out_of_range(range_message);
  }

  const auto upper = std::lower_bound(axis.begin(), axis.end(), value);
  if (upper != axis.end() && *upper == value) {
    const auto index = static_cast<size_t>(upper - axis.begin());
    return {.low = index, .high = index};
  }

  const auto high = static_cast<size_t>(upper - axis.begin());
  const auto low = high - 1;
  const auto low_value = static_cast<double>(axis[low]);
  const auto high_value = static_cast<double>(axis[high]);
  return {.low = low, .high = high, .fraction = (coordinate - low_value) / (high_value - low_value)};
}

double interpolate(double low, double high, double fraction)
{
  return low + (high - low) * fraction;
}

}  // namespace

TechRoutingLayerId TechRoutingLayerStorage::createLayer(TechLayerInfo info, TechRoutingLayer routing)
{
  validateLayerInfo(info);
  const auto entity = _registry.create();
  try {
    _registry.emplace<TechLayerInfo>(entity, std::move(info));
    _registry.emplace<TechRoutingLayer>(entity, std::move(routing));
  } catch (...) {
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
  return TechRoutingLayerId{entity};
}

void TechRoutingLayerStorage::validateLayerInfo(const TechLayerInfo& info)
{
  if (info.name.empty()) {
    throw std::invalid_argument("routing layer name is required");
  }
  if (info.mask_count == 0) {
    throw std::invalid_argument("routing layer mask count must be positive");
  }
}

bool TechRoutingLayerStorage::contains(TechRoutingLayerId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechLayerInfo, TechRoutingLayer>(id.entity());
}

TechRoutingLayerId TechRoutingLayerStorage::findLayerById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity) && _registry.all_of<TechLayerInfo, TechRoutingLayer>(entity) ? TechRoutingLayerId{entity}
                                                                                              : TechRoutingLayerId{};
}

TechLayerInfo& TechRoutingLayerStorage::layerInfo(TechRoutingLayerId id)
{
  ensureLayer(id);
  return _registry.get<TechLayerInfo>(id.entity());
}

const TechLayerInfo& TechRoutingLayerStorage::layerInfo(TechRoutingLayerId id) const
{
  ensureLayer(id);
  return _registry.get<TechLayerInfo>(id.entity());
}

TechRoutingLayer& TechRoutingLayerStorage::routingLayer(TechRoutingLayerId id)
{
  ensureLayer(id);
  return _registry.get<TechRoutingLayer>(id.entity());
}

const TechRoutingLayer& TechRoutingLayerStorage::routingLayer(TechRoutingLayerId id) const
{
  ensureLayer(id);
  return _registry.get<TechRoutingLayer>(id.entity());
}

uint32_t TechRoutingLayerStorage::ruleCount(TechRoutingLayerId id) const
{
  ensureLayer(id);
  const uint64_t count
      = repeatedRuleCount<TechRoutingSpacingRule>(id) + repeatedRuleCount<TechRoutingEndOfLineSpacingRule>(id)
        + repeatedRuleCount<TechRoutingMinEncloseAreaRule>(id)
        + repeatedRuleCount<TechRoutingMinStepRule>(id) + repeatedRuleCount<TechRoutingMinimumCutRule>(id)
        + repeatedRuleCount<TechRoutingSpacingNotchLengthRule>(id) + repeatedRuleCount<TechRoutingPrlSpacingTableRule>(id)
        + repeatedRuleCount<TechRoutingInfluenceSpacingTableRule>(id) + repeatedRuleCount<TechRoutingTwoWidthsSpacingTableRule>(id)
        + repeatedRuleCount<TechRoutingCurrentDensityRule>(id) + repeatedRuleCount<TechRoutingLef58AreaRule>(id)
        + repeatedRuleCount<TechRoutingLef58CornerFillSpacingRule>(id) + repeatedRuleCount<TechRoutingLef58CornerSpacingRule>(id)
        + repeatedRuleCount<TechRoutingLef58MinimumCutRule>(id)
        + repeatedRuleCount<TechRoutingLef58MinStepRule>(id) + repeatedRuleCount<TechRoutingLef58WidthTableRule>(id)
        + repeatedRuleCount<TechRoutingLef58SpacingEolRule>(id) + repeatedRuleCount<TechRoutingLef58SpacingNotchLengthRule>(id)
        + repeatedRuleCount<TechRoutingLef58SpacingTableJogToJogRule>(id);
  if (count > std::numeric_limits<uint32_t>::max()) {
    throw std::overflow_error("too many routing rules on one layer");
  }
  return static_cast<uint32_t>(count);
}

TechRoutingSpacingRuleId TechRoutingLayerStorage::addSpacingRule(TechRoutingLayerId owner, TechRoutingSpacingRule rule)
{
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingSpacingRuleId> TechRoutingLayerStorage::spacingRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingSpacingRule>(owner);
}

TechRoutingEndOfLineSpacingRuleId TechRoutingLayerStorage::addEndOfLineSpacingRule(TechRoutingLayerId owner,
                                                                                    TechRoutingEndOfLineSpacingRule rule)
{
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingEndOfLineSpacingRuleId> TechRoutingLayerStorage::endOfLineSpacingRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingEndOfLineSpacingRule>(owner);
}

TechRoutingMinEncloseAreaRuleId TechRoutingLayerStorage::addMinEncloseAreaRule(TechRoutingLayerId owner, TechRoutingMinEncloseAreaRule rule)
{
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingMinEncloseAreaRuleId> TechRoutingLayerStorage::minEncloseAreaRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingMinEncloseAreaRule>(owner);
}

TechRoutingMinStepRuleId TechRoutingLayerStorage::addMinStepRule(TechRoutingLayerId owner, TechRoutingMinStepRule rule)
{
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingMinStepRuleId> TechRoutingLayerStorage::minStepRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingMinStepRule>(owner);
}

TechRoutingMinimumCutRuleId TechRoutingLayerStorage::addMinimumCutRule(TechRoutingLayerId owner, TechRoutingMinimumCutRule rule)
{
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingMinimumCutRuleId> TechRoutingLayerStorage::minimumCutRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingMinimumCutRule>(owner);
}

TechRoutingSpacingNotchLengthRuleId TechRoutingLayerStorage::addSpacingNotchLengthRule(TechRoutingLayerId owner,
                                                                                       TechRoutingSpacingNotchLengthRule rule)
{
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingSpacingNotchLengthRuleId> TechRoutingLayerStorage::spacingNotchLengthRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingSpacingNotchLengthRule>(owner);
}

TechRoutingPrlSpacingTableRuleId TechRoutingLayerStorage::addPrlSpacingTableRule(TechRoutingLayerId owner,
                                                                                 TechRoutingPrlSpacingTableRule rule)
{
  validatePrlSpacingTable(rule);
  rule.flags &= ~(TechRoutingPrlSpacingTableRuleFlag::kHasExceptWithin | TechRoutingPrlSpacingTableRuleFlag::kHasInfluence);
  if (!rule.except_withins.empty()) {
    rule.flags |= TechRoutingPrlSpacingTableRuleFlag::kHasExceptWithin;
  }
  if (!rule.influences.empty()) {
    rule.flags |= TechRoutingPrlSpacingTableRuleFlag::kHasInfluence;
  }
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingPrlSpacingTableRuleId> TechRoutingLayerStorage::prlSpacingTableRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingPrlSpacingTableRule>(owner);
}

std::span<const int32_t> TechRoutingLayerStorage::prlSpacingTableWidths(TechRoutingPrlSpacingTableRuleId rule_id) const
{
  return rule(rule_id).widths;
}

std::span<const int32_t> TechRoutingLayerStorage::prlSpacingTableParallelRunLengths(TechRoutingPrlSpacingTableRuleId rule_id) const
{
  return rule(rule_id).parallel_run_lengths;
}

std::span<const int32_t> TechRoutingLayerStorage::prlSpacingTableCells(TechRoutingPrlSpacingTableRuleId rule_id) const
{
  return rule(rule_id).cells;
}

std::span<const TechRoutingPrlSpacingTableExceptWithin> TechRoutingLayerStorage::prlSpacingTableExceptWithins(
    TechRoutingPrlSpacingTableRuleId rule_id) const
{
  return rule(rule_id).except_withins;
}

std::span<const TechRoutingPrlSpacingTableInfluence> TechRoutingLayerStorage::prlSpacingTableInfluences(
    TechRoutingPrlSpacingTableRuleId rule_id) const
{
  return rule(rule_id).influences;
}

int32_t TechRoutingLayerStorage::prlSpacingTableCell(TechRoutingPrlSpacingTableRuleId rule_id, uint32_t width_index,
                                                     uint32_t parallel_run_length_index) const
{
  const auto& spacing_table = rule(rule_id);
  if (width_index >= spacing_table.widthCount() || parallel_run_length_index >= spacing_table.parallelRunLengthCount()) {
    throw std::out_of_range("routing PRL spacing table index out of range");
  }
  const auto cells = prlSpacingTableCells(rule_id);
  const auto index = static_cast<size_t>(width_index) * spacing_table.parallelRunLengthCount() + parallel_run_length_index;
  if (index >= cells.size()) {
    throw std::out_of_range("routing PRL spacing table cell list is incomplete");
  }
  return cells[index];
}

int32_t TechRoutingLayerStorage::prlSpacingFor(TechRoutingPrlSpacingTableRuleId rule_id, int32_t width_a, int32_t width_b,
                                               int32_t parallel_run_length) const
{
  const auto widths = prlSpacingTableWidths(rule_id);
  const auto lengths = prlSpacingTableParallelRunLengths(rule_id);
  if (widths.empty() || lengths.empty()) {
    throw std::logic_error("routing PRL spacing table has no axes");
  }
  return prlSpacingTableCell(rule_id, static_cast<uint32_t>(lastStrictlyLowerIndex(widths, std::max(width_a, width_b))),
                             static_cast<uint32_t>(lastStrictlyLowerIndex(lengths, parallel_run_length)));
}

std::optional<int32_t> TechRoutingLayerStorage::prlInfluenceSpacingFor(TechRoutingPrlSpacingTableRuleId rule_id, int32_t width,
                                                                       int32_t distance) const
{
  std::optional<int32_t> result;
  for (const auto& influence : prlSpacingTableInfluences(rule_id)) {
    if (width > influence.width && distance < influence.within && (!result || influence.spacing > *result)) {
      result = influence.spacing;
    }
  }
  return result;
}

TechRoutingInfluenceSpacingTableRuleId TechRoutingLayerStorage::addInfluenceSpacingTableRule(TechRoutingLayerId owner,
                                                                                             TechRoutingInfluenceSpacingTableRule rule)
{
  validateInfluenceSpacingTable(rule);
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingInfluenceSpacingTableRuleId> TechRoutingLayerStorage::influenceSpacingTableRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingInfluenceSpacingTableRule>(owner);
}

std::span<const TechRoutingInfluenceSpacingTableEntry> TechRoutingLayerStorage::influenceSpacingTableEntries(
    TechRoutingInfluenceSpacingTableRuleId rule_id) const
{
  return rule(rule_id).entries;
}

std::optional<int32_t> TechRoutingLayerStorage::influenceSpacingFor(TechRoutingInfluenceSpacingTableRuleId rule_id, int32_t width,
                                                                    int32_t distance) const
{
  std::optional<int32_t> result;
  for (const auto& entry : influenceSpacingTableEntries(rule_id)) {
    if (width > entry.width && distance < entry.within && (!result || entry.spacing > *result)) {
      result = entry.spacing;
    }
  }
  return result;
}

TechRoutingTwoWidthsSpacingTableRuleId TechRoutingLayerStorage::addTwoWidthsSpacingTableRule(TechRoutingLayerId owner,
                                                                                             TechRoutingTwoWidthsSpacingTableRule rule)
{
  validateTwoWidthsSpacingTable(rule);
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingTwoWidthsSpacingTableRuleId> TechRoutingLayerStorage::twoWidthsSpacingTableRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingTwoWidthsSpacingTableRule>(owner);
}

std::span<const TechRoutingTwoWidthsSpacingTableWidth> TechRoutingLayerStorage::twoWidthsSpacingTableWidths(
    TechRoutingTwoWidthsSpacingTableRuleId rule_id) const
{
  return rule(rule_id).widths;
}

std::span<const int32_t> TechRoutingLayerStorage::twoWidthsSpacingTableCells(TechRoutingTwoWidthsSpacingTableRuleId rule_id) const
{
  return rule(rule_id).cells;
}

int32_t TechRoutingLayerStorage::twoWidthsSpacingTableCell(TechRoutingTwoWidthsSpacingTableRuleId rule_id, uint32_t row_index,
                                                           uint32_t column_index) const
{
  const auto& spacing_table = rule(rule_id);
  if (row_index >= spacing_table.widthCount() || column_index >= spacing_table.widthCount()) {
    throw std::out_of_range("routing two-width spacing table index out of range");
  }
  const auto cells = twoWidthsSpacingTableCells(rule_id);
  const auto index = static_cast<size_t>(row_index) * spacing_table.widthCount() + column_index;
  if (index >= cells.size()) {
    throw std::out_of_range("routing two-width spacing table cell list is incomplete");
  }
  return cells[index];
}

int32_t TechRoutingLayerStorage::twoWidthsSpacingFor(TechRoutingTwoWidthsSpacingTableRuleId rule_id, int32_t width_a, int32_t width_b,
                                                     int32_t parallel_run_length) const
{
  const auto axes = twoWidthsSpacingTableWidths(rule_id);
  if (axes.empty()) {
    throw std::logic_error("routing two-width spacing table has no axis");
  }
  const auto safe_parallel_run_length = std::max(0, parallel_run_length);
  return twoWidthsSpacingTableCell(rule_id, static_cast<uint32_t>(selectTwoWidthAxis(axes, width_a, safe_parallel_run_length)),
                                   static_cast<uint32_t>(selectTwoWidthAxis(axes, width_b, safe_parallel_run_length)));
}

TechRoutingCurrentDensityRuleId TechRoutingLayerStorage::addCurrentDensityRule(TechRoutingLayerId owner, TechRoutingCurrentDensityRule rule)
{
  validateCurrentDensity(rule);
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingCurrentDensityRuleId> TechRoutingLayerStorage::currentDensityRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingCurrentDensityRule>(owner);
}

std::span<const double> TechRoutingLayerStorage::currentDensityFrequencies(TechRoutingCurrentDensityRuleId rule_id) const
{
  return rule(rule_id).frequencies;
}

std::span<const int32_t> TechRoutingLayerStorage::currentDensityWidths(TechRoutingCurrentDensityRuleId rule_id) const
{
  return rule(rule_id).widths;
}

std::span<const double> TechRoutingLayerStorage::currentDensityTableEntries(TechRoutingCurrentDensityRuleId rule_id) const
{
  return rule(rule_id).table_entries;
}

double TechRoutingLayerStorage::currentDensityTableEntry(TechRoutingCurrentDensityRuleId rule_id, uint32_t frequency_index,
                                                         uint32_t width_index) const
{
  const auto& current_density = rule(rule_id);
  const auto frequency_rows = current_density.frequencyCount() == 0u ? 1u : current_density.frequencyCount();
  const auto width_columns = current_density.widthCount() == 0u ? 1u : current_density.widthCount();
  if (frequency_index >= frequency_rows || width_index >= width_columns) {
    throw std::out_of_range("routing current-density table index out of range");
  }
  const auto entries = currentDensityTableEntries(rule_id);
  const auto index = static_cast<size_t>(frequency_index) * width_columns + width_index;
  if (index >= entries.size()) {
    throw std::out_of_range("routing current-density table-entry list is incomplete");
  }
  return entries[index];
}

double TechRoutingLayerStorage::currentDensityAt(TechRoutingCurrentDensityRuleId rule_id, double frequency, int32_t width) const
{
  const auto& current_density = rule(rule_id);
  if ((current_density.flags & TechRoutingCurrentDensityRuleFlag::kHasScalar) != 0u) {
    return current_density.scalar;
  }

  const auto frequencies = currentDensityFrequencies(rule_id);
  const auto widths = currentDensityWidths(rule_id);
  const auto entries = currentDensityTableEntries(rule_id);
  if (widths.empty()) {
    if (entries.size() != (frequencies.empty() ? 1u : frequencies.size())) {
      throw std::logic_error("routing current-density table has invalid implicit-width dimensions");
    }
    const auto frequency_bracket = interpolationBracket(frequencies, frequency, "routing current-density frequency outside table range");
    return interpolate(entries[frequency_bracket.low], entries[frequency_bracket.high], frequency_bracket.fraction);
  }

  const auto row_count = frequencies.empty() ? size_t{1} : frequencies.size();
  if (entries.size() != row_count * widths.size()) {
    throw std::logic_error("routing current-density table has invalid dimensions");
  }

  const auto width_bracket = interpolationBracket(widths, width, "routing current-density width outside table range");
  const auto frequency_bracket = interpolationBracket(frequencies, frequency, "routing current-density frequency outside table range");
  const auto at_width = [&](size_t frequency_index) {
    const auto row_offset = frequency_index * widths.size();
    return interpolate(entries[row_offset + width_bracket.low], entries[row_offset + width_bracket.high], width_bracket.fraction);
  };

  return interpolate(at_width(frequency_bracket.low), at_width(frequency_bracket.high), frequency_bracket.fraction);
}

TechRoutingLef58AreaRuleId TechRoutingLayerStorage::addLef58AreaRule(TechRoutingLayerId owner, TechRoutingLef58AreaRule rule)
{
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingLef58AreaRuleId> TechRoutingLayerStorage::lef58AreaRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingLef58AreaRule>(owner);
}

std::span<const TechRoutingLef58AreaExceptMinSize> TechRoutingLayerStorage::lef58AreaExceptMinSizes(
    TechRoutingLef58AreaRuleId rule_id) const
{
  return rule(rule_id).except_min_sizes;
}

TechRoutingLef58CornerFillSpacingRuleId TechRoutingLayerStorage::addLef58CornerFillSpacingRule(TechRoutingLayerId owner,
                                                                                               TechRoutingLef58CornerFillSpacingRule rule)
{
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingLef58CornerFillSpacingRuleId> TechRoutingLayerStorage::lef58CornerFillSpacingRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingLef58CornerFillSpacingRule>(owner);
}

TechRoutingLef58CornerSpacingRuleId TechRoutingLayerStorage::addLef58CornerSpacingRule(TechRoutingLayerId owner,
                                                                                       TechRoutingLef58CornerSpacingRule rule)
{
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingLef58CornerSpacingRuleId> TechRoutingLayerStorage::lef58CornerSpacingRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingLef58CornerSpacingRule>(owner);
}

std::span<const TechRoutingLef58CornerSpacingWidth> TechRoutingLayerStorage::lef58CornerSpacingWidths(
    TechRoutingLef58CornerSpacingRuleId rule_id) const
{
  return rule(rule_id).width_spacings;
}

TechRoutingLef58MinimumCutRuleId TechRoutingLayerStorage::addLef58MinimumCutRule(TechRoutingLayerId owner,
                                                                                 TechRoutingLef58MinimumCutRule rule)
{
  rule.flags &= ~TechRoutingLef58MinimumCutRuleFlag::kHasCutClasses;
  if (!rule.cutclasses.empty()) {
    rule.flags |= TechRoutingLef58MinimumCutRuleFlag::kHasCutClasses;
  }
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingLef58MinimumCutRuleId> TechRoutingLayerStorage::lef58MinimumCutRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingLef58MinimumCutRule>(owner);
}

std::span<const TechRoutingLef58MinimumCutClass> TechRoutingLayerStorage::lef58MinimumCutClasses(
    TechRoutingLef58MinimumCutRuleId rule_id) const
{
  return rule(rule_id).cutclasses;
}

TechRoutingLef58MinStepRuleId TechRoutingLayerStorage::addLef58MinStepRule(TechRoutingLayerId owner, TechRoutingLef58MinStepRule rule)
{
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingLef58MinStepRuleId> TechRoutingLayerStorage::lef58MinStepRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingLef58MinStepRule>(owner);
}

TechRoutingLef58WidthTableRuleId TechRoutingLayerStorage::addLef58WidthTableRule(TechRoutingLayerId owner,
                                                                                 TechRoutingLef58WidthTableRule rule)
{
  validateLef58WidthTable(rule);
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingLef58WidthTableRuleId> TechRoutingLayerStorage::lef58WidthTableRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingLef58WidthTableRule>(owner);
}

std::span<const int32_t> TechRoutingLayerStorage::lef58WidthTableWidths(TechRoutingLef58WidthTableRuleId rule_id) const
{
  return rule(rule_id).widths;
}

TechRoutingLef58SpacingEolRuleId TechRoutingLayerStorage::addLef58SpacingEolRule(TechRoutingLayerId owner,
                                                                                 TechRoutingLef58SpacingEolRule rule)
{
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingLef58SpacingEolRuleId> TechRoutingLayerStorage::lef58SpacingEolRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingLef58SpacingEolRule>(owner);
}

TechRoutingLef58SpacingNotchLengthRuleId TechRoutingLayerStorage::addLef58SpacingNotchLengthRule(
    TechRoutingLayerId owner, TechRoutingLef58SpacingNotchLengthRule rule)
{
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingLef58SpacingNotchLengthRuleId> TechRoutingLayerStorage::lef58SpacingNotchLengthRules(TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingLef58SpacingNotchLengthRule>(owner);
}

TechRoutingLef58SpacingTableJogToJogRuleId TechRoutingLayerStorage::addLef58SpacingTableJogToJogRule(
    TechRoutingLayerId owner, TechRoutingLef58SpacingTableJogToJogRule rule)
{
  return createRepeatedRule(owner, std::move(rule));
}

std::vector<TechRoutingLef58SpacingTableJogToJogRuleId> TechRoutingLayerStorage::lef58SpacingTableJogToJogRules(
    TechRoutingLayerId owner) const
{
  return repeatedRules<TechRoutingLef58SpacingTableJogToJogRule>(owner);
}

std::span<const TechRoutingLef58JogToJogWidth> TechRoutingLayerStorage::lef58JogToJogWidths(
    TechRoutingLef58SpacingTableJogToJogRuleId rule_id) const
{
  return rule(rule_id).widths;
}

void TechRoutingLayerStorage::ensureLayer(TechRoutingLayerId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid tech routing layer id");
  }
}

TechEntity TechRoutingLayerStorage::createOwnedRule(TechRoutingLayerId owner)
{
  ensureLayer(owner);
  const auto entity = _registry.create();
  try {
    _registry.emplace<TechRuleOwner>(entity, TechRuleOwner{.owner = owner.entity()});
  } catch (...) {
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
  return entity;
}

void TechRoutingLayerStorage::validatePrlSpacingTable(const TechRoutingPrlSpacingTableRule& rule)
{
  requireStrictlyIncreasing(rule.widths, "routing PRL widths must be strictly increasing");
  requireStrictlyIncreasing(rule.parallel_run_lengths, "routing PRL lengths must be strictly increasing");
  if (rule.cells.size()
      != checkedMatrixCellCount(rule.widths.size(), rule.parallel_run_lengths.size(), "invalid routing PRL table dimensions")) {
    throw std::invalid_argument("routing PRL table cells do not match its axes");
  }
  for (const auto& except_within : rule.except_withins) {
    if (except_within.width_index >= rule.widths.size() || except_within.low > except_within.high) {
      throw std::invalid_argument("invalid routing PRL EXCEPTWITHIN row");
    }
  }
  for (size_t index = 1; index < rule.influences.size(); ++index) {
    if (rule.influences[index - 1].width >= rule.influences[index].width) {
      throw std::invalid_argument("routing PRL INFLUENCE widths must be strictly increasing");
    }
  }
}

void TechRoutingLayerStorage::validateInfluenceSpacingTable(const TechRoutingInfluenceSpacingTableRule& rule)
{
  if (rule.entries.empty()) {
    throw std::invalid_argument("routing influence spacing table must contain entries");
  }
  for (size_t index = 1; index < rule.entries.size(); ++index) {
    if (rule.entries[index - 1].width >= rule.entries[index].width) {
      throw std::invalid_argument("routing influence spacing widths must be strictly increasing");
    }
  }
}

void TechRoutingLayerStorage::validateTwoWidthsSpacingTable(const TechRoutingTwoWidthsSpacingTableRule& rule)
{
  if (rule.widths.empty()) {
    throw std::invalid_argument("routing two-width spacing table must contain widths");
  }
  for (size_t index = 0; index < rule.widths.size(); ++index) {
    if (index > 0 && rule.widths[index - 1].width > rule.widths[index].width) {
      throw std::invalid_argument("routing two-width widths must be nondecreasing");
    }
    if (index > 0 && rule.widths[index - 1].has_prl && rule.widths[index].has_prl && rule.widths[index - 1].prl > rule.widths[index].prl) {
      throw std::invalid_argument("routing two-width PRLs must be nondecreasing");
    }
  }
  const auto count = checkedMatrixCellCount(rule.widths.size(), rule.widths.size(), "invalid routing two-width table dimensions");
  if (rule.cells.size() != count) {
    throw std::invalid_argument("routing two-width table cells do not match its axes");
  }
  for (size_t row = 0; row < rule.widths.size(); ++row) {
    for (size_t column = 0; column < rule.widths.size(); ++column) {
      const auto value = rule.cells[row * rule.widths.size() + column];
      if (column > 0 && rule.cells[row * rule.widths.size() + column - 1] > value) {
        throw std::invalid_argument("routing two-width table rows must be nondecreasing");
      }
      if (row > 0 && rule.cells[(row - 1) * rule.widths.size() + column] > value) {
        throw std::invalid_argument("routing two-width table columns must be nondecreasing");
      }
    }
  }
}

void TechRoutingLayerStorage::validateCurrentDensity(TechRoutingCurrentDensityRule& rule)
{
  const bool scalar = (rule.flags & TechRoutingCurrentDensityRuleFlag::kHasScalar) != 0u;
  const bool has_table_input = !rule.frequencies.empty() || !rule.widths.empty() || !rule.table_entries.empty();
  if (scalar && has_table_input) {
    throw std::invalid_argument("routing current-density scalar and table are mutually exclusive");
  }

  constexpr uint32_t kTableFlags = TechRoutingCurrentDensityRuleFlag::kHasFrequencies | TechRoutingCurrentDensityRuleFlag::kHasWidths
                                   | TechRoutingCurrentDensityRuleFlag::kHasTableEntries;
  if (scalar) {
    rule.flags &= ~kTableFlags;
    return;
  }

  if (!rule.widths.empty()) {
    requireStrictlyIncreasing(rule.widths, "routing current-density widths must be strictly increasing");
  }
  if (!rule.frequencies.empty()) {
    requireStrictlyIncreasing(rule.frequencies, "routing current-density frequencies must be strictly increasing");
  }
  const auto frequency_rows = rule.frequencies.empty() ? size_t{1} : rule.frequencies.size();
  const auto width_columns = rule.widths.empty() ? size_t{1} : rule.widths.size();
  if (rule.table_entries.size() != checkedMatrixCellCount(frequency_rows, width_columns, "invalid routing current-density dimensions")) {
    throw std::invalid_argument("routing current-density entries do not match its axes");
  }

  rule.flags &= ~(TechRoutingCurrentDensityRuleFlag::kHasScalar | kTableFlags);
  if (!rule.frequencies.empty()) {
    rule.flags |= TechRoutingCurrentDensityRuleFlag::kHasFrequencies;
  }
  if (!rule.widths.empty()) {
    rule.flags |= TechRoutingCurrentDensityRuleFlag::kHasWidths;
  }
  rule.flags |= TechRoutingCurrentDensityRuleFlag::kHasTableEntries;
}

void TechRoutingLayerStorage::validateLef58WidthTable(const TechRoutingLef58WidthTableRule& rule)
{
  requireStrictlyIncreasing(rule.widths, "routing LEF58 width table widths must be strictly increasing");
}

}  // namespace eccdb
