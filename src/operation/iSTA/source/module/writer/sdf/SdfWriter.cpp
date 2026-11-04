// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "SdfWriter.hpp"

#include <ctime>
#include <fstream>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

namespace ista {

// public

// function

void SdfWriter::write()
{
  write(getSdfFilePath());
}

void SdfWriter::write(const std::string_view file_path)
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  auto sdf_file = STAUTIL.openOutputFile(file_path);
  write(sdf_file);
  STAUTIL.finishOutputFile(sdf_file);

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

void SdfWriter::write(std::ostream& sdf_file)
{
  outputSdfHeader(sdf_file);
  outputSdfInterconnect(sdf_file);
  outputSdfCellList(sdf_file);
  sdf_file << ")\n";
  if (!sdf_file) {
    throw std::runtime_error("Failed to write SDF");
  }
}

std::string SdfWriter::getSdfFilePath()
{
  Database& database = _database;
  return STAUTIL.getString(_config.sw_temp_directory_path, database.network().name(), ".sdf");
}

void SdfWriter::outputSdfHeader(std::ostream& sdf_file)
{
  Database& database = _database;
  LibertyLibrary& timing_library = database.liberty();
  sdf_file << "(DELAYFILE\n";
  sdf_file << " (SDFVERSION \"3.0\")\n";
  sdf_file << " (DESIGN \"" << database.network().name() << "\")\n";

  std::time_t timestamp = std::time(nullptr);
  std::tm* local_time = std::localtime(&timestamp);
  if (local_time != nullptr) {
    char date_buffer[64] = {0};
    if (std::strftime(date_buffer, sizeof(date_buffer), "%a %b %d %H:%M:%S %Y", local_time) > 0) {
      sdf_file << " (DATE \"" << date_buffer << "\")\n";
    }
  }

  sdf_file << " (VENDOR \"ECC\")\n";
  sdf_file << " (PROGRAM \"iSTA\")\n";
  sdf_file << " (VERSION \"iSTA\")\n";
  sdf_file << " (DIVIDER /)\n";
  if (timing_library.get_nom_voltage() > 0.0) {
    std::string nominal_voltage = _format.number(timing_library.get_nom_voltage());
    sdf_file << " (VOLTAGE " << nominal_voltage << "::" << nominal_voltage << ")\n";
  }
  if (timing_library.get_nom_process()) {
    std::string nominal_process = _format.number(*timing_library.get_nom_process());
    sdf_file << " (PROCESS \"" << nominal_process << "::" << nominal_process << "\")\n";
  }
  if (timing_library.get_nom_temperature()) {
    std::string nominal_temperature = _format.number(*timing_library.get_nom_temperature());
    sdf_file << " (TEMPERATURE " << nominal_temperature << "::" << nominal_temperature << ")\n";
  }
  sdf_file << " (TIMESCALE 1ns)\n";
}

void SdfWriter::outputSdfInterconnect(std::ostream& sdf_file)
{
  Database& database = _database;
  sdf_file << " (CELL\n";
  sdf_file << "  (CELLTYPE \"" << database.network().name() << "\")\n";
  sdf_file << "  (INSTANCE)\n";
  sdf_file << "  (DELAY\n";
  sdf_file << "   (ABSOLUTE\n";
  for (Arc& arc : database.graph().arcs()) {
    if (arc.get_type() != ArcType::kNet || arc.get_is_disable_arc() || isSdfOutputOnlyCellArc(arc)) {
      continue;
    }
    outputSdfInterconnectArc(sdf_file, arc);
  }
  sdf_file << "   )\n";
  sdf_file << "  )\n";
  sdf_file << " )\n";
}

void SdfWriter::outputSdfInterconnectArc(std::ostream& sdf_file, Arc& arc)
{
  Database& database = _database;
  if (database.network().pins().count(arc.get_source_pin()) == 0 || database.network().pins().count(arc.get_sink_pin()) == 0) {
    return;
  }
  SdfDelay sdf_delay = _values.arcDelay(arc);
  if (!_values.hasDelay(sdf_delay)) {
    return;
  }
  std::string source_pin_name = _format.pathName(database.network().pins()[arc.get_source_pin()]);
  std::string sink_pin_name = _format.pathName(database.network().pins()[arc.get_sink_pin()]);
  sdf_file << "    (INTERCONNECT " << source_pin_name << " " << sink_pin_name << " ";
  _format.delay(sdf_file, sdf_delay);
  sdf_file << ")\n";
}

bool SdfWriter::isSdfOutputOnlyCellArc(Arc& arc)
{
  Database& database = _database;
  auto source_pin_it = database.network().pins().find(arc.get_source_pin());
  if (source_pin_it == database.network().pins().end() || source_pin_it->second.get_is_port()) {
    return false;
  }
  auto instance_it = database.network().instances().find(source_pin_it->second.get_instance_name());
  if (instance_it == database.network().instances().end()) {
    return false;
  }

  bool has_output_pin = false;
  for (std::string& pin_name : instance_it->second.get_pin_name_list()) {
    auto pin_it = database.network().pins().find(pin_name);
    if (pin_it == database.network().pins().end()) {
      continue;
    }

    const PinDirection direction = pin_it->second.get_direction();
    if (direction == PinDirection::kInput || direction == PinDirection::kInout) {
      return false;
    }
    if (direction == PinDirection::kOutput) {
      has_output_pin = true;
    }
  }
  return has_output_pin;
}

void SdfWriter::outputSdfCellList(std::ostream& sdf_file)
{
  Database& database = _database;
  buildInstanceCellArcMap();
  for (std::pair<const std::string, Instance>& instance_pair : database.network().instances()) {
    outputSdfCell(sdf_file, instance_pair.second);
  }
}

void SdfWriter::buildInstanceCellArcMap()
{
  Database& database = _database;
  _instance_cell_arcs.clear();
  for (Arc& arc : database.graph().arcs()) {
    if (arc.get_type() != ArcType::kCell || arc.get_is_disable_arc()) {
      continue;
    }
    _instance_cell_arcs[arc.get_owner_name()].push_back(&arc);
  }
}

void SdfWriter::outputSdfCell(std::ostream& sdf_file, Instance& instance)
{
  if (!hasSdfCellContent(instance)) {
    return;
  }
  sdf_file << " (CELL\n";
  sdf_file << "  (CELLTYPE \"" << instance.get_cell_name() << "\")\n";
  sdf_file << "  (INSTANCE " << _format.instanceName(instance) << ")\n";
  if (hasSdfCellDelay(instance)) {
    outputSdfCellDelay(sdf_file, instance);
  }
  if (hasSdfTimingCheck(instance)) {
    outputSdfTimingCheckList(sdf_file, instance);
  }
  sdf_file << " )\n";
}

bool SdfWriter::hasSdfCellContent(Instance& instance)
{
  return hasSdfCellDelay(instance) || hasSdfTimingCheck(instance);
}

bool SdfWriter::hasSdfCellDelay(Instance& instance)
{
  if (_instance_cell_arcs.count(instance.get_instance_name()) > 0 && !_instance_cell_arcs[instance.get_instance_name()].empty()) {
    return true;
  }
  LibertyCell* timing_cell = _values.timingCell(instance);
  if (timing_cell == nullptr) {
    return false;
  }
  for (TimingCellArc& timing_cell_arc : timing_cell->get_cell_arc_list()) {
    if (!timing_cell_arc.get_is_timing_graph_arc() && !timing_cell_arc.get_is_disable_arc() && _values.hasCellArcPins(instance, timing_cell_arc)) {
      return true;
    }
  }
  return false;
}

bool SdfWriter::hasSdfTimingCheck(Instance& instance)
{
  LibertyCell* timing_cell = _values.timingCell(instance);
  if (timing_cell == nullptr) {
    return false;
  }
  for (TimingCheckArc& timing_check_arc : timing_cell->get_sdf_check_arc_list()) {
    if (_values.hasCheckPins(instance, timing_check_arc)) {
      return true;
    }
  }
  return false;
}

void SdfWriter::outputSdfCellDelay(std::ostream& sdf_file, Instance& instance)
{
  sdf_file << "  (DELAY\n";
  sdf_file << "   (ABSOLUTE\n";
  if (_instance_cell_arcs.count(instance.get_instance_name()) > 0) {
    for (Arc* arc : _instance_cell_arcs[instance.get_instance_name()]) {
      outputSdfGraphCellArc(sdf_file, *arc);
    }
  }
  outputSdfOnlyCellArcList(sdf_file, instance);
  sdf_file << "   )\n";
  sdf_file << "  )\n";
}

void SdfWriter::outputSdfGraphCellArc(std::ostream& sdf_file, Arc& arc)
{
  Database& database = _database;
  if (database.network().pins().count(arc.get_source_pin()) == 0 || database.network().pins().count(arc.get_sink_pin()) == 0) {
    return;
  }
  std::string source_port_name = _format.portName(database.network().pins()[arc.get_source_pin()]);
  std::string sink_port_name = _format.portName(database.network().pins()[arc.get_sink_pin()]);
  auto cell_arc_delay_map = _values.graphCellArcDelays(arc);
  outputSdfCellArcDelayMap(sdf_file, source_port_name, sink_port_name, cell_arc_delay_map);
}

void SdfWriter::outputSdfCellArcDelayMap(std::ostream& sdf_file, std::string& source_port_name, std::string& sink_port_name,
                                         SdfCellArcDelayMap& cell_arc_delay_map)
{
  for (std::pair<const SdfCellArcKey, SdfDelay>& cell_arc_delay_pair : cell_arc_delay_map) {
    std::string condition = cell_arc_delay_pair.first.condition;
    TransType input_trans_type = cell_arc_delay_pair.first.input_trans_type;
    if (cell_arc_delay_pair.first.retaining) {
      if (cell_arc_delay_map.contains({condition, input_trans_type, false}))
        continue;
      SdfDelay uncomputed_normal;
      outputSdfCellArc(sdf_file, source_port_name, sink_port_name, condition, input_trans_type, uncomputed_normal, &cell_arc_delay_pair.second);
      continue;
    }
    auto retain = cell_arc_delay_map.find({condition, input_trans_type, true});
    outputSdfCellArc(sdf_file, source_port_name, sink_port_name, condition, input_trans_type, cell_arc_delay_pair.second,
                     retain == cell_arc_delay_map.end() ? nullptr : &retain->second);
  }
}

void SdfWriter::outputSdfOnlyCellArcList(std::ostream& sdf_file, Instance& instance)
{
  LibertyCell* timing_cell = _values.timingCell(instance);
  if (timing_cell == nullptr) {
    return;
  }
  for (TimingCellArc& timing_cell_arc : timing_cell->get_cell_arc_list()) {
    if (timing_cell_arc.get_is_timing_graph_arc() || timing_cell_arc.get_is_clear_preset_arc() || timing_cell_arc.get_is_disable_arc()
        || !_values.hasCellArcPins(instance, timing_cell_arc)) {
      continue;
    }
    outputSdfOnlyCellArc(sdf_file, timing_cell_arc);
  }
}

void SdfWriter::outputSdfOnlyCellArc(std::ostream& sdf_file, TimingCellArc& timing_cell_arc)
{
  std::string source_port_name = _format.escapeName(timing_cell_arc.get_source_port());
  std::string sink_port_name = _format.escapeName(timing_cell_arc.get_sink_port());
  auto cell_arc_delay_map = _values.cellArcDelays(timing_cell_arc);
  outputSdfCellArcDelayMap(sdf_file, source_port_name, sink_port_name, cell_arc_delay_map);
}

void SdfWriter::outputSdfCellArc(std::ostream& sdf_file, std::string& source_port_name, std::string& sink_port_name, std::string& condition,
                                 TransType input_trans_type, SdfDelay& sdf_delay, SdfDelay* retaining)
{
  if (!_values.hasDelay(sdf_delay) && (!retaining || !_values.hasDelay(*retaining))) {
    return;
  }
  sdf_file << "    ";
  if (!condition.empty()) {
    sdf_file << "(COND " << condition << " ";
  }
  sdf_file << "(IOPATH ";
  if (input_trans_type == TransType::kRise || input_trans_type == TransType::kFall) {
    sdf_file << "(" << _format.edgeName(input_trans_type) << " " << source_port_name << ")";
  } else {
    sdf_file << source_port_name;
  }
  sdf_file << " " << sink_port_name << " ";
  if (retaining && _values.hasDelay(*retaining)) {
    sdf_file << "(RETAIN ";
    _format.delay(sdf_file, *retaining, true);
    sdf_file << ") ";
  }
  _format.delay(sdf_file, sdf_delay, retaining != nullptr);
  sdf_file << ")";
  if (!condition.empty()) {
    sdf_file << ")";
  }
  sdf_file << "\n";
}

void SdfWriter::outputSdfTimingCheckList(std::ostream& sdf_file, Instance& instance)
{
  LibertyCell* timing_cell = _values.timingCell(instance);
  if (timing_cell == nullptr) {
    return;
  }
  sdf_file << "  (TIMINGCHECK\n";
  for (TimingCheckArc& timing_check_arc : timing_cell->get_sdf_check_arc_list()) {
    if (!_values.hasCheckPins(instance, timing_check_arc)) {
      continue;
    }
    outputSdfTimingCheck(sdf_file, instance, timing_check_arc);
  }
  sdf_file << "  )\n";
}

void SdfWriter::outputSdfTimingCheck(std::ostream& sdf_file, Instance& instance, TimingCheckArc& timing_check_arc)
{
  if (timing_check_arc.get_check_type() == TimingCheckType::kNone) {
    return;
  }
  if (timing_check_arc.get_timing_arc_list().empty()) {
    TimingArc timing_arc;
    if (timing_check_arc.get_check_type() == TimingCheckType::kWidth) {
      outputSdfWidthTimingCheck(sdf_file, instance, timing_check_arc, timing_arc, TransType::kRise);
    } else if (timing_check_arc.get_check_type() == TimingCheckType::kPeriod) {
      outputSdfPeriodTimingCheck(sdf_file, instance, timing_check_arc, timing_arc);
    } else {
      outputSdfEdgeTimingCheck(sdf_file, instance, timing_check_arc, timing_arc, TransType::kRise);
    }
    return;
  }

  for (TimingArc& timing_arc : timing_check_arc.get_timing_arc_list()) {
    if (timing_check_arc.get_check_type() == TimingCheckType::kPeriod) {
      outputSdfPeriodTimingCheck(sdf_file, instance, timing_check_arc, timing_arc);
      continue;
    }
    bool has_transition = false;
    for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
      if (timing_arc.get_check_table_map().count(trans_type) == 0) {
        continue;
      }
      has_transition = true;
      if (timing_check_arc.get_check_type() == TimingCheckType::kWidth) {
        outputSdfWidthTimingCheck(sdf_file, instance, timing_check_arc, timing_arc, trans_type);
      } else {
        outputSdfEdgeTimingCheck(sdf_file, instance, timing_check_arc, timing_arc, trans_type);
      }
    }
    if (!has_transition) {
      if (timing_check_arc.get_check_type() == TimingCheckType::kWidth) {
        outputSdfWidthTimingCheck(sdf_file, instance, timing_check_arc, timing_arc, TransType::kRise);
      } else {
        outputSdfEdgeTimingCheck(sdf_file, instance, timing_check_arc, timing_arc, TransType::kRise);
      }
    }
  }
}

