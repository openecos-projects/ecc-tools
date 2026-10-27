// SPDX-License-Identifier: MulanPSL-2.0
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "Config.hpp"
#include "Database.hpp"
#include "DelayCalculator.hpp"
#include "ElectricalChecker.hpp"
#include "ElectricalReportWriter.hpp"
#include "FanoutChecker.hpp"
#include "FanoutReportWriter.hpp"
#include "GraphBuilder.hpp"
#include "Logger.hpp"
#include "QorReportWriter.hpp"
#include "ReadLiberty.hpp"
#include "ReportFormatter.hpp"
#include "TimingAnalyzer.hpp"
using namespace ista;
namespace {
void require(bool condition, const std::string& message)
{
  if (!condition)
    throw std::runtime_error(message);
}
std::string readReport(const std::filesystem::path& path)
{
  std::ifstream file(path);
  require(file.good(), "report not written: " + path.string());
  return {std::istreambuf_iterator<char>(file), {}};
}
double jsonNumber(const std::string& json, const std::string& name)
{
  const auto at = json.find("\"" + name + "\":");
  require(at != std::string::npos, "JSON field missing: " + name);
  return std::stod(json.substr(at + name.size() + 3));
}
void checkJsonNumbers()
{
  // A serialization policy must not erase a small computed value or round it to zero.
  for (double value : {-9.7e-8, -1e-14, 1e-14, .061915694, 1.470014811}) {
    std::ostringstream stream;
    outputJsonNumber(stream, value);
    require(std::stod(stream.str()) == value, "JSON lost the original finite value: " + stream.str());
  }
  std::ostringstream special;
  outputJsonNumber(special, -0.0);
  require(!std::signbit(std::stod(special.str())), "JSON retained negative zero");
  special.str("");
  outputJsonNumber(special, std::numeric_limits<double>::infinity());
  require(special.str() == "null", "nonfinite JSON must be null");
}
void checkViolationReports()
{
  Database db;
  Config config;
  const auto dir = std::filesystem::temp_directory_path() / ("ista-electrical-report-" + std::to_string(getpid()));
  std::filesystem::create_directories(dir);
  config.tr_temp_directory_path = dir.string() + "/";
  config.output_timing_reports = 1;
  config.output_timing_features = 1;
  // Literal differences smaller than the timing tolerance, plus equality and positive slack.
  for (double actual : {1.000000097, 1.0, .999999903}) {
    const int count = actual > 1.0 ? 1 : 0;
    db.electricalChecks() = {{ElectricalRule::kCapacitance, "cap", "n", "", AnalysisType::kMax, TransType::kRise, false, actual, 1.0},
                             {ElectricalRule::kTransition, "tran", "n", "", AnalysisType::kMax, TransType::kRise, false, actual, 1.0}};
    FanoutCheck fanout;
    fanout.set_driver_pin("fanout");
    fanout.set_net_name("n");
    fanout.set_fanout_load(actual);
    fanout.set_limit(1.0);
    db.fanoutChecks() = {fanout};
    ElectricalReportWriter(db, config).write();
    FanoutReportWriter(db, config).write();
    QorReportWriter(db, config).write();
    for (const auto* rule : {"max_capacitance", "max_transition", "max_fanout"}) {
      const auto json = readReport(dir / (std::string(rule) + ".json"));
      require(jsonNumber(json, "violation_count") == count, std::string(rule) + " dropped a negative slack");
      require(jsonNumber(json, "slack") == 1.0 - actual, std::string(rule) + " JSON changed slack");
      const auto report = readReport(dir / (std::string(rule) + ".rpt"));
      require((report.find("VIOLATED") != std::string::npos) == (count == 1), std::string(rule) + " text/JSON disagree");
    }
    const auto qor = readReport(dir / "qor_summary.json");
    for (const auto* rule : {"cap", "tran", "fanout"})
      require(jsonNumber(qor, rule) == count, std::string("QoR lost ") + rule + " violation");
  }
  std::filesystem::remove_all(dir);
}
void checkInoutCapacitance()
{
  Database db;
  Config config;
  db.network().setName("pad_cap");
  auto addPin = [&](const std::string& name, const std::string& instance, const std::string& port, PinDirection direction) {
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_pin_name(port);
    pin.set_instance_name(instance);
    pin.set_is_port(instance.empty());
    pin.set_direction(direction);
    if (!instance.empty())
      db.network().instances()[instance].get_pin_name_list().push_back(name);
  };
  auto addNet = [&](const std::string& name, std::vector<std::string> pins) {
    auto& net = db.network().nets()[name];
    net.set_net_name(name);
    net.set_pin_name_list(pins);
    for (const auto& pin : pins)
      db.network().pins().at(pin).set_net_name(name);
  };
  for (const auto& instance : {"buf", "pad"}) {
    db.network().instances()[instance].set_instance_name(instance);
    db.network().instances()[instance].set_cell_name(instance);
  }
  auto& buf = db.liberty().get_cell_map()["buf"];
  buf.get_port_map()["A"].set_is_input(true);
  auto& output = buf.get_port_map()["Y"];
  output.set_is_output(true);
  output.set_max_capacitance(.5);
  ReadLiberty reader(db.liberty(), db.network());
  output.set_function_expression(reader.readLogicExpression("A"));
  auto& pad = db.liberty().get_cell_map()["pad"];
  pad.get_port_map()["OE"].set_is_input(true);
  auto& contact = pad.get_port_map()["PAD"];
  contact.set_is_input(true);
  contact.set_is_output(true);
  contact.set_capacitance(1.4);
  contact.set_three_state_expression(reader.readLogicExpression("!OE"));
  addPin("src", "", "src", PinDirection::kInput);
  addPin("enable", "", "enable", PinDirection::kInput);
  addPin("buf:A", "buf", "A", PinDirection::kInput);
  addPin("buf:Y", "buf", "Y", PinDirection::kOutput);
  addPin("pad:OE", "pad", "OE", PinDirection::kInput);
  addPin("pad:PAD", "pad", "PAD", PinDirection::kInout);
  addNet("input", {"src", "buf:A"});
  addNet("control", {"enable", "pad:OE"});
  addNet("pad_net", {"buf:Y", "pad:PAD"});
  // Distributed wire capacitance must be retained in the receiver's report too.
  auto& parasitic = db.parasitics().nets()["pad_net"];
  parasitic.set_net_name("pad_net");
  parasitic.get_node_map()["buf:Y"].set_capacitance(.03);
  parasitic.get_node_map()["pad:PAD"].set_capacitance(.04);
  ParasiticResistor resistor;
  resistor.set_source_node("buf:Y");
  resistor.set_sink_node("pad:PAD");
  resistor.set_resistance(10.);
  parasitic.get_resistor_list().push_back(resistor);
  DelayCalculator delay(db, config);
  auto checkMode = [&](int enable) {
    db.sdc().get_case_analysis_map().erase("enable");
    if (enable >= 0)
      db.sdc().get_case_analysis_map()["enable"] = enable ? TimingCaseValue::kOne : TimingCaseValue::kZero;
    delay.clear();
    db.graph() = Graph{};
    GraphBuilder(db).build();
    delay.initialize(db.graph());
    const auto checks = ElectricalChecker(db, delay).check();
    auto found = std::find_if(checks.begin(), checks.end(), [](const auto& check) { return check.pin == "pad:PAD"; });
    require((found != checks.end()) == (enable != 1), "inout cap check wrong for OE=" + std::to_string(enable));
    if (found != checks.end()) {
      require(std::abs(found->actual - 1.47) < 1e-12, "inout receiver lost its own pin or wire capacitance");
      require(found->limit == .5, "inout receiver did not inherit external driver limit");
    }
    require(checks.size() == (enable == 1 ? 1 : 2), "inout electrical checks duplicated or lost a physical pin");
  };
  for (int enable : {-1, 0, 1, -1, 0})
    checkMode(enable);
  db.sdc().get_case_analysis_map()["src"] = TimingCaseValue::kZero;
  delay.clear();
  db.graph() = Graph{};
  GraphBuilder(db).build();
  delay.initialize(db.graph());
  require(ElectricalChecker(db, delay).check().empty(), "constant driver created inout cap checks");
  db.sdc().get_case_analysis_map().erase("src");
  checkMode(0);
  db.sdc().get_clock_map()["ideal"].set_is_propagated(false);
  db.graph().driverPoint("buf:Y").get_clock_state("ideal").waveforms[TransType::kRise].physical_slew_map[AnalysisType::kMax][TransType::kRise] = .1;
  require(ElectricalChecker(db, delay).check().empty(), "ideal clock created inout cap checks");
  db.sdc().get_clock_map().clear();
  // Without a three_state expression the output is unconditionally enabled.
  contact.set_three_state_expression(LogicExpression{});
  delay.clear();
  db.graph() = Graph{};
  GraphBuilder(db).build();
  delay.initialize(db.graph());
  const auto always = ElectricalChecker(db, delay).check();
  require(std::none_of(always.begin(), always.end(), [](const auto& check) { return check.pin == "pad:PAD"; }),
          "always-enabled inout acquired an external receiver check");
  contact.set_three_state_expression(reader.readLogicExpression("!OE"));
  checkMode(0);
  db.sdc().electricalLimits().set({ElectricalRule::kCapacitance, LimitObject::kPin, "pad:PAD", false, TransType::kNone, 2.0, true});
  const auto forced = ElectricalChecker(db, delay).check();
  auto receiver = std::find_if(forced.begin(), forced.end(), [](const auto& check) { return check.pin == "pad:PAD"; });
  require(receiver != forced.end() && receiver->limit == 2.0 && receiver->slack() > 0., "receiver force was overridden by inherited driver limit");
  db.sdc().electricalLimits() = ElectricalLimits{};
  // A second external driver must not duplicate the PAD row; select the worst
  // complete driver evaluation. Both drivers see 1.67 pF, including the
  // inout driver's own .2 pF receiver capacitance. Its .25 pF limit wins.
  db.network().instances()["second"].set_instance_name("second");
  db.network().instances()["second"].set_cell_name("strong");
  auto& second = db.liberty().get_cell_map()["strong"].get_port_map()["Y"];
  second.set_is_input(true);
  second.set_is_output(true);
  second.set_capacitance(.2);
  second.set_max_capacitance(.25);
  db.sdc().electricalLimits().set({ElectricalRule::kCapacitance, LimitObject::kPin, "buf:Y", false, TransType::kNone, .5, true});
  second.set_three_state_expression(reader.readLogicExpression("E"));
  db.liberty().get_cell_map()["strong"].get_port_map()["E"].set_is_input(true);
  addPin("second:E", "second", "E", PinDirection::kInput);
  db.network().pins().at("second:E").set_net_name("control");
  db.network().nets().at("control").get_pin_name_list().push_back("second:E");
  addPin("second:Y", "second", "Y", PinDirection::kInout);
  db.network().pins().at("second:Y").set_net_name("pad_net");
  db.network().nets().at("pad_net").get_pin_name_list().push_back("second:Y");
  parasitic.get_node_map()["second:Y"].set_capacitance(0.);
  resistor.set_source_node("buf:Y");
  resistor.set_sink_node("second:Y");
  parasitic.get_resistor_list().push_back(resistor);
  delay.clear();
  db.graph() = Graph{};
  GraphBuilder(db).build();
  delay.initialize(db.graph());
  const auto multi = ElectricalChecker(db, delay).check();
  receiver = std::find_if(multi.begin(), multi.end(), [](const auto& check) { return check.pin == "pad:PAD"; });
  require(multi.size() == 3 && receiver != multi.end(), "multiple drivers duplicated or lost the receiver check");
  require(receiver->limit == .25 && std::abs(receiver->actual - 1.67) < 1e-12, "receiver did not keep worst driver load/limit pair");
  const auto buffer_check = std::find_if(multi.begin(), multi.end(), [](const auto& check) { return check.pin == "buf:Y"; });
  require(buffer_check != multi.end() && std::abs(buffer_check->actual - 1.67) < 1e-12, "multi-driver fixture lost shared receiver capacitance");
  second.set_capacitance(.3);
  delay.clear();
  delay.initialize(db.graph());
  const auto larger_load = ElectricalChecker(db, delay).check();
  receiver = std::find_if(larger_load.begin(), larger_load.end(), [](const auto& check) { return check.pin == "pad:PAD"; });
  require(receiver != larger_load.end() && receiver->limit == .25 && std::abs(receiver->actual - 1.77) < 1e-12,
          "receiver lost the increased inout capacitance or controlling limit");
}
}  // namespace
int main(int argc, char** argv)
{
  Logger::initInst();
  if (argc > 1) {
    const std::string mode = argv[1];
    if (mode == "json")
      checkJsonNumbers();
    else if (mode == "reports")
      checkViolationReports();
    else if (mode == "inout")
      checkInoutCapacitance();
    else
      throw std::runtime_error("unknown test mode");
    Logger::destroyInst();
    return 0;
  }
  Database db;
  Config config;
  const auto dir = std::filesystem::temp_directory_path() / ("ista-electrical-" + std::to_string(getpid()));
  std::filesystem::create_directories(dir);
  config.tr_temp_directory_path = dir.string() + "/";
  config.output_timing_reports = 0;
  config.output_timing_features = 1;
  db.network().setName("test");
  for (const auto* name : {"in", "out"}) {
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_pin_name(name);
    pin.set_is_port(true);
    pin.set_direction(std::string(name) == "in" ? PinDirection::kInput : PinDirection::kOutput);
    pin.set_net_name("wire");
  }
  auto& net = db.network().nets()["wire"];
  net.set_net_name("wire");
  net.get_pin_name_list() = {"in", "out"};
  for (auto mode : {AnalysisType::kMax, AnalysisType::kMin})
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      db.sdc().get_port_constraint_map()["in"].set_input_transition(mode, edge, .4);
      db.sdc().get_port_constraint_map()["out"].set_load(mode, edge, .3, false);
    }
  db.sdc().electricalLimits().set({ElectricalRule::kTransition, LimitObject::kDesign, "test", false, TransType::kNone, .2});
  db.sdc().electricalLimits().set({ElectricalRule::kCapacitance, LimitObject::kDesign, "test", false, TransType::kNone, .1});
  DelayCalculator delay(db, config);
  TimingAnalyzer(db, delay).analyze();
  QorReportWriter(db, config).write();
  std::ifstream file(dir / "qor_summary.json");
  const std::string result((std::istreambuf_iterator<char>(file)), {});
  if (result.find("\"cap\":1") == std::string::npos || result.find("\"tran\":2") == std::string::npos)
    throw std::runtime_error("QoR must count actual capacitance/transition violations, once per pin");
  db.sdc().electricalLimits().set({ElectricalRule::kCapacitance, LimitObject::kPort, "in", false, TransType::kNone, .8, true});
  db.sdc().electricalLimits().set({ElectricalRule::kCapacitance, LimitObject::kPort, "out", false, TransType::kNone, .01});
  for (const auto& check : ElectricalChecker(db, delay).check())
    if (check.rule == ElectricalRule::kCapacitance && std::abs(check.limit - .8) > 1e-9)
      throw std::runtime_error("forced driver cap limit was tightened by a receiver limit");
  db.sdc().get_case_analysis_map()["in"] = TimingCaseValue::kZero;
  TimingAnalyzer(db, delay).analyze();
  if (!db.electricalChecks().empty())
    throw std::runtime_error("static net must not acquire switching electrical checks");
  db.sdc().get_case_analysis_map().clear();
  TimingAnalyzer(db, delay).analyze();
  db.sdc().electricalLimits() = ElectricalLimits{};
  auto& input = db.graph().driverPoint("in");
  input.get_path_state_map()[AnalysisType::kMax][PathSourceType::kInput][TransType::kRise]["core"].set_clock_name("core");
  input.get_clock_state("core").waveforms[TransType::kRise].physical_slew_map[AnalysisType::kMax][TransType::kRise] = .15;
  input.get_clock_state("core").summarize();
  db.sdc().electricalLimits().set({ElectricalRule::kTransition, LimitObject::kClock, "core", true, TransType::kRise, .1});
  auto checks = ElectricalChecker(db, delay).check();
  if (checks.size() != 1 || !checks.front().clock_path || std::abs(checks.front().slack() + .05) > 1e-9)
    throw std::runtime_error("clock-path limit leaked into data or another pin");
  db.sdc().electricalLimits().set({ElectricalRule::kTransition, LimitObject::kClock, "core", false, TransType::kRise, .3});
  checks = ElectricalChecker(db, delay).check();
  if (checks.size() != 1 || checks.front().clock_path || std::abs(checks.front().slack() + .1) > 1e-9)
    throw std::runtime_error("data-path limit failed to select worst scoped slack");
  db.sdc().electricalLimits() = ElectricalLimits{};
  db.sdc().electricalLimits().set({ElectricalRule::kCapacitance, LimitObject::kPort, "out", false, TransType::kNone, .05});
  checks = ElectricalChecker(db, delay).check();
  if (checks.size() != 1 || std::abs(checks.front().actual - .3) > 1e-9 || std::abs(checks.front().limit - .05) > 1e-9)
    throw std::runtime_error("output-port capacitance limit did not check total net load");
  db.sdc().electricalLimits() = ElectricalLimits{};
  auto& instance = db.network().instances()["u"];
  instance.set_cell_name("BUF");
  auto& pin = db.network().pins()["u:A"];
  pin.set_full_name("u:A");
  pin.set_pin_name("A");
  pin.set_instance_name("u");
  auto& cell = db.liberty().get_cell_map()["BUF"];
  cell.set_library_name("lib");
  cell.set_slew_lower_threshold_pct_rise(.1);
  cell.set_slew_upper_threshold_pct_rise(.9);
  auto& point = db.graph().points()["u:A"];
  point.get_data_slew_map()[AnalysisType::kMax][TransType::kRise] = .3;
  auto& vertex = db.graph().vertices().emplace_back();
  vertex.set_pin_name("u:A");
  vertex.set_timing_point(&point);
  db.sdc().electricalLimits().set({ElectricalRule::kTransition, LimitObject::kPin, "u:A", false, TransType::kNone, .2});
  checks = ElectricalChecker(db, delay).check();
  if (checks.size() != 1 || std::abs(checks.front().limit - .4) > 1e-9)
    throw std::runtime_error("SDC transition limit did not use the pin's trip points");
  cell.get_port_map()["A"].set_max_transition(.25);
  checks = ElectricalChecker(db, delay).check();
  if (checks.size() != 1 || std::abs(checks.front().limit - .25) > 1e-9)
    throw std::runtime_error("more restrictive native Liberty transition limit lost");
  db.sdc().electricalLimits().set({ElectricalRule::kTransition, LimitObject::kPin, "u:A", false, TransType::kNone, .2, true});
  checks = ElectricalChecker(db, delay).check();
  if (checks.size() != 1 || std::abs(checks.front().limit - .4) > 1e-9)
    throw std::runtime_error("forced transition failed to override native Liberty limit in the pin trip-point basis");
  db.sdc().electricalLimits() = ElectricalLimits{};
  cell.set_slew_lower_threshold_pct_rise(.3);
  cell.set_slew_upper_threshold_pct_rise(.7);
  auto& driver_port = cell.get_port_map()["Y"];
  driver_port.set_max_transition(.2);
  driver_port.set_max_capacitance(.1);
  driver_port.set_max_fanout(1.);
  TimingDrivingCell driver;
  driver.set_cell_name("BUF");
  driver.set_to_pin("Y");
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    for (auto edge : {TransType::kRise, TransType::kFall})
      db.sdc().get_port_constraint_map()["in"].set_driving_cell(mode, edge, driver);
  checks = ElectricalChecker(db, delay).check();
  int inherited = 0;
  for (const auto& check : checks)
    if (check.pin == "in") {
      ++inherited;
      if (std::abs(check.slack() + .2) > 1e-9)
        throw std::runtime_error("inherited rule has incorrect violation slack");
    }
  if (inherited != 2 || db.sdc().fanoutLimit(db.network(), db.liberty(), "in") != 1.)
    throw std::runtime_error("driving cell did not supply input design rules");
  driver.set_no_design_rule(true);
  db.sdc().get_port_constraint_map()["in"].set_driving_cell(AnalysisType::kMax, TransType::kRise, driver);
  for (const auto& check : ElectricalChecker(db, delay).check())
    if (check.pin == "in")
      throw std::runtime_error("no_design_rule did not remove inherited limits across all edges");
  if (db.sdc().fanoutLimit(db.network(), db.liberty(), "in"))
    throw std::runtime_error("no_design_rule retained fanout limit");
  // Ideal clock networks are exempt even when data slew also reaches the pin.
  db.sdc().get_clock_map()["core"].set_is_propagated(false);
  db.sdc().electricalLimits().set({ElectricalRule::kCapacitance, LimitObject::kDesign, "test", false, TransType::kNone, .1});
  db.sdc().get_max_fanout() = .5;
  for (const auto& check : ElectricalChecker(db, delay).check())
    if (check.pin == "in")
      throw std::runtime_error("ideal clock acquired an electrical check through its data state");
  for (const auto& check : FanoutChecker(db).check())
    if (check.get_driver_pin() == "in")
      throw std::runtime_error("ideal clock acquired a fanout check");
  db.sdc().get_clock_map()["core"].set_is_propagated(true);
  bool found_cap = false;
  for (const auto& check : ElectricalChecker(db, delay).check())
    if (check.pin == "in" && check.rule == ElectricalRule::kCapacitance)
      found_cap = true;
  if (!found_cap)
    throw std::runtime_error("propagated clock lost capacitance check");
  std::filesystem::remove_all(dir);
  Logger::destroyInst();
}
