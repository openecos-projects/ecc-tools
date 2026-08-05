// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file LibWriter.cc
 * @brief Liberty writer implementations split out from Lib.cc to keep the
 *        parser/model code reviewable.
 */

#include "Lib.hh"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "json/json.hpp"

namespace idb {

namespace {

bool isClockTreePathArc(LibArc* lib_arc)
{
  auto timing_type = lib_arc->get_timing_type();
  return timing_type == LibArc::TimingType::kMinClockTree
         || timing_type == LibArc::TimingType::kMaxClockTree;
}

auto classifyCellArcBySnkPort(LibCell* lib_cell) -> std::map<std::string, std::vector<LibArc*>>
{
  std::map<std::string, std::vector<LibArc*>> snkport2arcset;
  for (auto& cell_arc_set : lib_cell->get_cell_arcs()) {
    for (auto& cell_arc : cell_arc_set->get_arcs()) {
      const char* snk_port_name = cell_arc->get_snk_port();
      snkport2arcset[snk_port_name].push_back(cell_arc.get());
    }
  }

  return snkport2arcset;
}

struct BusRange
{
  int min_index = 0;
  int max_index = 0;

  bool operator<(const BusRange& rhs) const
  {
    return std::tie(max_index, min_index) <
           std::tie(rhs.max_index, rhs.min_index);
  }
};

struct BusTypeDefinition
{
  std::string type_name;
  int min_index = 0;
  int max_index = 0;
};

using BusTypeDefinitions = std::map<std::string, BusTypeDefinition>;

std::pair<std::string, std::optional<int>> splitPortName(const char* port_name)
{
  std::string name(port_name);
  if (!name.ends_with("]")) {
    return {name, std::nullopt};
  }

  size_t left_bracket_idx = name.find('[');
  size_t right_bracket_idx = name.find(']', left_bracket_idx);
  if (left_bracket_idx == std::string::npos || right_bracket_idx == std::string::npos) {
    return {name, std::nullopt};
  }

  int index = std::atoi(name.substr(left_bracket_idx + 1, right_bracket_idx - left_bracket_idx - 1).c_str());
  return {name.substr(0, left_bracket_idx), index};
}

std::string makeBusRangeKey(const std::string& bus_name, int min_index,
                            int max_index)
{
  return bus_name + "[" + std::to_string(max_index) + ":" + std::to_string(min_index) + "]";
}

BusTypeDefinitions collectBusTypeDefinitions(LibLibrary* lib)
{
  std::map<std::string, std::set<BusRange>> bus_ranges_by_name;

  for (const auto& cell : lib->get_cells()) {
    std::map<std::string, BusRange> cell_bus_ranges;
    for (const auto& cell_port : cell->get_cell_ports()) {
      auto [bus_name, index] = splitPortName(cell_port->get_port_name());
      if (!index) {
        continue;
      }

      auto [iter, inserted] =
          cell_bus_ranges.emplace(bus_name, BusRange{*index, *index});
      if (!inserted) {
        iter->second.min_index = std::min(iter->second.min_index, *index);
        iter->second.max_index = std::max(iter->second.max_index, *index);
      }
    }

    for (const auto& [bus_name, range] : cell_bus_ranges) {
      bus_ranges_by_name[bus_name].insert(range);
    }
  }

  BusTypeDefinitions bus_type_definitions;
  for (const auto& [bus_name, ranges] : bus_ranges_by_name) {
    const bool has_multiple_ranges = ranges.size() > 1;
    for (const auto& range : ranges) {
      auto type_name =
          has_multiple_ranges
              ? bus_name + "__" + std::to_string(range.max_index) + "_" + std::to_string(range.min_index)
              : bus_name;
      bus_type_definitions.emplace(
          makeBusRangeKey(bus_name, range.min_index, range.max_index),
          BusTypeDefinition{std::move(type_name), range.min_index,
                            range.max_index});
    }
  }

  return bus_type_definitions;
}

std::string findBusTypeName(const BusTypeDefinitions& bus_type_definitions,
                            const std::string& bus_name,
                            const std::vector<std::pair<int, LibPort*>>& bit_ports)
{
  if (bit_ports.empty()) {
    return bus_name;
  }

  auto [min_iter, max_iter] =
      std::minmax_element(bit_ports.begin(), bit_ports.end(),
                          [](const auto& lhs, const auto& rhs) {
                            return lhs.first < rhs.first;
                          });
  const auto bus_range_key =
      makeBusRangeKey(bus_name, min_iter->first, max_iter->first);
  if (auto bus_type_iter = bus_type_definitions.find(bus_range_key);
      bus_type_iter != bus_type_definitions.end()) {
    return bus_type_iter->second.type_name;
  }

  return bus_name;
}

const char* getDelayModelCapUnitName(LibLibrary* lib)
{
  switch (lib->get_cap_unit()) {
    case CapacitiveUnit::kPF:
      return "pF";
    case CapacitiveUnit::kF:
      return "F";
    case CapacitiveUnit::kFF:
    default:
      return "fF";
  }
}

const char* getDelayModelTimeUnitName(LibLibrary* lib)
{
  switch (lib->get_time_unit()) {
    case TimeUnit::kFS:
      return "fs";
    case TimeUnit::kNS:
      return "ns";
    case TimeUnit::kPS:
    default:
      return "ps";
  }
}

const char* getDelayModelResistanceUnitName(LibLibrary* lib)
{
  switch (lib->get_resistance_unit()) {
    case ResistanceUnit::kOHM:
      return "ohm";
    case ResistanceUnit::kkOHM:
    default:
      return "kohm";
  }
}

double convertInternalTimeValueToExportUnit(LibLibrary* lib, double value_in_ns)
{
  switch (lib->get_time_unit()) {
    case TimeUnit::kFS:
      return value_in_ns * 1e6;
    case TimeUnit::kPS:
      return value_in_ns * 1e3;
    case TimeUnit::kNS:
    default:
      return value_in_ns;
  }
}

double convertInternalCapValueToExportUnit(LibLibrary* lib, double value_in_pf)
{
  switch (lib->get_cap_unit()) {
    case CapacitiveUnit::kF:
      return PF_TO_F(value_in_pf);
    case CapacitiveUnit::kFF:
      return PF_TO_FF(value_in_pf);
    case CapacitiveUnit::kPF:
    default:
      return value_in_pf;
  }
}

bool isTimeTableType(LibTable::TableType table_type)
{
  switch (table_type) {
    case LibTable::TableType::kCellRise:
    case LibTable::TableType::kCellFall:
    case LibTable::TableType::kRiseTransition:
    case LibTable::TableType::kFallTransition:
    case LibTable::TableType::kRiseConstrain:
    case LibTable::TableType::kFallConstrain:
    case LibTable::TableType::kCellRiseSigma:
    case LibTable::TableType::kCellFallSigma:
    case LibTable::TableType::kRiseTransitionSigma:
    case LibTable::TableType::kFallTransitionSigma:
      return true;
    case LibTable::TableType::kRiseCurrent:
    case LibTable::TableType::kFallCurrent:
    case LibTable::TableType::kRisePower:
    case LibTable::TableType::kFallPower:
    default:
      return false;
  }
}

bool isTimeTemplateVariable(LibLutTableTemplate::Variable variable)
{
  switch (variable) {
    case LibLutTableTemplate::Variable::INPUT_NET_TRANSITION:
    case LibLutTableTemplate::Variable::CONSTRAINED_PIN_TRANSITION:
    case LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION:
    case LibLutTableTemplate::Variable::INPUT_TRANSITION_TIME:
    case LibLutTableTemplate::Variable::TIME:
      return true;
    case LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE:
    case LibLutTableTemplate::Variable::INPUT_VOLTAGE:
    case LibLutTableTemplate::Variable::OUTPUT_VOLTAGE:
    case LibLutTableTemplate::Variable::INPUT_NOISE_HEIGHT:
    case LibLutTableTemplate::Variable::INPUT_NOISE_WIDTH:
    case LibLutTableTemplate::Variable::NORMALIZED_VOLTAGE:
    default:
      return false;
  }
}

std::optional<LibLutTableTemplate::Variable> getTemplateVariable(
    LibLutTableTemplate* lut_table_template, int index_order)
{
  if (!lut_table_template) {
    return std::nullopt;
  }

  switch (index_order) {
    case 0:
      return lut_table_template->get_template_variable1();
    case 1:
      return lut_table_template->get_template_variable2();
    case 2:
      return lut_table_template->get_template_variable3();
    case 3:
      return lut_table_template->get_template_variable4();
    default:
      return std::nullopt;
  }
}

double convertAxisValueForExport(LibLibrary* lib, LibAxis* lib_axis,
                                 LibLutTableTemplate* lut_table_template,
                                 int index_order, double raw_value)
{
  if (lib_axis && lib_axis->get_value_scale() == LibValueScale::kLibrary) {
    return raw_value;
  }

  auto variable = getTemplateVariable(lut_table_template, index_order);
  if (variable && isTimeTemplateVariable(*variable)) {
    return convertInternalTimeValueToExportUnit(lib, raw_value);
  }

  return raw_value;
}

double convertTableValueForExport(LibLibrary* lib, LibTable* table,
                                  double raw_value)
{
  if (table && table->get_value_scale() == LibValueScale::kLibrary) {
    return raw_value;
  }

  if (isTimeTableType(table->get_table_type())) {
    return convertInternalTimeValueToExportUnit(lib, raw_value);
  }

  return raw_value;
}

const char* getPortDirection(LibPort* cell_port)
{
  switch (cell_port->get_port_type()) {
    case LibPort::LibertyPortType::kInput:
      return "input";
    case LibPort::LibertyPortType::kOutput:
      return "output";
    case LibPort::LibertyPortType::kInOut:
      return "inout";
    case LibPort::LibertyPortType::kDefault:
    default:
      return "input";
  }
}

const char* getTemplateVariableName(LibLutTableTemplate::Variable variable)
{
  switch (variable) {
    case LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE:
      return "total_output_net_capacitance";
    case LibLutTableTemplate::Variable::INPUT_NET_TRANSITION:
      return "input_net_transition";
    case LibLutTableTemplate::Variable::CONSTRAINED_PIN_TRANSITION:
      return "constrained_pin_transition";
    case LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION:
      return "related_pin_transition";
    case LibLutTableTemplate::Variable::INPUT_TRANSITION_TIME:
      return "input_transition_time";
    case LibLutTableTemplate::Variable::TIME:
      return "time";
    case LibLutTableTemplate::Variable::INPUT_VOLTAGE:
      return "input_voltage";
    case LibLutTableTemplate::Variable::OUTPUT_VOLTAGE:
      return "output_voltage";
    case LibLutTableTemplate::Variable::INPUT_NOISE_HEIGHT:
      return "input_noise_height";
    case LibLutTableTemplate::Variable::INPUT_NOISE_WIDTH:
      return "input_noise_width";
    case LibLutTableTemplate::Variable::NORMALIZED_VOLTAGE:
      return "normalized_voltage";
  }
  return "";
}

void writeAxis(FILE* stream, LibLibrary* lib, LibAxis* lib_axis,
               LibLutTableTemplate* lut_table_template, int index_order)
{
  auto& axis_values = lib_axis->get_axis_values();
  fprintf(stream, "                              index_%d (\"", index_order + 1);
  for (int i = 0; i < axis_values.size(); ++i) {
    auto axis_float_value
        = dynamic_cast<LibFloatValue*>(axis_values[i].get())->getFloatValue();
    fprintf(stream, "%.8f",
            convertAxisValueForExport(lib, lib_axis, lut_table_template,
                                      index_order,
                                      axis_float_value));
    if (i < axis_values.size() - 1) {
      fprintf(stream, ",");
    }
  }
  fprintf(stream, "\");\n");
}

void writeTableValues(FILE* stream, LibLibrary* lib, LibTable* lib_table,
                      int columns)
{
  auto& lib_table_values = lib_table->get_table_values();
  fprintf(stream, "                                values (\"");
  for (int i = 0; i < lib_table_values.size(); ++i) {
    auto lib_table_float_value
        = dynamic_cast<LibFloatValue*>(lib_table_values[i].get())->getFloatValue();
    fprintf(stream, "%.8f",
            convertTableValueForExport(lib, lib_table, lib_table_float_value));
    if ((i + 1) % columns == 0 && i < lib_table_values.size() - 1) {
      fprintf(stream, "\", \\\n                                        \"");
    } else if (i < lib_table_values.size() - 1) {
      fprintf(stream, ",");
    }
  }
  fprintf(stream, "\");\n");
}

void writeDelayTable(FILE* stream, LibLibrary* lib, const char* table_name,
                     LibTable* table)
{
  auto* lut_table_template = table->get_table_template();
  if (lut_table_template) {
    std::string template_name = lut_table_template->get_template_name();
    fprintf(stream, "                        %s(%s) {\n", table_name,
            template_name.c_str());
    auto& axes = table->get_axes();
    int columns = axes.empty()
                      ? 1
                      : (axes.size() > 1 ? axes[1].get()->get_axis_values().size()
                                         : axes[0].get()->get_axis_values().size());
    for (int i = 0; i < axes.size(); i++) {
      writeAxis(stream, lib, axes[i].get(), lut_table_template, i);
    }
    writeTableValues(stream, lib, table, columns);
    fprintf(stream, "                       }\n");
    return;
  }

  auto& table_values = table->get_table_values();
  if (table_values.size() > 1) {
    ECCLOG.error(ecc::Loc::current(), "Liberty scalar table has more than one value.");
  }
  auto float_value
      = dynamic_cast<LibFloatValue*>(table_values.front().get())->getFloatValue();
  fprintf(stream, "                        %s(scalar) {\n", table_name);
  fprintf(stream, "                                values (\"%.8f\");\n",
          convertTableValueForExport(lib, table, float_value));
  fprintf(stream, "                       }\n");
}

void writeCheckTable(FILE* stream, LibLibrary* lib, const char* table_name,
                     LibTable* table)
{
  auto* lut_table_template = table->get_table_template();
  if (lut_table_template) {
    std::string template_name = lut_table_template->get_template_name();
    fprintf(stream, "                       %s(%s) {\n", table_name,
            template_name.c_str());
    auto& axes = table->get_axes();
    int columns = axes.empty()
                      ? 1
                      : (axes.size() > 1 ? axes[1].get()->get_axis_values().size()
                                         : axes[0].get()->get_axis_values().size());
    for (int i = 0; i < axes.size(); i++) {
      writeAxis(stream, lib, axes[i].get(), lut_table_template, i);
    }
    writeTableValues(stream, lib, table, columns);
    fprintf(stream, "                       }\n");
    return;
  }

  auto& table_values = table->get_table_values();
  if (table_values.size() > 1) {
    ECCLOG.error(ecc::Loc::current(), "Liberty scalar table has more than one value.");
  }
  auto float_value
      = dynamic_cast<LibFloatValue*>(table_values.front().get())->getFloatValue();
  fprintf(stream, "                       %s(scalar) {\n", table_name);
  fprintf(stream, "                                values (\"%.8f\");\n",
          convertTableValueForExport(lib, table, float_value));
  fprintf(stream, "                       }\n");
}

void writeDelayTableModel(FILE* stream, LibArc* lib_arc)
{
  auto* lib = lib_arc->get_owner_cell()->get_owner_lib();
  static const std::map<LibArc::TimingSense, const char*> timing_sense_map = {
      {LibArc::TimingSense::kPositiveUnate, "positive_unate"},
      {LibArc::TimingSense::kNegativeUnate, "negative_unate"},
      {LibArc::TimingSense::kNonUnate, "non_unate"}};
  static const std::map<LibArc::TimingType, const char*> timing_type_map = {
      {LibArc::TimingType::kComb, "combinational"},
      {LibArc::TimingType::kCombRise, "combinational_rise"},
      {LibArc::TimingType::kCombFall, "combinational_fall"},
      {LibArc::TimingType::kRisingEdge, "rising_edge"},
      {LibArc::TimingType::kFallingEdge, "falling_edge"},
      {LibArc::TimingType::kMaxClockTree, "max_clock_tree_path"},
      {LibArc::TimingType::kMinClockTree, "min_clock_tree_path"}};

  fprintf(stream, "                timing () {\n");
  if (const char* related_pin = lib_arc->get_src_port();
      related_pin && *related_pin) {
    fprintf(stream, "                       related_pin        : \"%s\";\n",
            related_pin);
  }
  if (const auto& sdf_cond = lib_arc->get_sdf_cond(); !sdf_cond.empty()) {
    fprintf(stream, "                       sdf_cond           : \"%s\";\n",
            sdf_cond.c_str());
  }
  if (const auto& when = lib_arc->get_when(); !when.empty()) {
    fprintf(stream, "                       when               : \"%s\";\n",
            when.c_str());
  }
  fprintf(stream, "                       timing_sense        : %s;\n",
          timing_sense_map.at(lib_arc->get_timing_sense()));
  if (auto timing_type = timing_type_map.find(lib_arc->get_timing_type());
      timing_type != timing_type_map.end()) {
    fprintf(stream, "                       timing_type        : %s;\n",
            timing_type->second);
  }

  auto* delay_model = dynamic_cast<LibDelayTableModel*>(lib_arc->get_table_model());
  if (auto* cell_fall_table
      = delay_model->getTable(int(LibTable::TableType::kCellFall))) {
    writeDelayTable(stream, lib, "cell_fall", cell_fall_table);
  }
  if (auto* cell_rise_table
      = delay_model->getTable(int(LibTable::TableType::kCellRise))) {
    writeDelayTable(stream, lib, "cell_rise", cell_rise_table);
  }
  if (auto* fall_transition_table
      = delay_model->getTable(int(LibTable::TableType::kFallTransition))) {
    writeDelayTable(stream, lib, "fall_transition", fall_transition_table);
  }
  if (auto* rise_transition_table
      = delay_model->getTable(int(LibTable::TableType::kRiseTransition))) {
    writeDelayTable(stream, lib, "rise_transition", rise_transition_table);
  }

  fprintf(stream, "                }\n");
}

void writeCheckTableModel(FILE* stream, LibArc* lib_arc)
{
  auto* lib = lib_arc->get_owner_cell()->get_owner_lib();
  static const std::map<LibArc::TimingType, const char*> timing_type_map = {
      {LibArc::TimingType::kSetupRising, "setup_rising"},
      {LibArc::TimingType::kHoldRising, "hold_rising"},
      {LibArc::TimingType::kSetupFalling, "setup_falling"},
      {LibArc::TimingType::kHoldFalling, "hold_falling"},
      {LibArc::TimingType::kRecoveryRising, "recovery_rising"},
      {LibArc::TimingType::kRecoveryFalling, "recovery_falling"},
      {LibArc::TimingType::kRemovalRising, "removal_rising"},
      {LibArc::TimingType::kRemovalFalling, "removal_falling"},
      {LibArc::TimingType::kMinPulseWidth, "min_pulse_width"},
      {LibArc::TimingType::kMinimunPeriod, "minimum_period"}};

  fprintf(stream, "                timing () {\n");
  if (const char* related_pin = lib_arc->get_src_port();
      related_pin && *related_pin) {
    fprintf(stream, "                       related_pin        : \"%s\";\n",
            related_pin);
  }
  if (const auto& sdf_cond = lib_arc->get_sdf_cond(); !sdf_cond.empty()) {
    fprintf(stream, "                       sdf_cond           : \"%s\";\n",
            sdf_cond.c_str());
  }
  if (const auto& when = lib_arc->get_when(); !when.empty()) {
    fprintf(stream, "                       when               : \"%s\";\n",
            when.c_str());
  }
  fprintf(stream, "                       timing_type        : %s;\n",
          timing_type_map.at(lib_arc->get_timing_type()));

  auto* check_model = dynamic_cast<LibCheckTableModel*>(lib_arc->get_table_model());
  if (auto* fall_constraint_table
      = check_model->getTable(int(LibTable::TableType::kFallConstrain) - 4)) {
    writeCheckTable(stream, lib, "fall_constraint", fall_constraint_table);
  }
  if (auto* rise_constraint_table
      = check_model->getTable(int(LibTable::TableType::kRiseConstrain) - 4)) {
    writeCheckTable(stream, lib, "rise_constraint", rise_constraint_table);
  }

  fprintf(stream, "                }\n");
}

bool isExportedCheckTimingType(LibArc::TimingType timing_type)
{
  switch (timing_type) {
    case LibArc::TimingType::kSetupRising:
    case LibArc::TimingType::kHoldRising:
    case LibArc::TimingType::kSetupFalling:
    case LibArc::TimingType::kHoldFalling:
    case LibArc::TimingType::kRecoveryRising:
    case LibArc::TimingType::kRecoveryFalling:
    case LibArc::TimingType::kRemovalRising:
    case LibArc::TimingType::kRemovalFalling:
    case LibArc::TimingType::kMinPulseWidth:
    case LibArc::TimingType::kMinimunPeriod:
      return true;
    default:
      return false;
  }
}

void writeBusTypeDefinitions(FILE* stream,
                             const BusTypeDefinitions& bus_type_definitions)
{
  for (const auto& [range_key, bus_type] : bus_type_definitions) {
    (void) range_key;
    fprintf(stream, "  type (\"%s\") {\n", bus_type.type_name.c_str());
    fprintf(stream, "    base_type : array;\n");
    fprintf(stream, "    data_type : bit;\n");
    fprintf(stream, "    bit_width : %d;\n",
            bus_type.max_index - bus_type.min_index + 1);
    fprintf(stream, "    bit_from : %d;\n", bus_type.max_index);
    fprintf(stream, "    bit_to : %d;\n", bus_type.min_index);
    fprintf(stream, "  }\n");
  }

  if (!bus_type_definitions.empty()) {
    fprintf(stream, "\n");
  }
}

void writeLutTemplateDefinitions(FILE* stream, LibLibrary* lib)
{
  for (const auto& lut_template : lib->get_lut_templates()) {
    fprintf(stream, "  lu_table_template(%s) {\n",
            lut_template->get_template_name());
    if (auto variable1 = lut_template->get_template_variable1()) {
      fprintf(stream, "    variable_1 : %s;\n",
              getTemplateVariableName(*variable1));
    }
    if (auto variable2 = lut_template->get_template_variable2()) {
      fprintf(stream, "    variable_2 : %s;\n",
              getTemplateVariableName(*variable2));
    }

    auto& axes = lut_template->get_axes();
    for (int i = 0; i < axes.size(); ++i) {
      fprintf(stream, "    index_%d(\"", i + 1);
      auto& axis_values = axes[i]->get_axis_values();
      for (int j = 0; j < axis_values.size(); ++j) {
        auto axis_float_value
            = dynamic_cast<LibFloatValue*>(axis_values[j].get())->getFloatValue();
        fprintf(stream, "%.8f",
                convertAxisValueForExport(lib, axes[i].get(),
                                          lut_template.get(), i,
                                          axis_float_value));
        if (j < axis_values.size() - 1) {
          fprintf(stream, ", ");
        }
      }
      fprintf(stream, "\");\n");
    }
    fprintf(stream, "  }\n");
  }

  if (!lib->get_lut_templates().empty()) {
    fprintf(stream, "\n");
  }
}

void writeLibertyCell(FILE* stream, LibCell* lib_cell,
                      const BusTypeDefinitions& bus_type_definitions)
{
  fprintf(stream, "  cell (%s) {\n", lib_cell->get_cell_name());
  if (lib_cell->get_cell_area() > 0.0) {
    fprintf(stream, "    area : %.8f;\n", lib_cell->get_cell_area());
  }
  if (lib_cell->isMacroCell()) {
    fprintf(stream, "    is_macro_cell : true;\n");
  }
  auto snkport2arcset = classifyCellArcBySnkPort(lib_cell);

  auto write_pin_block = [&](LibPort* cell_port, const char* indent) {
    const std::string port_name = cell_port->get_port_name();
    fprintf(stream, "%spin(\"%s\") {\n", indent, port_name.c_str());
    fprintf(stream, "%s  direction               : %s;\n", indent,
            getPortDirection(cell_port));
    if (cell_port->get_is_clock() || cell_port->get_is_clock_pin()) {
      fprintf(stream, "%s  clock                   : true;\n", indent);
    }
    fprintf(stream, "%s  capacitance             : %.8f;\n", indent,
            convertInternalCapValueToExportUnit(lib_cell->get_owner_lib(),
                                                cell_port->get_port_cap()));

    if (auto arc_iter = snkport2arcset.find(port_name);
        arc_iter != snkport2arcset.end()) {
      for (const auto& arc : arc_iter->second) {
        if (arc->isCheckTableArc()) {
          if (isExportedCheckTimingType(arc->get_timing_type())) {
            writeCheckTableModel(stream, arc);
          }
        } else if (arc->isDelayArc() || isClockTreePathArc(arc)) {
          writeDelayTableModel(stream, arc);
        }
      }
    }

    fprintf(stream, "%s}\n", indent);
  };

  std::map<std::string, std::vector<std::pair<int, LibPort*>>> bus_ports;
  for (const auto& cell_port : lib_cell->get_cell_ports()) {
    auto [bus_name, index] = splitPortName(cell_port->get_port_name());
    if (index) {
      bus_ports[bus_name].emplace_back(*index, cell_port.get());
    }
  }

  std::set<std::string> written_bus_names;
  std::set<std::string> written_port_names;
  for (const auto& cell_port : lib_cell->get_cell_ports()) {
    auto [bus_name, index] = splitPortName(cell_port->get_port_name());
    if (index) {
      if (!written_bus_names.insert(bus_name).second) {
        continue;
      }

      auto& bit_ports = bus_ports[bus_name];
      std::sort(bit_ports.begin(), bit_ports.end(),
                [](const auto& lhs, const auto& rhs) {
                  return lhs.first > rhs.first;
                });

      fprintf(stream, "    bus(\"%s\") {\n", bus_name.c_str());
      const auto bus_type_name =
          findBusTypeName(bus_type_definitions, bus_name, bit_ports);
      fprintf(stream, "      bus_type : %s;\n", bus_type_name.c_str());
      fprintf(stream, "      direction : %s;\n",
              getPortDirection(bit_ports.front().second));
      for (const auto& [bit_index, bit_port] : bit_ports) {
        (void) bit_index;
        written_port_names.insert(bit_port->get_port_name());
        write_pin_block(bit_port, "      ");
      }
      fprintf(stream, "    }\n");
      continue;
    }

    written_port_names.insert(cell_port->get_port_name());
    write_pin_block(cell_port.get(), "   ");
  }

  for (const auto& [port_name, arcs] : snkport2arcset) {
    (void) arcs;
    if (written_port_names.contains(port_name)) {
      continue;
    }

    if (auto* cell_port = lib_cell->get_cell_port_or_port_bus(port_name.c_str());
        cell_port && !cell_port->isLibertyPortBus()) {
      write_pin_block(cell_port, "   ");
    }
  }

  fprintf(stream, "  }\n");
}

nlohmann::json createTimingArcJson(LibArc* lib_arc)
{
  nlohmann::json timing_arc = nlohmann::json::object();
  timing_arc["source_sink"] = {lib_arc->get_src_port(), lib_arc->get_snk_port()};

  auto* delay_model = dynamic_cast<LibDelayTableModel*>(lib_arc->get_table_model());
  auto append_table_json = [&timing_arc](const char* table_name, LibTable* table) {
    auto& axes = table->get_axes();
    auto& lib_table_values = table->get_table_values();
    int columns = axes.empty()
                      ? 1
                      : (axes.size() > 1 ? axes[1].get()->get_axis_values().size()
                                         : static_cast<int>(lib_table_values.size()));
    nlohmann::json table_data;
    for (int i = 0; i < axes.size(); i++) {
      auto& axis_values = axes[i].get()->get_axis_values();
      nlohmann::json index = nlohmann::json::array();
      for (int j = 0; j < axis_values.size(); ++j) {
        auto axis_float_value
            = dynamic_cast<LibFloatValue*>(axis_values[j].get())->getFloatValue();
        index.push_back(axis_float_value);
      }
      table_data["index_" + std::to_string(i + 1)] = index;
    }

    nlohmann::json values_array = nlohmann::json::array();
    for (size_t i = 0; i < lib_table_values.size(); i += columns) {
      nlohmann::json row = nlohmann::json::array();
      for (size_t j = 0; j < columns && (i + j) < lib_table_values.size(); ++j) {
        auto lib_table_float_value
            = dynamic_cast<LibFloatValue*>(lib_table_values[i + j].get())->getFloatValue();
        row.push_back(lib_table_float_value);
      }
      values_array.push_back(row);
    }
    table_data["values"] = values_array;
    timing_arc[table_name] = table_data;
  };

  if (auto* cell_rise_table = delay_model->getTable(int(LibTable::TableType::kCellRise))) {
    append_table_json("cell_rise", cell_rise_table);
  }
  if (auto* rise_transition_table
      = delay_model->getTable(int(LibTable::TableType::kRiseTransition))) {
    append_table_json("rise_transition", rise_transition_table);
  }
  if (auto* cell_fall_table = delay_model->getTable(int(LibTable::TableType::kCellFall))) {
    append_table_json("cell_fall", cell_fall_table);
  }
  if (auto* fall_transition_table
      = delay_model->getTable(int(LibTable::TableType::kFallTransition))) {
    append_table_json("fall_transition", fall_transition_table);
  }

  return timing_arc;
}

}  // namespace

void LibLibrary::printLibertyLibrary(const char* lib_file_name)
{
  FILE* stream = std::fopen(lib_file_name, "w");
  if (!stream) {
    ECCLOG.warn(ecc::Loc::current(), "File ", lib_file_name, " NotWritable");
    return;
  }

  ECCLOG.info(ecc::Loc::current(), "start write liberty file ", lib_file_name);

  fprintf(stream, "library (%s) {\n", get_lib_name().c_str());

  auto write_percent_attr = [&stream](const char* attr_name,
                                      double percent_value) {
    fprintf(stream, "  %-30s : %g;\n", attr_name, percent_value);
  };

  if (const auto& comment = get_comment(); comment) {
    fprintf(stream, "  %-30s : \"%s\";\n", "comment", comment->c_str());
  }
  fprintf(stream, "  delay_model                    : table_lookup;\n");
  const bool simulation = get_simulation().value_or(false);
  fprintf(stream, "  %-30s : %s;\n", "simulation",
          simulation ? "true" : "false");
  fprintf(stream, "  capacitive_load_unit (1,%s);\n",
          getDelayModelCapUnitName(this));
  if (const auto& leakage_power_unit = get_leakage_power_unit();
      leakage_power_unit) {
    fprintf(stream, "  %-30s : %s;\n", "leakage_power_unit",
            leakage_power_unit->c_str());
  }
  if (const auto& current_unit_name = get_current_unit_name();
      current_unit_name) {
    fprintf(stream, "  %-30s : \"%s\";\n", "current_unit",
            current_unit_name->c_str());
  }
  fprintf(stream, "  pulling_resistance_unit        : \"1%s\";\n",
          getDelayModelResistanceUnitName(this));
  fprintf(stream, "  time_unit                      : \"1%s\";\n",
          getDelayModelTimeUnitName(this));
  if (const auto& voltage_unit_name = get_voltage_unit_name();
      voltage_unit_name) {
    fprintf(stream, "  %-30s : \"%s\";\n", "voltage_unit",
            voltage_unit_name->c_str());
  }
  if (get_library_features().empty()) {
    fprintf(stream, "  library_features(report_delay_calculation);\n");
  } else {
    for (const auto& feature : get_library_features()) {
      fprintf(stream, "  library_features(%s);\n", feature.c_str());
    }
  }

  write_percent_attr("input_threshold_pct_rise",
                     get_input_threshold_pct_rise() * 100.0);
  write_percent_attr("input_threshold_pct_fall",
                     get_input_threshold_pct_fall() * 100.0);
  write_percent_attr("output_threshold_pct_rise",
                     get_output_threshold_pct_rise() * 100.0);
  write_percent_attr("output_threshold_pct_fall",
                     get_output_threshold_pct_fall() * 100.0);
  write_percent_attr("slew_lower_threshold_pct_rise",
                     get_slew_lower_threshold_pct_rise() * 100.0);
  write_percent_attr("slew_lower_threshold_pct_fall",
                     get_slew_lower_threshold_pct_fall() * 100.0);
  write_percent_attr("slew_upper_threshold_pct_rise",
                     get_slew_upper_threshold_pct_rise() * 100.0);
  write_percent_attr("slew_upper_threshold_pct_fall",
                     get_slew_upper_threshold_pct_fall() * 100.0);
  fprintf(stream, "  %-30s : %g;\n", "slew_derate_from_library",
          get_slew_derate_from_library());
  if (const auto& nom_process = get_nom_process(); nom_process) {
    fprintf(stream, "  %-30s : %g;\n", "nom_process", *nom_process);
  }
  if (const auto& nom_temperature = get_nom_temperature();
      nom_temperature) {
    fprintf(stream, "  %-30s : %g;\n", "nom_temperature", *nom_temperature);
  }
  if (get_nom_voltage() > 0.0) {
    fprintf(stream, "  %-30s : %g;\n", "nom_voltage", get_nom_voltage());
  }
  fprintf(stream, "\n");

  auto bus_type_definitions = collectBusTypeDefinitions(this);
  writeLutTemplateDefinitions(stream, this);
  writeBusTypeDefinitions(stream, bus_type_definitions);
  for (const auto& cell : get_cells()) {
    writeLibertyCell(stream, cell.get(), bus_type_definitions);
  }
  fprintf(stream, "}\n");

  ECCLOG.info(ecc::Loc::current(), "finish write liberty file ", lib_file_name);
  std::fclose(stream);
}

void LibLibrary::printLibertyLibraryJson(const char* json_file_name)
{
  nlohmann::json json_data;
  json_data["lib_name"] = get_lib_name();
  for (const auto& cell : get_cells()) {
    nlohmann::json cell_info;
    cell_info["cell_name"] = cell->get_cell_name();
    cell_info["timing_arcs"] = nlohmann::json::array();

    auto snkport2arcset = classifyCellArcBySnkPort(cell.get());
    for (const auto& pair : snkport2arcset) {
      for (const auto& arc : pair.second) {
        if (arc->isDelayArc() || isClockTreePathArc(arc)) {
          cell_info["timing_arcs"].push_back(createTimingArcJson(arc));
        }
      }
    }
    json_data["cells_lib_info"].push_back(cell_info);
  }

  std::ofstream json_file(json_file_name);
  if (json_file.is_open()) {
    ECCLOG.info(ecc::Loc::current(), "start write liberty into json file: ", json_file_name);
    json_file << json_data.dump(1);
    json_file.close();
    ECCLOG.info(ecc::Loc::current(), "success write liberty into json file: ", json_file_name);
  } else {
    ECCLOG.info(ecc::Loc::current(), "fail write liberty into json file: ", json_file_name);
  }
}

}  // namespace idb
