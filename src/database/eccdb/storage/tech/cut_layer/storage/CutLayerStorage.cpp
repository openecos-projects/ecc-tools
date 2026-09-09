#include "tech/cut_layer/storage/CutLayerStorage.h"

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

void validateCutClassAxis(const std::vector<std::string>& names, const std::vector<CutClassEdge>& edges, const char* message)
{
  if (!edges.empty() && edges.size() != names.size()) {
    throw std::invalid_argument(message);
  }
  for (size_t index = 0; index < names.size(); ++index) {
    if (names[index].empty()) throw std::invalid_argument(message);
    const auto edge = edges.empty() ? CutClassEdge::kUnspecified : edges[index];
    for (size_t previous = 0; previous < index; ++previous) {
      const auto previous_edge = edges.empty() ? CutClassEdge::kUnspecified : edges[previous];
      if (names[previous] == names[index] && previous_edge == edge) throw std::invalid_argument(message);
    }
  }
}

void validateSpacingRule(const TechCutSpacingRule& rule)
{
  if (rule.spacing < 0) {
    throw std::invalid_argument("CUT spacing must be non-negative");
  }
  if ((rule.flags & TechCutSpacingRuleFlag::kSameNetPgOnly) != 0 && (rule.flags & TechCutSpacingRuleFlag::kSameNet) == 0) {
    throw std::invalid_argument("CUT SPACING PGONLY requires SAMENET");
  }
  if ((rule.flags & TechCutSpacingRuleFlag::kHasSecondLayer) != 0 && rule.second_layer_name.empty()) {
    throw std::invalid_argument("CUT SPACING LAYER requires a layer name");
  }
  if ((rule.flags & TechCutSpacingRuleFlag::kStack) != 0 && (rule.flags & TechCutSpacingRuleFlag::kHasSecondLayer) == 0) {
    throw std::invalid_argument("CUT SPACING STACK requires LAYER");
  }
  if ((rule.flags & TechCutSpacingRuleFlag::kExceptSamePgNet) != 0
      && (rule.flags & TechCutSpacingRuleFlag::kHasAdjacentCuts) == 0) {
    throw std::invalid_argument("CUT SPACING EXCEPTSAMEPGNET requires ADJACENTCUTS");
  }
  if ((rule.flags & TechCutSpacingRuleFlag::kHasCutArea) != 0 && rule.cut_area < 0) {
    throw std::invalid_argument("CUT SPACING AREA must be non-negative");
  }
}

void validateArraySpacingRule(const TechCutArraySpacingRule& rule)
{
  if ((rule.flags & TechCutArraySpacingRuleFlag::kHasViaWidth) != 0 && rule.via_width < 0) {
    throw std::invalid_argument("CUT array-spacing WIDTH must be non-negative");
  }
  uint32_t previous_count = 0;
  for (const auto& item : rule.items) {
    if (item.array_cut_count == 0 || item.array_cut_count <= previous_count || item.spacing < 0) {
      throw std::invalid_argument("CUT array-spacing entries must have increasing cut counts and non-negative spacing");
    }
    previous_count = item.array_cut_count;
  }
}

void validateOrthogonalSpacingTableRule(const TechCutOrthogonalSpacingTableRule& rule)
{
  if (rule.items.empty()) {
    throw std::invalid_argument("CUT orthogonal spacing table requires at least one row");
  }
  for (const auto& item : rule.items) {
    if (item.within < 0 || item.spacing < 0) {
      throw std::invalid_argument("CUT orthogonal spacing-table distances must be non-negative");
    }
  }
}

void validateEolSpacingRule(const TechCutLef58EolSpacingRule& rule)
{
  for (const auto& to_class : rule.to_classes) {
    if (to_class.cutclass_name.empty() || to_class.cut_spacing1 < 0 || to_class.cut_spacing2 < 0) {
      throw std::invalid_argument("CUT LEF58 EOL spacing TOCLASS entry is invalid");
    }
  }
}

void validateSpacingTableRule(const TechCutLef58SpacingTableRule& rule)
{
  if ((rule.flags & TechCutLef58SpacingTableRuleFlag::kMaxXY) != 0 && (rule.flags & TechCutLef58SpacingTableRuleFlag::kHasPrl) == 0) {
    throw std::invalid_argument("CUT LEF58 spacing table MAXXY requires PRL");
  }
  if ((rule.flags & TechCutLef58SpacingTableRuleFlag::kHasSecondLayer) != 0 && rule.second_layer_name.empty()) {
    throw std::invalid_argument("CUT LEF58 spacing table second layer is missing");
  }
  if ((rule.flags & TechCutLef58SpacingTableRuleFlag::kHasPrl) != 0 && rule.prl < 0) {
    throw std::invalid_argument("CUT LEF58 spacing table PRL is invalid");
  }
  if ((rule.flags & TechCutLef58SpacingTableRuleFlag::kHasDefault) != 0 && rule.default_spacing < 0) {
    throw std::invalid_argument("CUT LEF58 spacing table DEFAULT is invalid");
  }
  if ((rule.flags & TechCutLef58SpacingTableRuleFlag::kNoStack) != 0
      && (rule.flags & TechCutLef58SpacingTableRuleFlag::kHasSecondLayer) == 0) {
    throw std::invalid_argument("CUT LEF58 spacing table NOSTACK requires LAYER");
  }
  if ((rule.flags & TechCutLef58SpacingTableRuleFlag::kPrlForAlignedCut) != 0
      && ((rule.flags & TechCutLef58SpacingTableRuleFlag::kHasSecondLayer) == 0 || rule.prl_for_aligned_cut.empty())) {
    throw std::invalid_argument("CUT LEF58 spacing table PRLFORALIGNEDCUT requires LAYER and entries");
  }
  const auto same_kind_count = static_cast<unsigned>((rule.flags & TechCutLef58SpacingTableRuleFlag::kSameNet) != 0)
                               + static_cast<unsigned>((rule.flags & TechCutLef58SpacingTableRuleFlag::kSameMetal) != 0)
                               + static_cast<unsigned>((rule.flags & TechCutLef58SpacingTableRuleFlag::kSameVia) != 0);
  if (same_kind_count > 1) throw std::invalid_argument("CUT LEF58 spacing table same-kind qualifiers are exclusive");
  for (const auto& pair : rule.prl_for_aligned_cut) {
    if (pair.from.empty() || pair.to.empty()) throw std::invalid_argument("CUT LEF58 spacing table aligned-cut entry is invalid");
  }
  for (const auto& entry : rule.prl_entries) {
    if (entry.from.empty() || entry.to.empty() || entry.prl < 0)
      throw std::invalid_argument("CUT LEF58 spacing table PRL entry is invalid");
  }

  validateCutClassAxis(rule.cutclass1_names, rule.cutclass1_edges, "CUT LEF58 spacing table class1 axis is invalid");
  validateCutClassAxis(rule.cutclass2_names, rule.cutclass2_edges, "CUT LEF58 spacing table class2 axis is invalid");
  const auto expected_cells
      = checkedMatrixCellCount(rule.cutclass2_names.size(), rule.cutclass1_names.size(), "CUT LEF58 spacing table axes are invalid");
  if (rule.cells.size() != expected_cells) {
    throw std::invalid_argument("CUT LEF58 spacing table cell count does not match its axes");
  }
  for (const auto& cell : rule.cells) {
    if ((cell.has_cut_spacing1 && cell.cut_spacing1 < 0) || (cell.has_cut_spacing2 && cell.cut_spacing2 < 0)) {
      throw std::invalid_argument("CUT LEF58 spacing table spacing is invalid");
    }
  }
}

