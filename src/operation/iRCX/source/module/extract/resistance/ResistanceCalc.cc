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
#include "ResistanceCalc.hh"

#include <algorithm>
#include <optional>

#include "LayerTable.hh"
#include "LayoutData.hh"
#include "TopoPool.hh"
#include "ProcessCorner.hpp"
#include "ResistanceTemperature.hh"
#include "log/Log.hh"
namespace ircx {

namespace {

struct TemperatureCoefficients
{
  F64 crt1 = 0.0;
  F64 crt2 = 0.0;
};

TemperatureCoefficients get_conductor_temperature_coefficients(
    const itf::LayerConductor& layer,
    Micron width)
{
  TemperatureCoefficients coefficients;

  std::optional<double> width_crt1;
  std::optional<double> width_crt2;
  layer.query_crt_vs_si_width(width, width_crt1, width_crt2);

  if (width_crt1.has_value()) {
    coefficients.crt1 = width_crt1.value();
  } else if (auto crt1 = layer.get_crt1()) {
    coefficients.crt1 = crt1.value();
  }

  if (width_crt2.has_value()) {
    coefficients.crt2 = width_crt2.value();
  } else if (auto crt2 = layer.get_crt2()) {
    coefficients.crt2 = crt2.value();
  }

  return coefficients;
}

TemperatureCoefficients get_via_temperature_coefficients(
    const itf::LayerVia& layer,
    F64 area)
{
  TemperatureCoefficients coefficients;

  std::optional<double> area_crt1;
  std::optional<double> area_crt2;
  layer.query_crt_vs_area(area, area_crt1, area_crt2);

  if (area_crt1.has_value()) {
    coefficients.crt1 = area_crt1.value();
  } else if (auto crt1 = layer.get_crt1()) {
    coefficients.crt1 = crt1.value();
  }

  if (area_crt2.has_value()) {
    coefficients.crt2 = area_crt2.value();
  } else if (auto crt2 = layer.get_crt2()) {
    coefficients.crt2 = crt2.value();
  }

  return coefficients;
}

}  // namespace

bool ResistanceCalc::calc()
{
  if (layout_data_ == nullptr) {
    LOG_ERROR << "calculate resistance failed: LayoutData not initialized.";
    return false;
  }
  if (topo_pool_ == nullptr) {
    LOG_ERROR << "calculate resistance failed: TopoPool not initialized.";
    return false;
  }
  if (layer_table_ == nullptr) {
    LOG_ERROR << "calculate resistance failed: LayerTable not initialized.";
    return false;
  }
  if (corner_net_etch_pools_ == nullptr) {
    LOG_ERROR << "calculate resistance failed: etch pools not set.";
    return false;
  }
  if (rc_table_ == nullptr) {
    LOG_ERROR << "calculate resistance failed: RC table not set.";
    return false;
  }
  if (corners_.empty()) {
    LOG_ERROR << "calculate resistance failed: process corners not set.";
    return false;
  }

  const Size regular_net_count = layout_data_->regular_net_count();
  const Size corner_count = corners_.size();

  for (Size corner_idx = 0; corner_idx < corner_count; corner_idx++) {
    #pragma omp parallel for schedule(dynamic)
    for (Size net_idx = 0; net_idx < regular_net_count; net_idx++) {
      const auto net_edges = topo_pool_->net_edges(net_idx);
      const Size edge_count = net_edges.size();
      auto edge_resistances = rc_table_->corner_net_res_pool(corner_idx, net_idx);
      const EtchPool& corner_net_etch_pool =
          (*corner_net_etch_pools_)[corner_idx * regular_net_count + net_idx];
      for (Size edge_idx = 0; edge_idx < edge_count; edge_idx++) {
        const TopoEdge& edge = net_edges[edge_idx];
        const Size process_layer_id = layer_table_->design_to_process_id(edge.layer_id());

        if (edge.is_via()) {
          auto* via_layer = corners_[corner_idx]->get_layers()->find_via_layer(process_layer_id);

          F64 via_resistance = 0.0;
          const F64 via_area = geom::Area(edge.shape()) * dbu_to_micron_ * dbu_to_micron_;
          if (auto rpv = via_layer->get_rpv()) {
            via_resistance = rpv.value();
          } else {
            via_resistance = via_layer->query_rpv_vs_area(via_area);
          }
          edge_resistances[edge_idx] = apply_via_temperature_derating(
              *corners_[corner_idx], *via_layer, via_area, via_resistance);
          continue; // via res
        }

        auto* conductor_layer = corners_[corner_idx]->get_layers()->find_conductor_layer(process_layer_id);

        auto [segment_lo, segment_hi] = node_range(edge);
        const auto edge_etch_intervals = corner_net_etch_pool.edge_etch_interval_pool(edge_idx);
        F64 resistance = 0.0;
        for (const EtchInterval& etch_interval : edge_etch_intervals) {
          // Overlap of this etch interval with the u->v segment.
          const Micron overlap_lo = std::max(etch_interval.a0, segment_lo);
          const Micron overlap_hi = std::min(etch_interval.a1, segment_hi);
          if (overlap_hi <= overlap_lo) continue;

          const Micron overlap_length = overlap_hi - overlap_lo;

          // Modeled thickness: use value from ThicknessModel; fall back to nominal.
          const Micron thickness = etch_interval.thickness;
          const Micron width = etch_interval.width;
          LOG_ERROR_IF(width <= 0.0 || thickness <= 0.0) << "etch interval width/thickness <= 0.";

          // ρ (Ω·μm): look up from RHO_VS_SI_WIDTH_AND_THICKNESS (row=T, col=W).
          float resistivity = 0.0;
          {
            auto rho_opt = conductor_layer->get_rho_v_siw_t().query_interpolation(
                static_cast<float>(thickness), static_cast<float>(width));
            if (rho_opt.has_value()) {
              resistivity = rho_opt.value();
            } else {
              resistivity = conductor_layer->get_rho();
            }
          }

          float sheet_resistance = 0.0;
          if (resistivity <= 0.0) {
            // RPSQ path (sheet resistance): R = Rₛ × L / W
            // Used when the conductor layer only specifies RPSQ (e.g. poly, diffusion).
            auto rpsq_opt = conductor_layer->get_rpsq_vs_si_width().query_interpolation(
                static_cast<float>(width));
            if (rpsq_opt.has_value()) {
              sheet_resistance = rpsq_opt.value();
            } else {
              sheet_resistance = conductor_layer->get_rpsq();
            }
          }

          F64 interval_resistance = 0.0;
          if (resistivity > 0.0) {
            interval_resistance += resistivity * overlap_length / (width * thickness);
          }
          if (sheet_resistance > 0.0) {
            interval_resistance += sheet_resistance * overlap_length / width;
          }
          resistance += apply_conductor_temperature_derating(
              *corners_[corner_idx], *conductor_layer, width, interval_resistance);
        }
        edge_resistances[edge_idx] = resistance;
      }
      // results already written into pre-allocated span
    }
  }

  return true;
}

std::pair<Micron, Micron> ResistanceCalc::node_range(const TopoEdge& e) const
{
  const TopoNode& u_node = topo_pool_->node_at(e.u());
  const TopoNode& v_node = topo_pool_->node_at(e.v());

  Micron u_pos, v_pos;
  if (e.is_horz()) {
    u_pos = geom::X(u_node.point()) * dbu_to_micron_;
    v_pos = geom::X(v_node.point()) * dbu_to_micron_;
  } else {
    u_pos = geom::Y(u_node.point()) * dbu_to_micron_;
    v_pos = geom::Y(v_node.point()) * dbu_to_micron_;
  }
  return {u_pos, v_pos};
}

F64 ResistanceCalc::apply_conductor_temperature_derating(
    const itf::ProcessCorner& corner,
    const itf::LayerConductor& layer,
    Micron width,
    F64 base_resistance) const
{
  const TemperatureCoefficients coefficients =
      get_conductor_temperature_coefficients(layer, width);
  if (coefficients.crt1 == 0.0 && coefficients.crt2 == 0.0) {
    return base_resistance;
  }

  const F64 nominal_temperature = layer.has_t0()
                                      ? static_cast<F64>(layer.get_t0())
                                      : static_cast<F64>(corner.get_global_temperature());
  return applyResistanceTemperatureDerating(base_resistance, operating_temperature_,
                                            nominal_temperature, coefficients.crt1,
                                            coefficients.crt2);
}

F64 ResistanceCalc::apply_via_temperature_derating(
    const itf::ProcessCorner& corner,
    const itf::LayerVia& layer,
    F64 area,
    F64 base_resistance) const
{
  const TemperatureCoefficients coefficients =
      get_via_temperature_coefficients(layer, area);
  if (coefficients.crt1 == 0.0 && coefficients.crt2 == 0.0) {
    return base_resistance;
  }

  const F64 nominal_temperature = layer.has_t0()
                                      ? static_cast<F64>(layer.get_t0())
                                      : static_cast<F64>(corner.get_global_temperature());
  return applyResistanceTemperatureDerating(base_resistance, operating_temperature_,
                                            nominal_temperature, coefficients.crt1,
                                            coefficients.crt2);
}

}
