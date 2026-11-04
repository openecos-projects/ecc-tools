// SPDX-License-Identifier: MulanPSL-2.0
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "MinTimingDataExporter.hpp"
#include "SdcCommand.hpp"
#include "TimingAnalyzer.hpp"
#include "TimingDataExporter.hpp"

using namespace ista;

static void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}

static void addPin(Database& database, const std::string& name, PinDirection direction, bool port = true)
{
  auto& pin = database.network().pins()[name];
  pin.set_full_name(name);
  pin.set_pin_name(port ? name : "Z");
  pin.set_direction(direction);
  pin.set_is_port(port);
  if (!port)
    pin.set_instance_name("tie");
}

static void run(bool one, TimingCaseValue input_case, bool exclude_hold = false)
{
  DataManager::initInst();
  DelayCalculator::initInst();
  auto& database = STADM.getDatabase();
  STADM.getConfig().thread_number = 1;
  database.network().setName("min_constant_output");
  addPin(database, "in", PinDirection::kInput);
  addPin(database, "out", PinDirection::kOutput);
  addPin(database, "constant_out", PinDirection::kOutput);
  addPin(database, "tie:Z", PinDirection::kOutput, false);
  auto& instance = database.network().instances()["tie"];
  instance.set_instance_name("tie");
  instance.set_cell_name("tie");
  instance.get_pin_name_list().push_back("tie:Z");
  auto& cell = database.liberty().get_cell_map()["tie"];
  cell.set_cell_name("tie");
  auto& port = cell.get_port_map()["Z"];
  port.set_port_name("Z");
  port.set_is_output(true);
  LogicExpression function;
  LogicExpressionTerm term;
  term.set_operation_type(one ? LogicOperationType::kOne : LogicOperationType::kZero);
  function.get_term_list().push_back(term);
  port.set_function_expression(function);
  for (const auto& [name, pins] : std::array<std::pair<std::string, std::vector<std::string>>, 2>{
           {{"data", {"in", "out"}}, {"constant", {"tie:Z", "constant_out"}}}}) {
    auto& net = database.network().nets()[name];
    net.set_net_name(name);
    net.set_pin_name_list(pins);
    for (const auto& pin : pins)
      database.network().pins().at(pin).set_net_name(name);
  }
  auto& commands = SdcCommand::getInst();
  require(commands.evalString("create_clock -name clock -period 10; "
                              "set_input_delay -clock clock 1 [get_ports in]; "
                              "set_output_delay -clock clock 2 [get_ports {out constant_out}]") == TCL_OK,
          "min qualification fixture SDC failed");
  database.sdc().get_case_analysis_map()["in"] = input_case;
  if (exclude_hold)
    require(commands.evalString("set_false_path -hold -to [get_ports out]") == TCL_OK, "output hold exception failed");
  TimingAnalyzer analyzer(database, STADC);
  for (const bool structural : {true, false}) {
    if (structural)
      analyzer.prepareStructure();
    else
      analyzer.analyze(true);
    auto data = TimingDataExporter::exportData(database, STADM.getConfig());
    if (structural)
      exportMinTimingModel(data, database, STADM.getConfig());
    else
      exportMinTimingData(data, database, STADM.getConfig());
    require(database.graph().simValues().value("constant_out") == (one ? TimingCaseValue::kOne : TimingCaseValue::kZero),
            "Liberty constant function was not propagated to output");
    for (const auto& pin : data.pins) {
      if (pin.name != "out" && pin.name != "constant_out")
        continue;
      for (std::size_t edge = 0; edge < 2; ++edge) {
        const bool allowed = database.graph().simValues().allowsTransition(pin.name, edge == 0 ? TransType::kRise : TransType::kFall);
        const bool excluded_path = exclude_hold && pin.name == "out";
        require(pin.min_qualification.valid[edge] == (allowed && !excluded_path), "min output qualification ignored logic or path exceptions");
        require(pin.min_qualification.reason[edge] == (!allowed ? TimingMinQualificationReason::kDisabledTransition
                                                     : excluded_path ? TimingMinQualificationReason::kInactiveCheck
                                                                     : TimingMinQualificationReason::kValid),
                "static output or disabled lane was reported as a missing min state");
        if (!allowed)
          require(std::none_of(data.min_contexts.begin(), data.min_contexts.end(), [&](const auto& context) {
            return context.end_point == pin.name && context.data_transition == (edge == 0 ? 1 : -1);
          }), "disabled lane acquired a fabricated hold context");
      }
    }
  }
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
}