void validateCurrentDensityRule(TechCutCurrentDensityRule& rule)
{
  const bool has_scalar = (rule.flags & TechCutCurrentDensityRuleFlag::kHasScalar) != 0;
  const bool has_table_data = !rule.frequencies.empty() || !rule.cut_areas.empty() || !rule.table_entries.empty();
  if (has_scalar) {
    if (has_table_data) {
      throw std::invalid_argument("CUT current-density rule cannot have both scalar and table data");
    }
    rule.flags &= ~(TechCutCurrentDensityRuleFlag::kHasFrequencies | TechCutCurrentDensityRuleFlag::kHasCutAreas
                    | TechCutCurrentDensityRuleFlag::kHasTableEntries);
    return;
  }

  if (rule.table_entries.empty()) {
    throw std::invalid_argument("CUT current-density table requires table entries");
  }
  if (!rule.cut_areas.empty()) {
    requireStrictlyIncreasing(rule.cut_areas, "CUT current-density cut areas must be strictly increasing");
  }
  if (!rule.frequencies.empty()) {
    requireStrictlyIncreasing(rule.frequencies, "CUT current-density frequencies must be strictly increasing");
  }

  const auto frequency_rows = rule.frequencies.empty() ? 1u : rule.frequencies.size();
  const auto cut_area_columns = rule.cut_areas.empty() ? 1u : rule.cut_areas.size();
  const auto expected_entries = checkedMatrixCellCount(frequency_rows, cut_area_columns, "CUT current-density table axes are invalid");
  if (rule.table_entries.size() != expected_entries) {
    throw std::invalid_argument("CUT current-density table entry count does not match its axes");
  }

  rule.flags &= ~TechCutCurrentDensityRuleFlag::kHasScalar;
  rule.flags &= ~(TechCutCurrentDensityRuleFlag::kHasFrequencies | TechCutCurrentDensityRuleFlag::kHasCutAreas
                  | TechCutCurrentDensityRuleFlag::kHasTableEntries);
  if (!rule.frequencies.empty()) {
    rule.flags |= TechCutCurrentDensityRuleFlag::kHasFrequencies;
  }
  if (!rule.cut_areas.empty()) {
    rule.flags |= TechCutCurrentDensityRuleFlag::kHasCutAreas;
  }
  rule.flags |= TechCutCurrentDensityRuleFlag::kHasTableEntries;
}

struct InterpolationBracket
{
  size_t low = 0;
  size_t high = 0;
  double fraction = 0.0;
};

template <typename Axis, typename Value>
InterpolationBracket interpolationBracket(const std::vector<Axis>& axis, Value value, const char* range_message)
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

TechCutLayerId TechCutLayerStorage::createLayer(TechLayerInfo info, TechCutLayer cut)
{
  validateLayerInfo(info);
  const auto entity = _registry.create();
  try {
    _registry.emplace<TechLayerInfo>(entity, std::move(info));
    _registry.emplace<TechCutLayer>(entity, std::move(cut));
  } catch (...) {
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
  return TechCutLayerId{entity};
}

void TechCutLayerStorage::validateLayerInfo(const TechLayerInfo& info)
{
  if (info.name.empty()) {
    throw std::invalid_argument("cut layer name is required");
  }
  if (info.mask_count == 0) {
    throw std::invalid_argument("cut layer mask count must be positive");
  }
}

bool TechCutLayerStorage::contains(TechCutLayerId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechLayerInfo, TechCutLayer>(id.entity());
}

TechCutLayerId TechCutLayerStorage::findLayerById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity) && _registry.all_of<TechLayerInfo, TechCutLayer>(entity) ? TechCutLayerId{entity} : TechCutLayerId{};
}

TechLayerInfo& TechCutLayerStorage::layerInfo(TechCutLayerId id)
{
  ensureLayer(id);
  return _registry.get<TechLayerInfo>(id.entity());
}

const TechLayerInfo& TechCutLayerStorage::layerInfo(TechCutLayerId id) const
{
  ensureLayer(id);
  return _registry.get<TechLayerInfo>(id.entity());
}

TechCutLayer& TechCutLayerStorage::cutLayer(TechCutLayerId id)
{
  ensureLayer(id);
  return _registry.get<TechCutLayer>(id.entity());
}

