// SPDX-License-Identifier: MulanPSL-2.0
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "MinTimingDataExporter.hpp"
#include "SdcCommand.hpp"
#include "SparseTimingSnapshot.hpp"
#include "TimingAnalyzer.hpp"
#include "TimingDataExporter.hpp"
#include "liberty/TimingCellArc.hpp"

using namespace ista;

static void require(bool condition, const std::string& message)
{
  if (!condition)
    throw std::runtime_error(message);
}

static void command(const std::string& text)
{
  require(SdcCommand::getInst().evalString(text) == TCL_OK, "SDC failed: " + text);
}

static void addPin(Database& database, const std::string& name, const std::string& instance, PinDirection direction)
{
  auto& pin = database.network().pins()[name];
  pin.set_full_name(name);
  pin.set_pin_name(instance.empty() ? name : name.substr(name.find(':') + 1));
  pin.set_instance_name(instance);
  pin.set_is_port(instance.empty());
  pin.set_direction(direction);
  if (!instance.empty())
    database.network().instances()[instance].get_pin_name_list().push_back(name);
}

static void addCell(Database& database, const std::string& name, double delay, bool merge, bool non_unate = false)
{
  auto& instance = database.network().instances()[name];
  instance.set_instance_name(name);
  instance.set_cell_name(name);
  auto& cell = database.liberty().get_cell_map()[name];
  cell.set_cell_name(name);
  cell.get_port_map()["Y"].set_is_output(true);
  addPin(database, name + ":Y", name, PinDirection::kOutput);
  for (const auto* input : {"A", "B"}) {
    if (!merge && std::string(input) == "B")
      continue;
    cell.get_port_map()[input].set_is_input(true);
    addPin(database, name + ":" + input, name, PinDirection::kInput);
    TimingCellArc cell_arc;
    cell_arc.set_source_port(input);
    cell_arc.set_sink_port("Y");
    TimingArc arc;
    arc.set_arc_idx(0);
    arc.set_sense(non_unate ? TimingArcSense::kNonUnate : TimingArcSense::kPositive);
    TimingTable table;
    table.set_value_list({delay});
    for (const auto edge : {TransType::kRise, TransType::kFall}) {
      arc.get_delay_table_map()[edge] = table;
      arc.get_slew_table_map()[edge] = table;
    }
    cell_arc.get_timing_arc_list().push_back(arc);
    cell.get_cell_arc_list().push_back(cell_arc);
  }
}

static void addNet(Database& database, const std::string& name, std::vector<std::string> pins)
{
  auto& net = database.network().nets()[name];
  net.set_net_name(name);
  net.set_pin_name_list(std::move(pins));
  for (const auto& pin : net.get_pin_name_list())
    database.network().pins().at(pin).set_net_name(name);
}

static TimingData snapshot(bool model_only)
{
  auto& database = STADM.getDatabase();
  auto result = TimingDataExporter::exportData(database, STADM.getConfig());
  if (model_only) {
    exportMinTimingModel(result, database, STADM.getConfig());
    SparseTimingExportAdapter::compileModel(result);
  } else {
    exportMinTimingData(result, database, STADM.getConfig());
    SparseTimingExportAdapter::compile(result);
  }
  return result;
}

static std::vector<std::array<double, 2>> propagateSparse(const TimingData& data, const SparseTimingSnapshot& view)
{
  require(view.enabled && view.unsupported.empty(), "through sparse model is not supported");
  std::vector<std::array<double, 2>> arrivals(view.states.size(), {view.early ? INFINITY : -INFINITY, view.early ? INFINITY : -INFINITY});
  std::vector<std::size_t> order(view.states.size());
  for (std::size_t index = 0; index < order.size(); ++index) {
    order[index] = index;
    for (int lane = 0; lane < 2; ++lane)
      if (view.states[index].seed[lane])
        arrivals[index][lane] = view.states[index].reference[lane];
  }
  std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return view.states[a].level < view.states[b].level; });
  for (const auto sink : order) {
    auto propagate = [&](const auto& edges, bool cell) {
      for (const auto& edge : edges) {
        if (static_cast<std::size_t>(edge.sink) != sink)
          continue;
        double delay = 0;
        if (cell) {
          const auto& arc = data.graph_arcs.at(edge.base_arc);
          delay = (arc.library_cell == "a" ? 2.0 : arc.library_cell == "b" ? 5.0 : 1.0) * data.time_unit_ps;
        }
        require(edge.transition_mask != 0, "sparse edge has no legal transition");
        for (int input = 0; input < 2; ++input)
          for (int output = 0; output < 2; ++output)
            if (edge.transition_mask & (1 << (2 * input + output))) {
              require(view.states[edge.source].valid[input] && view.states[sink].valid[output], "edge uses an unreachable lane");
              const double candidate = arrivals[edge.source][input] + delay;
              arrivals[sink][output] = view.early ? std::min(arrivals[sink][output], candidate) : std::max(arrivals[sink][output], candidate);
            }
      }
    };
    propagate(view.cell_edges, true);
    propagate(view.net_edges, false);
  }
  return arrivals;
}

