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
#include "PeakPowerEngine.hpp"

#include <algorithm>
#include <vector>

#include "DataManager.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "PowerAnalyzer.hpp"
#include "PowerPropagator.hpp"
#include "PWHeader.hpp"

namespace ipw {

namespace {
// 窗口时长由 tick 换算而来，留一点余量，避免浮点误差把完整窗判成截断窗。
constexpr double WINDOW_LENGTH_EPSILON = 1E-9;
// Every worker keeps its own window state, so the thread count also drives peak memory.
constexpr int32_t kMaxPeakThreadNum = 32;
}  // namespace

// public

void PeakPowerEngine::initInst()
{
  if (_pe_instance == nullptr) {
    _pe_instance = new PeakPowerEngine();
  }
}

PeakPowerEngine& PeakPowerEngine::getInst()
{
  if (_pe_instance == nullptr) {
    PWLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_pe_instance;
}

void PeakPowerEngine::destroyInst()
{
  if (_pe_instance != nullptr) {
    delete _pe_instance;
    _pe_instance = nullptr;
  }
}

PeakPowerEngine* PeakPowerEngine::_pe_instance = nullptr;

// function

void PeakPowerEngine::run(const std::string& vcd_file_path)
{
  Monitor monitor;
  PWLOG.info(Loc::current(), "Starting...");

  const double window_length = check(vcd_file_path);
  if (window_length <= 0.0) {
    return;
  }
  Database& database = PWDM.getDatabase();
  PeakPowerSummary& peak_power_summary = database.get_peak_power_summary();
  peak_power_summary.reset();
  peak_power_summary.set_is_enabled(true);

  const vcd::ActivityModelOptions activity_options = buildActivityModelOptions();
  PWLOG.info(
      Loc::current(),
      "Activity model = time_based (X transition derate = ",
      activity_options.x_transition_derate_factor,
      ", include initial X transitions = ",
      activity_options.include_initial_x_transitions ? "true" : "false",
      ")"
  );

  const int32_t thread_num = resolveThreadNum();
  PWLOG.info(Loc::current(), "Window analysis runs on ", thread_num, " thread(s).");
  // Prepare the window-independent per-instance / per-pin data once, in the
  // main thread: the workers only read it while walking the windows.
  PowerAnalyzer::getInst().prepareStaticContext();
  PowerPropagator::getInst().prepareStaticContext();
  std::vector<Database::PowerWorkspace> workspace_list(thread_num);
  for (Database::PowerWorkspace& workspace : workspace_list) {
    database.resizePowerWorkspace(workspace);
  }
  std::vector<WindowWorkerResult> worker_result_list(thread_num);

#pragma omp parallel for schedule(static, 1) num_threads(thread_num)
  for (int32_t worker_id = 0; worker_id < thread_num; worker_id++) {
    worker_result_list[worker_id]
        = analyzeWindows(vcd_file_path, window_length, worker_id, thread_num, workspace_list[worker_id]);
  }

  bool is_open = true;
  for (const WindowWorkerResult& worker_result : worker_result_list) {
    is_open = is_open && worker_result.is_open;
  }
  if (!is_open) {
    PWLOG.error(Loc::current(), "Failed to open VCD file '", vcd_file_path, "'!");
    return;
  }

  std::size_t window_num = 0;
  std::size_t truncated_window_num = 0;
  mergeWorkerResults(worker_result_list, workspace_list, window_num, truncated_window_num);

  if (truncated_window_num > 0) {
    PWLOG.info(
        Loc::current(),
        "Skipped ",
        truncated_window_num,
        " truncated window(s) shorter than the ",
        window_length,
        " ns analysis window."
    );
  }
  if (window_num == 0) {
    PWLOG.error(Loc::current(), "The VCD holds no complete ", window_length, " ns window; check the waveform!");
    return;
  }

  const PeakPowerValue& total_peak_value = database.get_peak_power_summary().get_total_peak_value();
  PWLOG.info(
      Loc::current(),
      "Analyzed ",
      window_num,
      " window(s): peak power ",
      total_peak_value.get_power(),
      " W at ",
      total_peak_value.get_begin_time(),
      " ns - ",
      total_peak_value.get_end_time(),
      " ns"
  );
  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

double PeakPowerEngine::check(const std::string& vcd_file_path)
{
  if (vcd_file_path.empty()) {
    PWLOG.error(Loc::current(), "The VCD file path is empty!");
    return 0.0;
  }
  const double window_length = resolveWindowLength();
  if (window_length <= 0.0) {
    PWLOG.error(Loc::current(), "No reference clock found for peak power analysis!");
    return 0.0;
  }
  return window_length;
}

double PeakPowerEngine::resolveWindowLength()
{
  std::map<std::string, TimingClock>& clock_map = PWDM.getDatabase().get_timing_constraint().get_clock_map();
  double window_length = 0.0;
  std::string reference_clock_name;
  for (std::pair<const std::string, TimingClock>& clock_pair : clock_map) {
    const double clock_period = clock_pair.second.get_period();
    PWLOG.info(Loc::current(), "Clock '", clock_pair.first, "' period = ", clock_period, " ns");
    if (clock_period <= 0.0) {
      continue;
    }
    if (window_length <= 0.0 || clock_period < window_length) {
      window_length = clock_period;
      reference_clock_name = clock_pair.first;
    }
  }
  if (window_length > 0.0) {
    PWLOG.info(
        Loc::current(),
        "Reference clock '",
        reference_clock_name,
        "' period ",
        window_length,
        " ns is used as the peak analysis window."
    );
  }
  return window_length;
}

int32_t PeakPowerEngine::resolveThreadNum()
{
  const int32_t thread_num = PWDM.getConfig().thread_number;
  if (thread_num > kMaxPeakThreadNum) {
    PWLOG.info(Loc::current(), "The peak analysis is capped at ", kMaxPeakThreadNum, " thread(s).");
    return kMaxPeakThreadNum;
  }
  return std::max(thread_num, 1);
}

PeakPowerEngine::WindowWorkerResult PeakPowerEngine::analyzeWindows(
    const std::string& vcd_file_path,
    const double window_length,
    const int32_t worker_id,
    const int32_t worker_num,
    Database::PowerWorkspace& workspace
)
{
  WindowWorkerResult result;
  Database& database = PWDM.getDatabase();
  database.set_workspace(&workspace);

  vcd::VcdWindowReader window_reader;
  if (!window_reader.open(vcd_file_path, window_length, buildActivityModelOptions())) {
    database.set_workspace(nullptr);
    return result;
  }
  result.is_open = true;

  while (window_reader.advance_window()) {
    const vcd::VcdWindowSignalActivity& window_activity = window_reader.get_window_activity();
    // Every worker walks the whole waveform, so windows are handed out round-robin.
    const std::size_t window_index = window_activity.get_window_index();
    if (window_index % static_cast<std::size_t>(worker_num) != static_cast<std::size_t>(worker_id)) {
      continue;
    }
    // 波形末尾不足一窗的余量里可能挤进整周期的跳变（速率被抬高），不能参与峰值竞争。
    if (window_activity.get_window_duration() < window_length - WINDOW_LENGTH_EPSILON) {
      result.truncated_window_num++;
      continue;
    }
    PowerPropagator::annotateVcdActivity(window_activity);
    PWPP.propagate();
    PWPA.analyze();
    updatePeakPower(window_activity);
    result.window_num++;
    result.last_window_index = window_index;
    result.has_window = true;
  }

  database.set_workspace(nullptr);
  return result;
}

void PeakPowerEngine::mergeWorkerResults(
    const std::vector<WindowWorkerResult>& worker_result_list,
    const std::vector<Database::PowerWorkspace>& workspace_list,
    std::size_t& window_num,
    std::size_t& truncated_window_num
)
{
  // Serial analysis leaves the last complete window in the database, so adopt the workspace that
  // holds it before folding in the peaks.
  std::size_t last_worker_id = 0;
  std::size_t last_window_index = 0;
  bool has_last_window = false;
  for (std::size_t worker_id = 0; worker_id < worker_result_list.size(); worker_id++) {
    const WindowWorkerResult& worker_result = worker_result_list[worker_id];
    window_num += worker_result.window_num;
    truncated_window_num += worker_result.truncated_window_num;
    if (worker_result.has_window && (!has_last_window || worker_result.last_window_index > last_window_index)) {
      last_worker_id = worker_id;
      last_window_index = worker_result.last_window_index;
      has_last_window = true;
    }
  }
  if (!has_last_window) {
    return;
  }

  Database& database = PWDM.getDatabase();
  const Database::PowerWorkspace& last_window_workspace = workspace_list[last_worker_id];
  database.get_workspace().vcd_touched_pin_list = last_window_workspace.vcd_touched_pin_list;
  database.get_instance_power_list() = last_window_workspace.instance_power_list;
  database.get_net_power_list() = last_window_workspace.net_power_list;
  database.get_power_summary() = last_window_workspace.power_summary;

  PeakPowerSummary& peak_power_summary = database.get_peak_power_summary();
  for (const Database::PowerWorkspace& workspace : workspace_list) {
    peak_power_summary.get_total_peak_value().merge(workspace.peak_power_summary.get_total_peak_value());
    peak_power_summary.mergeInstancePeakValues(workspace.peak_power_summary);
    const std::vector<PeakPowerWindow>& worker_window_list = workspace.peak_power_summary.get_window_list();
    peak_power_summary.get_window_list()
        .insert(peak_power_summary.get_window_list().end(), worker_window_list.begin(), worker_window_list.end());
  }
  std::vector<PeakPowerWindow>& window_list = peak_power_summary.get_window_list();
  std::sort(
      window_list.begin(),
      window_list.end(),
      [](const PeakPowerWindow& left_window, const PeakPowerWindow& right_window) {
        return left_window.window_index < right_window.window_index;
      }
  );
}

// 峰值流程固定使用 time_based 口径；折算系数与初始 X 处理从配置读取，默认值与 PTPX 一致。
vcd::ActivityModelOptions PeakPowerEngine::buildActivityModelOptions() const
{
  vcd::ActivityModelOptions options;
  options.model = vcd::ActivityModel::kTimeBased;
  options.x_transition_derate_factor = PWDM.getConfig().x_transition_derate_factor;
  options.include_initial_x_transitions = PWDM.getConfig().include_initial_x_transitions;
  return options;
}

void PeakPowerEngine::updatePeakPower(const vcd::VcdWindowSignalActivity& window_activity)
{
  Database& database = PWDM.getDatabase();
  const double window_begin_time = window_activity.get_window_begin_time();
  const double window_end_time = window_activity.get_window_end_time();
  const std::size_t window_index = window_activity.get_window_index();

  PeakPowerSummary& peak_power_summary = database.get_peak_power_summary();
  peak_power_summary.get_window_list().push_back(
      {window_index,
       window_begin_time,
       window_end_time,
       database.get_power_summary().get_total_power_value().get_total_power()}
  );
  peak_power_summary.get_total_peak_value()
      .update(database.get_power_summary().get_total_power_value(), window_begin_time, window_end_time, window_index);
  std::vector<InstancePower>& instance_power_list = database.get_instance_power_list();
  for (std::size_t instance_id = 0; instance_id < instance_power_list.size(); instance_id++) {
    peak_power_summary.getOrCreateInstancePeakValue(static_cast<uint32_t>(instance_id))
        .update(instance_power_list[instance_id].get_power_value(), window_begin_time, window_end_time, window_index);
  }
}

}  // namespace ipw
