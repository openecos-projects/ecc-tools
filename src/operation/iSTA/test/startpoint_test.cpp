// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>
#include <string>
#include <vector>

#include "Config.hpp"
#include "DataSlewPropagator.hpp"
#include "Database.hpp"
#include "DelayCalculator.hpp"
#include "StartpointInitializer.hpp"

using namespace ista;

static void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

static void inputSlew()
{
  Database db;
  Config config;
  DelayCalculator delay(db, config);
  std::vector<std::string> starts{"in", "blocked", "inout"};
  for (const auto& name : starts) {
    db.graph().points()[name] = TimingPoint{};
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_is_port(true);
    pin.set_direction(name == "inout" ? PinDirection::kInout : PinDirection::kInput);
    auto& constraint = db.sdc().get_port_constraint_map()[name];
    constraint.set_input_transition(AnalysisType::kMax, TransType::kRise, 0.3);
    constraint.set_input_transition(AnalysisType::kMax, TransType::kFall, 0.4);
    constraint.set_input_transition(AnalysisType::kMin, TransType::kRise, 0.1);
    constraint.set_input_transition(AnalysisType::kMin, TransType::kFall, 0.2);
  }
  db.graph().driverPoints()["inout"] = TimingPoint{};
  db.graph().simValues().setValue("blocked", TimingCaseValue::kOne);
  db.graph().simValues().setValue("inout", TimingCaseValue::kZero);
  db.graph().simValues().setValue("inout", TimingCaseValue::kRise, true);
  StartpointInitializer(db, delay).seed(starts);
  const auto& slew = db.graph().driverPoint("in").get_data_slew_map();
  require(!slew.empty(), "source initialization omitted physical slew");
  require(slew.at(AnalysisType::kMax).at(TransType::kRise) == 0.3 && slew.at(AnalysisType::kMax).at(TransType::kFall) == 0.4
              && slew.at(AnalysisType::kMin).at(TransType::kRise) == 0.1 && slew.at(AnalysisType::kMin).at(TransType::kFall) == 0.2,
          "source slew lost mode or transition");
  require(db.graph().driverPoint("blocked").get_data_slew_map().empty(), "case-constant source acquired slew");
  const auto& inout = db.graph().driverPoint("inout").get_data_slew_map();
  require(inout.at(AnalysisType::kMin).at(TransType::kRise) == 0.1 && !inout.at(AnalysisType::kMin).contains(TransType::kFall),
          "inout source did not use its separate driver transition mask");
  require(db.graph().points()["inout"].get_data_slew_map().empty(), "inout source overwrote receiver slew");
  db.sdc().get_port_constraint_map().at("in").set_input_transition(AnalysisType::kMax, TransType::kRise, 0.8);
  StartpointInitializer(db, delay).seed(starts);
  require(db.graph().driverPoint("in").get_data_slew_map().at(AnalysisType::kMax).at(TransType::kRise) == 0.8,
          "source initialization retained an old input transition");
  db.sdc().get_port_constraint_map().at("in").set_input_transition(AnalysisType::kMax, TransType::kRise, 9.0);
  DataSlewPropagator(db, delay).propagate();
  require(db.graph().driverPoint("in").get_data_slew_map().at(AnalysisType::kMax).at(TransType::kRise) == 0.8,
          "graph propagation unexpectedly repeated source initialization");
}

