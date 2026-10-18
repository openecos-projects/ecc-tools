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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "ArcTimingLookup.hpp"

#include "DataManager.hpp"
#include "DCTask.hpp"
#include "DelayCache.hpp"
#include "ParasiticGraph.hpp"
#include "ParasiticNodeName.hpp"
#include "Database.hpp"
#include "Instance.hpp"
#include "Net.hpp"
#include "OutputLoadEstimator.hpp"
#include "ParasiticArnoldiSolver.hpp"
#include "ParasiticDmpSolver.hpp"
#include "Pin.hpp"
#include "PWHeader.hpp"
#include "RcTreeApprox.hpp"
#include "TimingArc.hpp"
#include "TimingCell.hpp"
#include "TimingCellArc.hpp"

namespace ipw {

ArcTimingLookup::ArcTimingLookup(ParasiticArnoldiSolver& arnoldi_solver, ParasiticDmpSolver& dmp_solver)
    : _arnoldi_solver(arnoldi_solver), _dmp_solver(dmp_solver)
{
}

double ArcTimingLookup::calcArcSlew(
    Arc& arc,
    AnalysisType analysis_type,
    TransType input_trans_type,
    TransType output_trans_type,
    double input_slew
)
{
  if (arc.get_type() == ArcType::kNet) {
    return calcNetArcSlew(arc, analysis_type, output_trans_type, input_slew);
  }
  TimingCellArc* timing_cell_arc = getTimingCellArc(arc);
  if (timing_cell_arc != nullptr) {
    return calcTimingCellArcSlew(arc, *timing_cell_arc, analysis_type, input_trans_type, output_trans_type, input_slew);
  }
  return input_slew;
}

double ArcTimingLookup::calcTimingCellArcSlew(
    Arc& arc,
    TimingCellArc& timing_cell_arc,
    AnalysisType analysis_type,
    TransType input_trans_type,
    TransType output_trans_type,
    double input_slew
)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    return input_slew;
  }
  if (!isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return input_slew;
  }
  double output_load = getArcOutputLoad(arc, analysis_type, output_trans_type);
  std::vector<TimingArc*> candidate_arc_list
      = getCandidateTimingArcList(timing_cell_arc, input_trans_type, output_trans_type, arc.get_sink_pin());
  std::vector<double> slew_list;
  for (TimingArc* timing_arc : candidate_arc_list) {
    if (timing_arc->get_slew_table_map().count(output_trans_type) == 0) {
      continue;
    }
    double slew
        = calcTimingArcSlew(arc.get_sink_pin(), *timing_arc, analysis_type, output_trans_type, input_slew, output_load);
    slew_list.push_back(slew);
  }
  if (slew_list.empty()) {
    return input_slew;
  }
  std::ranges::sort(slew_list, std::greater<double>());
  if (analysis_type == AnalysisType::kMin) {
    return slew_list.back();
  }
  return slew_list.front();
}

double ArcTimingLookup::calcTimingCellArcSlew(
    TimingCellArc& timing_cell_arc,
    AnalysisType analysis_type,
    TransType input_trans_type,
    TransType output_trans_type,
    double input_slew,
    double output_load
)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    return input_slew;
  }
  if (!isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return input_slew;
  }
  std::vector<TimingArc*> candidate_arc_list
      = getCandidateTimingArcList(timing_cell_arc, input_trans_type, output_trans_type);
  std::vector<double> slew_list;
  for (TimingArc* timing_arc : candidate_arc_list) {
    if (timing_arc->get_slew_table_map().count(output_trans_type) == 0) {
      continue;
    }
    double slew = calcTimingArcSlewByLoad(*timing_arc, output_trans_type, input_slew, output_load);
    slew_list.push_back(slew);
  }
  if (slew_list.empty()) {
    return input_slew;
  }
  std::ranges::sort(slew_list, std::greater<double>());
  if (analysis_type == AnalysisType::kMin) {
    return slew_list.back();
  }
  return slew_list.front();
}

