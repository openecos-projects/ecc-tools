#include "tech/global/storage/TechGlobalStorage.h"

#include <stdexcept>
#include <utility>

#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace eccdb {
namespace {

void validateUnitValue(uint32_t flags, uint32_t flag, int32_t value, const char* message)
{
  if ((flags & flag) != 0u) {
    if (value <= 0) {
      throw std::invalid_argument(message);
    }
  } else if (value != 0) {
    throw std::invalid_argument(message);
  }
}

}  // namespace

bool TechGlobalStorage::containsRoot() const noexcept
{
  return _registry.valid(_root.entity()) && _registry.all_of<TechRoot>(_root.entity());
}

bool TechGlobalStorage::hasUnits() const noexcept
{
  return tryGetUnits() != nullptr;
}

const TechGlobalUnits* TechGlobalStorage::tryGetUnits() const noexcept
{
  return containsRoot() ? _registry.try_get<TechGlobalUnits>(_root.entity()) : nullptr;
}

const TechGlobalUnits& TechGlobalStorage::getUnits() const
{
  const auto* units = tryGetUnits();
  if (units == nullptr) {
    throw std::out_of_range("tech global units are absent");
  }
  return *units;
}

void TechGlobalStorage::setUnits(TechGlobalUnits units)
{
  validateUnits(units);
  _registry.emplace_or_replace<TechGlobalUnits>(rootEntity(), std::move(units));
}

void TechGlobalStorage::removeUnits() noexcept
{
  if (containsRoot()) {
    _registry.remove<TechGlobalUnits>(_root.entity());
  }
}

bool TechGlobalStorage::hasManufacturingGrid() const noexcept
{
  return tryGetManufacturingGrid() != nullptr;
}

const TechManufacturingGrid* TechGlobalStorage::tryGetManufacturingGrid() const noexcept
{
  return containsRoot() ? _registry.try_get<TechManufacturingGrid>(_root.entity()) : nullptr;
}

const TechManufacturingGrid& TechGlobalStorage::getManufacturingGrid() const
{
  const auto* manufacturing_grid = tryGetManufacturingGrid();
  if (manufacturing_grid == nullptr) {
    throw std::out_of_range("tech manufacturing grid is absent");
  }
  return *manufacturing_grid;
}

void TechGlobalStorage::setManufacturingGrid(int32_t manufacturing_grid)
{
  validateManufacturingGrid(manufacturing_grid);
  _registry.emplace_or_replace<TechManufacturingGrid>(rootEntity(), TechManufacturingGrid{.value = manufacturing_grid});
}

void TechGlobalStorage::removeManufacturingGrid() noexcept
{
  if (containsRoot()) {
    _registry.remove<TechManufacturingGrid>(_root.entity());
  }
}

bool TechGlobalStorage::hasMaxViaStack() const noexcept
{
  return tryGetMaxViaStack() != nullptr;
}

const TechMaxViaStack* TechGlobalStorage::tryGetMaxViaStack() const noexcept
{
  return containsRoot() ? _registry.try_get<TechMaxViaStack>(_root.entity()) : nullptr;
}

const TechMaxViaStack& TechGlobalStorage::getMaxViaStack() const
{
  const auto* max_via_stack = tryGetMaxViaStack();
  if (max_via_stack == nullptr) {
    throw std::out_of_range("tech max via stack is absent");
  }
  return *max_via_stack;
}

void TechGlobalStorage::setMaxViaStack(TechMaxViaStack max_via_stack)
{
  validateMaxViaStack(max_via_stack);
  _registry.emplace_or_replace<TechMaxViaStack>(rootEntity(), std::move(max_via_stack));
}

void TechGlobalStorage::removeMaxViaStack() noexcept
{
  if (containsRoot()) {
    _registry.remove<TechMaxViaStack>(_root.entity());
  }
}

TechEntity TechGlobalStorage::rootEntity() const
{
  if (!containsRoot()) {
    throw std::logic_error("invalid technology root id");
  }
  return _root.entity();
}

const TechLayerSequence& TechGlobalStorage::layerSequence() const
{
  const auto* sequence = containsRoot() ? _registry.try_get<TechLayerSequence>(_root.entity()) : nullptr;
  if (sequence == nullptr) {
    throw std::logic_error("technology root has no layer sequence");
  }
  return *sequence;
}

