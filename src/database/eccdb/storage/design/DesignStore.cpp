#include "design/DesignStore.h"

#include <stdexcept>

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

DesignStore::DesignStore(const TechRegistry& tech_registry, const LibraryRegistry& library_registry)
    : _tech_registry(tech_registry),
      _library_registry(library_registry),
      _root(createRoot(_registry)),
      _global(_registry, _root),
      _floorplan(_registry, _tech_registry, _library_registry, _root),
      _netlist(_registry, _tech_registry, _library_registry),
      _routing(_registry, _tech_registry),
      _constraints(_registry, _tech_registry),
      _fills(_registry, _tech_registry)
{
}

void DesignStore::resetForBinaryLoad()
{
  _registry.registry() = DesignRegistry::registry_type{};
  _root = {};
  _global.rebindRoot(_root);
  _floorplan.rebindRoot(_root);
  _routing.clearRoutingPool();
  _netlist.rebuildNameIndexes();
  _routing.rebuildWireIndex();
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
  _netlist.rebuildNameIndexes();
  _routing.rebuildWireIndex();
  _routing.validateRestoredRoutingState();
}

}  // namespace eccdb
