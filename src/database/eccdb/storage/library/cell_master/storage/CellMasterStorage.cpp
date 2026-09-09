#include "library/cell_master/storage/CellMasterStorage.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "library/common/LibraryGeometry.h"
#include "library/master_port/model/MasterPortComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "library/site/model/SiteComponents.h"
#include "tech/TechRegistry.h"

namespace eccdb {

LibraryCellMasterId LibraryCellMasterStorage::createCellMaster(LibraryCellMaster master)
{
  validateCellMaster(master);
  return createCellMasterTrusted(std::move(master));
}

LibraryCellMasterId LibraryCellMasterStorage::createCellMasterTrusted(LibraryCellMaster master)
{
  master.terms.clear();
  const auto entity = _registry.create();
  try {
    _registry.emplace<LibraryCellMaster>(entity, std::move(master));
    return LibraryCellMasterId{entity};
  } catch (...) {
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
}

bool LibraryCellMasterStorage::contains(LibraryCellMasterId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<LibraryCellMaster>(id.entity());
}

LibraryCellMasterId LibraryCellMasterStorage::findCellMasterById(uint32_t id) const
{
  const auto entity = static_cast<LibraryEntity>(id);
  return _registry.valid(entity) && _registry.all_of<LibraryCellMaster>(entity) ? LibraryCellMasterId{entity} : LibraryCellMasterId{};
}

LibraryCellMasterId LibraryCellMasterStorage::findCellMaster(std::string_view name) const
{
  const auto view = _registry.view<const LibraryCellMaster>();
  for (const auto entity : view) {
    if (view.get<const LibraryCellMaster>(entity).name == name) {
      return LibraryCellMasterId{entity};
    }
  }
  return {};
}

std::vector<LibraryCellMasterId> LibraryCellMasterStorage::cellMasters() const
{
  std::vector<LibraryCellMasterId> result;
  const auto view = _registry.view<const LibraryCellMaster>();
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

std::size_t LibraryCellMasterStorage::cellMasterCount() const
{
  std::size_t count = 0;
  const auto view = _registry.view<const LibraryCellMaster>();
  for (const auto entity : view) {
    static_cast<void>(entity);
    ++count;
  }
  return count;
}

const LibraryCellMaster& LibraryCellMasterStorage::cellMaster(LibraryCellMasterId id) const
{
  ensureCellMaster(id);
  return _registry.get<const LibraryCellMaster>(id.entity());
}

void LibraryCellMasterStorage::updateCellMaster(LibraryCellMasterId id, LibraryCellMaster master)
{
  ensureCellMaster(id);
  validateCellMaster(master, id);
  updateCellMasterTrusted(id, std::move(master));
}

void LibraryCellMasterStorage::updateCellMasterTrusted(LibraryCellMasterId id, LibraryCellMaster master)
{
  ensureCellMaster(id);
  master.terms = _registry.get<const LibraryCellMaster>(id.entity()).terms;
  _registry.replace<LibraryCellMaster>(id.entity(), std::move(master));
}

bool LibraryCellMasterStorage::destroyCellMaster(LibraryCellMasterId id)
{
  if (!contains(id)) {
    return false;
  }

  const auto term_ids = _registry.get<const LibraryCellMaster>(id.entity()).terms;
  for (const auto term_id : term_ids) {
    if (!_registry.valid(term_id.entity()) || !_registry.all_of<LibraryMasterTerm>(term_id.entity())) {
      continue;
    }

    const auto& term = _registry.get<const LibraryMasterTerm>(term_id.entity());
    if (term.master != id) {
      continue;
    }
    for (const auto port_id : term.ports) {
      if (_registry.valid(port_id.entity()) && _registry.all_of<LibraryMasterPort>(port_id.entity())
          && _registry.get<const LibraryMasterPort>(port_id.entity()).term == term_id) {
        _registry.destroy(port_id.entity());
      }
    }
    _registry.destroy(term_id.entity());
  }

  _registry.destroy(id.entity());
  return true;
}

bool LibraryCellMasterStorage::hasObs(LibraryCellMasterId owner) const
{
  ensureCellMaster(owner);
  return _registry.all_of<LibraryMasterObs>(owner.entity());
}

const LibraryMasterObs& LibraryCellMasterStorage::obs(LibraryCellMasterId owner) const
{
  ensureCellMaster(owner);
  const auto* value = _registry.try_get<const LibraryMasterObs>(owner.entity());
  if (value == nullptr) {
    throw std::out_of_range("library cell master has no OBS component");
  }
  return *value;
}

void LibraryCellMasterStorage::setObs(LibraryCellMasterId owner, LibraryMasterObsInput obs)
{
  const auto mark = _geometry.checkpoint();
  try {
    ensureCellMaster(owner);
    validateObs(obs);
    _registry.emplace_or_replace<LibraryMasterObs>(owner.entity(), materializeObs(std::move(obs), true));
  } catch (...) {
    _geometry.rollback(mark);
    throw;
  }
}

void LibraryCellMasterStorage::appendObs(LibraryCellMasterId owner, LibraryMasterObsInput obs)
{
  const auto mark = _geometry.checkpoint();
  try {
    ensureCellMaster(owner);
    validateObs(obs);
    auto materialized = materializeObs(std::move(obs), true);
    auto* existing = _registry.try_get<LibraryMasterObs>(owner.entity());
    if (existing == nullptr) {
      _registry.emplace<LibraryMasterObs>(owner.entity(), std::move(materialized));
      return;
    }
    existing->layer_clauses.reserve(existing->layer_clauses.size() + materialized.layer_clauses.size());
    existing->vias.reserve(existing->vias.size() + materialized.vias.size());
    for (auto& clause : materialized.layer_clauses) {
      existing->layer_clauses.push_back(std::move(clause));
    }
    for (auto& via : materialized.vias) {
      existing->vias.push_back(std::move(via));
    }
  } catch (...) {
    _geometry.rollback(mark);
    throw;
  }
}

void LibraryCellMasterStorage::appendObsTrusted(LibraryCellMasterId owner, LibraryMasterObsInput obs)
{
  const auto mark = _geometry.checkpoint();
  try {
    ensureCellMaster(owner);
    auto materialized = materializeObs(std::move(obs), false);
    auto* existing = _registry.try_get<LibraryMasterObs>(owner.entity());
    if (existing == nullptr) {
      _registry.emplace<LibraryMasterObs>(owner.entity(), std::move(materialized));
      return;
    }
    existing->layer_clauses.reserve(existing->layer_clauses.size() + materialized.layer_clauses.size());
    existing->vias.reserve(existing->vias.size() + materialized.vias.size());
    for (auto& clause : materialized.layer_clauses) existing->layer_clauses.push_back(std::move(clause));
    for (auto& via : materialized.vias) existing->vias.push_back(std::move(via));
  } catch (...) {
    _geometry.rollback(mark);
    throw;
  }
}

void LibraryCellMasterStorage::clearObs(LibraryCellMasterId owner)
{
  ensureCellMaster(owner);
  _registry.remove<LibraryMasterObs>(owner.entity());
}

void LibraryCellMasterStorage::ensureCellMaster(LibraryCellMasterId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid library cell master id");
  }
}

void LibraryCellMasterStorage::validateCellMaster(const LibraryCellMaster& master, LibraryCellMasterId ignored) const
{
  if (master.name.empty()) {
    throw std::invalid_argument("library cell master name is required");
  }

  const auto existing = findCellMaster(master.name);
  if (existing && existing != ignored) {
    throw std::invalid_argument("duplicate library cell master name: " + master.name);
  }

  if (master.site.has_value() && (!_registry.valid(master.site->entity()) || !_registry.all_of<LibrarySite>(master.site->entity()))) {
    throw std::invalid_argument("library cell master references an invalid site");
  }
}

void LibraryCellMasterStorage::validateObs(const LibraryMasterObsInput& obs) const
{
  if (obs.layer_clauses.empty() && obs.vias.empty()) {
    throw std::invalid_argument("library cell master OBS must contain geometry");
  }
  for (const auto& clause : obs.layer_clauses) {
    validateObsLayerClause(clause);
  }
  validateLibraryViaPlacements(_tech_registry, obs.vias);
}

void LibraryCellMasterStorage::validateObsLayerClause(const LibraryObsLayerClauseInput& clause) const
{
  const auto& registry = _tech_registry.registry();
  if (!clause.layer || !registry.valid(clause.layer.entity()) || !registry.all_of<TechLayerInfo>(clause.layer.entity())) {
    throw std::invalid_argument("library OBS references an invalid tech layer");
  }

  constexpr uint32_t kKnownFlags = LibraryObsLayerFlag::kExceptPgNet | LibraryObsLayerFlag::kHasSpacing
                                   | LibraryObsLayerFlag::kHasDesignRuleWidth | LibraryObsLayerFlag::kHasPathWidth;
  if ((clause.flags & ~kKnownFlags) != 0u) {
    throw std::invalid_argument("library OBS layer clause has unknown flags");
  }

  const bool has_spacing = (clause.flags & LibraryObsLayerFlag::kHasSpacing) != 0u;
  const bool has_design_rule_width = (clause.flags & LibraryObsLayerFlag::kHasDesignRuleWidth) != 0u;
  const bool has_path_width = (clause.flags & LibraryObsLayerFlag::kHasPathWidth) != 0u;
  if (has_spacing && has_design_rule_width) {
    throw std::invalid_argument("library OBS layer clause cannot combine SPACING and DESIGNRULEWIDTH");
  }
  if ((has_spacing && clause.spacing <= 0) || (!has_spacing && clause.spacing != 0)) {
    throw std::invalid_argument("library OBS layer clause has inconsistent SPACING payload");
  }
  if ((has_design_rule_width && clause.design_rule_width <= 0) || (!has_design_rule_width && clause.design_rule_width != 0)) {
    throw std::invalid_argument("library OBS layer clause has inconsistent DESIGNRULEWIDTH payload");
  }
  if ((has_path_width && clause.path_width <= 0) || (!has_path_width && clause.path_width != 0)) {
    throw std::invalid_argument("library OBS layer clause has inconsistent WIDTH payload");
  }
  if (clause.geometry.empty()) {
    throw std::invalid_argument("library OBS layer clause must contain geometry");
  }
  for (const auto rect : clause.geometry.rects) {
    if (!rect.isValid() || !rect.hasArea()) {
      throw std::invalid_argument("library OBS rectangle must have area");
    }
  }
}

LibraryMasterObs LibraryCellMasterStorage::materializeObs(LibraryMasterObsInput obs, bool validate_geometry)
{
  LibraryMasterObs result{.vias = std::move(obs.vias)};
  result.layer_clauses.reserve(obs.layer_clauses.size());
  for (auto& clause : obs.layer_clauses) {
    result.layer_clauses.push_back(LibraryObsLayerClause{.layer = clause.layer,
                                                         .flags = clause.flags,
                                                         .spacing = clause.spacing,
                                                         .design_rule_width = clause.design_rule_width,
                                                         .path_width = clause.path_width,
                                                         .geometry = validate_geometry
                                                                          ? _geometry.append(clause.geometry)
                                                                          : _geometry.appendTrusted(clause.geometry.rects,
                                                                                                    clause.geometry.polygons)});
  }
  return result;
}

}  // namespace eccdb
