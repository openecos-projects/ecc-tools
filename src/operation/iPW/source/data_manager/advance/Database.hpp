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
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <algorithm>
#include <unordered_map>

#include "Arc.hpp"
#include "GraphVertex.hpp"
#include "Instance.hpp"
#include "InstancePower.hpp"
#include "Net.hpp"
#include "NetPower.hpp"
#include "ParasiticNet.hpp"
#include "PeakPowerSummary.hpp"
#include "Pin.hpp"
#include "PowerActivity.hpp"
#include "PowerSummary.hpp"
#include "PWHeader.hpp"
#include "TimingConstraint.hpp"
#include "TimingLibrary.hpp"
#include "TimingPoint.hpp"

namespace ipw {

class Database
{
 public:
  // Per-window analysis state. One instance is the main workspace, extra ones are handed to the
  // peak engine workers so that every worker writes its own activity, power and peak maps.
  // 每 pin 的静态解析结果（构建后只读）：把传播/分析热路径上的
  // pin_map -> instance_map -> cell_map -> port_map 四级查找压缩成一次索引访问。
  struct PinStaticContext
  {
    uint32_t pin_index = 0;
    Pin* pin = nullptr;
    const std::string* full_name = nullptr;
    Instance* instance = nullptr;         // port/悬空 pin -> nullptr
    TimingCell* timing_cell = nullptr;    // 黑盒 -> nullptr
    TimingCellPort* cell_port = nullptr;  // 该 pin 对应的 cell port
    bool is_output_pin = false;           // == 原 isOutputPin(pin_name)
    bool is_clock_point = false;          // 建图时的快照（ClockPropagator 之后会改写，读者请走 timing_point）
    TimingPoint* timing_point = nullptr;  // 指向 timing_point_map 内的实点（地址稳定，读到的是实时值）
  };

  struct PowerWorkspace
  {
    std::map<uint32_t, PowerActivity> driver_activity_map;
    // 每窗口/每次注解用世代号代替"清空 map"：stamp[i] == generation 才算写过。
    uint32_t vcd_generation = 1;
    uint32_t activity_generation = 1;

    std::vector<uint32_t> vcd_stamp;                 // by pin index
    std::vector<PowerActivity> vcd_activity_list;    // by pin index
    std::vector<uint32_t> vcd_touched_pin_list;      // 本次写过的 pin（写入序）

    std::vector<uint32_t> activity_stamp;            // by pin index
    std::vector<PowerActivity> activity_list;        // by pin index

    std::vector<InstancePower> instance_power_list;  // by instance id（每窗口全量写）
    std::vector<NetPower> net_power_list;            // by net index
    std::vector<uint8_t> net_power_written;          // by net index

    PowerSummary power_summary;
    PeakPowerSummary peak_power_summary;
  };

