// SPDX-License-Identifier: MulanPSL-2.0
#include "DmpResponseCalculator.hpp"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "DmpDelayCalculator.hpp"
#include "ThresholdNormalizer.hpp"

namespace ista {
DriverResponse& DmpResponseCalculator::response(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type,
                                                double input_slew, double output_load)
{
  std::string timing_result_key = timingKey(output_pin, timing_arc, analysis_type, output_trans_type, input_slew);
  if (_responses.count(timing_result_key) == 0) {
    _responses[timing_result_key] = calculate(output_pin, timing_arc, analysis_type, output_trans_type, input_slew, output_load);
  }
  return _responses[timing_result_key];
}

std::string DmpResponseCalculator::timingKey(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type,
                                             double input_slew)
{
  std::stringstream key_stream;
  key_stream << output_pin << "|" << reinterpret_cast<std::uintptr_t>(&timing_arc) << "|" << static_cast<int32_t>(analysis_type) << "|"
             << static_cast<int32_t>(output_trans_type) << "|" << std::setprecision(17) << input_slew;
  return key_stream.str();
}

DriverResponse DmpResponseCalculator::calculate(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type,
                                                double input_slew, double output_load)
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

  ParasiticDmpModel& dmp_model = _models.dmpModel(parasitic_net, source_node_name, analysis_type, output_trans_type);
  if (!dmp_model.get_is_valid()) {
    return timing_result;
  }

  auto calculated = DmpDelayCalculator{}.calculate(timing_arc, output_trans_type, input_slew, dmp_model, output_load);
  if (!calculated.get_is_valid()) {
    return timing_result;
  }
  double gate_delay = calculated.get_gate_delay();
  double driver_slew = calculated.get_driver_slew();
  double effective_capacitance = calculated.get_effective_capacitance();

  timing_result.set_is_valid(true);
  timing_result.set_effective_capacitance(effective_capacitance);
  timing_result.set_gate_delay(gate_delay);
  timing_result.set_driver_slew(driver_slew);
  timing_result.get_wire_delay_map()[source_node_name] = 0.0;
  timing_result.get_wire_delay_map()[output_pin] = 0.0;
  timing_result.get_load_slew_map()[source_node_name] = driver_slew;
  timing_result.get_load_slew_map()[output_pin] = driver_slew;

  for (std::pair<const std::string, ParasiticDmpLoadModel>& load_model_pair : dmp_model.get_load_model_map()) {
    std::string load_node_name = load_model_pair.first;
    std::string load_pin_name = _rc.pinName(load_node_name);
    if (!calculated.get_wire_delay_map().contains(load_node_name)) {
      continue;
    }
    double wire_delay = calculated.get_wire_delay_map().at(load_node_name);
    double load_slew = calculated.get_load_slew_map().at(load_node_name);
    const double ratio = _threshold.voltageRatio(timing_arc, load_pin_name);
    if (std::abs(ratio - 1.) > 1e-12) {
      auto* receiver = _threshold.getThresholdTimingCell(load_pin_name);
      if (!DmpDelayCalculator::receiverResponse(load_model_pair.second, timing_arc, output_trans_type, driver_slew,
                                                ratio * normalizedThreshold(_threshold.getTimingCellInputThreshold(*receiver, output_trans_type)),
                                                ratio * normalizedThreshold(_threshold.getTimingCellSlewLowerThreshold(*receiver, output_trans_type)),
                                                ratio * normalizedThreshold(_threshold.getTimingCellSlewUpperThreshold(*receiver, output_trans_type)),
                                                receiver->get_slew_derate_from_library(), wire_delay, load_slew)) {
        timing_result.setLoadFailed(load_node_name);
        timing_result.setLoadFailed(load_pin_name);
        continue;
      }
    } else {
      _threshold.adjust(timing_arc, load_pin_name, output_trans_type, wire_delay, load_slew);
    }
    timing_result.get_wire_delay_map()[load_node_name] = wire_delay;
    timing_result.get_wire_delay_map()[load_pin_name] = wire_delay;
    timing_result.get_load_slew_map()[load_node_name] = load_slew;
    timing_result.get_load_slew_map()[load_pin_name] = load_slew;
  }
  cacheDriverResponse(output_pin, analysis_type, output_trans_type, driver_slew, timing_result);
  return timing_result;
}

void DmpResponseCalculator::cacheDriverResponse(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type, double driver_slew,
                                                DriverResponse& timing_result)
{
  std::string driver_result_key = driverKey(output_pin, analysis_type, output_trans_type, driver_slew);
  _driver_responses[driver_result_key] = timing_result;
}

std::string DmpResponseCalculator::driverKey(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type, double driver_slew)
{
  std::stringstream key_stream;
  key_stream << output_pin << "|" << static_cast<int32_t>(analysis_type) << "|" << static_cast<int32_t>(output_trans_type) << "|" << std::setprecision(17)
             << driver_slew;
  return key_stream.str();
}

bool DmpResponseCalculator::loadFailed(Arc& arc, AnalysisType analysis, TransType edge, double slew)
{
  if (!std::isfinite(slew))
    return false;
  const auto found = _driver_responses.find(driverKey(arc.get_source_pin(), analysis, edge, slew));
  return found != _driver_responses.end() && found->second.loadFailed(arc.get_sink_pin());
}

std::optional<double> DmpResponseCalculator::wireDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew)
{
  if (!std::isfinite(input_slew)) {
    return std::nullopt;
  }
  std::string& source_pin = arc.get_source_pin();
  std::string driver_result_key = driverKey(source_pin, analysis_type, trans_type, input_slew);
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

std::optional<double> DmpResponseCalculator::loadSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew)
{
  if (!std::isfinite(input_slew)) {
    return std::nullopt;
  }
  std::string& source_pin = arc.get_source_pin();
  std::string driver_result_key = driverKey(source_pin, analysis_type, trans_type, input_slew);
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
