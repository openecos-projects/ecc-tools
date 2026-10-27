// SPDX-License-Identifier: MulanPSL-2.0
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "DelayCalculator.hpp"
#include "TimingAnalyzer.hpp"

using namespace ista;

static void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}

static void checkOutputExceptions(int scenario, bool reverse, AnalysisType analysis)
{
  Database db;
  Config config;
  for (const auto* name : {"in", "out"}) {
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_pin_name(name);
    pin.set_is_port(true);
    pin.set_net_name("wire");
    pin.set_direction(std::string(name) == "in" ? PinDirection::kInput : PinDirection::kOutput);
  }
  auto& net = db.network().nets()["wire"];
  net.set_net_name("wire");
  net.set_pin_name_list({"in", "out"});
  for (const auto* name : {"launch", "a", "b"}) {
    auto& clock = db.sdc().get_clock_map()[name];
    clock.set_clock_name(name);
    clock.set_period(10);
    clock.set_fall_edge(5);
  }
  std::vector<TimingIoDelay> inputs, outputs;
  for (auto transition : {TransType::kRise, TransType::kFall}) {
    TimingIoDelay input;
    input.set_clock_name("launch");
    input.set_analysis_type(analysis);
    input.set_trans_type(transition);
    input.set_delay(1);
    inputs.push_back(input);
    for (const auto* clock : {"a", "b"}) {
      TimingIoDelay output;
      output.set_clock_name(clock);
      output.set_analysis_type(analysis);
      output.set_trans_type(transition);
      output.set_clock_trans_type(std::string(clock) == "a" ? TransType::kRise : TransType::kFall);
      output.set_delay(std::string(clock) == "a" ? 2 : analysis == AnalysisType::kMax ? 4 : -8);
      outputs.push_back(output);
    }
  }
  if (reverse)
    std::reverse(outputs.begin(), outputs.end());
  db.sdc().get_port_constraint_map()["in"].set_input_delays(inputs, false);
  db.sdc().get_port_constraint_map()["in"].set_input_transition(0.1);
  db.sdc().get_port_constraint_map()["out"].set_output_delays(outputs, false);
  if (scenario != 0) {
    TimingException exception;
    exception.set_to_objects({{ExceptionObjectType::kClock, scenario == 2 ? "a" : "b"}});
    exception.set_to_trans_type(scenario == 2 ? TransType::kRise : TransType::kFall);
    exception.set_type(scenario == 3   ? TimingExceptionType::kMulticycle
                       : scenario == 4 ? (analysis == AnalysisType::kMax ? TimingExceptionType::kMaxDelay : TimingExceptionType::kMinDelay)
                                       : TimingExceptionType::kFalsePath);
    if (scenario == 3) {
      exception.set_setup_multiplier(2);
      exception.set_setup_use_end_clock(true);
    }
    if (scenario == 4)
      exception.set_delay(3);
    db.sdc().get_path_exception_list().push_back(exception);
  }
  DelayCalculator delay(db, config);
  TimingAnalyzer(db, delay).analyze();
  TimingPath* selected = nullptr;
  for (auto& group : db.pathGroups())
    for (auto& [endpoint, paths] : group.endpointPaths())
      for (auto& path : paths)
        if (endpoint == "out" && path.get_analysis_type() == analysis)
          selected = &path;
  require(selected != nullptr, "one capture exception removed all output paths");
  const bool choose_a = scenario == 1 || (scenario == 3 && analysis == AnalysisType::kMax);
  // Hold pairs the previous falling edge (-5); setup pairs the next one (+5).
  const double required = choose_a                         ? (analysis == AnalysisType::kMax ? 8 : -2)
                          : scenario == 4                  ? (analysis == AnalysisType::kMax ? -1 : 11)
                          : scenario == 3                  ? 13
                          : analysis == AnalysisType::kMax ? 1
                                                           : 3;
  require(selected->get_capture_clock_name() == (choose_a ? "a" : "b"), "output exception was applied after discarding the other capture candidate");
  require(std::abs(selected->get_required_time() - required) < 1e-8, "output exception used a different capture clock or edge");
  require(selected->get_output_delay() == (choose_a ? 2 : analysis == AnalysisType::kMax ? 4 : -8), "selected output delay and exception context disagree");
}

