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
 * @file LibParserCpp.cc
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief The liberty parser C++ API.
 * @version 0.1
 * @date 2023-10-13
 *
 */
#include <map>

#include "BTreeMap.hh"
#include "BTreeSet.hh"
#include "Lib.hh"
#include "LibParserCpp.hh"
#include "log/Log.hh"

namespace ista {

/**
 * @brief liberty expr builder.
 *
 */
void LibertyExprBuilder::execute() {
  if (std::string::npos != _expr_str.find('\\')) {
    // LOG_INFO << "before remove backslash, expr is " << _expr_str;
    // contain backslash, remove backslash.
    _expr_str = Str::concateBackSlashStr(_expr_str);
    // LOG_INFO << "after remove backslash, expr is " << _expr_str;
  }
  auto* expr_result = liberty_parse_expr(_expr_str.c_str());
  _result_expr = liberty_convert_expr(expr_result);
}

/**
 * @brief Visit the liberty simple attribute statement.
 *
 * @param attri
 * @return unsigned return 1 if success, else 0
 */
unsigned LibertyReader::visitSimpleAttri(LibertySimpleAttrStmt* attri) {
  LibBuilder* lib_builder = get_library_builder();
  LibLibrary* current_lib = lib_builder->get_lib();
  LibPort* lib_port = lib_builder->get_port();
  if (!lib_port) {
    lib_port = lib_builder->get_port_bus();
  }
  LibCell* lib_cell = lib_builder->get_cell();
  LibLeakagePower* leakage_power = lib_builder->get_leakage_power();
  LibArc* lib_arc = lib_builder->get_arc();
  LibPowerArc* lib_power_arc = lib_builder->get_power_arc();
  auto* lib_obj = lib_builder->get_obj();
  LibBuilder::LibertyOwnPortType own_port_type =
      lib_builder->get_own_port_type();
  LibBuilder::LibertyOwnPgOrWhenType own_pg_or_when_type =
      lib_builder->get_own_pg_or_when_type();

  double cap_unit_convert = 1.0;  // sta use pf internal
  if (CapacitiveUnit::kFF == current_lib->get_cap_unit()) {
    cap_unit_convert = 0.001;
  }

  double resistance_unit_convert = 1000.0;  // sta use ohm internal
  if (ResistanceUnit::kOHM == current_lib->get_resistance_unit()) {
    resistance_unit_convert = 1.0;
  }

  const char* attri_name = attri->attri_name;
  void* attri_value = const_cast<void*>(attri->attri_value);

  auto convert_string_to_bool = [](const std::string& str) -> bool {
    bool ret;
    std::istringstream(str) >> std::boolalpha >> ret;
    return ret;
  };

  std::map<std::string, std::function<void()>> process_attri = {
      {"slew_lower_threshold_pct_rise",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double slew_lower_threshold_pct_rise = attri_value_handle->value;
         current_lib->set_slew_lower_threshold_pct_rise(
             slew_lower_threshold_pct_rise);
         liberty_free_float_value(attri_value_handle);
       }},
      {"slew_upper_threshold_pct_rise",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double slew_upper_threshold_pct_rise = attri_value_handle->value;
         current_lib->set_slew_upper_threshold_pct_rise(
             slew_upper_threshold_pct_rise);
         liberty_free_float_value(attri_value_handle);
       }},
      {"slew_lower_threshold_pct_fall",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double slew_lower_threshold_pct_fall = attri_value_handle->value;
         current_lib->set_slew_lower_threshold_pct_fall(
             slew_lower_threshold_pct_fall);
         liberty_free_float_value(attri_value_handle);
       }},
      {"slew_upper_threshold_pct_fall",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double slew_upper_threshold_pct_fall = attri_value_handle->value;
         current_lib->set_slew_upper_threshold_pct_fall(
             slew_upper_threshold_pct_fall);
         liberty_free_float_value(attri_value_handle);
       }},
      {"input_threshold_pct_rise",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double input_threshold_pct_rise = attri_value_handle->value;
         current_lib->set_input_threshold_pct_rise(input_threshold_pct_rise);
         liberty_free_float_value(attri_value_handle);
       }},
      {"output_threshold_pct_rise",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double output_threshold_pct_rise = attri_value_handle->value;
         current_lib->set_output_threshold_pct_rise(output_threshold_pct_rise);
         liberty_free_float_value(attri_value_handle);
       }},
      {"input_threshold_pct_fall",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double input_threshold_pct_fall = attri_value_handle->value;
         current_lib->set_input_threshold_pct_fall(input_threshold_pct_fall);
         liberty_free_float_value(attri_value_handle);
       }},
      {"output_threshold_pct_fall",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double output_threshold_pct_fall = attri_value_handle->value;
         current_lib->set_output_threshold_pct_fall(output_threshold_pct_fall);
         liberty_free_float_value(attri_value_handle);
       }},
      {"slew_derate_from_library",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double slew_derate_from_library = attri_value_handle->value;
         current_lib->set_slew_derate_from_library(slew_derate_from_library);
         liberty_free_float_value(attri_value_handle);
       }},
      {"pulling_resistance_unit",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         const char* pulling_resistance_unit = attri_value_handle->value;
         if (Str::equal(pulling_resistance_unit, "1kohm")) {
           current_lib->set_resistance_unit(ResistanceUnit::kkOHM);
         } else if (Str::equal(pulling_resistance_unit, "1ohm")) {
           current_lib->set_resistance_unit(ResistanceUnit::kOHM);
         }
         liberty_free_string_value(attri_value_handle);
       }},
      {"comment",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         current_lib->set_comment(attri_value_handle->value);
         liberty_free_string_value(attri_value_handle);
       }},
      {"simulation",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         current_lib->set_simulation(
             convert_string_to_bool(attri_value_handle->value));
         liberty_free_string_value(attri_value_handle);
       }},
      {"time_unit",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         const char* time_unit = attri_value_handle->value;
         if (Str::equal(time_unit, "1fs")) {
           current_lib->set_time_unit(TimeUnit::kFS);
         } else if (Str::equal(time_unit, "1ps")) {
           current_lib->set_time_unit(TimeUnit::kPS);
         }
         liberty_free_string_value(attri_value_handle);
       }},
      {"current_unit",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         current_lib->set_current_unit_name(attri_value_handle->value);
         liberty_free_string_value(attri_value_handle);
       }},
      {"voltage_unit",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         current_lib->set_voltage_unit_name(attri_value_handle->value);
         liberty_free_string_value(attri_value_handle);
       }},
      {"leakage_power_unit",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         const char* leakage_power_unit = attri_value_handle->value;
         double power_unit_mw_scale = 1.0;
         if (Str::noCaseEqual(leakage_power_unit, "1pw")) {
           power_unit_mw_scale = 1e-9;
         } else if (Str::noCaseEqual(leakage_power_unit, "1nw")) {
           power_unit_mw_scale = 1e-6;
         } else if (Str::noCaseEqual(leakage_power_unit, "1uw")) {
           power_unit_mw_scale = 1e-3;
         } else if (Str::noCaseEqual(leakage_power_unit, "1mw")) {
           power_unit_mw_scale = 1.0;
         }
         current_lib->set_leakage_power_unit(leakage_power_unit);
         current_lib->set_power_unit_mw_scale(power_unit_mw_scale);
         liberty_free_string_value(attri_value_handle);
       }},

      {"nom_voltage",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double nom_voltage = attri_value_handle->value;
         current_lib->set_nom_voltage(nom_voltage);
         liberty_free_float_value(attri_value_handle);
       }},
      {"nom_process",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         current_lib->set_nom_process(attri_value_handle->value);
         liberty_free_float_value(attri_value_handle);
       }},
      {"nom_temperature",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         current_lib->set_nom_temperature(attri_value_handle->value);
         liberty_free_float_value(attri_value_handle);
       }},

      {"default_max_transition",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double default_max_transition = attri_value_handle->value;
         default_max_transition =
             current_lib->convert_time_unit_to_ns(default_max_transition);
         current_lib->set_default_max_transition(default_max_transition);
         liberty_free_float_value(attri_value_handle);
       }},

      {"default_max_fanout",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double default_max_fanout = attri_value_handle->value;
         current_lib->set_default_max_fanout(default_max_fanout);
         liberty_free_float_value(attri_value_handle);
       }},
      {"direction",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         const char* port_type = attri_value_handle->value;
         lib_port->set_port_type(port_type);
         liberty_free_string_value(attri_value_handle);
       }},
      {"clock",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         const char* attri_str = attri_value_handle->value;
         bool is_clock_pin = convert_string_to_bool(attri_str);
         lib_port->set_is_clock_pin(is_clock_pin);
         liberty_free_string_value(attri_value_handle);
       }},
      {"clock_gate_clock_pin",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         const char* clock_gate_clock_pin = attri_value_handle->value;
         bool clock_gate_clock_pin1 =
             convert_string_to_bool(clock_gate_clock_pin);
         lib_port->set_clock_gate_clock_pin(clock_gate_clock_pin1);
         liberty_free_string_value(attri_value_handle);
       }},
      {"clock_gate_enable_pin",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         const char* clock_gate_enable_pin = attri_value_handle->value;
         bool clock_gate_enable_pin1 =
             convert_string_to_bool(clock_gate_enable_pin);
         lib_port->set_clock_gate_enable_pin(clock_gate_enable_pin1);
         liberty_free_string_value(attri_value_handle);
       }},
      {"default_fanout_load",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double default_fanout_load_val = attri_value_handle->value;
         current_lib->set_default_fanout_load(default_fanout_load_val);
         liberty_free_float_value(attri_value_handle);
       }},
      {"default_wire_load",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         const char* default_wire_load = attri_value_handle->value;
         current_lib->set_default_wire_load(default_wire_load);
         liberty_free_string_value(attri_value_handle);
       }},
      {"fanout_load",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double fanout_load_val = attri_value_handle->value;
         lib_port->set_fanout_load(fanout_load_val);
         liberty_free_float_value(attri_value_handle);
       }},
      {"capacitance",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double cap = attri_value_handle->value;
         cap *= cap_unit_convert;
         if (lib_port) {
           lib_port->set_port_cap(cap);
         } else {
           dynamic_cast<LibWireLoad*>(lib_obj)->set_cap_per_length_unit(cap);
         }
         liberty_free_float_value(attri_value_handle);
       }},
      {"area",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double cell_area = attri_value_handle->value;
         if (lib_cell) {
           lib_cell->set_cell_area(cell_area);
         }
         liberty_free_float_value(attri_value_handle);
       }},
      {"is_macro_cell",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         const char* is_macro = attri_value_handle->value;
         if (Str::noCaseEqual(is_macro, "TRUE")) {
           lib_cell->set_is_macro();
         }
         liberty_free_string_value(attri_value_handle);
       }},
      {"cell_leakage_power",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double cell_leakage_power = attri_value_handle->value;
         cell_leakage_power =
             current_lib->convert_power_unit_to_mw(cell_leakage_power);
         lib_cell->set_cell_leakage_power(cell_leakage_power);
         liberty_free_float_value(attri_value_handle);
       }},
      {"clock_gating_integrated_cell",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         std::string clock_gating_integrated_cell = attri_value_handle->value;
         lib_cell->set_clock_gating_integrated_cell(
             clock_gating_integrated_cell);
         lib_cell->set_is_clock_gating_integrated_cell(true);
         liberty_free_string_value(attri_value_handle);
       }},
      {"resistance",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double resistance = attri_value_handle->value;
         resistance *= resistance_unit_convert;
         dynamic_cast<LibWireLoad*>(lib_obj)->set_resistance_per_length_unit(
             resistance);
         liberty_free_float_value(attri_value_handle);
       }},
      {"slope",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double slope = attri_value_handle->value;
         dynamic_cast<LibWireLoad*>(lib_obj)->set_slope(slope);
         liberty_free_float_value(attri_value_handle);
       }},
      {"rise_capacitance",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double cap = attri_value_handle->value;
         cap *= cap_unit_convert;
         lib_port->set_port_cap(AnalysisMode::kMaxMin, TransType::kRise, cap);
         liberty_free_float_value(attri_value_handle);
       }},
      {"fall_capacitance",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double cap = attri_value_handle->value;
         cap *= cap_unit_convert;
         lib_port->set_port_cap(AnalysisMode::kMaxMin, TransType::kFall, cap);
         liberty_free_float_value(attri_value_handle);
       }},
      {"max_capacitance",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double max_cap_limit = attri_value_handle->value;
         max_cap_limit *= cap_unit_convert;
         lib_port->set_port_cap_limit(AnalysisMode::kMax, max_cap_limit);
         liberty_free_float_value(attri_value_handle);
       }},
      {"min_capacitance",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double min_cap_limit = attri_value_handle->value;
         min_cap_limit *= cap_unit_convert;
         lib_port->set_port_cap_limit(AnalysisMode::kMin, min_cap_limit);
         liberty_free_float_value(attri_value_handle);
       }},
      {"max_transition",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double max_slew_limit = attri_value_handle->value;
         max_slew_limit = current_lib->convert_time_unit_to_ns(max_slew_limit);
         lib_port->set_port_slew_limit(AnalysisMode::kMax, max_slew_limit);
         liberty_free_float_value(attri_value_handle);
       }},
      {"min_transition",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double min_slew_limit = attri_value_handle->value;
         min_slew_limit = current_lib->convert_time_unit_to_ns(min_slew_limit);
         lib_port->set_port_slew_limit(AnalysisMode::kMin, min_slew_limit);
         liberty_free_float_value(attri_value_handle);
       }},
      {"function",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         const char* expr_str = attri_value_handle->value;
         LibertyExprBuilder expr_builder(expr_str);
         expr_builder.execute();
         auto* func_expr = expr_builder.get_result_expr();
         lib_port->set_func_expr(func_expr);
         lib_port->set_func_expr_str(expr_str);
         liberty_free_string_value(attri_value_handle);
       }},
      {"related_pin",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         const char* pin_name = attri_value_handle->value;
         if (own_port_type == LibBuilder::LibertyOwnPortType::kTimingArc) {
           lib_arc->set_src_port(pin_name);
         } else if (own_port_type ==
                    LibBuilder::LibertyOwnPortType::kPowerArc) {
           lib_power_arc->set_src_port(pin_name);
         }
         liberty_free_string_value(attri_value_handle);
       }},
      {"related_pg_pin",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         const char* pg_pin_name = attri_value_handle->value;
         if (own_pg_or_when_type ==
             LibBuilder::LibertyOwnPgOrWhenType::kLibertyLeakagePower) {
           leakage_power->set_related_pg_port(pg_pin_name);
         } else if (own_pg_or_when_type ==
                    LibBuilder::LibertyOwnPgOrWhenType::kPowerArc) {
           lib_power_arc->set_related_pg_port(pg_pin_name);
         }
         liberty_free_string_value(attri_value_handle);
       }},
      {"when",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         const char* when = attri_value_handle->value;
         if (own_port_type == LibBuilder::LibertyOwnPortType::kTimingArc) {
           if (lib_arc) {
             lib_arc->set_when(when);
           }
         } else if (own_pg_or_when_type ==
             LibBuilder::LibertyOwnPgOrWhenType::kLibertyLeakagePower) {
           leakage_power->set_when(when);
         } else if (own_pg_or_when_type ==
                    LibBuilder::LibertyOwnPgOrWhenType::kPowerArc) {
           if (lib_power_arc) {
             lib_power_arc->set_when(when);
           }
         }
         liberty_free_string_value(attri_value_handle);
       }},
      {"value",
       [=]() {
         if (liberty_is_string_value(attri_value)) {
           auto* attri_value_handle = liberty_convert_string_value(attri_value);
           const char* value = attri_value_handle->value;  // ysxy
           leakage_power->set_value(
               current_lib->convert_power_unit_to_mw(atof(value)));
           liberty_free_string_value(attri_value_handle);
         } else {
           auto* attri_value_handle = liberty_convert_float_value(attri_value);
           double value = attri_value_handle->value;  // T28
           leakage_power->set_value(
               current_lib->convert_power_unit_to_mw(value));
           liberty_free_float_value(attri_value_handle);
         }
       }},
      {"timing_sense",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         const char* timing_sense = attri_value_handle->value;
         lib_arc->set_timing_sense(timing_sense);
         liberty_free_string_value(attri_value_handle);
       }},
      {"timing_type",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         const char* timing_type = attri_value_handle->value;
         lib_arc->set_timing_type(timing_type);
         liberty_free_string_value(attri_value_handle);
       }},
      {"variable_1",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         auto* lib_template = lib_obj;
         const char* variable_name = attri_value_handle->value;
         lib_template->set_template_variable1(variable_name);
         liberty_free_string_value(attri_value_handle);
       }},
      {"variable_2",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         auto* lib_template = lib_obj;
         const char* variable_name = attri_value_handle->value;
         lib_template->set_template_variable2(variable_name);
         liberty_free_string_value(attri_value_handle);
       }},
      {"variable_3",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         auto* lib_template = lib_obj;
         const char* variable_name = attri_value_handle->value;
         lib_template->set_template_variable3(variable_name);
         liberty_free_string_value(attri_value_handle);
       }},
      {"reference_time",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         auto* lib_table = dynamic_cast<LibVectorTable*>(lib_obj);
         double ref_time = attri_value_handle->value;
         lib_table->set_ref_time(ref_time);
         liberty_free_float_value(attri_value_handle);
       }},
      {"base_type",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         std::string base_type = attri_value_handle->value;
         dynamic_cast<LibType*>(lib_obj)->set_base_type(std::move(base_type));
         liberty_free_string_value(attri_value_handle);
       }},
      {"data_type",
       [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         std::string data_type = attri_value_handle->value;
         dynamic_cast<LibType*>(lib_obj)->set_data_type(std::move(data_type));
         liberty_free_string_value(attri_value_handle);
       }},
      {"bit_width",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double bit_width = attri_value_handle->value;
         dynamic_cast<LibType*>(lib_obj)->set_bit_width(
             static_cast<unsigned>(bit_width));
         liberty_free_float_value(attri_value_handle);
       }},
      {"bit_from",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double bit_from = attri_value_handle->value;
         dynamic_cast<LibType*>(lib_obj)->set_bit_from(
             static_cast<unsigned>(bit_from));
         liberty_free_float_value(attri_value_handle);
       }},
      {"bit_to",
       [=]() {
         auto* attri_value_handle = liberty_convert_float_value(attri_value);
         double bit_to = attri_value_handle->value;
         dynamic_cast<LibType*>(lib_obj)->set_bit_to(
             static_cast<unsigned>(bit_to));
         liberty_free_float_value(attri_value_handle);
       }},
      {"bus_type", [=]() {
         auto* attri_value_handle = liberty_convert_string_value(attri_value);
         auto* port_bus = lib_builder->get_port_bus();
         std::string bus_type = attri_value_handle->value;
         auto* lib_type = current_lib->getLibType(bus_type.c_str());
         port_bus->set_bus_type(lib_type);
         liberty_free_string_value(attri_value_handle);
       }}};

  if (process_attri.contains(attri_name)) {
    process_attri[attri_name]();
  }

  return 1;
}

