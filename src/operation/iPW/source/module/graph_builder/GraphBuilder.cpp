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
#include "GraphBuilder.hpp"

#include "DataManager.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

namespace ipw {

// public

void GraphBuilder::initInst()
{
  if (_gb_instance == nullptr) {
    _gb_instance = new GraphBuilder();
  }
}

GraphBuilder& GraphBuilder::getInst()
{
  if (_gb_instance == nullptr) {
    PWLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_gb_instance;
}

void GraphBuilder::destroyInst()
{
  if (_gb_instance != nullptr) {
    delete _gb_instance;
    _gb_instance = nullptr;
  }
}

// function

void GraphBuilder::build()
{
  Monitor monitor;
  PWLOG.info(Loc::current(), "Starting...");
  buildSignalPointList();
  normalizePinDirectionByTimingCell();
  buildCellArcs();
  buildInoutPinDirectionByGraph();
  buildNetDriverLoadList();
  buildNetArcs();
  buildGraphVertexList();
  applyCaseAnalysis();
  buildSourcePinList();
  // TODO:
  // break loop and build signal order could merge
  // so we can save the full graph scan
  breakLoopArcList();
  buildSignalOrder();
  buildPinOutputFlagMap();
  buildPowerIndex();
  // 依赖 buildPowerIndex 摊平出来的 signal_order_pin_index_list / outgoing_arc_index_list
  buildSignalLevel();
  printLoopInfo();
  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// 按 signal_order 做一趟最长路径，得到每 pin 的层号，并把"层桶"边界记下来。
// FIFO Kahn 产出的 signal_order 本身按层非降序，所以桶 = signal_order 上的连续区间；
// 断言不成立时退化成"一 pin 一桶"（等价于原来的逐 pin 串行，只是没有并行度）。
void GraphBuilder::buildSignalLevel()
{
  Database& database = PWDM.getDatabase();
  const std::vector<int32_t>& pin_order = database.get_graph_order_list();
  std::vector<uint32_t>& level_list = database.get_signal_level_list();
  std::vector<uint32_t>& level_begin_list = database.get_signal_level_begin_list();

  level_list.assign(database.get_graph_vertex_list().size(), 0);
  for (int32_t pin_index : pin_order) {
    const uint32_t sink_level = level_list[pin_index] + 1;
    for (std::size_t arc_idx : database.get_graph_vertex_list()[pin_index].get_outgoing_arc_list()) {
      Arc& arc = database.get_arc_list()[arc_idx];
      if (isDisableArc(arc)) {
        continue;
      }
      const uint32_t sink_pin_index = arc.get_sink_vertex_idx();
      if (sink_pin_index < level_list.size() && level_list[sink_pin_index] < sink_level) {
        level_list[sink_pin_index] = sink_level;
      }
    }
  }

  bool is_monotone = true;
  uint32_t previous_level = 0;
  for (std::size_t idx = 0; idx < pin_order.size(); idx++) {
    const uint32_t current_level = level_list[pin_order[idx]];
    if (idx > 0 && current_level < previous_level) {
      is_monotone = false;
      break;
    }
    previous_level = current_level;
  }

  level_begin_list.clear();
  if (is_monotone) {
    uint32_t bucket_level = 0;
    level_begin_list.push_back(0);
    for (std::size_t idx = 0; idx < pin_order.size(); idx++) {
      const uint32_t current_level = level_list[pin_order[idx]];
      if (current_level != bucket_level) {
        bucket_level = current_level;
        level_begin_list.push_back(static_cast<uint32_t>(idx));
      }
    }
  } else {
    // 兜底：保证每个桶只有一个 pin，任何情况下都与逐 pin 串行等价
    level_begin_list.reserve(pin_order.size() + 1);
    for (std::size_t idx = 0; idx <= pin_order.size(); idx++) {
      level_begin_list.push_back(static_cast<uint32_t>(idx));
    }
    PWLOG.warn(Loc::current(), "Signal order is not level monotone; falling back to per-pin scheduling.");
  }
  level_begin_list.push_back(static_cast<uint32_t>(pin_order.size()));
  PWLOG.info(
      Loc::current(),
      "Signal propagation: ",
      pin_order.size(),
      " pin(s) in ",
      level_begin_list.size() - 1,
      " bucket(s), max level ",
      is_monotone ? level_list.empty() ? 0 : *std::max_element(level_list.begin(), level_list.end()) : 0
  );
}

void GraphBuilder::applyCaseAnalysis()
{
  Database& database = PWDM.getDatabase();
  TimingConstraint& constraint = database.get_timing_constraint();
  constraint.get_effective_case_analysis_map() = constraint.get_case_analysis_map();
  constraint.get_driver_case_analysis_map().clear();
  for (auto& [pin_name, pin] : database.get_pin_map()) {
    pin.set_is_driver_disabled(false);
    if (pin.get_constant_value() != TimingCaseValue::kNone) {
      constraint.get_effective_case_analysis_map().try_emplace(pin_name, pin.get_constant_value());
      if (pin.get_direction() == PinDirection::kInout) {
        constraint.get_driver_case_analysis_map()[pin_name] = pin.get_constant_value();
      }
    }
    auto value = constraint.get_case_analysis_map().find(pin_name);
    if (pin.get_direction() == PinDirection::kInout && value != constraint.get_case_analysis_map().end()) {
      constraint.get_driver_case_analysis_map()[pin_name] = value->second;
    }
  }
  propagateCaseValue();
  disableCaseInsensitiveArc();
}

void GraphBuilder::propagateCaseValue()
{
  Database& database = PWDM.getDatabase();
  TimingConstraint& constraint = database.get_timing_constraint();
  bool changed = true;
  for (std::size_t iteration = 0; changed && iteration <= database.get_pin_map().size(); ++iteration) {
    changed = false;
    for (auto& [net_name, net] : database.get_net_map()) {
      for (const std::string& load : net.get_load_pin_list()) {
        const std::optional<TimingCaseValue> value = getNetDriverCaseValue(constraint, net, load);
        if (value.has_value()) {
          changed = updateEffectiveCaseValue(constraint, load, *value) || changed;
        }
      }
    }

    for (auto& [instance_name, instance] : database.get_instance_map()) {
      const auto cell = database.get_timing_library().get_cell_map().find(instance.get_cell_name());
      if (cell == database.get_timing_library().get_cell_map().end()) {
        continue;
      }
      const std::map<std::string, bool> values = getInstanceConstantInputMap(instance);
      for (auto& [port_name, port] : cell->second.get_port_map()) {
        if (!port.get_is_output()) {
          continue;
        }
        const std::string output_pin = instance_name + ":" + port_name;
        auto pin = database.get_pin_map().find(output_pin);
        if (pin == database.get_pin_map().end()) {
          continue;
        }
        LogicExpression& three_state = port.get_three_state_expression();
        if (!three_state.get_is_empty()) {
          std::optional<bool> disabled = three_state.evaluate_constant(values);
          if (pin->second.get_is_driver_disabled() != disabled.value_or(false)) {
            pin->second.set_is_driver_disabled(disabled.value_or(false));
            changed = true;
          }
          if (!disabled.has_value() || *disabled) {
            continue;
          }
        }
        if (port.get_function_expression().get_is_empty()) {
          continue;
        }
        const std::optional<bool> value = port.get_function_expression().evaluate_constant(values);
        if (value.has_value()) {
          changed
              = updateDriverCaseValue(output_pin, *value ? TimingCaseValue::kOne : TimingCaseValue::kZero) || changed;
        } else if (hasStaticExpressionInput(instance, port.get_function_expression())) {
          changed = updateDriverCaseValue(output_pin, TimingCaseValue::kStatic) || changed;
        }
      }
    }
  }
}

bool GraphBuilder::updateDriverCaseValue(const std::string& pin_name, TimingCaseValue value)
{
  Database& database = PWDM.getDatabase();
  TimingConstraint& constraint = database.get_timing_constraint();
  if (database.get_pin_map().at(pin_name).get_direction() != PinDirection::kInout) {
    return updateEffectiveCaseValue(constraint, pin_name, value);
  }
  auto& values = constraint.get_driver_case_analysis_map();
  auto current = values.find(pin_name);
  if (current != values.end() && current->second == value) {
    return false;
  }
  values[pin_name] = value;
  return true;
}

std::optional<TimingCaseValue> GraphBuilder::getNetDriverCaseValue(
    TimingConstraint& constraint,
    Net& net,
    const std::string& load
)
{
  Database& database = PWDM.getDatabase();
  auto& effective = constraint.get_effective_case_analysis_map();
  std::optional<TimingCaseValue> result;
  for (const std::string& driver : net.get_driver_pin_list()) {
    Pin& pin = database.get_pin_map().at(driver);
    if (driver == load || pin.get_is_driver_disabled()) {
      continue;
    }
    // Unknown candidate drivers must not be replaced by the first constant found.
    auto& values = pin.get_direction() == PinDirection::kInout ? constraint.get_driver_case_analysis_map() : effective;
    const auto value = values.find(driver);
    if (value == values.end() || (result.has_value() && *result != value->second)) {
      return std::nullopt;
    }
    result = value->second;
  }
  return result;
}

std::map<std::string, bool> GraphBuilder::getInstanceConstantInputMap(Instance& instance)
{
  Database& database = PWDM.getDatabase();
  std::map<std::string, bool> value_map;
  for (const std::string& pin_name : instance.get_pin_name_list()) {
    const auto pin = database.get_pin_map().find(pin_name);
    const auto value = database.get_timing_constraint().get_effective_case_analysis_map().find(pin_name);
    if (pin == database.get_pin_map().end()
        || value == database.get_timing_constraint().get_effective_case_analysis_map().end()
        || !isConstantCaseValue(value->second)) {
      continue;
    }
    value_map[pin->second.get_pin_name()] = value->second == TimingCaseValue::kOne;
  }
  return value_map;
}

bool GraphBuilder::hasStaticExpressionInput(Instance& instance, LogicExpression& expression)
{
  Database& database = PWDM.getDatabase();
  for (LogicExpressionTerm& term : expression.get_term_list()) {
    if (term.get_operation_type() != LogicOperationType::kPort) {
      continue;
    }
    const std::string pin_name = instance.get_instance_name() + ":" + term.get_port_name();
    const auto value = database.get_timing_constraint().get_effective_case_analysis_map().find(pin_name);
    if (value == database.get_timing_constraint().get_effective_case_analysis_map().end()
        || !isStaticCaseValue(value->second)) {
      return false;
    }
  }
  return !expression.get_is_empty();
}

bool GraphBuilder::isStaticCaseValue(TimingCaseValue value)
{
  return isConstantCaseValue(value) || value == TimingCaseValue::kStatic;
}

bool GraphBuilder::isConstantCaseValue(TimingCaseValue value)
{
  return value == TimingCaseValue::kZero || value == TimingCaseValue::kOne;
}

bool GraphBuilder::updateEffectiveCaseValue(
    TimingConstraint& constraint,
    const std::string& pin_name,
    TimingCaseValue value
)
{
  if (constraint.get_case_analysis_map().contains(pin_name)) {
    return false;
  }
  auto& effective_value_map = constraint.get_effective_case_analysis_map();
  const auto current = effective_value_map.find(pin_name);
  if (current != effective_value_map.end() && current->second == value) {
    return false;
  }
  if (current != effective_value_map.end() && current->second == TimingCaseValue::kZero) {
    return false;
  }
  effective_value_map[pin_name] = value;
  return true;
}

void GraphBuilder::disableCaseInsensitiveArc()
{
  Database& database = PWDM.getDatabase();
  TimingConstraint& constraint = database.get_timing_constraint();
  auto& effective = constraint.get_effective_case_analysis_map();
  for (Arc& arc : database.get_arc_list()) {
    arc.set_is_case_analysis_disable(false);
    Pin& source_pin = database.get_pin_map().at(arc.get_source_pin());
    Pin& sink_pin = database.get_pin_map().at(arc.get_sink_pin());
    if ((arc.get_type() == ArcType::kNet && source_pin.get_is_driver_disabled())
        || (arc.get_type() == ArcType::kCell && sink_pin.get_is_driver_disabled())) {
      arc.set_is_case_analysis_disable(true);
      continue;
    }
    auto& source_values = arc.get_type() == ArcType::kNet && source_pin.get_direction() == PinDirection::kInout
                              ? constraint.get_driver_case_analysis_map()
                              : effective;
    auto& sink_values = arc.get_type() == ArcType::kCell && sink_pin.get_direction() == PinDirection::kInout
                            ? constraint.get_driver_case_analysis_map()
                            : effective;
    const auto source_value = source_values.find(arc.get_source_pin());
    const auto sink_value = sink_values.find(arc.get_sink_pin());
    if ((source_value != source_values.end() && isStaticCaseValue(source_value->second))
        || (sink_value != sink_values.end() && isStaticCaseValue(sink_value->second))) {
      arc.set_is_case_analysis_disable(true);
      continue;
    }
    if (arc.get_type() != ArcType::kCell) {
      continue;
    }
    const auto instance = database.get_instance_map().find(arc.get_owner_name());
    if (instance == database.get_instance_map().end()) {
      continue;
    }
    const auto cell = database.get_timing_library().get_cell_map().find(instance->second.get_cell_name());
    if (cell == database.get_timing_library().get_cell_map().end()) {
      continue;
    }
    std::map<std::string, bool> values = getInstanceConstantInputMap(instance->second);
    TimingCellArc* cell_arc = arc.get_timing_cell_arc();
    bool enabled = cell_arc == nullptr || cell_arc->get_timing_arc_list().empty();
    if (cell_arc != nullptr) {
      for (TimingArc& timing_arc : cell_arc->get_timing_arc_list()) {
        LogicExpression& condition = timing_arc.get_when_expression();
        if (condition.get_is_empty() || condition.evaluate_constant(values) != std::optional<bool>(false)) {
          enabled = true;
        }
      }
    }
    if (!enabled) {
      arc.set_is_case_analysis_disable(true);
      continue;
    }
    // A sequential output function names its state, not the triggering clock.
    // Keep clock-to-Q arcs unless their case value or explicit condition disables them.
    if (arc.get_is_clock_arc()) {
      continue;
    }
    const auto output = cell->second.get_port_map().find(arc.get_library_sink_port());
    if (output == cell->second.get_port_map().end() || output->second.get_function_expression().get_is_empty()) {
      continue;
    }
    if (!output->second.get_function_expression()
             .is_sensitive_to(arc.get_library_source_port(), getInstanceConstantInputMap(instance->second))) {
      arc.set_is_case_analysis_disable(true);
    }
  }
}

// private

GraphBuilder* GraphBuilder::_gb_instance = nullptr;

// Mirrors PowerPropagator::isOutputPin(): the flag only depends on the input
// database, so it is computed once here and read as a hash lookup in the
// propagation loop (which runs per signal, and per window in the peak flow).
// Dense indices for the per-instance / per-pin caches used by the power flow.
// Instance ids come from idb but nothing in iPW consumes them, so they are
// renumbered here in instance_map order (a std::map: the order is stable).
void GraphBuilder::buildPowerIndex()
{
  Database& database = PWDM.getDatabase();
  uint64_t index = 0;
  for (std::pair<const std::string, Instance>& instance_pair : database.get_instance_map()) {
    instance_pair.second.set_instance_id(index++);
  }
  index = 0;
  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    pin_pair.second.set_index(index++);
  }
  index = 0;
  for (std::pair<const std::string, Net>& net_pair : database.get_net_map()) {
    net_pair.second.set_index(index++);
  }
  // 每 pin 的静态解析：instance / cell / cell port / 输出判定 / 时钟点。
  std::vector<Database::PinStaticContext>& pin_static_context_list = database.get_pin_static_context_list();
  pin_static_context_list.assign(database.get_pin_map().size(), Database::PinStaticContext());
  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    const uint64_t pin_index = pin_pair.second.get_index();
    Database::PinStaticContext& pin_static_context = pin_static_context_list[pin_index];
    pin_static_context.pin_index = static_cast<uint32_t>(pin_index);
    pin_static_context.pin = &pin_pair.second;
    pin_static_context.full_name = &pin_pair.first;
    if (!pin_pair.second.get_is_port()) {
      auto instance_iter = database.get_instance_map().find(pin_pair.second.get_instance_name());
      if (instance_iter != database.get_instance_map().end()) {
        pin_static_context.instance = &instance_iter->second;
        auto cell_iter = database.get_timing_library().get_cell_map().find(instance_iter->second.get_cell_name());
        if (cell_iter != database.get_timing_library().get_cell_map().end()) {
          pin_static_context.timing_cell = &cell_iter->second;
          auto port_iter = cell_iter->second.get_port_map().find(pin_pair.second.get_pin_name());
          if (port_iter != cell_iter->second.get_port_map().end()) {
            pin_static_context.cell_port = &port_iter->second;
            pin_static_context.is_output_pin = port_iter->second.get_is_output();
          }
        }
      }
    }
    auto timing_point_iter = database.get_timing_point_map().find(pin_pair.first);
    if (timing_point_iter != database.get_timing_point_map().end()) {
      pin_static_context.is_clock_point = timing_point_iter->second.get_is_clock_point();
      // 取地址：std::map 的节点地址稳定，ClockPropagator 之后原地改写也读得到实时值
      pin_static_context.timing_point = &timing_point_iter->second;
    }
  }
  // 弧端点索引（构建完成后一次性解析）
  for (Arc& arc : database.get_arc_list()) {
    auto source_iter = database.get_pin_map().find(arc.get_source_pin());
    if (source_iter != database.get_pin_map().end()) {
      arc.set_source_pin_index(static_cast<uint32_t>(source_iter->second.get_index()));
    }
    auto sink_iter = database.get_pin_map().find(arc.get_sink_pin());
    if (sink_iter != database.get_pin_map().end()) {
      arc.set_sink_pin_index(static_cast<uint32_t>(sink_iter->second.get_index()));
    }
  }
  // 信号序与出入弧邻接表（按 pin 索引摊平）
  std::vector<uint32_t>& signal_order_pin_index_list = database.get_signal_order_pin_index_list();
  signal_order_pin_index_list.clear();
  signal_order_pin_index_list.reserve(database.get_signal_order_list().size());
  for (std::string& pin_name : database.get_signal_order_list()) {
    auto pin_iter = database.get_pin_map().find(pin_name);
    if (pin_iter != database.get_pin_map().end()) {
      signal_order_pin_index_list.push_back(static_cast<uint32_t>(pin_iter->second.get_index()));
    }
  }
  std::vector<std::vector<uint32_t>>& outgoing_arc_index_list = database.get_outgoing_arc_index_list();
  std::vector<std::vector<uint32_t>>& incoming_arc_index_list = database.get_incoming_arc_index_list();
  outgoing_arc_index_list.assign(database.get_pin_map().size(), std::vector<uint32_t>());
  incoming_arc_index_list.assign(database.get_pin_map().size(), std::vector<uint32_t>());
  const auto fill_arc_index_list = [&database](std::map<std::string, std::vector<std::size_t>>& arc_list_map,
                                               std::vector<std::vector<uint32_t>>& arc_index_list) {
    for (std::pair<const std::string, std::vector<std::size_t>>& arc_list_pair : arc_list_map) {
      auto pin_iter = database.get_pin_map().find(arc_list_pair.first);
      if (pin_iter == database.get_pin_map().end()) {
        continue;
      }
      std::vector<uint32_t>& target_list = arc_index_list[pin_iter->second.get_index()];
      target_list.reserve(arc_list_pair.second.size());
      for (std::size_t arc_idx : arc_list_pair.second) {
        target_list.push_back(static_cast<uint32_t>(arc_idx));
      }
    }
  };
  fill_arc_index_list(database.get_outgoing_arc_list_map(), outgoing_arc_index_list);
  fill_arc_index_list(database.get_incoming_arc_list_map(), incoming_arc_index_list);
  // 每实例的 port -> pin 索引（分析/传播路径用它替代 "instance:port" 拼接 + pin_map 查找）
  std::vector<std::vector<Database::InstancePortPinIndex>>& instance_port_pin_index_list
      = database.get_instance_port_pin_index_list();
  instance_port_pin_index_list.assign(database.get_instance_map().size(), std::vector<Database::InstancePortPinIndex>());
  for (std::pair<const std::string, Instance>& instance_pair : database.get_instance_map()) {
    auto cell_iter = database.get_timing_library().get_cell_map().find(instance_pair.second.get_cell_name());
    if (cell_iter == database.get_timing_library().get_cell_map().end()) {
      continue;
    }
    std::vector<Database::InstancePortPinIndex>& port_pin_index_list
        = instance_port_pin_index_list[instance_pair.second.get_instance_id()];
    std::map<std::string, TimingCellPort>& port_map = cell_iter->second.get_port_map();
    port_pin_index_list.reserve(port_map.size());
    for (std::pair<const std::string, TimingCellPort>& port_pair : port_map) {
      auto pin_iter = database.get_pin_map().find(instance_pair.first + ":" + port_pair.first);
      if (pin_iter == database.get_pin_map().end()) {
        continue;
      }
      Database::InstancePortPinIndex port_pin_index;
      port_pin_index.port_name = &port_pair.first;
      port_pin_index.pin_index = static_cast<uint32_t>(pin_iter->second.get_index());
      port_pin_index_list.push_back(port_pin_index);
    }
  }
  // 注意：每窗口容器（resizePowerWorkspace）在 PWInterface::wrapPowerIndex() 里
  // 已经分配好；这里不能重复分配，否则会清掉 init 阶段写入的 VCD 注解。
}

void GraphBuilder::buildPinOutputFlagMap()
{
  Database& database = PWDM.getDatabase();
  std::unordered_map<std::string, bool>& pin_is_output_map = database.get_pin_is_output_map();
  pin_is_output_map.clear();
  pin_is_output_map.reserve(database.get_pin_map().size());
  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    Pin& pin = pin_pair.second;
    bool is_output_pin = false;
    if (!pin.get_is_port()) {
      auto instance_iter = database.get_instance_map().find(pin.get_instance_name());
      if (instance_iter != database.get_instance_map().end()) {
        auto cell_iter = database.get_timing_library().get_cell_map().find(instance_iter->second.get_cell_name());
        if (cell_iter != database.get_timing_library().get_cell_map().end()) {
          auto port_iter = cell_iter->second.get_port_map().find(pin.get_pin_name());
          is_output_pin
              = port_iter != cell_iter->second.get_port_map().end() && port_iter->second.get_is_output();
        }
      }
    }
    pin_is_output_map.emplace(pin_pair.first, is_output_pin);
  }
}

