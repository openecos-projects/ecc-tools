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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "ReceiverThresholdCalculator.hpp"

#include <cmath>
#include <string>

#include "ArnoldiWaveformSolver.hpp"
#include "ThresholdNormalizer.hpp"
#include "TimingTolerance.hpp"

namespace ista {

namespace {
double railRatio(double source, double load)
{
  // Missing nominal voltages retain the existing normalized signal convention.
  return std::isfinite(source) && std::isfinite(load) && source > 0. && load > 0. ? load / source : 1.;
}
}  // namespace

double ReceiverThresholdCalculator::voltageRatio(const TimingArc& source, std::string& load)
{
  const auto* cell = getThresholdTimingCell(load);
  return cell ? railRatio(source.get_nom_voltage(), cell->get_nom_voltage()) : 1.;
}

LibertyCell* ReceiverThresholdCalculator::sourceCell(std::string& source, AnalysisType analysis, TransType edge)
{
  auto* from = getThresholdTimingCell(source);
  if (!from) {
    auto rule = _database.sdc().get_port_constraint_map().find(source);
    if (rule != _database.sdc().get_port_constraint_map().end()) {
      auto* driving = rule->second.get_driving_cell(analysis, edge);
      if (driving) {
        auto cell = _database.liberty().get_cell_map().find(driving->get_cell_name());
        if (cell != _database.liberty().get_cell_map().end())
          from = &cell->second;
      }
    }
  }
  return from;
}

void ReceiverThresholdCalculator::adjustSignalLevel(std::string& source, std::string& load, AnalysisType analysis, TransType edge, double& delay, double& slew)
{
  auto* from = sourceCell(source, analysis, edge);
  auto* to = getThresholdTimingCell(load);
  const auto source_pin = _database.network().pins().find(source);
  if (!to || !std::isfinite(slew) || (!from && (source_pin == _database.network().pins().end() || !source_pin->second.get_is_port())))
    return;
  const bool fall = edge == TransType::kFall;
  const auto thresholds = [fall](const auto& owner) -> WaveformThresholds {
    return {normalizedThreshold(fall ? owner.get_output_threshold_pct_fall() : owner.get_output_threshold_pct_rise()),
            normalizedThreshold(fall ? owner.get_slew_lower_threshold_pct_fall() : owner.get_slew_lower_threshold_pct_rise()),
            normalizedThreshold(fall ? owner.get_slew_upper_threshold_pct_fall() : owner.get_slew_upper_threshold_pct_rise()),
            owner.get_slew_derate_from_library()};
  };
  // A prescribed port waveform uses the main library unless set_driving_cell selects a cell.
  const auto source_thresholds = from ? thresholds(*from) : thresholds(_database.liberty());
  const double source_voltage = from ? from->get_nom_voltage() : _database.liberty().get_nom_voltage();
  const double source_delay = source_thresholds.delay;
  const double load_delay = normalizedThreshold(getTimingCellInputThreshold(*to, edge));
  const double source_window = source_thresholds.upper - source_thresholds.lower;
  const double load_window = normalizedThreshold(getTimingCellSlewUpperThreshold(*to, edge)) - normalizedThreshold(getTimingCellSlewLowerThreshold(*to, edge));
  const double source_derate = source_thresholds.derate, load_derate = to->get_slew_derate_from_library();
  if (!(source_window > 0. && load_window > 0. && source_derate > 0. && load_derate > 0.) || !std::isfinite(source_window) || !std::isfinite(load_window)
      || !std::isfinite(source_derate) || !std::isfinite(load_derate) || !std::isfinite(source_delay) || !std::isfinite(load_delay))
    return;
  const double ratio = railRatio(source_voltage, to->get_nom_voltage());
  const double shift = std::abs(slew) * (ratio * load_delay - source_delay) / source_window;
  delay += fall ? -shift : shift;
  slew *= ratio * source_derate / load_derate * load_window / source_window;
}

bool ReceiverThresholdCalculator::compatible(TimingArc& timing_arc, std::string& load_pin, TransType trans_type)
{
  // Equal voltage windows describe the same physical transition. Different
  // slew derates only change its reported scale, which adjust() converts.
  // Different windows still need a waveform crossing, not a scalar replacement.
  LibertyCell* load_timing_cell = getThresholdTimingCell(load_pin);
  if (load_timing_cell == nullptr) {
    return false;
  }
  double driver_lower_threshold
      = trans_type == TransType::kFall ? timing_arc.get_slew_lower_threshold_pct_fall() : timing_arc.get_slew_lower_threshold_pct_rise();
  double driver_upper_threshold
      = trans_type == TransType::kFall ? timing_arc.get_slew_upper_threshold_pct_fall() : timing_arc.get_slew_upper_threshold_pct_rise();
  double load_lower_threshold = getTimingCellSlewLowerThreshold(*load_timing_cell, trans_type);
  double load_upper_threshold = getTimingCellSlewUpperThreshold(*load_timing_cell, trans_type);
  return std::abs(normalizedThreshold(driver_lower_threshold) - normalizedThreshold(load_lower_threshold)) < ista::kTimingTolerance
         && std::abs(normalizedThreshold(driver_upper_threshold) - normalizedThreshold(load_upper_threshold)) < ista::kTimingTolerance
         && timing_arc.get_slew_derate() > 0. && std::isfinite(timing_arc.get_slew_derate()) && load_timing_cell->get_slew_derate_from_library() > 0.
         && std::isfinite(load_timing_cell->get_slew_derate_from_library());
}

void ReceiverThresholdCalculator::adjust(TimingArc& timing_arc, std::string& load_pin, TransType trans_type, double& wire_delay, double& load_slew)
{
  LibertyCell* load_timing_cell = getThresholdTimingCell(load_pin);
  if (load_timing_cell == nullptr || load_timing_cell->get_library_name() == timing_arc.get_library_name()) {
    return;
  }

  double driver_output_threshold = trans_type == TransType::kFall ? timing_arc.get_output_threshold_pct_fall() : timing_arc.get_output_threshold_pct_rise();
  double driver_slew_lower_threshold
      = trans_type == TransType::kFall ? timing_arc.get_slew_lower_threshold_pct_fall() : timing_arc.get_slew_lower_threshold_pct_rise();
  double driver_slew_upper_threshold
      = trans_type == TransType::kFall ? timing_arc.get_slew_upper_threshold_pct_fall() : timing_arc.get_slew_upper_threshold_pct_rise();
  double driver_slew_derate = timing_arc.get_slew_derate();
  double load_input_threshold = getTimingCellInputThreshold(*load_timing_cell, trans_type);
  double load_slew_lower_threshold = getTimingCellSlewLowerThreshold(*load_timing_cell, trans_type);
  double load_slew_upper_threshold = getTimingCellSlewUpperThreshold(*load_timing_cell, trans_type);
  double load_slew_derate = load_timing_cell->get_slew_derate_from_library();

  driver_output_threshold = normalizedThreshold(driver_output_threshold);
  driver_slew_lower_threshold = normalizedThreshold(driver_slew_lower_threshold);
  driver_slew_upper_threshold = normalizedThreshold(driver_slew_upper_threshold);
  load_input_threshold = normalizedThreshold(load_input_threshold);
  load_slew_lower_threshold = normalizedThreshold(load_slew_lower_threshold);
  load_slew_upper_threshold = normalizedThreshold(load_slew_upper_threshold);

  double driver_slew_delta = driver_slew_upper_threshold - driver_slew_lower_threshold;
  double load_slew_delta = load_slew_upper_threshold - load_slew_lower_threshold;
  if (!(driver_slew_delta > ista::kTimingTolerance && load_slew_delta > ista::kTimingTolerance && driver_slew_derate > ista::kTimingTolerance
        && load_slew_derate > ista::kTimingTolerance)) {
    return;
  }

  double wire_delay_delta = load_slew * ((load_input_threshold - driver_output_threshold) / driver_slew_delta);
  if (trans_type == TransType::kFall) {
    wire_delay -= wire_delay_delta;
  } else {
    wire_delay += wire_delay_delta;
  }
  load_slew = load_slew * ((load_slew_delta / load_slew_derate) / (driver_slew_delta / driver_slew_derate));
}

LibertyCell* ReceiverThresholdCalculator::getThresholdTimingCell(std::string& pin_name)
{
  Database& database = _database;
  if (database.network().pins().count(pin_name) == 0) {
    return nullptr;
  }
  Pin& pin = database.network().pins()[pin_name];
  if (pin.get_is_port() || database.network().instances().count(pin.get_instance_name()) == 0) {
    return nullptr;
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  if (database.liberty().get_cell_map().count(instance.get_cell_name()) == 0) {
    return nullptr;
  }
  return &database.liberty().get_cell_map()[instance.get_cell_name()];
}

double ReceiverThresholdCalculator::getTimingCellSlewLowerThreshold(LibertyCell& timing_cell, TransType trans_type)
{
  if (trans_type == TransType::kFall) {
    return timing_cell.get_slew_lower_threshold_pct_fall();
  }
  return timing_cell.get_slew_lower_threshold_pct_rise();
}

double ReceiverThresholdCalculator::getTimingCellSlewUpperThreshold(LibertyCell& timing_cell, TransType trans_type)
{
  if (trans_type == TransType::kFall) {
    return timing_cell.get_slew_upper_threshold_pct_fall();
  }
  return timing_cell.get_slew_upper_threshold_pct_rise();
}

double ReceiverThresholdCalculator::getTimingCellInputThreshold(LibertyCell& timing_cell, TransType trans_type)
{
  if (trans_type == TransType::kFall) {
    return timing_cell.get_input_threshold_pct_fall();
  }
  return timing_cell.get_input_threshold_pct_rise();
}

}  // namespace ista
