// SPDX-License-Identifier: MulanPSL-2.0
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "PathChecker.hpp"
#include "sdc/ExceptionMatcher.hpp"

namespace {
void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}
}  // namespace

static void registerChecks(bool reverse, bool split_tables, ista::AnalysisType mode)
{
  using namespace ista;
  Database db;
  Config config;
  for (const auto* name : {"launch", "a", "b"}) {
    auto& clock = db.sdc().get_clock_map()[name];
    clock.set_clock_name(name);
    clock.set_period(10);
    clock.set_fall_edge(5);
  }
  auto& input = db.network().pins()["in"];
  input.set_is_port(true);
  auto& instance = db.network().instances()["ff"];
  instance.set_is_sequential(true);
  instance.set_clock_pin_name("ff:CK");
  instance.set_data_pin_name("ff:D");
  for (const auto* port : {"D", "CK", "CK2"}) {
    auto name = std::string("ff:") + port;
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_instance_name("ff");
  }
  for (bool second : {false, true}) {
    const auto pin = second ? "ff:CK2" : "ff:CK";
    const auto clock = second ? "b" : "a";
    auto& point = db.graph().points()[pin];
    point.set_is_clock_point(true);
    point.setDefaultClock(clock);
    point.setLaunchClock(clock);
    for (auto analysis : {AnalysisType::kMax, AnalysisType::kMin})
      for (auto edge : {TransType::kRise, TransType::kFall})
        point.get_clock_state(clock).waveforms[edge].arrival_map[analysis][edge] = second ? 0.7 : 0.4;
    TimingCheckArc arc;
    arc.set_clock_port(pin);
    arc.set_data_port("ff:D");
    arc.set_clock_trans_type(second ? TransType::kFall : TransType::kRise);
    arc.set_check_type(mode == AnalysisType::kMax ? TimingCheckType::kSetup : TimingCheckType::kHold);
    TimingArc table;
    for (auto edge : {TransType::kRise, TransType::kFall})
      if (!split_tables || edge == (second ? TransType::kFall : TransType::kRise))
        table.get_check_table_map()[edge].set_value_list({1.0});
    arc.get_timing_arc_list().push_back(table);
    instance.get_check_arc_list().push_back(arc);
  }
  if (reverse)
    std::reverse(instance.get_check_arc_list().begin(), instance.get_check_arc_list().end());
  TimingPathState path;
  path.set_start_point("in");
  path.set_clock_name("launch");
  path.set_launch_clock_trans_type(TransType::kRise);
  path.set_trans_type(TransType::kFall);
  path.set_arrival(1);
  DelayCalculator delay(db, config);
  PathChecker checker(db, delay);
  const auto result = checker.checkPath(path, "ff:D", mode);
  require(result.has_value(), "first check arc hid another valid data transition");
  const auto candidates = checker.checkPathCandidates(path, "ff:D", mode);
  require(candidates.size() >= (split_tables ? 1U : 2U), "candidate API lost a legal capture/check context");
  if (!split_tables) {
    std::set<std::string> capture_pins;
    for (const auto& candidate : candidates)
      capture_pins.insert(candidate.check().capture_pin);
    require(capture_pins == std::set<std::string>{"ff:CK", "ff:CK2"}, "candidate API omitted a capture pin");
  }
  const bool second = split_tables || mode == AnalysisType::kMax;
  const double expected = mode == AnalysisType::kMax ? 4.7 : second ? -3.3 : 1.4;
  require(std::abs(result->required() - expected) < 1e-8 && result->check().capture_clock == (second ? "b" : "a")
              && result->check().capture_pin == (second ? "ff:CK2" : "ff:CK")
              && result->check().capture_transition == (second ? TransType::kFall : TransType::kRise),
          "register check lost the selected capture pin, edge or clock");
  TimingException exception;
  exception.set_type(mode == AnalysisType::kMax ? TimingExceptionType::kMaxDelay : TimingExceptionType::kMinDelay);
  exception.set_to_objects({{ExceptionObjectType::kClock, "b"}});
  exception.set_to_trans_type(TransType::kFall);
  exception.set_delay(3);
  db.sdc().get_path_exception_list().push_back(exception);
  path.set_exception_state_list(ExceptionMatcher(db.sdc(), db.network()).initState("in", "launch", TransType::kFall, mode, TransType::kRise));
  const auto constrained = checker.checkPath(path, "ff:D", mode);
  require(constrained && constrained->check().capture_pin == "ff:CK2" && std::abs(constrained->required() - (mode == AnalysisType::kMax ? 2.7 : 4.7)) < 1e-8,
          "path delay exception reselected the first register capture pin");

  db.sdc().get_path_exception_list().clear();
  path.set_exception_state_list({});
  db.sdc().timingDerates().set(mode, false, TransType::kFall, DerateDelay::kCheck, 1.2, DerateObject::kCell, "ff");
  const auto derated = checker.checkPath(path, "ff:D", mode);
  require(derated && std::abs(derated->check().check_time - 1.2) < 1e-9, "scoped check derate did not reach endpoint evaluation");
  require(std::abs(derated->required() - (expected + (mode == AnalysisType::kMax ? -.2 : .2))) < 1e-8, "check derate used the wrong sign in required time");
  db.sdc().timingDerates().set(mode, false, TransType::kFall, DerateDelay::kCheck, .1, DerateObject::kDesign, "", true);
  const auto incremented = checker.checkPath(path, "ff:D", mode);
  require(incremented && std::abs(incremented->check().check_time - 1.3) < 1e-9, "scoped check baseline lost global check increment");
  // PrimeTime negative-check fixture: -0.2 at factor 1.3 becomes -0.14.
  for (auto& arc : instance.get_check_arc_list())
    for (auto& table : arc.get_timing_arc_list())
      for (auto& [edge, values] : table.get_check_table_map())
        values.set_value_list({-.2});
  const auto negative = checker.checkPath(path, "ff:D", mode);
  require(negative && std::abs(negative->check().check_time + .14) < 1e-9, "negative check derate disagrees with PT absolute-variation semantics");
}

