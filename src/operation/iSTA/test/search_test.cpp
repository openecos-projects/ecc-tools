// SPDX-License-Identifier: MulanPSL-2.0
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "PathSearcher.hpp"
#include "RequiredPropagator.hpp"
#include "TimingTolerance.hpp"
#include "sdc/ExceptionMatcher.hpp"

namespace {
using namespace ista;
static_assert(!std::is_constructible_v<PathEnd, TimingPathState&&, PathCheck, double, double>, "PathEnd must not borrow a temporary propagation path");
void require(bool ok, const char* message)
{
  if (!ok)
    throw std::runtime_error(message);
}

void testCaptureClocks()
{
  Database db;
  Config config;
  DelayCalculator delay(db, config);
  std::string endpoint = "ff:D";
  auto& ff = db.network().instances()["ff"];
  ff.set_is_sequential(true);
  ff.set_clock_pin_name("ff:CK");
  ff.set_data_pin_name(endpoint);
  db.network().pins()[endpoint].set_instance_name("ff");
  db.network().pins()["ff:CK"].set_instance_name("ff");
  db.network().pins()["in"].set_is_port(true);
  auto& ck = db.graph().points()["ff:CK"];
  ck.set_is_clock_point(true);
  ck.setDefaultClock("a");
  ck.setLaunchClock("a");
  for (const auto* name : {"launch", "a", "b", "source_only"}) {
    auto& clock = db.sdc().get_clock_map()[name];
    clock.set_clock_name(name);
    clock.set_period(10.0);
  }
  for (const auto* clock : {"a", "b"})
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
      ck.get_clock_state(clock).waveforms[TransType::kRise].arrival_map[mode][TransType::kRise] = 0.;
  ck.get_clock_state("source_only").is_source_path_only = true;
  db.sdc().get_clock_map()["source_only"].set_setup_uncertainty(100.0);
  TimingCheckArc check;
  check.set_data_port(endpoint);
  check.set_clock_port("ff:CK");
  check.set_check_type(TimingCheckType::kSetup);
  check.set_check_time(1.0);
  ff.get_check_arc_list().push_back(check);
  check.set_check_type(TimingCheckType::kHold);
  check.set_check_time(0.25);
  ff.get_check_arc_list().push_back(check);
  for (auto analysis : {AnalysisType::kMax, AnalysisType::kMin}) {
    for (const auto* tag : {"a_path", "b_path"}) {
      auto& path = db.graph().points()[endpoint].get_path_state_map()[analysis][PathSourceType::kInput][TransType::kRise][tag];
      path.set_start_point("in");
      path.set_clock_name("launch");
      path.set_path_state_tag(tag);
      path.set_trans_type(TransType::kRise);
      path.set_arrival(analysis == AnalysisType::kMax ? 3.0 : 1.0);
    }
  }
  PathChecker path_check(db, delay);
  ExceptionThroughIndex index;
  PathSearcher search(db, path_check, index);
  const auto find_paths = [&] {
    index.rebuild(db.sdc(), db.network());
    const ExceptionMatcher matcher(db.sdc(), db.network());
    for (auto& [analysis, sources] : db.graph().points()[endpoint].get_path_state_map())
      for (auto& [source, transitions] : sources)
        for (auto& [transition, paths] : transitions)
          for (auto& [tag, path] : paths)
            path.set_exception_state_list(
                matcher.initState(path.get_start_point(), path.get_clock_name(), transition, analysis, path.get_launch_clock_trans_type()));
    return search.findPathEnds(endpoint);
  };
  auto paths = find_paths();
  require(paths.size() == 2, "setup/hold candidates missing");
  require(paths[0].get_capture_clock_name() == "a" && paths[0].get_required_time() == 9.0 && paths[0].get_slack() == 6.0,
          "equal capture clocks must retain first clock; source-only traces are not capture clocks");
  require(paths[1].get_capture_clock_name() == "a" && paths[1].get_required_time() == 0.25 && paths[1].get_slack() == 0.75, "hold capture or sign changed");
  db.sdc().get_clock_map()["b"].set_setup_uncertainty(2.0);
  db.sdc().get_clock_map()["b"].set_hold_uncertainty(0.5);
  paths = find_paths();
  require(paths[0].get_capture_clock_name() == "b" && paths[0].get_required_time() == 7.0 && paths[0].get_slack() == 4.0,
          "setup must evaluate each capture independently");
  require(paths[1].get_capture_clock_name() == "b" && paths[1].get_required_time() == 0.75 && paths[1].get_slack() == 0.25,
          "hold must evaluate each capture independently");
  TimingException cut;
  cut.set_type(TimingExceptionType::kFalsePath);
  cut.set_to_objects({{ExceptionObjectType::kClock, "b"}});
  cut.set_hold(false);
  db.sdc().get_path_exception_list().push_back(cut);
  paths = find_paths();
  require(paths[0].get_capture_clock_name() == "a" && paths[0].get_required_time() == 9.0 && paths[1].get_capture_clock_name() == "b",
          "capture-specific setup false path leaked into hold");
  db.sdc().get_path_exception_list().clear();
  TimingException mcp;
  mcp.set_type(TimingExceptionType::kMulticycle);
  mcp.set_to_objects({{ExceptionObjectType::kClock, "b"}});
  mcp.set_setup_multiplier(2);
  db.sdc().get_path_exception_list().push_back(mcp);
  db.sdc().get_clock_map()["b"].set_period(20.0);
  paths = find_paths();
  require(paths[0].get_capture_clock_name() == "a" && paths[0].get_required_time() == 9.0, "multicycle used a different capture context");
  cut.set_to_objects({{ExceptionObjectType::kClock, "a"}});
  db.sdc().get_path_exception_list().push_back(cut);
  paths = find_paths();
  require(paths[0].get_capture_clock_name() == "b" && paths[0].get_required_time() == 27.0 && paths[0].get_required_time_adjustment() == 20.0,
          "multicycle used launch period instead of selected capture period");
  db.sdc().get_clock_map()["b"].set_period(10.0);
  db.sdc().get_path_exception_list().clear();
  TimingException maximum;
  maximum.set_type(TimingExceptionType::kMaxDelay);
  maximum.set_to_objects({{ExceptionObjectType::kClock, "b"}});
  maximum.set_delay(5.0);
  db.sdc().get_path_exception_list().push_back(maximum);
  paths = find_paths();
  require(paths[0].get_capture_clock_name() == "b" && paths[0].get_required_time() == 2.0 && paths[0].get_slack() == -1.0,
          "path-delay exception lost capture-dependent check/uncertainty margin");
  db.sdc().get_path_exception_list().clear();
  db.sdc().get_clock_map()["b"].set_setup_uncertainty(0.0);
  auto& states = db.graph().points()[endpoint].get_path_state_map()[AnalysisType::kMax][PathSourceType::kInput][TransType::kRise];
  states.at("b_path").set_arrival(3.0 + ista::kTimingTolerance / 4);
  paths = find_paths();
  require(paths[0].get_path_delay() == 3.0 && paths[0].get_capture_clock_name() == "a", "near-equal slack must preserve ordered path/capture selection");
  // Each inner reduction sees only a 0.75 * ista::kTimingTolerance difference. A flattened
  // reduction would incorrectly accumulate two such differences and select b.
  states.at("b_path").set_arrival(3.0 + 0.75 * ista::kTimingTolerance);
  db.sdc().get_clock_map()["b"].set_setup_uncertainty(0.75 * ista::kTimingTolerance);
  paths = find_paths();
  require(paths[0].get_path_delay() == 3.0 && paths[0].get_capture_clock_name() == "a" && paths[0].get_slack() == 6.0,
          "nested capture/path reduction was flattened across non-transitive tolerance");
  const auto repeat = find_paths();
  require(repeat.size() == paths.size(), "repeated search changed candidate count");
  cut.set_to_objects({{ExceptionObjectType::kClock, "a"}, {ExceptionObjectType::kClock, "b"}});
  cut.set_hold(true);
  db.sdc().get_path_exception_list().push_back(cut);
  require(find_paths().empty(), "all-false paths retained a previous endpoint result");
  const auto& path = states.at("a_path");
  PathCheck check_a, check_b;
  check_a.capture_clock = "a";
  check_a.required = 9.0;
  check_b.capture_clock = "b";
  check_b.required = 7.0;
  const PathEnd a(path, check_a, 9.0, 6.0), b(path, check_b, 7.0, 4.0);
  require(&a.path() == &b.path() && a.captureClock() == "a" && b.captureClock() == "b" && a.required() == 9.0 && b.required() == 7.0
              && path.get_clock_name() == "launch",
          "independent endpoint results must share only a const propagation path");
}
}  // namespace

