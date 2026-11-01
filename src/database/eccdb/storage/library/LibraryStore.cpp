// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "library/LibraryStore.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

#include <entt/container/dense_set.hpp>

#include "common/GeometryBounds.h"
#include "geometry/DomainGeometryComponents.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/cell_master/model/MasterObsComponents.h"
#include "library/common/LibraryGeometry.h"
#include "library/master_port/model/MasterPortComponents.h"
#include "library/master_term/model/MasterTermComponents.h"

namespace eccdb {

LibraryStore::LibraryStore(const TechRegistry& tech_registry, LibraryStoreOptions)
    : _tech_registry(tech_registry),
      _sites(_registry),
      _cell_masters(_registry, _tech_registry, _geometry),
      _master_terms(_registry, _geometry),
      _master_ports(_registry, _tech_registry, _geometry)
{
}

void LibraryStore::validateRestoredGeometry() const
{
  const auto& objects = _registry.registry();
  const auto& geometry = _geometry.geometryRegistry();
  const auto& geometry_registry = geometry.registry();
  // Bounds are derived runtime data and are not serialized. Reattach the
  // component to restored library entities and geometry primitives before
  // validating relations; all entries start dirty and can be rebuilt lazily.
  const auto attach = [&objects](auto view) {
    for (const auto entity : view) {
      static_cast<void>(const_cast<LibraryRegistry::registry_type&>(objects).get_or_emplace<GeometryBounds>(entity));
    }
  };
  attach(objects.view<LibraryCellMaster>());
  attach(objects.view<LibraryMasterTerm>());
  attach(objects.view<LibraryMasterPort>());
  for (const auto entity : geometry_registry.view<const GeometryShape>()) {
    static_cast<void>(geometry.ensureBounds(GeometryShapeId{entity}));
  }
  entt::dense_set<GeometryEntity> claimed;
  const auto payloadCount = [&geometry_registry](GeometryEntity entity) {
    return static_cast<unsigned>(geometry_registry.all_of<GeometryRectangle>(entity))
           + static_cast<unsigned>(geometry_registry.all_of<GeometryPolygon>(entity))
           + static_cast<unsigned>(geometry_registry.all_of<LibraryViaPlacement>(entity));
  };

  const auto claim = [&](GeometryOwner owner, GeometryShapeId id) {
    if (!geometry.contains(id) || !claimed.emplace(id.entity()).second) {
      throw std::runtime_error("binary library geometry has a missing or duplicate owner relation");
    }
    const auto& metadata = geometry.shape(id);
    if (metadata.owner.domain != owner.domain || metadata.owner.value != owner.value) {
      throw std::runtime_error("binary library geometry owner disagrees with its relation");
    }
    if (payloadCount(id.entity()) != 1u) {
      throw std::runtime_error("binary library geometry has missing or extra typed payloads");
    }
    return metadata;
  };
  const auto claimLayerGeometry = [&](GeometryOwner owner, const auto& ids) {
    for (const auto id : ids) {
      const auto& metadata = claim(owner, id);
      if (metadata.kind == GeometryShapeKind::kRectangle) {
        if (geometry.tryGet<GeometryRectangle>(id) == nullptr) {
          throw std::runtime_error("binary library rectangle geometry payload is missing");
        }
      } else if (metadata.kind == GeometryShapeKind::kPolygon) {
        if (geometry.tryGet<GeometryPolygon>(id) == nullptr) {
          throw std::runtime_error("binary library polygon geometry payload is missing");
        }
      } else {
        throw std::runtime_error("binary library layer geometry has an invalid kind");
      }
    }
  };
  const auto claimVias = [&](GeometryOwner owner, const auto& ids) {
    for (const auto id : ids) {
      const auto& metadata = claim(owner, id);
      const auto* via = geometry.tryGet<LibraryViaPlacement>(id);
      if (metadata.kind != GeometryShapeKind::kLibraryVia || via == nullptr) {
        throw std::runtime_error("binary library VIA geometry payload is missing");
      }
      try {
        validateLibraryViaPlacements(_tech_registry, std::vector<LibraryViaPlacement>{*via});
      } catch (const std::exception&) {
        throw std::runtime_error("binary library VIA geometry references an invalid tech VIA");
      }
    }
  };

  const auto port_view = objects.view<const LibraryMasterPort>();
  for (const auto entity : port_view) {
    const auto& port = port_view.get<const LibraryMasterPort>(entity);
    const auto port_id = LibraryMasterPortId{entity};
    if (!port.term || !objects.valid(port.term.entity()) || !objects.all_of<LibraryMasterTerm>(port.term.entity())) {
      throw std::runtime_error("binary library master PORT has an invalid TERM owner");
    }
    const auto& term = objects.get<const LibraryMasterTerm>(port.term.entity());
    if (std::find(term.ports.begin(), term.ports.end(), port_id) == term.ports.end()) {
      throw std::runtime_error("binary library master PORT is missing from its TERM relation");
    }
    const auto owner = GeometryOwner::from(GeometryOwnerDomain::kLibraryMaster, LibraryMasterPortId{entity});
    for (const auto& clause : port.layer_clauses) {
      claimLayerGeometry(owner, clause.geometry);
    }
    claimVias(owner, port.vias);
  }

  const auto obs_view = objects.view<const LibraryMasterObs>();
  for (const auto entity : obs_view) {
    if (!objects.all_of<LibraryCellMaster>(entity)) {
      throw std::runtime_error("binary library OBS has an invalid MACRO owner");
    }
    const auto& obs = obs_view.get<const LibraryMasterObs>(entity);
    const auto owner = GeometryOwner::from(GeometryOwnerDomain::kLibraryMaster, LibraryCellMasterId{entity});
    for (const auto& clause : obs.layer_clauses) {
      claimLayerGeometry(owner, clause.geometry);
    }
    claimVias(owner, obs.vias);
  }

  const auto metadata = geometry_registry.view<const GeometryShape>();
  for (const auto entity : metadata) {
    if (!claimed.contains(entity) || payloadCount(entity) != 1u) {
      throw std::runtime_error("binary library geometry entity has no owner relation or an invalid payload count");
    }
  }

  const auto validatePayloadMetadata = [&]<typename Payload>() {
    const auto payloads = geometry_registry.view<const Payload>();
    for (const auto entity : payloads) {
      if (!geometry_registry.all_of<GeometryShape>(entity)) {
        throw std::runtime_error("binary library geometry payload has no GeometryShape metadata");
      }
    }
  };
  validatePayloadMetadata.template operator()<GeometryRectangle>();
  validatePayloadMetadata.template operator()<GeometryPolygon>();
  validatePayloadMetadata.template operator()<LibraryViaPlacement>();
}

}  // namespace eccdb