double ArcTimingLookup::calcTimingCellArcSlew(
    std::string& output_pin,
    TimingCellArc& timing_cell_arc,
    AnalysisType analysis_type,
    TransType input_trans_type,
    TransType output_trans_type,
    double input_slew
)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    return input_slew;
  }
  if (!isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return input_slew;
  }
  double output_load = getOutputPinLoad(output_pin, analysis_type, output_trans_type);
  std::vector<double> slew_list;
  for (TimingArc* timing_arc :
       getCandidateTimingArcList(timing_cell_arc, input_trans_type, output_trans_type, output_pin)) {
    if (timing_arc->get_slew_table_map().count(output_trans_type) == 0) {
      continue;
    }
    double slew = calcTimingArcSlew(output_pin, *timing_arc, analysis_type, output_trans_type, input_slew, output_load);
    slew_list.push_back(slew);
  }
  if (slew_list.empty()) {
    return input_slew;
  }
  std::ranges::sort(slew_list, std::greater<double>());
  if (analysis_type == AnalysisType::kMin) {
    return slew_list.back();
  }
  return slew_list.front();
}

std::vector<TransType> ArcTimingLookup::getOutputTransTypeList(Arc& arc, TransType input_trans_type)
{
  if (arc.get_type() == ArcType::kNet) {
    return {input_trans_type};
  }
  TimingCellArc* timing_cell_arc = getTimingCellArc(arc);
  if (timing_cell_arc == nullptr || !isClockArcTriggerTrans(*timing_cell_arc, input_trans_type)) {
    return {};
  }
  return getOutputTransTypeList(*timing_cell_arc, input_trans_type);
}

std::vector<TransType> ArcTimingLookup::getOutputTransTypeList(
    TimingCellArc& timing_cell_arc,
    TransType input_trans_type
)
{
  std::vector<TransType> output_trans_type_list;
  if (!isUnateArc(timing_cell_arc) || isTwoTypeSenseArcSet(timing_cell_arc) || timing_cell_arc.get_is_clock_arc()) {
    for (TransType output_trans_type : {TransType::kRise, TransType::kFall}) {
      if (isMatchTimingType(timing_cell_arc, output_trans_type)) {
        output_trans_type_list.push_back(output_trans_type);
      }
    }
    return output_trans_type_list;
  }

  TransType output_trans_type = getOutputTransType(timing_cell_arc, input_trans_type);
  if (isMatchTimingType(timing_cell_arc, output_trans_type)) {
    output_trans_type_list.push_back(output_trans_type);
  }
  return output_trans_type_list;
}

bool ArcTimingLookup::isClockArcTriggerTrans(TimingCellArc& timing_cell_arc, TransType input_trans_type)
{
  if (!timing_cell_arc.get_is_clock_arc()) {
    return true;
  }
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    return true;
  }
  TimingArc& timing_arc = timing_cell_arc.get_timing_arc_list().front();
  if (timing_arc.get_trigger_trans_type() == TransType::kFall) {
    return input_trans_type == TransType::kFall;
  }
  if (timing_arc.get_trigger_trans_type() == TransType::kRise) {
    return input_trans_type == TransType::kRise;
  }
  return input_trans_type == TransType::kRise;
}

TimingCellArc* ArcTimingLookup::getTimingCellArc(Arc& arc)
{
  Database& database = PWDM.getDatabase();
  if (arc.get_timing_cell_arc() != nullptr) {
    return arc.get_timing_cell_arc();
  }
  // net 弧的 owner 是 net 名（永远不是实例名），下面的 instance_map 探测必然落空：
  // 直接短路，省掉每条 net 弧每轮一次 935k 项字符串 map 的探测。
  if (arc.get_type() == ArcType::kNet) {
    return nullptr;
  }
  if (database.get_instance_map().count(arc.get_owner_name()) == 0) {
    return nullptr;
  }
  Instance& instance = database.get_instance_map()[arc.get_owner_name()];
  std::map<std::string, TimingCell>& timing_cell_map = database.get_timing_library().get_cell_map();
  if (timing_cell_map.count(instance.get_cell_name()) == 0) {
    return nullptr;
  }
  TimingCell& timing_cell = timing_cell_map[instance.get_cell_name()];
  for (TimingCellArc& timing_cell_arc : timing_cell.get_cell_arc_list()) {
    if (timing_cell_arc.get_source_port() == arc.get_library_source_port()
        && timing_cell_arc.get_sink_port() == arc.get_library_sink_port()) {
      return &timing_cell_arc;
    }
  }
  return nullptr;
}

TransType ArcTimingLookup::getOutputTransType(TimingCellArc& timing_cell_arc, TransType input_trans_type)
{
  if (isNegativeArc(timing_cell_arc)) {
    return input_trans_type == TransType::kRise ? TransType::kFall : TransType::kRise;
  }
  return input_trans_type;
}