/**
 * @brief Visit table axis and values.
 *
 * @param attri
 * @return unsigned
 */
unsigned LibertyReader::visitAxisOrValues(
    LibertyComplexAttrStmt* attri) {
  LibBuilder* lib_builder = get_library_builder();

  const char* attri_name = attri->attri_name;
  auto& attribute_values = attri->attri_values;

  /**
  @note the origial value may be quote by string.
   * So we need recover the double value.*/
  auto convert_attri_values =
      [](auto& attribute_values) -> std::vector<std::unique_ptr<LibAttrValue>> {
    auto split_str = [](std::string const& original,
                        char separator) -> std::vector<std::string> {
      std::vector<std::string> results;
      std::string token;
      std::istringstream is(original);
      while (std::getline(is, token, separator)) {
        if (!token.empty()) {
          results.push_back(token);
        }
      }
      return results;
    };

    std::vector<std::unique_ptr<LibAttrValue>> result_values;

    void* attri_value;
    FOREACH_LIBERTY_VEC_ELEM(&attribute_values, void, attri_value) {
      if (liberty_is_string_value(attri_value)) {
        std::string val = liberty_convert_string_value(attri_value)->value;
        auto str_vec = split_str(val, ',');
        for (auto& str : str_vec) {
          auto double_val =
              std::make_unique<LibFloatValue>(std::atof(str.c_str()));
          result_values.emplace_back(std::move(double_val));
        }
      } else {
        double val = liberty_convert_float_value(attri_value)->value;
        auto double_val = std::make_unique<LibFloatValue>(val);
        result_values.emplace_back(std::move(double_val));
      }
    }

    return result_values;
  };

  auto* lib_obj = lib_builder->get_obj();
  if (lib_obj) {
    auto result_values = convert_attri_values(attribute_values);

    if (Str::equal(attri_name, "values")) {
      auto* lib_table = dynamic_cast<LibTable*>(lib_obj);
      LOG_FATAL_IF(!lib_table);
      lib_table->set_value_scale(LibValueScale::kLibrary);
      lib_table->set_table_values(std::move(result_values));
    } else {
      auto liberty_axis = std::make_unique<LibAxis>(attri_name);
      liberty_axis->set_value_scale(LibValueScale::kLibrary);
      liberty_axis->set_axis_values(std::move(result_values));
      lib_obj->addAxis(std::move(liberty_axis));
    }
  }

  return 1;
}

