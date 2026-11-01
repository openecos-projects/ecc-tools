// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

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

// Keep borrowed index component addresses stable across unrelated master
// deletion. The map itself remains a plain dense_map.
template <typename Entity>
struct component_traits<eccdb::LibraryMasterTermNameIndex, Entity>
{
  using element_type = eccdb::LibraryMasterTermNameIndex;
  using entity_type = Entity;

  static constexpr bool in_place_delete = true;
  static constexpr std::size_t page_size = ENTT_PACKED_PAGE;
};

}  // namespace entt