void SdfWriter::outputSdfEdgeTimingCheck(std::ostream& sdf_file, Instance& instance, TimingCheckArc& timing_check_arc, TimingArc& timing_arc,
                                         TransType data_trans_type)
{
  Database& database = _database;
  std::string timing_check_name = _format.checkName(timing_check_arc.get_check_type());
  if (timing_check_name.empty()) {
    return;
  }
  std::string data_pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_data_port());
  std::string clock_pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_clock_port());
  if (database.network().pins().count(data_pin_name) == 0 || database.network().pins().count(clock_pin_name) == 0) {
    return;
  }
  std::string data_port_name = _format.portName(database.network().pins()[data_pin_name]);
  std::string clock_port_name = _format.portName(database.network().pins()[clock_pin_name]);
  std::string data_edge_name = _format.edgeName(data_trans_type);
  std::string clock_edge_name = _format.edgeName(timing_check_arc.get_clock_trans_type());
  std::string condition = _format.condition(timing_arc);
  const auto [min_delay, max_delay] = _values.checkValues(instance, timing_check_arc, timing_arc, data_trans_type);
  sdf_file << "    (" << timing_check_name << " ";
  if (!condition.empty()) {
    sdf_file << "(COND " << condition << " ";
  }
  sdf_file << "(" << data_edge_name << " " << data_port_name << ")";
  if (!condition.empty()) {
    sdf_file << ")";
  }
  sdf_file << " ";
  if (!condition.empty()) {
    sdf_file << "(COND " << condition << " ";
  }
  sdf_file << "(" << clock_edge_name << " " << clock_port_name << ")";
  if (!condition.empty()) {
    sdf_file << ")";
  }
  sdf_file << " ";
  _format.triple(sdf_file, min_delay, max_delay);
  sdf_file << ")\n";
}

