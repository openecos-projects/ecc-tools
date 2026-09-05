#include "library/master_term/storage/MasterTermStorage.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_port/model/MasterPortComponents.h"

namespace eccdb {

LibraryMasterTermId LibraryMasterTermStorage::createMasterTerm(LibraryCellMasterId owner, LibraryMasterTerm term)
{
  ensureMaster(owner);
  validateMasterTerm(owner, term);
  return createMasterTermTrusted(owner, std::move(term));
}

LibraryMasterTermId LibraryMasterTermStorage::createMasterTermTrusted(LibraryCellMasterId owner, LibraryMasterTerm term)
{
  ensureMaster(owner);
  term.master = owner;
  term.ports.clear();

  const auto entity = _registry.create();
  try {
    _registry.emplace<LibraryMasterTerm>(entity, std::move(term));
    _registry.get<LibraryCellMaster>(owner.entity()).terms.emplace_back(entity);
    return LibraryMasterTermId{entity};
  } catch (...) {
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
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
  for (const auto id : _registry.get<const LibraryCellMaster>(owner.entity()).terms) {
    if (!contains(id)) {
      throw std::logic_error("corrupt library master-term relation");
    }
    const auto& term = _registry.get<const LibraryMasterTerm>(id.entity());
    if (term.master != owner) {
      throw std::logic_error("corrupt library master-term owner");
    }
    if (term.name == name) {
      return id;
    }
  }
  return {};
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
  validateMasterTerm(master, term, id);
  term.master = master;
  term.ports = _registry.get<const LibraryMasterTerm>(id.entity()).ports;
  _registry.replace<LibraryMasterTerm>(id.entity(), std::move(term));
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
      _registry.destroy(port_id.entity());
    }
  }

  if (_registry.valid(term.master.entity()) && _registry.all_of<LibraryCellMaster>(term.master.entity())) {
    auto& terms = _registry.get<LibraryCellMaster>(term.master.entity()).terms;
    const auto first = std::remove(terms.begin(), terms.end(), id);
    terms.erase(first, terms.end());
  }

  _registry.destroy(id.entity());
  return true;
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