void GraphBuilder::buildSignalPointList()
{
  Database& database = PWDM.getDatabase();
  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    database.get_timing_point_map()[pin_pair.first] = TimingPoint();
  }
}

void GraphBuilder::normalizePinDirectionByTimingCell()
{
  Database& database = PWDM.getDatabase();
  for (auto& pin_pair : database.get_pin_map()) {
    Pin& pin = pin_pair.second;
    if (pin.get_is_port()) {
      continue;
    }
    TimingCellPort* port = getTimingCellPort(pin);
    PinDirection timing_cell_direction = port != nullptr && port->get_is_input() && port->get_is_output()
                                             ? PinDirection::kInout
                                             : inferInoutPinDirectionByTimingCell(pin);
    if (timing_cell_direction != PinDirection::kNone) {
      // Liberty defines the signal roles, including a genuine bidirectional port.
      pin.set_direction(timing_cell_direction);
    }
  }
}

void GraphBuilder::buildCellArcs()
{
  Database& database = PWDM.getDatabase();
  for (auto& [instance_name, instance] : database.get_instance_map()) {
    if (buildLibraryCellArcs(instance)) {
      continue;
    }
    std::vector<std::string> input_pin_list = collectInputPins(instance);
    std::vector<std::string> output_pin_list = collectOutputPins(instance);
    if (input_pin_list.empty() || output_pin_list.empty()) {
      continue;
    }
    for (std::string& input_pin : input_pin_list) {
      for (std::string& output_pin : output_pin_list) {
        if (input_pin == output_pin) {
          continue;
        }
        addArc(input_pin, output_pin, ArcType::kCell, instance_name);
      }
    }
  }
}

