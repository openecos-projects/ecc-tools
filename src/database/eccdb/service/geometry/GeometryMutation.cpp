// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "geometry/GeometryMutation.h"

#include <optional>
#include <stdexcept>

#include "design/DesignStore.h"
#include "design/netlist/model/NetlistComponents.h"

namespace eccdb::detail {
namespace {

void appendNet(GeometryMutation& result, DesignNetId net) noexcept
{
  if (!net || result.net_count == result.nets.size()) return;
  for (uint8_t index = 0; index < result.net_count; ++index) {
    if (result.nets[index] == net) return;
  }
  result.nets[result.net_count++] = net;
}

}  // namespace

DatabaseChange GeometryMutation::change(bool permanently_deleted) const noexcept
{
  DatabaseChange result;
  result.domains = domains;
  result.nets = std::span<const DesignNetId>{nets.data(), net_count};
  if (instance) result.instances = std::span<const DesignInstanceId>{&instance, 1u};
  result.shapes = std::span<const GeometryShapeId>{&shape, 1u};
  if (permanently_deleted) result.geometry_deletions = std::span<const GeometryShapeId>{&shape, 1u};
  return result;
}

GeometryMutation captureGeometryMutation(DesignStore& design, GeometryShapeId shape)
{
  auto& geometry = design.geometryStorage();
  if (!geometry.contains(shape)) throw std::out_of_range("invalid geometry entity");

  const auto& metadata = geometry.registry().shape(shape);
  GeometryMutation result{
      .shape = shape,
      .owner = metadata.owner,
      .old_bounds = geometry.boundsUncached(shape),
      .previous_epoch = geometry.geometryEpoch(),
  };

  const auto owner = static_cast<DesignEntity>(metadata.owner.value);
  switch (metadata.owner.domain) {
    case GeometryOwnerDomain::kDesignWire:
      result.domains = DatabaseChangeDomain::kGeometry | DatabaseChangeDomain::kRouting;
      appendNet(result, design.routingStorage().wire(DesignWireId{owner}).net);
      break;
    case GeometryOwnerDomain::kDesignNet:
    case GeometryOwnerDomain::kDesignGuide:
    case GeometryOwnerDomain::kDesignTrackAssignment:
      result.domains = DatabaseChangeDomain::kGeometry | DatabaseChangeDomain::kRouting;
      appendNet(result, DesignNetId{owner});
      break;
    case GeometryOwnerDomain::kDesignBlockPin: {
      result.domains = DatabaseChangeDomain::kGeometry | DatabaseChangeDomain::kPinAccess;
      const auto& pin = design.netlistStorage().blockPin(DesignBlockPinId{owner});
      const auto& term = design.netlistStorage().blockTerm(pin.term);
      appendNet(result, term.net);
      appendNet(result, term.special_net);
      break;
    }
    case GeometryOwnerDomain::kDesignInstance:
      result.domains = DatabaseChangeDomain::kGeometry | DatabaseChangeDomain::kInstances;
      result.instance = DesignInstanceId{owner};
      break;
    case GeometryOwnerDomain::kDesignRegion:
    case GeometryOwnerDomain::kDesignBlockage:
      result.domains = DatabaseChangeDomain::kGeometry | DatabaseChangeDomain::kConstraints;
      break;
    default: break;
  }
  return result;
}

void finishGeometryMutation(DesignStore& design, const GeometryMutation& mutation, bool permanently_deleted)
{
  auto& geometry = design.geometryStorage();
  design.geometryIndex().applyChange(mutation.shape, mutation.old_bounds, mutation.previous_epoch);
  const auto new_bounds = geometry.contains(mutation.shape)
                              ? std::optional<Rect>{geometry.boundsUncached(mutation.shape)}
                              : std::nullopt;
  design.geometryOverview().applyChange(mutation.old_bounds, new_bounds, mutation.previous_epoch);
  if (permanently_deleted) design.geometryIndex().forgetRemovedSlot(mutation.shape);
}

void finishGeometryOwnerReplacement(DesignStore& design, uint64_t previous_epoch) noexcept
{
  if (design.geometryStorage().geometryEpoch() == previous_epoch) return;
  design.geometryIndex().invalidate();
  design.geometryOverview().invalidate();
}

}  // namespace eccdb::detail
