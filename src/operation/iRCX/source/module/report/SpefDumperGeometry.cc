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
#include "SpefDumper.hh"

#include <cstdlib>
#include <iomanip>
#include <sstream>

#include "Geometry.hh"
#include "RCXConfig.hh"
#include "TopoPool.hh"

namespace ircx {

namespace {

auto formatMicron(Micron value, int precision) -> Str
{
  std::ostringstream ss;
  ss << std::fixed << std::setprecision(precision) << value;
  return ss.str();
}

}  // namespace

void SpefDumper::writeNodeGeometry(std::ostream& os, const TopoNode& node, Micron dbu_to_micron) const
{
  if (!RCX_CONFIG_INST.get_report_geometry()) {
    return;
  }

  const GtlRectI& rect = node.shape();
  os << " // $llx=" << formatMicron(geom::min_x(rect) * dbu_to_micron, 3)
     << " $lly=" << formatMicron(geom::min_y(rect) * dbu_to_micron, 3)
     << " $urx=" << formatMicron(geom::max_x(rect) * dbu_to_micron, 3)
     << " $ury=" << formatMicron(geom::max_y(rect) * dbu_to_micron, 3)
     << " $lvl=" << reportLayerLevel(node.layer_id());
}

void SpefDumper::writeResistanceGeometry(std::ostream& os, Size corner_idx, const TopoEdge& edge, Micron dbu_to_micron) const
{
  if (!RCX_CONFIG_INST.get_report_geometry()) {
    return;
  }

  const GtlRectI& rect = edge.shape();
  const Dbu dx = geom::delta_x(rect);
  const Dbu dy = geom::delta_y(rect);

  os << " // ";
  if (edge.is_via()) {
    const Micron area = geom::area(rect) * dbu_to_micron * dbu_to_micron;
    os << " $a=" << formatMicron(area, 6)
       << " $lvl=" << reportLayerLevel(edge.layer_id())
       << " $llx=" << formatMicron(geom::min_x(rect) * dbu_to_micron, 3)
       << " $lly=" << formatMicron(geom::min_y(rect) * dbu_to_micron, 3)
       << " $urx=" << formatMicron(geom::max_x(rect) * dbu_to_micron, 3)
       << " $ury=" << formatMicron(geom::max_y(rect) * dbu_to_micron, 3);
    return;
  }

  const TopoNode& node_u = topo_pool_->node_at(edge.u());
  const TopoNode& node_v = topo_pool_->node_at(edge.v());
  const Dbu node_dx = std::abs(geom::x(node_u.point()) - geom::x(node_v.point()));
  const Dbu node_dy = std::abs(geom::y(node_u.point()) - geom::y(node_v.point()));
  const bool is_horz = (node_dx == 0 && node_dy == 0) ? edge.is_horz() : (node_dx >= node_dy);
  const Dbu axis_distance = is_horz ? node_dx : node_dy;
  const Dbu shape_axis_distance = is_horz ? dx : dy;
  const Dbu shape_width = is_horz ? dy : dx;
  const Micron length = ((axis_distance > 0 ? axis_distance : shape_axis_distance) * dbu_to_micron)
                        * (*corner_data_)[corner_idx].halfNodeScaleFactor();
  const Micron width = shape_width * dbu_to_micron;
  const bool virtual_overlap_edge = (node_dx == 0 && node_dy == 0) || dx == 0 || dy == 0;

  os << " $l=" << formatMicron(length, 3)
     << " $w=" << formatMicron(width, 3)
     << " $lvl=" << reportLayerLevel(edge.layer_id());

  if (virtual_overlap_edge) {
    return;
  }

  os << " $llx=" << formatMicron(geom::min_x(rect) * dbu_to_micron, 3)
     << " $lly=" << formatMicron(geom::min_y(rect) * dbu_to_micron, 3)
     << " $urx=" << formatMicron(geom::max_x(rect) * dbu_to_micron, 3)
     << " $ury=" << formatMicron(geom::max_y(rect) * dbu_to_micron, 3)
     << " $dir=" << (is_horz ? 0 : 1);
}

Size SpefDumper::reportLayerLevel(Size design_layer_id) const
{
  auto it = design_to_report_layer_level_.find(design_layer_id);
  if (it != design_to_report_layer_level_.end()) {
    return it->second;
  }
  return design_layer_id == kMaxSize ? 0 : design_layer_id;
}

}  // namespace ircx