/**
 * @brief Visit the liberty complex attribute statement.
 *
 * @param attri
 * @return unsigned return 1 if success, else 0
 */
unsigned LibertyReader::visitComplexAttri(
    LibertyComplexAttrStmt* attri) {
  const char* attri_name = attri->attri_name;
  LibBuilder* lib_builder = get_library_builder();
  auto* the_lib = lib_builder->get_lib();
  auto* lib_obj = lib_builder->get_obj();
  auto* lib_port = lib_builder->get_port();

  auto& attri_values = attri->attri_values;

  unsigned is_ok = 1;

  void* attri_0 = GetLibertyVecElem<void>(&attri_values, 0);
  void* attri_1 = GetLibertyVecElem<void>(&attri_values, 1);

  double cap_unit_convert = 1.0;  // sta use pf internal
  if (the_lib && CapacitiveUnit::kFF == the_lib->get_cap_unit()) {
    cap_unit_convert = 0.001;
  }

  std::map<std::string, std::function<void()>> process_attri = {
      {"capacitive_load_unit",
       [&]() {
         if ((static_cast<int>(liberty_convert_float_value(attri_0)->value) ==
              1) &&
             (Str::equal(liberty_convert_string_value(attri_1)->value, "pf"))) {
           the_lib->set_cap_unit(CapacitiveUnit::kPF);
         }
       }},
      {{"rise_capacitance_range"},
       [&]() {
         double min_rise_cap = liberty_convert_float_value(attri_0)->value;
         double max_rise_cap = liberty_convert_float_value(attri_1)->value;
         min_rise_cap *= cap_unit_convert;
         max_rise_cap *= cap_unit_convert;

         lib_port->set_port_cap(AnalysisMode::kMin, TransType::kRise,
                                min_rise_cap);
         lib_port->set_port_cap(AnalysisMode::kMax, TransType::kRise,
                                max_rise_cap);
       }},
      {{"fall_capacitance_range"},
       [&]() {
         double min_fall_cap = liberty_convert_float_value(attri_0)->value;
         double max_fall_cap = liberty_convert_float_value(attri_1)->value;
         min_fall_cap *= cap_unit_convert;
         max_fall_cap *= cap_unit_convert;

         lib_port->set_port_cap(AnalysisMode::kMin, TransType::kFall,
                                min_fall_cap);
         lib_port->set_port_cap(AnalysisMode::kMax, TransType::kFall,
                                max_fall_cap);
       }},
      {"fanout_length", [&]() {
         if (attri_values.len == 2) {
           double fanout = liberty_convert_float_value(attri_0)->value;
           double length = liberty_convert_float_value(attri_1)->value;
           dynamic_cast<LibWireLoad*>(lib_obj)->add_length_to_map(
               static_cast<int>(fanout), length);
         } else if (attri_values.len == 1) {
           // this case is fix cx55 issue, such as:
           // fanout_length("1", \
           //  "0");

           char* fanout_length = liberty_convert_string_value(attri_0)->value;
           auto fanout_lenth_vec = Str::split(fanout_length, ",");
           LOG_FATAL_IF(fanout_lenth_vec.size() != 2);

           double fanout = std::atof(fanout_lenth_vec[0].c_str());
           double length = std::atof(fanout_lenth_vec[1].c_str());

           dynamic_cast<LibWireLoad*>(lib_obj)->add_length_to_map(
               static_cast<int>(fanout), length);
         }
       }},
      {"library_features",
       [&]() {
         void* attri_value = nullptr;
         FOREACH_LIBERTY_VEC_ELEM(&attri_values, void, attri_value) {
           if (liberty_is_string_value(attri_value)) {
             auto* feature_value = liberty_convert_string_value(attri_value);
             the_lib->add_library_feature(feature_value->value);
             liberty_free_string_value(feature_value);
           } else if (liberty_is_float_value(attri_value)) {
             auto* feature_value = liberty_convert_float_value(attri_value);
              the_lib->add_library_feature(
                  std::to_string(feature_value->value));
              liberty_free_float_value(feature_value);
            }
          }
       }}};

  if (process_attri.contains(attri_name)) {
    process_attri[attri_name]();
  } else if (Str::startWith(attri_name, "index") ||
             Str::equal(attri_name, "values")) {
    is_ok = visitAxisOrValues(attri);
  }
  else {
    LOG_INFO_EVERY_N(10) << "unkown attri name: " << attri_name << " in "
                         << attri->file_name << " line no " << attri->line_no;
  }
  return is_ok;
}