const TechCutLayer& TechCutLayerStorage::cutLayer(TechCutLayerId id) const
{
  ensureLayer(id);
  return _registry.get<TechCutLayer>(id.entity());
}

uint32_t TechCutLayerStorage::ruleCount(TechCutLayerId id) const
{
  ensureLayer(id);

  uint32_t count = 0;
  if (const auto* refs = _registry.try_get<TechRuleRefs<TechCutSpacingRuleId>>(id.entity()); refs != nullptr) {
    count += static_cast<uint32_t>(refs->values.size());
  }
  if (const auto* refs = _registry.try_get<TechRuleRefs<TechCutEnclosureRuleId>>(id.entity()); refs != nullptr) {
    count += static_cast<uint32_t>(refs->values.size());
  }
  if (const auto* ref = _registry.try_get<TechRuleRef<TechCutArraySpacingRuleId>>(id.entity()); ref != nullptr && ref->rule) {
    ++count;
  }
  if (const auto* refs = _registry.try_get<TechRuleRefs<TechCutOrthogonalSpacingTableRuleId>>(id.entity()); refs != nullptr) {
    count += static_cast<uint32_t>(refs->values.size());
  }
  if (const auto* refs = _registry.try_get<TechRuleRefs<TechCutLef58CutClassRuleId>>(id.entity()); refs != nullptr) {
    count += static_cast<uint32_t>(refs->values.size());
  }
  if (const auto* refs = _registry.try_get<TechRuleRefs<TechCutLef58EnclosureRuleId>>(id.entity()); refs != nullptr) {
    count += static_cast<uint32_t>(refs->values.size());
  }
  if (const auto* refs = _registry.try_get<TechRuleRefs<TechCutLef58EnclosureEdgeRuleId>>(id.entity()); refs != nullptr) {
    count += static_cast<uint32_t>(refs->values.size());
  }
  if (const auto* ref = _registry.try_get<TechRuleRef<TechCutLef58EolEnclosureRuleId>>(id.entity()); ref != nullptr && ref->rule) {
    ++count;
  }
  if (const auto* ref = _registry.try_get<TechRuleRef<TechCutLef58EolSpacingRuleId>>(id.entity()); ref != nullptr && ref->rule) {
    ++count;
  }
  if (const auto* refs = _registry.try_get<TechRuleRefs<TechCutLef58SpacingTableRuleId>>(id.entity()); refs != nullptr) {
    count += static_cast<uint32_t>(refs->values.size());
  }
  if (const auto* refs = _registry.try_get<TechRuleRefs<TechCutCurrentDensityRuleId>>(id.entity()); refs != nullptr) {
    count += static_cast<uint32_t>(refs->values.size());
  }
  return count;
}

TechCutSpacingRuleId TechCutLayerStorage::addSpacingRule(TechCutLayerId owner, TechCutSpacingRule rule)
{
  validateSpacingRule(rule);
  return addRepeatedRule(owner, std::move(rule));
}

std::vector<TechCutSpacingRuleId> TechCutLayerStorage::spacingRules(TechCutLayerId owner) const
{
  return repeatedRules<TechCutSpacingRule>(owner);
}

TechCutEnclosureRuleId TechCutLayerStorage::addEnclosureRule(TechCutLayerId owner, TechCutEnclosureRule rule)
{
  return addRepeatedRule(owner, std::move(rule));
}

TechCutEnclosureRuleId TechCutLayerStorage::enclosureRule(TechCutLayerId owner, CutLayerSide side) const
{
  const auto rules = enclosureRules(owner);
  const auto match = std::find_if(rules.begin(), rules.end(), [&](const auto id) { return enclosureRule(id).side == side; });
  return match == rules.end() ? TechCutEnclosureRuleId{} : *match;
}

std::vector<TechCutEnclosureRuleId> TechCutLayerStorage::enclosureRules(TechCutLayerId owner) const
{
  return repeatedRules<TechCutEnclosureRule>(owner);
}

