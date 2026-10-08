#include "LibertyExportAdapter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <regex>
#include <tuple>

#include "advance/TimingArc.hpp"
#include "advance/TimingArcSense.hpp"
#include "advance/TimingCell.hpp"
#include "advance/TimingCellArc.hpp"
#include "advance/TimingCellPort.hpp"
#include "advance/TimingCheckArc.hpp"
#include "advance/TimingLibrary.hpp"
#include "advance/TimingTable.hpp"
#include "advance/TimingTableVariableType.hpp"

#include <utility>
#include <vector>
#include "advance/TimingTimeUnit.hpp"
#include "advance/TransType.hpp"

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

TimingLutSnapshot copyTable(TimingTable& table, double time_scale, double cap_scale)
{
  TimingLutSnapshot result;
  result.axes = table.get_axis_list();
  result.values = table.get_value_list();
  const bool first_is_cap = table.get_variable_type1() == TimingTableVariableType::kOutputCapacitance;
  const bool second_is_cap = table.get_variable_type2() == TimingTableVariableType::kOutputCapacitance;
  if (!result.axes.empty()) {
    for (double& value : result.axes[0]) value *= first_is_cap ? cap_scale : time_scale;
  }
  if (result.axes.size() > 1) {
    for (double& value : result.axes[1]) value *= second_is_cap ? cap_scale : time_scale;
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
  for (double& value : result.values) value *= time_scale;
  return result;
}

void copyArcTables(TimingArc& arc, TimingLibArcSnapshot& result)
{
  const double time_scale = 1000.0 / arc.get_time_unit_scale();
  const double cap_scale = 1.0 / arc.get_cap_unit_scale();
  auto copy = [&](std::map<TransType, TimingTable>& tables, TimingLutSnapshot& rise, TimingLutSnapshot& fall) {
    const auto rise_it = tables.find(TransType::kRise);
    const auto fall_it = tables.find(TransType::kFall);
    if (rise_it != tables.end() && rise.values.empty()) rise = copyTable(rise_it->second, time_scale, cap_scale);
    if (fall_it != tables.end() && fall.values.empty()) fall = copyTable(fall_it->second, time_scale, cap_scale);
  };
  copy(arc.get_delay_table_map(), result.rise_delay, result.fall_delay);
  copy(arc.get_slew_table_map(), result.rise_slew, result.fall_slew);
  copy(arc.get_check_table_map(), result.rise_check, result.fall_check);
}

struct FamilyClass
{
  std::string key;
  std::string drive;
  std::string vt;
};

FamilyClass classifyFamily(const TimingLibCellSnapshot& cell)
{
  // Only classify the characterized ICS55 X<drive>H7<VT> masters. Other
  // libraries remain singleton families until their naming contract is known.
  static const std::regex master_name(R"(^(.+)X([0-9]+(?:P[0-9]+)?)(H7[HRL])$)");
  std::smatch match;
  if (cell.is_macro || !std::regex_match(cell.name, match, master_name)) {
    return {cell.name, cell.name, cell.name};
  }
  std::string signature = match[1].str();
  signature += cell.is_sequential ? "|sequential" : "|combinational";
  for (const auto& port : cell.ports) {
    signature += "|" + port.name + ":" + std::to_string(port.is_input) + std::to_string(port.is_output)
                 + std::to_string(port.is_clock);
  }
  for (const auto& arc : cell.arcs) {
    signature += "|" + arc.source_port + ":" + arc.sink_port + ":" + std::to_string(arc.sense)
                 + ":" + std::to_string(arc.trigger) + ":" + std::to_string(arc.check_type);
  }
  return {signature, match[2].str(), match[3].str()};
}

double leakageRank(const TimingLibCellSnapshot& cell)
{
  return std::isfinite(cell.leakage) && cell.leakage > 0.0 ? cell.leakage : std::numeric_limits<double>::infinity();
}

bool isBuffer(TimingCell& cell)
{
  if (cell.get_is_sequential() || cell.get_is_macro()) return false;
  TimingCellPort* input = nullptr;
  TimingCellPort* output = nullptr;
  for (auto& [name, port] : cell.get_port_map()) {
    if (port.get_is_clock()) return false;
    if (port.get_is_input()) {
      if (input != nullptr) return false;
      input = &port;
    }
    if (port.get_is_output()) {
      if (output != nullptr) return false;
      output = &port;
    }
  }
  if (input == nullptr || output == nullptr || input == output) return false;
  auto& function = output->get_function_expression();
  return function.evaluate_constant({{input->get_port_name(), false}}) == std::optional<bool>(false)
         && function.evaluate_constant({{input->get_port_name(), true}}) == std::optional<bool>(true);
}

std::map<std::string, int32_t> rankAxis(const std::vector<TimingLibCellSnapshot>& cells, bool drive)
{
  std::map<std::string, double> rank_by_name;
  for (const auto& cell : cells) {
    const auto classification = classifyFamily(cell);
    const std::string& key = drive ? classification.drive : classification.vt;
    auto [it, inserted] = rank_by_name.emplace(key, leakageRank(cell));
    if (!inserted) it->second = std::min(it->second, leakageRank(cell));
  }
  std::vector<std::pair<std::string, double>> ranked(rank_by_name.begin(), rank_by_name.end());
  std::sort(ranked.begin(), ranked.end(), [](const auto& lhs, const auto& rhs) {
    return std::tie(lhs.second, lhs.first) < std::tie(rhs.second, rhs.first);
  });
  std::map<std::string, int32_t> result;
  for (std::size_t index = 0; index < ranked.size(); ++index) {
    result.emplace(ranked[index].first, static_cast<int32_t>(index));
  }
  return result;
}

}  // namespace

