// SPDX-License-Identifier: MulanPSL-2.0
#include "LibertyWriter.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "Lib.hh"
#include "Logger.hpp"
#include "Utility.hpp"
#include "liberty/LibertyLibrary.hpp"
#include "liberty/TimingModel.hpp"

namespace ista::liberty_writer {
namespace {
std::string getLibFilePath(CharacterizedLibrary& tc_lib, std::string& output_path);
void buildLibHeader(idb::LibLibrary& lib_library, LibertyLibrary& timing_library);
std::unique_ptr<idb::LibCell> buildLibCell(idb::LibLibrary& lib_library, CharacterizedLibrary& tc_lib);
void buildLibPortList(idb::LibCell& lib_cell, CharacterizedLibrary& tc_lib);
void buildLibPort(idb::LibCell& lib_cell, CharacterizedPort& tc_port);
void buildLibTimingArcList(idb::LibCell& lib_cell, CharacterizedLibrary& tc_lib);
void buildLibTimingArc(idb::LibCell& lib_cell, CharacterizedTimingArc& tc_timing_arc);
void buildLibDelayTableModel(idb::LibArc& lib_arc, CharacterizedTimingArc& tc_timing_arc);
void buildLibCheckTableModel(idb::LibArc& lib_arc, CharacterizedTimingArc& tc_timing_arc);
std::unique_ptr<idb::LibArc> makeLibArc(std::string& source_port, std::string& sink_port, std::string& timing_type);
std::unique_ptr<idb::LibTable> makeLibScalarTable(int32_t table_type, ScalarTable& tc_scalar_table);

std::string getLibFilePath(CharacterizedLibrary& tc_lib, std::string& output_path)
{
  return STAUTIL.getString(output_path, tc_lib.get_design_name(), "_", GetAnalysisTypeName()(tc_lib.get_analysis_type()), ".lib");
}

void buildLibHeader(idb::LibLibrary& lib_library, LibertyLibrary& timing_library)
{
  if (timing_library.get_has_library_info()) {
    if (timing_library.get_comment()) {
      lib_library.set_comment(*timing_library.get_comment());
    }
    if (timing_library.get_simulation()) {
      lib_library.set_simulation(*timing_library.get_simulation());
    }
    for (std::string& library_feature : timing_library.get_library_feature_list()) {
      lib_library.add_library_feature(library_feature);
    }
    if (timing_library.get_current_unit_name()) {
      lib_library.set_current_unit_name(*timing_library.get_current_unit_name());
    }
    if (timing_library.get_voltage_unit_name()) {
      lib_library.set_voltage_unit_name(*timing_library.get_voltage_unit_name());
    }
    if (timing_library.get_cap_unit() == TimingCapacitiveUnit::kFF) {
      lib_library.set_cap_unit(idb::CapacitiveUnit::kFF);
    } else if (timing_library.get_cap_unit() == TimingCapacitiveUnit::kF) {
      lib_library.set_cap_unit(idb::CapacitiveUnit::kF);
    } else {
      lib_library.set_cap_unit(idb::CapacitiveUnit::kPF);
    }
    if (timing_library.get_resistance_unit() == TimingResistanceUnit::kOHM) {
      lib_library.set_resistance_unit(idb::ResistanceUnit::kOHM);
    } else {
      lib_library.set_resistance_unit(idb::ResistanceUnit::kkOHM);
    }
    if (timing_library.get_time_unit() == TimingTimeUnit::kPS) {
      lib_library.set_time_unit(idb::TimeUnit::kPS);
    } else if (timing_library.get_time_unit() == TimingTimeUnit::kFS) {
      lib_library.set_time_unit(idb::TimeUnit::kFS);
    } else {
      lib_library.set_time_unit(idb::TimeUnit::kNS);
    }
    if (timing_library.get_default_max_transition()) {
      lib_library.set_default_max_transition(*timing_library.get_default_max_transition());
    }
    if (timing_library.get_default_max_fanout()) {
      lib_library.set_default_max_fanout(*timing_library.get_default_max_fanout());
    }
    if (timing_library.get_default_fanout_load()) {
      lib_library.set_default_fanout_load(*timing_library.get_default_fanout_load());
    }
    if (timing_library.get_nom_process()) {
      lib_library.set_nom_process(*timing_library.get_nom_process());
    }
    lib_library.set_nom_voltage(timing_library.get_nom_voltage());
    if (timing_library.get_nom_temperature()) {
      lib_library.set_nom_temperature(*timing_library.get_nom_temperature());
    }
    lib_library.set_slew_lower_threshold_pct_rise(timing_library.get_slew_lower_threshold_pct_rise() * 100.0);
    lib_library.set_slew_upper_threshold_pct_rise(timing_library.get_slew_upper_threshold_pct_rise() * 100.0);
    lib_library.set_slew_lower_threshold_pct_fall(timing_library.get_slew_lower_threshold_pct_fall() * 100.0);
    lib_library.set_slew_upper_threshold_pct_fall(timing_library.get_slew_upper_threshold_pct_fall() * 100.0);
    lib_library.set_input_threshold_pct_rise(timing_library.get_input_threshold_pct_rise() * 100.0);
    lib_library.set_output_threshold_pct_rise(timing_library.get_output_threshold_pct_rise() * 100.0);
    lib_library.set_input_threshold_pct_fall(timing_library.get_input_threshold_pct_fall() * 100.0);
    lib_library.set_output_threshold_pct_fall(timing_library.get_output_threshold_pct_fall() * 100.0);
    lib_library.set_slew_derate_from_library(timing_library.get_slew_derate_from_library());
  } else {
    lib_library.set_simulation(false);
    lib_library.set_time_unit(idb::TimeUnit::kNS);
    lib_library.set_cap_unit(idb::CapacitiveUnit::kPF);
    lib_library.set_resistance_unit(idb::ResistanceUnit::kkOHM);
  }
  if (lib_library.get_library_features().empty()) {
    lib_library.add_library_feature("report_delay_calculation");
  }
}

std::unique_ptr<idb::LibCell> buildLibCell(idb::LibLibrary& lib_library, CharacterizedLibrary& tc_lib)
{
  std::unique_ptr<idb::LibCell> lib_cell = std::make_unique<idb::LibCell>(tc_lib.get_design_name().c_str(), &lib_library);
  lib_cell->set_is_macro();
  lib_cell->set_cell_area(tc_lib.get_area());
  buildLibPortList(*lib_cell, tc_lib);
  buildLibTimingArcList(*lib_cell, tc_lib);
  return lib_cell;
}

void buildLibPortList(idb::LibCell& lib_cell, CharacterizedLibrary& tc_lib)
{
  for (std::pair<const std::string, CharacterizedPort>& port_pair : tc_lib.get_port_map()) {
    buildLibPort(lib_cell, port_pair.second);
  }
}

void buildLibPort(idb::LibCell& lib_cell, CharacterizedPort& tc_port)
{
  std::unique_ptr<idb::LibPort> lib_port = std::make_unique<idb::LibPort>(tc_port.get_port_name().c_str());
  lib_port->set_ower_cell(&lib_cell);
  if (tc_port.get_direction() == PinDirection::kInput) {
    lib_port->set_port_type(idb::LibPort::LibertyPortType::kInput);
  } else if (tc_port.get_direction() == PinDirection::kOutput) {
    lib_port->set_port_type(idb::LibPort::LibertyPortType::kOutput);
  } else if (tc_port.get_direction() == PinDirection::kInout) {
    lib_port->set_port_type(idb::LibPort::LibertyPortType::kInOut);
  } else {
    lib_port->set_port_type(idb::LibPort::LibertyPortType::kDefault);
  }
  lib_port->set_is_clock(tc_port.get_is_clock());
  lib_port->set_port_cap(tc_port.get_capacitance());
  lib_cell.addLibertyPort(std::move(lib_port));
}

void buildLibTimingArcList(idb::LibCell& lib_cell, CharacterizedLibrary& tc_lib)
{
  for (CharacterizedTimingArc& tc_timing_arc : tc_lib.get_timing_arc_list()) {
    buildLibTimingArc(lib_cell, tc_timing_arc);
  }
}

void buildLibTimingArc(idb::LibCell& lib_cell, CharacterizedTimingArc& tc_timing_arc)
{
  std::unique_ptr<idb::LibArc> lib_arc = makeLibArc(tc_timing_arc.get_source_port(), tc_timing_arc.get_sink_port(), tc_timing_arc.get_timing_type());
  if (tc_timing_arc.get_is_check_arc()) {
    buildLibCheckTableModel(*lib_arc, tc_timing_arc);
  } else {
    lib_arc->set_timing_sense(tc_timing_arc.get_timing_sense().c_str());
    buildLibDelayTableModel(*lib_arc, tc_timing_arc);
  }
  lib_arc->set_owner_cell(&lib_cell);
  lib_cell.addLibertyArc(std::move(lib_arc));
}

void buildLibDelayTableModel(idb::LibArc& lib_arc, CharacterizedTimingArc& tc_timing_arc)
{
  std::unique_ptr<idb::LibDelayTableModel> delay_table_model = std::make_unique<idb::LibDelayTableModel>();
  for (ScalarTable& tc_scalar_table : tc_timing_arc.get_scalar_table_list()) {
    if (tc_scalar_table.get_table_name() == "cell_rise") {
      delay_table_model->addTable(makeLibScalarTable(static_cast<int32_t>(idb::LibTable::TableType::kCellRise), tc_scalar_table));
    } else if (tc_scalar_table.get_table_name() == "cell_fall") {
      delay_table_model->addTable(makeLibScalarTable(static_cast<int32_t>(idb::LibTable::TableType::kCellFall), tc_scalar_table));
    } else if (tc_scalar_table.get_table_name() == "rise_transition") {
      delay_table_model->addTable(makeLibScalarTable(static_cast<int32_t>(idb::LibTable::TableType::kRiseTransition), tc_scalar_table));
    } else if (tc_scalar_table.get_table_name() == "fall_transition") {
      delay_table_model->addTable(makeLibScalarTable(static_cast<int32_t>(idb::LibTable::TableType::kFallTransition), tc_scalar_table));
    }
  }
  lib_arc.set_table_model(std::move(delay_table_model));
}

void buildLibCheckTableModel(idb::LibArc& lib_arc, CharacterizedTimingArc& tc_timing_arc)
{
  std::unique_ptr<idb::LibCheckTableModel> check_table_model = std::make_unique<idb::LibCheckTableModel>();
  for (ScalarTable& tc_scalar_table : tc_timing_arc.get_scalar_table_list()) {
    if (tc_scalar_table.get_table_name() == "rise_constraint") {
      check_table_model->addTable(makeLibScalarTable(static_cast<int32_t>(idb::LibTable::TableType::kRiseConstrain), tc_scalar_table));
    } else if (tc_scalar_table.get_table_name() == "fall_constraint") {
      check_table_model->addTable(makeLibScalarTable(static_cast<int32_t>(idb::LibTable::TableType::kFallConstrain), tc_scalar_table));
    }
  }
  lib_arc.set_table_model(std::move(check_table_model));
}

std::unique_ptr<idb::LibArc> makeLibArc(std::string& source_port, std::string& sink_port, std::string& timing_type)
{
  std::unique_ptr<idb::LibArc> lib_arc = std::make_unique<idb::LibArc>();
  lib_arc->set_src_port(source_port.c_str());
  lib_arc->set_snk_port(sink_port.c_str());
  lib_arc->set_timing_type(timing_type.c_str());
  return lib_arc;
}

std::unique_ptr<idb::LibTable> makeLibScalarTable(int32_t table_type, ScalarTable& tc_scalar_table)
{
  std::unique_ptr<idb::LibTable> lib_table = std::make_unique<idb::LibTable>(static_cast<idb::LibTable::TableType>(table_type), nullptr);
  lib_table->addTableValue(std::make_unique<idb::LibFloatValue>(tc_scalar_table.get_value()));
  return lib_table;
}

}  // namespace

void writeLib(TimingModel& tc_model, LibertyLibrary& timing_library, std::string& output_path)
{
  for (CharacterizedLibrary& tc_lib : tc_model.get_lib_list()) {
    std::string lib_file_path = getLibFilePath(tc_lib, output_path);
    std::unique_ptr<idb::LibLibrary> lib_library = buildLib(tc_lib, timing_library);
    lib_library->printLibertyLibrary(lib_file_path.c_str());
    STALOG.info(Loc::current(), "Output iSTA extracted lib: ", lib_file_path);
  }
}

std::unique_ptr<idb::LibLibrary> buildLib(CharacterizedLibrary& tc_lib, LibertyLibrary& timing_library)
{
  std::unique_ptr<idb::LibLibrary> lib_library = std::make_unique<idb::LibLibrary>(tc_lib.get_design_name().c_str());
  buildLibHeader(*lib_library, timing_library);
  lib_library->addLibertyCell(buildLibCell(*lib_library, tc_lib));
  return lib_library;
}

}  // namespace ista::liberty_writer