TechCutArraySpacingRuleId TechCutLayerStorage::setArraySpacingRule(TechCutLayerId owner, TechCutArraySpacingRule rule)
{
  ensureLayer(owner);
  validateArraySpacingRule(rule);

  const auto* ref = _registry.try_get<TechRuleRef<TechCutArraySpacingRuleId>>(owner.entity());
  if (ref != nullptr && ref->rule) {
    if (!hasArraySpacingRule(ref->rule)) {
      throw std::logic_error("corrupt CUT array-spacing rule reference");
    }
    _registry.replace<TechCutArraySpacingRule>(ref->rule.entity(), std::move(rule));
    return ref->rule;
  }

  const auto entity = createOwnedRule(owner);
  try {
    _registry.emplace<TechCutArraySpacingRule>(entity, std::move(rule));
    if (ref == nullptr) {
      _registry.emplace<TechRuleRef<TechCutArraySpacingRuleId>>(
          owner.entity(), TechRuleRef<TechCutArraySpacingRuleId>{.rule = TechCutArraySpacingRuleId{entity}});
    } else {
      _registry.get<TechRuleRef<TechCutArraySpacingRuleId>>(owner.entity()).rule = TechCutArraySpacingRuleId{entity};
    }
  } catch (...) {
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
  return TechCutArraySpacingRuleId{entity};
}

TechCutArraySpacingRuleId TechCutLayerStorage::arraySpacingRule(TechCutLayerId owner) const
{
  ensureLayer(owner);
  const auto* ref = _registry.try_get<TechRuleRef<TechCutArraySpacingRuleId>>(owner.entity());
  if (ref == nullptr) {
    return {};
  }
  if (ref->rule && !hasArraySpacingRule(ref->rule)) {
    throw std::logic_error("corrupt CUT array-spacing rule reference");
  }
  return ref->rule;
}

TechCutOrthogonalSpacingTableRuleId TechCutLayerStorage::addOrthogonalSpacingTableRule(
    TechCutLayerId owner, TechCutOrthogonalSpacingTableRule rule)
{
  validateOrthogonalSpacingTableRule(rule);
  return addRepeatedRule(owner, std::move(rule));
}

std::vector<TechCutOrthogonalSpacingTableRuleId> TechCutLayerStorage::orthogonalSpacingTableRules(TechCutLayerId owner) const
{
  return repeatedRules<TechCutOrthogonalSpacingTableRule>(owner);
}

TechCutLef58CutClassRuleId TechCutLayerStorage::addLef58CutClassRule(TechCutLayerId owner, TechCutLef58CutClassRule rule)
{
  return addRepeatedRule(owner, std::move(rule));
}

std::vector<TechCutLef58CutClassRuleId> TechCutLayerStorage::lef58CutClassRules(TechCutLayerId owner) const
{
  return repeatedRules<TechCutLef58CutClassRule>(owner);
}

TechCutLef58EnclosureRuleId TechCutLayerStorage::addLef58EnclosureRule(TechCutLayerId owner, TechCutLef58EnclosureRule rule)
{
  return addRepeatedRule(owner, std::move(rule));
}

std::vector<TechCutLef58EnclosureRuleId> TechCutLayerStorage::lef58EnclosureRules(TechCutLayerId owner) const
{
  return repeatedRules<TechCutLef58EnclosureRule>(owner);
}

TechCutLef58EnclosureEdgeRuleId TechCutLayerStorage::addLef58EnclosureEdgeRule(TechCutLayerId owner, TechCutLef58EnclosureEdgeRule rule)
{
  return addRepeatedRule(owner, std::move(rule));
}

std::vector<TechCutLef58EnclosureEdgeRuleId> TechCutLayerStorage::lef58EnclosureEdgeRules(TechCutLayerId owner) const
{
  return repeatedRules<TechCutLef58EnclosureEdgeRule>(owner);
}

TechCutLef58EolEnclosureRuleId TechCutLayerStorage::setLef58EolEnclosureRule(TechCutLayerId owner, TechCutLef58EolEnclosureRule rule)
{
  ensureLayer(owner);
  const auto* ref = _registry.try_get<TechRuleRef<TechCutLef58EolEnclosureRuleId>>(owner.entity());
  if (ref != nullptr && ref->rule) {
    if (!hasLef58EolEnclosureRule(ref->rule)) {
      throw std::logic_error("corrupt CUT LEF58 EOL enclosure rule reference");
    }
    _registry.replace<TechCutLef58EolEnclosureRule>(ref->rule.entity(), std::move(rule));
    return ref->rule;
  }

  const auto entity = createOwnedRule(owner);
  try {
    _registry.emplace<TechCutLef58EolEnclosureRule>(entity, std::move(rule));
    if (ref == nullptr) {
      _registry.emplace<TechRuleRef<TechCutLef58EolEnclosureRuleId>>(
          owner.entity(), TechRuleRef<TechCutLef58EolEnclosureRuleId>{.rule = TechCutLef58EolEnclosureRuleId{entity}});
    } else {
      _registry.get<TechRuleRef<TechCutLef58EolEnclosureRuleId>>(owner.entity()).rule = TechCutLef58EolEnclosureRuleId{entity};
    }
  } catch (...) {
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
  return TechCutLef58EolEnclosureRuleId{entity};
}

TechCutLef58EolEnclosureRuleId TechCutLayerStorage::lef58EolEnclosureRule(TechCutLayerId owner) const
{
  ensureLayer(owner);
  const auto* ref = _registry.try_get<TechRuleRef<TechCutLef58EolEnclosureRuleId>>(owner.entity());
  if (ref == nullptr) {
    return {};
  }
  if (ref->rule && !hasLef58EolEnclosureRule(ref->rule)) {
    throw std::logic_error("corrupt CUT LEF58 EOL enclosure rule reference");
  }
  return ref->rule;
}

TechCutLef58EolSpacingRuleId TechCutLayerStorage::setLef58EolSpacingRule(TechCutLayerId owner, TechCutLef58EolSpacingRule rule)
{
  ensureLayer(owner);
  validateEolSpacingRule(rule);

  const auto* ref = _registry.try_get<TechRuleRef<TechCutLef58EolSpacingRuleId>>(owner.entity());
  if (ref != nullptr && ref->rule) {
    if (!hasLef58EolSpacingRule(ref->rule)) {
      throw std::logic_error("corrupt CUT LEF58 EOL spacing rule reference");
    }
    _registry.replace<TechCutLef58EolSpacingRule>(ref->rule.entity(), std::move(rule));
    return ref->rule;
  }

  const auto entity = createOwnedRule(owner);
  try {
    _registry.emplace<TechCutLef58EolSpacingRule>(entity, std::move(rule));
    if (ref == nullptr) {
      _registry.emplace<TechRuleRef<TechCutLef58EolSpacingRuleId>>(
          owner.entity(), TechRuleRef<TechCutLef58EolSpacingRuleId>{.rule = TechCutLef58EolSpacingRuleId{entity}});
    } else {
      _registry.get<TechRuleRef<TechCutLef58EolSpacingRuleId>>(owner.entity()).rule = TechCutLef58EolSpacingRuleId{entity};
    }
  } catch (...) {
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
  return TechCutLef58EolSpacingRuleId{entity};
}

TechCutLef58EolSpacingRuleId TechCutLayerStorage::lef58EolSpacingRule(TechCutLayerId owner) const
{
  ensureLayer(owner);
  const auto* ref = _registry.try_get<TechRuleRef<TechCutLef58EolSpacingRuleId>>(owner.entity());
  if (ref == nullptr) {
    return {};
  }
  if (ref->rule && !hasLef58EolSpacingRule(ref->rule)) {
    throw std::logic_error("corrupt CUT LEF58 EOL spacing rule reference");
  }
  return ref->rule;
}

TechCutLef58SpacingTableRuleId TechCutLayerStorage::addLef58SpacingTableRule(TechCutLayerId owner, TechCutLef58SpacingTableRule rule)
{
  validateSpacingTableRule(rule);
  return addRepeatedRule(owner, std::move(rule));
}

std::vector<TechCutLef58SpacingTableRuleId> TechCutLayerStorage::lef58SpacingTableRules(TechCutLayerId owner) const
{
  return repeatedRules<TechCutLef58SpacingTableRule>(owner);
}

const TechCutLef58SpacingTableCell& TechCutLayerStorage::lef58SpacingTableCell(TechCutLef58SpacingTableRuleId rule_id,
                                                                               uint32_t class1_index, uint32_t class2_index) const
{
  const auto& spacing_table = lef58SpacingTableRule(rule_id);
  if (class1_index >= spacing_table.class1Count() || class2_index >= spacing_table.class2Count()) {
    throw std::out_of_range("cut LEF58 spacing table index out of range");
  }

  const auto index = static_cast<size_t>(class2_index) * spacing_table.class1Count() + class1_index;
  if (index >= spacing_table.cells.size()) {
    throw std::out_of_range("cut LEF58 spacing table cell list is incomplete");
  }
  return spacing_table.cells[index];
}

TechCutCurrentDensityRuleId TechCutLayerStorage::addCurrentDensityRule(TechCutLayerId owner, TechCutCurrentDensityRule rule)
{
  validateCurrentDensityRule(rule);
  return addRepeatedRule(owner, std::move(rule));
}

std::vector<TechCutCurrentDensityRuleId> TechCutLayerStorage::currentDensityRules(TechCutLayerId owner) const
{
  return repeatedRules<TechCutCurrentDensityRule>(owner);
}

double TechCutLayerStorage::currentDensityTableEntry(TechCutCurrentDensityRuleId rule_id, uint32_t frequency_index,
                                                     uint32_t cut_area_index) const
{
  const auto& current_density = currentDensityRule(rule_id);
  const auto frequency_rows = current_density.frequencyCount() == 0 ? 1u : current_density.frequencyCount();
  const auto cut_area_columns = current_density.cutAreaCount() == 0 ? 1u : current_density.cutAreaCount();
  if (frequency_index >= frequency_rows || cut_area_index >= cut_area_columns) {
    throw std::out_of_range("cut current-density table index out of range");
  }

  const auto index = static_cast<size_t>(frequency_index) * cut_area_columns + cut_area_index;
  if (index >= current_density.table_entries.size()) {
    throw std::out_of_range("cut current-density table-entry list is incomplete");
  }
  return current_density.table_entries[index];
}

double TechCutLayerStorage::currentDensityAt(TechCutCurrentDensityRuleId rule_id, double frequency, int64_t cut_area) const
{
  const auto& current_density = currentDensityRule(rule_id);
  if ((current_density.flags & TechCutCurrentDensityRuleFlag::kHasScalar) != 0) {
    return current_density.scalar;
  }

  const auto frequency_axis = interpolationBracket(current_density.frequencies, frequency, "cut current-density frequency is out of range");
  if (current_density.cut_areas.empty()) {
    const auto low = currentDensityTableEntry(rule_id, static_cast<uint32_t>(frequency_axis.low), 0u);
    const auto high = currentDensityTableEntry(rule_id, static_cast<uint32_t>(frequency_axis.high), 0u);
    return interpolate(low, high, frequency_axis.fraction);
  }

  const auto area = interpolationBracket(current_density.cut_areas, cut_area, "cut current-density cut area is out of range");
  const auto low_frequency = interpolate(
      currentDensityTableEntry(rule_id, static_cast<uint32_t>(frequency_axis.low), static_cast<uint32_t>(area.low)),
      currentDensityTableEntry(rule_id, static_cast<uint32_t>(frequency_axis.low), static_cast<uint32_t>(area.high)), area.fraction);
  if (frequency_axis.low == frequency_axis.high) {
    return low_frequency;
  }

  const auto high_frequency = interpolate(
      currentDensityTableEntry(rule_id, static_cast<uint32_t>(frequency_axis.high), static_cast<uint32_t>(area.low)),
      currentDensityTableEntry(rule_id, static_cast<uint32_t>(frequency_axis.high), static_cast<uint32_t>(area.high)), area.fraction);
  return interpolate(low_frequency, high_frequency, frequency_axis.fraction);
}

bool TechCutLayerStorage::hasSpacingRule(TechCutSpacingRuleId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechRuleOwner, TechCutSpacingRule>(id.entity());
}

TechCutSpacingRuleId TechCutLayerStorage::findSpacingRuleById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity) && _registry.all_of<TechRuleOwner, TechCutSpacingRule>(entity) ? TechCutSpacingRuleId{entity}
                                                                                                : TechCutSpacingRuleId{};
}

TechCutSpacingRule& TechCutLayerStorage::spacingRule(TechCutSpacingRuleId id)
{
  if (!hasSpacingRule(id)) {
    throw std::out_of_range("invalid CUT spacing rule id");
  }
  return _registry.get<TechCutSpacingRule>(id.entity());
}

const TechCutSpacingRule& TechCutLayerStorage::spacingRule(TechCutSpacingRuleId id) const
{
  if (!hasSpacingRule(id)) {
    throw std::out_of_range("invalid CUT spacing rule id");
  }
  return _registry.get<TechCutSpacingRule>(id.entity());
}

TechCutLayerId TechCutLayerStorage::spacingRuleOwner(TechCutSpacingRuleId id) const
{
  if (!hasSpacingRule(id)) {
    throw std::out_of_range("invalid CUT spacing rule id");
  }
  return ownerOf(id.entity());
}

bool TechCutLayerStorage::destroySpacingRule(TechCutSpacingRuleId id)
{
  return destroyRepeatedRule(id);
}

bool TechCutLayerStorage::hasEnclosureRule(TechCutEnclosureRuleId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechRuleOwner, TechCutEnclosureRule>(id.entity());
}

TechCutEnclosureRuleId TechCutLayerStorage::findEnclosureRuleById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity) && _registry.all_of<TechRuleOwner, TechCutEnclosureRule>(entity) ? TechCutEnclosureRuleId{entity}
                                                                                                  : TechCutEnclosureRuleId{};
}