static void checkInputEdges(int excluded_edge, bool reverse, AnalysisType analysis)
{
  Database db;
  Config config;
  for (const auto* name : {"in", "out"}) {
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_pin_name(name);
    pin.set_is_port(true);
    pin.set_net_name("wire");
    pin.set_direction(std::string(name) == "in" ? PinDirection::kInput : PinDirection::kOutput);
  }
  auto& net = db.network().nets()["wire"];
  net.set_net_name("wire");
  net.set_pin_name_list({"in", "out"});
  auto& clock = db.sdc().get_clock_map()["clock"];
  clock.set_clock_name("clock");
  clock.set_period(10);
  clock.set_fall_edge(5);
  std::vector<TimingIoDelay> inputs, outputs;
  for (auto transition : {TransType::kRise, TransType::kFall}) {
    for (auto launch_edge : {TransType::kRise, TransType::kFall}) {
      TimingIoDelay input;
      input.set_clock_name("clock");
      input.set_analysis_type(analysis);
      input.set_trans_type(transition);
      input.set_clock_trans_type(launch_edge);
      input.set_delay(launch_edge == TransType::kRise ? 2 : 1);
      inputs.push_back(input);
    }
    TimingIoDelay output;
    output.set_clock_name("clock");
    output.set_analysis_type(analysis);
    output.set_trans_type(transition);
    output.set_clock_trans_type(TransType::kFall);
    outputs.push_back(output);
  }
  if (reverse)
    std::reverse(inputs.begin(), inputs.end());
  db.sdc().get_port_constraint_map()["in"].set_input_delays(inputs, false);
  db.sdc().get_port_constraint_map()["in"].set_input_transition(0.1);
  db.sdc().get_port_constraint_map()["out"].set_output_delays(outputs, false);
  if (excluded_edge != 0) {
    TimingException exception;
    exception.set_type(TimingExceptionType::kFalsePath);
    exception.set_from_objects({{ExceptionObjectType::kClock, "clock"}});
    exception.set_from_trans_type(excluded_edge == 1 ? TransType::kRise : TransType::kFall);
    db.sdc().get_path_exception_list().push_back(exception);
  }
  DelayCalculator delay(db, config);
  TimingAnalyzer(db, delay).analyze();
  TimingPath* selected = nullptr;
  for (auto& group : db.pathGroups())
    for (auto& [endpoint, paths] : group.endpointPaths())
      for (auto& path : paths)
        if (endpoint == "out" && path.get_analysis_type() == analysis)
          selected = &path;
  require(selected, "launch-edge false path removed the other input clock edge");
  const bool fall = excluded_edge == 1 || (excluded_edge == 0 && analysis == AnalysisType::kMin);
  const double required = analysis == AnalysisType::kMax ? (fall ? 15 : 5) : (fall ? 5 : -5);
  require(selected->get_launch_clock_transition() == (fall ? TransType::kFall : TransType::kRise),
          "propagation merged different input launch edges before endpoint checking");
  require(std::abs(selected->get_required_time() - required) < 1e-8 && selected->get_path_delay() == (fall ? 6 : 2),
          "input path check lost its launch-edge relationship");
}

static void checkClocklessIo(bool input_clock, bool output_clock, bool extra_clock, bool path_delay, AnalysisType analysis)
{
  Database db;
  Config config;
  for (const auto* name : {"in", "out"}) {
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_pin_name(name);
    pin.set_is_port(true);
    pin.set_net_name("wire");
    pin.set_direction(std::string(name) == "in" ? PinDirection::kInput : PinDirection::kOutput);
  }
  auto& net = db.network().nets()["wire"];
  net.set_net_name("wire");
  net.set_pin_name_list({"in", "out"});
  for (const auto* name : {"M", "A"}) {
    if (std::string(name) == "A" && !extra_clock)
      continue;
    auto& clock = db.sdc().get_clock_map()[name];
    clock.set_clock_name(name);
    clock.set_period(10);
    clock.set_fall_edge(5);
  }
  std::vector<TimingIoDelay> inputs, outputs;
  for (auto transition : {TransType::kRise, TransType::kFall}) {
    TimingIoDelay input, output;
    input.set_clock_name(input_clock ? "M" : "");
    input.set_delay(.3);
    output.set_clock_name(output_clock ? "M" : "");
    output.set_delay(.4);
    for (auto* value : {&input, &output}) {
      value->set_analysis_type(analysis);
      value->set_trans_type(transition);
    }
    inputs.push_back(input);
    outputs.push_back(output);
  }
  db.sdc().get_port_constraint_map()["in"].set_input_delays(inputs, false);
  db.sdc().get_port_constraint_map()["in"].set_input_transition(.1);
  db.sdc().get_port_constraint_map()["out"].set_output_delays(outputs, false);
  if (path_delay) {
    TimingException exception;
    exception.set_type(analysis == AnalysisType::kMax ? TimingExceptionType::kMaxDelay : TimingExceptionType::kMinDelay);
    exception.set_from_objects({{ExceptionObjectType::kPort, "in"}});
    exception.set_to_objects({{ExceptionObjectType::kPort, "out"}});
    exception.set_delay(analysis == AnalysisType::kMax ? 2 : .1);
    db.sdc().get_path_exception_list().push_back(exception);
  }
  DelayCalculator delay(db, config);
  TimingAnalyzer(db, delay).analyze();
  TimingPath* selected = nullptr;
  for (auto& group : db.pathGroups())
    for (auto& [endpoint, paths] : group.endpointPaths())
      for (auto& path : paths)
        if (endpoint == "out" && path.get_analysis_type() == analysis)
          selected = &path;
  require(bool(selected) == (path_delay || (input_clock && output_clock)), "clockless I/O acquired a fabricated clock check");
  if (selected) {
    require(selected->get_clock_name() == (input_clock ? "M" : ""), "clockless launch acquired unrelated clock");
    require(selected->get_capture_clock_name() == (output_clock ? "M" : ""), "clockless capture acquired unrelated clock");
    require(std::abs(selected->get_path_delay() - .3) < 1e-9, "clockless arrival lost input delay");
  }
}

int main()
{
  for (auto mode : {AnalysisType::kMax, AnalysisType::kMin})
    for (bool extra : {false, true})
      for (bool input : {false, true})
        for (bool output : {false, true})
          for (bool exception : {false, true})
            checkClocklessIo(input, output, extra, exception, mode);
  for (auto mode : {AnalysisType::kMax, AnalysisType::kMin})
    for (bool reverse : {false, true})
      for (int scenario : {0, 1, 2, 3, 4})
        checkOutputExceptions(scenario, reverse, mode);
  for (auto mode : {AnalysisType::kMax, AnalysisType::kMin})
    for (bool reverse : {false, true})
      for (int excluded_edge : {0, 1, 2})
        checkInputEdges(excluded_edge, reverse, mode);
}
