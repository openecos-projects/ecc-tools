// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <map>
#include <optional>
#include <string>
#include <tuple>

#include "Database.hpp"
#include "DelayResult.hpp"
#include "RcModelBuilder.hpp"
#include "RcTreeAnalyzer.hpp"
#include "ReceiverThresholdCalculator.hpp"

namespace ista {
// Owns prescribed input-port waveform responses for one analysis.
class InputPortResponseCalculator
{
 public:
  InputPortResponseCalculator(Database& database, RcTreeAnalyzer& rc, RcModelBuilder& models, ReceiverThresholdCalculator& threshold)
      : _database(database), _rc(rc), _models(models), _threshold(threshold)
  {
  }
  InputPortResponseCalculator(const InputPortResponseCalculator&) = delete;
  InputPortResponseCalculator& operator=(const InputPortResponseCalculator&) = delete;
  void clear() { _responses.clear(); }
  bool loadFailed(ParasiticNet& net, std::string& source, std::string& sink, AnalysisType analysis, TransType edge, double slew);
  std::optional<double> delay(ParasiticNet& parasitic_net, std::string& source_node_name, std::string& sink_node_name, AnalysisType analysis_type,
                              TransType trans_type, double input_slew);
  std::optional<double> slew(ParasiticNet& parasitic_net, std::string& source_node_name, std::string& sink_node_name, AnalysisType analysis_type,
                             TransType trans_type, double input_slew);

 private:
  using ResponseKey = std::tuple<std::string, AnalysisType, TransType, double>;

  DriverResponse& response(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type, TransType trans_type, double input_slew);
  DriverResponse calculate(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type, TransType trans_type, double input_slew);
  Database& _database;
  RcTreeAnalyzer& _rc;
  RcModelBuilder& _models;
  ReceiverThresholdCalculator& _threshold;
  std::map<ResponseKey, DriverResponse> _responses;
};
}  // namespace ista
