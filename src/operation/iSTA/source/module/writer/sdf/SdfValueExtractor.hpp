// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "Database.hpp"
#include "SdfDelay.hpp"
namespace ista {
struct SdfCellArcKey
{
  std::string condition;
  TransType input_trans_type = TransType::kNone;

  bool retaining = false;
  bool operator<(const SdfCellArcKey& other) const
  {
    return std::tie(condition, input_trans_type, retaining) < std::tie(other.condition, other.input_trans_type, other.retaining);
  }
};

using SdfCellArcDelayMap = std::map<SdfCellArcKey, SdfDelay>;

struct SdfCheckValues
{
  double minimum;
  double maximum;
};

class SdfValueExtractor
{
 public:
  explicit SdfValueExtractor(Database& database) : _database(database) {}
  LibertyCell* timingCell(Instance& instance);
  bool hasCellArcPins(Instance& instance, TimingCellArc& timing_cell_arc);
  bool hasCheckPins(Instance& instance, TimingCheckArc& timing_check_arc);
  SdfDelay arcDelay(Arc& arc, TransType input_trans_type = TransType::kNone);
  bool hasDelay(SdfDelay& sdf_delay);

  SdfCellArcDelayMap graphCellArcDelays(Arc& arc);
  SdfCellArcDelayMap cellArcDelays(TimingCellArc& cell_arc);
  SdfCheckValues checkValues(Instance& instance, TimingCheckArc& check_arc, TimingArc& timing_arc, TransType transition);

 private:
  SdfDelay timingArcDelay(Arc& arc, TimingArc& timing_arc, TransType input_trans_type, bool allow_aggregate);
  SdfDelay cellArcDelay(TimingCellArc& cell_arc, TimingArc& timing_arc, TransType input_trans_type);
  double checkDelay(Instance& instance, TimingCheckArc& check_arc, TimingArc& timing_arc, AnalysisType analysis_type, TransType transition);
  void adjustHoldDelay(Instance& instance, TimingCheckArc& check_arc, TimingArc& timing_arc, TransType transition, double& minimum, double& maximum);
  void mergeCellArcDelay(SdfCellArcDelayMap& delays, TimingArc& timing_arc, TransType input_transition, SdfDelay& delay);
  void mergeDelay(SdfDelay& target, SdfDelay& source);
  TimingCheckArc* findSetupCheck(LibertyCell& timing_cell, TimingCheckArc& hold_timing_check_arc);
  TimingArc* findSetupArc(TimingCheckArc& setup_timing_check_arc, TimingArc& hold_timing_arc, TransType data_trans_type);
  void updateDelay(SdfDelay& sdf_delay, std::map<AnalysisType, std::map<TransType, std::map<TransType, double>>>& delay_map, TransType input_trans_type);
  double dataSlew(std::string& pin_name, AnalysisType analysis_type, TransType trans_type);
  double checkClockSlew(Instance& instance, TimingCheckArc& timing_check_arc, AnalysisType analysis_type, TransType data_trans_type);
  double slew(std::string& pin_name, AnalysisType analysis_type, TransType trans_type);
  AnalysisType captureAnalysis(AnalysisType analysis_type);
  Database& _database;
};
}  // namespace ista
