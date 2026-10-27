// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>

#include "LibertyDataExporter.hpp"
#include "liberty/LibertyLibrary.hpp"

int main()
{
  using namespace ista;
  LibertyLibrary library;
  auto& cell = library.get_cell_map()["TEST"];
  cell.set_cell_name("TEST");
  TimingTable table;
  table.set_variable_type1(TimingTableVariableType::kConstrainedTransition);
  table.set_axis_list({{0, 1, 2}});
  table.set_value_list({0, 1, 3});
  TimingArc arc;
  arc.set_time_unit_scale(1);
  arc.set_cap_unit_scale(1);
  arc.get_check_table_map()[TransType::kRise] = table;
  TimingCheckArc check;
  check.set_clock_port("CK");
  check.set_data_port("D");
  check.set_check_type(TimingCheckType::kSetup);
  check.set_timing_arc_list({arc});
  cell.get_check_arc_list().push_back(check);
  const auto cells = LibertyDataExporter::exportCells(library, TimingTimeUnit::kNS, TimingCapacitiveUnit::kPF);
  const auto& exported = cells.at(0).arcs.at(0).rise_check;
  if (exported.axes != std::vector<std::vector<double>>{{}, {0, 1000, 2000}} || exported.values != std::vector<double>{0, 1000, 3000}) {
    throw std::runtime_error("1D data slew LUT was exported as clock slew");
  }
}