bool GraphBuilder::buildLibraryCellArcs(Instance& instance)
{
  Database& database = PWDM.getDatabase();
  std::map<std::string, TimingCell>& timing_cell_map = database.get_timing_library().get_cell_map();
  if (timing_cell_map.count(instance.get_cell_name()) == 0) {
    return false;
  }

  TimingCell& timing_cell = timing_cell_map[instance.get_cell_name()];
  if (timing_cell.get_cell_arc_list().empty()) {
    return false;
  }

  for (TimingCellArc& timing_cell_arc : timing_cell.get_cell_arc_list()) {
    if (!timing_cell_arc.get_is_timing_graph_arc()) {
      continue;
    }
    addCellArc(instance, timing_cell_arc);
  }
  return true;
}

std::string GraphBuilder::getInstancePinName(Instance& instance, std::string& port_name)
{
  return instance.get_instance_name() + ":" + port_name;
}

void GraphBuilder::addCellArc(Instance& instance, TimingCellArc& timing_cell_arc)
{
  Database& database = PWDM.getDatabase();
  std::string source_pin = getInstancePinName(instance, timing_cell_arc.get_source_port());
  std::string sink_pin = getInstancePinName(instance, timing_cell_arc.get_sink_port());
  if (database.get_pin_map().count(source_pin) == 0 || database.get_pin_map().count(sink_pin) == 0) {
    return;
  }
  bool is_disable_arc = timing_cell_arc.get_is_disable_arc();
  addArc(
      source_pin,
      sink_pin,
      ArcType::kCell,
      instance.get_instance_name(),
      timing_cell_arc.get_source_port(),
      timing_cell_arc.get_sink_port(),
      timing_cell_arc.get_is_clock_arc(),
      is_disable_arc,
      &timing_cell_arc
  );
}

