#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include <entt/container/dense_map.hpp>
#include <entt/entity/component.hpp>

#include "index/NameHash.h"
#include "library/common/LibraryTypes.h"

namespace eccdb {

struct LibraryMasterTermNameIndexEntry
{
  LibraryMasterTermId term;
  std::size_t ordinal = 0;
};

// Derived, non-persistent map attached to each LibraryCellMaster. The ordinal
// follows LibraryCellMaster::terms and is rebuilt after a restore.
using LibraryMasterTermNameIndex
    = entt::dense_map<std::string, LibraryMasterTermNameIndexEntry, index::TransparentStringHash, std::equal_to<>>;

}  // namespace eccdb

namespace entt {

// DesignInstancePins caches this component's address. Keep the map itself as
// a plain dense_map while using tombstones instead of moving an unrelated map
// on deletion.
template <typename Entity>
struct component_traits<eccdb::LibraryMasterTermNameIndex, Entity>
{
  using element_type = eccdb::LibraryMasterTermNameIndex;
  using entity_type = Entity;

  static constexpr bool in_place_delete = true;
  static constexpr std::size_t page_size = ENTT_PACKED_PAGE;
};

}  // namespace entt