const TechCutEnclosureRule& TechCutLayerStorage::enclosureRule(TechCutEnclosureRuleId id) const
{
  if (!hasEnclosureRule(id)) {
    throw std::out_of_range("invalid CUT enclosure rule id");
  }
  return _registry.get<TechCutEnclosureRule>(id.entity());
}

TechCutEnclosureRule& TechCutLayerStorage::enclosureRule(TechCutEnclosureRuleId id)
{
  return const_cast<TechCutEnclosureRule&>(std::as_const(*this).enclosureRule(id));
}

TechCutLayerId TechCutLayerStorage::enclosureRuleOwner(TechCutEnclosureRuleId id) const
{
  if (!hasEnclosureRule(id)) {
    throw std::out_of_range("invalid CUT enclosure rule id");
  }
  return ownerOf(id.entity());
}

bool TechCutLayerStorage::destroyEnclosureRule(TechCutEnclosureRuleId id)
{
  return destroyRepeatedRule(id);
}

bool TechCutLayerStorage::hasArraySpacingRule(TechCutArraySpacingRuleId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechRuleOwner, TechCutArraySpacingRule>(id.entity());
}

TechCutArraySpacingRuleId TechCutLayerStorage::findArraySpacingRuleById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity) && _registry.all_of<TechRuleOwner, TechCutArraySpacingRule>(entity) ? TechCutArraySpacingRuleId{entity}
                                                                                                     : TechCutArraySpacingRuleId{};
}