void GraphBuilder::addArc(
    const std::string& source_pin,
    const std::string& sink_pin,
    ArcType type,
    const std::string& owner_name,
    const std::string& library_source_port,
    const std::string& library_sink_port,
    bool is_clock_arc,
    bool is_disable_arc,
    TimingCellArc* timing_cell_arc
)
{
  Database& database = PWDM.getDatabase();
  Arc arc;
  arc.set_source_pin(source_pin);
  arc.set_sink_pin(sink_pin);
  arc.set_owner_name(owner_name);
  arc.set_library_source_port(library_source_port);
  arc.set_library_sink_port(library_sink_port);
  arc.set_type(type);
  arc.set_is_clock_arc(is_clock_arc);
  arc.set_is_disable_arc(is_disable_arc);
  arc.set_timing_cell_arc(timing_cell_arc);

  database.get_arc_list().push_back(arc);
  const std::size_t arc_idx = database.get_arc_list().size() - 1;
  database.get_outgoing_arc_list_map()[source_pin].push_back(arc_idx);
  database.get_incoming_arc_list_map()[sink_pin].push_back(arc_idx);
}

std::vector<std::string> GraphBuilder::collectInputPins(Instance& instance)
{
  Database& database = PWDM.getDatabase();
  std::vector<std::string> input_pin_list;
  for (std::string& pin_name : instance.get_pin_name_list()) {
    if (isInputLike(database.get_pin_map()[pin_name].get_direction())) {
      input_pin_list.push_back(pin_name);
    }
  }
  return input_pin_list;
}