// A recovery/removal table constrains the asynchronous pin; CK is related_pin.
// f(data_slew, clock_slew) = data_slew + 2*clock_slew deliberately distinguishes axes.
static void asynchronousChecks()
{
  using namespace ista;
  for (auto type : {TimingCheckType::kRecovery, TimingCheckType::kRemoval}) {
    Database db;
    Config config;
    auto& clock = db.sdc().get_clock_map()["clk"];
    clock.set_clock_name("clk");
    clock.set_period(10.);
    clock.set_fall_edge(5.);
    auto& instance = db.network().instances()["ff"];
    instance.set_is_sequential(true);
    instance.set_clock_pin_name("ff:CK");
    for (auto name : {"ff:RN", "ff:CK"}) {
      auto& pin = db.network().pins()[name];
      pin.set_full_name(name);
      pin.set_instance_name("ff");
    }
    auto& ck = db.graph().points()["ff:CK"];
    ck.set_is_clock_point(true);
    ck.setDefaultClock("clk");
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
      ck.get_clock_state("clk").waveforms[TransType::kRise].arrival_map[mode][TransType::kRise] = .5;
      ck.get_clock_state("clk").waveforms[TransType::kRise].slew_map[mode][TransType::kRise] = .1;
    }
    TimingCheckArc arc;
    arc.set_clock_port("ff:CK");
    arc.set_data_port("ff:RN");
    arc.set_clock_trans_type(TransType::kRise);
    arc.set_check_type(type);
    TimingArc timing;
    auto& table = timing.get_check_table_map()[TransType::kRise];
    table.set_variable_type1(TimingTableVariableType::kConstrainedTransition);
    table.set_axis_list({{0., 1.}, {0., 1.}});
    table.set_value_list({0., 2., 1., 3.});
    arc.get_timing_arc_list().push_back(timing);
    instance.get_check_arc_list().push_back(arc);
    TimingPathState path;
    path.set_start_point("in");
    path.set_clock_name("clk");
    path.set_launch_clock_trans_type(TransType::kRise);
    path.set_trans_type(TransType::kRise);
    path.set_slew(.3);
    path.set_arrival(1.);
    DelayCalculator delay(db, config);
    PathChecker checker(db, delay);
    const auto analysis = type == TimingCheckType::kRecovery ? AnalysisType::kMax : AnalysisType::kMin;
    const auto result = checker.checkPath(path, "ff:RN", analysis);
    require(result && std::abs(result->check().check_time - .5) < 1e-9, "asynchronous check swapped constrained and related slews");
    require(std::abs(result->required() - (type == TimingCheckType::kRecovery ? 10. : 1.)) < 1e-9,
            "recovery must subtract its check from capture; removal must add it");
    db.sdc().timingDerates().set(analysis, false, TransType::kRise, DerateDelay::kCheck, 1.4);
    const auto derated = checker.checkPath(path, "ff:RN", analysis);
    require(derated && std::abs(derated->check().check_time - .7) < 1e-9, "cell_check omitted recovery/removal");
  }
}

