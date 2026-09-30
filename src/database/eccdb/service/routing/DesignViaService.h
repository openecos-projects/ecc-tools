// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>

#include "design/common/DesignTypes.h"
#include "design/via/component/ViaComponents.h"

namespace eccdb {

class DesignStore;

// Mutable, adapter-facing facade for DEF design-local VIAs.  It keeps the
// complete DesignVia value type visible while leaving validation, ownership,
// and reference protection in DesignRoutingStorage.
class DesignViaService
{
 public:
  explicit DesignViaService(DesignStore& design);

  [[nodiscard]] DesignViaId createVia(DesignVia via);
  [[nodiscard]] DesignViaId findVia(std::string_view name) const;
  [[nodiscard]] const DesignVia& via(DesignViaId id) const;
  void updateVia(DesignViaId id, DesignVia via);
  [[nodiscard]] bool destroyVia(DesignViaId id);

 private:
  DesignStore& _design;
};

}  // namespace eccdb