static void missingAndZeroClockSlew()
{
  Database db;
  Config config;
  DelayCalculator delay(db, config);
  const std::string name = "divider:CK";
  auto& pin = db.network().pins()[name];
  pin.set_full_name(name);
  pin.set_instance_name("divider");
  auto& instance = db.network().instances()["divider"];
  instance.set_is_sequential(true);
  instance.set_clock_pin_name(name);
  auto& arc = db.graph().arcs().emplace_back();
  arc.set_type(ArcType::kNet);
  arc.set_source_pin("driver");
  arc.set_sink_pin(name);
  db.graph().incomingArcs()[name] = {0};
  std::vector<std::string> starts{name};
  auto& point = db.graph().points()[name];
  point.setLaunchClock("clock");
  point.setDefaultClock("clock");
  StartpointInitializer(db, delay).seed(starts);
  require(point.get_data_slew_map().empty(), "missing divider waveform was seeded as zero");
  point.get_clock_state("clock").slew_map[AnalysisType::kMin][TransType::kRise] = 0.0;
  StartpointInitializer(db, delay).seed(starts);
  require(point.get_data_slew_map().contains(AnalysisType::kMin) && point.get_data_slew_map().at(AnalysisType::kMin).at(TransType::kRise) == 0.0,
          "explicit zero clock slew was mistaken for a missing waveform");
  require(!point.get_data_slew_map().contains(AnalysisType::kMax), "missing max waveform was synthesized");

  point.set_is_clock_point(true);
  instance.set_output_pin_name("divider:Q");
  point.get_clock_state("clock").slew_map[AnalysisType::kMax][TransType::kRise] = 0.3;
  point.get_clock_state("second").slew_map[AnalysisType::kMax][TransType::kRise] = 0.7;
  for (const auto* clock_name : {"clock", "second"}) {
    auto& clock = db.sdc().get_clock_map()[clock_name];
    clock.set_clock_name(clock_name);
    clock.set_period(10.0);
    auto& state = point.get_clock_state(clock_name);
    state.waveforms[TransType::kRise] = state;
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
      state.waveforms[TransType::kRise].arrival_map[mode][TransType::kRise] = 0.;
  }
  StartpointInitializer(db, delay).seed(starts);
  const auto& paths = point.get_path_state_map().at(AnalysisType::kMax).at(PathSourceType::kRegister).at(TransType::kRise);
  require(paths.size() == 2, "source initialization collapsed two launch clocks");
  for (const auto& [tag, path] : paths)
    require(path.get_slew() == (path.get_clock_name() == "clock" ? 0.3 : 0.7), "path state did not use its named clock waveform");
  require(point.get_data_slew_map().at(AnalysisType::kMax).at(TransType::kRise) == 0.3, "physical source slew stopped using the default clock");
}

static void bidirectionalIoClocks()
{
  for (bool input_last : {false, true}) {
    Database db;
    Config config;
    DelayCalculator delay(db, config);
    auto& pin = db.network().pins()["io"];
    pin.set_full_name("io");
    pin.set_is_port(true);
    pin.set_direction(PinDirection::kInout);
    db.graph().driverPoints()["io"] = TimingPoint{};
    for (const auto* name : {"input_clock", "output_clock"}) {
      auto& clock = db.sdc().get_clock_map()[name];
      clock.set_clock_name(name);
      clock.set_period(10);
      clock.set_rise_edge(std::string(name) == "input_clock" ? 1 : 3);
    }
    TimingIoDelay input, output;
    input.set_clock_name("input_clock");
    input.set_delay(2);
    output.set_clock_name("output_clock");
    output.set_delay(4);
    auto& port = db.sdc().get_port_constraint_map()["io"];
    if (!input_last)
      port.set_input_delays({input}, false);
    port.set_output_delays({output}, false);
    if (input_last)
      port.set_input_delays({input}, false);
    std::vector<std::string> starts{"io"};
    StartpointInitializer(db, delay).seed(starts);
    const auto& transitions = db.graph().driverPoint("io").get_path_state_map().at(AnalysisType::kMax).at(PathSourceType::kInput);
    require(!transitions.contains(TransType::kFall) || transitions.at(TransType::kFall).empty(), "rise-only input delay fabricated a falling input path");
    const auto& paths = transitions.at(TransType::kRise);
    require(paths.size() == 1 && paths.begin()->second.get_clock_name() == "input_clock" && paths.begin()->second.get_arrival() == 3,
            "output constraint overwrote the bidirectional port's input clock");
  }
}

int main()
{
  bidirectionalIoClocks();
  inputSlew();
  missingAndZeroClockSlew();
}
