#include "design/fill/storage/DesignFillStorage.h"

#include <stdexcept>
#include <utility>

#include "tech/TechRegistry.h"
#include "tech/common/TechLayerTypes.h"

namespace eccdb {

DesignFillId DesignFillStorage::createFill(DesignFillInput value)
{
  validateFill(value);
  const auto entity = _registry.create();
  const DesignFillId id{entity};
  DesignFill materialized;
  try {
    materialized = materializeFill(id, std::move(value));
    _registry.emplace<DesignFill>(entity, std::move(materialized));
  } catch (...) {
    destroyFillGeometry(materialized);
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
  _design_registry.markGeometryChanged();
  return id;
}

void DesignFillStorage::updateFill(DesignFillId id, DesignFillInput value)
{
  ensureFill(id);
  validateFill(value);
  const auto old = fill(id);
  auto replacement = materializeFill(id, std::move(value));
  try {
    _registry.replace<DesignFill>(id.entity(), std::move(replacement));
  } catch (...) {
    destroyFillGeometry(replacement);
    throw;
  }
  destroyFillGeometry(old);
  _design_registry.markGeometryChanged();
}

bool DesignFillStorage::destroyFill(DesignFillId id)
{
  if (!contains(id)) {
    return false;
  }
  const auto value = fill(id);
  _registry.destroy(id.entity());
  destroyFillGeometry(value);
  _design_registry.markGeometryChanged();
  return true;
}

bool DesignFillStorage::contains(DesignFillId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignFill>(id.entity());
}

const DesignFill& DesignFillStorage::fill(DesignFillId id) const
{
  ensureFill(id);
  return _registry.get<const DesignFill>(id.entity());
}

std::vector<DesignFillId> DesignFillStorage::fills() const
{
  const auto view = _registry.view<const DesignFill>();
  std::vector<DesignFillId> result;
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

uint32_t DesignFillStorage::fillCount() const
{
  return static_cast<uint32_t>(_registry.storage<DesignFill>().size());
}

void DesignFillStorage::validateFill(const DesignFillInput& value) const
{
  constexpr uint32_t kKnownFlags = DesignFillFlag::kOpc | DesignFillFlag::kHasMask;
  if (!value.layer || (value.flags & ~kKnownFlags) != 0u || value.rectangles.empty()) {
    throw std::invalid_argument("design fill layer, flags or geometry are invalid");
  }

  const auto& tech = _tech_registry.registry();
  if (!tech.valid(value.layer.entity()) || !tech.all_of<TechLayerInfo>(value.layer.entity())) {
    throw std::invalid_argument("design fill references an invalid technology layer");
  }
  const bool has_mask = (value.flags & DesignFillFlag::kHasMask) != 0u;
  if (has_mask != (value.mask != 0u) || (has_mask && value.mask > 3u)) {
    throw std::invalid_argument("design fill mask and flags are inconsistent");
  }
  for (const auto rectangle : value.rectangles) {
    if (!rectangle.isValid() || !rectangle.hasArea()) {
      throw std::invalid_argument("design fill rectangle must have area");
    }
  }
}

DesignFill DesignFillStorage::materializeFill(DesignFillId id, DesignFillInput input)
{
  DesignFill result{.layer = input.layer, .flags = input.flags, .mask = input.mask};
  try {
    result.rectangles.reserve(input.rectangles.size());
    for (const auto rectangle : input.rectangles) {
      result.rectangles.push_back(
          geometryRegistry().create(designFillOwner(id), GeometryRectangle{.rectangle = rectangle.normalized()}));
    }
    return result;
  } catch (...) {
    destroyFillGeometry(result);
    throw;
  }
}

void DesignFillStorage::destroyFillGeometry(const DesignFill& value) noexcept
{
  geometryRegistry().destroy(value.rectangles);
}

void DesignFillStorage::ensureFill(DesignFillId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design fill id");
  }
}

}  // namespace eccdb
