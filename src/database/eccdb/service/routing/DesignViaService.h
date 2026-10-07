// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>

#include "design/common/DesignTypes.h"
#include "design/via/component/ViaComponents.h"

namespace eccdb {

class DesignStore;

// Mutable, adapter-facing facade for DEF design-local VIAs. Input geometry is
// expanded into independent GeometryShapeId entities by DesignRoutingStorage;
// the returned DesignVia is the canonical ID-only relation.
class DesignViaService
{
 public:
  explicit DesignViaService(DesignStore& design);

  [[nodiscard]] DesignViaId createVia(DesignViaInput via);
  [[nodiscard]] DesignViaId findVia(std::string_view name) const;
  [[nodiscard]] const DesignVia& via(DesignViaId id) const;
  void updateVia(DesignViaId id, DesignViaInput via);
  [[nodiscard]] bool destroyVia(DesignViaId id);

 private:
  DesignStore& _design;
};

}  // namespace eccdb
