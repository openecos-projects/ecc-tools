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
#include "WireResistanceModel.hh"

#include <algorithm>
#include <optional>

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

auto get_conductor_temperature_coefficients(
    const itf::LayerConductor& layer,
    Micron width) -> TemperatureCoefficients
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

auto apply_conductor_temperature_derating(F64 base_resistance,
                                          F64 operating_temperature,
                                          const itf::ProcessCorner& corner,
                                          const itf::LayerConductor& layer,
                                          Micron width) -> F64
{
  const TemperatureCoefficients coefficients =
      get_conductor_temperature_coefficients(layer, width);
  if (coefficients.crt1 == 0.0 && coefficients.crt2 == 0.0) {
    return base_resistance;
  }

  const F64 nominal_temperature = layer.has_t0()
                                      ? static_cast<F64>(layer.get_t0())
                                      : static_cast<F64>(corner.get_global_temperature());
  return applyResistanceTemperatureDerating(base_resistance, operating_temperature,
                                            nominal_temperature, coefficients.crt1,
                                            coefficients.crt2);
}

auto interval_resistance(const itf::LayerConductor& layer,
                         const EdgeEtchInterval& etch_interval,
                         Micron overlap_length) -> F64
{
  const Micron thickness = etch_interval.thickness;
  const Micron width = etch_interval.width;
  LOG_ERROR_IF(width <= 0.0 || thickness <= 0.0)
      << "etch interval width/thickness <= 0.";

  float resistivity = 0.0;
  const auto rho_opt = layer.get_rho_v_siw_t().query_interpolation(
      static_cast<float>(thickness), static_cast<float>(width));
  if (rho_opt.has_value()) {
    resistivity = rho_opt.value();
  } else {
    resistivity = layer.get_rho();
  }

  float sheet_resistance = 0.0;
  if (resistivity <= 0.0) {
    const auto rpsq_opt = layer.get_rpsq_vs_si_width().query_interpolation(
        static_cast<float>(width));
    if (rpsq_opt.has_value()) {
      sheet_resistance = rpsq_opt.value();
    } else {
      sheet_resistance = layer.get_rpsq();
    }
  }

  F64 resistance = 0.0;
  if (resistivity > 0.0) {
    resistance += resistivity * overlap_length / (width * thickness);
  }
  if (sheet_resistance > 0.0) {
    resistance += sheet_resistance * overlap_length / width;
  }
  return resistance;
}

}  // namespace

auto WireResistanceModel::calc(LineSegment<Micron> segment,
                               std::span<const EdgeEtchInterval> edge_etch_intervals,
                               const itf::ProcessCorner& corner,
                               const itf::LayerConductor& layer,
                               F64 operating_temperature) -> F64
{
  F64 resistance = 0.0;

  for (const EdgeEtchInterval& etch_interval : edge_etch_intervals) {
    const Micron overlap_lo = std::max(etch_interval.a0, segment.lo);
    const Micron overlap_hi = std::min(etch_interval.a1, segment.hi);
    if (overlap_hi <= overlap_lo) {
      continue;
    }

    const Micron overlap_length = overlap_hi - overlap_lo;
    const F64 base_resistance =
        interval_resistance(layer, etch_interval, overlap_length);
    resistance += apply_conductor_temperature_derating(
        base_resistance, operating_temperature, corner, layer, etch_interval.width);
  }

  return resistance;
}

}  // namespace ircx