/**
 * @brief Visit group attri for name.
 *
 * @param group
 * @return const char*
 */
const char* LibertyReader::getGroupAttriName(LibertyGroupStmt* group) {
  auto& attri_values = group->attri_values;
  LOG_FATAL_IF(!liberty_is_string_value(attri_values.data));
  auto* lib_name_attri = liberty_convert_string_value(attri_values.data);

  return lib_name_attri->value;
}

/**
 * @brief Visit stmt of the group stmt.
 *
 * @param group
 * @return unsigned
 */
unsigned LibertyReader::visitStmtInGroup(LibertyGroupStmt* group) {
  unsigned is_ok = 1;

  auto lib_stmts = group->stmts;
  void* lib_stmt;
  FOREACH_LIBERTY_VEC_ELEM(&lib_stmts, void, lib_stmt) {
    // simple attri stmt first, we need set attribute.
    if (liberty_is_simple_attri_stmt(lib_stmt)) {
      auto* simple_lib_stmt = liberty_convert_simple_attribute_stmt(lib_stmt);
      is_ok &= visitSimpleAttri(simple_lib_stmt);
      liberty_free_simple_attribute_stmt(simple_lib_stmt);
    }
  }

  // visit complex/group data finally.
  FOREACH_LIBERTY_VEC_ELEM(&lib_stmts, void, lib_stmt) {
    if (liberty_is_complex_attri_stmt(lib_stmt)) {
      auto* complex_lib_stmt = liberty_convert_complex_attribute_stmt(lib_stmt);
      is_ok &= visitComplexAttri(complex_lib_stmt);
      liberty_free_complex_attribute_stmt(complex_lib_stmt);
    } else if (liberty_is_group_stmt(lib_stmt)) {
      // group stmt.
      auto* group_lib_stmt = liberty_convert_group_stmt(lib_stmt);
      is_ok &= visitGroup(group_lib_stmt);
      liberty_free_group_stmt(group_lib_stmt);
    }
  }

  return is_ok;
}

