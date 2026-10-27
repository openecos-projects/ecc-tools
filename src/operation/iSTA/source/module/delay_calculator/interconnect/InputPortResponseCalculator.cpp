// SPDX-License-Identifier: MulanPSL-2.0
#include "InputPortResponseCalculator.hpp"

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "ArnoldiWaveformSolver.hpp"
#include "ThresholdNormalizer.hpp"

namespace ista {
bool InputPortResponseCalculator::loadFailed(ParasiticNet& net, std::string& source, std::string& sink, AnalysisType analysis, TransType edge, double slew)
{
  return response(net, source, analysis, edge, slew).loadFailed(sink);
}

std::optional<double> InputPortResponseCalculator::delay(ParasiticNet& parasitic_net, std::string& source_node_name, std::string& sink_node_name,
                                                         AnalysisType analysis_type, TransType trans_type, double input_slew)
{
  Database& database = _database;
  std::string source_pin_name = _rc.pinName(source_node_name);
  if (!database.network().pins().contains(source_pin_name) || !database.network().pins().at(source_pin_name).get_is_port()) {
    return std::nullopt;
  }
  DriverResponse& result = response(parasitic_net, source_node_name, analysis_type, trans_type, input_slew);
  if (!result.get_is_valid() || !result.get_wire_delay_map().contains(sink_node_name)) {
    return std::nullopt;
  }
  return result.get_wire_delay_map().at(sink_node_name);
}

DriverResponse& InputPortResponseCalculator::response(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                                      TransType trans_type, double input_slew)
{
  double slew = std::isfinite(input_slew) ? std::abs(input_slew) : 0.0;
  ResponseKey key{source_node_name, analysis_type, trans_type, slew};
  if (!_responses.contains(key)) {
    _responses[key] = calculate(parasitic_net, source_node_name, analysis_type, trans_type, slew);
  }
  return _responses.at(key);
}

DriverResponse InputPortResponseCalculator::calculate(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                                      TransType trans_type, double input_slew)
{
  DriverResponse result;
  Database& database = _database;
  std::string source_pin_name = _rc.pinName(source_node_name);
  if (!database.network().pins().contains(source_pin_name) || !database.network().pins().at(source_pin_name).get_is_port()) {
    return result;
  }
  ParasiticArnoldiModel& model = _models.arnoldiModel(parasitic_net, source_node_name, analysis_type, trans_type);
  if (!model.get_is_valid() || model.get_order() <= 0) {
    return result;
  }
  double derate = 1.0;
  double lower = 0.1;
  double upper = 0.9;
  double voltage_log = 0.0;
  double min_slew_factor = 0.0;
  double x1 = 0.0;
  double y1 = 0.0;
  ArnoldiWaveformSolver::thresholds(database.liberty(), trans_type, derate, lower, upper, voltage_log, min_slew_factor, x1, y1);
  LibertyLibrary& library = database.liberty();
  bool is_fall = trans_type == TransType::kFall;
  double source_threshold = normalizedThreshold(is_fall ? library.get_output_threshold_pct_fall() : library.get_output_threshold_pct_rise());
  WaveformThresholds source{source_threshold, lower, upper, derate};
  double source_voltage = library.get_nom_voltage();
  if (auto* source_cell = _threshold.sourceCell(source_pin_name, analysis_type, trans_type)) {
    source = {normalizedThreshold(is_fall ? source_cell->get_output_threshold_pct_fall() : source_cell->get_output_threshold_pct_rise()),
              normalizedThreshold(_threshold.getTimingCellSlewLowerThreshold(*source_cell, trans_type)),
              normalizedThreshold(_threshold.getTimingCellSlewUpperThreshold(*source_cell, trans_type)), source_cell->get_slew_derate_from_library()};
    source_voltage = source_cell->get_nom_voltage();
  }
  std::vector<WaveformThresholds> loads;
  loads.reserve(model.get_term_node_list().size());
  for (std::size_t term_idx = 0; term_idx < model.get_term_node_list().size(); ++term_idx) {
    std::string& node = model.get_term_node_list()[term_idx];
    std::string pin_name = _rc.pinName(node);
    LibertyCell* load_cell = _threshold.getThresholdTimingCell(pin_name);
    double load_threshold = normalizedThreshold(is_fall ? library.get_input_threshold_pct_fall() : library.get_input_threshold_pct_rise());
    double load_lower = lower;
    double load_upper = upper;
    double load_derate = derate;
    if (load_cell != nullptr) {
      load_threshold = normalizedThreshold(_threshold.getTimingCellInputThreshold(*load_cell, trans_type));
      load_lower = normalizedThreshold(_threshold.getTimingCellSlewLowerThreshold(*load_cell, trans_type));
      load_upper = normalizedThreshold(_threshold.getTimingCellSlewUpperThreshold(*load_cell, trans_type));
      load_derate = load_cell->get_slew_derate_from_library();
      const double load_voltage = load_cell->get_nom_voltage();
      if (term_idx > 0 && std::isfinite(source_voltage) && std::isfinite(load_voltage) && source_voltage > 0. && load_voltage > 0.) {
        const double ratio = load_voltage / source_voltage;
        load_threshold *= ratio;
        load_lower *= ratio;
        load_upper *= ratio;
        if (load_threshold >= 1. || load_lower >= 1. || load_upper >= 1.) {
          // Solve the remaining receivers independently of an unreachable rail.
          result.setLoadFailed(node);
          result.setLoadFailed(pin_name);
          loads.push_back(source);
          continue;
        }
      }
    }
    loads.push_back({load_threshold, load_lower, load_upper, load_derate});
  }
  auto calculated = ArnoldiWaveformSolver::inputPortResponse(model, trans_type, input_slew, source, loads);
  if (!calculated) {
    // A valid RC model with an unsolved waveform is a failure, not an absent model.
    for (std::size_t index = 1; index < model.get_term_node_list().size(); ++index) {
      auto& node = model.get_term_node_list()[index];
      result.setLoadFailed(node);
      result.setLoadFailed(_rc.pinName(node));
    }
    return result;
  }
  for (std::size_t term_idx = 0; term_idx < model.get_term_node_list().size(); ++term_idx) {
    std::string& node = model.get_term_node_list()[term_idx];
    std::string pin_name = _rc.pinName(node);
    if (result.loadFailed(node))
      continue;
    result.get_wire_delay_map()[node] = calculated->delays[term_idx];
    result.get_load_slew_map()[node] = calculated->slews[term_idx];
    result.get_wire_delay_map()[pin_name] = result.get_wire_delay_map()[node];
    result.get_load_slew_map()[pin_name] = result.get_load_slew_map()[node];
  }
  result.set_is_valid(true);
  return result;
}

std::optional<double> InputPortResponseCalculator::slew(ParasiticNet& parasitic_net, std::string& source_node_name, std::string& sink_node_name,
                                                        AnalysisType analysis_type, TransType trans_type, double input_slew)
{
  Database& database = _database;
  std::string source_pin_name = _rc.pinName(source_node_name);
  if (!database.network().pins().contains(source_pin_name) || !database.network().pins().at(source_pin_name).get_is_port()) {
    return std::nullopt;
  }
  DriverResponse& result = response(parasitic_net, source_node_name, analysis_type, trans_type, input_slew);
  if (!result.get_is_valid() || !result.get_load_slew_map().contains(sink_node_name)) {
    return std::nullopt;
  }
  double output_slew = result.get_load_slew_map().at(sink_node_name);
  return input_slew < 0.0 ? -output_slew : output_slew;
}

}  // namespace ista