bool GraphBuilder::isInputLike(PinDirection direction)
{
  return direction == PinDirection::kInput || direction == PinDirection::kInout;
}

std::vector<std::string> GraphBuilder::collectOutputPins(Instance& instance)
{
  Database& database = PWDM.getDatabase();
  std::vector<std::string> output_pin_list;
  for (std::string& pin_name : instance.get_pin_name_list()) {
    if (isOutputLike(database.get_pin_map()[pin_name].get_direction())) {
      output_pin_list.push_back(pin_name);
    }
  }
  return output_pin_list;
}

bool GraphBuilder::isOutputLike(PinDirection direction)
{
  return direction == PinDirection::kOutput || direction == PinDirection::kInout;
}

void GraphBuilder::buildInoutPinDirectionByGraph()
{
  Database& database = PWDM.getDatabase();
  std::map<std::string, PinDirection> inout_pin_direction_map = makeInoutPinDirectionMap();
  for (std::pair<const std::string, PinDirection>& pin_direction_pair : inout_pin_direction_map) {
    database.get_pin_map()[pin_direction_pair.first].set_direction(pin_direction_pair.second);
  }
  rebuildCellArcListByPinDirection();
}

std::map<std::string, PinDirection> GraphBuilder::makeInoutPinDirectionMap()
{
  Database& database = PWDM.getDatabase();
  std::map<std::string, PinDirection> inout_pin_direction_map;
  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    Pin& pin = pin_pair.second;
    if (pin.get_direction() != PinDirection::kInout) {
      continue;
    }
    TimingCellPort* port = getTimingCellPort(pin);
    if (pin.get_is_port() || (port != nullptr && port->get_is_input() && port->get_is_output())) {
      // True bidirectional ports retain both graph roles; they are not missing directions.
      continue;
    }
    if (isFloatingInoutPin(pin)) {
      continue;
    }
    PinDirection pin_direction = inferInoutPinDirection(pin_pair.first, pin, inout_pin_direction_map);
    if (pin_direction == PinDirection::kNone || pin_direction == PinDirection::kInout) {
      PWLOG.error(
          Loc::current(),
          "Failed to infer inout pin direction: pin=",
          pin_pair.first,
          " net=",
          pin.get_net_name()
      );
    }
    inout_pin_direction_map[pin_pair.first] = pin_direction;
  }
  return inout_pin_direction_map;
}

bool GraphBuilder::isFloatingInoutPin(Pin& pin)
{
  return pin.get_net_name().empty() && inferInoutPinDirectionByTimingCell(pin) == PinDirection::kNone;
}

PinDirection GraphBuilder::inferInoutPinDirection(
    const std::string& pin_name,
    Pin& pin,
    std::map<std::string, PinDirection>& inout_pin_direction_map
)
{
  PinDirection timing_cell_direction = inferInoutPinDirectionByTimingCell(pin);
  if (timing_cell_direction != PinDirection::kNone) {
    return timing_cell_direction;
  }

  PinDirection timing_graph_direction = inferInoutPinDirectionByTimingGraph(pin_name);
  if (timing_graph_direction != PinDirection::kNone) {
    return timing_graph_direction;
  }

  return inferInoutPinDirectionByNet(pin, inout_pin_direction_map);
}

PinDirection GraphBuilder::inferInoutPinDirectionByTimingCell(Pin& pin)
{
  TimingCellPort* timing_cell_port = getTimingCellPort(pin);
  if (timing_cell_port == nullptr) {
    return PinDirection::kNone;
  }
  if (timing_cell_port->get_is_output() && !timing_cell_port->get_is_input()) {
    return PinDirection::kOutput;
  }
  if (timing_cell_port->get_is_input() && !timing_cell_port->get_is_output()) {
    return PinDirection::kInput;
  }
  return PinDirection::kNone;
}