/**
 * @brief Visit library group stmt.
 *
 * @param group
 * @return unsigned
 */
unsigned LibertyReader::visitLibrary(LibertyGroupStmt* group) {
  const char* lib_name = getGroupAttriName(group);

  auto* library_builder = new LibBuilder(lib_name);
  set_library_builder(library_builder);

  auto* curr_lib = library_builder->get_lib();
  curr_lib->set_file_name(group->file_name);

  unsigned is_ok = visitStmtInGroup(group);

  return 1;
}

/**
 * @brief Visit the wire load group stmt.
 *
 * @param group
 * @return unsigned
 */
unsigned LibertyReader::visitWireLoad(LibertyGroupStmt* group) {
  LibBuilder* lib_builder = get_library_builder();
  LibLibrary* lib = lib_builder->get_lib();

  const char* wire_load_name = getGroupAttriName(group);
  auto wire_load = std::make_unique<LibWireLoad>(wire_load_name);
  lib_builder->set_obj(wire_load.get());

  unsigned is_ok = visitStmtInGroup(group);

  lib->addWireLoad(std::move(wire_load));
  lib_builder->set_obj(nullptr);
  return is_ok;
}

/**
 * @brief Visit the lut table template group.
 *
 * @param group
 * @return unsigned
 */
unsigned LibertyReader::visitLuTableTemplate(LibertyGroupStmt* group) {
  LibBuilder* lib_builder = get_library_builder();
  LibLibrary* lib = lib_builder->get_lib();

  const char* template_name = getGroupAttriName(group);
  auto lut_table_template =
      std::make_unique<LibLutTableTemplate>(template_name);

  lib_builder->set_port(nullptr);
  lib_builder->set_obj(lut_table_template.get());

  unsigned is_ok = visitStmtInGroup(group);

  lib->addLutTemplate(std::move(lut_table_template));

  lib_builder->set_obj(nullptr);

  return is_ok;
}

/**
 * @brief Visit the type of lib.
 *
 * @param group
 * @return unsigned
 */
