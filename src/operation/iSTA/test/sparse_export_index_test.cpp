// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>
#include <string>
#include <vector>

#include "DataManager.hpp"
#include "Logger.hpp"
#include "SparseTimingSnapshot.hpp"
#include "TimingDataExporter.hpp"

using namespace ista;

static void require(bool condition, const std::string& message)
{
  if (!condition)
    throw std::runtime_error(message);
}

int main()
{
  Logger::initInst();
  DataManager::initInst();
  auto& database = STADM.getDatabase();
  auto& instance = database.network().instances()["ff"];
  instance.set_instance_name("ff");
  instance.set_cell_name("FF");
  instance.set_is_sequential(true);
  instance.set_clock_pin_name("ff:CK");
  instance.set_data_pin_name("ff:D");
  auto& cell = database.liberty().get_cell_map()["FF"];
  cell.set_cell_name("FF");
  for (const auto* name : {"CK", "D"}) {
    cell.get_port_map()[name].set_is_input(true);
    auto& pin = database.network().pins()[std::string("ff:") + name];
    pin.set_instance_name("ff");
    pin.set_is_port(false);
    pin.set_pin_name(name);
    pin.set_full_name(std::string("ff:") + name);
    pin.set_direction(PinDirection::kInput);
  }
  auto& input = database.network().pins()["in"];
  input.set_full_name("in");
  input.set_pin_name("in");
  input.set_is_port(true);
  input.set_direction(PinDirection::kInput);
  for (const auto* name : {"launch", "capture"}) {
    auto& clock = database.sdc().get_clock_map()[name];
    clock.set_clock_name(name);
    clock.set_period(10);
    clock.set_fall_edge(5);
  }
  auto& capture = database.graph().points()["ff:CK"];
  capture.set_is_clock_point(true);
  capture.setDefaultClock("capture");
  capture.setLaunchClock("capture");
  capture.get_clock_state("capture").waveforms[TransType::kRise].arrival_map[AnalysisType::kMin][TransType::kRise] = 0;
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    auto& path = capture.get_path_state_map()[AnalysisType::kMax][PathSourceType::kRegister][edge]["clock"];
    path.set_path_state_tag("clock");
    path.set_start_point("ff:CK");
    path.set_clock_name("capture");
    path.set_trans_type(edge);
    path.set_arrival(0);
  }

  TimingCheckArc check;
  check.set_clock_port("CK");
  check.set_data_port("D");
  check.set_clock_trans_type(TransType::kRise);
  check.set_check_type(TimingCheckType::kSetup);
  TimingArc variant;
  variant.set_arc_idx(7);
  for (auto edge : {TransType::kRise, TransType::kFall})
    variant.get_check_table_map()[edge].set_value_list({0.1});
  check.get_timing_arc_list().push_back(variant);
  cell.get_check_arc_list().push_back(check);
  check.set_clock_port("ff:CK");
  check.set_data_port("ff:D");
  instance.get_check_arc_list().push_back(check);
  database.graph().endpoints().push_back("ff:D");
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    for (const auto* tag : {"first", "second"}) {
      auto& path = database.graph().points()["ff:D"].get_path_state_map()[AnalysisType::kMax][PathSourceType::kInput][edge][tag];
      path.set_path_state_tag(tag);
      path.set_start_point("in");
      path.set_clock_name("launch");
      path.set_trans_type(edge);
      path.set_arrival(3);
    }
  }
  const auto original = TimingDataExporter::exportData(database, STADM.getConfig());
  require(original.timing_check_arcs.size() == 1, "fixture lost the selected check LUT");
  for (bool prefix_duplicate : {false, true}) {
    auto snapshot = original;
    const auto exact = snapshot.timing_check_arcs.front();
    snapshot.timing_check_arcs.clear();
    // A row differing in any one key field must never shadow the exact row.
    for (int field = 0; field < 4; ++field) {
      auto decoy = exact;
      if (field == 0)
        decoy.sink_pin = "other:D";
      else if (field == 1)
        decoy.source_pin = "other:CK";
      else if (field == 2)
        ++decoy.check_type;
      else
        ++decoy.library_arc_id;
      snapshot.timing_check_arcs.push_back(decoy);
    }
    if (prefix_duplicate)
      snapshot.timing_check_arcs.insert(snapshot.timing_check_arcs.begin(), exact);
    const int32_t expected = prefix_duplicate ? 0 : 4;
    snapshot.timing_check_arcs.push_back(exact);
    snapshot.timing_check_arcs.push_back(exact);
    SparseTimingExportAdapter::compileModel(snapshot);
    require(snapshot.sparse.unsupported.empty(), snapshot.sparse.unsupported.empty() ? "" : snapshot.sparse.unsupported.front());
    std::vector<int32_t> selected;
    for (const auto& row : snapshot.sparse.checks)
      selected.push_back(row.endpoint_arc);
    require(selected == std::vector<int32_t>(4, expected), "check lookup lost exact identity or first-duplicate semantics");
  }
  DataManager::destroyInst();
  Logger::destroyInst();
}
