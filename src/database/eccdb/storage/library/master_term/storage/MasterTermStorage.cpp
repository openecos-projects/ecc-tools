// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "library/master_term/storage/MasterTermStorage.h"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "library/cell_master/model/CellMasterComponents.h"
#include "common/GeometryBounds.h"
#include "library/master_port/model/MasterPortComponents.h"

namespace eccdb {
namespace {

void reserveNameInsert(LibraryMasterTermNameIndex& index)
{
  const auto next_size = index.size() + 1u;
  const auto bucket_capacity = static_cast<std::size_t>(static_cast<float>(index.bucket_count()) * index.max_load_factor());
  if (next_size > bucket_capacity) {
    index.reserve(std::max(next_size, index.size() * 2u));
  }
}

bool isCurrentMasterTermNameEntry(const LibraryMasterTermStorage::registry_type& registry, LibraryCellMasterId owner,
                                  std::string_view name, const LibraryMasterTermNameIndexEntry& entry)
{
  if (!owner || !registry.valid(owner.entity()) || !registry.all_of<LibraryCellMaster>(owner.entity())) {
    return false;
  }

  const auto& terms = registry.get<const LibraryCellMaster>(owner.entity()).terms;
  if (entry.ordinal >= terms.size() || terms[entry.ordinal] != entry.term || !entry.term || !registry.valid(entry.term.entity())
      || !registry.all_of<LibraryMasterTerm>(entry.term.entity())) {
    return false;
  }

  const auto& term = registry.get<const LibraryMasterTerm>(entry.term.entity());
  return term.master == owner && term.name == name;
}

LibraryMasterTermNameIndex makeMasterTermNameIndex(const LibraryMasterTermStorage::registry_type& registry,
                                                   LibraryCellMasterId owner)
{
  LibraryMasterTermNameIndex result;
  if (!owner || !registry.valid(owner.entity()) || !registry.all_of<LibraryCellMaster>(owner.entity())) {
    return result;
  }

  const auto& terms = registry.get<const LibraryCellMaster>(owner.entity()).terms;
  result.reserve(terms.size());
  for (std::size_t ordinal = 0; ordinal < terms.size(); ++ordinal) {
    const auto id = terms[ordinal];
    if (!id || !registry.valid(id.entity()) || !registry.all_of<LibraryMasterTerm>(id.entity())) {
      throw std::logic_error("corrupt library master-term relation");
    }
    const auto& term = registry.get<const LibraryMasterTerm>(id.entity());
    if (term.master != owner) {
      throw std::logic_error("corrupt library master-term owner");
    }
    static_cast<void>(result.try_emplace(term.name, LibraryMasterTermNameIndexEntry{.term = id, .ordinal = ordinal}));
  }
  return result;
}

void updateMasterTermNameIndexForRename(LibraryMasterTermNameIndex& index,
                                        const LibraryMasterTermStorage::registry_type& registry, LibraryCellMasterId owner,
                                        LibraryMasterTermId id, std::size_t ordinal, std::string_view old_name,
                                        std::string_view new_name)
{
  if (const auto old = index.find(old_name); old != index.end() && old->second.term == id) {
    index.erase(old);
  }

  const auto found = index.find(new_name);
  const LibraryMasterTermNameIndexEntry entry{.term = id, .ordinal = ordinal};
  if (found == index.end()) {
    reserveNameInsert(index);
    static_cast<void>(index.emplace(std::string{new_name}, entry));
  } else if (!isCurrentMasterTermNameEntry(registry, owner, new_name, found->second)) {
    found->second = entry;
  }
}

}  // namespace

std::optional<LibraryMasterTermNameIndexEntry> findLibraryMasterTermName(const LibraryRegistry::registry_type& registry,
                                                                          LibraryCellMasterId owner, std::string_view name)
{
  if (const auto* index = registry.try_get<const LibraryMasterTermNameIndex>(owner.entity()); index != nullptr) {
    const auto found = index->find(name);
    if (found != index->end()) {
      return found->second;
    }
  }
  return std::nullopt;
}

LibraryMasterTermId LibraryMasterTermStorage::createMasterTerm(LibraryCellMasterId owner, LibraryMasterTerm term)
{
  ensureMaster(owner);
  validateMasterTerm(owner, term);
  return createMasterTermTrusted(owner, std::move(term));
}

LibraryMasterTermId LibraryMasterTermStorage::createMasterTermTrusted(LibraryCellMasterId owner, LibraryMasterTerm term)
{
  ensureMaster(owner);
  const auto name = term.name;
  term.master = owner;
  term.ports.clear();

  auto* index = _registry.try_get<LibraryMasterTermNameIndex>(owner.entity());
  const bool index_was_missing = index == nullptr;

  std::optional<LibraryEntity> entity;
  LibraryMasterTermId id;
  bool appended_to_master = false;
  try {
    if (index == nullptr) {
      _registry.emplace<LibraryMasterTermNameIndex>(owner.entity());
      index = _registry.try_get<LibraryMasterTermNameIndex>(owner.entity());
    }
    // Reserve before mutating the registry so dense_map insertion cannot need
    // a rehash after the term has been made visible.
    reserveNameInsert(*index);
    entity = _registry.create();
    id = LibraryMasterTermId{*entity};
    _registry.emplace<LibraryMasterTerm>(*entity, std::move(term));
    _registry.emplace<GeometryBounds>(*entity);
    auto& terms = _registry.get<LibraryCellMaster>(owner.entity()).terms;
    const auto ordinal = terms.size();
    terms.emplace_back(*entity);
    appended_to_master = true;

    const auto found = index->find(name);
    const LibraryMasterTermNameIndexEntry entry{.term = id, .ordinal = ordinal};
    if (found == index->end()) {
      static_cast<void>(index->emplace(name, entry));
    } else if (!isCurrentMasterTermNameEntry(_registry, owner, name, found->second)) {
      found->second = entry;
    }
    return id;
  } catch (...) {
    if (appended_to_master) {
      auto& terms = _registry.get<LibraryCellMaster>(owner.entity()).terms;
      const auto first = std::remove(terms.begin(), terms.end(), id);
      terms.erase(first, terms.end());
    }
    if (!index_was_missing && id) {
      const auto found = index->find(name);
      if (found != index->end() && found->second.term == id) {
        index->erase(name);
      }
    }
    if (entity.has_value() && _registry.valid(*entity)) {
      _registry.destroy(*entity);
    }
    if (index_was_missing && _registry.all_of<LibraryMasterTermNameIndex>(owner.entity())) {
      _registry.remove<LibraryMasterTermNameIndex>(owner.entity());
    }
    throw;
  }
}

bool LibraryMasterTermStorage::contains(LibraryMasterTermId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<LibraryMasterTerm>(id.entity());
}

LibraryMasterTermId LibraryMasterTermStorage::findMasterTermById(uint32_t id) const
{
  const auto entity = static_cast<LibraryEntity>(id);
  return _registry.valid(entity) && _registry.all_of<LibraryMasterTerm>(entity) ? LibraryMasterTermId{entity} : LibraryMasterTermId{};
}

LibraryMasterTermId LibraryMasterTermStorage::findMasterTerm(LibraryCellMasterId owner, std::string_view name) const
{
  ensureMaster(owner);
  const auto resolved = findLibraryMasterTermName(_registry, owner, name);
  return resolved.has_value() ? resolved->term : LibraryMasterTermId{};
}

std::vector<LibraryMasterTermId> LibraryMasterTermStorage::masterTerms() const
{
  std::vector<LibraryMasterTermId> result;
  const auto view = _registry.view<const LibraryMasterTerm>();
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

std::vector<LibraryMasterTermId> LibraryMasterTermStorage::masterTerms(LibraryCellMasterId owner) const
{
  ensureMaster(owner);
  const auto& terms = _registry.get<const LibraryCellMaster>(owner.entity()).terms;
  std::vector<LibraryMasterTermId> result;
  result.reserve(terms.size());
  for (const auto id : terms) {
    if (!contains(id) || _registry.get<const LibraryMasterTerm>(id.entity()).master != owner) {
      throw std::logic_error("corrupt library master-term relation");
    }
    result.push_back(id);
  }
  return result;
}

std::size_t LibraryMasterTermStorage::masterTermCount() const
{
  std::size_t count = 0;
  for (const auto entity : _registry.view<const LibraryMasterTerm>()) {
    static_cast<void>(entity);
    ++count;
  }
  return count;
}

const LibraryMasterTerm& LibraryMasterTermStorage::masterTerm(LibraryMasterTermId id) const
{
  ensureMasterTerm(id);
  return _registry.get<const LibraryMasterTerm>(id.entity());
}

LibraryCellMasterId LibraryMasterTermStorage::owner(LibraryMasterTermId id) const
{
  return masterTerm(id).master;
}

void LibraryMasterTermStorage::updateMasterTerm(LibraryMasterTermId id, LibraryMasterTerm term)
{
  ensureMasterTerm(id);
  const auto master = owner(id);
  const auto& stored = _registry.get<const LibraryMasterTerm>(id.entity());
  validateMasterTerm(master, term, id);
  term.master = master;
  term.ports = stored.ports;
  if (term.name == stored.name) {
    _registry.replace<LibraryMasterTerm>(id.entity(), std::move(term));
    return;
  }

  const auto& terms = _registry.get<const LibraryCellMaster>(master.entity()).terms;
  const auto position = std::find(terms.begin(), terms.end(), id);
  if (position == terms.end()) {
    throw std::logic_error("corrupt library master-term relation");
  }
  const auto ordinal = static_cast<std::size_t>(position - terms.begin());

  const auto* current_index = _registry.try_get<const LibraryMasterTermNameIndex>(master.entity());
  auto next_index = current_index == nullptr ? makeMasterTermNameIndex(_registry, master) : *current_index;
  updateMasterTermNameIndexForRename(next_index, _registry, master, id, ordinal, stored.name, term.name);

  if (current_index != nullptr) {
    auto& index = _registry.get<LibraryMasterTermNameIndex>(master.entity());
    index.swap(next_index);
    try {
      _registry.replace<LibraryMasterTerm>(id.entity(), std::move(term));
    } catch (...) {
      index.swap(next_index);
      throw;
    }
    return;
  }

  try {
    _registry.emplace<LibraryMasterTermNameIndex>(master.entity(), std::move(next_index));
    _registry.replace<LibraryMasterTerm>(id.entity(), std::move(term));
  } catch (...) {
    if (_registry.all_of<LibraryMasterTermNameIndex>(master.entity())) {
      _registry.remove<LibraryMasterTermNameIndex>(master.entity());
    }
    throw;
  }
}

bool LibraryMasterTermStorage::destroyMasterTerm(LibraryMasterTermId id)
{
  if (!contains(id)) {
    return false;
  }

  const auto term = _registry.get<const LibraryMasterTerm>(id.entity());
  for (const auto port_id : term.ports) {
    if (_registry.valid(port_id.entity()) && _registry.all_of<LibraryMasterPort>(port_id.entity())
        && _registry.get<const LibraryMasterPort>(port_id.entity()).term == id) {
      const auto& port = _registry.get<const LibraryMasterPort>(port_id.entity());
      for (const auto& clause : port.layer_clauses) {
        _geometry.destroy(clause.geometry);
      }
      for (const auto shape : port.vias) {
        static_cast<void>(_geometry.geometryRegistry().destroy(shape));
      }
      _registry.destroy(port_id.entity());
    }
  }

  if (_registry.valid(term.master.entity()) && _registry.all_of<LibraryCellMaster>(term.master.entity())) {
    auto& terms = _registry.get<LibraryCellMaster>(term.master.entity()).terms;
    const auto first = std::remove(terms.begin(), terms.end(), id);
    terms.erase(first, terms.end());

    if (auto* index = _registry.try_get<LibraryMasterTermNameIndex>(term.master.entity()); index != nullptr) {
      const auto found = index->find(term.name);
      if (found != index->end() && found->second.term == id) {
        index->erase(term.name);
      }
      for (std::size_t ordinal = 0; ordinal < terms.size(); ++ordinal) {
        const auto& remaining = _registry.get<const LibraryMasterTerm>(terms[ordinal].entity());
        const auto current = index->find(remaining.name);
        if (current != index->end() && current->second.term == terms[ordinal]) {
          current->second.ordinal = ordinal;
        }
      }
    }
  }

  _registry.destroy(id.entity());
  return true;
}

void LibraryMasterTermStorage::rebuildNameIndexes()
{
  struct PendingIndex
  {
    LibraryEntity entity;
    LibraryMasterTermNameIndex index;
    bool had_index = false;
    bool committed = false;
  };

  std::vector<PendingIndex> pending;
  const auto masters = _registry.view<const LibraryCellMaster>();
  pending.reserve(_registry.storage<LibraryCellMaster>().size());
  for (const auto entity : masters) {
    pending.push_back(PendingIndex{.entity = entity,
                                   .index = makeMasterTermNameIndex(_registry, LibraryCellMasterId{entity}),
                                   .had_index = _registry.all_of<LibraryMasterTermNameIndex>(entity)});
  }

  try {
    for (auto& entry : pending) {
      if (entry.had_index) {
        _registry.get<LibraryMasterTermNameIndex>(entry.entity).swap(entry.index);
      } else {
        _registry.emplace<LibraryMasterTermNameIndex>(entry.entity, std::move(entry.index));
      }
      entry.committed = true;
    }
  } catch (...) {
    for (auto current = pending.rbegin(); current != pending.rend(); ++current) {
      if (current->had_index && current->committed) {
        _registry.get<LibraryMasterTermNameIndex>(current->entity).swap(current->index);
      } else if (!current->had_index && _registry.all_of<LibraryMasterTermNameIndex>(current->entity)) {
        _registry.remove<LibraryMasterTermNameIndex>(current->entity);
      }
    }
    throw;
  }
}

void LibraryMasterTermStorage::ensureMaster(LibraryCellMasterId id) const
{
  if (!_registry.valid(id.entity()) || !_registry.all_of<LibraryCellMaster>(id.entity())) {
    throw std::out_of_range("invalid library cell master id");
  }
}

void LibraryMasterTermStorage::ensureMasterTerm(LibraryMasterTermId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid library master term id");
  }
}

void LibraryMasterTermStorage::validateMasterTerm(LibraryCellMasterId owner, const LibraryMasterTerm& term,
                                                  LibraryMasterTermId ignored) const
{
  if (term.name.empty()) {
    throw std::invalid_argument("library master term name is required");
  }

  const auto existing = findMasterTerm(owner, term.name);
  if (existing && existing != ignored) {
    throw std::invalid_argument("duplicate library master term name: " + term.name);
  }
}

}  // namespace eccdb