unsigned LibertyReader::visitType(LibertyGroupStmt* group) {
  LibBuilder* lib_builder = get_library_builder();
  LibLibrary* lib = lib_builder->get_lib();

  const char* type_name = getGroupAttriName(group);
  auto lib_type = std::make_unique<LibType>(type_name);

  lib_builder->set_port(nullptr);
  lib_builder->set_obj(lib_type.get());

  unsigned is_ok = visitStmtInGroup(group);

  lib->addLibType(std::move(lib_type));

  lib_builder->set_obj(nullptr);

  return is_ok;
}

/**
 * @brief Visit output current template.
 *
 * @param group
 * @return unsigned
 */
unsigned LibertyReader::visitOutputCurrentTemplate(
    LibertyGroupStmt* group) {
  LibBuilder* lib_builder = get_library_builder();
  LibLibrary* lib = lib_builder->get_lib();

  const char* template_name = getGroupAttriName(group);
  auto current_table_template =
      std::make_unique<LibCurrentTemplate>(template_name);

  lib_builder->set_obj(current_table_template.get());

  unsigned is_ok = visitStmtInGroup(group);

  lib->addLutTemplate(std::move(current_table_template));

  lib_builder->set_obj(nullptr);

  return is_ok;
}

/**
 * @brief Visit the cell group.
 *
 * @param group
 * @return unsigned return 1 if success, else 0
 */
unsigned LibertyReader::visitCell(LibertyGroupStmt* group) {
  LibBuilder* lib_builder = get_library_builder();
  LibLibrary* lib = lib_builder->get_lib();

  const char* cell_name = getGroupAttriName(group);
  // if not need build, return to speed up.
  if (!isNeedBuild(cell_name)) {
    return 1;
  }

  auto lib_cell = std::make_unique<LibCell>(cell_name, lib);
  lib_builder->set_cell(lib_cell.get());

  unsigned is_ok = visitStmtInGroup(group);

  lib->addLibertyCell(std::move(lib_cell));

  lib_builder->set_obj(nullptr);

  return is_ok;
}

/**
 * @brief Visit leakage power.
 *
 * @param group
 * @return unsigned
 */
unsigned LibertyReader::visitLeakagePower(LibertyGroupStmt* group) {
  LibBuilder* lib_builder = get_library_builder();
  LibCell* lib_cell = lib_builder->get_cell();

  lib_builder->set_own_pg_or_when_type(
      LibBuilder::LibertyOwnPgOrWhenType::kLibertyLeakagePower);
  auto leakage_power = std::make_unique<LibLeakagePower>();
  lib_builder->set_leakage_power(leakage_power.get());
  leakage_power->set_owner_cell(lib_cell);

  unsigned is_ok = visitStmtInGroup(group);

  lib_cell->addLeakagePower(std::move(leakage_power));

  lib_builder->set_obj(nullptr);

  return is_ok;
}

/**
 * @brief Visit the bus pin.
 *
 * @param group
 * @return unsigned
 */
unsigned LibertyReader::visitBus(LibertyGroupStmt* group) {
  LibBuilder* lib_builder = get_library_builder();
  LibCell* cell = lib_builder->get_cell();

  const char* bus_port_name = getGroupAttriName(group);
  auto lib_port_bus = std::make_unique<LibPortBus>(bus_port_name);
  lib_port_bus->set_ower_cell(cell);
  lib_builder->set_port_bus(lib_port_bus.get());
  lib_builder->set_port(lib_port_bus.get());
  cell->addLibertyPortBus(std::move(lib_port_bus));

  unsigned is_ok = visitStmtInGroup(group);

  // reset the port bus pointer.
  lib_builder->set_port_bus(nullptr);

  return is_ok;
}

/**
 * @brief Visit the pin group.
 *
 * @param group
 * @return unsigned return 1 if success, else 0
 */
unsigned LibertyReader::visitPin(LibertyGroupStmt* group) {
  LibBuilder* lib_builder = get_library_builder();
  LibCell* cell = lib_builder->get_cell();

  const char* port_name = getGroupAttriName(group);

  auto create_port = [lib_builder, cell](const char* port_name) {
    auto lib_port = std::make_unique<LibPort>(port_name);
    lib_port->set_ower_cell(cell);

    if (auto* port_bus = lib_builder->get_port_bus(); !port_bus) {
      lib_builder->set_port(lib_port.get());
      cell->addLibertyPort(std::move(lib_port));
    } else {
      lib_port->set_port_type(port_bus->get_port_type());
      port_bus->addlibertyPort(std::move(lib_port));
    }
  };

  std::string regex_pattern = "([A-Za-z]+)\\[(\\d+):(\\d+)\\]";
  auto ret_val = Str::matchPattern(port_name, regex_pattern);
  if (ret_val.empty()) {
    create_port(port_name);
  } else {
    std::string port_bus_name = ret_val[1];
    int port_range_left = std::atoi(ret_val[2].c_str());
    int port_range_right = std::atoi(ret_val[3].c_str());

    for (int index = port_range_left; index >= port_range_right; --index) {
      const char* one_port_name =
          Str::printf("%s[%d]", port_bus_name.c_str(), index);
      create_port(one_port_name);
    }
  }

  unsigned is_ok = visitStmtInGroup(group);
  // reset the port pointer.
  lib_builder->set_port(nullptr);

  return is_ok;
}

/**
 * @brief Visit the timing group.
 *
 * @param group
 * @return unsigned return 1 if success, else 0
 */
unsigned LibertyReader::visitTiming(LibertyGroupStmt* group) {
  LibBuilder* lib_builder = get_library_builder();
  LibPort* lib_port = lib_builder->get_port();
  LibPortBus* lib_port_bus;
  if (!lib_port) {
    lib_port_bus = lib_builder->get_port_bus();
  }
  LibCell* lib_cell = lib_builder->get_cell();
  lib_builder->set_own_port_type(LibBuilder::LibertyOwnPortType::kTimingArc);
  auto lib_arc = std::make_unique<LibArc>();
  lib_builder->set_arc(lib_arc.get());
  lib_builder->set_table_model(nullptr);  // reset table model.
  lib_port ? lib_arc->set_snk_port(lib_port->get_port_name())
           : lib_arc->set_snk_port(lib_port_bus->get_port_name());
  lib_arc->set_owner_cell(lib_cell);

  unsigned is_ok = visitStmtInGroup(group);

  lib_cell->addLibertyArc(std::move(lib_arc));

  return is_ok;
}

