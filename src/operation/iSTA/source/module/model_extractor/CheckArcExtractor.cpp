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
#include "CheckArcExtractor.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <tuple>
#include <utility>

#include "Utility.hpp"

namespace ista {
namespace {
class CheckArcCandidate
{
 public:
  CheckArcCandidate() = default;
  ~CheckArcCandidate() = default;
  // getter
  std::string& get_source_port() { return _source_port; }
  std::string& get_sink_port() { return _sink_port; }
  std::string& get_timing_type() { return _timing_type; }
  double get_rise_constraint() const { return _rise_constraint; }
  double get_fall_constraint() const { return _fall_constraint; }
  bool get_has_rise_constraint() const { return _has_rise_constraint; }
  bool get_has_fall_constraint() const { return _has_fall_constraint; }
  // setter
  void set_source_port(const std::string& source_port) { _source_port = source_port; }
  void set_sink_port(const std::string& sink_port) { _sink_port = sink_port; }
  void set_timing_type(const std::string& timing_type) { _timing_type = timing_type; }
  void set_rise_constraint(const double rise_constraint) { _rise_constraint = rise_constraint; }
  void set_fall_constraint(const double fall_constraint) { _fall_constraint = fall_constraint; }
  void set_has_rise_constraint(const bool has_rise_constraint) { _has_rise_constraint = has_rise_constraint; }
  void set_has_fall_constraint(const bool has_fall_constraint) { _has_fall_constraint = has_fall_constraint; }
  // function

 private:
  std::string _source_port;
  std::string _sink_port;
  std::string _timing_type;
  double _rise_constraint = 0.0;
  double _fall_constraint = 0.0;
  bool _has_rise_constraint = false;
  bool _has_fall_constraint = false;
};
}  // namespace

void CheckArcExtractor::extract(CharacterizedLibrary& tc_lib, AnalysisType analysis_type)
{
  std::map<std::tuple<std::string, std::string, std::string>, CheckArcCandidate> candidate_map;
  for (TimingPath* timing_path : _model_path_query.getTimingPathList(analysis_type)) {
    if (_model_path_query.isInputPort(timing_path->get_start_point()) && _model_path_query.isRegisterEndPoint(timing_path->get_end_point())) {
      std::string timing_type = getTimingCheckType(*timing_path, analysis_type);
      std::string related_clock_port = _model_path_query.getRelatedClockPort(timing_path->get_capture_clock_name());
      if (timing_type.empty() || related_clock_port.empty()) {
        continue;
      }
      std::string sink_port = timing_path->get_start_point();
      std::tuple<std::string, std::string, std::string> candidate_key = std::make_tuple(related_clock_port, sink_port, timing_type);
      CheckArcCandidate& candidate = candidate_map[candidate_key];
      candidate.set_source_port(related_clock_port);
      candidate.set_sink_port(sink_port);
      candidate.set_timing_type(timing_type);
      double constraint = getCheckConstraint(*timing_path, analysis_type);
      if (timing_path->get_trans_type() == TransType::kFall) {
        candidate.set_fall_constraint(candidate.get_has_fall_constraint() ? std::max(candidate.get_fall_constraint(), constraint) : constraint);
        candidate.set_has_fall_constraint(true);
      } else {
        candidate.set_rise_constraint(candidate.get_has_rise_constraint() ? std::max(candidate.get_rise_constraint(), constraint) : constraint);
        candidate.set_has_rise_constraint(true);
      }
    }
  }
  for (std::pair<const std::tuple<std::string, std::string, std::string>, CheckArcCandidate>& candidate_pair : candidate_map) {
    CheckArcCandidate& candidate = candidate_pair.second;
    buildCheckArc(tc_lib, candidate.get_source_port(), candidate.get_sink_port(), candidate.get_timing_type(), candidate.get_rise_constraint(),
                  candidate.get_fall_constraint(), candidate.get_has_rise_constraint(), candidate.get_has_fall_constraint());
  }
}

void CheckArcExtractor::buildCheckArc(CharacterizedLibrary& tc_lib, std::string& source_port, std::string& sink_port, std::string& timing_type,
                                      double rise_constraint, double fall_constraint, bool has_rise_constraint, bool has_fall_constraint)
{
  if (source_port.empty() || sink_port.empty() || timing_type.empty() || (!has_rise_constraint && !has_fall_constraint)) {
    return;
  }
  CharacterizedTimingArc timing_arc;
  timing_arc.set_source_port(source_port);
  timing_arc.set_sink_port(sink_port);
  timing_arc.set_timing_type(timing_type);
  timing_arc.set_is_check_arc(true);
  if (has_rise_constraint) {
    timing_arc.addScalarTable("rise_constraint", rise_constraint);
  }
  if (has_fall_constraint) {
    timing_arc.addScalarTable("fall_constraint", fall_constraint);
  }
  tc_lib.get_timing_arc_list().push_back(timing_arc);
}

std::string CheckArcExtractor::getTimingCheckType(TimingPath& timing_path, AnalysisType analysis_type)
{
  std::string timing_type_suffix = getTimingCheckTypeSuffix(timing_path.get_capture_clock_transition());
  if (analysis_type == AnalysisType::kMax) {
    if (timing_path.get_check_type() == TimingCheckType::kRecovery) {
      return STAUTIL.getString(GetTimingCheckTypeName()(TimingCheckType::kRecovery), "_", timing_type_suffix);
    }
    return STAUTIL.getString(GetTimingCheckTypeName()(TimingCheckType::kSetup), "_", timing_type_suffix);
  }
  if (timing_path.get_check_type() == TimingCheckType::kRemoval) {
    return STAUTIL.getString(GetTimingCheckTypeName()(TimingCheckType::kRemoval), "_", timing_type_suffix);
  }
  return STAUTIL.getString(GetTimingCheckTypeName()(TimingCheckType::kHold), "_", timing_type_suffix);
}

std::string CheckArcExtractor::getTimingCheckTypeSuffix(TransType clock_trans_type)
{
  return GetTransTypeLibName()(clock_trans_type);
}

double CheckArcExtractor::getCheckConstraint(TimingPath& timing_path, AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMin) {
    return std::fabs(timing_path.get_check_time());
  }
  return timing_path.get_check_time();
}
}  // namespace ista
