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
#include "DelayArcExtractor.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <tuple>
#include <utility>

namespace ista {
namespace {
class DelayArcCandidate
{
 public:
  DelayArcCandidate() = default;
  ~DelayArcCandidate() = default;
  // getter
  std::string& get_source_port() { return _source_port; }
  std::string& get_sink_port() { return _sink_port; }
  std::string& get_timing_type() { return _timing_type; }
  std::string& get_timing_sense() { return _timing_sense; }
  double get_rise_delay() const { return _rise_delay; }
  double get_fall_delay() const { return _fall_delay; }
  bool get_has_rise_delay() const { return _has_rise_delay; }
  bool get_has_fall_delay() const { return _has_fall_delay; }
  // setter
  void set_source_port(const std::string& source_port) { _source_port = source_port; }
  void set_sink_port(const std::string& sink_port) { _sink_port = sink_port; }
  void set_timing_type(const std::string& timing_type) { _timing_type = timing_type; }
  void set_timing_sense(const std::string& timing_sense) { _timing_sense = timing_sense; }
  void set_rise_delay(const double rise_delay) { _rise_delay = rise_delay; }
  void set_fall_delay(const double fall_delay) { _fall_delay = fall_delay; }
  void set_has_rise_delay(const bool has_rise_delay) { _has_rise_delay = has_rise_delay; }
  void set_has_fall_delay(const bool has_fall_delay) { _has_fall_delay = has_fall_delay; }
  // function

 private:
  std::string _source_port;
  std::string _sink_port;
  std::string _timing_type;
  std::string _timing_sense;
  double _rise_delay = 0.0;
  double _fall_delay = 0.0;
  bool _has_rise_delay = false;
  bool _has_fall_delay = false;
};
}  // namespace

void DelayArcExtractor::extract(CharacterizedLibrary& tc_lib, AnalysisType analysis_type)
{
  std::map<std::tuple<std::string, std::string, std::string>, DelayArcCandidate> candidate_map;
  for (TimingPath* timing_path : _model_path_query.getTimingPathList(analysis_type)) {
    if (_model_path_query.isOutputPort(timing_path->get_end_point())) {
      std::string source_port = getDelayArcRelatedPin(*timing_path);
      if (source_port.empty()) {
        continue;
      }
      std::string sink_port = timing_path->get_end_point();
      std::string timing_type = getDelayArcTimingType(*timing_path);
      std::tuple<std::string, std::string, std::string> candidate_key = std::make_tuple(source_port, sink_port, timing_type);
      DelayArcCandidate& candidate = candidate_map[candidate_key];
      candidate.set_source_port(source_port);
      candidate.set_sink_port(sink_port);
      candidate.set_timing_type(timing_type);
      candidate.set_timing_sense(getDelayArcTimingSense(*timing_path));
      double delay = getDelayArcValue(*timing_path);
      if (timing_path->get_trans_type() == TransType::kFall) {
        candidate.set_fall_delay(getWorseDelay(candidate.get_fall_delay(), delay, candidate.get_has_fall_delay(), analysis_type));
        candidate.set_has_fall_delay(true);
      } else {
        candidate.set_rise_delay(getWorseDelay(candidate.get_rise_delay(), delay, candidate.get_has_rise_delay(), analysis_type));
        candidate.set_has_rise_delay(true);
      }
    }
  }
  for (std::pair<const std::tuple<std::string, std::string, std::string>, DelayArcCandidate>& candidate_pair : candidate_map) {
    DelayArcCandidate& candidate = candidate_pair.second;
    buildDelayArc(tc_lib, candidate.get_source_port(), candidate.get_sink_port(), candidate.get_timing_type(), candidate.get_timing_sense(),
                  candidate.get_rise_delay(), candidate.get_fall_delay(), candidate.get_has_rise_delay(), candidate.get_has_fall_delay());
  }
}

double DelayArcExtractor::getWorseDelay(double current_delay, double delay, bool has_delay, AnalysisType analysis_type)
{
  if (!has_delay) {
    return delay;
  }
  if (analysis_type == AnalysisType::kMin) {
    return std::min(current_delay, delay);
  }
  return std::max(current_delay, delay);
}

void DelayArcExtractor::buildDelayArc(CharacterizedLibrary& tc_lib, std::string& source_port, std::string& sink_port, std::string& timing_type,
                                      std::string& timing_sense, double rise_delay, double fall_delay, bool has_rise_delay, bool has_fall_delay)
{
  if (source_port.empty() || sink_port.empty() || timing_type.empty() || (!has_rise_delay && !has_fall_delay)) {
    return;
  }
  CharacterizedTimingArc timing_arc;
  timing_arc.set_source_port(source_port);
  timing_arc.set_sink_port(sink_port);
  timing_arc.set_timing_type(timing_type);
  timing_arc.set_timing_sense(timing_sense);
  if (has_rise_delay) {
    timing_arc.addScalarTable("cell_rise", rise_delay);
    timing_arc.addScalarTable("rise_transition", 0.0);
  }
  if (has_fall_delay) {
    timing_arc.addScalarTable("cell_fall", fall_delay);
    timing_arc.addScalarTable("fall_transition", 0.0);
  }
  tc_lib.get_timing_arc_list().push_back(timing_arc);
}

std::string DelayArcExtractor::getDelayArcRelatedPin(TimingPath& timing_path)
{
  if (_model_path_query.isInputPort(timing_path.get_start_point())) {
    return timing_path.get_start_point();
  }
  if (_model_path_query.isRegisterStartPoint(timing_path.get_start_point())) {
    return _model_path_query.getRelatedClockPort(timing_path.get_clock_name());
  }
  return "";
}

std::string DelayArcExtractor::getDelayArcTimingType(TimingPath& timing_path)
{
  if (_model_path_query.isRegisterStartPoint(timing_path.get_start_point())) {
    return GetTransTypeLibEdgeName()(getRegisterClockTransType(timing_path));
  }
  return "combinational";
}

TransType DelayArcExtractor::getRegisterClockTransType(TimingPath& timing_path)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[timing_path.get_start_point()];
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  if (instance.get_clock_to_q_arc().get_timing_arc_list().empty()) {
    return TransType::kRise;
  }
  TimingArc& timing_arc = instance.get_clock_to_q_arc().get_timing_arc_list().front();
  if (timing_arc.get_trigger_trans_type() == TransType::kFall) {
    return TransType::kFall;
  }
  return TransType::kRise;
}

std::string DelayArcExtractor::getDelayArcTimingSense(TimingPath& timing_path)
{
  if (timing_path.get_point_list().empty()) {
    return "positive_unate";
  }
  if (timing_path.get_trans_type() == timing_path.get_point_list().front().get_trans_type()) {
    return "positive_unate";
  }
  return "negative_unate";
}

double DelayArcExtractor::getDelayArcValue(TimingPath& timing_path)
{
  return timing_path.get_path_delay();
}
}  // namespace ista