static void checkSlewRange()
{
  using namespace ista;
  Database db;
  Config config;
  auto& clock = db.sdc().get_clock_map()["clk"];
  clock.set_clock_name("clk");
  clock.set_period(10.);
  auto& instance = db.network().instances()["ff"];
  instance.set_is_sequential(true);
  instance.set_clock_pin_name("ff:CK");
  for (auto name : {"ff:D", "ff:CK"}) {
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_instance_name("ff");
  }
  auto& point = db.graph().points()["ff:CK"];
  point.set_is_clock_point(true);
  point.setDefaultClock("clk");
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
    point.get_clock_state("clk").waveforms[TransType::kRise].arrival_map[mode][TransType::kRise] = 0.;
    point.get_clock_state("clk").waveforms[TransType::kRise].slew_map[mode][TransType::kRise] = mode == AnalysisType::kMin ? .1 : .2;
    db.graph().points()["ff:D"].get_data_slew_map()[mode][TransType::kRise] = mode == AnalysisType::kMin ? .1 : .3;
  }
  TimingCheckArc arc;
  arc.set_clock_port("ff:CK");
  arc.set_data_port("ff:D");
  arc.set_clock_trans_type(TransType::kRise);
  arc.set_check_type(TimingCheckType::kSetup);
  TimingArc timing;
  auto& table = timing.get_check_table_map()[TransType::kRise];
  // f(data,clock)=1-data+clock: the fastest data slew gives the worst setup check.
  table.set_variable_type1(TimingTableVariableType::kConstrainedTransition);
  table.set_axis_list({{0., 1.}, {0., 1.}});
  table.set_value_list({1., 2., 0., 1.});
  arc.get_timing_arc_list().push_back(timing);
  instance.get_check_arc_list().push_back(arc);
  TimingPathState path;
  path.set_start_point("in");
  path.set_clock_name("clk");
  path.set_launch_clock_trans_type(TransType::kRise);
  path.set_trans_type(TransType::kRise);
  path.set_slew(.3);
  path.set_arrival(1.);
  DelayCalculator delay(db, config);
  PathChecker checker(db, delay);
  auto result = checker.checkPath(path, "ff:D", AnalysisType::kMax);
  require(result && std::abs(result->check().check_time - 1.1) < 1e-9, "setup check did not evaluate the physical slew range");
  instance.get_check_arc_list().front().set_check_type(TimingCheckType::kHold);
  path.set_slew(.1);
  result = checker.checkPath(path, "ff:D", AnalysisType::kMin);
  require(result && std::abs(result->check().check_time - 1.1) < 1e-9, "hold must use the most restrictive check across physical slews");
}

