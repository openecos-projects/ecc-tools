// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "design/DesignStore.h"

#include <stdexcept>

#include "common/GeometryBounds.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/global/model/DesignGlobalComponents.h"

namespace eccdb {
namespace {

DesignRootId createRoot(DesignRegistry& registry)
{
  const auto entity = registry.registry().create();
  registry.registry().emplace<DesignRoot>(entity);
  return DesignRootId{entity};
}

}  // namespace

DesignStore::DesignStore(const TechRegistry& tech_registry, const LibraryRegistry& library_registry,
                         const GeometryRegistry* tech_geometry_registry)
    : _tech_registry(tech_registry),
      _library_registry(library_registry),
      _root(createRoot(_registry)),
      _global(_registry, _root),
      _floorplan(_registry, _tech_registry, _library_registry, _root),
      _netlist(_registry, _tech_registry, _library_registry),
      _routing(_registry, _tech_registry, tech_geometry_registry),
      _geometry(_routing),
      _geometry_index(*this),
      _geometry_overview(*this),
      _guides(_registry, _tech_registry),
      _pin_access(_registry, _tech_registry, _library_registry, _netlist, _root),
      _constraints(_registry, _tech_registry),
      _fills(_registry, _tech_registry)
{
}

void DesignStore::resetForBinaryLoad()
{
  _routing.clearGeometry();
  _registry.registry() = DesignRegistry::registry_type{};
  _root = {};
  _global.rebindRoot(_root);
  _floorplan.rebindRoot(_root);
  _pin_access.rebindRoot(_root);
  _netlist.rebuildNameIndexes();
  _routing.rebuildWireIndex();
  _routing.markGeometryChanged();
}

void DesignStore::bindBinaryLoadedRoot()
{
  auto view = _registry.registry().view<DesignRoot>();
  auto current = view.begin();
  if (current == view.end()) {
    throw std::runtime_error("binary design has no DesignRoot");
  }
  const auto root = *current;
  if (++current != view.end()) {
    throw std::runtime_error("binary design has multiple DesignRoot entities");
  }
  _root = DesignRootId{root};
  _global.rebindRoot(_root);
  _floorplan.rebindRoot(_root);
  _pin_access.rebindRoot(_root);
  _netlist.rebuildNameIndexes();
  _routing.rebuildWireIndex();
  _routing.markGeometryChanged();
  // GeometryBounds is a rebuildable runtime component and is intentionally
  // absent from the binary schema. Reattach it to restored design entities so
  // subsequent edits and queries can use the same component contract as newly
  // created objects.
  auto& objects = _registry.registry();
  const auto attach = [&objects](auto view) {
    for (const auto entity : view) {
      static_cast<void>(objects.get_or_emplace<GeometryBounds>(entity));
    }
  };
  attach(objects.view<DesignInstance>());
  attach(objects.view<DesignBlockTerm>());
  attach(objects.view<DesignBlockPin>());
  attach(objects.view<DesignNet>());
  attach(objects.view<DesignWire>());
  _routing.validateRestoredRoutingState();
  _geometry.validateRestoredState();
  _guides.validateRestoredState();
  _netlist.rebuildLibraryCaches();
  _pin_access.validateRestoredState();
}

}  // namespace eccdb
