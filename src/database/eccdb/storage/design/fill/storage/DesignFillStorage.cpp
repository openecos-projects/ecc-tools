#include "design/fill/storage/DesignFillStorage.h"

#include <stdexcept>
#include <utility>

#include "tech/TechRegistry.h"
#include "tech/common/TechLayerTypes.h"

namespace eccdb {

DesignFillId DesignFillStorage::createFill(DesignFill value)
{
  validateFill(value);
  const auto entity = _registry.create();
  try {
    _registry.emplace<DesignFill>(entity, std::move(value));
  } catch (...) {
    _registry.destroy(entity);
    throw;
  }
  return DesignFillId{entity};
}

void DesignFillStorage::updateFill(DesignFillId id, DesignFill value)
{
  ensureFill(id);
  validateFill(value);
  _registry.replace<DesignFill>(id.entity(), std::move(value));
}

bool DesignFillStorage::destroyFill(DesignFillId id)
{
  if (!contains(id)) {
    return false;
  }
  _registry.destroy(id.entity());
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

void DesignFillStorage::validateFill(const DesignFill& value) const
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

void DesignFillStorage::ensureFill(DesignFillId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design fill id");
  }
}

}  // namespace eccdb