const TechCutArraySpacingRule& TechCutLayerStorage::arraySpacingRule(TechCutArraySpacingRuleId id) const
{
  if (!hasArraySpacingRule(id)) {
    throw std::out_of_range("invalid CUT array-spacing rule id");
  }
  return _registry.get<TechCutArraySpacingRule>(id.entity());
}

TechCutLayerId TechCutLayerStorage::arraySpacingRuleOwner(TechCutArraySpacingRuleId id) const
{
  if (!hasArraySpacingRule(id)) {
    throw std::out_of_range("invalid CUT array-spacing rule id");
  }
  return ownerOf(id.entity());
}

bool TechCutLayerStorage::destroyArraySpacingRule(TechCutArraySpacingRuleId id)
{
  if (!hasArraySpacingRule(id)) {
    return false;
  }
  const auto owner = ownerOf(id.entity());
  auto* ref = _registry.try_get<TechRuleRef<TechCutArraySpacingRuleId>>(owner.entity());
  if (ref == nullptr || ref->rule != id) {
    throw std::logic_error("corrupt CUT array-spacing rule reference");
  }
  _registry.remove<TechRuleRef<TechCutArraySpacingRuleId>>(owner.entity());
  _registry.destroy(id.entity());
  return true;
}

bool TechCutLayerStorage::hasOrthogonalSpacingTableRule(TechCutOrthogonalSpacingTableRuleId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechRuleOwner, TechCutOrthogonalSpacingTableRule>(id.entity());
}

TechCutOrthogonalSpacingTableRuleId TechCutLayerStorage::findOrthogonalSpacingTableRuleById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity) && _registry.all_of<TechRuleOwner, TechCutOrthogonalSpacingTableRule>(entity)
             ? TechCutOrthogonalSpacingTableRuleId{entity}
             : TechCutOrthogonalSpacingTableRuleId{};
}

TechCutOrthogonalSpacingTableRule& TechCutLayerStorage::orthogonalSpacingTableRule(TechCutOrthogonalSpacingTableRuleId id)
{
  if (!hasOrthogonalSpacingTableRule(id)) {
    throw std::out_of_range("invalid CUT orthogonal spacing-table rule id");
  }
  return _registry.get<TechCutOrthogonalSpacingTableRule>(id.entity());
}

const TechCutOrthogonalSpacingTableRule& TechCutLayerStorage::orthogonalSpacingTableRule(
    TechCutOrthogonalSpacingTableRuleId id) const
{
  if (!hasOrthogonalSpacingTableRule(id)) {
    throw std::out_of_range("invalid CUT orthogonal spacing-table rule id");
  }
  return _registry.get<TechCutOrthogonalSpacingTableRule>(id.entity());
}

TechCutLayerId TechCutLayerStorage::orthogonalSpacingTableRuleOwner(TechCutOrthogonalSpacingTableRuleId id) const
{
  if (!hasOrthogonalSpacingTableRule(id)) {
    throw std::out_of_range("invalid CUT orthogonal spacing-table rule id");
  }
  return ownerOf(id.entity());
}

bool TechCutLayerStorage::destroyOrthogonalSpacingTableRule(TechCutOrthogonalSpacingTableRuleId id)
{
  return destroyRepeatedRule(id);
}

bool TechCutLayerStorage::hasLef58CutClassRule(TechCutLef58CutClassRuleId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechRuleOwner, TechCutLef58CutClassRule>(id.entity());
}

TechCutLef58CutClassRuleId TechCutLayerStorage::findLef58CutClassRuleById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity) && _registry.all_of<TechRuleOwner, TechCutLef58CutClassRule>(entity) ? TechCutLef58CutClassRuleId{entity}
                                                                                                      : TechCutLef58CutClassRuleId{};
}

TechCutLef58CutClassRule& TechCutLayerStorage::lef58CutClassRule(TechCutLef58CutClassRuleId id)
{
  if (!hasLef58CutClassRule(id)) {
    throw std::out_of_range("invalid CUT LEF58 cut-class rule id");
  }
  return _registry.get<TechCutLef58CutClassRule>(id.entity());
}

const TechCutLef58CutClassRule& TechCutLayerStorage::lef58CutClassRule(TechCutLef58CutClassRuleId id) const
{
  if (!hasLef58CutClassRule(id)) {
    throw std::out_of_range("invalid CUT LEF58 cut-class rule id");
  }
  return _registry.get<TechCutLef58CutClassRule>(id.entity());
}

