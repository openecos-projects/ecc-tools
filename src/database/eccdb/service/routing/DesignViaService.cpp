// SPDX-License-Identifier: Apache-2.0
#include "service/routing/DesignViaService.h"

#include <utility>

#include "design/DesignStore.h"

namespace eccdb {

DesignViaService::DesignViaService(DesignStore& design) : _design(design)
{
}

DesignViaId DesignViaService::createVia(DesignVia via)
{
  return _design.routingStorage().createVia(std::move(via));
}

DesignViaId DesignViaService::findVia(std::string_view name) const
{
  return _design.routingStorage().findVia(name);
}

const DesignVia& DesignViaService::via(DesignViaId id) const
{
  return _design.routingStorage().via(id);
}

void DesignViaService::updateVia(DesignViaId id, DesignVia via)
{
  _design.routingStorage().updateVia(id, std::move(via));
}

bool DesignViaService::destroyVia(DesignViaId id)
{
  return _design.routingStorage().destroyVia(id);
}

}  // namespace eccdb
