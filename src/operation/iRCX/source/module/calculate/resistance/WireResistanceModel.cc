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
 * @file WireResistanceModel.cc
 * @brief iRCX module implementation detail.
 */
#include "WireResistanceModel.hh"

#include <algorithm>

#include "ProcessCorner.hpp"
#include "ResistanceTemperature.hh"
#include "log/Log.hh"

namespace ircx {

namespace {

auto intervalResistance(const LayerConductor& layer,
                        const EdgeEtchInterval& etch_interval,
                        Micron overlap_length) -> F64
{
  const Micron thickness = etch_interval.thickness;
  const Micron width = etch_interval.width;
  LOG_ERROR_IF(width <= 0.0 || thickness <= 0.0) << "etch interval width/thickness <= 0.";

  F32 resistivity = 0.0;
  const auto rho_opt = layer.query_rho_by_si_width_and_thickness(thickness, width);
  if (rho_opt.has_value()) {
    resistivity = rho_opt.value();
  } else {
    resistivity = layer.get_rho();
  }

  F32 sheet_resistance = 0.0;
  if (resistivity <= 0.0) {
    const auto rpsq_opt = layer.query_rpsq_by_si_width(width);
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
                               const ProcessCorner& corner,
                               const LayerConductor& layer,
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
    const F64 base_resistance = intervalResistance(layer, etch_interval, overlap_length);
    const ResistanceTemperatureCoefficients coefficients =
        resistanceTemperatureCoefficients(layer, [&](auto& crt1, auto& crt2) {
          layer.queryCrtBySiWidth(etch_interval.width, crt1, crt2);
        });
    resistance += applyResistanceTemperatureDerating(
        base_resistance,
        operating_temperature,
        resistanceNominalTemperature(layer, corner),
        coefficients);
  }

  return resistance;
}

}  // namespace ircx