TimingCellPort* GraphBuilder::getTimingCellPort(Pin& pin)
{
  Database& database = PWDM.getDatabase();
  if (pin.get_is_port()) {
    return nullptr;
  }
  if (database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return nullptr;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  std::map<std::string, TimingCell>& timing_cell_map = database.get_timing_library().get_cell_map();
  if (timing_cell_map.count(instance.get_cell_name()) == 0) {
    return nullptr;
  }
  TimingCell& timing_cell = timing_cell_map[instance.get_cell_name()];
  if (timing_cell.get_port_map().count(pin.get_pin_name()) == 0) {
    return nullptr;
  }
  return &timing_cell.get_port_map()[pin.get_pin_name()];
}

PinDirection GraphBuilder::inferInoutPinDirectionByTimingGraph(const std::string& pin_name)
{
  const bool has_outgoing_cell_arc = hasOutgoingCellArc(pin_name);
  const bool has_incoming_cell_arc = hasIncomingCellArc(pin_name);
  if (has_outgoing_cell_arc && !has_incoming_cell_arc) {
    return PinDirection::kInput;
  }
  if (has_incoming_cell_arc && !has_outgoing_cell_arc) {
    return PinDirection::kOutput;
  }
  return PinDirection::kNone;
}

bool GraphBuilder::hasOutgoingCellArc(const std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  for (std::size_t arc_idx : database.get_outgoing_arc_list_map()[pin_name]) {
    if (database.get_arc_list()[arc_idx].get_type() == ArcType::kCell) {
      return true;
    }
  }
  return false;
}

bool GraphBuilder::hasIncomingCellArc(const std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  for (std::size_t arc_idx : database.get_incoming_arc_list_map()[pin_name]) {
    if (database.get_arc_list()[arc_idx].get_type() == ArcType::kCell) {
      return true;
    }
  }
  return false;
}

PinDirection GraphBuilder::inferInoutPinDirectionByNet(
    Pin& pin,
    std::map<std::string, PinDirection>& inout_pin_direction_map
)
{
  Net* net = getPinNet(pin);
  if (net == nullptr) {
    return PinDirection::kNone;
  }
  int32_t driver_pin_num = getDriverPinNum(*net, inout_pin_direction_map);
  int32_t unresolved_inout_pin_num = getUnresolvedInoutPinNum(*net, inout_pin_direction_map);
  if (driver_pin_num == 0 && unresolved_inout_pin_num == 1) {
    return getDriverPinDirection(pin);
  }
  if (driver_pin_num == 1) {
    return getLoadPinDirection(pin);
  }
  if (driver_pin_num > 1) {
    std::vector<std::string> driver_pin_list = getDriverPinList(*net, inout_pin_direction_map);
    PWLOG.error(
        Loc::current(),
        "The net has multiple driver pins: net=",
        pin.get_net_name(),
        " drivers=",
        getPinNameListString(driver_pin_list)
    );
  }
  return PinDirection::kNone;
}

Net* GraphBuilder::getPinNet(Pin& pin)
{
  Database& database = PWDM.getDatabase();
  if (pin.get_net_name().empty() || database.get_net_map().count(pin.get_net_name()) == 0) {
    return nullptr;
  }
  return &database.get_net_map()[pin.get_net_name()];
}

std::vector<std::string> GraphBuilder::getDriverPinList(
    Net& net,
    std::map<std::string, PinDirection>& inout_pin_direction_map
)
{
  std::vector<std::string> driver_pin_list;
  for (std::string& pin_name : net.get_pin_name_list()) {
    if (isResolvedDriverPin(pin_name, inout_pin_direction_map)) {
      driver_pin_list.push_back(pin_name);
    }
  }
  return driver_pin_list;
}

int32_t GraphBuilder::getDriverPinNum(Net& net, std::map<std::string, PinDirection>& inout_pin_direction_map)
{
  return static_cast<int32_t>(getDriverPinList(net, inout_pin_direction_map).size());
}

int32_t GraphBuilder::getUnresolvedInoutPinNum(Net& net, std::map<std::string, PinDirection>& inout_pin_direction_map)
{
  Database& database = PWDM.getDatabase();
  int32_t unresolved_inout_pin_num = 0;
  for (std::string& pin_name : net.get_pin_name_list()) {
    Pin& pin = database.get_pin_map()[pin_name];
    if (pin.get_direction() == PinDirection::kInout && inout_pin_direction_map.count(pin_name) == 0) {
      unresolved_inout_pin_num++;
    }
  }
  return unresolved_inout_pin_num;
}

bool GraphBuilder::isResolvedDriverPin(
    const std::string& pin_name,
    std::map<std::string, PinDirection>& inout_pin_direction_map
)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[pin_name];
  PinDirection direction = pin.get_direction();
  if (direction == PinDirection::kInout && inout_pin_direction_map.count(pin_name) > 0) {
    direction = inout_pin_direction_map[pin_name];
  }
  return isDriverDirection(pin, direction);
}

bool GraphBuilder::isDriverDirection(Pin& pin, PinDirection direction)
{
  if (pin.get_is_port()) {
    return direction == PinDirection::kInput;
  }
  return direction == PinDirection::kOutput;
}

PinDirection GraphBuilder::getDriverPinDirection(Pin& pin)
{
  if (pin.get_is_port()) {
    return PinDirection::kInput;
  }
  return PinDirection::kOutput;
}

PinDirection GraphBuilder::getLoadPinDirection(Pin& pin)
{
  if (pin.get_is_port()) {
    return PinDirection::kOutput;
  }
  return PinDirection::kInput;
}

void GraphBuilder::rebuildCellArcListByPinDirection()
{
  Database& database = PWDM.getDatabase();
  database.get_arc_list().clear();
  database.get_outgoing_arc_list_map().clear();
  database.get_incoming_arc_list_map().clear();
  buildCellArcs();
}

void GraphBuilder::buildNetDriverLoadList()
{
  Database& database = PWDM.getDatabase();
  for (std::pair<const std::string, Net>& net_pair : database.get_net_map()) {
    makeNetDriverLoad(net_pair.first, net_pair.second);
  }
}

