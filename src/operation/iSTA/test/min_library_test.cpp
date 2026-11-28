// SPDX-License-Identifier: MulanPSL-2.0
#include <unistd.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
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
#include "SdfWriter.hpp"
#include "idm.h"

using namespace ista;
namespace {
void require(bool value, const std::string& message)
{
  if (!value)
    throw std::runtime_error(message);
}

std::filesystem::path fixture(const std::string& mode, double temperature = 25.)
{
  auto file = std::filesystem::temp_directory_path() / ("ista-" + std::to_string(getpid()) + "-" + mode + ".lib");
  std::ofstream out(file);
  const bool early = mode == "min";
  out << "library(" << mode << ") { delay_model:table_lookup; time_unit:\"1ns\"; voltage_unit:\"1V\";"
      << "capacitive_load_unit(1,pf); nom_voltage:1.2; nom_process:1; nom_temperature:" << temperature << ";"
      << "cell(macro) { area:1; pin(CLK){direction:input; clock:true;} pin(A){direction:input; capacitance:" << (early ? .03 : .01) << ";"
      << "timing(){related_pin:\"CLK\"; timing_type:setup_rising; rise_constraint(scalar){values(\"" << (early ? .1 : .3)
      << "\");} fall_constraint(scalar){values(\"" << (early ? .1 : .3) << "\");}}"
      << "timing(){related_pin:\"CLK\"; timing_type:hold_rising; rise_constraint(scalar){values(\"" << (early ? .4 : .1)
      << "\");} fall_constraint(scalar){values(\"" << (early ? .4 : .1) << "\");}}}"
      << "pin(Y){direction:output; function:\"A\"; timing(){related_pin:\"A\"; timing_type:combinational; timing_sense:positive_unate;"
      << "cell_rise(scalar){values(\"" << (early ? .9 : .2) << "\");} cell_fall(scalar){values(\"" << (early ? .9 : .2)
      << "\");} rise_transition(scalar){values(\"0.1\");} fall_transition(scalar){values(\"0.1\");}}}}}";
  return file;
}

void design(Database& db)
{
  db.network().setName("paired_model");
  auto& instance = db.network().instances()["u"];
  instance.set_instance_name("u");
  instance.set_cell_name("macro");
  for (const std::string member : {"CLK", "A", "Y"}) {
    const auto full = "u:" + member;
    auto& net = db.network().nets()[member];
    net.set_net_name(member);
    net.set_pin_name_list({member, full});
    for (const auto& name : {member, full}) {
      auto& pin = db.network().pins()[name];
      pin.set_full_name(name);
      pin.set_pin_name(member);
      pin.set_is_port(name == member);
      pin.set_instance_name(name == member ? "" : "u");
      pin.set_direction(member == "Y" ? PinDirection::kOutput : PinDirection::kInput);
      pin.set_net_name(member);
    }
    instance.get_pin_name_list().push_back(full);
  }
}

void checkModel(Database& db)
{
  auto& cell = db.liberty().get_cell_map().at("macro");
  auto& arcs = cell.get_cell_arc_list();
  require(arcs.size() == 2, "paired delay families were overwritten");
  std::string pin = "u:Y";
  for (auto mode : {AnalysisType::kMax, AnalysisType::kMin}) {
    unsigned active = 0;
    for (auto& arc : arcs) {
      const auto delay = STADC.cellDelay(pin, arc, mode, TransType::kRise, TransType::kRise, .1);
      if (!delay)
        continue;
      ++active;
      require(std::abs(delay->get_delay() - (mode == AnalysisType::kMax ? .2 : .9)) < 1e-9,
              "model selection used numerical extrema, not explicit analysis type");
    }
    require(active == 1, "wrong number of active analysis families");
  }
  require(cell.get_check_arc_list().size() == 2, "setup/hold checks lost or duplicated");
  for (auto& check : cell.get_check_arc_list()) {
    const bool hold = check.get_check_type() == TimingCheckType::kHold;
    const auto value = STADC.checkDelay(check, hold ? AnalysisType::kMin : AnalysisType::kMax, TransType::kRise, TransType::kRise, .1, .1);
    require(std::abs(value - (hold ? .4 : .3)) < 1e-9, "setup/hold selected the wrong model");
  }
  require(std::abs(cell.get_port_map().at("A").get_trans_capacitance_map().at(AnalysisType::kMin).at(TransType::kRise) - .03) < 1e-9,
          "min capacitance did not use the early model");
}

void propagation(Database& db)
{
  for (const auto* command : {"create_clock -name clk -period 10 [get_ports CLK]", "set_propagated_clock [all_clocks]",
                              "set_input_delay -clock clk 1 [get_ports A]", "set_output_delay -clock clk 2 [get_ports Y]"})
    require(SdcCommand::getInst().evalString(command) == TCL_OK, command);
  GraphBuilder(db).build();
  STADC.initialize(db.graph());
  ClockPropagator(db, STADC).propagate();
  DataPropagator(db, STADC).propagate();
  PathChecker check(db, STADC);
  RequiredPropagator(db, check).propagate();
  PathAnalyzer(db, check).analyze();
  unsigned late = 0, early = 0;
  for (auto& group : db.pathGroups())
    for (auto& [endpoint, paths] : group.endpointPaths())
      for (auto& path : paths) {
        if (path.get_start_point() != "A" || path.get_end_point() != "Y")
          continue;
        const bool is_max = path.get_analysis_type() == AnalysisType::kMax;
        is_max ? ++late : ++early;
        require(std::abs(path.get_point_list().back().get_arrival() - (is_max ? 1.2 : 1.9)) < 1e-9, "inactive family produced a false propagation path");
      }
  require(late > 0 && early > 0, "paired graph lost an analysis lane");
  std::ostringstream sdf;
  SdfWriter(db, STADM.getConfig()).write(sdf);
  const auto text = sdf.str();
  const auto iopath = text.find("(IOPATH A Y ");
  require(iopath != std::string::npos && text.find("(IOPATH A Y ", iopath + 1) == std::string::npos, "paired models emitted duplicate SDF IOPATHs");
  const auto line = text.substr(iopath, text.find('\n', iopath) - iopath);
  require(line.find("0.9") != std::string::npos && line.find("0.2") != std::string::npos, "SDF lost a paired-model delay");
}

void clockPropagation(Database& db)
{
  const auto library = db.liberty();
  STADC.clear();
  db = Database{};
  db.liberty() = library;
  design(db);
  for (const auto* command : {"create_clock -name clk -period 10 [get_ports A]", "set_propagated_clock [all_clocks]"})
    require(SdcCommand::getInst().evalString(command) == TCL_OK, command);
  GraphBuilder(db).build();
  STADC.initialize(db.graph());
  ClockPropagator(db, STADC).propagate();
  const auto* clock = db.graph().driverPoint("u:Y").find_clock_state("clk");
  require(clock != nullptr, "paired macro lost propagated clock");
  for (auto mode : {AnalysisType::kMax, AnalysisType::kMin}) {
    const auto arrival = clock->arrival(mode, TransType::kRise);
    require(arrival && std::abs(*arrival - (mode == AnalysisType::kMax ? .2 : .9)) < 1e-9, "clock propagation selected the wrong early/late model");
  }
}

void scalarSdf(Database& db)
{
  TimingCellArc arc;
  arc.setAnalysisType(AnalysisType::kMin);
  arc.set_delay_min(.9);
  arc.set_delay_max(.2);
  for (bool has_variant : {false, true}) {
    if (has_variant)
      arc.get_timing_arc_list().emplace_back();
    auto values = SdfValueExtractor(db).cellArcDelays(arc);
    auto& delay = values.at({});
    require(delay.get_rise_min_delay() == .9 && delay.get_fall_min_delay() == .9 && !delay.get_rise_max_delay() && !delay.get_fall_max_delay(),
            "scalar SDF fallback invented an inactive analysis lane");
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
  const auto late = fixture("max"), early = fixture("min"), wrong = fixture("wrong", 125.);
  auto& db = STADM.getDatabase();
  design(db);
  require(dmInst->readLib({early.string(), late.string()}), "fixture parse failed");
  ReadLiberty(db.liberty(), db.network()).read({early.string()});
  checkModel(db);
  propagation(db);
  clockPropagation(db);
  scalarSdf(db);
  for (const auto& paths : std::vector<std::vector<std::string>>{{"not-loaded.lib"}, {early.string(), early.string()}, {late.string(), early.string()}}) {
    bool rejected = false;
    try {
      ReadLiberty(db.liberty(), db.network()).read(paths);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "invalid min-library selection was silently accepted");
  }
  require(dmInst->readLib({late.string(), wrong.string()}), "PVT fixture parse failed");
  bool rejected = false;
  try {
    ReadLiberty(db.liberty(), db.network()).read({wrong.string()});
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "mismatched PVT was silently paired");
  require(dmInst->readLib({late.string()}), "reload failed");
  ReadLiberty(db.liberty(), db.network()).read();
  const auto& arcs = db.liberty().get_cell_map().at("macro").get_cell_arc_list();
  require(arcs.size() == 1 && arcs.front().supportsAnalysis(AnalysisType::kMin) && arcs.front().supportsAnalysis(AnalysisType::kMax),
          "reload retained stale paired models or changed ordinary-library semantics");
  for (const auto& file : {late, early, wrong})
    std::filesystem::remove(file);
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
  SdcCommand::destroyInst();
  Logger::destroyInst();
}