  Database() = default;
  ~Database() = default;
  // getter
  std::vector<GraphVertex>& get_graph_vertex_list() { return _graph_vertex_list; }
  std::vector<int32_t>& get_graph_order_list() { return _graph_order_list; }
  std::map<std::string, TimingPoint>& get_driver_timing_point_map() { return _driver_timing_point_map; }
  std::string& get_design_name() { return _design_name; }
  std::map<std::string, Instance>& get_instance_map() { return _instance_map; }
  std::map<std::string, Pin>& get_pin_map() { return _pin_map; }
  // Precomputed by GraphBuilder: does the pin drive an output port of its
  // timing cell? Power propagation probes this once per signal, so the answer
  // is cached here instead of re-walking pin/instance/cell maps every time.
  std::unordered_map<std::string, bool>& get_pin_is_output_map() { return _pin_is_output_map; }
  std::map<std::string, Net>& get_net_map() { return _net_map; }
  std::vector<Arc>& get_arc_list() { return _arc_list; }
  std::map<std::string, std::vector<std::size_t>>& get_outgoing_arc_list_map() { return _outgoing_arc_list_map; }
  std::map<std::string, std::vector<std::size_t>>& get_incoming_arc_list_map() { return _incoming_arc_list_map; }
  std::vector<std::string>& get_source_pin_list() { return _source_pin_list; }
  std::vector<std::string>& get_signal_order_list() { return _signal_order_list; }
  std::map<std::string, TimingPoint>& get_timing_point_map() { return _timing_point_map; }
  PowerWorkspace& get_workspace() { return currentWorkspace() != nullptr ? *currentWorkspace() : _workspace; }
  const PowerWorkspace& get_workspace() const
  {
    return currentWorkspace() != nullptr ? *currentWorkspace() : _workspace;
  }
  // 网表构建完成后调用一次：给（每个 worker 的）workspace 分配按 id 索引的向量。
  void resizePowerWorkspace(PowerWorkspace& workspace)
  {
    const std::size_t pin_num = _pin_map.size();
    workspace.vcd_stamp.assign(pin_num, 0);
    workspace.vcd_activity_list.assign(pin_num, PowerActivity());
    workspace.vcd_touched_pin_list.clear();
    workspace.activity_stamp.assign(pin_num, 0);
    workspace.activity_list.assign(pin_num, PowerActivity());
    workspace.instance_power_list.assign(_instance_map.size(), InstancePower());
    workspace.net_power_list.assign(_net_map.size(), NetPower());
    workspace.net_power_written.assign(_net_map.size(), 0);
    workspace.vcd_generation = 1;
    workspace.activity_generation = 1;
    workspace.peak_power_summary.resizeInstancePeakValueList(_instance_map.size());
  }
  // ---- 窗口边界 ----
  void beginVcdAnnotation()
  {
    PowerWorkspace& workspace = get_workspace();
    workspace.vcd_generation++;
    workspace.vcd_touched_pin_list.clear();
  }
  void beginActivityWindow()
  {
    get_workspace().activity_generation++;
    get_workspace().driver_activity_map.clear();
  }
  void beginPowerWindow()
  {
    std::vector<uint8_t>& net_power_written = get_workspace().net_power_written;
    std::fill(net_power_written.begin(), net_power_written.end(), 0);
  }
  // ---- VCD 注解（按 pin 索引）----
  void setVcdActivity(const uint32_t pin_index, const PowerActivity& activity)
  {
    PowerWorkspace& workspace = get_workspace();
    if (workspace.vcd_stamp[pin_index] != workspace.vcd_generation) {
      workspace.vcd_stamp[pin_index] = workspace.vcd_generation;
      workspace.vcd_touched_pin_list.push_back(pin_index);
    }
    workspace.vcd_activity_list[pin_index] = activity;
  }
  const PowerActivity* findVcdActivity(const uint32_t pin_index) const
  {
    const PowerWorkspace& workspace = get_workspace();
    return workspace.vcd_stamp[pin_index] == workspace.vcd_generation
               ? &workspace.vcd_activity_list[pin_index]
               : nullptr;
  }
  bool hasVcdActivity() const { return !get_workspace().vcd_touched_pin_list.empty(); }
  std::size_t getVcdActivityNum() const { return get_workspace().vcd_touched_pin_list.size(); }
  std::vector<uint32_t>& get_vcd_touched_pin_list() { return get_workspace().vcd_touched_pin_list; }
  // ---- 引脚活动性（按 pin 索引）----
  PowerActivity& getOrCreatePowerActivity(const uint32_t pin_index, bool& is_created)
  {
    static PowerActivity invalid_activity;
    if (pin_index >= get_workspace().activity_stamp.size()) {
      is_created = false;
      invalid_activity = PowerActivity();
      return invalid_activity;
    }
    PowerWorkspace& workspace = get_workspace();
    is_created = workspace.activity_stamp[pin_index] != workspace.activity_generation;
    if (is_created) {
      workspace.activity_stamp[pin_index] = workspace.activity_generation;
      workspace.activity_list[pin_index] = PowerActivity();
    }
    return workspace.activity_list[pin_index];
  }
  PowerActivity& getOrCreatePowerActivity(const uint32_t pin_index)
  {
    PowerWorkspace& workspace = get_workspace();
    if (workspace.activity_stamp[pin_index] != workspace.activity_generation) {
      workspace.activity_stamp[pin_index] = workspace.activity_generation;
      workspace.activity_list[pin_index] = PowerActivity();
    }
    return workspace.activity_list[pin_index];
  }
  const PowerActivity* findPowerActivity(const uint32_t pin_index) const
  {
    const PowerWorkspace& workspace = get_workspace();
    if (pin_index >= workspace.activity_stamp.size()) {
      return nullptr;
    }
    return workspace.activity_stamp[pin_index] == workspace.activity_generation
               ? &workspace.activity_list[pin_index]
               : nullptr;
  }
  // ---- 功耗结果（按 id/索引）----
  std::vector<InstancePower>& get_instance_power_list() { return get_workspace().instance_power_list; }
  std::vector<NetPower>& get_net_power_list() { return get_workspace().net_power_list; }
  std::vector<uint8_t>& get_net_power_written() { return get_workspace().net_power_written; }
  std::vector<PinStaticContext>& get_pin_static_context_list() { return _pin_static_context_list; }
  // 每实例：cell port 名 -> pin 索引（构建后只读）。热路径据此避免
  // 拼接 "instance:port" 字符串与 pin_map 查找。
  struct InstancePortPinIndex
  {
    const std::string* port_name = nullptr;
    uint32_t pin_index = 0;
  };
  std::vector<std::vector<InstancePortPinIndex>>& get_instance_port_pin_index_list()
  {
    return _instance_port_pin_index_list;
  }
  // 传播路径的索引化视图（构建后只读）。
  std::vector<uint32_t>& get_signal_order_pin_index_list() { return _signal_order_pin_index_list; }
  // 按 pin 索引的最长路径层号，以及"层桶"在 _signal_order_pin_index_list 上的边界。
  // 一层一个桶时桶内 pin 的源已全部定值，可以并行算、再按原序串行提交（见 SignalPropagator）。
  std::vector<uint32_t>& get_signal_level_list() { return _signal_level_list; }
  std::vector<uint32_t>& get_signal_level_begin_list() { return _signal_level_begin_list; }
  std::vector<std::vector<uint32_t>>& get_outgoing_arc_index_list() { return _outgoing_arc_index_list; }
  std::vector<std::vector<uint32_t>>& get_incoming_arc_index_list() { return _incoming_arc_index_list; }
  PowerSummary& get_power_summary() { return get_workspace().power_summary; }
  PeakPowerSummary& get_peak_power_summary() { return get_workspace().peak_power_summary; }
  TimingLibrary& get_timing_library() { return _timing_library; }
  std::map<std::string, ParasiticNet>& get_parasitic_net_map() { return _parasitic_net_map; }
  TimingConstraint& get_timing_constraint() { return _timing_constraint; }
  // setter
  void set_design_name(const std::string& design_name) { _design_name = design_name; }
  void set_workspace(PowerWorkspace* workspace) { currentWorkspace() = workspace; }
  // function
  TimingPoint& getDriverTimingPoint(const std::string& pin_name)
  {
    auto iter = _driver_timing_point_map.find(pin_name);
    return iter == _driver_timing_point_map.end() ? _timing_point_map.at(pin_name) : iter->second;
  }
  TimingPoint& getSourceTimingPoint(Arc& arc)
  {
    return arc.get_type() == ArcType::kNet ? getDriverTimingPoint(arc.get_source_pin())
                                           : _timing_point_map.at(arc.get_source_pin());
  }
  TimingPoint& getSinkTimingPoint(Arc& arc)
  {
    return arc.get_type() == ArcType::kCell ? getDriverTimingPoint(arc.get_sink_pin())
                                            : _timing_point_map.at(arc.get_sink_pin());
  }