void TechGlobalStorage::validateUnits(const TechGlobalUnits& units)
{
  constexpr uint32_t kKnownFlags = TechGlobalUnitsFlag::kHasNanoseconds | TechGlobalUnitsFlag::kHasPicofarads
                                   | TechGlobalUnitsFlag::kHasOhms | TechGlobalUnitsFlag::kHasMilliwatts
                                   | TechGlobalUnitsFlag::kHasMilliamps | TechGlobalUnitsFlag::kHasVolts
                                   | TechGlobalUnitsFlag::kHasDatabaseUnitsPerMicron | TechGlobalUnitsFlag::kHasMegahertz;
  if ((units.flags & ~kKnownFlags) != 0u) {
    throw std::invalid_argument("tech units have unknown flags");
  }
  validateUnitValue(units.flags, TechGlobalUnitsFlag::kHasNanoseconds, units.nanoseconds, "tech units nanoseconds must be positive");
  validateUnitValue(units.flags, TechGlobalUnitsFlag::kHasPicofarads, units.picofarads, "tech units picofarads must be positive");
  validateUnitValue(units.flags, TechGlobalUnitsFlag::kHasOhms, units.ohms, "tech units ohms must be positive");
  validateUnitValue(units.flags, TechGlobalUnitsFlag::kHasMilliwatts, units.milliwatts, "tech units milliwatts must be positive");
  validateUnitValue(units.flags, TechGlobalUnitsFlag::kHasMilliamps, units.milliamps, "tech units milliamps must be positive");
  validateUnitValue(units.flags, TechGlobalUnitsFlag::kHasVolts, units.volts, "tech units volts must be positive");
  validateUnitValue(units.flags, TechGlobalUnitsFlag::kHasDatabaseUnitsPerMicron, units.database_units_per_micron,
                    "tech units database units per micron must be positive");
  validateUnitValue(units.flags, TechGlobalUnitsFlag::kHasMegahertz, units.megahertz, "tech units megahertz must be positive");
}

void TechGlobalStorage::validateManufacturingGrid(int32_t manufacturing_grid)
{
  if (manufacturing_grid <= 0) {
    throw std::invalid_argument("tech manufacturing grid must be positive");
  }
}

bool TechGlobalStorage::hasRange(const TechMaxViaStack& max_via_stack) noexcept
{
  return (max_via_stack.flags & TechMaxViaStackFlag::kHasRange) != 0u;
}

void TechGlobalStorage::validateMaxViaStack(const TechMaxViaStack& max_via_stack) const
{
  if (max_via_stack.max_stack_count == 0) {
    throw std::invalid_argument("tech max via stack count must be positive");
  }
  constexpr uint32_t kKnownFlags = TechMaxViaStackFlag::kNoSingle | TechMaxViaStackFlag::kHasRange;
  if ((max_via_stack.flags & ~kKnownFlags) != 0u) {
    throw std::invalid_argument("tech max via stack has unknown flags");
  }
  if (hasRange(max_via_stack) && (!max_via_stack.bottom_layer || !max_via_stack.top_layer)) {
    throw std::invalid_argument("tech max via stack range routing layers must not be null");
  }
  if (!hasRange(max_via_stack) && (max_via_stack.bottom_layer || max_via_stack.top_layer)) {
    throw std::invalid_argument("tech max via stack range routing layers require the range flag");
  }
  if (!hasRange(max_via_stack)) {
    return;
  }

  const auto bottom = max_via_stack.bottom_layer;
  const auto top = max_via_stack.top_layer;
  if (!_registry.valid(bottom.entity()) || !_registry.all_of<TechLayerInfo, TechRoutingLayer>(bottom.entity())
      || !_registry.valid(top.entity()) || !_registry.all_of<TechLayerInfo, TechRoutingLayer>(top.entity())) {
    throw std::invalid_argument("tech max via stack range requires routing layers from this technology database");
  }
  if (!techLayerIsBelow(layerSequence(), bottom.entity(), top.entity())) {
    throw std::invalid_argument("tech max via stack bottom routing layer must precede the top routing layer");
  }
}

}  // namespace eccdb
