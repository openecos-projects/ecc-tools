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

std::span<const Point> DesignGlobalStorage::dieBoundary() const
{
  return geometryRegistry().get<const GeometryBoundary>(dieArea().boundary).points;
}

Rect DesignGlobalStorage::dieBounds() const
{
  return validateAndGetDieBounds(dieBoundary());
}

void DesignGlobalStorage::setDieArea(DesignDieAreaInput die_area)
{
  static_cast<void>(validateAndGetDieBounds(die_area.boundary));
  const auto root = rootEntity();
  const auto boundary = geometryRegistry().createBoundary(designDieAreaOwner(_root), std::move(die_area.boundary));
  const auto* previous = _registry.try_get<const DesignDieArea>(root);
  const auto old_boundary = previous == nullptr ? GeometryShapeId{} : previous->boundary;
  try {
    _registry.emplace_or_replace<DesignDieArea>(root, DesignDieArea{.boundary = boundary});
  } catch (...) {
    static_cast<void>(geometryRegistry().destroy(boundary));
    throw;
  }
  static_cast<void>(geometryRegistry().destroy(old_boundary));
  _design_registry.markGeometryChanged();
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

Rect DesignGlobalStorage::validateAndGetDieBounds(std::span<const Point> boundary)
{
  return validateDesignOrthogonalBoundary(boundary, "design die area");
}

}  // namespace eccdb
