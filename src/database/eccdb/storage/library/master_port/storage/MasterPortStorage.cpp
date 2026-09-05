#include "library/master_port/storage/MasterPortStorage.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "library/common/LibraryGeometry.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "tech/TechRegistry.h"

namespace eccdb {

LibraryMasterPortId LibraryMasterPortStorage::createMasterPort(LibraryMasterTermId owner, LibraryMasterPortInput port)
{
  const auto mark = _geometry.checkpoint();
  try {
    ensureMasterTerm(owner);
    validateMasterPort(port);
    auto stored = materializeMasterPort(std::move(port), true);
    stored.term = owner;

    const auto entity = _registry.create();
    try {
      _registry.emplace<LibraryMasterPort>(entity, std::move(stored));
      _registry.get<LibraryMasterTerm>(owner.entity()).ports.emplace_back(entity);
      return LibraryMasterPortId{entity};
    } catch (...) {
      if (_registry.valid(entity)) {
        _registry.destroy(entity);
      }
      throw;
    }
  } catch (...) {
    _geometry.rollback(mark);
    throw;
  }
}

LibraryMasterPortId LibraryMasterPortStorage::createMasterPortTrusted(LibraryMasterTermId owner, LibraryMasterPortInput port)
{
  const auto mark = _geometry.checkpoint();
  try {
    ensureMasterTerm(owner);
    auto stored = materializeMasterPort(std::move(port), false);
    stored.term = owner;

    const auto entity = _registry.create();
    try {
      _registry.emplace<LibraryMasterPort>(entity, std::move(stored));
      _registry.get<LibraryMasterTerm>(owner.entity()).ports.emplace_back(entity);
      return LibraryMasterPortId{entity};
    } catch (...) {
      if (_registry.valid(entity)) {
        _registry.destroy(entity);
      }
      throw;
    }
  } catch (...) {
    _geometry.rollback(mark);
    throw;
  }
}

bool LibraryMasterPortStorage::contains(LibraryMasterPortId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<LibraryMasterPort>(id.entity());
}

LibraryMasterPortId LibraryMasterPortStorage::findMasterPortById(uint32_t id) const
{
  const auto entity = static_cast<LibraryEntity>(id);
  return _registry.valid(entity) && _registry.all_of<LibraryMasterPort>(entity) ? LibraryMasterPortId{entity} : LibraryMasterPortId{};
}

std::vector<LibraryMasterPortId> LibraryMasterPortStorage::masterPorts() const
{
  std::vector<LibraryMasterPortId> result;
  const auto view = _registry.view<const LibraryMasterPort>();
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

std::vector<LibraryMasterPortId> LibraryMasterPortStorage::masterPorts(LibraryMasterTermId owner) const
{
  ensureMasterTerm(owner);
  const auto& ports = _registry.get<const LibraryMasterTerm>(owner.entity()).ports;
  std::vector<LibraryMasterPortId> result;
  result.reserve(ports.size());
  for (const auto id : ports) {
    if (!contains(id) || _registry.get<const LibraryMasterPort>(id.entity()).term != owner) {
      throw std::logic_error("corrupt library master-port relation");
    }
    result.push_back(id);
  }
  return result;
}

std::size_t LibraryMasterPortStorage::masterPortCount() const
{
  std::size_t count = 0;
  for (const auto entity : _registry.view<const LibraryMasterPort>()) {
    static_cast<void>(entity);
    ++count;
  }
  return count;
}

const LibraryMasterPort& LibraryMasterPortStorage::masterPort(LibraryMasterPortId id) const
{
  ensureMasterPort(id);
  return _registry.get<const LibraryMasterPort>(id.entity());
}

LibraryMasterTermId LibraryMasterPortStorage::owner(LibraryMasterPortId id) const
{
  return masterPort(id).term;
}

std::optional<Rect> LibraryMasterPortStorage::boundingBox(LibraryMasterPortId id) const
{
  const auto& port = masterPort(id);
  std::optional<Rect> result;
  for (const auto& clause : port.layer_clauses) {
    const auto bounds = _geometry.bounds(clause.geometry);
    result = result.has_value() ? result->united(bounds) : bounds;
  }

  const auto& tech_registry = _tech_registry.registry();
  for (const auto& placement : port.vias) {
    const auto& via = tech_registry.get<const TechViaGeometry>(placement.via.entity());
    const auto bounds = via.bounding_box.offset(placement.origin.x, placement.origin.y);
    result = result.has_value() ? result->united(bounds) : bounds;
  }
  return result;
}

void LibraryMasterPortStorage::updateMasterPort(LibraryMasterPortId id, LibraryMasterPortInput port)
{
  const auto mark = _geometry.checkpoint();
  try {
    ensureMasterPort(id);
    validateMasterPort(port);
    auto stored = materializeMasterPort(std::move(port), true);
    stored.term = _registry.get<const LibraryMasterPort>(id.entity()).term;
    _registry.replace<LibraryMasterPort>(id.entity(), std::move(stored));
  } catch (...) {
    _geometry.rollback(mark);
    throw;
  }
}

bool LibraryMasterPortStorage::destroyMasterPort(LibraryMasterPortId id)
{
  if (!contains(id)) {
    return false;
  }

  const auto term = owner(id);
  if (_registry.valid(term.entity()) && _registry.all_of<LibraryMasterTerm>(term.entity())) {
    auto& ports = _registry.get<LibraryMasterTerm>(term.entity()).ports;
    const auto first = std::remove(ports.begin(), ports.end(), id);
    ports.erase(first, ports.end());
  }

  _registry.destroy(id.entity());
  return true;
}

void LibraryMasterPortStorage::ensureMasterTerm(LibraryMasterTermId id) const
{
  if (!_registry.valid(id.entity()) || !_registry.all_of<LibraryMasterTerm>(id.entity())) {
    throw std::out_of_range("invalid library master term id");
  }
}

void LibraryMasterPortStorage::ensureMasterPort(LibraryMasterPortId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid library master port id");
  }
}

void LibraryMasterPortStorage::validateMasterPort(const LibraryMasterPortInput& port) const
{
  const auto& registry = _tech_registry.registry();
  for (const auto& clause : port.layer_clauses) {
    if (!clause.layer || !registry.valid(clause.layer.entity()) || !registry.all_of<TechLayerInfo>(clause.layer.entity())) {
      throw std::invalid_argument("library port rectangle references an invalid tech layer");
    }
    if (clause.geometry.empty()) {
      throw std::invalid_argument("library port LAYER clause must contain geometry");
    }
    for (const auto rect : clause.geometry.rects) {
      if (!rect.isValid() || !rect.hasArea()) {
        throw std::invalid_argument("library port rectangle must have area");
      }
    }
  }
  validateLibraryViaPlacements(_tech_registry, port.vias);
}

LibraryMasterPort LibraryMasterPortStorage::materializeMasterPort(LibraryMasterPortInput port, bool validate_geometry)
{
  LibraryMasterPort result{.port_class = port.port_class, .vias = std::move(port.vias)};
  result.layer_clauses.reserve(port.layer_clauses.size());
  for (auto& clause : port.layer_clauses) {
    result.layer_clauses.push_back(LibraryPortLayerGeometry{.layer = clause.layer,
                                                            .geometry = validate_geometry
                                                                          ? _geometry.append(clause.geometry)
                                                                          : _geometry.appendTrusted(clause.geometry.rects,
                                                                                                    clause.geometry.polygons)});
  }
  return result;
}

}  // namespace eccdb
