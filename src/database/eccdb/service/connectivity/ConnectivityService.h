// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <span>
#include <string>
#include <vector>

#include "context/DatabaseContext.h"
#include "design/netlist/model/NetlistComponents.h"

namespace eccdb {

class DesignStore;

// Coordinates graph-like design connectivity changes that cross the netlist,
// routing, geometry, guide, and pin-access stores. Read-only traversal stays
// on the native Storage APIs; this service owns only multi-object mutations.
class ConnectivityService
{
 public:
  explicit ConnectivityService(DatabaseContext& context) noexcept;

  // A MUSTJOIN Net may be created without an endpoint while it is being
  // assembled. All subsequent public connectivity edits preserve its DEF
  // invariant: exactly one regular instance Term and no block Term.
  [[nodiscard]] DesignNetId createNet(DesignNet net, bool special = false);
  [[nodiscard]] bool deleteNet(DesignNetId net);
  void renameNet(DesignNetId net, std::string name);
  void setNetProperties(DesignNetId net, DesignNet properties);
  void setNetOptions(DesignNetId net, DesignNetOptions options);
  void setNetAliases(DesignNetId net, DesignNetAliases aliases);

  // Connect/reconnect selects the relation matching the target Net kind.
  // A regular and a special relation can therefore coexist on one Term. A
  // non-empty allowed_source_nets list restricts which current same-kind Net
  // may be replaced; it is intended for scoped CTS reconnects. These calls
  // enforce MUSTJOIN's one-instance-term/no-block-term boundary before using
  // the unchecked Storage connect primitive.
  void connect(DesignInstanceTermId term,
               DesignNetId net,
               std::span<const DesignNetId> allowed_source_nets = {});
  void connect(DesignBlockTermId term,
               DesignNetId net,
               std::span<const DesignNetId> allowed_source_nets = {});
  void reconnect(DesignInstanceTermId term,
                 DesignNetId net,
                 std::span<const DesignNetId> allowed_source_nets = {});
  void reconnect(DesignBlockTermId term,
                 DesignNetId net,
                 std::span<const DesignNetId> allowed_source_nets = {});

  // The target-Net overload detaches only the matching regular/special
  // relation. The one-argument overload removes both relations on a Term.
  void disconnect(DesignInstanceTermId term, DesignNetId net);
  void disconnect(DesignBlockTermId term, DesignNetId net);
  void disconnect(DesignInstanceTermId term);
  void disconnect(DesignBlockTermId term);

  // Removes every same-kind relation owned by net. Routing and geometry stay
  // intact; deleteNet is the operation that also deletes Net-owned objects.
  void disconnectAll(DesignNetId net);

  // Replaces the complete endpoint set for one regular or special Net.
  // Input terms may currently belong to another Net of the same kind only
  // when that Net is allowed by allowed_source_nets. Other-kind relations are
  // not touched. A MUSTJOIN replacement must contain exactly one instance
  // Term and no block Terms.
  void replaceNetConnections(DesignNetId net,
                             std::span<const DesignInstanceTermId> instance_terms,
                             std::span<const DesignBlockTermId> block_terms,
                             std::span<const DesignNetId> allowed_source_nets = {});

  [[nodiscard]] DesignBlockTermId createBlockTerm(DesignBlockTerm term,
                                                    std::vector<DesignBlockPinInput> pins = {});
  [[nodiscard]] bool deleteBlockTerm(DesignBlockTermId term);
  void renameBlockTerm(DesignBlockTermId term, std::string name);

 private:
  [[nodiscard]] DesignStore& design() const noexcept;
  void ensureNet(DesignNetId net) const;
  void ensureInstanceTerm(DesignInstanceTermId term) const;
  void ensureBlockTerm(DesignBlockTermId term) const;
  void ensureAllowedSources(DesignNetId target, std::span<const DesignNetId> allowed_source_nets) const;
  [[nodiscard]] bool sourceIsAllowed(DesignNetId source, std::span<const DesignNetId> allowed_source_nets) const noexcept;

  void notifyAfter(DatabaseChangeDomain domains,
                   std::span<const DesignInstanceId> instances,
                   std::span<const DesignNetId> nets) noexcept;

  DatabaseContext& _context;
};

}  // namespace eccdb