static void checkSparse(const TimingData& data, bool normal_fall, bool setup_exception)
{
  const auto& view = data.sparse;
  const auto arrivals = propagateSparse(data, view);
  std::array<double, 2> worst{INFINITY, INFINITY};
  for (const auto& check : view.checks) {
    if (check.valid)
      worst[check.transition] = std::min(worst[check.transition], check.intercept_ps - arrivals[check.state][check.transition]);
  }
  const std::array<double, 2> expected{setup_exception ? -1.0 : 94.0, !setup_exception || normal_fall ? 94.0 : -1.0};
  for (int lane = 0; lane < 2; ++lane)
    require(std::abs(worst[lane] / data.time_unit_ps - expected[lane]) < 1E-9, "through setup arrival or required rule is wrong");
}

static void checkSparseMin(const TimingData& data, const std::array<double, 2>& expected)
{
  const auto& view = data.sparse_min;
  const auto arrivals = propagateSparse(data, view);
  std::array<double, 2> worst{INFINITY, INFINITY};
  for (const auto& context : data.min_contexts) {
    const int edge = context.data_transition == 1 ? 0 : 1;
    bool found = false;
    for (std::size_t state = 0; state < view.states.size(); ++state) {
      const auto& candidate = view.states[state];
      if (candidate.pin == context.end_point && candidate.role == 0 && view.tags[candidate.tag] == "L\x1f" + context.path_tag
          && view.domains[candidate.domain] == context.launch_clock && candidate.source_type == context.source_type && candidate.valid[edge]) {
        worst[edge] = std::min(worst[edge], (arrivals[state][edge] - context.required_ps) / data.time_unit_ps);
        found = true;
      }
    }
    require(found, "min context has no legal native sparse state");
  }
  for (int edge = 0; edge < 2; ++edge)
    require(std::abs(worst[edge] - expected[edge]) < 1E-9, "through-qualified min sparse arrival is wrong");
}

static void run(bool rise_only, bool non_unate, bool input_selector, bool setup_exception)
{
  DataManager::initInst();
  DelayCalculator::initInst();
  auto& database = STADM.getDatabase();
  STADM.getConfig().thread_number = 1;
  database.network().setName("through_reconvergence");
  addPin(database, "in", "", PinDirection::kInput);
  addPin(database, "out", "", PinDirection::kOutput);
  addCell(database, "a", 2, false, non_unate);
  addCell(database, "b", 5, false);
  addCell(database, "merge", 1, true);
  addNet(database, "input", {"in", "a:A", "b:A"});
  addNet(database, "hit", {"a:Y", "merge:A"});
  addNet(database, "bypass", {"b:Y", "merge:B"});
  addNet(database, "output", {"merge:Y", "out"});
  command("create_clock -name clock -period 100");
  command("set_input_delay -clock clock 0 [get_ports in]");
  command("set_output_delay -clock clock -max 0 [get_ports out]");
  command("set_output_delay -clock clock -min -10 [get_ports out]");
  const std::string through = rise_only ? " -rise_through " : " -through ";
  const std::string selector = input_selector ? "[get_pins a/A]" : "[get_pins a/Y]";
  if (setup_exception)
    command("set_max_delay 2" + through + selector);
  command("set_false_path -hold" + through + selector);

  TimingAnalyzer analyzer(database, STADC);
  analyzer.prepareStructure();
  const auto structural = snapshot(true);
  require(database.pathGroups().empty(), "structural preparation ran full numeric STA");
  analyzer.analyze(true);
  const auto complete = snapshot(false);
  auto min_rules = [](const TimingData& data) {
    std::set<std::tuple<std::string, std::string, std::string, int32_t, int32_t, double>> rules;
    for (const auto& context : data.min_contexts)
      rules.emplace(context.start_point, context.end_point, context.path_tag, context.data_transition,
                    context.launch_transition, context.required_ps - context.check_time_ps);
    return rules;
  };
  require(min_rules(structural) == min_rules(complete), "structural preparation changed through-qualified min rules");
  std::array<double, 2> hold{INFINITY, INFINITY};
  for (const auto& context : complete.min_contexts) {
    const int lane = context.data_transition == 1 ? 0 : 1;
    hold[lane] = std::min(hold[lane], (context.arrival_ps - context.required_ps) / complete.time_unit_ps);
  }
  const bool falling_input_bypasses = rise_only && non_unate && input_selector;
  const bool normal_fall = rise_only && !falling_input_bypasses;
  require(std::abs(hold[0] + (falling_input_bypasses ? 7 : 4)) < 1E-9 && std::abs(hold[1] + (rise_only ? 7 : 4)) < 1E-9,
          "native hold exception did not distinguish hit/bypass or rise/fall");
  checkSparse(structural, normal_fall, setup_exception);
  checkSparse(complete, normal_fall, setup_exception);
  const std::array<double, 2> expected_hold{falling_input_bypasses ? -7.0 : -4.0, rise_only ? -7.0 : -4.0};
  checkSparseMin(structural, expected_hold);
  checkSparseMin(complete, expected_hold);
  STADC.clear();
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
}

int main(int argc, char**)
{
  Logger::initInst();
  SdcCommand::initInst();
  sdc::registerSdcCommands(SdcCommand::getInst());
  for (const bool rise_only : {false, true})
    for (const bool non_unate : {false, true})
      for (const bool input_selector : {false, true})
        run(rise_only, non_unate, input_selector, argc == 1);
  SdcCommand::destroyInst();
  Logger::destroyInst();
}