void GraphBuilder::makeNetDriverLoad(const std::string& net_name, Net& net)
{
  Database& database = PWDM.getDatabase();
  net.get_driver_pin_list().clear();
  net.get_load_pin_list().clear();

  for (std::string& pin_name : net.get_pin_name_list()) {
    Pin& pin = database.get_pin_map()[pin_name];
    if (isDriverPin(pin)) {
      net.get_driver_pin_list().push_back(pin_name);
    }
    if (!isDriverPin(pin) || pin.get_direction() == PinDirection::kInout) {
      net.get_load_pin_list().push_back(pin_name);
    }
  }

  int32_t unconditional_driver_num = 0;
  for (std::string& driver : net.get_driver_pin_list()) {
    Pin& pin = database.get_pin_map()[driver];
    TimingCellPort* port = getTimingCellPort(pin);
    if (pin.get_direction() != PinDirection::kInout
        && (port == nullptr || port->get_three_state_expression().get_is_empty())) {
      ++unconditional_driver_num;
    }
  }
  if (unconditional_driver_num > 1) {
    // 一条 net 上有多个 driver（常见于修复 buffer 的输出与原 driver 同时挂在同一
    // net 上）：下游 buildNetArcs() 对这类 net 是"warn + 跳过"，这里保持一致，
    // 不再因为单个 net 中止整个流程。
    PWLOG.warn(
        Loc::current(),
        "The net has multiple driver pins: net=",
        net_name,
        " drivers=",
        getPinNameListString(net.get_driver_pin_list())
    );
  }
}

bool GraphBuilder::isDriverPin(Pin& pin)
{
  return pin.get_direction() == PinDirection::kInout || isDriverDirection(pin, pin.get_direction());
}

std::string GraphBuilder::getPinNameListString(std::vector<std::string>& pin_name_list)
{
  std::string pin_name_list_string;
  for (std::string& pin_name : pin_name_list) {
    if (!pin_name_list_string.empty()) {
      pin_name_list_string += ", ";
    }
    pin_name_list_string += pin_name;
  }
  return pin_name_list_string;
}

void GraphBuilder::buildNetArcs()
{
  Database& database = PWDM.getDatabase();
  for (auto& [net_name, net] : database.get_net_map()) {
    int32_t unconditional_driver_num = 0;
    for (std::string& driver : net.get_driver_pin_list()) {
      Pin& pin = database.get_pin_map().at(driver);
      TimingCellPort* port = getTimingCellPort(pin);
      if (pin.get_direction() != PinDirection::kInout
          && (port == nullptr || port->get_three_state_expression().get_is_empty())) {
        ++unconditional_driver_num;
      }
    }
    if (unconditional_driver_num > 1) {
      continue;
    }
    for (std::string& driver_pin : net.get_driver_pin_list()) {
      for (std::string& load_pin : net.get_load_pin_list()) {
        if (load_pin != driver_pin) {
          addArc(driver_pin, load_pin, ArcType::kNet, net_name);
        }
      }
    }
  }
}

void GraphBuilder::buildGraphVertexList()
{
  Database& database = PWDM.getDatabase();
  database.get_graph_vertex_list().clear();
  database.get_driver_timing_point_map().clear();
  for (auto& [pin_name, pin] : database.get_pin_map()) {
    int32_t idx = addGraphVertex(pin_name, isDriverPin(pin), database.get_timing_point_map()[pin_name]);
    pin.set_load_vertex_idx(idx);
    pin.set_driver_vertex_idx(idx);
  }
  for (auto& [pin_name, pin] : database.get_pin_map()) {
    if (pin.get_direction() == PinDirection::kInout) {
      database.get_graph_vertex_list()[pin.get_load_vertex_idx()].set_is_driver(false);
      pin.set_driver_vertex_idx(addGraphVertex(pin_name, true, database.get_driver_timing_point_map()[pin_name]));
    }
  }
  for (std::size_t idx = 0; idx < database.get_arc_list().size(); ++idx) {
    Arc& arc = database.get_arc_list()[idx];
    Pin& source = database.get_pin_map()[arc.get_source_pin()];
    Pin& sink = database.get_pin_map()[arc.get_sink_pin()];
    arc.set_source_vertex_idx(
        arc.get_type() == ArcType::kNet ? source.get_driver_vertex_idx() : source.get_load_vertex_idx()
    );
    arc.set_sink_vertex_idx(
        arc.get_type() == ArcType::kNet ? sink.get_load_vertex_idx() : sink.get_driver_vertex_idx()
    );
    database.get_graph_vertex_list()[arc.get_source_vertex_idx()].get_outgoing_arc_list().push_back(idx);
    database.get_graph_vertex_list()[arc.get_sink_vertex_idx()].get_incoming_arc_list().push_back(idx);
  }
}

int32_t GraphBuilder::addGraphVertex(const std::string& pin_name, bool is_driver, TimingPoint& point)
{
  Database& database = PWDM.getDatabase();
  GraphVertex vertex;
  vertex.set_pin_name(pin_name);
  vertex.set_is_driver(is_driver);
  vertex.set_timing_point(&point);
  database.get_graph_vertex_list().push_back(vertex);
  return static_cast<int32_t>(database.get_graph_vertex_list().size()) - 1;
}

void GraphBuilder::addArc(
    const std::string& source_pin,
    const std::string& sink_pin,
    ArcType type,
    const std::string& owner_name
)
{
  Database& database = PWDM.getDatabase();
  Arc arc;
  arc.set_source_pin(source_pin);
  arc.set_sink_pin(sink_pin);
  arc.set_owner_name(owner_name);
  arc.set_type(type);
  arc.set_is_disable_arc(shouldDisableNetArc(source_pin, sink_pin));

  database.get_arc_list().push_back(arc);
  const std::size_t arc_idx = database.get_arc_list().size() - 1;
  database.get_outgoing_arc_list_map()[source_pin].push_back(arc_idx);
  database.get_incoming_arc_list_map()[sink_pin].push_back(arc_idx);
}

bool GraphBuilder::shouldDisableNetArc(const std::string& source_pin, const std::string& sink_pin)
{
  Database& database = PWDM.getDatabase();
  Pin& source_pin_inst = database.get_pin_map()[source_pin];
  Pin& sink_pin_inst = database.get_pin_map()[sink_pin];
  if (source_pin_inst.get_is_port() || sink_pin_inst.get_is_port()) {
    return false;
  }
  if (database.get_instance_map().count(source_pin_inst.get_instance_name()) == 0
      || database.get_instance_map().count(sink_pin_inst.get_instance_name()) == 0) {
    return false;
  }
  Instance& source_instance = database.get_instance_map()[source_pin_inst.get_instance_name()];
  Instance& sink_instance = database.get_instance_map()[sink_pin_inst.get_instance_name()];
  std::map<std::string, TimingCell>& timing_cell_map = database.get_timing_library().get_cell_map();
  if (timing_cell_map.count(source_instance.get_cell_name()) == 0) {
    return false;
  }
  TimingCell& source_cell = timing_cell_map[source_instance.get_cell_name()];
  return source_cell.get_is_sequential() && !source_cell.get_is_clock_gating() && !source_cell.get_is_macro()
         && sink_pin == sink_instance.get_clock_pin_name();
}