std::vector<TimingLibCellSnapshot> LibertyExportAdapter::exportCells(TimingLibrary& library, TimingTimeUnit, TimingCapacitiveUnit)
{
  std::vector<TimingLibCellSnapshot> cells;
  cells.reserve(library.get_cell_map().size());
  for (auto& [cell_name, cell] : library.get_cell_map()) {
    TimingLibCellSnapshot exported;
    exported.name = cell_name;
    exported.area = cell.get_area();
    exported.leakage = cell.get_leakage_power();
    exported.is_sequential = cell.get_is_sequential();
    exported.is_macro = cell.get_is_macro();
    exported.is_buffer = isBuffer(cell);
    for (auto& [port_name, port] : cell.get_port_map()) {
      TimingLibPortSnapshot exported_port;
      exported_port.name = port_name;
      exported_port.capacitance_pf = port.get_capacitance();
      exported_port.rise_capacitance_pf = exported_port.capacitance_pf;
      exported_port.fall_capacitance_pf = exported_port.capacitance_pf;
      auto& caps = port.get_trans_capacitance_map();
      if (const auto it = caps.find(AnalysisType::kMax); it != caps.end()) {
        if (const auto rise = it->second.find(TransType::kRise); rise != it->second.end()) exported_port.rise_capacitance_pf = rise->second;
        if (const auto fall = it->second.find(TransType::kFall); fall != it->second.end()) exported_port.fall_capacitance_pf = fall->second;
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
        TimingLibArcSnapshot exported_arc;
        exported_arc.source_port = cell_arc.get_source_port();
        exported_arc.sink_port = cell_arc.get_sink_port();
        exported_arc.sense = senseCode(arc.get_sense());
        exported_arc.trigger = transCode(arc.get_trigger_trans_type());
        copyArcTables(arc, exported_arc);
        exported.arcs.push_back(std::move(exported_arc));
      }
    }
    for (TimingCheckArc& check_arc : cell.get_check_arc_list()) {
      if (check_arc.get_check_type() != TimingCheckType::kSetup || check_arc.get_timing_arc_list().empty()) continue;
      TimingLibArcSnapshot exported_arc;
      exported_arc.source_port = check_arc.get_clock_port();
      exported_arc.sink_port = check_arc.get_data_port();
      exported_arc.check_type = static_cast<int32_t>(check_arc.get_check_type());
      exported_arc.sense = senseCode(check_arc.get_timing_arc_list().front().get_sense());
      exported_arc.trigger = transCode(check_arc.get_clock_trans_type());
      for (TimingArc& arc : check_arc.get_timing_arc_list()) copyArcTables(arc, exported_arc);
      exported_arc.rise_delay = exported_arc.rise_check;
      exported_arc.fall_delay = exported_arc.fall_check;
      exported.arcs.push_back(std::move(exported_arc));
    }
    cells.push_back(std::move(exported));
  }
  std::map<std::string, std::vector<TimingLibCellSnapshot>> families;
  for (auto& cell : cells) families[classifyFamily(cell).key].push_back(std::move(cell));

  std::vector<TimingLibCellSnapshot> ordered;
  ordered.reserve(cells.size());
  for (auto& entry : families) {
    auto& family = entry.second;
    const auto drives = rankAxis(family, true);
    const auto vts = rankAxis(family, false);
    std::sort(family.begin(), family.end(), [](const auto& lhs, const auto& rhs) {
      return std::make_tuple(leakageRank(lhs), lhs.name) < std::make_tuple(leakageRank(rhs), rhs.name);
    });
    const auto main_id = static_cast<int32_t>(ordered.empty() ? 0 : ordered.back().main_id + 1);
    for (auto& cell : family) {
      const auto classification = classifyFamily(cell);
      cell.main_id = main_id;
      cell.size_index = drives.at(classification.drive) + 1;
      cell.vt_index = vts.at(classification.vt);
      ordered.push_back(std::move(cell));
    }
  }
  return ordered;
}

}  // namespace ista
