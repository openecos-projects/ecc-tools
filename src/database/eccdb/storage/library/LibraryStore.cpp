#include "library/LibraryStore.h"

namespace eccdb {

LibraryStore::LibraryStore(const TechRegistry& tech_registry, LibraryStoreOptions options)
    : _tech_registry(tech_registry),
      _geometry(options.geometry),
      _sites(_registry),
      _cell_masters(_registry, _tech_registry, _geometry),
      _master_terms(_registry),
      _master_ports(_registry, _tech_registry, _geometry)
{
}

}  // namespace eccdb