bool GraphBuilder::isDisableArc(Arc& arc)
{
  return arc.get_is_disable_arc() || arc.get_is_loop_disable();
}

void GraphBuilder::buildSourcePinList()
{
  Database& database = PWDM.getDatabase();
  database.get_source_pin_list().clear();
  for (auto& [pin_name, pin] : database.get_pin_map()) {
    if (isSourcePin(pin_name, pin)) {
      database.get_source_pin_list().push_back(pin_name);
    }
  }
}

bool GraphBuilder::isSourcePin(const std::string& pin_name, Pin& pin)
{
  if (isSequentialClockSource(pin_name, pin)) {
    return true;
  }
  if (isClockPin(pin_name, pin)) {
    return false;
  }
  if (isClockSource(pin_name)) {
    return isInputPort(pin);
  }
  return !hasIncomingArc(pin_name) || isInputPort(pin);
}

bool GraphBuilder::isSequentialClockSource(const std::string& pin_name, Pin& pin)
{
  Database& database = PWDM.getDatabase();
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  return instance.get_is_sequential() && !instance.get_is_clock_gating() && pin_name == instance.get_clock_pin_name();
}

bool GraphBuilder::isClockPin(const std::string& pin_name, Pin& pin)
{
  Database& database = PWDM.getDatabase();
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return false;
  }
  return pin_name == database.get_instance_map()[pin.get_instance_name()].get_clock_pin_name();
}

bool GraphBuilder::isClockSource(const std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  for (auto& [clock_name, timing_clock] : database.get_timing_constraint().get_clock_map()) {
    if (PWUTIL.exist(timing_clock.get_source_list(), pin_name)) {
      return true;
    }
  }
  return false;
}

bool GraphBuilder::hasIncomingArc(const std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  return database.get_incoming_arc_list_map().count(pin_name) > 0
         && !database.get_incoming_arc_list_map()[pin_name].empty();
}

bool GraphBuilder::isInputPort(Pin& pin)
{
  return pin.get_is_port()
         && (pin.get_direction() == PinDirection::kInput || pin.get_direction() == PinDirection::kInout);
}

void GraphBuilder::breakLoopArcList()
{
  Database& database = PWDM.getDatabase();
  std::vector<GraphVertex>& vertices = database.get_graph_vertex_list();
  std::vector<GBColorType> colors(vertices.size(), GBColorType::kNone);
  int32_t disabled_loop_num = 0;
  // Use graph vertices, not physical pin names: PAD receive and drive are independent.
  for (int32_t root = 0; root < static_cast<int32_t>(vertices.size()); ++root) {
    if (colors[root] != GBColorType::kNone) {
      continue;
    }
    std::vector<std::pair<int32_t, std::size_t>> stack{{root, 0}};
    colors[root] = GBColorType::kGray;
    while (!stack.empty()) {
      auto& [vertex_idx, next_arc_idx] = stack.back();
      std::vector<std::size_t>& outgoing = vertices[vertex_idx].get_outgoing_arc_list();
      if (next_arc_idx == outgoing.size()) {
        colors[vertex_idx] = GBColorType::kBlack;
        stack.pop_back();
        continue;
      }
      Arc& arc = database.get_arc_list()[outgoing[next_arc_idx++]];
      if (isDisableArc(arc)) {
        continue;
      }
      int32_t sink_idx = arc.get_sink_vertex_idx();
      if (colors[sink_idx] == GBColorType::kGray) {
        arc.set_is_loop_disable(true);
        ++disabled_loop_num;
        PWLOG.warn(Loc::current(), "Combinational loop cut: ", arc.get_source_pin(), " -> ", arc.get_sink_pin());
      } else if (colors[sink_idx] == GBColorType::kNone) {
        colors[sink_idx] = GBColorType::kGray;
        stack.emplace_back(sink_idx, 0);
      }
    }
  }
  if (disabled_loop_num > 0) {
    PWLOG.info(Loc::current(), "Break iPW loop arcs: disabled_arcs=", disabled_loop_num);
  }
}

void GraphBuilder::buildSignalOrder()
{
  Database& database = PWDM.getDatabase();
  std::vector<GraphVertex>& vertices = database.get_graph_vertex_list();
  std::vector<int32_t> indegree_list(vertices.size(), 0);
  std::queue<int32_t> vertex_queue;
  for (int32_t idx = 0; idx < static_cast<int32_t>(vertices.size()); ++idx) {
    for (std::size_t arc_idx : vertices[idx].get_incoming_arc_list()) {
      if (!isDisableArc(database.get_arc_list()[arc_idx])) {
        ++indegree_list[idx];
      }
    }
    if (indegree_list[idx] == 0) {
      vertex_queue.push(idx);
    }
  }
  database.get_graph_order_list().clear();
  database.get_signal_order_list().clear();
  while (!vertex_queue.empty()) {
    int32_t vertex_idx = vertex_queue.front();
    vertex_queue.pop();
    GraphVertex& vertex = vertices[vertex_idx];
    database.get_graph_order_list().push_back(vertex_idx);
    database.get_signal_order_list().push_back(vertex.get_pin_name());
    for (std::size_t arc_idx : vertex.get_outgoing_arc_list()) {
      Arc& arc = database.get_arc_list()[arc_idx];
      if (isDisableArc(arc)) {
        continue;
      }
      int32_t sink_idx = arc.get_sink_vertex_idx();
      if (--indegree_list[sink_idx] == 0) {
        vertex_queue.push(sink_idx);
      }
    }
  }
}

void GraphBuilder::printLoopInfo()
{
  Database& database = PWDM.getDatabase();
  if (database.get_graph_order_list().size() != database.get_graph_vertex_list().size()) {
    PWLOG.error(Loc::current(), "Incomplete graph order after loop cleanup.");
  }
}

}  // namespace ipw