std::vector<TimingArc*> ArcTimingLookup::getCandidateTimingArcList(
    TimingCellArc& timing_cell_arc,
    TransType input_trans_type,
    TransType output_trans_type,
    const std::string& output_pin
)
{
  bool is_flip = input_trans_type != output_trans_type;
  std::vector<TimingArc*> candidate_arc_list;
  for (TimingArc& timing_arc : timing_cell_arc.get_timing_arc_list()) {
    if (is_flip && isPositiveArc(timing_arc)) {
      continue;
    }
    if (!is_flip && isNegativeArc(timing_arc)) {
      continue;
    }
    if (!isMatchTimingType(timing_arc, output_trans_type)) {
      continue;
    }
    if (!timing_arc.get_when_expression().get_is_empty() && !output_pin.empty()) {
      Database& database = PWDM.getDatabase();
      auto pin = database.get_pin_map().find(output_pin);
      if (pin != database.get_pin_map().end() && !pin->second.get_is_port()) {
        std::map<std::string, bool> values;
        for (LogicExpressionTerm& term : timing_arc.get_when_expression().get_term_list()) {
          if (term.get_operation_type() != LogicOperationType::kPort) {
            continue;
          }
          auto& effective = database.get_timing_constraint().get_effective_case_analysis_map();
          auto value = effective.find(pin->second.get_instance_name() + ":" + term.get_port_name());
          if (value != effective.end()
              && (value->second == TimingCaseValue::kZero || value->second == TimingCaseValue::kOne)) {
            values[term.get_port_name()] = value->second == TimingCaseValue::kOne;
          }
        }
        if (timing_arc.get_when_expression().evaluate_constant(values) == std::optional<bool>(false)) {
          continue;
        }
      }
    }
    candidate_arc_list.push_back(&timing_arc);
  }
  return candidate_arc_list;
}

bool ArcTimingLookup::isMatchTimingType(TimingArc& timing_arc, TransType trans_type)
{
  return timing_arc.get_delay_table_map().count(trans_type) > 0
         || timing_arc.get_slew_table_map().count(trans_type) > 0;
}

bool ArcTimingLookup::isMatchTimingType(TimingCellArc& timing_cell_arc, TransType trans_type)
{
  for (TimingArc& timing_arc : timing_cell_arc.get_timing_arc_list()) {
    if (isMatchTimingType(timing_arc, trans_type)) {
      return true;
    }
  }
  return false;
}

bool ArcTimingLookup::isPositiveArc(TimingArc& timing_arc)
{
  return timing_arc.get_sense() == TimingArcSense::kPositive || timing_arc.get_sense() == TimingArcSense::kNone;
}

bool ArcTimingLookup::isNegativeArc(TimingArc& timing_arc)
{
  return timing_arc.get_sense() == TimingArcSense::kNegative;
}

bool ArcTimingLookup::isNegativeArc(TimingCellArc& timing_cell_arc)
{
  for (TimingArc& timing_arc : timing_cell_arc.get_timing_arc_list()) {
    if (!isNegativeArc(timing_arc)) {
      return false;
    }
  }
  return true;
}

bool ArcTimingLookup::isUnateArc(TimingCellArc& timing_cell_arc)
{
  for (TimingArc& timing_arc : timing_cell_arc.get_timing_arc_list()) {
    if (timing_arc.get_sense() == TimingArcSense::kNonUnate) {
      return false;
    }
  }
  return true;
}

bool ArcTimingLookup::isTwoTypeSenseArcSet(TimingCellArc& timing_cell_arc)
{
  bool has_positive = false;
  bool has_negative = false;
  for (TimingArc& timing_arc : timing_cell_arc.get_timing_arc_list()) {
    if (isPositiveArc(timing_arc)) {
      has_positive = true;
    } else if (isNegativeArc(timing_arc)) {
      has_negative = true;
    }
  }
  return has_positive && has_negative;
}

double ArcTimingLookup::convertOutputLoad(TimingArc& timing_arc, double output_load)
{
  if (std::abs(timing_arc.get_cap_unit_scale() - 1.0) < PW_ERROR) {
    return output_load;
  }
  return static_cast<int>(std::ceil(output_load * timing_arc.get_cap_unit_scale()));
}

double ArcTimingLookup::calcTimingArcDelay(
    std::string& output_pin,
    TimingArc& timing_arc,
    AnalysisType analysis_type,
    TransType output_trans_type,
    double input_slew,
    double output_load
)
{
  ParasiticDelayResult& timing_result
      = _arnoldi_solver
            .getTimingResult(output_pin, timing_arc, analysis_type, output_trans_type, input_slew, output_load);
  if (timing_result.get_is_valid()) {
    return timing_result.get_gate_delay();
  }
  ParasiticDelayResult& dmp_timing_result
      = _dmp_solver.getTimingResult(output_pin, timing_arc, analysis_type, output_trans_type, input_slew, output_load);
  if (dmp_timing_result.get_is_valid()) {
    return dmp_timing_result.get_gate_delay();
  }
  return calcTimingArcDelayByLoad(timing_arc, output_trans_type, input_slew, output_load);
}

