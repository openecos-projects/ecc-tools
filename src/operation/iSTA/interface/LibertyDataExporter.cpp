#include "LibertyDataExporter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "common/TransType.hpp"
#include "liberty/LibertyCell.hpp"
#include "liberty/LibertyLibrary.hpp"
#include "liberty/LibertyPort.hpp"
#include "liberty/LibertyUnits.hpp"
#include "liberty/LogicExpression.hpp"
#include "liberty/TimingArc.hpp"
#include "liberty/TimingArcSense.hpp"
#include "liberty/TimingCellArc.hpp"
#include "liberty/TimingCheckArc.hpp"
#include "liberty/TimingTable.hpp"
#include "liberty/TimingTableVariableType.hpp"

namespace ista {
namespace {

int32_t senseCode(TimingArcSense sense)
{
  switch (sense) {
    case TimingArcSense::kPositive:
      return 1;
    case TimingArcSense::kNegative:
      return -1;
    case TimingArcSense::kNonUnate:
      return 0;
    default:
      return 0;
  }
}

int32_t transCode(TransType trans)
{
  return trans == TransType::kRise ? 1 : trans == TransType::kFall ? -1 : 0;
}

LibertyTableData copyTable(TimingTable& table, double time_scale, double cap_scale)
{
  LibertyTableData result;
  result.axes = table.get_axis_list();
  result.values = table.get_value_list();
  const bool first_is_cap = table.get_variable_type1() == TimingTableVariableType::kOutputCapacitance;
  const bool second_is_cap = table.get_variable_type2() == TimingTableVariableType::kOutputCapacitance;
  if (!result.axes.empty()) {
    for (double& value : result.axes[0])
      value *= first_is_cap ? cap_scale : time_scale;
  }
  if (result.axes.size() == 1 && (first_is_cap || table.get_variable_type1() == TimingTableVariableType::kConstrainedTransition)) {
    // Keep a 1D load/data-slew table in the second canonical input slot.
    result.axes.insert(result.axes.begin(), std::vector<double>{});
  } else if (result.axes.size() > 1) {
    for (double& value : result.axes[1])
      value *= second_is_cap ? cap_scale : time_scale;
    if (first_is_cap || table.get_variable_type1() == TimingTableVariableType::kConstrainedTransition) {
      const std::size_t rows = result.axes[0].size();
      const std::size_t columns = result.axes[1].size();
      if (result.values.size() == rows * columns) {
        std::vector<double> transposed(result.values.size());
        for (std::size_t row = 0; row < rows; ++row) {
          for (std::size_t column = 0; column < columns; ++column) {
            transposed[column * rows + row] = result.values[row * columns + column];
          }
        }
        result.values = std::move(transposed);
        std::swap(result.axes[0], result.axes[1]);
      }
    }
  }
  for (double& value : result.values)
    value *= time_scale;
  return result;
}

void copyArcTables(TimingArc& arc, LibertyArcData& result)
{
  const double time_scale = 1000.0 / arc.get_time_unit_scale();
  const double cap_scale = 1.0 / arc.get_cap_unit_scale();
  auto copy = [&](std::map<TransType, TimingTable>& tables, LibertyTableData& rise, LibertyTableData& fall) {
    const auto rise_it = tables.find(TransType::kRise);
    const auto fall_it = tables.find(TransType::kFall);
    if (rise_it != tables.end() && rise.values.empty())
      rise = copyTable(rise_it->second, time_scale, cap_scale);
    if (fall_it != tables.end() && fall.values.empty())
      fall = copyTable(fall_it->second, time_scale, cap_scale);
  };
  copy(arc.get_delay_table_map(), result.rise_delay, result.fall_delay);
  copy(arc.get_slew_table_map(), result.rise_slew, result.fall_slew);
  copy(arc.get_check_table_map(), result.rise_check, result.fall_check);
}

// Flattened form of a Liberty logic expression, for signature comparison.
std::string expressionSignature(const LogicExpression& expression)
{
  std::string result;
  for (const auto& term : expression.get_term_list())
    result += ":" + std::to_string(static_cast<int>(term.get_operation_type())) + ":" + term.get_port_name();
  return result;
}

// Logic signature shared by interchangeable masters: output functions and
// their tri-state enables, pin directions and arc kinds. Macros, sequential
// cells and cells without an output function stay singletons; this interface
// cannot prove equivalence of state or reset behavior.
std::optional<std::string> logicSignature(const LibertyCellData& cell, const LibertyLibrary& library)
{
  const auto liberty = library.get_cell_map().find(cell.name);
  if (cell.is_macro || cell.is_sequential || liberty == library.get_cell_map().end())
    return std::nullopt;
  std::string signature;
  for (const auto& [name, port] : liberty->second.get_port_map()) {
    if (!port.get_is_output())
      continue;
    if (port.get_function_expression().get_is_empty())
      return std::nullopt;
    signature += "|function:" + name + expressionSignature(port.get_function_expression());
    // Output enable is part of what the cell computes: masters agreeing on the
    // data function but disabling on opposite conditions are not substitutes,
    // and a plain driver is not a substitute for a tri-state one.
    const auto& enable = port.get_three_state_expression();
    if (!enable.get_is_empty())
      signature += "|three_state:" + name + expressionSignature(enable);
  }
  if (signature.empty())
    return std::nullopt;
  for (const auto& port : cell.ports) {
    signature += "|" + port.name + ":" + std::to_string(port.is_input) + std::to_string(port.is_output) + std::to_string(port.is_clock);
  }
  for (const auto& arc : cell.arcs) {
    signature += "|" + arc.source_port + ":" + arc.sink_port + ":" + std::to_string(arc.sense) + ":" + std::to_string(arc.trigger) + ":"
                 + std::to_string(arc.check_type);
  }
  return signature;
}

// Drive resistance (ps/pF) of the weakest delay arc: the delay slope over the
// load axis at the median input slew. Cells without a delay table rank last.
double driveResistance(const LibertyCellData& cell)
{
  double result = -1.0;
  for (const auto& arc : cell.arcs) {
    if (!arc.rise_check.values.empty() || !arc.fall_check.values.empty())
      continue;
    for (const auto* table : {&arc.rise_delay, &arc.fall_delay}) {
      if (table->axes.size() != 2)
        continue;
      const auto& loads = table->axes[1];
      const std::size_t columns = loads.size();
      const std::size_t rows = table->axes[0].empty() ? 1 : table->axes[0].size();
      if (columns < 2 || table->values.size() != rows * columns || !(loads.back() > loads.front()))
        continue;
      const std::size_t row = rows / 2 * columns;
      result = std::max(result, (table->values[row + columns - 1] - table->values[row]) / (loads.back() - loads.front()));
    }
  }
  return result < 0.0 ? std::numeric_limits<double>::infinity() : result;
}

double leakageRank(const LibertyCellData& cell)
{
  return std::isfinite(cell.leakage) && cell.leakage > 0.0 ? cell.leakage : std::numeric_limits<double>::infinity();
}

bool isBuffer(LibertyCell& cell)
{
  if (cell.get_is_sequential() || cell.get_is_macro())
    return false;
  LibertyPort* input = nullptr;
  LibertyPort* output = nullptr;
  for (auto& [name, port] : cell.get_port_map()) {
    if (port.get_is_clock())
      return false;
    if (port.get_is_input()) {
      if (input != nullptr)
        return false;
      input = &port;
    }
    if (port.get_is_output()) {
      if (output != nullptr)
        return false;
      output = &port;
    }
  }
  if (input == nullptr || output == nullptr || input == output)
    return false;
  auto& function = output->get_function_expression();
  return function.evaluate_constant({{input->get_port_name(), false}}) == std::optional<bool>(false)
         && function.evaluate_constant({{input->get_port_name(), true}}) == std::optional<bool>(true);
}

// Dense ranks of keys ordered by value, ties broken by key.
template <typename Key>
std::map<Key, int32_t> rank(const std::map<Key, double>& values)
{
  std::vector<std::pair<double, Key>> ordered;
  for (const auto& [key, value] : values)
    ordered.emplace_back(value, key);
  std::sort(ordered.begin(), ordered.end());
  std::map<Key, int32_t> result;
  for (std::size_t index = 0; index < ordered.size(); ++index)
    result.emplace(ordered[index].second, static_cast<int32_t>(index));
  return result;
}

// Assigns size and VT coordinates within one logic family from Liberty data.
// The VT axis is the source library, ranked by its lowest leakage in the
// family. VT variants of one drive share their footprint, so the drive axis is
// the area plus the strength rank among same-area cells of one library,
// ranked by the strongest variant's drive resistance.
void assignCoordinates(std::vector<LibertyCellData>& family, const LibertyLibrary& library)
{
  using Drive = std::pair<double, std::size_t>;
  std::vector<std::string> vts(family.size());
  std::vector<double> resistances(family.size());
  std::map<std::pair<std::string, double>, std::vector<std::size_t>> footprints;
  for (std::size_t index = 0; index < family.size(); ++index) {
    vts[index] = library.get_cell_map().at(family[index].name).get_library_name();
    resistances[index] = driveResistance(family[index]);
    footprints[{vts[index], family[index].area}].push_back(index);
  }
  std::vector<Drive> drives(family.size());
  for (auto& [footprint, members] : footprints) {
    std::sort(members.begin(), members.end(), [&](std::size_t lhs, std::size_t rhs) {
      return std::make_tuple(-resistances[lhs], family[lhs].name) < std::make_tuple(-resistances[rhs], family[rhs].name);
    });
    for (std::size_t position = 0; position < members.size(); ++position)
      drives[members[position]] = {footprint.second, position};
  }
  std::map<Drive, double> drive_values;
  std::map<std::string, double> vt_values;
  for (std::size_t index = 0; index < family.size(); ++index) {
    auto [drive, inserted] = drive_values.emplace(drives[index], -resistances[index]);
    if (!inserted)
      drive->second = std::max(drive->second, -resistances[index]);
    auto [vt, vt_inserted] = vt_values.emplace(vts[index], leakageRank(family[index]));
    if (!vt_inserted)
      vt->second = std::min(vt->second, leakageRank(family[index]));
  }
  const auto drive_ranks = rank(drive_values);
  const auto vt_ranks = rank(vt_values);
  for (std::size_t index = 0; index < family.size(); ++index) {
    family[index].size_index = drive_ranks.at(drives[index]) + 1;
    family[index].vt_index = vt_ranks.at(vts[index]);
  }
}

}  // namespace

std::vector<LibertyCellData> LibertyDataExporter::exportCells(LibertyLibrary& library, TimingTimeUnit, TimingCapacitiveUnit)
{
  std::vector<LibertyCellData> cells;
  cells.reserve(library.get_cell_map().size());
  for (auto& [cell_name, cell] : library.get_cell_map()) {
    LibertyCellData exported;
    exported.name = cell_name;
    exported.area = cell.get_area();
    exported.leakage = cell.get_leakage_power();
    exported.is_sequential = cell.get_is_sequential();
    exported.is_macro = cell.get_is_macro();
    exported.is_buffer = isBuffer(cell);
    for (auto& [port_name, port] : cell.get_port_map()) {
      LibertyPortData exported_port;
      exported_port.name = port_name;
      exported_port.capacitance_pf = port.get_capacitance();
      exported_port.rise_capacitance_pf = exported_port.capacitance_pf;
      exported_port.fall_capacitance_pf = exported_port.capacitance_pf;
      exported_port.min_rise_capacitance_pf = exported_port.capacitance_pf;
      exported_port.min_fall_capacitance_pf = exported_port.capacitance_pf;
      auto& caps = port.get_trans_capacitance_map();
      if (const auto it = caps.find(AnalysisType::kMax); it != caps.end()) {
        if (const auto rise = it->second.find(TransType::kRise); rise != it->second.end())
          exported_port.rise_capacitance_pf = rise->second;
        if (const auto fall = it->second.find(TransType::kFall); fall != it->second.end())
          exported_port.fall_capacitance_pf = fall->second;
      }
      if (const auto it = caps.find(AnalysisType::kMin); it != caps.end()) {
        if (const auto rise = it->second.find(TransType::kRise); rise != it->second.end())
          exported_port.min_rise_capacitance_pf = rise->second;
        if (const auto fall = it->second.find(TransType::kFall); fall != it->second.end())
          exported_port.min_fall_capacitance_pf = fall->second;
      }
      exported_port.max_capacitance_pf = port.get_max_capacitance().value_or(0.0);
      exported_port.max_slew_ps = port.get_max_transition().value_or(0.0) * 1000.0;
      exported_port.is_input = port.get_is_input();
      exported_port.is_output = port.get_is_output();
      exported_port.is_clock = port.get_is_clock();
      exported.ports.push_back(std::move(exported_port));
    }
    for (TimingCellArc& cell_arc : cell.get_cell_arc_list()) {
      for (TimingArc& arc : cell_arc.get_timing_arc_list()) {
        LibertyArcData exported_arc;
        exported_arc.source_port = cell_arc.get_source_port();
        exported_arc.sink_port = cell_arc.get_sink_port();
        exported_arc.sense = senseCode(arc.get_sense());
        exported_arc.trigger = transCode(arc.get_trigger_trans_type());
        copyArcTables(arc, exported_arc);
        exported.arcs.push_back(std::move(exported_arc));
      }
    }
    for (TimingCheckArc& check_arc : cell.get_check_arc_list()) {
      for (TimingArc& arc : check_arc.get_timing_arc_list()) {
        LibertyArcData exported_arc;
        exported_arc.source_port = check_arc.get_clock_port();
        exported_arc.sink_port = check_arc.get_data_port();
        exported_arc.check_type = static_cast<int32_t>(check_arc.get_check_type());
        exported_arc.sense = senseCode(arc.get_sense());
        exported_arc.trigger = transCode(check_arc.get_clock_trans_type());
        copyArcTables(arc, exported_arc);
        exported_arc.rise_delay = exported_arc.rise_check;
        exported_arc.fall_delay = exported_arc.fall_check;
        exported.arcs.push_back(std::move(exported_arc));
      }
    }
    cells.push_back(std::move(exported));
  }
  std::map<std::string, std::vector<LibertyCellData>> families;
  for (auto& cell : cells) {
    const auto signature = logicSignature(cell, library);
    families[signature ? "logic" + *signature : "cell:" + cell.name].push_back(std::move(cell));
  }

  std::vector<LibertyCellData> ordered;
  ordered.reserve(cells.size());
  for (auto& entry : families) {
    auto& family = entry.second;
    assignCoordinates(family, library);
    std::sort(family.begin(), family.end(),
              [](const auto& lhs, const auto& rhs) { return std::make_tuple(leakageRank(lhs), lhs.name) < std::make_tuple(leakageRank(rhs), rhs.name); });
    const auto main_id = static_cast<int32_t>(ordered.empty() ? 0 : ordered.back().main_id + 1);
    for (auto& cell : family) {
      cell.main_id = main_id;
      ordered.push_back(std::move(cell));
    }
  }
  return ordered;
}

}  // namespace ista
