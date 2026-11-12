// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>

#include "Config.hpp"
#include "Database.hpp"
#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "TimingAnalyzer.hpp"
#include "liberty/TimingCellArc.hpp"
#include "liberty/TimingCheckArc.hpp"
#include "sdc/TimingException.hpp"

using namespace ista;

static void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

static void makeDesign(Database& database, double input_delay, double output_delay)
{
  auto& network = database.network();
  for (const auto* name : {"in", "out"}) {
    auto& pin = network.pins()[name];
    pin.set_full_name(name);
    pin.set_pin_name(name);
    pin.set_is_port(true);
    pin.set_direction(std::string(name) == "in" ? PinDirection::kInput : PinDirection::kOutput);
    pin.set_net_name("wire");
  }
  auto& net = network.nets()["wire"];
  net.set_net_name("wire");
  net.get_pin_name_list() = {"in", "out"};
  auto& clock = database.sdc().get_clock_map()["clock"];
  clock.set_clock_name("clock");
  clock.set_period(10.0);
  clock.set_fall_edge(5.0);
  std::vector<TimingIoDelay> input_delays, output_delays;
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    TimingIoDelay delay;
    delay.set_clock_name("clock");
    delay.set_trans_type(edge);
    delay.set_delay(input_delay);
    input_delays.push_back(delay);
    delay.set_delay(output_delay);
    output_delays.push_back(delay);
  }
  database.sdc().get_port_constraint_map()["in"].set_input_delays(input_delays, false);
  database.sdc().get_port_constraint_map()["out"].set_output_delays(output_delays, false);
}

static void check(Database& database, double arrival, double required)
{
  require(database.graph().arcs().size() == 1, "analysis did not rebuild one wire arc");
  if (database.graph().endpoints().size() != 1)
    throw std::runtime_error("expected one endpoint, found " + std::to_string(database.graph().endpoints().size()));
  const auto& endpoint = database.graph().points().at("out");
  require(std::abs(endpoint.get_arrival() - arrival) < 1E-9, "analysis read another database or skipped data propagation");
  require(std::abs(endpoint.get_required() - required) < 1E-9, "required propagation skipped output constraint");
  bool found = false;
  for (const auto& group : database.pathGroups()) {
    for (const auto& [name, paths] : group.endpointPaths()) {
      for (const auto& path : paths) {
        if (path.get_analysis_type() == AnalysisType::kMax) {
          require(std::abs(path.get_slack() - (required - arrival)) < 1E-9, "published path lost timing result");
          found = true;
        }
      }
    }
  }
  require(found, "analysis did not publish max path");
}