 private:
  std::vector<GraphVertex> _graph_vertex_list;
  std::vector<int32_t> _graph_order_list;
  std::map<std::string, TimingPoint> _driver_timing_point_map;
  static PowerWorkspace*& currentWorkspace()
  {
    static thread_local PowerWorkspace* workspace = nullptr;
    return workspace;
  }

  std::string _design_name;
  std::map<std::string, Instance> _instance_map;
  std::map<std::string, Pin> _pin_map;
  std::unordered_map<std::string, bool> _pin_is_output_map;
  std::vector<PinStaticContext> _pin_static_context_list;
  std::vector<uint32_t> _signal_order_pin_index_list;
  std::vector<uint32_t> _signal_level_list;
  std::vector<uint32_t> _signal_level_begin_list;
  std::vector<std::vector<uint32_t>> _outgoing_arc_index_list;
  std::vector<std::vector<uint32_t>> _incoming_arc_index_list;
  std::vector<std::vector<InstancePortPinIndex>> _instance_port_pin_index_list;
  std::map<std::string, Net> _net_map;
  std::vector<Arc> _arc_list;
  std::map<std::string, std::vector<std::size_t>> _outgoing_arc_list_map;
  std::map<std::string, std::vector<std::size_t>> _incoming_arc_list_map;
  std::vector<std::string> _source_pin_list;
  std::vector<std::string> _signal_order_list;
  std::map<std::string, TimingPoint> _timing_point_map;
  PowerWorkspace _workspace;
  TimingLibrary _timing_library;
  std::map<std::string, ParasiticNet> _parasitic_net_map;
  TimingConstraint _timing_constraint;
};

}  // namespace ipw
