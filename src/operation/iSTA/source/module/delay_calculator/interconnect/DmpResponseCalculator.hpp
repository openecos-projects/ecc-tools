// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <map>
#include <optional>
#include <string>

#include "Database.hpp"
#include "DelayResult.hpp"
#include "RcModelBuilder.hpp"
#include "RcTreeAnalyzer.hpp"
#include "ReceiverThresholdCalculator.hpp"

namespace ista {
// Owns DMP gate and receiver responses for one analysis.
class DmpResponseCalculator
{
 public:
  DmpResponseCalculator(Database& database, RcTreeAnalyzer& rc, RcModelBuilder& models, ReceiverThresholdCalculator& threshold)
      : _database(database), _rc(rc), _models(models), _threshold(threshold)
  {
  }
  DmpResponseCalculator(const DmpResponseCalculator&) = delete;
  DmpResponseCalculator& operator=(const DmpResponseCalculator&) = delete;
  void clear()
  {
    _driver_responses.clear();
    _responses.clear();
  }
  DriverResponse& response(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type, double input_slew,
                           double output_load);
  std::optional<double> wireDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);
  std::optional<double> loadSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);
  bool loadFailed(Arc& arc, AnalysisType analysis, TransType edge, double slew);

 private:
  std::string timingKey(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type, double input_slew);
  DriverResponse calculate(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type, double input_slew,
                           double output_load);
  void cacheDriverResponse(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type, double driver_slew, DriverResponse& timing_result);
  std::string driverKey(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type, double driver_slew);
  Database& _database;
  RcTreeAnalyzer& _rc;
  RcModelBuilder& _models;
  ReceiverThresholdCalculator& _threshold;
  std::map<std::string, DriverResponse> _responses;
  std::map<std::string, DriverResponse> _driver_responses;
};
}  // namespace ista