static void registerCheck(bool exclude_hold, bool clocked = true)
{
  DataManager::initInst();
  DelayCalculator::initInst();
  auto& database = STADM.getDatabase();
  STADM.getConfig().thread_number = 1;
  auto& instance = database.network().instances()["ff"];
  instance.set_instance_name("ff");
  instance.set_cell_name("FF");
  instance.set_is_sequential(true);
  instance.set_clock_pin_name("ff:CK");
  instance.set_data_pin_name("ff:D");
  instance.set_output_pin_name("ff:Q");
  auto& cell = database.liberty().get_cell_map()["FF"];
  cell.set_cell_name("FF");
  cell.set_is_sequential(true);
  for (const auto* name : {"CK", "D", "Q"}) {
    const std::string full = std::string("ff:") + name;
    auto& pin = database.network().pins()[full];
    pin.set_full_name(full);
    pin.set_pin_name(name);
    pin.set_instance_name("ff");
    pin.set_direction(std::string(name) == "Q" ? PinDirection::kOutput : PinDirection::kInput);
    instance.get_pin_name_list().push_back(full);
    auto& port = cell.get_port_map()[name];
    port.set_port_name(name);
    port.set_is_input(std::string(name) != "Q");
    port.set_is_output(std::string(name) == "Q");
    port.set_is_clock(std::string(name) == "CK");
  }
  TimingCellArc clock_arc;
  clock_arc.set_source_port("CK");
  clock_arc.set_sink_port("Q");
  clock_arc.set_is_clock_arc(true);
  TimingArc variant;
  variant.set_arc_idx(0);
  variant.set_trigger_trans_type(TransType::kRise);
  variant.set_sense(TimingArcSense::kPositive);
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    variant.get_delay_table_map()[edge].set_value_list({0.1});
    variant.get_slew_table_map()[edge].set_value_list({0.1});
    variant.get_check_table_map()[edge].set_value_list({0.1});
  }
  clock_arc.get_timing_arc_list().push_back(variant);
  cell.get_cell_arc_list().push_back(clock_arc);
  for (auto type : {TimingCheckType::kSetup, TimingCheckType::kHold}) {
    TimingCheckArc check;
    check.set_clock_port("CK");
    check.set_data_port("D");
    check.set_clock_trans_type(TransType::kRise);
    check.set_check_type(type);
    check.get_timing_arc_list().push_back(variant);
    cell.get_check_arc_list().push_back(check);
    check.set_clock_port("ff:CK");
    check.set_data_port("ff:D");
    instance.get_check_arc_list().push_back(check);
  }
  addPin(database, "in", PinDirection::kInput);
  addPin(database, "clk", PinDirection::kInput);
  addPin(database, "out", PinDirection::kOutput);
  for (const auto& [name, pins] : std::array<std::pair<std::string, std::vector<std::string>>, 3>{
           {{"clock", {"clk", "ff:CK"}}, {"data", {"in", "ff:D"}}, {"output", {"ff:Q", "out"}}}}) {
    auto& net = database.network().nets()[name];
    net.set_net_name(name);
    net.set_pin_name_list(pins);
    for (const auto& pin : pins)
      database.network().pins().at(pin).set_net_name(name);
  }
  auto& commands = SdcCommand::getInst();
  const std::string clock_source = clocked ? " [get_ports clk]" : "";
  require(commands.evalString("create_clock -name clock -period 10" + clock_source + "; "
                              "set_input_delay -clock clock 1 [get_ports in]; "
                              "set_output_delay -clock clock 0 [get_ports out]") == TCL_OK, "register fixture SDC failed");
  if (exclude_hold)
    require(commands.evalString("set_false_path -hold -from [get_ports in] -to [get_pins ff/D]") == TCL_OK, "hold exception failed");
  TimingAnalyzer analyzer(database, STADC);
  for (bool structural : {true, false}) {
    if (structural)
      analyzer.prepareStructure();
    else
      analyzer.analyze(true);
    auto data = TimingDataExporter::exportData(database, STADM.getConfig());
    if (structural)
      exportMinTimingModel(data, database, STADM.getConfig());
    else
      exportMinTimingData(data, database, STADM.getConfig());
    const auto pin = std::find_if(data.pins.begin(), data.pins.end(), [](const auto& pin) { return pin.name == "ff:D"; });
    const bool active = clocked && !exclude_hold;
    require(pin != data.pins.end() && pin->min_qualification.valid == std::array{active, active},
            "hold endpoint ignored its path exception");
    require(std::count_if(data.min_contexts.begin(), data.min_contexts.end(), [](const auto& row) { return row.end_point == "ff:D"; })
                == (active ? 2 : 0), "hold exception changed the legal capture context count");
    const auto output = std::find_if(data.pins.begin(), data.pins.end(), [](const auto& pin) { return pin.name == "out"; });
    require(output != data.pins.end() && output->min_qualification.valid == std::array{clocked, clocked},
            "output declaration qualified a path without a launch clock");
    if (!clocked)
      require(output->min_qualification.reason == std::array{TimingMinQualificationReason::kInactiveCheck,
                                                             TimingMinQualificationReason::kInactiveCheck},
              "unclocked register output was reported as a missing min model state");
    bool found_hold = false;
    for (const auto& arc : data.timing_check_arcs) {
      if (arc.sink_pin != "ff:D" || arc.check_type != static_cast<int32_t>(TimingCheckType::kHold))
        continue;
      found_hold = true;
      require(arc.min_qualification.valid == std::array{active, active},
              "min check remained qualified without a legal endpoint/capture context");
    }
    require(found_hold, "fixture lost its hold LUT");
  }
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
}

int main()
{
  Logger::initInst();
  SdcCommand::initInst();
  sdc::registerSdcCommands(SdcCommand::getInst());
  for (const bool one : {false, true})
    for (const auto input_case : {TimingCaseValue::kNone, TimingCaseValue::kRise, TimingCaseValue::kFall, TimingCaseValue::kStatic})
      run(one, input_case);
  for (bool exclude_hold : {false, true})
    registerCheck(exclude_hold);
  registerCheck(false, false);
  run(false, TimingCaseValue::kNone, true);
  SdcCommand::destroyInst();
  Logger::destroyInst();
}
