// SPDX-License-Identifier: MulanPSL-2.0
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <unistd.h>
#include <set>
#include <stdexcept>
#include <vector>

#include "Config.hpp"
#include "Database.hpp"
#include "LibertyDataExporter.hpp"
#include "STAInterface.hpp"
#include "TimingData.hpp"
#include "TimingDataExporter.hpp"
#include "liberty/LibertyLibrary.hpp"

namespace {
using namespace ista;

void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

void checkUnreadyExport()
{
  bool rejected = false;
  try {
    static_cast<void>(STAInterface::getInst().exportTimingData());
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  STAInterface::destroyInst();
  require(rejected, "timing export accepted an uninitialized session");
}

void checkRectangularTableExport()
{
  LibertyLibrary library;
  auto& cell = library.get_cell_map()["sample"];
  cell.set_cell_name("sample");

  TimingTable table;
  table.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
  table.set_variable_type2(TimingTableVariableType::kInputTransition);
  table.set_axis_list({{2.0, 4.0}, {10.0, 20.0, 30.0}});
  table.set_value_list({1.0, 2.0, 3.0, 4.0, 5.0, 6.0});

  TimingArc arc;
  arc.set_time_unit_scale(100.0);
  arc.set_cap_unit_scale(2.0);
  arc.set_sense(TimingArcSense::kPositive);
  arc.get_delay_table_map()[TransType::kRise] = table;
  // Scalar tables have no axes and must still receive the time scale.
  TimingTable scalar;
  scalar.set_value_list({7.0});
  arc.get_slew_table_map()[TransType::kFall] = scalar;
  TimingCellArc cell_arc;
  cell_arc.set_source_port("A");
  cell_arc.set_sink_port("Y");
  cell_arc.get_timing_arc_list().push_back(arc);
  cell.get_cell_arc_list().push_back(cell_arc);

  TimingCheckArc check;
  check.set_clock_port("CK");
  check.set_data_port("D");
  check.set_clock_trans_type(TransType::kFall);
  check.set_check_type(TimingCheckType::kSetup);
  table.set_variable_type1(TimingTableVariableType::kConstrainedTransition);
  table.set_variable_type2(TimingTableVariableType::kInputTransition);
  arc.get_delay_table_map().clear();
  arc.get_slew_table_map().clear();
  arc.get_check_table_map()[TransType::kFall] = table;
  check.get_timing_arc_list().push_back(arc);
  cell.get_check_arc_list().push_back(check);

  const auto cells = LibertyDataExporter::exportCells(library, TimingTimeUnit::kNS, TimingCapacitiveUnit::kPF);
  require(cells.size() == 1 && cells.front().arcs.size() == 2, "export lost delay/check arcs");
  const auto& delay = cells.front().arcs[0];
  require(delay.source_port == "A" && delay.sink_port == "Y" && delay.sense == 1, "delay arc identity changed");
  require(delay.rise_delay.axes == std::vector<std::vector<double>>{{100.0, 200.0, 300.0}, {1.0, 2.0}},
          "capacitance-first rectangular LUT axes were not scaled and swapped");
  require(delay.rise_delay.values == std::vector<double>{10.0, 40.0, 20.0, 50.0, 30.0, 60.0}, "rectangular LUT values were not transposed and scaled");
  require(delay.fall_delay.values.empty(), "missing transition table was synthesized");
  require(delay.fall_slew.axes.empty() && delay.fall_slew.values == std::vector<double>{70.0}, "scalar slew export changed");
  const auto& exported_check = cells.front().arcs[1];
  require(exported_check.source_port == "CK" && exported_check.sink_port == "D" && exported_check.trigger == -1, "check arc identity changed");
  require(exported_check.check_type == static_cast<int32_t>(TimingCheckType::kSetup), "check type changed");
  require(exported_check.fall_check.axes == std::vector<std::vector<double>>{{100.0, 200.0, 300.0}, {20.0, 40.0}},
          "constrained-transition LUT axes were not scaled and swapped");
  require(exported_check.fall_check.values == std::vector<double>{10.0, 40.0, 20.0, 50.0, 30.0, 60.0}, "check LUT values changed");
  require(exported_check.fall_delay.axes == exported_check.fall_check.axes && exported_check.fall_delay.values == exported_check.fall_check.values,
          "legacy check-as-delay export changed");
  auto& source_table = cell.get_cell_arc_list().front().get_timing_arc_list().front().get_delay_table_map().at(TransType::kRise);
  require(source_table.get_axis_list() == std::vector<std::vector<double>>{{2.0, 4.0}, {10.0, 20.0, 30.0}}, "export mutated source LUT axes");
  require(source_table.get_value_list() == std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0, 6.0}, "export mutated source LUT values");
}
void checkTransitionCapacitanceExport()
{
  LibertyLibrary library;
  auto& cell = library.get_cell_map()["sample"];
  cell.set_cell_name("sample");
  auto& pin = cell.get_port_map()["A"];
  pin.set_capacitance(0.002);
  pin.get_trans_capacitance_map()[AnalysisType::kMax] = {{TransType::kRise, 0.006}, {TransType::kFall, 0.007}};
  pin.get_trans_capacitance_map()[AnalysisType::kMin] = {{TransType::kRise, 0.0018}, {TransType::kFall, 0.0017}};
  const auto cells = LibertyDataExporter::exportCells(library, TimingTimeUnit::kNS, TimingCapacitiveUnit::kPF);
  const auto& port = cells.at(0).ports.at(0);
  require(std::vector<double>{port.capacitance_pf, port.rise_capacitance_pf, port.fall_capacitance_pf,
                              port.min_rise_capacitance_pf, port.min_fall_capacitance_pf}
              == std::vector<double>{0.002, 0.006, 0.007, 0.0018, 0.0017},
          "min pin capacitance was substituted with the max transition range");
}

void checkV2CellFamilies()
{
  LibertyLibrary library;
  const std::vector<std::string> names{"vendor_slow", "vendor_normal", "vendor_fast", "vendor_strong", "vendor_inverter"};
  double leakage = 1.0;
  for (const auto& name : names) {
    auto& cell = library.get_cell_map()[name];
    cell.set_cell_name(name);
    cell.set_leakage_power(leakage++);
    auto& input = cell.get_port_map()["A"];
    input.set_port_name("A");
    input.set_is_input(true);
    auto& output = cell.get_port_map()["Y"];
    output.set_port_name("Y");
    output.set_is_output(true);
    LogicExpressionTerm term;
    term.set_operation_type(LogicOperationType::kPort);
    term.set_port_name("A");
    output.get_function_expression().get_term_list().push_back(term);
    if (name == names.back()) {
      term.set_operation_type(LogicOperationType::kNot);
      output.get_function_expression().get_term_list().push_back(term);
    }
  }
  const auto unclassified = LibertyDataExporter::exportCells(library, TimingTimeUnit::kNS, TimingCapacitiveUnit::kPF);
  require(unclassified.front().main_id != unclassified.back().main_id, "missing catalog guessed a shared family");
  const auto catalog = std::filesystem::temp_directory_path() / ("ista-catalog-" + std::to_string(getpid()) + ".tsv");
  {
    std::ofstream file(catalog);
    file << "vendor_slow buffer slow 0.5\nvendor_normal buffer normal 0.5\nvendor_fast buffer fast 0.5\n"
            "vendor_strong buffer fast 24\nvendor_inverter buffer normal 1\n";
  }
  const auto cells = LibertyDataExporter::exportCells(library, TimingTimeUnit::kNS, TimingCapacitiveUnit::kPF, catalog.string());
  std::filesystem::remove(catalog);
  std::map<std::string, LibertyCellData> by_name;
  for (const auto& cell : cells)
    by_name.emplace(cell.name, cell);
  require(by_name.at(names[0]).main_id == by_name.at(names[3]).main_id, "explicit catalog did not group buffers");
  require(by_name.at(names[0]).main_id != by_name.at(names[4]).main_id, "catalog grouped cells with incompatible logic");
  require(by_name.at(names[0]).size_index < by_name.at(names[3]).size_index, "explicit drive rank was ignored");
  require(by_name.at(names[2]).vt_index == by_name.at(names[3]).vt_index, "explicit VT group was ignored");
}

void checkEndpointIdentity()
{
  for (bool reverse : {false, true}) {
    Database db;
    Config config;
    auto& instance = db.network().instances()["ff"];
    instance.set_cell_name("FF");
    auto& cell = db.liberty().get_cell_map()["FF"];
    cell.set_cell_name("FF");
    for (const auto* name : {"CK", "D"}) {
      auto& pin = db.network().pins()[std::string("ff:") + name];
      pin.set_instance_name("ff");
      pin.set_pin_name(name);
    }
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      TimingCheckArc check;
      check.set_clock_port("CK");
      check.set_data_port("D");
      check.set_clock_trans_type(edge);
      check.set_check_type(TimingCheckType::kSetup);
      for (int condition : {0, 1}) {
        TimingArc arc;
        arc.set_arc_idx(condition);
        arc.set_trigger_trans_type(edge);
        LogicExpression expression;
        LogicExpressionTerm term;
        term.set_operation_type(LogicOperationType::kPort);
        term.set_port_name(condition == 0 ? "EN" : "MODE");
        expression.get_term_list().push_back(term);
        arc.set_when_expression(expression);
        arc.get_check_table_map()[TransType::kRise].set_value_list({(edge == TransType::kRise ? 1.0 : 3.0) + condition});
        check.get_timing_arc_list().push_back(arc);
      }
      cell.get_check_arc_list().push_back(check);
      check.set_clock_port("ff:CK");
      check.set_data_port("ff:D");
      instance.get_check_arc_list().push_back(check);
    }
    if (reverse)
      std::reverse(cell.get_check_arc_list().begin(), cell.get_check_arc_list().end());
    const auto result = TimingDataExporter::exportData(db, config);
    require(result.timing_check_arcs.size() == 4, "endpoint checks lost");
    std::set<double> values;
    for (std::size_t i = 0; i < 4; ++i) {
      const auto& endpoint = result.timing_check_arcs[i];
      require(endpoint.library_arc_id >= 0, "check identity not mapped");
      const auto& arc = result.lib_cells.front().arcs.at(endpoint.library_arc_id);
      require(arc.trigger == endpoint.timing_type, "capture edge mapped to another LUT");
      const double value = arc.rise_check.values.at(0);
      require(endpoint.timing_type == 1 ? (value == 1000.0 || value == 2000.0) : (value == 3000.0 || value == 4000.0),
              "capture edge selected another edge's table");
      values.insert(value);
    }
    require(values == std::set<double>{1000, 2000, 3000, 4000}, "conditional check mapped to another LUT");
  }
}

void checkDiagnosticOrder()
{
  Database database;
  Config config;
  // Repeated malformed graph arcs share one message; undriven nets produce
  // distinct messages that must retain network traversal order.
  database.graph().arcs().resize(3);
  for (const auto* name : {"z", "a", "m"})
    database.network().nets()[name].set_net_name(name);
  const auto result = TimingDataExporter::exportData(database, config);
  require(result.diagnostics == std::vector<std::string>{
              "unsupported arc with missing pin identity", "net without a resolved driver: a",
              "net without a resolved driver: m", "net without a resolved driver: z",
              "no constrained start points exported", "no constrained end points exported", "no sequential clock pins exported"},
          "diagnostic deduplication lost first-occurrence order or distinct messages");
}
}  // namespace

int main()
{
  checkEndpointIdentity();
  checkUnreadyExport();
  checkRectangularTableExport();
  checkTransitionCapacitanceExport();
  checkV2CellFamilies();
  checkDiagnosticOrder();
}
