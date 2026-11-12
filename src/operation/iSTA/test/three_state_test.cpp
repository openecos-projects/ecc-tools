// SPDX-License-Identifier: MulanPSL-2.0
#include <unistd.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ClockPropagator.hpp"
#include "DataManager.hpp"
#include "DataPropagator.hpp"
#include "DelayCalculator.hpp"
#include "GraphBuilder.hpp"
#include "Logger.hpp"
#include "PathAnalyzer.hpp"
#include "PathChecker.hpp"
#include "ReadLiberty.hpp"
#include "RequiredPropagator.hpp"
#include "SdcCommand.hpp"
#include "SdfValueExtractor.hpp"
#include "cell/LibertyArcSelector.hpp"
#include "idm.h"

using namespace ista;
namespace {
void require(bool value, const std::string& message)
{
  if (!value)
    throw std::runtime_error(message);
}
void command(const std::string& text)
{
  require(SdcCommand::getInst().evalString(text) == TCL_OK, text);
}

std::filesystem::path fixture()
{
  auto file = std::filesystem::temp_directory_path() / ("ista-three-state-" + std::to_string(getpid()) + ".lib");
  std::ofstream out(file);
  out << "library(tristate) { delay_model:table_lookup; time_unit:\"1ns\"; capacitive_load_unit(1,pf);\n"
         "lu_table_template(slew) { variable_1:input_net_transition; index_1(\"0,1\"); }\n";
  for (const std::string event : {"enable", "disable"}) {
    for (const std::string suffix : {"", "_rise", "_fall"}) {
      for (const std::string sense : {"positive_unate", "negative_unate", "non_unate"}) {
        out << "cell(" << event << suffix << "_" << sense << ") { area:1; pin(D){direction:input;} pin(EN){direction:input;}\n"
            << "pin(Y){ direction:output; function:\"D\"; three_state:\"!EN\"; timing(){related_pin:\"EN\"; timing_type:three_state_"
            << event << suffix << "; timing_sense:" << sense << ";\n"
            << "cell_rise(slew){values(\"2,3\");} cell_fall(slew){values(\"4,6\");}\n"
            << "rise_transition(slew){values(\"0.1,0.1\");} fall_transition(slew){values(\"0.2,0.2\");}\n}}}\n";
      }
    }
  }
  out << "}\n";
  return file;
}

void design(Database& db, const LibertyLibrary& library, const std::string& cell)
{
  STADC.clear();
  db = Database{};
  db.liberty() = library;
  db.network().setName("three_state_test");
  auto& instance = db.network().instances()["u"];
  instance.set_instance_name("u");
  instance.set_cell_name(cell);
  for (const auto& pair : std::vector<std::pair<std::string, std::string>>{{"enable", "EN"}, {"data", "D"}, {"out", "Y"}}) {
    const auto& [port, member] = pair;
    const std::string full = "u:" + member;
    auto& net = db.network().nets()[port];
    net.set_net_name(port);
    net.set_pin_name_list({port, full});
    for (const auto& name : {port, full}) {
      auto& pin = db.network().pins()[name];
      pin.set_full_name(name);
      pin.set_pin_name(name == port ? port : member);
      pin.set_is_port(name == port);
      pin.set_instance_name(name == port ? "" : "u");
      pin.set_direction(port == "out" ? PinDirection::kOutput : PinDirection::kInput);
      pin.set_net_name(port);
    }
    instance.get_pin_name_list().push_back(full);
  }
}

void selection(const LibertyLibrary& library)
{
  auto& db = STADM.getDatabase();
  for (const std::string event : {"enable", "disable"})
    for (const std::string suffix : {"", "_rise", "_fall"})
      for (const std::string sense : {"positive_unate", "negative_unate", "non_unate"}) {
        const auto cell = event + suffix + "_" + sense;
        design(db, library, cell);
        auto& arcs = db.liberty().get_cell_map().at(cell).get_cell_arc_list();
        require(arcs.size() == 1 && arcs.front().get_is_timing_graph_arc(), "three-state control arc was excluded from timing graph: " + cell);
        auto& arc = arcs.front();
        LibertyArcSelector select(db);
        for (auto input : {TransType::kRise, TransType::kFall}) {
          const bool active = sense == "non_unate" || (sense == "positive_unate" ? input == TransType::kRise : input == TransType::kFall);
          auto outputs = select.getOutputTransTypeList(arc, input);
          require(outputs.size() == (active ? (suffix.empty() ? 2U : 1U) : 0U), "wrong control edge or output lane count: " + cell);
          for (auto output : {TransType::kRise, TransType::kFall}) {
            const bool allowed = active && (suffix.empty() || (suffix == "_rise" ? output == TransType::kRise : output == TransType::kFall));
            auto result = STADC.cellDelay(db.network().pins().at("u:Y").get_full_name(), arc, AnalysisType::kMax, input, output, .1);
            require(result.has_value() == allowed, "inactive three-state lane returned a fallback result: " + cell);
            if (result)
              require(std::abs(result->get_delay() - (output == TransType::kRise ? 2.1 : 4.2)) < 1e-9, "control slew/output table mismatch: " + cell);
          }
        }
      }
}

void conditionalPath(const LibertyLibrary& library)
{
  auto& db = STADM.getDatabase();
  design(db, library, "enable_positive_unate");
  auto& cell = db.liberty().get_cell_map().at("enable_positive_unate");
  ReadLiberty reader(db.liberty(), db.network());
  cell.get_port_map().at("Y").set_three_state_expression(reader.readLogicExpression("EN ^ D"));
  auto& candidates = cell.get_cell_arc_list().front().get_timing_arc_list();
  candidates.front().set_when_expression(reader.readLogicExpression("D"));
  auto negative = candidates.front();
  negative.set_sense(TimingArcSense::kNegative);
  negative.set_trigger_trans_type(TransType::kFall);
  negative.set_when_expression(reader.readLogicExpression("!D"));
  candidates.push_back(negative);
  command("create_clock -name vclk -period 10");
  command("set_input_delay -clock vclk 1 [get_ports enable]");
  command("set_input_transition 0.1 [get_ports enable]");
  command("set_output_delay -clock vclk 2 [get_ports out]");
  command("set_case_analysis 1 [get_ports data]");
  GraphBuilder(db).build();
  STADC.initialize(db.graph());
  for (auto& arc : db.graph().arcs()) {
    if (arc.get_type() != ArcType::kCell) continue;
    for (auto analysis : {AnalysisType::kMax, AnalysisType::kMin}) {
      auto& lanes = arc.get_input_output_delay_map().at(analysis);
      require(!lanes.contains(TransType::kFall) || lanes.at(TransType::kFall).empty(),
              "false when condition created a zero-delay control lane");
      require(lanes.at(TransType::kRise).size() == 2, "active conditional control lanes were removed");
    }
  }
  ClockPropagator(db, STADC).propagate();
  DataPropagator(db, STADC).propagate();
  PathChecker check(db, STADC);
  RequiredPropagator(db, check).propagate();
  PathAnalyzer(db, check).analyze();
  bool found = false;
  for (auto& group : db.pathGroups())
    for (auto& [endpoint, paths] : group.endpointPaths())
      for (auto& path : paths)
        if (path.get_start_point() == "enable" && path.get_end_point() == "out") {
          found = true;
          require(path.get_point_list().back().get_arrival() > 3.0, "inactive conditional lane produced a false short path");
        }
  require(found, "active conditional control path was removed");
}

void path(const LibertyLibrary& library, bool disabled, bool clock)
{
  auto& db = STADM.getDatabase();
  design(db, library, "enable_positive_unate");
  command("create_clock -name vclk -period 10");
  command("set_input_delay -clock vclk 1 [get_ports enable]");
  command("set_input_transition 0.1 [get_ports enable]");
  command("set_output_delay -clock vclk 2 [get_ports out]");
  // Constant data must not erase a changing enable event.
  command("set_case_analysis 0 [get_ports data]");
  if (disabled)
    command("set_case_analysis 0 [get_ports enable]");
  if (clock) {
    command("create_clock -name control -period 12 [get_ports enable]");
    command("set_propagated_clock [get_clocks control]");
  }
  GraphBuilder(db).build();
  STADC.initialize(db.graph());
  ClockPropagator(db, STADC).propagate();
  if (clock) {
    require(db.graph().driverPoint("u:Y").find_clock_state("control") == nullptr, "clock propagated through three-state control by default");
    return;
  }
  DataPropagator(db, STADC).propagate();
  PathChecker check(db, STADC);
  RequiredPropagator(db, check).propagate();
  PathAnalyzer(db, check).analyze();
  bool found = false;
  for (auto& group : db.pathGroups())
    for (auto& [endpoint, paths] : group.endpointPaths())
      for (auto& path : paths)
        if (path.get_start_point() == "enable" && path.get_end_point() == "out")
          found = true;
  require(found != disabled, "control path missing or static enable propagated");
  if (!disabled) {
    bool written = false;
    for (auto& arc : db.graph().arcs()) {
      if (arc.get_type() != ArcType::kCell)
        continue;
      auto values = SdfValueExtractor(db).graphCellArcDelays(arc);
      require(values.contains({"", TransType::kRise}) && !values.contains({"", TransType::kNone}), "SDF lost control edge identity");
      written = true;
    }
    require(written, "control arc absent from SDF");
  }
}
}  // namespace

int main()
{
  Logger::initInst();
  SdcCommand::initInst();
  sdc::registerSdcCommands(SdcCommand::getInst());
  DataManager::initInst();
  DelayCalculator::initInst();
  STADM.getConfig().thread_number = 1;
  auto file = fixture();
  require(dmInst->readLib({file.string()}), "fixture parse failed");
  ReadLiberty(STADM.getDatabase().liberty(), STADM.getDatabase().network()).read();
  std::filesystem::remove(file);
  const auto library = STADM.getDatabase().liberty();
  selection(library);
  conditionalPath(library);
  path(library, false, false);
  path(library, true, false);
  path(library, false, true);
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
  SdcCommand::destroyInst();
  Logger::destroyInst();
}
