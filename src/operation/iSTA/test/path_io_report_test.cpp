// SPDX-License-Identifier: MulanPSL-2.0
#include <unistd.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

#include "DelayCalculator.hpp"
#include "PathReportWriter.hpp"
#include "TimingAnalyzer.hpp"
#include "TimingJsonWriter.hpp"
using namespace ista;
static void require(bool ok, const char* message)
{
  if (!ok)
    throw std::runtime_error(message);
}
static bool near(double a, double b)
{
  return std::abs(a - b) < 1e-8;
}
static void checkLine(const std::string& text, const std::string& label, double increment, double arrival)
{
  auto pos = text.find(label);
  require(pos != std::string::npos, "selected IO row missing");
  std::istringstream row(text.substr(pos + label.size()));
  double actual_increment, actual_arrival;
  require(bool(row >> actual_increment >> actual_arrival) && near(actual_increment, increment) && near(actual_arrival, arrival),
          "report row does not match the selected IO constraint");
}
static void failedReceiverPath()
{
  Database db;
  Config config;
  db.liberty().set_nom_voltage(1.2);
  const auto directory = std::filesystem::temp_directory_path() / ("ista-failed-rc-" + std::to_string(getpid()));
  std::filesystem::create_directories(directory);
  config.tr_temp_directory_path = directory.string() + "/";
  config.timing_path_limit = 100;
  for (const auto& name : {std::string("in"), std::string("out"), std::string("u:A"), std::string("u:Y")}) {
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_is_port(name == "in" || name == "out");
    pin.set_pin_name(pin.get_is_port() ? name : name.substr(2));
    pin.set_instance_name(pin.get_is_port() ? "" : "u");
    pin.set_direction(name == "in" || name == "u:A" ? PinDirection::kInput : PinDirection::kOutput);
    pin.set_net_name(name == "in" || name == "u:A" ? "n1" : "n2");
  }
  auto& instance = db.network().instances()["u"];
  instance.set_cell_name("io");
  instance.set_instance_name("u");
  instance.set_pin_name_list({"u:A", "u:Y"});
  for (auto name : {"n1", "n2"}) {
    auto& net = db.network().nets()[name];
    net.set_net_name(name);
    net.set_pin_name_list(std::string(name) == "n1" ? std::vector<std::string>{"in", "u:A"} : std::vector<std::string>{"u:Y", "out"});
  }
  auto& cell = db.liberty().get_cell_map()["io"];
  cell.set_cell_name("io");
  cell.set_nom_voltage(3.3);
  cell.get_port_map()["A"].set_is_input(true);
  cell.get_port_map()["Y"].set_is_output(true);
  auto& cell_arc = cell.get_cell_arc_list().emplace_back();
  cell_arc.set_source_port("A");
  cell_arc.set_sink_port("Y");
  cell_arc.set_is_timing_graph_arc(true);
  auto& timing = cell_arc.get_timing_arc_list().emplace_back();
  timing.set_sense(TimingArcSense::kPositive);
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    timing.get_delay_table_map()[edge].set_value_list({.2});
    timing.get_slew_table_map()[edge].set_value_list({.3});
  }
  auto& parasitic = db.parasitics().nets()["n1"];
  parasitic.set_net_name("n1");
  parasitic.get_node_map()["in"].set_capacitance(.01);
  parasitic.get_node_map()["u:A"].set_capacitance(.04);
  ParasiticResistor resistance;
  resistance.set_source_node("in");
  resistance.set_sink_node("u:A");
  resistance.set_resistance(100.);
  parasitic.get_resistor_list().push_back(resistance);
  auto& clock = db.sdc().get_clock_map()["clk"];
  clock.set_clock_name("clk");
  clock.set_period(10.);
  clock.set_fall_edge(5.);
  std::vector<TimingIoDelay> ios;
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    TimingIoDelay io;
    io.set_clock_name("clk");
    io.set_trans_type(edge);
    io.set_delay(1.);
    ios.push_back(io);
  }
  db.sdc().get_port_constraint_map()["in"].set_input_delays(ios, false);
  db.sdc().get_port_constraint_map()["in"].set_input_transition(.4);
  db.sdc().get_port_constraint_map()["out"].set_output_delays(ios, false);
  DelayCalculator delay(db, config);
  TimingAnalyzer(db, delay).analyze();
  bool found = false;
  for (auto& group : db.pathGroups())
    for (auto& [endpoint, paths] : group.endpointPaths())
      for (auto& path : paths)
        if (endpoint == "out" && path.get_analysis_type() == AnalysisType::kMax) {
          found = true;
          require(near(path.get_path_delay(), 1.2), "failed crossing did not propagate the explicit zero net delay");
        }
  require(found, "PT2018 retains nonbounding endpoint paths after RC-005/008");
  require(near(db.graph().points().at("u:A").get_data_slew_map().at(AnalysisType::kMax).at(TransType::kRise), .4),
          "failed receiver must retain source slew for downstream calculation");
  TimingJsonWriter(db, config).write();
  std::ifstream input(directory / "timing_paths.json");
  std::string json((std::istreambuf_iterator<char>(input)), {});
  require(json.find("\"rc_nonbounding\":true") != std::string::npos && json.find("\"rc_failed\":true") != std::string::npos,
          "reported fallback path lost failure provenance");
  std::filesystem::remove_all(directory);
}