void SdfWriter::outputSdfWidthTimingCheck(std::ostream& sdf_file, Instance& instance, TimingCheckArc& timing_check_arc, TimingArc& timing_arc,
                                          TransType trans_type)
{
  Database& database = _database;
  std::string pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_data_port());
  if (database.network().pins().count(pin_name) == 0) {
    return;
  }
  std::string port_name = _format.portName(database.network().pins()[pin_name]);
  std::string edge_name = _format.edgeName(trans_type);
  std::string condition = _format.condition(timing_arc);
  const auto [min_delay, max_delay] = _values.checkValues(instance, timing_check_arc, timing_arc, trans_type);
  sdf_file << "    (WIDTH ";
  if (!condition.empty()) {
    sdf_file << "(COND " << condition << " ";
  }
  sdf_file << "(" << edge_name << " " << port_name << ")";
  if (!condition.empty()) {
    sdf_file << ")";
  }
  sdf_file << " ";
  _format.triple(sdf_file, min_delay, max_delay);
  sdf_file << ")\n";
}

void SdfWriter::outputSdfPeriodTimingCheck(std::ostream& sdf_file, Instance& instance, TimingCheckArc& timing_check_arc, TimingArc& timing_arc)
{
  Database& database = _database;
  std::string pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_data_port());
  if (database.network().pins().count(pin_name) == 0) {
    return;
  }
  std::string port_name = _format.portName(database.network().pins()[pin_name]);
  std::string condition = _format.condition(timing_arc);
  const auto [min_delay, max_delay] = _values.checkValues(instance, timing_check_arc, timing_arc, TransType::kRise);
  sdf_file << "    (PERIOD ";
  if (!condition.empty()) {
    sdf_file << "(COND " << condition << " ";
  }
  sdf_file << port_name;
  if (!condition.empty()) {
    sdf_file << ")";
  }
  sdf_file << " ";
  _format.triple(sdf_file, min_delay, max_delay);
  sdf_file << ")\n";
}

}  // namespace ista
