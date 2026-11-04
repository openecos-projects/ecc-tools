// SPDX-License-Identifier: MulanPSL-2.0
#include <unistd.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "CheckArcExtractor.hpp"
#include "DelayCalculator.hpp"
#include "PathChecker.hpp"
#include "PathReportWriter.hpp"
#include "PathSearcher.hpp"
#include "ReportPathContext.hpp"
using namespace ista;
static void require(bool ok, const char* message)
{
  if (!ok)
    throw std::runtime_error(message);
}
int main(int argc, char** argv)
{
  const std::string mode = argc > 1 ? argv[1] : "query";
  Database db;
  Config config;
  auto& reg = db.network().instances()["ff"];
  reg.set_instance_name("ff");
  reg.set_is_sequential(true);
  reg.set_output_pin_name("ff:Q");
  reg.set_clock_pin_name("ff:CK");
  reg.set_data_pin_name("ff:D");
  TimingArc trigger;
  trigger.set_trigger_trans_type(TransType::kFall);
  reg.get_clock_to_q_arc().get_timing_arc_list().push_back(trigger);
  for (const auto* port : {"Q", "CK", "D"}) {
    auto& pin = db.network().pins()[std::string("ff:") + port];
    pin.set_instance_name("ff");
    pin.set_pin_name(port);
    pin.set_full_name(std::string("ff:") + port);
  }
  TimingPath path;
  path.set_start_point("ff:Q");
  path.set_launch_clock_pin_transition(TransType::kFall);
  path.set_clock_name("launch");
  ReportPathContext context(db);
  if (mode == "query" || mode == "edge") {
    const auto pins = db.network().pins().size(), nets = db.network().nets().size(), instances = db.network().instances().size();
    const auto description = context.startPoint(path).description;
    if (mode == "query")
      require(db.network().pins().size() == pins && db.network().nets().size() == nets && db.network().instances().size() == instances,
              "report query inserted missing model objects");
    else
      require(description.find("falling edge-triggered") != std::string::npos, "falling launch described as rising");
    return 0;
  }
  for (const auto* name : {"launch", "capture", "in"}) {
    auto& pin = db.network().pins()[name];
    pin.set_is_port(true);
    pin.set_pin_name(name);
    pin.set_direction(PinDirection::kInput);
  }
  for (const auto* name : {"launch", "capture"}) {
    auto& clock = db.sdc().get_clock_map()[name];
    clock.set_clock_name(name);
    clock.set_period(10);
    clock.set_fall_edge(5);
    clock.set_source_list({name});
  }
  TimingCheckArc check;
  check.set_data_port("ff:D");
  check.set_clock_port("ff:CK");
  check.set_clock_trans_type(TransType::kRise);
  check.set_check_type(TimingCheckType::kSetup);
  check.set_check_time(0.1);
  reg.get_check_arc_list().push_back(check);
  path.set_start_point("in");
  path.set_end_point("ff:D");
  path.set_capture_clock_name("capture");
  path.set_analysis_type(AnalysisType::kMax);
  path.set_source_type(PathSourceType::kInput);
  path.set_check_type(TimingCheckType::kSetup);
  path.set_check_time(0.1);
  path.set_trans_type(TransType::kRise);
  if (mode == "model") {
    PathGroup group;
    group.setName("capture");
    group.endpointPaths()["ff:D"].push_back(path);
    db.pathGroups().push_back(group);
    for (bool virtual_launch : {false, true}) {
      if (virtual_launch)
        db.sdc().get_clock_map().at("launch").set_source_list({});
      for (auto edge : {TransType::kRise, TransType::kFall}) {
        db.pathGroups().front().endpointPaths().at("ff:D").front().set_capture_clock_transition(edge);
        CharacterizedLibrary model;
        CheckArcExtractor(db).extract(model, AnalysisType::kMax);
        require(model.get_timing_arc_list().size() == 1 && model.get_timing_arc_list().front().get_source_port() == "capture",
                "extracted input check used the launch clock or vanished with a virtual launch");
        require(model.get_timing_arc_list().front().get_timing_type() == (edge == TransType::kFall ? "setup_falling" : "setup_rising"),
                "model extraction reselected the first check arc instead of the path's capture edge");
      }
    }
    return 0;
  }
  auto& clock_point = db.graph().points()["ff:CK"];
  clock_point.set_is_clock_point(true);
  clock_point.setDefaultClock("capture");
  clock_point.setLaunchClock("capture");
  clock_point.get_clock_state("capture").waveforms[TransType::kRise].arrival_map[AnalysisType::kMin][TransType::kRise] = 0.;
  TimingClockUncertainty uncertainty;
  uncertainty.set_from_clocks({"launch"});
  uncertainty.set_to_clocks({"capture"});
  uncertainty.set_value(0.7);
  uncertainty.set_setup(true);
  uncertainty.set_hold(false);
  db.sdc().get_clock_uncertainty_list().push_back(uncertainty);
  auto& state = db.graph().points()["ff:D"].get_path_state_map()[AnalysisType::kMax][PathSourceType::kInput][TransType::kRise]["path"];
  state.set_path_state_tag("path");
  state.set_start_point("in");
  state.set_clock_name("launch");
  state.set_trans_type(TransType::kRise);
  state.set_arrival(3);
  DelayCalculator delay(db, config);
  PathChecker checker(db, delay);
  const ExceptionThroughIndex index(db.sdc(), db.network());
  auto paths = PathSearcher(db, checker, index).findPathEnds("ff:D");
  require(paths.size() == 1 && std::abs(paths[0].get_required_time() - 9.2) < 1e-9 && std::abs(paths[0].get_slack() - 6.2) < 1e-9,
          "pair uncertainty check fixture changed");
  PathGroup group;
  group.setName("capture");
  group.endpointPaths()["ff:D"] = paths;
  db.pathGroups().push_back(group);
  const auto directory = std::filesystem::temp_directory_path() / ("ista-path-context-" + std::to_string(getpid()));
  std::filesystem::create_directories(directory);
  config.tr_temp_directory_path = directory.string() + "/";
  config.path_report_number = config.endpoint_path_report_number = 1;
  PathReportWriter(db, config).write(DelayType::kMax, StartEndType::kNone);
  std::ifstream report(directory / "timing_max.rpt");
  std::string text{std::istreambuf_iterator<char>(report), {}};
  std::filesystem::remove_all(directory);
  require(text.find("clock uncertainty") != std::string::npos && text.find("-0.700") != std::string::npos, "report omitted the selected pair uncertainty");
}