/**
 * @brief Visit the internal power.
 *
 * @param group
 * @return unsigned
 */
unsigned LibertyReader::visitInternalPower(LibertyGroupStmt* group) {
  LibBuilder* lib_builder = get_library_builder();
  LibPort* lib_port = lib_builder->get_port();
  LibPortBus* lib_port_bus;
  if (!lib_port) {
    lib_port_bus = lib_builder->get_port_bus();
  }
  LibCell* lib_cell = lib_builder->get_cell();
  lib_builder->set_own_port_type(LibBuilder::LibertyOwnPortType::kPowerArc);
  lib_builder->set_own_pg_or_when_type(
      LibBuilder::LibertyOwnPgOrWhenType::kPowerArc);
  auto lib_power_arc = std::make_unique<LibPowerArc>();
  lib_builder->set_power_arc(lib_power_arc.get());
  lib_builder->set_table_model(nullptr);  // reset table model.
  if (lib_port) {
    lib_power_arc->set_snk_port(lib_port->get_port_name());
  } else if (lib_port_bus) {
    lib_power_arc->set_snk_port(lib_port_bus->get_port_name());
  }

  lib_power_arc->set_owner_cell(lib_cell);

  auto internal_power_info = std::make_unique<LibInternalPowerInfo>();
  internal_power_info->set_file_name(group->file_name);
  internal_power_info->set_line_no(group->line_no);
  lib_power_arc->set_internal_power_info(std::move(internal_power_info));

  unsigned is_ok = 1;
  auto lib_stmts = group->stmts;
  void* lib_stmt;

  // for simple stmt, need first visit set powr arc attribute.
  FOREACH_LIBERTY_VEC_ELEM(&lib_stmts, void, lib_stmt) {
    if (liberty_is_simple_attri_stmt(lib_stmt)) {
      // for the power arc attribute.
      auto* simple_lib_stmt = liberty_convert_simple_attribute_stmt(lib_stmt);
      is_ok &= visitSimpleAttri(simple_lib_stmt);
      liberty_free_simple_attribute_stmt(simple_lib_stmt);
    }
  }

  // visit group data finally.
  FOREACH_LIBERTY_VEC_ELEM(&lib_stmts, void, lib_stmt) {
    if (liberty_is_group_stmt(lib_stmt)) {
      // for the power data.
      // group stmt.
      auto* group_lib_stmt = liberty_convert_group_stmt(lib_stmt);
      is_ok &= visitGroup(group_lib_stmt);
      liberty_free_group_stmt(group_lib_stmt);
    }
  }

  if (!lib_power_arc->isSrcPortEmpty()) {
    lib_cell->addLibertyPowerArc(std::move(lib_power_arc));
  } else if (lib_power_arc->isSrcPortEmpty() &&
             lib_power_arc->isSnkPortEmpty()) {
    lib_cell->addLibertyPowerArc(std::move(
        lib_power_arc));  // TODO(to taosimin), for s180, the internal power
                          // calculation may be power arc src and snk is empty.
  } else {
    auto& internal_power_info = lib_power_arc->get_internal_power_info();
    lib_port ? lib_port->addInternalPower(std::move(internal_power_info))
             : lib_port_bus->addInternalPower(std::move(internal_power_info));
    lib_builder->set_power_arc(nullptr);
  }

  return is_ok;
}

/**
 * @brief Visit the output current table.
 *
 * @param group
 * @return unsigned
 */
unsigned LibertyReader::visitCurrentTable(LibertyGroupStmt* group) {
  LibBuilder* lib_builder = get_library_builder();
  auto* lib_model = lib_builder->get_table_model();
  auto* lib_delay_model = dynamic_cast<LibDelayTableModel*>(lib_model);

  const auto* const table_name = group->group_name;
  auto table_type = STR_TO_TABLE_TYPE(table_name);

  auto lib_table = std::make_unique<LibCCSTable>(table_type);
  lib_builder->set_current_table(lib_table.get());

  unsigned is_ok = visitStmtInGroup(group);

  lib_delay_model->addCurrentTable(std::move(lib_table));

  return is_ok;
}

/**
 * @brief Visit the current vector.
 *
 * @param group
 * @return unsigned
 */
unsigned LibertyReader::visitVector(LibertyGroupStmt* group) {
  LibBuilder* lib_builder = get_library_builder();

  const char* table_template_name = getGroupAttriName(group);
  auto* the_lib = lib_builder->get_lib();
  auto* lut_template = the_lib->getLutTemplate(table_template_name);
  LOG_FATAL_IF(!lut_template) << "not found template " << table_template_name;

  auto* current_table =
      dynamic_cast<LibCCSTable*>(lib_builder->get_current_table());
  auto table_type = current_table->get_table_type();

  auto lib_table = std::make_unique<LibVectorTable>(table_type, lut_template);
  lib_table->set_file_name(group->file_name);
  lib_table->set_line_no(group->line_no);

  lib_builder->set_obj(lib_table.get());

  current_table->addTable(std::move(lib_table));

  unsigned is_ok = visitStmtInGroup(group);

  lib_builder->set_obj(nullptr);

  return is_ok;
}

/**
 * @brief Visit the timing table group.
 *
 * @param group
 * @return unsigned
 */
