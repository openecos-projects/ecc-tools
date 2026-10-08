// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include "connectivity/ConnectivityService.h"

#include <algorithm>
#include <array>
#include <functional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "design/DesignStore.h"
#include "design/geometry/DesignGeometryStorage.h"
#include "design/guide/storage/DesignGuideStorage.h"
#include "design/netlist/storage/DesignNetlistStorage.h"
#include "design/pin_access/storage/DesignPinAccessStorage.h"
#include "design/routing/storage/DesignRoutingStorage.h"

namespace eccdb {
namespace {

template <typename Id>
void appendPresent(std::vector<Id>& ids, Id id)
{
  if (id) ids.push_back(id);
}

template <typename Id>
void sortAndUnique(std::vector<Id>& ids)
{
  std::sort(ids.begin(), ids.end(), [](Id lhs, Id rhs) { return lhs.packed() < rhs.packed(); });
  ids.erase(std::unique(ids.begin(), ids.end(), [](Id lhs, Id rhs) { return lhs.packed() == rhs.packed(); }), ids.end());
}

template <typename Id, typename Validate>
void validateUniqueIds(std::span<const Id> ids, Validate&& validate, std::string_view description)
{
  std::vector<decltype(Id{}.packed())> values;
  values.reserve(ids.size());
  for (const auto id : ids) {
    std::invoke(validate, id);
    values.push_back(id.packed());
  }
  std::sort(values.begin(), values.end());
  if (std::adjacent_find(values.begin(), values.end()) != values.end()) {
    throw std::invalid_argument(std::string{description} + " contains duplicate IDs");
  }
}

[[nodiscard]] DatabaseChangeDomain connectionDomains(bool block_terms) noexcept
{
  return block_terms ? DatabaseChangeDomain::kConnectivity | DatabaseChangeDomain::kGeometry
                     : DatabaseChangeDomain::kConnectivity;
}

[[nodiscard]] bool isMustJoin(const DesignNetlistStorage& netlist, DesignNetId net)
{
  return (netlist.net(net).flags & DesignNetFlag::kMustJoin) != 0u;
}

void ensureMayDetachFromMustJoin(const DesignNetlistStorage& netlist, DesignNetId source)
{
  if (source && isMustJoin(netlist, source)) {
    throw std::invalid_argument("connectivity service cannot detach the endpoint of a MUSTJOIN net");
  }
}

void ensureMustJoinInstanceTarget(const DesignNetlistStorage& netlist, DesignNetId target)
{
  if (isMustJoin(netlist, target) && !netlist.instanceTerms(target).empty()) {
    throw std::invalid_argument("MUSTJOIN net already has its single instance term");
  }
}

void ensureMustJoinBlockTarget(const DesignNetlistStorage& netlist, DesignNetId target)
{
  if (isMustJoin(netlist, target)) {
    throw std::invalid_argument("MUSTJOIN net cannot connect a block term");
  }
}

void ensureMustJoinReplacement(const DesignNetlistStorage& netlist,
                               DesignNetId target,
                               std::span<const DesignInstanceTermId> instance_terms,
                               std::span<const DesignBlockTermId> block_terms)
{
  if (isMustJoin(netlist, target) && (instance_terms.size() != 1u || !block_terms.empty())) {
    throw std::invalid_argument("MUSTJOIN replacement requires exactly one instance term and no block terms");
  }
}

}  // namespace

ConnectivityService::ConnectivityService(DatabaseContext& context) noexcept : _context(context)
{
}

DesignStore& ConnectivityService::design() const noexcept
{
  return _context.design();
}

void ConnectivityService::ensureNet(DesignNetId net) const
{
  if (!design().netlistStorage().contains(net)) {
    throw std::invalid_argument("connectivity service requires a live design net");
  }
}

void ConnectivityService::ensureInstanceTerm(DesignInstanceTermId term) const
{
  if (!design().netlistStorage().contains(term)) {
    throw std::invalid_argument("connectivity service requires a live design instance term");
  }
}

void ConnectivityService::ensureBlockTerm(DesignBlockTermId term) const
{
  if (!design().netlistStorage().contains(term)) {
    throw std::invalid_argument("connectivity service requires a live design block term");
  }
}

void ConnectivityService::ensureAllowedSources(DesignNetId target, std::span<const DesignNetId> allowed_source_nets) const
{
  if (allowed_source_nets.empty()) {
    return;
  }

  const auto& netlist = design().netlistStorage();
  const bool target_is_special = netlist.isSpecialNet(target);
  for (const auto source : allowed_source_nets) {
    if (!netlist.contains(source) || netlist.isSpecialNet(source) != target_is_special) {
      throw std::invalid_argument("allowed reconnect source has an incompatible net kind");
    }
  }
}

bool ConnectivityService::sourceIsAllowed(DesignNetId source, std::span<const DesignNetId> allowed_source_nets) const noexcept
{
  return allowed_source_nets.empty()
         || std::find(allowed_source_nets.begin(), allowed_source_nets.end(), source) != allowed_source_nets.end();
}

void ConnectivityService::notifyAfter(DatabaseChangeDomain domains,
                                      std::span<const DesignInstanceId> instances,
                                      std::span<const DesignNetId> nets) noexcept
{
  _context.notifyAfter(DatabaseChange{.domains = domains, .instances = instances, .nets = nets});
}

DesignNetId ConnectivityService::createNet(DesignNet net, bool special)
{
  auto& netlist = design().netlistStorage();
  const auto id = special ? netlist.createSpecialNet(std::move(net)) : netlist.createNet(std::move(net));
  const std::array nets{id};
  notifyAfter(DatabaseChangeDomain::kConnectivity, std::span<const DesignInstanceId>{}, nets);
  return id;
}

bool ConnectivityService::deleteNet(DesignNetId net)
{
  if (!design().netlistStorage().contains(net)) {
    return false;
  }
  auto& store = design();
  auto& netlist = store.netlistStorage();
  auto& routing = store.routingStorage();
  auto& geometry = store.geometryStorage();
  auto& guides = store.guideStorage();

  const auto instance_terms = netlist.instanceTerms(net);
  std::vector<DesignInstanceId> changed_instances;
  changed_instances.reserve(instance_terms.size());
  for (const auto term : instance_terms) {
    changed_instances.push_back(netlist.instanceTerm(term).instance);
  }
  sortAndUnique(changed_instances);

  const auto wires = routing.wires(net);
  std::vector<GeometryShapeId> standalone_shapes;
  if (const auto* standalone = routing.netGeometry(net); standalone != nullptr) {
    standalone_shapes = standalone->shapes;
  }
  const bool has_guides = guides.hasGuides(net);
  const bool has_tracks = guides.hasTrackAssignments(net);
  const std::array nets{net};
  const auto domains = DatabaseChangeDomain::kConnectivity | DatabaseChangeDomain::kRouting | DatabaseChangeDomain::kGeometry;
  const DatabaseChange change{.domains = domains, .instances = changed_instances, .nets = nets};

  // The ID is still live here, which lets observers discard net-owned caches
  // before its routes, geometry, and endpoint relations disappear.
  _context.notifyBefore(change);
  netlist.disconnectAll(net);
  for (const auto wire : wires) {
    static_cast<void>(routing.destroyWire(wire));
  }
  for (const auto shape : standalone_shapes) {
    static_cast<void>(geometry.destroy(shape));
  }
  if (has_guides) {
    guides.clearGuides(net);
  }
  if (has_tracks) {
    guides.clearTrackAssignments(net);
  }
  if (!netlist.destroyNet(net)) {
    throw std::logic_error("connectivity service could not remove a prepared design net");
  }
  _context.notifyAfter(change);
  return true;
}

void ConnectivityService::renameNet(DesignNetId net, std::string name)
{
  ensureNet(net);
  auto& netlist = design().netlistStorage();
  auto replacement = netlist.net(net);
  replacement.name = std::move(name);
  netlist.updateNet(net, std::move(replacement));
  const std::array nets{net};
  notifyAfter(DatabaseChangeDomain::kConnectivity, std::span<const DesignInstanceId>{}, nets);
}

void ConnectivityService::setNetProperties(DesignNetId net, DesignNet properties)
{
  ensureNet(net);
  auto& netlist = design().netlistStorage();
  const bool was_must_join = isMustJoin(netlist, net);
  const bool becomes_must_join = (properties.flags & DesignNetFlag::kMustJoin) != 0u;
  if (!was_must_join && becomes_must_join
      && (netlist.isSpecialNet(net) || netlist.instanceTerms(net).size() != 1u || !netlist.blockTerms(net).empty())) {
    throw std::invalid_argument("MUSTJOIN net requires exactly one instance term and no block terms");
  }
  netlist.updateNet(net, std::move(properties));
  const std::array nets{net};
  notifyAfter(DatabaseChangeDomain::kConnectivity | DatabaseChangeDomain::kGeometry,
              std::span<const DesignInstanceId>{}, nets);
}

void ConnectivityService::setNetOptions(DesignNetId net, DesignNetOptions options)
{
  ensureNet(net);
  design().netlistStorage().setNetOptions(net, std::move(options));
  const std::array nets{net};
  notifyAfter(DatabaseChangeDomain::kRouting | DatabaseChangeDomain::kGeometry,
              std::span<const DesignInstanceId>{}, nets);
}

void ConnectivityService::connect(DesignInstanceTermId term,
                                  DesignNetId net,
                                  std::span<const DesignNetId> allowed_source_nets)
{
  ensureInstanceTerm(term);
  ensureNet(net);

  auto& netlist = design().netlistStorage();
  const bool special = netlist.isSpecialNet(net);
  const auto& value = netlist.instanceTerm(term);
  if ((special ? value.special_net : value.net) == net) {
    return;
  }

  ensureAllowedSources(net, allowed_source_nets);
  const auto source = special ? value.special_net : value.net;
  if (source && source != net && !sourceIsAllowed(source, allowed_source_nets)) {
    throw std::invalid_argument("instance term reconnect source is outside the allowed scope");
  }
  if (source && source != net) {
    ensureMayDetachFromMustJoin(netlist, source);
  }
  ensureMustJoinInstanceTarget(netlist, net);
  const auto instance = value.instance;
  const std::array instances{instance};
  const std::array changed_nets{source, net};
  const auto changed_net_span = source ? std::span<const DesignNetId>{changed_nets}
                                       : std::span<const DesignNetId>{changed_nets}.subspan(1u);

  // Reserve before detaching source. The unchecked Storage connect then only
  // appends into already-capacious target storage, so this path stays O(1)
  // instead of copying every endpoint on a high-degree Net.
  auto& registry = design().designRegistry().registry();
  auto& target = registry.get_or_emplace<DesignNetInstancePins>(net.entity()).values;
  target.reserve(target.size() + 1u);
  if (source && source != net) {
    netlist.disconnect(term, source);
  }
  netlist.connect(term, net);
  notifyAfter(DatabaseChangeDomain::kConnectivity, instances, changed_net_span);
}

void ConnectivityService::connect(DesignBlockTermId term,
                                  DesignNetId net,
                                  std::span<const DesignNetId> allowed_source_nets)
{
  ensureBlockTerm(term);
  ensureNet(net);

  auto& netlist = design().netlistStorage();
  const bool special = netlist.isSpecialNet(net);
  const auto& value = netlist.blockTerm(term);
  if ((special ? value.special_net : value.net) == net) {
    return;
  }

  ensureAllowedSources(net, allowed_source_nets);
  const auto source = special ? value.special_net : value.net;
  if (source && source != net && !sourceIsAllowed(source, allowed_source_nets)) {
    throw std::invalid_argument("block term reconnect source is outside the allowed scope");
  }
  if (source && source != net) {
    ensureMayDetachFromMustJoin(netlist, source);
  }
  ensureMustJoinBlockTarget(netlist, net);
  const std::array changed_nets{source, net};
  const auto changed_net_span = source ? std::span<const DesignNetId>{changed_nets}
                                       : std::span<const DesignNetId>{changed_nets}.subspan(1u);

  auto& registry = design().designRegistry().registry();
  auto& target = registry.get_or_emplace<DesignNetIoPins>(net.entity()).values;
  target.reserve(target.size() + 1u);
  if (source && source != net) {
    netlist.disconnect(term, source);
  }
  netlist.connect(term, net);
  notifyAfter(connectionDomains(true), std::span<const DesignInstanceId>{}, changed_net_span);
}

void ConnectivityService::reconnect(DesignInstanceTermId term,
                                    DesignNetId net,
                                    std::span<const DesignNetId> allowed_source_nets)
{
  connect(term, net, allowed_source_nets);
}

void ConnectivityService::reconnect(DesignBlockTermId term,
                                    DesignNetId net,
                                    std::span<const DesignNetId> allowed_source_nets)
{
  connect(term, net, allowed_source_nets);
}

void ConnectivityService::disconnect(DesignInstanceTermId term, DesignNetId net)
{
  ensureInstanceTerm(term);
  ensureNet(net);
  auto& netlist = design().netlistStorage();
  const bool special = netlist.isSpecialNet(net);
  const auto& value = netlist.instanceTerm(term);
  if ((special ? value.special_net : value.net) != net) {
    throw std::invalid_argument("instance term is not connected to the requested net");
  }
  ensureMayDetachFromMustJoin(netlist, net);

  const std::array instances{value.instance};
  const std::array nets{net};
  netlist.disconnect(term, net);
  notifyAfter(DatabaseChangeDomain::kConnectivity, instances, nets);
}

void ConnectivityService::disconnect(DesignBlockTermId term, DesignNetId net)
{
  ensureBlockTerm(term);
  ensureNet(net);
  auto& netlist = design().netlistStorage();
  const bool special = netlist.isSpecialNet(net);
  const auto& value = netlist.blockTerm(term);
  if ((special ? value.special_net : value.net) != net) {
    throw std::invalid_argument("block term is not connected to the requested net");
  }
  ensureMayDetachFromMustJoin(netlist, net);

  const std::array nets{net};
  netlist.disconnect(term, net);
  notifyAfter(connectionDomains(true), {}, nets);
}

void ConnectivityService::disconnect(DesignInstanceTermId term)
{
  ensureInstanceTerm(term);
  auto& netlist = design().netlistStorage();
  const auto& value = netlist.instanceTerm(term);
  std::vector<DesignNetId> nets;
  nets.reserve(2u);
  appendPresent(nets, value.net);
  appendPresent(nets, value.special_net);
  sortAndUnique(nets);
  if (nets.empty()) {
    return;
  }
  ensureMayDetachFromMustJoin(netlist, value.net);

  const std::array instances{value.instance};
  netlist.disconnect(term);
  notifyAfter(DatabaseChangeDomain::kConnectivity, instances, nets);
}

void ConnectivityService::disconnect(DesignBlockTermId term)
{
  ensureBlockTerm(term);
  auto& netlist = design().netlistStorage();
  const auto& value = netlist.blockTerm(term);
  std::vector<DesignNetId> nets;
  nets.reserve(2u);
  appendPresent(nets, value.net);
  appendPresent(nets, value.special_net);
  sortAndUnique(nets);
  if (nets.empty()) {
    return;
  }
  ensureMayDetachFromMustJoin(netlist, value.net);

  netlist.disconnect(term);
  notifyAfter(connectionDomains(true), {}, nets);
}

void ConnectivityService::disconnectAll(DesignNetId net)
{
  ensureNet(net);
  auto& netlist = design().netlistStorage();
  const auto instance_terms = netlist.instanceTerms(net);
  const auto block_terms = netlist.blockTerms(net);
  if (instance_terms.empty() && block_terms.empty()) {
    return;
  }
  ensureMayDetachFromMustJoin(netlist, net);

  std::vector<DesignInstanceId> instances;
  instances.reserve(instance_terms.size());
  for (const auto term : instance_terms) {
    instances.push_back(netlist.instanceTerm(term).instance);
  }
  sortAndUnique(instances);
  const std::array nets{net};
  netlist.disconnectAll(net);
  notifyAfter(connectionDomains(!block_terms.empty()), instances, nets);
}

void ConnectivityService::replaceNetConnections(DesignNetId net,
                                                 std::span<const DesignInstanceTermId> instance_terms,
                                                 std::span<const DesignBlockTermId> block_terms,
                                                 std::span<const DesignNetId> allowed_source_nets)
{
  ensureNet(net);
  ensureAllowedSources(net, allowed_source_nets);
  auto& netlist = design().netlistStorage();
  validateUniqueIds<DesignInstanceTermId>(instance_terms, [this](DesignInstanceTermId term) { ensureInstanceTerm(term); },
                                           "replacement instance-term list");
  validateUniqueIds<DesignBlockTermId>(block_terms, [this](DesignBlockTermId term) { ensureBlockTerm(term); },
                                        "replacement block-term list");

  const bool special = netlist.isSpecialNet(net);
  ensureMustJoinReplacement(netlist, net, instance_terms, block_terms);
  const auto current_instance_terms = netlist.instanceTerms(net);
  const auto current_block_terms = netlist.blockTerms(net);
  const bool same_instance_terms = current_instance_terms.size() == instance_terms.size()
                                   && std::equal(current_instance_terms.begin(), current_instance_terms.end(), instance_terms.begin());
  const bool same_block_terms = current_block_terms.size() == block_terms.size()
                                && std::equal(current_block_terms.begin(), current_block_terms.end(), block_terms.begin());

  std::vector<DesignNetId> changed_nets;
  changed_nets.reserve(1u + instance_terms.size() + block_terms.size());
  changed_nets.push_back(net);
  std::vector<DesignInstanceId> changed_instances;
  changed_instances.reserve(current_instance_terms.size() + instance_terms.size());
  for (const auto term : current_instance_terms) {
    changed_instances.push_back(netlist.instanceTerm(term).instance);
  }
  for (const auto term : instance_terms) {
    const auto& value = netlist.instanceTerm(term);
    changed_instances.push_back(value.instance);
    const auto source = special ? value.special_net : value.net;
    if (source && source != net) {
      if (!sourceIsAllowed(source, allowed_source_nets)) {
        throw std::invalid_argument("instance term reconnect source is outside the allowed scope");
      }
      ensureMayDetachFromMustJoin(netlist, source);
      changed_nets.push_back(source);
    }
  }
  for (const auto term : block_terms) {
    const auto& value = netlist.blockTerm(term);
    const auto source = special ? value.special_net : value.net;
    if (source && source != net) {
      if (!sourceIsAllowed(source, allowed_source_nets)) {
        throw std::invalid_argument("block term reconnect source is outside the allowed scope");
      }
      ensureMayDetachFromMustJoin(netlist, source);
      changed_nets.push_back(source);
    }
  }
  sortAndUnique(changed_nets);
  sortAndUnique(changed_instances);

  if (same_instance_terms && same_block_terms && changed_nets.size() == 1u) {
    return;
  }

  // Storage prepares its target/source vectors before its no-allocation
  // cross-source commit. Do not pre-disconnect here: that would expose a
  // partially detached design if its preparation failed.
  netlist.replaceConnections(net, instance_terms, block_terms);
  notifyAfter(connectionDomains(!current_block_terms.empty() || !block_terms.empty()), changed_instances, changed_nets);
}

DesignBlockTermId ConnectivityService::createBlockTerm(DesignBlockTerm term, std::vector<DesignBlockPinInput> pins)
{
  auto& netlist = design().netlistStorage();
  const bool has_geometry = !pins.empty();
  const auto id = netlist.createBlockTerm(std::move(term), std::move(pins));
  notifyAfter(has_geometry ? DatabaseChangeDomain::kConnectivity | DatabaseChangeDomain::kGeometry
                           : DatabaseChangeDomain::kConnectivity,
              std::span<const DesignInstanceId>{}, std::span<const DesignNetId>{});
  return id;
}

bool ConnectivityService::deleteBlockTerm(DesignBlockTermId term)
{
  if (!design().netlistStorage().contains(term)) {
    return false;
  }
  auto& store = design();
  auto& netlist = store.netlistStorage();
  auto& pin_access = store.pinAccessStorage();
  const auto& value = netlist.blockTerm(term);
  const auto pins = netlist.blockPins(term);
  std::vector<DesignNetId> nets;
  nets.reserve(2u);
  appendPresent(nets, value.net);
  appendPresent(nets, value.special_net);
  sortAndUnique(nets);
  const auto domains = DatabaseChangeDomain::kConnectivity | DatabaseChangeDomain::kGeometry | DatabaseChangeDomain::kPinAccess;
  const DatabaseChange change{.domains = domains, .instances = {}, .nets = nets};

  _context.notifyBefore(change);
  for (const auto pin : pins) {
    static_cast<void>(pin_access.clearBlockPinCandidateGroups(pin));
  }
  netlist.disconnect(term);
  if (!netlist.destroyBlockTerm(term)) {
    throw std::logic_error("connectivity service could not remove a prepared design block term");
  }
  _context.notifyAfter(change);
  return true;
}

void ConnectivityService::renameBlockTerm(DesignBlockTermId term, std::string name)
{
  ensureBlockTerm(term);
  auto& netlist = design().netlistStorage();
  auto replacement = netlist.blockTerm(term);
  replacement.name = std::move(name);
  netlist.updateBlockTerm(term, std::move(replacement));
  const auto& value = netlist.blockTerm(term);
  std::vector<DesignNetId> nets;
  nets.reserve(2u);
  appendPresent(nets, value.net);
  appendPresent(nets, value.special_net);
  sortAndUnique(nets);
  notifyAfter(DatabaseChangeDomain::kConnectivity, std::span<const DesignInstanceId>{}, nets);
}

}  // namespace eccdb