TechCutLayerId TechCutLayerStorage::lef58CutClassRuleOwner(TechCutLef58CutClassRuleId id) const
{
  if (!hasLef58CutClassRule(id)) {
    throw std::out_of_range("invalid CUT LEF58 cut-class rule id");
  }
  return ownerOf(id.entity());
}

bool TechCutLayerStorage::destroyLef58CutClassRule(TechCutLef58CutClassRuleId id)
{
  return destroyRepeatedRule(id);
}

bool TechCutLayerStorage::hasLef58EnclosureRule(TechCutLef58EnclosureRuleId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechRuleOwner, TechCutLef58EnclosureRule>(id.entity());
}

TechCutLef58EnclosureRuleId TechCutLayerStorage::findLef58EnclosureRuleById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity) && _registry.all_of<TechRuleOwner, TechCutLef58EnclosureRule>(entity) ? TechCutLef58EnclosureRuleId{entity}
                                                                                                       : TechCutLef58EnclosureRuleId{};
}

TechCutLef58EnclosureRule& TechCutLayerStorage::lef58EnclosureRule(TechCutLef58EnclosureRuleId id)
{
  if (!hasLef58EnclosureRule(id)) {
    throw std::out_of_range("invalid CUT LEF58 enclosure rule id");
  }
  return _registry.get<TechCutLef58EnclosureRule>(id.entity());
}

const TechCutLef58EnclosureRule& TechCutLayerStorage::lef58EnclosureRule(TechCutLef58EnclosureRuleId id) const
{
  if (!hasLef58EnclosureRule(id)) {
    throw std::out_of_range("invalid CUT LEF58 enclosure rule id");
  }
  return _registry.get<TechCutLef58EnclosureRule>(id.entity());
}

TechCutLayerId TechCutLayerStorage::lef58EnclosureRuleOwner(TechCutLef58EnclosureRuleId id) const
{
  if (!hasLef58EnclosureRule(id)) {
    throw std::out_of_range("invalid CUT LEF58 enclosure rule id");
  }
  return ownerOf(id.entity());
}

bool TechCutLayerStorage::destroyLef58EnclosureRule(TechCutLef58EnclosureRuleId id)
{
  return destroyRepeatedRule(id);
}

bool TechCutLayerStorage::hasLef58EnclosureEdgeRule(TechCutLef58EnclosureEdgeRuleId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechRuleOwner, TechCutLef58EnclosureEdgeRule>(id.entity());
}

TechCutLef58EnclosureEdgeRuleId TechCutLayerStorage::findLef58EnclosureEdgeRuleById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity) && _registry.all_of<TechRuleOwner, TechCutLef58EnclosureEdgeRule>(entity)
             ? TechCutLef58EnclosureEdgeRuleId{entity}
             : TechCutLef58EnclosureEdgeRuleId{};
}

TechCutLef58EnclosureEdgeRule& TechCutLayerStorage::lef58EnclosureEdgeRule(TechCutLef58EnclosureEdgeRuleId id)
{
  if (!hasLef58EnclosureEdgeRule(id)) {
    throw std::out_of_range("invalid CUT LEF58 enclosure-edge rule id");
  }
  return _registry.get<TechCutLef58EnclosureEdgeRule>(id.entity());
}

const TechCutLef58EnclosureEdgeRule& TechCutLayerStorage::lef58EnclosureEdgeRule(TechCutLef58EnclosureEdgeRuleId id) const
{
  if (!hasLef58EnclosureEdgeRule(id)) {
    throw std::out_of_range("invalid CUT LEF58 enclosure-edge rule id");
  }
  return _registry.get<TechCutLef58EnclosureEdgeRule>(id.entity());
}

TechCutLayerId TechCutLayerStorage::lef58EnclosureEdgeRuleOwner(TechCutLef58EnclosureEdgeRuleId id) const
{
  if (!hasLef58EnclosureEdgeRule(id)) {
    throw std::out_of_range("invalid CUT LEF58 enclosure-edge rule id");
  }
  return ownerOf(id.entity());
}

bool TechCutLayerStorage::destroyLef58EnclosureEdgeRule(TechCutLef58EnclosureEdgeRuleId id)
{
  return destroyRepeatedRule(id);
}

bool TechCutLayerStorage::hasLef58EolEnclosureRule(TechCutLef58EolEnclosureRuleId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechRuleOwner, TechCutLef58EolEnclosureRule>(id.entity());
}

TechCutLef58EolEnclosureRuleId TechCutLayerStorage::findLef58EolEnclosureRuleById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity) && _registry.all_of<TechRuleOwner, TechCutLef58EolEnclosureRule>(entity)
             ? TechCutLef58EolEnclosureRuleId{entity}
             : TechCutLef58EolEnclosureRuleId{};
}

TechCutLef58EolEnclosureRule& TechCutLayerStorage::lef58EolEnclosureRule(TechCutLef58EolEnclosureRuleId id)
{
  if (!hasLef58EolEnclosureRule(id)) {
    throw std::out_of_range("invalid CUT LEF58 EOL enclosure rule id");
  }
  return _registry.get<TechCutLef58EolEnclosureRule>(id.entity());
}

const TechCutLef58EolEnclosureRule& TechCutLayerStorage::lef58EolEnclosureRule(TechCutLef58EolEnclosureRuleId id) const
{
  if (!hasLef58EolEnclosureRule(id)) {
    throw std::out_of_range("invalid CUT LEF58 EOL enclosure rule id");
  }
  return _registry.get<TechCutLef58EolEnclosureRule>(id.entity());
}

TechCutLayerId TechCutLayerStorage::lef58EolEnclosureRuleOwner(TechCutLef58EolEnclosureRuleId id) const
{
  if (!hasLef58EolEnclosureRule(id)) {
    throw std::out_of_range("invalid CUT LEF58 EOL enclosure rule id");
  }
  return ownerOf(id.entity());
}