unsigned LibertyReader::visitTable(LibertyGroupStmt* group) {
  LibBuilder* lib_builder = get_library_builder();

  const auto* const table_name = group->group_name;
  auto table_type = STR_TO_TABLE_TYPE(table_name);
  auto* lib_arc = lib_builder->get_arc();
  auto* lib_model = lib_builder->get_table_model();
  std::unique_ptr<LibTableModel> table_model;

  if (!lib_model) {
    if (lib_arc->isCheckArc()) {
      table_model = std::make_unique<LibCheckTableModel>();
    } else {
      table_model = std::make_unique<LibDelayTableModel>();
    }

    table_model->set_file_name(group->file_name);
    table_model->set_line_no(group->line_no);

    lib_builder->set_table_model(table_model.get());
    lib_model = lib_builder->get_table_model();
    lib_arc->set_table_model(std::move(table_model));
  }

  const char* table_template_name = getGroupAttriName(group);
  auto* the_lib = lib_builder->get_lib();
  auto* lut_template = the_lib->getLutTemplate(table_template_name);
  // LOG_FATAL_IF(!lut_template) << "not found template " <<
  // table_template_name;

  auto lib_table = std::make_unique<LibTable>(table_type, lut_template);
  lib_table->set_file_name(group->file_name);
  lib_table->set_line_no(group->line_no);

  lib_builder->set_table(lib_table.get());

  lib_model->addTable(std::move(lib_table));

  unsigned is_ok = visitStmtInGroup(group);
  return is_ok;
}

/**
 * @brief Visit the power table group.
 *
 * @param group
 * @return unsigned
 */
unsigned LibertyReader::visitPowerTable(LibertyGroupStmt* group) {
  LibBuilder* lib_builder = get_library_builder();

  const auto* const table_name = group->group_name;
  auto table_type = STR_TO_TABLE_TYPE(table_name);
  auto* lib_power_arc = lib_builder->get_power_arc();
  auto* lib_port = lib_builder->get_port();

  auto* lib_model = lib_builder->get_table_model();
  std::unique_ptr<LibTableModel> table_model;

  if (!lib_model) {
    table_model = std::make_unique<LibPowerTableModel>();

    lib_builder->set_table_model(table_model.get());
    lib_model = lib_builder->get_table_model();
    lib_power_arc->set_power_table_model(std::move(table_model));
  }

  const char* table_template_name = getGroupAttriName(group);
  auto* the_lib = lib_builder->get_lib();
  auto* lut_template = the_lib->getLutTemplate(table_template_name);
  // LOG_FATAL_IF(!lut_template) << "not found template " <<
  // table_template_name;

  auto lib_table = std::make_unique<LibTable>(table_type, lut_template);
  lib_table->set_file_name(group->file_name);
  lib_table->set_line_no(group->line_no);

  lib_builder->set_table(lib_table.get());

  // power_lib_model->addTable
  lib_model->addTable(std::move(lib_table));

  unsigned is_ok = visitStmtInGroup(group);

  return is_ok;
}

/**
 * @brief Visit the liberty group stmt to build liberty data.
 *
 * @param group
 * @return unsigned
 */
unsigned LibertyReader::visitGroup(LibertyGroupStmt* group) {
  unsigned is_ok = 1;
  const char* group_name = group->group_name;

  static const ieda::BTreeSet<std::string> table_names = {
      "cell_rise",       "cell_fall",       "rise_transition",
      "fall_transition", "rise_constraint", "fall_constraint"};
  static const ieda::BTreeSet<std::string> power_table_names = {"rise_power",
                                                                "fall_power"};

  using std::placeholders::_1;

  std::map<std::string, std::function<unsigned(LibertyGroupStmt * group)>>
      visit_fun_map = {
          {"library", std::bind(&LibertyReader::visitLibrary, this, _1)},
          {"wire_load", std::bind(&LibertyReader::visitWireLoad, this, _1)},
          {"lu_table_template",
           std::bind(&LibertyReader::visitLuTableTemplate, this, _1)},
          {"power_lut_template",
           std::bind(&LibertyReader::visitLuTableTemplate, this, _1)},
          {"type", std::bind(&LibertyReader::visitType, this, _1)},
          {"output_current_template",
           std::bind(&LibertyReader::visitOutputCurrentTemplate, this, _1)},
          {"cell", std::bind(&LibertyReader::visitCell, this, _1)},
          {"leakage_power",
           std::bind(&LibertyReader::visitLeakagePower, this, _1)},
          {"bus", std::bind(&LibertyReader::visitBus, this, _1)},
          {"bundle", std::bind(&LibertyReader::visitBus, this, _1)},
          {"pin", std::bind(&LibertyReader::visitPin, this, _1)},
          {"timing", std::bind(&LibertyReader::visitTiming, this, _1)},
          {"internal_power",
           std::bind(&LibertyReader::visitInternalPower, this, _1)},
          {"output_current_rise",
           std::bind(&LibertyReader::visitCurrentTable, this, _1)},
          {"output_current_fall",
           std::bind(&LibertyReader::visitCurrentTable, this, _1)},
          {"vector", std::bind(&LibertyReader::visitVector, this, _1)}};

  if (visit_fun_map.contains(group_name)) {
    auto read_func = visit_fun_map[group_name];
    is_ok = read_func(group);
  } else if (table_names.contains(group_name)) {
    is_ok = visitTable(group);
  } else if (power_table_names.contains(group_name)) {
    is_ok = visitPowerTable(group);
  } else {
    DLOG_INFO_EVERY_N(100000) << "group " << group_name << " is not supported.";
  }

  return 1;
}

/**
 * @brief Read the lib file use rust parser.
 *
 * @return unsigned
 */
unsigned LibertyReader::readLib() {
  LOG_INFO << "load liberty file " << _file_name;

  _lib_file = liberty_parse_lib(_file_name.c_str());

  if (!_lib_file) {
    LOG_INFO << "load liberty file " << _file_name << " failed.";
    return 0;
  }

  LOG_INFO << "load liberty file " << _file_name << " success.";
  return 1;
}

/**
 * @brief link the lib to construct the data.
 *
 * @return unsigned
 */
unsigned LibertyReader::linkLib() {
  LOG_INFO << "link liberty file " << _file_name << " start.";
  if (_lib_file) {
    auto* lib_group = liberty_convert_raw_group_stmt(_lib_file);
    unsigned result = visitGroup(lib_group);
    liberty_free_lib_group(_lib_file);

    LOG_INFO << "link liberty file " << _file_name << " success.";
    return result;
  }

  LOG_INFO << "link liberty file " << _file_name << " failed.";
  return 0;
}

}  // namespace ista