double ArcTimingLookup::calcTimingArcSlew(
    std::string& output_pin,
    TimingArc& timing_arc,
    AnalysisType analysis_type,
    TransType output_trans_type,
    double input_slew,
    double output_load
)
{
  ParasiticDelayResult& timing_result
      = _arnoldi_solver
            .getTimingResult(output_pin, timing_arc, analysis_type, output_trans_type, input_slew, output_load);
  if (timing_result.get_is_valid()) {
    return timing_result.get_driver_slew();
  }
  ParasiticDelayResult& dmp_timing_result
      = _dmp_solver.getTimingResult(output_pin, timing_arc, analysis_type, output_trans_type, input_slew, output_load);
  if (dmp_timing_result.get_is_valid()) {
    return dmp_timing_result.get_driver_slew();
  }
  return calcTimingArcSlewByLoad(timing_arc, output_trans_type, input_slew, output_load);
}

double ArcTimingLookup::calcTimingArcDelayByLoad(
    TimingArc& timing_arc,
    TransType output_trans_type,
    double input_slew,
    double output_load
)
{
  auto delay_table_iter = timing_arc.get_delay_table_map().find(output_trans_type);
  if (delay_table_iter == timing_arc.get_delay_table_map().end()) {
    return 0.0;
  }
  double converted_output_load = convertOutputLoad(timing_arc, output_load);
  double delay = delay_table_iter->second.findValue(
      input_slew * timing_arc.get_time_unit_scale(),
      converted_output_load
  );
  return delay / timing_arc.get_time_unit_scale();
}

double ArcTimingLookup::calcTimingArcSlewByLoad(
    TimingArc& timing_arc,
    TransType output_trans_type,
    double input_slew,
    double output_load
)
{
  auto slew_table_iter = timing_arc.get_slew_table_map().find(output_trans_type);
  if (slew_table_iter == timing_arc.get_slew_table_map().end()) {
    return input_slew;
  }
  double converted_output_load = convertOutputLoad(timing_arc, output_load);
  double slew = slew_table_iter->second.findValue(
      input_slew * timing_arc.get_time_unit_scale(),
      converted_output_load
  );
  return slew / timing_arc.get_time_unit_scale();
}

double ArcTimingLookup::calcNetArcSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew)
{
  Database& database = PWDM.getDatabase();
  if (database.get_parasitic_net_map().find(arc.get_owner_name()) != database.get_parasitic_net_map().end()) {
    return calcParasiticSlew(arc, analysis_type, trans_type, input_slew);
  }
  return input_slew;
}

double ArcTimingLookup::calcParasiticSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew)
{
  Database& database = PWDM.getDatabase();
  auto parasitic_net_iter = database.get_parasitic_net_map().find(arc.get_owner_name());
  if (parasitic_net_iter == database.get_parasitic_net_map().end()) {
    return input_slew;
  }
  ParasiticNet& parasitic_net = parasitic_net_iter->second;
  const ParasiticGraph& graph = getParasiticGraph(parasitic_net);
  const uint32_t source_node_id = getParasiticNodeId(graph, arc.get_source_pin());
  const uint32_t sink_node_id = getParasiticNodeId(graph, arc.get_sink_pin());
  if (source_node_id == kInvalidParasiticNode || sink_node_id == kInvalidParasiticNode) {
    return input_slew;
  }
  // 图里的节点名指向 ParasiticNet 的稳定字符串，这里按引用用，不再每调用拷两份。
  const std::string& source_node_name = *graph.node_name[source_node_id];
  const std::string& sink_node_name = *graph.node_name[sink_node_id];

  std::optional<double> cached_load_slew
      = _arnoldi_solver.getCachedLoadSlew(arc, analysis_type, trans_type, input_slew);
  if (cached_load_slew) {
    return *cached_load_slew;
  }
  cached_load_slew = _dmp_solver.getCachedLoadSlew(arc, analysis_type, trans_type, input_slew);
  if (cached_load_slew) {
    return *cached_load_slew;
  }
  std::optional<double> input_port_slew
      = _arnoldi_solver
            .getInputPortSlew(parasitic_net, source_node_name, sink_node_name, analysis_type, trans_type, input_slew);
  if (input_port_slew) {
    return *input_port_slew;
  }

  DelayCache::ParasiticCornerState& corner_state
      = DelayCache::threadCache().parasiticCornerState(parasitic_net.get_net_name(), analysis_type, trans_type);
  buildParasiticDelayMap(graph, corner_state, source_node_id, analysis_type, trans_type);
  if (corner_state.node_impulse_valid[sink_node_id] == 0) {
    return input_slew;
  }
  double impulse = corner_state.node_impulse[sink_node_id];
  double output_slew = std::sqrt(input_slew * input_slew + impulse);
  if (input_slew < 0.0) {
    return -output_slew;
  }
  return output_slew;
}