int main()
{
  using namespace ista;
  Logger::initInst();
  asynchronousChecks();
  checkSlewRange();
  for (auto mode : {AnalysisType::kMax, AnalysisType::kMin})
    for (bool reverse : {false, true})
      for (bool split_tables : {false, true})
        registerChecks(reverse, split_tables, mode);
  Database db, other;
  Config config;
  DelayCalculator delay(db, config), other_delay(other, config);
  for (auto* database : {&db, &other}) {
    auto& pin = database->network().pins()["out"];
    pin.set_is_port(true);
    pin.set_direction(PinDirection::kOutput);
    auto& clock = database->sdc().get_clock_map()["clk"];
    clock.set_clock_name("clk");
    clock.set_period(10.0);
    auto& constraint = database->sdc().get_port_constraint_map()["out"];
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
  }
  other.sdc().get_clock_map()["clk"].set_period(20.0);
  PathChecker check(db, delay);
  PathChecker second(other, other_delay);
  TimingPathState path;
  path.set_start_point("out");
  path.set_clock_name("clk");
  path.set_trans_type(TransType::kRise);
  path.set_arrival(3.0);
  const double setup = check.checkPath(path, "out", AnalysisType::kMax)->required();
  const double hold = check.checkPath(path, "out", AnalysisType::kMin)->required();
  require(setup == 8.0 && hold == -0.5, "output setup/hold boundary changed");
  require(second.checkPath(path, "out", AnalysisType::kMax)->required() == 18.0, "endpoint check borrowed a different database");
  require(check.calcPathSlack(path, setup, AnalysisType::kMax) == 5.0 && check.calcPathSlack(path, hold, AnalysisType::kMin) == 3.5, "slack sign changed");
  TimingException maximum;
  maximum.set_type(TimingExceptionType::kMaxDelay);
  maximum.set_to_objects({{ExceptionObjectType::kPort, "out"}});
  maximum.set_delay(5.0);
  db.sdc().get_path_exception_list().push_back(maximum);
  path.set_exception_state_list(ExceptionMatcher(db.sdc(), db.network()).initState("out", "clk", TransType::kRise, AnalysisType::kMax, TransType::kRise));
  require(check.checkPath(path, "out", AnalysisType::kMax)->required() == 3.0, "max-delay exception lost the output-delay margin");
  require(path.get_arrival() == 3.0 && path.get_clock_name() == "clk" && path.get_predecessor().empty(), "check changed the propagated path");
  require(db.pathGroups().empty() && other.pathGroups().empty(), "check published path results");
  db.sdc().get_path_exception_list().clear();
  TimingIoDelay rise_only;
  rise_only.set_clock_name("clk");
  rise_only.set_clock_trans_type(TransType::kFall);
  rise_only.set_delay(2.0);
  db.sdc().get_port_constraint_map().at("out") = TimingPortConstraint{};
  db.sdc().get_port_constraint_map().at("out").set_output_delays({rise_only}, false);
  path.set_trans_type(TransType::kFall);
  path.set_exception_state_list({});
  require(!check.checkPath(path, "out", AnalysisType::kMax), "rise-only output delay fabricated a falling output check");
  db.sdc().get_path_exception_list().push_back(maximum);
  path.set_exception_state_list(ExceptionMatcher(db.sdc(), db.network()).initState("out", "clk", TransType::kFall, AnalysisType::kMax, TransType::kRise));
  const auto constrained = check.checkPath(path, "out", AnalysisType::kMax);
  require(constrained && constrained->required() == 5.0 && !constrained->check().output_delay,
          "explicit max delay must still constrain an edge without output delay");
  db.sdc().get_path_exception_list().clear();
  rise_only.set_analysis_type(AnalysisType::kMax);
  db.sdc().get_port_constraint_map().at("out").set_output_delays({rise_only}, false);
  path.set_trans_type(TransType::kRise);
  path.set_exception_state_list({});
  require(!check.checkPath(path, "out", AnalysisType::kMin), "max-only output delay fabricated a hold constraint");
  maximum.set_type(TimingExceptionType::kMinDelay);
  maximum.set_delay(.5);
  db.sdc().get_path_exception_list().push_back(maximum);
  path.set_exception_state_list(ExceptionMatcher(db.sdc(), db.network()).initState("out", "clk", TransType::kRise, AnalysisType::kMin, TransType::kRise));
  const auto minimum = check.checkPath(path, "out", AnalysisType::kMin);
  require(minimum && std::abs(minimum->required() - .5) < 1e-9 && !minimum->check().output_delay,
          "explicit min-delay exception lost an otherwise unconstrained output");
  Logger::destroyInst();
}
