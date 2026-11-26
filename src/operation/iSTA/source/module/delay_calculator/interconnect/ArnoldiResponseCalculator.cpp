// SPDX-License-Identifier: MulanPSL-2.0
#include "ArnoldiResponseCalculator.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "ArnoldiDelayCalculator.hpp"
#include "ArnoldiWaveformSolver.hpp"
#include "ThresholdNormalizer.hpp"

namespace ista {
std::optional<DriverResponse> ArnoldiResponseCalculator::capacitiveResponse(TimingArc& timing_arc, AnalysisType analysis, TransType edge,
                                                                          double input_slew, ParasiticNet& parasitic_net,
                                                                          const ParasiticArnoldiModel& model)
{
  const double limit = _config.small_net_limit_ps * 1e-3;
  const double capacitance = model.get_total_capacitance();
  if (!(limit > 0. && capacitance > 0. && input_slew >= 0.) || !std::isfinite(limit) || !std::isfinite(capacitance)
      || !std::isfinite(input_slew) || !model.get_is_valid())
    return std::nullopt;
  const auto& nodes = model.get_term_node_list();
  const auto& pins = model.get_term_pin_list();
  const auto logical = _database.network().nets().find(parasitic_net.get_net_name());
  if (pins.size() < 2 || nodes.size() != pins.size() || logical == _database.network().nets().end()
      || logical->second.get_driver_pin_list().size() != 1 || logical->second.get_driver_pin_list().front() != pins.front()
      || logical->second.get_load_pin_list().size() + 1 != pins.size())
    return std::nullopt;
  for (const auto& pin : logical->second.get_load_pin_list())
    if (pin == pins.front() || !model.get_term_index_map().contains(pin))
      return std::nullopt;
  for (const auto& resistor : parasitic_net.get_resistor_list())
    if (!(resistor.get_resistance() > 0.) || !std::isfinite(resistor.get_resistance()))
      return std::nullopt;
  const double time_bound = _rc.totalResistance(parasitic_net) * 1e-3 * capacitance;
  if (!(time_bound >= 0. && time_bound < limit))
    return std::nullopt;
  for (const auto& [name, node] : parasitic_net.get_node_map())
    if (!(node.get_capacitance() >= 0.) || !std::isfinite(node.get_capacitance()))
      return std::nullopt;
  // Do not silently omit isolated internal parasitics when estimating the load.
  std::vector<std::string> connected;
  std::vector<int32_t> parents;
  std::vector<double> resistances, capacitances;
  auto root = nodes.front();
  _rc.buildTree(parasitic_net, root, analysis, edge, connected, parents, resistances, capacitances);
  if (connected.size() != parasitic_net.get_node_map().size())
    return std::nullopt;
  for (double value : capacitances)
    if (!(value >= 0.) || !std::isfinite(value))
      return std::nullopt;

  const bool fall = edge == TransType::kFall;
  const double lower = normalizedThreshold(fall ? timing_arc.get_slew_lower_threshold_pct_fall() : timing_arc.get_slew_lower_threshold_pct_rise());
  const double upper = normalizedThreshold(fall ? timing_arc.get_slew_upper_threshold_pct_fall() : timing_arc.get_slew_upper_threshold_pct_rise());
  const double middle = normalizedThreshold(fall ? timing_arc.get_output_threshold_pct_fall() : timing_arc.get_output_threshold_pct_rise());
  const double derate = timing_arc.get_slew_derate();
  if (!(lower > 0. && upper < 1. && upper > lower && derate > 0.) || !std::isfinite(derate) || !std::isfinite(middle)
      || std::abs(lower + upper - 1.) > 1e-12 || std::abs(middle - .5) > 1e-12)
    return std::nullopt;
  for (std::size_t term = 1; term < pins.size(); ++term) {
    auto pin = pins[term];
    auto* receiver = _threshold.getThresholdTimingCell(pin);
    if (!receiver)
      return std::nullopt;
    const double receiver_midpoint = normalizedThreshold(_threshold.getTimingCellInputThreshold(*receiver, edge));
    if (!std::isfinite(receiver_midpoint) || std::abs(receiver_midpoint - middle) > 1e-12
        || std::abs(_threshold.voltageRatio(timing_arc, pin) - 1.) > 1e-12 || !_threshold.compatible(timing_arc, pin, edge))
      return std::nullopt;
  }
  const double time_scale = timing_arc.get_time_unit_scale();
  const double cap_scale = timing_arc.get_cap_unit_scale();
  const auto delay_table = timing_arc.get_delay_table_map().find(edge);
  const auto slew_table = timing_arc.get_slew_table_map().find(edge);
  if (!(time_scale > 0. && cap_scale > 0.) || !std::isfinite(time_scale) || !std::isfinite(cap_scale)
      || delay_table == timing_arc.get_delay_table_map().end() || slew_table == timing_arc.get_slew_table_map().end()
      || delay_table->second.get_value_list().empty() || slew_table->second.get_value_list().empty())
    return std::nullopt;
  const double delay = delay_table->second.findValue(input_slew * time_scale, capacitance * cap_scale) / time_scale;
  const double slew = slew_table->second.findValue(input_slew * time_scale, capacitance * cap_scale) / time_scale;
  const double ramp = slew * derate / (upper - lower);
  if (!std::isfinite(delay) || !(slew >= 0.) || !std::isfinite(slew) || !std::isfinite(ramp))
    return std::nullopt;
  const auto& projection = model.responseProjection();
  if (projection.empty() || projection.front().size() != pins.size())
    return std::nullopt;

  DriverResponse result;
  result.set_is_valid(true);
  result.set_effective_capacitance(capacitance);
  result.set_gate_delay(delay);
  result.set_driver_slew(slew);
  for (std::size_t term = 0; term < pins.size(); ++term) {
    double wire_delay = 0., load_slew = slew;
    if (term > 0) {
      // The first basis is 1/sqrt(Ctotal). Its G^-1 C projection therefore
      // gives each receiver's first moment after multiplication by sqrt(Ctotal).
      // A one-pole transfer preserves that moment and distinct branch delays.
      const double moment = projection.front()[term] * model.get_sqrt_total_capacitance();
      if (!(moment >= 0.) || !std::isfinite(moment))
        return std::nullopt;
      const auto crossing = [&](double voltage) {
        return moment == 0. ? (1. - voltage) * ramp : ArnoldiWaveformSolver::rampTime(1. / moment, ramp, voltage);
      };
      wire_delay = crossing(.5) - .5 * ramp;
      auto pin = pins[term];
      const auto* receiver = _threshold.getThresholdTimingCell(pin);
      load_slew = (crossing(lower) - crossing(upper)) / receiver->get_slew_derate_from_library();
      if (!std::isfinite(wire_delay) || !(load_slew >= 0.) || !std::isfinite(load_slew))
        return std::nullopt;
    }
    for (const auto& name : {nodes[term], pins[term]}) {
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
  if (_rc.hasIncompleteNetwork(parasitic_net, output_pin))
    return timing_result;
  std::string source_node_name = _rc.nodeName(parasitic_net, output_pin);
  if (source_node_name.empty()) {
    return timing_result;
  }

  ParasiticArnoldiModel& arnoldi_model = _models.arnoldiModel(parasitic_net, source_node_name, analysis_type, output_trans_type);
  if (auto compact = capacitiveResponse(timing_arc, analysis_type, output_trans_type, input_slew, parasitic_net, arnoldi_model)) {
    cacheDriverResponse(output_pin, analysis_type, output_trans_type, compact->get_driver_slew(), *compact);
    return *compact;
  }
  auto calculated = ArnoldiDelayCalculator{}.calculate(timing_arc, output_trans_type, input_slew, arnoldi_model);
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
  for (std::size_t term_idx = 0; term_idx < arnoldi_model.get_term_node_list().size(); term_idx++) {
    std::string& term_node_name = arnoldi_model.get_term_node_list()[term_idx];
    double wire_delay = delay_list[term_idx] - delay_list.front();
    double load_slew = slew_list[term_idx];
    std::string pin_name = _rc.pinName(term_node_name);
    if (term_idx > 0) {
      if (auto* receiver = _threshold.getThresholdTimingCell(pin_name)) {
        const double ratio = _threshold.voltageRatio(timing_arc, pin_name);
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
          // This receiver's observation is unreachable. Its threshold does
          // not change the circuit solution at other terminals.
          timing_result.setLoadFailed(term_node_name);
          timing_result.setLoadFailed(pin_name);
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
      }
    }
    timing_result.get_wire_delay_map()[term_node_name] = wire_delay;
    timing_result.get_load_slew_map()[term_node_name] = load_slew;
    timing_result.get_wire_delay_map()[pin_name] = wire_delay;
    timing_result.get_load_slew_map()[pin_name] = load_slew;
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
