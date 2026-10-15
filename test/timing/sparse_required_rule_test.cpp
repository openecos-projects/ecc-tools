// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <stdexcept>

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "SdcCommand.hpp"
#include "TimingExceptionMatcher.hpp"
#include "TimingExportAdapter.hpp"

int main()
{
  using namespace ista;
  DataManager::initInst();
  DelayCalculator::initInst();
  SdcCommand::initInst();
  auto& database = STADM.getDatabase();
  auto& interpreter = SdcCommand::getInst();
  sdc::registerSdcCommands(interpreter);
  auto& clock_pin = database.get_pin_map()["clk"];
  clock_pin.set_is_port(true);
  clock_pin.set_full_name("clk");
  clock_pin.set_direction(PinDirection::kInput);
  if (interpreter.evalString("create_clock -name clk -period 10 [get_ports clk]; set_clock_uncertainty -setup 0.1 [get_clocks clk]")
      != TCL_OK) {
    throw std::runtime_error("clock fixture failed");
  }
  auto& instance = database.get_instance_map()["ff"];
  instance.set_instance_name("ff");
  instance.set_is_sequential(true);
  instance.set_clock_pin_name("ff:CK");
  instance.set_data_pin_name("ff:D");
  for (const auto& name : {"ff:CK", "ff:D"}) {
    auto& pin = database.get_pin_map()[name];
    pin.set_instance_name("ff");
    pin.set_full_name(name);
  }
  auto& state = database.get_timing_point_map()["ff:CK"].get_clock_state("clk");
  state.arrival_map[AnalysisType::kMin][TransType::kRise] = 0;
  state.arrival_map[AnalysisType::kMax][TransType::kRise] = 0;
  TimingTable table;
  table.set_value_list({0.2});
  TimingArc arc;
  arc.get_check_table_map()[TransType::kRise] = table;
  TimingCheckArc check;
  check.set_clock_port("ff:CK");
  check.set_data_port("ff:D");
  check.set_clock_trans_type(TransType::kRise);
  check.set_check_type(TimingCheckType::kSetup);
  check.set_timing_arc_list({arc});
  instance.get_check_arc_list().push_back(check);
  auto& path = database.get_timing_point_map()["ff:D"]
                   .get_path_state_map()[AnalysisType::kMax][PathSourceType::kRegister][TransType::kRise]["launch"];
  path.set_clock_name("clk");
  path.set_start_point("ff:CK");
  path.set_trans_type(TransType::kRise);
  path.set_launch_clock_trans_type(TransType::kRise);
  path.set_launch_time(0);
  path.set_arrival(1);
  path.set_slew(0.2);
  auto run = [&](bool ignore_latency, bool expect_clock = true) {
    database.get_timing_constraint().get_path_exception_list().clear();
    const std::string command = std::string("set_max_delay 3 ") + (ignore_latency ? "-ignore_clock_latency " : "")
                                + "-from [get_clocks clk] -to [get_clocks clk]";
    if (interpreter.evalString(command) != TCL_OK)
      throw std::runtime_error("max-delay fixture failed");
    path.set_exception_state_list(TimingExceptionMatcher::initState(database, "ff:CK", "clk", TransType::kRise, AnalysisType::kMax));
    TimingSnapshot snapshot;
    snapshot.time_unit_ps = 1000;
    snapshot.clock_names = {"clk"};
    snapshot.end_points = {"ff:D"};
    for (const auto& name : {"ff:CK", "ff:D"}) {
      TimingPinSnapshot pin;
      pin.name = name;
      snapshot.pins.push_back(pin);
    }
    TimingEndpointArcSnapshot endpoint;
    endpoint.source_pin = "ff:CK";
    endpoint.sink_pin = "ff:D";
    endpoint.check_type = static_cast<int32_t>(TimingCheckType::kSetup);
    endpoint.library_arc_id = 0;
    snapshot.timing_check_arcs.push_back(endpoint);
    SparseTimingExportAdapter::compile(snapshot);
    if (!expect_clock) {
      if (!snapshot.sparse.checks.empty())
        throw std::runtime_error("unclocked endpoint acquired a fallback capture clock");
      return;
    }
    if (!snapshot.sparse.unsupported.empty() || snapshot.sparse.checks.size() != 1)
      throw std::runtime_error("invalid sparse rule fixture");
    const auto& rule = snapshot.sparse.checks.front();
    const double expected = ignore_latency ? 2800 : 2700;
    if (!rule.valid || rule.kind != SparseTimingRuleKind::kMaxDelay || std::abs(rule.reference_required_ps - expected) > 1e-6
        || std::abs(rule.intercept_ps - (expected + 200)) > 1e-6) {
      throw std::runtime_error("ignore-clock-latency=" + std::to_string(ignore_latency) + " required="
                               + std::to_string(rule.reference_required_ps) + " intercept=" + std::to_string(rule.intercept_ps)
                               + " kind=" + std::to_string(static_cast<int>(rule.kind)));
    }
  };
  run(false);
  run(true);
  state.arrival_map[AnalysisType::kMin].clear();
  run(false, false);
  SdcCommand::destroyInst();
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
}
