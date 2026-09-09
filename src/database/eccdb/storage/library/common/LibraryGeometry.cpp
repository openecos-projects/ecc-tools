#include "library/common/LibraryGeometry.h"

#include <stdexcept>

#include "tech/TechRegistry.h"

namespace eccdb {

void validateLibraryViaPlacements(const TechRegistry& tech_registry, const std::vector<LibraryViaPlacement>& placements)
{
  const auto& registry = tech_registry.registry();
  for (const auto& placement : placements) {
    if (!placement.via || !registry.valid(placement.via.entity())
        || !registry.all_of<TechViaMaster, TechViaGeometry>(placement.via.entity())) {
      throw std::invalid_argument("library geometry references an invalid tech via master");
    }
    if (!registry.get<const TechViaGeometry>(placement.via.entity()).bounding_box.hasArea()) {
      throw std::invalid_argument("library geometry references a via master without geometry");
    }
  }
}

}  // namespace eccdb
