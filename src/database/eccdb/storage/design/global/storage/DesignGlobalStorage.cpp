#include "design/global/storage/DesignGlobalStorage.h"

#include <stdexcept>
#include <utility>

#include "design/common/DesignGeometryValidation.h"

namespace eccdb {

bool DesignGlobalStorage::containsRoot() const noexcept
{
  return _registry.valid(_root.entity()) && _registry.all_of<DesignRoot>(_root.entity());
}

bool DesignGlobalStorage::hasInfo() const noexcept
{
  return containsRoot() && _registry.all_of<DesignInfo>(_root.entity());
}

const DesignInfo& DesignGlobalStorage::info() const
{
  if (!hasInfo()) {
    throw std::out_of_range("design information is absent");
  }
  return _registry.get<const DesignInfo>(_root.entity());
}

void DesignGlobalStorage::setInfo(DesignInfo info)
{
  validateInfo(info);
  _registry.emplace_or_replace<DesignInfo>(rootEntity(), std::move(info));
}

bool DesignGlobalStorage::hasDieArea() const noexcept
{
  return containsRoot() && _registry.all_of<DesignDieArea>(_root.entity());
}

const DesignDieArea& DesignGlobalStorage::dieArea() const
{
  if (!hasDieArea()) {
    throw std::out_of_range("design die area is absent");
  }
  return _registry.get<const DesignDieArea>(_root.entity());
}

Rect DesignGlobalStorage::dieBounds() const
{
  return validateAndGetDieBounds(dieArea());
}

void DesignGlobalStorage::setDieArea(DesignDieArea die_area)
{
  static_cast<void>(validateAndGetDieBounds(die_area));
  _registry.emplace_or_replace<DesignDieArea>(rootEntity(), std::move(die_area));
}

DesignEntity DesignGlobalStorage::rootEntity() const
{
  if (!containsRoot()) {
    throw std::logic_error("invalid design root id");
  }
  return _root.entity();
}

void DesignGlobalStorage::validateInfo(const DesignInfo& info)
{
  if (info.name.empty()) {
    throw std::invalid_argument("design name is required");
  }
  if (info.database_units_per_micron <= 0) {
    throw std::invalid_argument("design database units per micron must be positive");
  }
  if (info.divider_character.size() != 1u) {
    throw std::invalid_argument("design divider character must contain one character");
  }
  if (info.bus_bit_characters.size() != 2u) {
    throw std::invalid_argument("design bus bit characters must contain two characters");
  }
}

Rect DesignGlobalStorage::validateAndGetDieBounds(const DesignDieArea& die_area)
{
  return validateDesignOrthogonalBoundary(die_area.boundary, "design die area");
}

}  // namespace eccdb