bool ArcTimingLookup::calcDrivingCellSlew(
    std::string& output_pin,
    TimingCellArc& timing_cell_arc,
    AnalysisType analysis_type,
    TransType output_trans_type,
    double input_transition_rise,
    double input_transition_fall,
    double& output_slew
)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    output_slew = output_trans_type == TransType::kRise ? input_transition_rise : input_transition_fall;
    return true;
  }

  const double output_load = getOutputPinLoad(output_pin, analysis_type, output_trans_type);
  bool has_result = false;
  double selected_delay = 0.0;
  double selected_slew = 0.0;
  for (TransType input_trans_type : {TransType::kRise, TransType::kFall}) {
    const double input_slew = input_trans_type == TransType::kRise ? input_transition_rise : input_transition_fall;
    for (TimingArc* timing_arc :
         getCandidateTimingArcList(timing_cell_arc, input_trans_type, output_trans_type, output_pin)) {
      const bool has_delay_table = timing_arc->get_delay_table_map().contains(output_trans_type);
      const bool has_slew_table = timing_arc->get_slew_table_map().contains(output_trans_type);
      if (!has_delay_table && !has_slew_table) {
        continue;
      }

      double load_delay = 0.0;
      if (has_delay_table) {
        const double loaded_delay
            = calcTimingArcDelay(output_pin, *timing_arc, analysis_type, output_trans_type, input_slew, output_load);
        const double intrinsic_delay = calcTimingArcDelayByLoad(*timing_arc, output_trans_type, input_slew, 0.0);
        load_delay = loaded_delay - intrinsic_delay;
      }
      const double candidate_output_slew
          = has_slew_table
                ? calcTimingArcSlew(output_pin, *timing_arc, analysis_type, output_trans_type, input_slew, output_load)
                : input_slew;
      if (!has_result || (analysis_type == AnalysisType::kMin && load_delay < selected_delay)
          || (analysis_type == AnalysisType::kMax && load_delay > selected_delay)) {
        selected_delay = load_delay;
        selected_slew = candidate_output_slew;
        has_result = true;
      }
    }
  }
  if (!has_result) {
    return false;
  }
  output_slew = selected_slew;
  return true;
}

void ArcTimingLookup::calculateArc(DCTask& dc_task)
{
  Arc* arc = dc_task.get_arc();
  if (arc == nullptr) {
    return;
  }
  TimingCellArc* timing_cell_arc = getTimingCellArc(*arc);
  if (timing_cell_arc != nullptr && !isClockArcTriggerTrans(*timing_cell_arc, dc_task.get_input_trans_type())) {
    return;
  }

  const double output_slew_input_slew
      = dc_task.get_has_output_slew_input_slew() ? dc_task.get_output_slew_input_slew() : dc_task.get_input_slew();
  dc_task.set_output_slew(calcArcSlew(
      *arc,
      dc_task.get_analysis_type(),
      dc_task.get_input_trans_type(),
      dc_task.get_output_trans_type(),
      output_slew_input_slew
  ));
  dc_task.set_is_valid(true);
}

void ArcTimingLookup::calculateTimingCellArc(DCTask& dc_task)
{
  TimingCellArc* timing_cell_arc = dc_task.get_timing_cell_arc();
  if (timing_cell_arc == nullptr || !isClockArcTriggerTrans(*timing_cell_arc, dc_task.get_input_trans_type())) {
    return;
  }

  dc_task.set_output_slew(calcTimingCellArcSlew(
      dc_task.get_output_pin(),
      *timing_cell_arc,
      dc_task.get_analysis_type(),
      dc_task.get_input_trans_type(),
      dc_task.get_output_trans_type(),
      dc_task.get_input_slew()
  ));
  dc_task.set_is_valid(true);
}

}  // namespace ipw