int main()
{
  failedReceiverPath();
  for (int choice : {0, 1})
    for (int scenario : {0, 1, 2, 3, 4}) {
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
      for (const auto* name : {"launch", "capture"}) {
        auto& clock = db.sdc().get_clock_map()[name];
        clock.set_clock_name(name);
        clock.set_period(10);
        clock.set_fall_edge(5);
        for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
          for (auto edge : {TransType::kRise, TransType::kFall}) {
            clock.set_source_latency(mode, clockVariation(mode), edge, std::string(name) == "launch" ? 2 : 1);
            clock.set_network_latency(mode, edge, std::string(name) == "launch" ? 3 : 2);
          }
      }
      if (scenario == 2)
        db.sdc().get_clock_map().at("capture").set_is_propagated(true);
      if (scenario >= 3) {
        auto& ref = db.network().pins()["ref"];
        ref.set_full_name("ref");
        ref.set_pin_name("ref");
        ref.set_is_port(true);
        ref.set_direction(scenario == 4 ? PinDirection::kInout : PinDirection::kInput);
        ref.set_net_name("reference");
        auto& ref_net = db.network().nets()["reference"];
        ref_net.set_net_name("reference");
        ref_net.set_pin_name_list({"ref"});
        db.sdc().get_clock_map().at("capture").set_source_list({"ref"});
      }
      std::vector<TimingIoDelay> inputs, outputs;
      for (auto edge : {TransType::kRise, TransType::kFall}) {
        const bool fall = edge == TransType::kFall;
        TimingIoDelay in;
        in.set_clock_name("launch");
        in.set_clock_trans_type(edge);
        in.set_trans_type(edge);
        in.set_delay(choice == 0 ? (fall ? 1 : 9) : (fall ? 20 : 1));
        in.set_source_latency_included(scenario == 1);
        in.set_network_latency_included(scenario == 1);
        inputs.push_back(in);
        TimingIoDelay out;
        out.set_clock_name("capture");
        out.set_clock_trans_type(edge);
        out.set_trans_type(edge);
        out.set_delay(choice == 0 ? (fall ? 20 : 0) : (fall ? 1 : 9));
        out.set_source_latency_included(scenario == 1);
        out.set_network_latency_included(scenario == 1);
        if (scenario >= 3)
          out.set_reference_pin("ref");
        outputs.push_back(out);
      }
      db.sdc().get_port_constraint_map()["in"].set_input_delays(inputs, false);
      db.sdc().get_port_constraint_map()["in"].set_input_transition(0.1);
      db.sdc().get_port_constraint_map()["out"].set_output_delays(outputs, false);
      DelayCalculator delay(db, config);
      TimingAnalyzer(db, delay).analyze();
      TimingPath* chosen = nullptr;
      for (auto& group : db.pathGroups())
        for (auto& [endpoint, paths] : group.endpointPaths())
          for (auto& path : paths)
            if (endpoint == "out" && path.get_analysis_type() == AnalysisType::kMax)
              chosen = &path;
      require(chosen && chosen->get_trans_type() == TransType::kFall, "fixture did not select the falling data path");
      // A reference pin on an ideal clock includes source latency only (PT manual pp.2203-2204).
      const double launch = scenario == 1 ? 5 : 10, capture = scenario == 1 ? 15 : (scenario == 2 || scenario >= 3) ? 16 : 18;
      const double in = choice == 0 ? 1 : 20, out = choice == 0 ? 20 : 1;
      require(chosen->get_input_delay() == in && chosen->get_output_delay() == out && chosen->get_launch_clock_transition() == TransType::kFall,
              "selected IO context was lost in propagation or tracing");
      require(near(chosen->get_launch_time(), launch) && near(chosen->get_capture_time(), capture) && near(chosen->get_path_delay(), launch + in)
                  && near(chosen->get_required_time(), capture - out),
              "selected clock latency changed");
      const auto directory = std::filesystem::temp_directory_path() / ("ista-io-report-" + std::to_string(getpid()));
      std::filesystem::create_directories(directory);
      config.tr_temp_directory_path = directory.string() + "/";
      config.path_report_number = config.endpoint_path_report_number = 1;
      PathReportWriter(db, config).write(DelayType::kMax, StartEndType::kNone);
      std::ifstream report(directory / "timing_max.rpt");
      std::string text{std::istreambuf_iterator<char>(report), {}};
      std::filesystem::remove_all(directory);
      checkLine(text, "input external delay", in, launch + in);
      checkLine(text, "output external delay", -out, capture - out);
      require(text.find("clock launch (fall edge)") != std::string::npos, "selected falling launch label lost");
      if (scenario == 2)
        require(text.find("clock network delay (ideal)") != std::string::npos && text.find("clock network delay (propagated)") != std::string::npos,
                "capture network label reused the launch clock policy");
    }
}
