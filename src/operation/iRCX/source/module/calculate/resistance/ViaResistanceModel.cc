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
#include "ViaResistanceModel.hh"

#include <optional>

#include "Geometry.hh"
#include "ProcessCorner.hpp"
#include "ResistanceTemperature.hh"
#include "TopoPool.hh"

namespace ircx {

namespace {

auto get_via_temperature_coefficients(const itf::LayerVia& layer, F64 area) -> ResistanceTemperatureCoefficients
{
  ResistanceTemperatureCoefficients coefficients;

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

auto ViaResistanceModel::calc(const TopoEdge& edge, const itf::ProcessCorner& corner, const itf::LayerVia& layer, Micron dbu_to_micron,
                              F64 operating_temperature) -> F64
{
  const F64 via_area = geom::area(edge.shape()) * dbu_to_micron * dbu_to_micron;

  F64 via_resistance = 0.0;
  if (auto rpv = layer.get_rpv()) {
    via_resistance = rpv.value();
  } else {
    via_resistance = layer.query_rpv_vs_area(via_area);
  }

  return applyResistanceTemperatureDerating(via_resistance, operating_temperature, resistanceNominalTemperature(layer, corner),
                                            get_via_temperature_coefficients(layer, via_area));
}

}  // namespace ircx