static void structuralFfPath()
{
  Database database;
  database.network().setName("structural_ff");

  auto add_pin = [&](const std::string& name, const std::string& instance, const std::string& pin_name, PinDirection direction, bool is_port) {
    auto& pin = database.network().pins()[name];
    pin.set_full_name(name);
    pin.set_pin_name(pin_name);
    pin.set_instance_name(instance);
    pin.set_is_port(is_port);
    pin.set_direction(direction);
    if (!instance.empty())
      database.network().instances()[instance].get_pin_name_list().push_back(name);
  };
  add_pin("data", "", "data", PinDirection::kInput, true);
  add_pin("clk", "", "clk", PinDirection::kInput, true);
  add_pin("inv:A", "inv", "A", PinDirection::kInput, false);
  add_pin("inv:Y", "inv", "Y", PinDirection::kOutput, false);
  add_pin("buf:A", "buf", "A", PinDirection::kInput, false);
  add_pin("buf:Y", "buf", "Y", PinDirection::kOutput, false);
  add_pin("merge:A", "merge", "A", PinDirection::kInput, false);
  add_pin("merge:B", "merge", "B", PinDirection::kInput, false);
  add_pin("merge:Y", "merge", "Y", PinDirection::kOutput, false);
  add_pin("ff:CK", "ff", "CK", PinDirection::kInput, false);
  add_pin("ff:D", "ff", "D", PinDirection::kInput, false);
  add_pin("ff:Q", "ff", "Q", PinDirection::kOutput, false);
  add_pin("unclocked:CK", "unclocked", "CK", PinDirection::kInput, false);
  add_pin("unclocked:D", "unclocked", "D", PinDirection::kInput, false);
  add_pin("unclocked:Q", "unclocked", "Q", PinDirection::kOutput, false);
  add_pin("latch:GN", "latch", "GN", PinDirection::kInput, false);
  add_pin("latch:D", "latch", "D", PinDirection::kInput, false);
  add_pin("latch:Q", "latch", "Q", PinDirection::kOutput, false);

  auto add_net = [&](const std::string& name, std::vector<std::string> pins) {
    auto& net = database.network().nets()[name];
    net.set_net_name(name);
    net.set_pin_name_list(std::move(pins));
    for (const auto& pin : net.get_pin_name_list())
      database.network().pins().at(pin).set_net_name(name);
  };
  add_net("data_net", {"data", "inv:A", "buf:A", "unclocked:CK", "unclocked:D", "latch:GN"});
  add_net("inv_net", {"inv:Y", "merge:A"});
  add_net("buf_net", {"buf:Y", "merge:B"});
  add_net("logic_net", {"merge:Y", "ff:D", "latch:D"});
  add_net("clock_net", {"clk", "ff:CK"});

  auto& inv_instance = database.network().instances()["inv"];
  inv_instance.set_instance_name("inv");
  inv_instance.set_cell_name("INV");
  auto& buf_instance = database.network().instances()["buf"];
  buf_instance.set_instance_name("buf");
  buf_instance.set_cell_name("BUF");
  auto& merge_instance = database.network().instances()["merge"];
  merge_instance.set_instance_name("merge");
  merge_instance.set_cell_name("MERGE");
  auto& ff_instance = database.network().instances()["ff"];
  ff_instance.set_instance_name("ff");
  ff_instance.set_cell_name("DFF");
  ff_instance.set_is_sequential(true);
  ff_instance.set_clock_pin_name("ff:CK");
  ff_instance.set_data_pin_name("ff:D");
  ff_instance.set_output_pin_name("ff:Q");
  auto& unclocked = database.network().instances()["unclocked"];
  unclocked.set_instance_name("unclocked");
  unclocked.set_cell_name("DFF");
  unclocked.set_is_sequential(true);
  unclocked.set_clock_pin_name("unclocked:CK");
  unclocked.set_data_pin_name("unclocked:D");
  unclocked.set_output_pin_name("unclocked:Q");
  auto& latch_instance = database.network().instances()["latch"];
  latch_instance.set_instance_name("latch");
  latch_instance.set_cell_name("LATCH");
  latch_instance.set_is_sequential(true);
  latch_instance.set_clock_pin_name("latch:GN");
  latch_instance.set_data_pin_name("latch:D");
  latch_instance.set_output_pin_name("latch:Q");

  auto& inv = database.liberty().get_cell_map()["INV"];
  inv.set_cell_name("INV");
  inv.get_port_map()["A"].set_is_input(true);
  inv.get_port_map()["Y"].set_is_output(true);
  TimingCellArc inv_arc;
  inv_arc.set_source_port("A");
  inv_arc.set_sink_port("Y");
  inv.get_cell_arc_list().push_back(inv_arc);

  auto& buf = database.liberty().get_cell_map()["BUF"];
  buf.set_cell_name("BUF");
  buf.get_port_map()["A"].set_is_input(true);
  buf.get_port_map()["Y"].set_is_output(true);
  TimingCellArc buf_arc;
  buf_arc.set_source_port("A");
  buf_arc.set_sink_port("Y");
  buf.get_cell_arc_list().push_back(buf_arc);

  auto& merge = database.liberty().get_cell_map()["MERGE"];
  merge.set_cell_name("MERGE");
  merge.get_port_map()["A"].set_is_input(true);
  merge.get_port_map()["B"].set_is_input(true);
  merge.get_port_map()["Y"].set_is_output(true);
  for (const auto* source : {"A", "B"}) {
    TimingCellArc merge_arc;
    merge_arc.set_source_port(source);
    merge_arc.set_sink_port("Y");
    merge.get_cell_arc_list().push_back(merge_arc);
  }

  auto& dff = database.liberty().get_cell_map()["DFF"];
  dff.set_cell_name("DFF");
  dff.set_is_sequential(true);
  dff.get_port_map()["CK"].set_is_input(true);
  dff.get_port_map()["D"].set_is_input(true);
  dff.get_port_map()["Q"].set_is_output(true);
  TimingCellArc clock_to_q;
  clock_to_q.set_source_port("CK");
  clock_to_q.set_sink_port("Q");
  clock_to_q.set_is_clock_arc(true);
  dff.get_cell_arc_list().push_back(clock_to_q);
  auto& latch = database.liberty().get_cell_map()["LATCH"];
  latch.set_cell_name("LATCH");
  latch.set_is_sequential(true);
  latch.get_port_map()["GN"].set_is_input(true);
  latch.get_port_map()["D"].set_is_input(true);
  auto& latch_output = latch.get_port_map()["Q"];
  latch_output.set_is_output(true);
  LogicExpression state_function;
  LogicExpressionTerm state_term;
  state_term.set_operation_type(LogicOperationType::kPort);
  state_term.set_port_name("IQ");
  state_function.get_term_list().push_back(state_term);
  latch_output.set_function_expression(state_function);
  TimingCellArc latch_data;
  latch_data.set_source_port("D");
  latch_data.set_sink_port("Q");
  latch_data.set_is_latch_data_arc(true);
  latch.get_cell_arc_list().push_back(latch_data);
  for (const auto check_type : {TimingCheckType::kSetup, TimingCheckType::kHold}) {
    TimingCheckArc check_arc;
    check_arc.set_clock_port("ff:CK");
    check_arc.set_data_port("ff:D");
    check_arc.set_check_type(check_type);
    check_arc.set_check_time(0.1);
    dff.get_check_arc_list().push_back(check_arc);
    ff_instance.get_check_arc_list().push_back(check_arc);
  }

  auto& clock = database.sdc().get_clock_map()["core"];
  clock.set_clock_name("core");
  clock.set_period(10.0);
  clock.set_rise_edge(1.0);
  clock.set_fall_edge(6.0);
  clock.set_source_list({"clk"});
  clock.set_source_latency(AnalysisType::kMax, clockVariation(AnalysisType::kMax), TransType::kRise, 0.4);
  clock.set_source_latency(AnalysisType::kMin, clockVariation(AnalysisType::kMin), TransType::kRise, 0.2);
  std::vector<TimingIoDelay> input_delays;
  for (auto mode : {AnalysisType::kMax, AnalysisType::kMin})
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      TimingIoDelay input;
      input.set_clock_name("core");
      input.set_analysis_type(mode);
      input.set_trans_type(edge);
      input.set_delay(mode == AnalysisType::kMax ? 0.7 : 0.3);
      input_delays.push_back(input);
    }
  database.sdc().get_port_constraint_map()["data"].set_input_delays(input_delays, false);

  auto add_through_exception = [&](bool setup, bool hold, const char* through_pin) {
    TimingException exception;
    exception.set_type(TimingExceptionType::kFalsePath);
    exception.set_setup(setup);
    exception.set_hold(hold);
    exception.set_from_objects({{ExceptionObjectType::kPort, "data"}});
    exception.set_to_objects({{ExceptionObjectType::kPin, "ff:D"}});
    TimingExceptionThrough through;
    through.set_objects({{ExceptionObjectType::kPin, through_pin}});
    exception.set_through_list({through});
    database.sdc().get_path_exception_list().push_back(exception);
  };
  add_through_exception(true, false, "inv:Y");
  add_through_exception(false, true, "buf:Y");

  Config config;
  DelayCalculator delay(database, config);
  TimingAnalyzer analyzer(database, delay);
  analyzer.prepareStructure();
  require(analyzer.isStructureCurrent(), "FF structural model signature is not current");
  require(database.pathGroups().empty(), "FF structural preparation ran numeric path analysis");
  require(database.graph().points().at("ff:CK").has_clock_state("core"), "clock PI did not reach FF CK");
  require(database.graph().points().at("ff:CK").find_clock_state("core")->is_structural,
          "clock state was not marked structural");
  for (const auto* pin : {"unclocked:CK", "unclocked:Q"}) {
    const auto& paths = database.graph().points().at(pin).get_path_state_map();
    for (const auto mode : {AnalysisType::kMax, AnalysisType::kMin}) {
      const auto analysis = paths.find(mode);
      if (analysis == paths.end())
        continue;
      const auto inputs = analysis->second.find(PathSourceType::kInput);
      if (inputs == analysis->second.end())
        continue;
      for (const auto& [edge, states] : inputs->second)
        require(states.empty(), "structural data state crossed a sequential clock boundary");
    }
  }

  const auto& data_paths = database.graph().points().at("ff:D").get_path_state_map().at(AnalysisType::kMax).at(PathSourceType::kInput);
  bool found_data_path = false;
  for (const auto& [tag, path] : data_paths.at(TransType::kRise)) {
    if (path.get_start_point() == "data") {
      found_data_path = true;
      require(path.get_arrival() == 0.0 && path.get_slew() == 0.0, "structural data path acquired numeric timing");
      break;
    }
  }
  require(found_data_path, "PI -> INV -> FF:D structural path was not preserved");

  const auto& merge_paths = database.graph().points().at("merge:Y").get_path_state_map().at(AnalysisType::kMax).at(PathSourceType::kInput);
  std::set<std::pair<int32_t, int32_t>> merge_progress;
  for (const auto& [tag, path] : merge_paths.at(TransType::kRise))
    if (path.get_start_point() == "data" && path.get_exception_state_list().size() == 2)
      merge_progress.emplace(path.get_exception_state_list()[0], path.get_exception_state_list()[1]);
  require(merge_progress == std::set<std::pair<int32_t, int32_t>>{{0, -1}, {1, -1}},
          "setup through hit and bypass paths were not both retained at reconvergence");

  std::set<std::pair<int32_t, int32_t>> endpoint_progress;
  const auto& endpoint_paths = database.graph().points().at("ff:D").get_path_state_map().at(AnalysisType::kMax).at(PathSourceType::kInput);
  for (const auto& [tag, path] : endpoint_paths.at(TransType::kRise))
    if (path.get_start_point() == "data" && path.get_exception_state_list().size() == 2)
      endpoint_progress.emplace(path.get_exception_state_list()[0], path.get_exception_state_list()[1]);
  require(endpoint_progress == merge_progress, "through progress was lost before the timing-check endpoint");

  std::set<std::pair<int32_t, int32_t>> hold_progress;
  const auto& min_merge_paths = database.graph().points().at("merge:Y").get_path_state_map().at(AnalysisType::kMin).at(PathSourceType::kInput);
  for (const auto& [tag, path] : min_merge_paths.at(TransType::kRise))
    if (path.get_start_point() == "data" && path.get_exception_state_list().size() == 2)
      hold_progress.emplace(path.get_exception_state_list()[0], path.get_exception_state_list()[1]);
  require(hold_progress == std::set<std::pair<int32_t, int32_t>>{{-1, 0}, {-1, 1}},
          "hold-only through progress was copied from setup instead of propagated independently");

  const auto& register_paths = database.graph().points().at("ff:Q").get_path_state_map().at(AnalysisType::kMax).at(PathSourceType::kRegister);
  bool found_register_path = false;
  for (const auto& [tag, path] : register_paths.at(TransType::kRise)) {
    if (path.get_start_point() == "ff:Q") {
      found_register_path = true;
      require(std::abs(path.get_launch_source_latency() - 0.4) < 1E-12, "ideal source latency was lost in structural register state");
      require(std::abs(path.get_launch_time() - 1.4) < 1E-12, "ideal source latency was not included in structural launch time");
      break;
    }
  }
  require(found_register_path, "clock PI -> FF:CK -> FF:Q structural path was not preserved");
  const auto& min_register_paths = database.graph().points().at("ff:Q").get_path_state_map().at(AnalysisType::kMin).at(PathSourceType::kRegister);
  for (const auto& [tag, path] : min_register_paths.at(TransType::kRise))
    require(std::abs(path.get_launch_source_latency() - 0.2) < 1E-12 && std::abs(path.get_launch_time() - 1.2) < 1E-12,
            "structural early launch used late clock latency instead of min latency");

  const auto& latch_paths = database.graph().points().at("latch:Q").get_path_state_map();
  for (const auto& [mode, sources] : latch_paths)
    for (const auto& [source, edges] : sources)
      for (const auto& [edge, states] : edges)
        require(states.empty(), "dynamic unclocked latch acquired a transparent timing path");
  database.sdc().get_case_analysis_map()["latch:GN"] = TimingCaseValue::kZero;
  analyzer.prepareStructure();
  const auto& open_paths = database.graph().points().at("latch:Q").get_path_state_map();
  for (const auto mode : {AnalysisType::kMax, AnalysisType::kMin})
    require(!open_paths.at(mode).at(PathSourceType::kInput).at(TransType::kRise).empty(),
            "statically open latch lost its transparent data path");
}

int main()
{
  Logger::initInst();
  structuralFfPath();
  Database first, second;
  Config config;
  makeDesign(first, 1.0, 2.0);
  makeDesign(second, 3.0, 4.0);
  DelayCalculator first_delay(first, config), second_delay(second, config);
  TimingAnalyzer first_analysis(first, first_delay), second_analysis(second, second_delay);
  first_analysis.prepareStructure();
  require(first_analysis.isStructureCurrent(), "structural preparation did not publish a reusable graph identity");
  require(first.pathGroups().empty(), "structural preparation published numeric path analysis");
  first_analysis.analyze(true);
  second_analysis.analyze();
  check(first, 1.0, 8.0);
  check(second, 3.0, 6.0);
  const auto groups = first.pathGroups().size();
  auto updated = first.sdc().get_port_constraint_map().at("in").get_input_delay_list();
  for (auto& delay : updated)
    delay.set_delay(2.0);
  first.sdc().get_port_constraint_map().at("in").set_input_delays(updated, false);
  first_analysis.analyze();
  check(first, 2.0, 8.0);
  check(second, 3.0, 6.0);
  require(first.pathGroups().size() == groups, "repeat analysis appended stale path groups");
  Logger::destroyInst();
}
