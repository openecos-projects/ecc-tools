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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "SignalPropagator.hpp"

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "SharedDriverResultCache.hpp"

namespace ipw {

// public

void SignalPropagator::initInst()
{
  if (_sp_instance == nullptr) {
    _sp_instance = new SignalPropagator();
  }
}

SignalPropagator& SignalPropagator::getInst()
{
  if (_sp_instance == nullptr) {
    PWLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_sp_instance;
}

void SignalPropagator::destroyInst()
{
  if (_sp_instance != nullptr) {
    delete _sp_instance;
    _sp_instance = nullptr;
  }
}

// function

void SignalPropagator::propagate()
{
  Monitor monitor;
  PWLOG.info(Loc::current(), "Starting...");

  seedSignalSlewList();
  propagateSignalSlew();

  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

SignalPropagator* SignalPropagator::_sp_instance = nullptr;

bool SignalPropagator::isDisableArc(Arc& arc)
{
  return arc.get_is_disable_arc() || arc.get_is_loop_disable();
}

bool SignalPropagator::shouldStopSignalSlewPropagation(Arc& arc)
{
  if (arc.get_type() != ArcType::kNet) {
    return false;
  }
  const uint32_t sink_pin_index = arc.get_sink_pin_index();
  if (!isSequentialClockPin(sink_pin_index)) {
    return false;
  }
  // 必须读实时的 TimingPoint：ClockPropagator 在建图之后才 mark clock point，
  // PinStaticContext 里的 is_clock_point 只是那时的快照。
  TimingPoint* timing_point = findTimingPoint(sink_pin_index);
  return timing_point != nullptr && timing_point->get_is_clock_point();
}

TimingPoint* SignalPropagator::findTimingPoint(uint32_t pin_index)
{
  const std::vector<Database::PinStaticContext>& pin_static_context_list
      = PWDM.getDatabase().get_pin_static_context_list();
  if (pin_index >= pin_static_context_list.size()) {
    return nullptr;
  }
  return pin_static_context_list[pin_index].timing_point;
}

bool SignalPropagator::isSequentialClockPin(uint32_t pin_index)
{
  const std::vector<Database::PinStaticContext>& pin_static_context_list
      = PWDM.getDatabase().get_pin_static_context_list();
  if (pin_index >= pin_static_context_list.size()) {
    return false;
  }
  const Database::PinStaticContext& pin_static_context = pin_static_context_list[pin_index];
  if (pin_static_context.pin == nullptr || pin_static_context.instance == nullptr
      || pin_static_context.full_name == nullptr || pin_static_context.pin->get_is_port()) {
    return false;
  }
  Instance& instance = *pin_static_context.instance;
  return instance.get_is_sequential() && *pin_static_context.full_name == instance.get_clock_pin_name();
}

bool SignalPropagator::isSequentialClockPin(std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  auto pin_iter = database.get_pin_map().find(pin_name);
  if (pin_iter == database.get_pin_map().end()) {
    return false;
  }
  return isSequentialClockPin(static_cast<uint32_t>(pin_iter->second.get_index()));
}

bool SignalPropagator::hasIncomingPhysicalSlewArc(uint32_t pin_index)
{
  Database& database = PWDM.getDatabase();
  const std::vector<std::vector<uint32_t>>& incoming_arc_index_list = database.get_incoming_arc_index_list();
  if (pin_index >= incoming_arc_index_list.size()) {
    return false;
  }
  for (uint32_t arc_idx : incoming_arc_index_list[pin_index]) {
    Arc& arc = database.get_arc_list()[arc_idx];
    if (arc.get_type() == ArcType::kNet && !isDisableArc(arc)) {
      return true;
    }
  }
  return false;
}

bool SignalPropagator::hasIncomingPhysicalSlewArc(std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  auto pin_iter = database.get_pin_map().find(pin_name);
  if (pin_iter == database.get_pin_map().end()) {
    return false;
  }
  return hasIncomingPhysicalSlewArc(static_cast<uint32_t>(pin_iter->second.get_index()));
}

void SignalPropagator::seedSignalSlewList()
{
  for (std::string& source_pin : PWDM.getDatabase().get_source_pin_list()) {
    seedSignalSlew(source_pin, AnalysisType::kMax);
    seedSignalSlew(source_pin, AnalysisType::kMin);
  }
}

void SignalPropagator::seedSignalSlew(std::string& source_pin, AnalysisType analysis_type)
{
  seedSignalSlew(source_pin, analysis_type, TransType::kRise);
  seedSignalSlew(source_pin, analysis_type, TransType::kFall);
}

void SignalPropagator::seedSignalSlew(std::string& source_pin, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  if (!database.get_timing_constraint().allowsTransition(
          source_pin,
          trans_type,
          database.get_pin_map().at(source_pin).get_direction() == PinDirection::kInout
      )) {
    return;
  }
  TimingPoint& timing_point = database.getDriverTimingPoint(source_pin);
  if (isSequentialClockPin(source_pin) && hasIncomingPhysicalSlewArc(source_pin)
      && (timing_point.get_clock_slew_map().count(analysis_type) == 0
          || timing_point.get_clock_slew_map()[analysis_type].count(trans_type) == 0)) {
    return;
  }
  timing_point.get_data_slew_map()[analysis_type][trans_type] = getSourcePinSlew(source_pin, analysis_type, trans_type);
}

double SignalPropagator::getSourcePinSlew(std::string& source_pin, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[source_pin];
  if (pin.get_is_port()) {
    std::map<std::string, TimingPortConstraint>& port_constraint_map
        = database.get_timing_constraint().get_port_constraint_map();
    if (port_constraint_map.count(source_pin) == 0) {
      return 0.0;
    }
    TimingPortConstraint& port_constraint = port_constraint_map[source_pin];
    if (port_constraint.get_has_driving_cell(analysis_type, trans_type)) {
      std::optional<double> driving_cell_slew = getDrivingCellSlew(source_pin, analysis_type, trans_type);
      return driving_cell_slew ? *driving_cell_slew : 0.0;
    }
    if (port_constraint.get_has_input_transition(analysis_type, trans_type)) {
      return port_constraint.get_input_transition(analysis_type, trans_type);
    }
    return 0.0;
  }
  if (database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return 0.0;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  if (instance.get_is_sequential() && source_pin == instance.get_clock_pin_name()) {
    TimingCellArc* clock_to_q_arc = instance.get_clock_to_q_arc();
    TransType clock_trans_type = clock_to_q_arc == nullptr ? TransType::kRise : getClockTransType(*clock_to_q_arc);
    return getClockSlew(instance.get_clock_pin_name(), analysis_type, clock_trans_type);
  }
  if (instance.get_is_sequential() && source_pin == instance.get_output_pin_name()) {
    TimingCellArc* clock_to_q_arc = instance.get_clock_to_q_arc();
    if (clock_to_q_arc == nullptr) {
      return 0.0;
    }
    TransType clock_trans_type = getClockTransType(*clock_to_q_arc);
    DCTask dc_task;
    dc_task.set_timing_cell_arc(clock_to_q_arc);
    dc_task.set_output_pin(source_pin);
    dc_task.set_analysis_type(analysis_type);
    dc_task.set_input_trans_type(clock_trans_type);
    dc_task.set_output_trans_type(trans_type);
    dc_task.set_input_slew(getClockSlew(instance.get_clock_pin_name(), analysis_type, clock_trans_type));
    PWDC.calculate(dc_task);
    if (dc_task.get_is_valid()) {
      return dc_task.get_output_slew();
    }
  }
  return 0.0;
}

std::optional<double> SignalPropagator::getDrivingCellSlew(
    std::string& source_pin,
    AnalysisType analysis_type,
    TransType trans_type
)
{
  Database& database = PWDM.getDatabase();
  std::map<std::string, TimingPortConstraint>& port_constraint_map
      = database.get_timing_constraint().get_port_constraint_map();
  if (!port_constraint_map.contains(source_pin)) {
    return std::nullopt;
  }
  TimingDrivingCell* driving_cell = port_constraint_map.at(source_pin).get_driving_cell(analysis_type, trans_type);
  if (driving_cell == nullptr) {
    return std::nullopt;
  }
  std::map<std::string, TimingCell>& timing_cell_map = database.get_timing_library().get_cell_map();
  if (!timing_cell_map.contains(driving_cell->get_cell_name())) {
    return std::nullopt;
  }
  TimingCell& timing_cell = timing_cell_map.at(driving_cell->get_cell_name());
  if (timing_cell.get_library_name() != driving_cell->get_library_name()) {
    return std::nullopt;
  }
  for (TimingCellArc& timing_cell_arc : timing_cell.get_cell_arc_list()) {
    if (timing_cell_arc.get_source_port() != driving_cell->get_from_pin()
        || timing_cell_arc.get_sink_port() != driving_cell->get_to_pin()) {
      continue;
    }
    double output_slew = 0.0;
    if (PWDC.calculateDrivingCell(
            source_pin,
            timing_cell_arc,
            analysis_type,
            trans_type,
            driving_cell->get_input_transition_rise(),
            driving_cell->get_input_transition_fall(),
            output_slew
        )) {
      return output_slew;
    }
  }
  return std::nullopt;
}

TransType SignalPropagator::getClockTransType(TimingCellArc& timing_cell_arc)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    return TransType::kRise;
  }
  TimingArc& timing_arc = timing_cell_arc.get_timing_arc_list().front();
  if (timing_arc.get_trigger_trans_type() != TransType::kNone) {
    return timing_arc.get_trigger_trans_type();
  }
  return TransType::kRise;
}

double SignalPropagator::getClockSlew(std::string& pin_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  if (database.get_timing_point_map().count(pin_name) == 0) {
    return 0.0;
  }
  TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
  if (timing_point.get_clock_slew_map().count(analysis_type) == 0
      || timing_point.get_clock_slew_map()[analysis_type].count(trans_type) == 0) {
    return 0.0;
  }
  return timing_point.get_clock_slew_map()[analysis_type][trans_type];
}

double SignalPropagator::getClockSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type)
{
  std::map<AnalysisType, std::map<TransType, double>>& clock_slew_map = timing_point.get_clock_slew_map();
  auto analysis_iter = clock_slew_map.find(analysis_type);
  if (analysis_iter == clock_slew_map.end()) {
    return 0.0;
  }
  auto trans_iter = analysis_iter->second.find(trans_type);
  return trans_iter == analysis_iter->second.end() ? 0.0 : trans_iter->second;
}

// 按"层桶"推进：桶内每个 pin 的源斜率都已定值，于是①可以并行算，②必须按原序串行提交。
// 桶 = signal_order 上的连续区间（buildSignalLevel 保证），提交顺序与旧逐 pin 实现完全一致。
void SignalPropagator::propagateSignalSlew()
{
  Database& database = PWDM.getDatabase();
  const std::vector<int32_t>& pin_order_list = database.get_graph_order_list();
  if (pin_order_list.empty()) {
    return;
  }
  const std::vector<uint32_t>& level_begin_list = database.get_signal_level_begin_list();
  // 没有层信息时退化成"一 pin 一桶"：与旧实现等价，只是没有并行度。
  std::vector<uint32_t> fallback_begin_list;
  const std::vector<uint32_t>* bucket_begin_list = &level_begin_list;
  if (level_begin_list.size() < 2) {
    fallback_begin_list.reserve(pin_order_list.size() + 1);
    for (std::size_t idx = 0; idx <= pin_order_list.size(); idx++) {
      fallback_begin_list.push_back(static_cast<uint32_t>(idx));
    }
    bucket_begin_list = &fallback_begin_list;
  }
  const int32_t thread_num = std::max(PWDM.getConfig().thread_number, 1);

  // 每个 pin 一个结果槽；同桶内不同 pin 槽位不同，所以并行写各自槽位无竞争。
  std::vector<std::vector<SlewUpdate>> update_list;
  for (std::size_t bucket = 0; bucket + 1 < bucket_begin_list->size(); bucket++) {
    const std::size_t begin = (*bucket_begin_list)[bucket];
    const std::size_t end = (*bucket_begin_list)[bucket + 1];
    const std::int64_t bucket_pin_num = static_cast<std::int64_t>(end) - static_cast<std::int64_t>(begin);
    if (bucket_pin_num <= 0) {
      continue;
    }
    update_list.resize(static_cast<std::size_t>(bucket_pin_num));
#pragma omp parallel for schedule(dynamic, 32) num_threads(thread_num)
    for (std::int64_t i = 0; i < bucket_pin_num; i++) {
      const std::size_t in_bucket_idx = static_cast<std::size_t>(i);
      std::vector<SlewUpdate>& updates = update_list[in_bucket_idx];
      updates.clear();
      computeSignalSlewPin(pin_order_list[begin + in_bucket_idx], updates);
    }
    // 串行提交：严格按 signal_order 原序逐 pin 回放，保持同一 sink 的更新次序不变。
    for (std::size_t in_bucket_idx = 0; in_bucket_idx < update_list.size(); in_bucket_idx++) {
      applySignalSlewList(update_list[in_bucket_idx]);
    }
  }
}

void SignalPropagator::computeSignalSlewPin(uint32_t pin_index, std::vector<SlewUpdate>& updates)
{
  Database& database = PWDM.getDatabase();
  for (std::size_t arc_idx : database.get_graph_vertex_list()[pin_index].get_outgoing_arc_list()) {
    if (isDisableArc(database.get_arc_list()[arc_idx])) {
      continue;
    }
    computeSignalSlewArc(arc_idx, updates);
  }
}

void SignalPropagator::computeSignalSlewArc(uint32_t arc_idx, std::vector<SlewUpdate>& updates)
{
  computeSignalSlewArc(arc_idx, AnalysisType::kMax, TransType::kRise, updates);
  computeSignalSlewArc(arc_idx, AnalysisType::kMax, TransType::kFall, updates);
  computeSignalSlewArc(arc_idx, AnalysisType::kMin, TransType::kRise, updates);
  computeSignalSlewArc(arc_idx, AnalysisType::kMin, TransType::kFall, updates);
}

// 阶段①：纯计算。除了 thread_local 的延时缓存，不写任何共享状态。
void SignalPropagator::computeSignalSlewArc(
    uint32_t arc_idx,
    AnalysisType analysis_type,
    TransType input_trans_type,
    std::vector<SlewUpdate>& updates
)
{
  Database& database = PWDM.getDatabase();
  Arc& arc = database.get_arc_list()[arc_idx];
  if (isDisableArc(arc) || shouldStopSignalSlewPropagation(arc)
      || !database.get_timing_constraint().allowsTransition(
          arc.get_source_pin(),
          input_trans_type,
          arc.get_type() == ArcType::kNet
              && database.get_pin_map().at(arc.get_source_pin()).get_direction() == PinDirection::kInout
      )) {
    return;
  }
  TimingPoint* source_point = &database.getSourceTimingPoint(arc);
  if (source_point == nullptr || !hasSignalSlew(*source_point, analysis_type, input_trans_type)) {
    return;
  }
  const uint32_t sink_pin_index = arc.get_sink_pin_index();
  if (findTimingPoint(sink_pin_index) == nullptr) {
    return;
  }
  for (TransType output_trans_type : getOutputTransTypeList(arc, input_trans_type)) {
    if (!database.get_timing_constraint().allowsTransition(
            arc.get_sink_pin(),
            output_trans_type,
            arc.get_type() == ArcType::kCell
                && database.get_pin_map().at(arc.get_sink_pin()).get_direction() == PinDirection::kInout
        )) {
      continue;
    }
    DCTask dc_task;
    dc_task.set_arc(&arc);
    dc_task.set_analysis_type(analysis_type);
    dc_task.set_input_trans_type(input_trans_type);
    dc_task.set_output_trans_type(output_trans_type);
    dc_task.set_input_slew(getSignalSlew(*source_point, analysis_type, input_trans_type));
    if (isSequentialClockPin(arc.get_source_pin_index())) {
      dc_task.set_output_slew_input_slew(getClockSlew(*source_point, analysis_type, input_trans_type));
    }
    PWDC.calculate(dc_task);
    if (!dc_task.get_is_valid()) {
      continue;
    }
    updates.push_back(SlewUpdate{
        static_cast<uint32_t>(arc.get_sink_vertex_idx()),
        analysis_type,
        output_trans_type,
        dc_task.get_output_slew()
    });
  }
}

// 阶段②：唯一写 TimingPoint 的地方，按 updates 的记录序回放。
void SignalPropagator::applySignalSlewList(const std::vector<SlewUpdate>& updates)
{
  for (const SlewUpdate& update : updates) {
    TimingPoint* timing_point = &PWDM.getDatabase().get_graph_vertex_list()[update.sink_vertex_idx].get_timing_point();
    if (timing_point == nullptr) {
      continue;
    }
    updateSignalSlew(*timing_point, update.analysis_type, update.trans_type, update.slew);
  }
}

void SignalPropagator::updateSignalSlew(
    TimingPoint& timing_point,
    AnalysisType analysis_type,
    TransType trans_type,
    double signal_slew
)
{
  if (!hasSignalSlew(timing_point, analysis_type, trans_type)
      || isBetterSlew(signal_slew, timing_point.get_data_slew_map()[analysis_type][trans_type], analysis_type)) {
    timing_point.get_data_slew_map()[analysis_type][trans_type] = signal_slew;
  }
}

bool SignalPropagator::hasSignalSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type)
{
  return timing_point.get_data_slew_map().count(analysis_type) > 0
         && timing_point.get_data_slew_map()[analysis_type].count(trans_type) > 0;
}

double SignalPropagator::getSignalSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type)
{
  if (!hasSignalSlew(timing_point, analysis_type, trans_type)) {
    return 0.0;
  }
  return timing_point.get_data_slew_map()[analysis_type][trans_type];
}

bool SignalPropagator::isBetterSlew(double candidate_slew, double current_slew, AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMin) {
    return candidate_slew < current_slew - PW_ERROR;
  }
  return candidate_slew > current_slew + PW_ERROR;
}

std::vector<TransType> SignalPropagator::getOutputTransTypeList(Arc& arc, TransType input_trans_type)
{
  return PWDC.getOutputTransTypeList(arc, input_trans_type);
}

}  // namespace ipw
