// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>

#include "Config.hpp"
#include "Database.hpp"
#include "DelayResult.hpp"
#include "RcModelBuilder.hpp"
#include "RcTreeAnalyzer.hpp"
#include "ReceiverThresholdCalculator.hpp"

namespace ista {
// Owns Arnoldi gate and receiver responses for one analysis.
class ArnoldiResponseCalculator
{
 public:
  ArnoldiResponseCalculator(Database& database, const Config& config, RcTreeAnalyzer& rc, RcModelBuilder& models, ReceiverThresholdCalculator& threshold)
      : _database(database), _config(config), _rc(rc), _models(models), _threshold(threshold)
  {
  }
  ArnoldiResponseCalculator(const ArnoldiResponseCalculator&) = delete;
  ArnoldiResponseCalculator& operator=(const ArnoldiResponseCalculator&) = delete;
  void clear()
  {
    _driver_responses.clear();
    _responses.clear();
  }
  DriverResponse& response(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type, double input_slew);
  bool loadFailed(Arc& arc, AnalysisType analysis, TransType edge, double slew);
  std::optional<double> wireDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);
  std::optional<double> loadSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);

 private:
  using TimingResultKey = std::tuple<std::string, std::uintptr_t, AnalysisType, TransType, double>;
  using DriverResultKey = std::tuple<std::string, AnalysisType, TransType, double>;

  TimingResultKey timingKey(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type, double input_slew);
  DriverResponse calculate(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type, double input_slew);
  std::optional<DriverResponse> smallNetResponse(TimingArc& timing_arc, TransType edge, double slew, const ParasiticArnoldiModel& model);
  void cacheDriverResponse(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type, double driver_slew, DriverResponse& timing_result);
  DriverResultKey driverKey(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type, double driver_slew);
  Database& _database;
  const Config& _config;
  RcTreeAnalyzer& _rc;
  RcModelBuilder& _models;
  ReceiverThresholdCalculator& _threshold;
  std::map<TimingResultKey, DriverResponse> _responses;
  std::map<DriverResultKey, DriverResponse> _driver_responses;
};
}  // namespace ista