int main()
{
  ista::Logger::initInst();
  ista::Database first, second;
  ista::Config config;
  ista::DelayCalculator first_delay(first, config), second_delay(second, config);
  std::string end = "out";
  for (auto* database : {&first, &second}) {
    database->graph().endpoints().push_back(end);
    auto& pin = database->network().pins()[end];
    pin.set_is_port(true);
    pin.set_direction(ista::PinDirection::kOutput);
  }
  first.graph().points()[end].set_arrival(3.0);
  second.graph().points()[end].set_arrival(7.0);
  ista::PathChecker first_check(first, first_delay);
  ista::ExceptionThroughIndex first_index, second_index;
  ista::PathSearcher search(first, first_check, first_index);
  ista::PathChecker second_check(second, second_delay);
  ista::PathSearcher other(second, second_check, second_index);
  ista::RequiredPropagator(first, first_check).propagate();
  ista::RequiredPropagator(second, second_check).propagate();
  if (first.graph().points()[end].get_required() != 3.0 || second.graph().points()[end].get_required() != 7.0 || !search.findPathEnds(end).empty()
      || !other.findPathEnds(end).empty()) {
    throw std::runtime_error("search changed unconstrained required fallback or borrowed database isolation");
  }
  // A constrained output exercises all four candidate dimensions without a
  // global DataManager. Required values and emission order are observable API.
  auto& clock = first.sdc().get_clock_map()["clk"];
  clock.set_clock_name("clk");
  clock.set_period(10.0);
  auto& constraint = first.sdc().get_port_constraint_map()[end];
  std::vector<TimingIoDelay> delays;
  for (auto mode : {AnalysisType::kMax, AnalysisType::kMin}) {
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      TimingIoDelay delay;
      delay.set_clock_name("clk");
      delay.set_analysis_type(mode);
      delay.set_trans_type(edge);
      delay.set_delay(mode == AnalysisType::kMax ? 2.0 : 0.5);
      delays.push_back(delay);
    }
  }
  constraint.set_output_delays(delays, false);
  for (auto analysis : {ista::AnalysisType::kMax, ista::AnalysisType::kMin}) {
    for (auto source : {ista::PathSourceType::kInput, ista::PathSourceType::kRegister}) {
      auto& state = first.graph().points()[end].get_path_state_map()[analysis][source][ista::TransType::kRise]["root"];
      state.set_start_point(end);
      state.set_clock_name("clk");
      state.set_trans_type(ista::TransType::kRise);
      state.set_path_state_tag("root");
      state.set_arrival(analysis == ista::AnalysisType::kMax ? 3.0 : 1.0);
    }
  }
  ista::RequiredPropagator(first, first_check).propagate();
  auto paths = search.findPathEnds(end);
  if (paths.size() != 4)
    throw std::runtime_error("constrained output candidate dimensions changed");
  for (std::size_t i = 0; i < paths.size(); ++i) {
    const auto analysis = i < 2 ? ista::AnalysisType::kMax : ista::AnalysisType::kMin;
    const auto source = i % 2 == 0 ? ista::PathSourceType::kInput : ista::PathSourceType::kRegister;
    if (paths[i].get_analysis_type() != analysis || paths[i].get_source_type() != source || paths[i].get_required_time() != (i < 2 ? 8.0 : -0.5)
        || paths[i].get_slack() != (i < 2 ? 5.0 : 1.5) || paths[i].get_capture_clock_name() != "clk" || paths[i].get_point_list().size() != 1) {
      throw std::runtime_error("output required/slack, path construction or candidate order changed");
    }
  }
  for (const auto& [analysis, sources] : first.graph().points()[end].get_path_state_map())
    for (const auto& [source, transitions] : sources)
      for (const auto& [transition, states] : transitions)
        for (const auto& [tag, state] : states)
          if (state.get_clock_name() != "clk" || state.get_start_point() != end || state.get_path_state_tag() != tag || !state.get_predecessor().empty()
              || state.get_arrival() != (analysis == ista::AnalysisType::kMax ? 3.0 : 1.0))
            throw std::runtime_error("endpoint evaluation overwrote propagated path");
  testCaptureClocks();
  ista::Logger::destroyInst();
}