bool TechCutLayerStorage::destroyLef58EolEnclosureRule(TechCutLef58EolEnclosureRuleId id)
{
  if (!hasLef58EolEnclosureRule(id)) {
    return false;
  }
  const auto owner = ownerOf(id.entity());
  const auto* ref = _registry.try_get<TechRuleRef<TechCutLef58EolEnclosureRuleId>>(owner.entity());
  if (ref == nullptr || ref->rule != id) {
    throw std::logic_error("corrupt CUT LEF58 EOL enclosure rule reference");
  }
  _registry.remove<TechRuleRef<TechCutLef58EolEnclosureRuleId>>(owner.entity());
  _registry.destroy(id.entity());
  return true;
}

bool TechCutLayerStorage::hasLef58EolSpacingRule(TechCutLef58EolSpacingRuleId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechRuleOwner, TechCutLef58EolSpacingRule>(id.entity());
}

TechCutLef58EolSpacingRuleId TechCutLayerStorage::findLef58EolSpacingRuleById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity) && _registry.all_of<TechRuleOwner, TechCutLef58EolSpacingRule>(entity)
             ? TechCutLef58EolSpacingRuleId{entity}
             : TechCutLef58EolSpacingRuleId{};
}

const TechCutLef58EolSpacingRule& TechCutLayerStorage::lef58EolSpacingRule(TechCutLef58EolSpacingRuleId id) const
{
  if (!hasLef58EolSpacingRule(id)) {
    throw std::out_of_range("invalid CUT LEF58 EOL spacing rule id");
  }
  return _registry.get<TechCutLef58EolSpacingRule>(id.entity());
}

TechCutLayerId TechCutLayerStorage::lef58EolSpacingRuleOwner(TechCutLef58EolSpacingRuleId id) const
{
  if (!hasLef58EolSpacingRule(id)) {
    throw std::out_of_range("invalid CUT LEF58 EOL spacing rule id");
  }
  return ownerOf(id.entity());
}

bool TechCutLayerStorage::destroyLef58EolSpacingRule(TechCutLef58EolSpacingRuleId id)
{
  if (!hasLef58EolSpacingRule(id)) {
    return false;
  }
  const auto owner = ownerOf(id.entity());
  const auto* ref = _registry.try_get<TechRuleRef<TechCutLef58EolSpacingRuleId>>(owner.entity());
  if (ref == nullptr || ref->rule != id) {
    throw std::logic_error("corrupt CUT LEF58 EOL spacing rule reference");
  }
  _registry.remove<TechRuleRef<TechCutLef58EolSpacingRuleId>>(owner.entity());
  _registry.destroy(id.entity());
  return true;
}

bool TechCutLayerStorage::hasLef58SpacingTableRule(TechCutLef58SpacingTableRuleId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechRuleOwner, TechCutLef58SpacingTableRule>(id.entity());
}

TechCutLef58SpacingTableRuleId TechCutLayerStorage::findLef58SpacingTableRuleById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity) && _registry.all_of<TechRuleOwner, TechCutLef58SpacingTableRule>(entity)
             ? TechCutLef58SpacingTableRuleId{entity}
             : TechCutLef58SpacingTableRuleId{};
}

const TechCutLef58SpacingTableRule& TechCutLayerStorage::lef58SpacingTableRule(TechCutLef58SpacingTableRuleId id) const
{
  if (!hasLef58SpacingTableRule(id)) {
    throw std::out_of_range("invalid CUT LEF58 spacing-table rule id");
  }
  return _registry.get<TechCutLef58SpacingTableRule>(id.entity());
}

TechCutLayerId TechCutLayerStorage::lef58SpacingTableRuleOwner(TechCutLef58SpacingTableRuleId id) const
{
  if (!hasLef58SpacingTableRule(id)) {
    throw std::out_of_range("invalid CUT LEF58 spacing-table rule id");
  }
  return ownerOf(id.entity());
}

bool TechCutLayerStorage::destroyLef58SpacingTableRule(TechCutLef58SpacingTableRuleId id)
{
  return destroyRepeatedRule(id);
}

bool TechCutLayerStorage::hasCurrentDensityRule(TechCutCurrentDensityRuleId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechRuleOwner, TechCutCurrentDensityRule>(id.entity());
}

TechCutCurrentDensityRuleId TechCutLayerStorage::findCurrentDensityRuleById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity) && _registry.all_of<TechRuleOwner, TechCutCurrentDensityRule>(entity) ? TechCutCurrentDensityRuleId{entity}
                                                                                                       : TechCutCurrentDensityRuleId{};
}

const TechCutCurrentDensityRule& TechCutLayerStorage::currentDensityRule(TechCutCurrentDensityRuleId id) const
{
  if (!hasCurrentDensityRule(id)) {
    throw std::out_of_range("invalid CUT current-density rule id");
  }
  return _registry.get<TechCutCurrentDensityRule>(id.entity());
}

TechCutLayerId TechCutLayerStorage::currentDensityRuleOwner(TechCutCurrentDensityRuleId id) const
{
  if (!hasCurrentDensityRule(id)) {
    throw std::out_of_range("invalid CUT current-density rule id");
  }
  return ownerOf(id.entity());
}

bool TechCutLayerStorage::destroyCurrentDensityRule(TechCutCurrentDensityRuleId id)
{
  return destroyRepeatedRule(id);
}

void TechCutLayerStorage::ensureLayer(TechCutLayerId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid CUT layer id");
  }
}

void TechCutLayerStorage::ensureOwnedRule(TechEntity id) const
{
  if (!_registry.valid(id) || !_registry.all_of<TechRuleOwner>(id)) {
    throw std::out_of_range("invalid CUT rule entity");
  }
}

TechEntity TechCutLayerStorage::createOwnedRule(TechCutLayerId owner)
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

TechCutLayerId TechCutLayerStorage::ownerOf(TechEntity rule) const
{
  ensureOwnedRule(rule);
  const auto owner = TechCutLayerId{_registry.get<TechRuleOwner>(rule).owner};
  ensureLayer(owner);
  return owner;
}

}  // namespace eccdb
