// ***************************************************************************************
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
/**
 * @file PlotSpefCapToEdge.cc
 * @brief Assign capacitors to edges only when the owner edge is unique.
 */
#include "cap/PlotSpefCapToEdge.hh"

#include "RCXHeader.hpp"
#include "model/PlotSpefModel.hh"

namespace ircx::plot_spef {
namespace {

struct EdgeCandidate
{
  EdgeRef ref;
  const Resistor* edge = nullptr;
};

using IncidentEdges = std::unordered_map<std::string, std::vector<EdgeCandidate>>;

class IncidentEdgeIndex
{
 public:
  explicit IncidentEdgeIndex(const Model& model)
  {
    for (Size net_index = 0; net_index < model.nets.size(); ++net_index) {
      const auto& net = model.nets[net_index];
      for (Size resistor_index = 0; resistor_index < net.resistors.size(); ++resistor_index) {
        const auto& resistor = net.resistors[resistor_index];
        EdgeCandidate candidate;
        candidate.ref = {.net_index = net_index, .resistor_index = resistor_index, .valid = true};
        candidate.edge = &resistor;
        incident_edges_[resistor.node1].push_back(candidate);
        incident_edges_[resistor.node2].push_back(candidate);
      }
    }
  }

  auto candidatesFor(const std::string& node_name) const -> const std::vector<EdgeCandidate>&
  {
    const auto it = incident_edges_.find(node_name);
    return it == incident_edges_.end() ? empty_candidates_ : it->second;
  }

 private:
  IncidentEdges incident_edges_;
  std::vector<EdgeCandidate> empty_candidates_;
};

auto sameEdgeRef(const EdgeRef& lhs, const EdgeRef& rhs) -> bool
{
  return lhs.valid == rhs.valid && lhs.net_index == rhs.net_index && lhs.resistor_index == rhs.resistor_index;
}

auto uniqueEdgeCandidate(const std::vector<EdgeCandidate>& candidates) -> EdgeRef
{
  EdgeRef unique;
  for (const auto& candidate : candidates) {
    if (!candidate.ref.valid || candidate.edge == nullptr) {
      continue;
    }
    if (!unique.valid) {
      unique = candidate.ref;
      continue;
    }
    if (!sameEdgeRef(unique, candidate.ref)) {
      return {};
    }
  }
  return unique;
}

class CapToEdge
{
 public:
  explicit CapToEdge(const IncidentEdgeIndex& incident_edges) : incident_edges_(incident_edges) {}

  auto resolve(Model& model) const -> void
  {
    for (auto& net : model.nets) {
      for (auto& cap : net.coupling_caps) {
        resolveCouplingCap(cap);
      }
      for (auto& cap : net.ground_caps) {
        resolveGroundCap(cap);
      }
    }
  }

 private:
  auto resolveGroundCap(Capacitor& cap) const -> void
  {
    if (cap.edge1.valid) {
      return;
    }
    cap.edge1 = uniqueEdgeCandidate(incident_edges_.candidatesFor(cap.node1));
  }

  auto resolveCouplingCap(Capacitor& cap) const -> void
  {
    if (cap.edge1.valid && cap.edge2.valid) {
      return;
    }
    cap.edge1 = cap.edge1.valid ? cap.edge1 : uniqueEdgeCandidate(incident_edges_.candidatesFor(cap.node1));
    cap.edge2 = cap.edge2.valid ? cap.edge2 : uniqueEdgeCandidate(incident_edges_.candidatesFor(cap.node2));
  }

  const IncidentEdgeIndex& incident_edges_;
};

}  // namespace

auto assignCapEdges(Model& model) -> void
{
  const IncidentEdgeIndex incident_edges(model);
  CapToEdge(incident_edges).resolve(model);
}

}  // namespace ircx::plot_spef
