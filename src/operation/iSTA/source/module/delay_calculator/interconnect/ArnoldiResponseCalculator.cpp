// SPDX-License-Identifier: MulanPSL-2.0
#include "ArnoldiResponseCalculator.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>

#include "ArnoldiDelayCalculator.hpp"
#include "ArnoldiWaveformSolver.hpp"
#include "ThresholdNormalizer.hpp"

namespace ista {
std::optional<DriverResponse> ArnoldiResponseCalculator::smallNetResponse(TimingArc& timing_arc, TransType edge, double input_slew,
                                                                          const ParasiticArnoldiModel& model)
{
  if (!model.get_is_valid() || !model.smallNetTimeConstants())
    return std::nullopt;
  const bool fall = edge == TransType::kFall;
  const double lower = normalizedThreshold(fall ? timing_arc.get_slew_lower_threshold_pct_fall() : timing_arc.get_slew_lower_threshold_pct_rise());
  const double upper = normalizedThreshold(fall ? timing_arc.get_slew_upper_threshold_pct_fall() : timing_arc.get_slew_upper_threshold_pct_rise());
  const double middle = normalizedThreshold(fall ? timing_arc.get_output_threshold_pct_fall() : timing_arc.get_output_threshold_pct_rise());
  const double derate = timing_arc.get_slew_derate();
  if (!(lower > 0. && upper < 1. && upper > lower && derate > 0.) || !std::isfinite(derate) || std::abs(lower + upper - 1.) > 1e-12
      || std::abs(middle - .5) > 1e-12)
    return std::nullopt;
  // Keep the distributed solver for mixed voltage/threshold domains until
  // their small-network transfer model is established independently.
  for (std::size_t term = 1; term < model.get_term_pin_list().size(); ++term) {
    auto pin = model.get_term_pin_list()[term];
    auto* receiver = _threshold.getThresholdTimingCell(pin);
    if (!receiver || std::abs(normalizedThreshold(_threshold.getTimingCellInputThreshold(*receiver, edge)) - middle) > 1e-12)
      return std::nullopt;
    if (std::abs(_threshold.voltageRatio(timing_arc, pin) - 1.) > 1e-12 || !_threshold.compatible(timing_arc, pin, edge))
      return std::nullopt;
  }
  const double time_scale = timing_arc.get_time_unit_scale();
  const double load = model.get_total_capacitance();
  const auto delay_table = timing_arc.get_delay_table_map().find(edge);
  const auto slew_table = timing_arc.get_slew_table_map().find(edge);
  if (!(time_scale > 0.) || delay_table == timing_arc.get_delay_table_map().end() || slew_table == timing_arc.get_slew_table_map().end())
    return std::nullopt;
  const double delay = delay_table->second.findValue(input_slew * time_scale, load * timing_arc.get_cap_unit_scale()) / time_scale;
  const double slew = slew_table->second.findValue(input_slew * time_scale, load * timing_arc.get_cap_unit_scale()) / time_scale;
  const auto& constants = *model.smallNetTimeConstants();
  const double tau = constants[fall ? 1 : 0];
  const double slew_factor = std::log(upper / lower) / derate;
  // The balanced model corrects the source using the rise time constant
  // for both slew edges. Ordinary low RC retains the table source values.
  const double correction = model.hasSmallNetSourceCorrection() ? .3 : 0.;
  const double source_slew = std::max(0., slew - correction * constants[0] * slew_factor);
  if (!std::isfinite(delay) || !std::isfinite(slew) || slew < 0.)
    return std::nullopt;
  DriverResponse result;
  result.set_is_valid(true);
  result.set_effective_capacitance(load);
  result.set_gate_delay(delay - correction * tau * std::log(2.));
  result.set_driver_slew(source_slew);
  for (std::size_t term = 0; term < model.get_term_pin_list().size(); ++term) {
    auto pin = model.get_term_pin_list()[term];
    double wire_delay = term == 0 ? 0. : tau * std::log(2.);
    double load_slew = term == 0 ? source_slew : std::hypot(source_slew, tau * slew_factor);
    if (term > 0)
      _threshold.adjust(timing_arc, pin, edge, wire_delay, load_slew);
    for (const auto& name : {pin, model.get_term_node_list()[term]}) {
      result.get_wire_delay_map()[name] = wire_delay;
      result.get_load_slew_map()[name] = load_slew;
    }
  }
  return result;
}

DriverResponse& ArnoldiResponseCalculator::response(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type,
                                                    double input_slew)
{
  TimingResultKey timing_result_key = timingKey(output_pin, timing_arc, analysis_type, output_trans_type, input_slew);
  if (_responses.count(timing_result_key) == 0) {
    _responses[timing_result_key] = calculate(output_pin, timing_arc, analysis_type, output_trans_type, input_slew);
  }
  return _responses[timing_result_key];
}

ArnoldiResponseCalculator::TimingResultKey ArnoldiResponseCalculator::timingKey(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type,
                                                                                TransType output_trans_type, double input_slew)
{
  return std::make_tuple(output_pin, reinterpret_cast<std::uintptr_t>(&timing_arc), analysis_type, output_trans_type, input_slew);
}

DriverResponse ArnoldiResponseCalculator::calculate(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type,
                                                    double input_slew)
{
  DriverResponse timing_result;
  Database& database = _database;
  if (database.network().pins().count(output_pin) == 0) {
    return timing_result;
  }
  Pin& output_pin_data = database.network().pins()[output_pin];
  if (output_pin_data.get_net_name().empty() || database.parasitics().nets().count(output_pin_data.get_net_name()) == 0) {
    return timing_result;
  }

  if (_rc.hasDisabledDriver(output_pin_data.get_net_name(), output_pin))
    return timing_result;

  ParasiticNet& parasitic_net = database.parasitics().nets()[output_pin_data.get_net_name()];
  std::string source_node_name = _rc.nodeName(parasitic_net, output_pin);
  if (source_node_name.empty()) {
    return timing_result;
  }

  ParasiticArnoldiModel& arnoldi_model = _models.arnoldiModel(parasitic_net, source_node_name, analysis_type, output_trans_type);
  if (auto small = smallNetResponse(timing_arc, output_trans_type, input_slew, arnoldi_model)) {
    cacheDriverResponse(output_pin, analysis_type, output_trans_type, small->get_driver_slew(), *small);
    return *small;
  }
  auto calculated = ArnoldiDelayCalculator{}.calculate(timing_arc, output_trans_type, input_slew, arnoldi_model, _config.adjust_driver_resistance != 0);
  if (!calculated) {
    return timing_result;
  }
  const auto& delay_list = calculated->delays;
  const auto& slew_list = calculated->slews;
  double effective_capacitance = calculated->effective_capacitance;

  timing_result.set_is_valid(true);
  timing_result.set_effective_capacitance(effective_capacitance);
  timing_result.set_gate_delay(delay_list.front());
  timing_result.set_driver_slew(slew_list.front());
  bool suppress_min_slew = _config.min_slew_degradation == 0 && analysis_type == AnalysisType::kMin && calculated->resistance_adjusted;
  bool unreachable_receiver = false;
  for (std::size_t term_idx = 0; term_idx < arnoldi_model.get_term_node_list().size(); term_idx++) {
    std::string& term_node_name = arnoldi_model.get_term_node_list()[term_idx];
    double wire_delay = delay_list[term_idx] - delay_list.front();
    double load_slew = slew_list[term_idx];
    std::string pin_name = _rc.pinName(term_node_name);
    if (term_idx > 0) {
      const double ratio = _threshold.voltageRatio(timing_arc, pin_name);
      if (std::abs(ratio - 1.) > 1e-12) {
        auto* receiver = _threshold.getThresholdTimingCell(pin_name);
        const double input_threshold = normalizedThreshold(_threshold.getTimingCellInputThreshold(*receiver, output_trans_type));
        const double slew_lower = normalizedThreshold(_threshold.getTimingCellSlewLowerThreshold(*receiver, output_trans_type));
        const double slew_upper = normalizedThreshold(_threshold.getTimingCellSlewUpperThreshold(*receiver, output_trans_type));
        const double derate = receiver->get_slew_derate_from_library();
        if (!(input_threshold > 0. && input_threshold < 1. && slew_lower > 0. && slew_upper < 1. && slew_upper > slew_lower && derate > 0.
              && std::isfinite(derate))) {
          timing_result.setLoadFailed(term_node_name);
          timing_result.setLoadFailed(pin_name);
          continue;
        }
        const double middle = ratio * input_threshold;
        const double lower = ratio * slew_lower;
        const double upper = ratio * slew_upper;
        if (!(middle > 0. && middle < 1. && lower > 0. && upper < 1. && upper > lower)) {
          // Valid receiver thresholds outside the source voltage swing make
          // the shared network response unreachable, not merely uncached.
          timing_result.setLoadFailed(term_node_name);
          timing_result.setLoadFailed(pin_name);
          unreachable_receiver = true;
          continue;
        }
        const bool fall = output_trans_type == TransType::kFall;
        double source_time = 0., middle_time = 0., early = 0., late = 0.;
        const double source_threshold = normalizedThreshold(fall ? timing_arc.get_output_threshold_pct_fall() : timing_arc.get_output_threshold_pct_rise());
        ArnoldiWaveformSolver::crossingTime(calculated->driver_ramp, calculated->waveform, 0, fall ? source_threshold : 1. - source_threshold, source_time);
        ArnoldiWaveformSolver::crossingTime(calculated->driver_ramp, calculated->waveform, term_idx, fall ? middle : 1. - middle, middle_time);
        ArnoldiWaveformSolver::crossingTime(calculated->driver_ramp, calculated->waveform, term_idx, fall ? upper : 1. - lower, early);
        ArnoldiWaveformSolver::crossingTime(calculated->driver_ramp, calculated->waveform, term_idx, fall ? lower : 1. - upper, late);
        wire_delay = middle_time - source_time;
        load_slew = (late - early) / derate;
        if (!std::isfinite(wire_delay) || !std::isfinite(load_slew) || load_slew < 0.) {
          timing_result.setLoadFailed(term_node_name);
          timing_result.setLoadFailed(pin_name);
          continue;
        }
      } else {
        _threshold.adjust(timing_arc, pin_name, output_trans_type, wire_delay, load_slew);
      }
      if (suppress_min_slew) {
        // The adjusted-driver minimum-slew policy also applies across voltage
        // domains and threshold windows. Keep the physical wire delay; only
        // scale the replacement source slew to the receiver's convention.
        auto* receiver = _threshold.getThresholdTimingCell(pin_name);
        if (receiver) {
          const bool fall = output_trans_type == TransType::kFall;
          const double source_window
              = normalizedThreshold(fall ? timing_arc.get_slew_upper_threshold_pct_fall() : timing_arc.get_slew_upper_threshold_pct_rise())
                - normalizedThreshold(fall ? timing_arc.get_slew_lower_threshold_pct_fall() : timing_arc.get_slew_lower_threshold_pct_rise());
          const double receiver_window = normalizedThreshold(_threshold.getTimingCellSlewUpperThreshold(*receiver, output_trans_type))
                                         - normalizedThreshold(_threshold.getTimingCellSlewLowerThreshold(*receiver, output_trans_type));
          const double scale = ratio * timing_arc.get_slew_derate() * receiver_window / (receiver->get_slew_derate_from_library() * source_window);
          if (source_window > 0. && receiver_window > 0. && std::isfinite(scale) && scale > 0.)
            load_slew = slew_list.front() * scale;
        }
      }
    }
    timing_result.get_wire_delay_map()[term_node_name] = wire_delay;
    timing_result.get_load_slew_map()[term_node_name] = load_slew;
    timing_result.get_wire_delay_map()[pin_name] = wire_delay;
    timing_result.get_load_slew_map()[pin_name] = load_slew;
  }
  if (unreachable_receiver) {
    // Receivers share one distributed-network solve. An unreachable crossing
    // invalidates that solve for this driver/analysis/edge, including siblings
    // whose individual thresholds were reachable. Keep the fitted gate values;
    // WireDelayCalculator publishes explicit nonbounding 0/source-slew values.
    for (std::size_t term_idx = 1; term_idx < arnoldi_model.get_term_node_list().size(); ++term_idx) {
      std::string& term = arnoldi_model.get_term_node_list()[term_idx];
      for (const auto& name : {term, _rc.pinName(term)}) {
        timing_result.setLoadFailed(name);
        timing_result.get_wire_delay_map().erase(name);
        timing_result.get_load_slew_map().erase(name);
      }
    }
  }
  cacheDriverResponse(output_pin, analysis_type, output_trans_type, timing_result.get_driver_slew(), timing_result);
  return timing_result;
}

void ArnoldiResponseCalculator::cacheDriverResponse(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type, double driver_slew,
                                                    DriverResponse& timing_result)
{
  DriverResultKey driver_result_key = driverKey(output_pin, analysis_type, output_trans_type, driver_slew);
  _driver_responses[driver_result_key] = timing_result;
}

ArnoldiResponseCalculator::DriverResultKey ArnoldiResponseCalculator::driverKey(std::string& output_pin, AnalysisType analysis_type,
                                                                                TransType output_trans_type, double driver_slew)
{
  return std::make_tuple(output_pin, analysis_type, output_trans_type, driver_slew);
}

bool ArnoldiResponseCalculator::loadFailed(Arc& arc, AnalysisType analysis, TransType edge, double slew)
{
  if (!std::isfinite(slew))
    return false;
  auto key = driverKey(arc.get_source_pin(), analysis, edge, slew);
  auto found = _driver_responses.find(key);
  return found != _driver_responses.end() && found->second.loadFailed(arc.get_sink_pin());
}

std::optional<double> ArnoldiResponseCalculator::wireDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew)
{
  if (!std::isfinite(input_slew)) {
    return std::nullopt;
  }
  std::string& source_pin = arc.get_source_pin();
  DriverResultKey driver_result_key = driverKey(source_pin, analysis_type, trans_type, input_slew);
  if (_driver_responses.count(driver_result_key) == 0) {
    return std::nullopt;
  }
  DriverResponse& timing_result = _driver_responses[driver_result_key];
  std::string& sink_pin = arc.get_sink_pin();
  if (timing_result.get_wire_delay_map().count(sink_pin) == 0) {
    return std::nullopt;
  }
  return timing_result.get_wire_delay_map()[sink_pin];
}

std::optional<double> ArnoldiResponseCalculator::loadSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew)
{
  if (!std::isfinite(input_slew)) {
    return std::nullopt;
  }
  std::string& source_pin = arc.get_source_pin();
  DriverResultKey driver_result_key = driverKey(source_pin, analysis_type, trans_type, input_slew);
  if (_driver_responses.count(driver_result_key) == 0) {
    return std::nullopt;
  }
  DriverResponse& timing_result = _driver_responses[driver_result_key];
  std::string& sink_pin = arc.get_sink_pin();
  if (timing_result.get_load_slew_map().count(sink_pin) == 0) {
    return std::nullopt;
  }
  return timing_result.get_load_slew_map()[sink_pin];
}

}  // namespace ista
