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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file PlotSpefVisibility.hh
 * @brief Per-plot visibility data separated from immutable plot_spef geometry.
 */
#pragma once

#include <cstdint>
#include <vector>

#include "model/PlotSpefModel.hh"

namespace ircx::plot_spef {

using VisibilityFlags = std::vector<std::uint8_t>;

inline auto flagAt(const VisibilityFlags& flags,
                   Size index) -> bool
{
  return index < flags.size() && flags[index] != 0;
}

inline auto setFlag(VisibilityFlags& flags,
                    Size index,
                    bool value = true) -> void
{
  if (index < flags.size()) {
    flags[index] = value ? 1 : 0;
  }
}

struct NetVisibility
{
  bool visible = false;
  bool context_only = true;
  VisibilityFlags nodes;
  VisibilityFlags resistors;
  VisibilityFlags target_resistors;
  VisibilityFlags coupling_caps;
  VisibilityFlags ground_caps;

  auto nodeVisible(Size index) const -> bool { return flagAt(nodes, index); }
  auto resistorVisible(Size index) const -> bool { return flagAt(resistors, index); }
  auto resistorTarget(Size index) const -> bool { return flagAt(target_resistors, index); }
  auto couplingCapVisible(Size index) const -> bool { return flagAt(coupling_caps, index); }
  auto groundCapVisible(Size index) const -> bool { return flagAt(ground_caps, index); }
};

struct Visibility
{
  std::vector<NetVisibility> nets;

  auto netVisible(Size index) const -> bool
  {
    return index < nets.size() && nets[index].visible;
  }

  auto netContextOnly(Size index) const -> bool
  {
    return index < nets.size() && nets[index].context_only;
  }
};

inline auto makeVisibility(const Model& model,
                           bool visible) -> Visibility
{
  Visibility visibility;
  visibility.nets.reserve(model.nets.size());
  for (const auto& net : model.nets) {
    visibility.nets.push_back(NetVisibility{
        .visible = visible,
        .context_only = !visible,
        .nodes = VisibilityFlags(net.nodes.size(), visible ? 1 : 0),
        .resistors = VisibilityFlags(net.resistors.size(), visible ? 1 : 0),
        .target_resistors = VisibilityFlags(net.resistors.size(), 0),
        .coupling_caps = VisibilityFlags(net.coupling_caps.size(), visible ? 1 : 0),
        .ground_caps = VisibilityFlags(net.ground_caps.size(), visible ? 1 : 0)});
  }
  return visibility;
}

}  // namespace ircx::plot_spef
