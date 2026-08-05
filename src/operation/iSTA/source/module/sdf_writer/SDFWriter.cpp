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
#include "SDFWriter.hpp"

#include "Logger.hpp"
#include "Monitor.hpp"
#include "SDFTimingCheck.hpp"
#include "Utility.hpp"

namespace ista {

// public

void SDFWriter::initInst()
{
  if (_sw_instance == nullptr) {
    _sw_instance = new SDFWriter();
  }
}

SDFWriter& SDFWriter::getInst()
{
  if (_sw_instance == nullptr) {
    STALOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_sw_instance;
}

void SDFWriter::destroyInst()
{
  if (_sw_instance != nullptr) {
    delete _sw_instance;
    _sw_instance = nullptr;
  }
}

// function

void SDFWriter::write()
{
  write(getSDFFilePath());
}

void SDFWriter::write(const std::string_view file_path)
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  outputSDF(file_path);

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

SDFWriter* SDFWriter::_sw_instance = nullptr;

void SDFWriter::outputSDF(const std::string_view file_path)
{
  std::ofstream* sdf_file = STAUTIL.getOutputFileStream(file_path);
  outputSDFHeader(sdf_file);
  outputSDFInterconnect(sdf_file);
  outputSDFCellList(sdf_file);
  (*sdf_file) << ")\n";
  STAUTIL.closeFileStream(sdf_file);
}

std::string SDFWriter::getSDFFilePath()
{
  Database& database = STADM.getDatabase();
  return STAUTIL.getString(STADM.getConfig().sw_temp_directory_path, database.get_design_name(), ".sdf");
}

void SDFWriter::outputSDFHeader(std::ofstream* sdf_file)
{
  Database& database = STADM.getDatabase();
  TimingLibrary& timing_library = database.get_timing_library();
  (*sdf_file) << "(DELAYFILE\n";
  (*sdf_file) << " (SDFVERSION \"3.0\")\n";
  (*sdf_file) << " (DESIGN \"" << database.get_design_name() << "\")\n";

  std::time_t timestamp = std::time(nullptr);
  std::tm* local_time = std::localtime(&timestamp);
  if (local_time != nullptr) {
    char date_buffer[64] = {0};
    if (std::strftime(date_buffer, sizeof(date_buffer), "%a %b %d %H:%M:%S %Y", local_time) > 0) {
      (*sdf_file) << " (DATE \"" << date_buffer << "\")\n";
    }
  }

  (*sdf_file) << " (VENDOR \"ECC\")\n";
  (*sdf_file) << " (PROGRAM \"iSTA\")\n";
  (*sdf_file) << " (VERSION \"iSTA\")\n";
  (*sdf_file) << " (DIVIDER /)\n";
  if (timing_library.get_nom_voltage() > 0.0) {
    std::string nominal_voltage = getSDFNumberString(timing_library.get_nom_voltage());
    (*sdf_file) << " (VOLTAGE " << nominal_voltage << "::" << nominal_voltage << ")\n";
  }
  if (timing_library.get_nom_process()) {
    std::string nominal_process = getSDFNumberString(*timing_library.get_nom_process());
    (*sdf_file) << " (PROCESS \"" << nominal_process << "::" << nominal_process << "\")\n";
  }
  if (timing_library.get_nom_temperature()) {
    std::string nominal_temperature = getSDFNumberString(*timing_library.get_nom_temperature());
    (*sdf_file) << " (TEMPERATURE " << nominal_temperature << "::" << nominal_temperature << ")\n";
  }
  (*sdf_file) << " (TIMESCALE 1ns)\n";
}

void SDFWriter::outputSDFInterconnect(std::ofstream* sdf_file)
{
  Database& database = STADM.getDatabase();
  (*sdf_file) << " (CELL\n";
  (*sdf_file) << "  (CELLTYPE \"" << database.get_design_name() << "\")\n";
  (*sdf_file) << "  (INSTANCE)\n";
  (*sdf_file) << "  (DELAY\n";
  (*sdf_file) << "   (ABSOLUTE\n";
  for (Arc& arc : database.get_arc_list()) {
    if (arc.get_type() != ArcType::kNet || arc.get_is_disable_arc() || isSDFOutputOnlyCellArc(arc)) {
      continue;
    }
    outputSDFInterconnectArc(sdf_file, arc);
  }
  (*sdf_file) << "   )\n";
  (*sdf_file) << "  )\n";
  (*sdf_file) << " )\n";
}

void SDFWriter::outputSDFInterconnectArc(std::ofstream* sdf_file, Arc& arc)
{
  Database& database = STADM.getDatabase();
  if (database.get_pin_map().count(arc.get_source_pin()) == 0 || database.get_pin_map().count(arc.get_sink_pin()) == 0) {
    return;
  }
  SDFDelay sdf_delay = getSDFArcDelay(arc);
  if (!hasSDFDelay(sdf_delay)) {
    return;
  }
  std::string source_pin_name = getSDFPathName(database.get_pin_map()[arc.get_source_pin()]);
  std::string sink_pin_name = getSDFPathName(database.get_pin_map()[arc.get_sink_pin()]);
  (*sdf_file) << "    (INTERCONNECT " << source_pin_name << " " << sink_pin_name << " ";
  outputSDFDelay(sdf_file, sdf_delay);
  (*sdf_file) << ")\n";
}

bool SDFWriter::isSDFOutputOnlyCellArc(Arc& arc)
{
  Database& database = STADM.getDatabase();
  auto source_pin_it = database.get_pin_map().find(arc.get_source_pin());
  if (source_pin_it == database.get_pin_map().end() || source_pin_it->second.get_is_port()) {
    return false;
  }
  auto instance_it = database.get_instance_map().find(source_pin_it->second.get_instance_name());
  if (instance_it == database.get_instance_map().end()) {
    return false;
  }

  bool has_output_pin = false;
  for (std::string& pin_name : instance_it->second.get_pin_name_list()) {
    auto pin_it = database.get_pin_map().find(pin_name);
    if (pin_it == database.get_pin_map().end()) {
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

void SDFWriter::outputSDFCellList(std::ofstream* sdf_file)
{
  Database& database = STADM.getDatabase();
  buildInstanceCellArcMap();
  for (std::pair<const std::string, Instance>& instance_pair : database.get_instance_map()) {
    outputSDFCell(sdf_file, instance_pair.second);
  }
}

void SDFWriter::buildInstanceCellArcMap()
{
  Database& database = STADM.getDatabase();
  _sw_model.get_instance_cell_arc_map().clear();
  for (Arc& arc : database.get_arc_list()) {
    if (arc.get_type() != ArcType::kCell || arc.get_is_disable_arc()) {
      continue;
    }
    _sw_model.get_instance_cell_arc_map()[arc.get_owner_name()].push_back(&arc);
  }
}

void SDFWriter::outputSDFCell(std::ofstream* sdf_file, Instance& instance)
{
  if (!hasSDFCellContent(instance)) {
    return;
  }
  (*sdf_file) << " (CELL\n";
  (*sdf_file) << "  (CELLTYPE \"" << instance.get_cell_name() << "\")\n";
  (*sdf_file) << "  (INSTANCE " << getSDFInstanceName(instance) << ")\n";
  if (hasSDFCellDelay(instance)) {
    outputSDFCellDelay(sdf_file, instance);
  }
  if (hasSDFTimingCheck(instance)) {
    outputSDFTimingCheckList(sdf_file, instance);
  }
  (*sdf_file) << " )\n";
}

bool SDFWriter::hasSDFCellContent(Instance& instance)
{
  return hasSDFCellDelay(instance) || hasSDFTimingCheck(instance);
}

bool SDFWriter::hasSDFCellDelay(Instance& instance)
{
  if (_sw_model.get_instance_cell_arc_map().count(instance.get_instance_name()) > 0
      && !_sw_model.get_instance_cell_arc_map()[instance.get_instance_name()].empty()) {
    return true;
  }
  TimingCell* timing_cell = getTimingCell(instance);
  if (timing_cell == nullptr) {
    return false;
  }
  for (TimingCellArc& timing_cell_arc : timing_cell->get_cell_arc_list()) {
    if (!timing_cell_arc.get_is_timing_graph_arc() && !timing_cell_arc.get_is_disable_arc() && isSDFCellArc(instance, timing_cell_arc)) {
      return true;
    }
  }
  return false;
}

bool SDFWriter::hasSDFTimingCheck(Instance& instance)
{
  TimingCell* timing_cell = getTimingCell(instance);
  if (timing_cell == nullptr) {
    return false;
  }
  for (TimingCheckArc& timing_check_arc : timing_cell->get_sdf_check_arc_list()) {
    if (isSDFTimingCheck(instance, timing_check_arc)) {
      return true;
    }
  }
  return false;
}

void SDFWriter::outputSDFCellDelay(std::ofstream* sdf_file, Instance& instance)
{
  (*sdf_file) << "  (DELAY\n";
  (*sdf_file) << "   (ABSOLUTE\n";
  if (_sw_model.get_instance_cell_arc_map().count(instance.get_instance_name()) > 0) {
    for (Arc* arc : _sw_model.get_instance_cell_arc_map()[instance.get_instance_name()]) {
      outputSDFGraphCellArc(sdf_file, *arc);
    }
  }
  outputSDFOnlyCellArcList(sdf_file, instance);
  (*sdf_file) << "   )\n";
  (*sdf_file) << "  )\n";
}

void SDFWriter::outputSDFGraphCellArc(std::ofstream* sdf_file, Arc& arc)
{
  Database& database = STADM.getDatabase();
  if (database.get_pin_map().count(arc.get_source_pin()) == 0 || database.get_pin_map().count(arc.get_sink_pin()) == 0) {
    return;
  }
  std::string source_port_name = getSDFPortName(database.get_pin_map()[arc.get_source_pin()]);
  std::string sink_port_name = getSDFPortName(database.get_pin_map()[arc.get_sink_pin()]);
  TimingCellArc* timing_cell_arc = arc.get_timing_cell_arc();
  if (timing_cell_arc == nullptr || timing_cell_arc->get_timing_arc_list().empty()) {
    std::string condition;
    SDFDelay sdf_delay = getSDFArcDelay(arc);
    outputSDFCellArc(sdf_file, source_port_name, sink_port_name, condition, TransType::kNone, sdf_delay);
    return;
  }

  SDFCellArcDelayMap cell_arc_delay_map;
  for (TimingArc& timing_arc : timing_cell_arc->get_timing_arc_list()) {
    for (TransType input_trans_type : getSDFInputTransTypeList(timing_arc)) {
      SDFDelay sdf_delay = getSDFTimingArcDelay(arc, timing_arc, input_trans_type);
      mergeSDFCellArcDelay(cell_arc_delay_map, timing_arc, input_trans_type, sdf_delay);
    }
  }
  if (cell_arc_delay_map.empty()) {
    std::string condition;
    SDFDelay sdf_delay = getSDFArcDelay(arc);
    outputSDFCellArc(sdf_file, source_port_name, sink_port_name, condition, TransType::kNone, sdf_delay);
    return;
  }
  outputSDFCellArcDelayMap(sdf_file, source_port_name, sink_port_name, cell_arc_delay_map);
}

void SDFWriter::mergeSDFCellArcDelay(SDFCellArcDelayMap& cell_arc_delay_map, TimingArc& timing_arc, TransType input_trans_type,
                                     SDFDelay& sdf_delay)
{
  if (!hasSDFDelay(sdf_delay)) {
    return;
  }
  TransType sdf_input_trans_type = input_trans_type == TransType::kNone ? timing_arc.get_trigger_trans_type() : input_trans_type;
  SDFCellArcKey cell_arc_key{getSDFCondition(timing_arc), sdf_input_trans_type};
  mergeSDFDelay(cell_arc_delay_map[cell_arc_key], sdf_delay);
}

void SDFWriter::mergeSDFDelay(SDFDelay& target_delay, SDFDelay& source_delay)
{
  if (source_delay.get_rise_min_delay()) {
    target_delay.update(AnalysisType::kMin, TransType::kRise, *source_delay.get_rise_min_delay());
  }
  if (source_delay.get_rise_max_delay()) {
    target_delay.update(AnalysisType::kMax, TransType::kRise, *source_delay.get_rise_max_delay());
  }
  if (source_delay.get_fall_min_delay()) {
    target_delay.update(AnalysisType::kMin, TransType::kFall, *source_delay.get_fall_min_delay());
  }
  if (source_delay.get_fall_max_delay()) {
    target_delay.update(AnalysisType::kMax, TransType::kFall, *source_delay.get_fall_max_delay());
  }
}

void SDFWriter::outputSDFCellArcDelayMap(std::ofstream* sdf_file, std::string& source_port_name, std::string& sink_port_name,
                                         SDFCellArcDelayMap& cell_arc_delay_map)
{
  for (std::pair<const SDFCellArcKey, SDFDelay>& cell_arc_delay_pair : cell_arc_delay_map) {
    std::string condition = cell_arc_delay_pair.first.condition;
    TransType input_trans_type = cell_arc_delay_pair.first.input_trans_type;
    outputSDFCellArc(sdf_file, source_port_name, sink_port_name, condition, input_trans_type, cell_arc_delay_pair.second);
  }
}

void SDFWriter::outputSDFOnlyCellArcList(std::ofstream* sdf_file, Instance& instance)
{
  TimingCell* timing_cell = getTimingCell(instance);
  if (timing_cell == nullptr) {
    return;
  }
  for (TimingCellArc& timing_cell_arc : timing_cell->get_cell_arc_list()) {
    if (timing_cell_arc.get_is_timing_graph_arc() || timing_cell_arc.get_is_clear_preset_arc() || timing_cell_arc.get_is_disable_arc()
        || !isSDFCellArc(instance, timing_cell_arc)) {
      continue;
    }
    outputSDFOnlyCellArc(sdf_file, instance, timing_cell_arc);
  }
}

void SDFWriter::outputSDFOnlyCellArc(std::ofstream* sdf_file, Instance& instance, TimingCellArc& timing_cell_arc)
{
  std::string source_port_name = getSDFName(timing_cell_arc.get_source_port());
  std::string sink_port_name = getSDFName(timing_cell_arc.get_sink_port());
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    std::string condition;
    SDFDelay sdf_delay;
    sdf_delay.update(AnalysisType::kMin, TransType::kRise, timing_cell_arc.get_delay_min());
    sdf_delay.update(AnalysisType::kMax, TransType::kRise, timing_cell_arc.get_delay_max());
    sdf_delay.update(AnalysisType::kMin, TransType::kFall, timing_cell_arc.get_delay_min());
    sdf_delay.update(AnalysisType::kMax, TransType::kFall, timing_cell_arc.get_delay_max());
    outputSDFCellArc(sdf_file, source_port_name, sink_port_name, condition, TransType::kNone, sdf_delay);
    return;
  }

  SDFCellArcDelayMap cell_arc_delay_map;
  for (TimingArc& timing_arc : timing_cell_arc.get_timing_arc_list()) {
    for (TransType input_trans_type : getSDFInputTransTypeList(timing_arc)) {
      SDFDelay sdf_delay = getSDFTimingCellArcDelay(timing_cell_arc, timing_arc, input_trans_type);
      mergeSDFCellArcDelay(cell_arc_delay_map, timing_arc, input_trans_type, sdf_delay);
    }
  }
  outputSDFCellArcDelayMap(sdf_file, source_port_name, sink_port_name, cell_arc_delay_map);
}

void SDFWriter::outputSDFCellArc(std::ofstream* sdf_file, std::string& source_port_name, std::string& sink_port_name, std::string& condition,
                                 TransType input_trans_type, SDFDelay& sdf_delay)
{
  if (!hasSDFDelay(sdf_delay)) {
    return;
  }
  (*sdf_file) << "    ";
  if (!condition.empty()) {
    (*sdf_file) << "(COND " << condition << " ";
  }
  (*sdf_file) << "(IOPATH ";
  if (input_trans_type == TransType::kRise || input_trans_type == TransType::kFall) {
    (*sdf_file) << "(" << getSDFEdgeName(input_trans_type) << " " << source_port_name << ")";
  } else {
    (*sdf_file) << source_port_name;
  }
  (*sdf_file) << " " << sink_port_name << " ";
  outputSDFDelay(sdf_file, sdf_delay);
  (*sdf_file) << ")";
  if (!condition.empty()) {
    (*sdf_file) << ")";
  }
  (*sdf_file) << "\n";
}

void SDFWriter::outputSDFTimingCheckList(std::ofstream* sdf_file, Instance& instance)
{
  TimingCell* timing_cell = getTimingCell(instance);
  if (timing_cell == nullptr) {
    return;
  }
  (*sdf_file) << "  (TIMINGCHECK\n";
  for (TimingCheckArc& timing_check_arc : timing_cell->get_sdf_check_arc_list()) {
    if (!isSDFTimingCheck(instance, timing_check_arc)) {
      continue;
    }
    outputSDFTimingCheck(sdf_file, instance, timing_check_arc);
  }
  (*sdf_file) << "  )\n";
}

void SDFWriter::outputSDFTimingCheck(std::ofstream* sdf_file, Instance& instance, TimingCheckArc& timing_check_arc)
{
  if (timing_check_arc.get_check_type() == TimingCheckType::kNone) {
    return;
  }
  if (timing_check_arc.get_timing_arc_list().empty()) {
    TimingArc timing_arc;
    if (timing_check_arc.get_check_type() == TimingCheckType::kWidth) {
      outputSDFWidthTimingCheck(sdf_file, instance, timing_check_arc, timing_arc, TransType::kRise);
    } else if (timing_check_arc.get_check_type() == TimingCheckType::kPeriod) {
      outputSDFPeriodTimingCheck(sdf_file, instance, timing_check_arc, timing_arc);
    } else {
      outputSDFEdgeTimingCheck(sdf_file, instance, timing_check_arc, timing_arc, TransType::kRise);
    }
    return;
  }

  for (TimingArc& timing_arc : timing_check_arc.get_timing_arc_list()) {
    if (timing_check_arc.get_check_type() == TimingCheckType::kPeriod) {
      outputSDFPeriodTimingCheck(sdf_file, instance, timing_check_arc, timing_arc);
      continue;
    }
    bool has_transition = false;
    for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
      if (timing_arc.get_check_table_map().count(trans_type) == 0) {
        continue;
      }
      has_transition = true;
      if (timing_check_arc.get_check_type() == TimingCheckType::kWidth) {
        outputSDFWidthTimingCheck(sdf_file, instance, timing_check_arc, timing_arc, trans_type);
      } else {
        outputSDFEdgeTimingCheck(sdf_file, instance, timing_check_arc, timing_arc, trans_type);
      }
    }
    if (!has_transition) {
      if (timing_check_arc.get_check_type() == TimingCheckType::kWidth) {
        outputSDFWidthTimingCheck(sdf_file, instance, timing_check_arc, timing_arc, TransType::kRise);
      } else {
        outputSDFEdgeTimingCheck(sdf_file, instance, timing_check_arc, timing_arc, TransType::kRise);
      }
    }
  }
}

void SDFWriter::outputSDFEdgeTimingCheck(std::ofstream* sdf_file, Instance& instance, TimingCheckArc& timing_check_arc, TimingArc& timing_arc,
                                          TransType data_trans_type)
{
  Database& database = STADM.getDatabase();
  std::string timing_check_name = getSDFTimingCheckName(timing_check_arc.get_check_type());
  if (timing_check_name.empty()) {
    return;
  }
  std::string data_pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_data_port());
  std::string clock_pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_clock_port());
  if (database.get_pin_map().count(data_pin_name) == 0 || database.get_pin_map().count(clock_pin_name) == 0) {
    return;
  }
  std::string data_port_name = getSDFPortName(database.get_pin_map()[data_pin_name]);
  std::string clock_port_name = getSDFPortName(database.get_pin_map()[clock_pin_name]);
  std::string data_edge_name = getSDFEdgeName(data_trans_type);
  std::string clock_edge_name = getSDFEdgeName(timing_check_arc.get_clock_trans_type());
  std::string condition = getSDFCondition(timing_arc);
  double min_delay = getSDFTimingCheckDelay(instance, timing_check_arc, timing_arc, AnalysisType::kMin, data_trans_type);
  double max_delay = getSDFTimingCheckDelay(instance, timing_check_arc, timing_arc, AnalysisType::kMax, data_trans_type);
  // Timing analysis can occasionally report min_delay greater than max_delay; keep the SDF range ordered.
  if (min_delay > max_delay) {
    std::swap(min_delay, max_delay);
  }
  adjustSDFHoldTimingCheckDelay(instance, timing_check_arc, timing_arc, data_trans_type, min_delay, max_delay);
  (*sdf_file) << "    (" << timing_check_name << " ";
  if (!condition.empty()) {
    (*sdf_file) << "(COND " << condition << " ";
  }
  (*sdf_file) << "(" << data_edge_name << " " << data_port_name << ")";
  if (!condition.empty()) {
    (*sdf_file) << ")";
  }
  (*sdf_file) << " ";
  if (!condition.empty()) {
    (*sdf_file) << "(COND " << condition << " ";
  }
  (*sdf_file) << "(" << clock_edge_name << " " << clock_port_name << ")";
  if (!condition.empty()) {
    (*sdf_file) << ")";
  }
  (*sdf_file) << " ";
  outputSDFTriple(sdf_file, min_delay, max_delay);
  (*sdf_file) << ")\n";
}

void SDFWriter::outputSDFWidthTimingCheck(std::ofstream* sdf_file, Instance& instance, TimingCheckArc& timing_check_arc, TimingArc& timing_arc,
                                           TransType trans_type)
{
  Database& database = STADM.getDatabase();
  std::string pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_data_port());
  if (database.get_pin_map().count(pin_name) == 0) {
    return;
  }
  std::string port_name = getSDFPortName(database.get_pin_map()[pin_name]);
  std::string edge_name = getSDFEdgeName(trans_type);
  std::string condition = getSDFCondition(timing_arc);
  double min_delay = getSDFTimingCheckDelay(instance, timing_check_arc, timing_arc, AnalysisType::kMin, trans_type);
  double max_delay = getSDFTimingCheckDelay(instance, timing_check_arc, timing_arc, AnalysisType::kMax, trans_type);
  // Keep the WIDTH SDF triple ordered when the constraint table reverses the
  // numeric order of the analysis corners.
  if (min_delay > max_delay) {
    std::swap(min_delay, max_delay);
  }

  (*sdf_file) << "    (WIDTH ";
  if (!condition.empty()) {
    (*sdf_file) << "(COND " << condition << " ";
  }
  (*sdf_file) << "(" << edge_name << " " << port_name << ")";
  if (!condition.empty()) {
    (*sdf_file) << ")";
  }
  (*sdf_file) << " ";
  outputSDFTriple(sdf_file, min_delay, max_delay);
  (*sdf_file) << ")\n";
}

void SDFWriter::outputSDFPeriodTimingCheck(std::ofstream* sdf_file, Instance& instance, TimingCheckArc& timing_check_arc,
                                            TimingArc& timing_arc)
{
  Database& database = STADM.getDatabase();
  std::string pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_data_port());
  if (database.get_pin_map().count(pin_name) == 0) {
    return;
  }
  std::string port_name = getSDFPortName(database.get_pin_map()[pin_name]);
  double min_delay = getSDFTimingCheckDelay(instance, timing_check_arc, timing_arc, AnalysisType::kMin, TransType::kRise);
  double max_delay = getSDFTimingCheckDelay(instance, timing_check_arc, timing_arc, AnalysisType::kMax, TransType::kRise);

  (*sdf_file) << "    (PERIOD " << port_name << " ";
  outputSDFTriple(sdf_file, min_delay, max_delay);
  (*sdf_file) << ")\n";
}

void SDFWriter::adjustSDFHoldTimingCheckDelay(Instance& instance, TimingCheckArc& hold_timing_check_arc, TimingArc& hold_timing_arc,
                                               TransType data_trans_type, double& minimum_hold_delay, double& maximum_hold_delay)
{
  if (hold_timing_check_arc.get_check_type() != TimingCheckType::kHold) {
    return;
  }
  TimingCell* timing_cell = getTimingCell(instance);
  if (timing_cell == nullptr) {
    return;
  }
  TimingCheckArc* setup_timing_check_arc = findSDFSetupTimingCheck(*timing_cell, hold_timing_check_arc);
  if (setup_timing_check_arc == nullptr) {
    return;
  }
  TimingArc default_setup_timing_arc;
  TimingArc* setup_timing_arc = findSDFSetupTimingArc(*setup_timing_check_arc, hold_timing_arc, data_trans_type);
  if (setup_timing_arc == nullptr) {
    if (!setup_timing_check_arc->get_timing_arc_list().empty()) {
      return;
    }
    setup_timing_arc = &default_setup_timing_arc;
  }

  double minimum_setup_delay
      = getSDFTimingCheckDelay(instance, *setup_timing_check_arc, *setup_timing_arc, AnalysisType::kMin, data_trans_type);
  double maximum_setup_delay
      = getSDFTimingCheckDelay(instance, *setup_timing_check_arc, *setup_timing_arc, AnalysisType::kMax, data_trans_type);
  if (minimum_setup_delay > maximum_setup_delay) {
    std::swap(minimum_setup_delay, maximum_setup_delay);
  }

  if (ista::adjustSDFHoldTimingCheckDelay(minimum_setup_delay, minimum_hold_delay, maximum_hold_delay)) {
    STALOG.warn(Loc::current(), "Adjusted HOLD timing-check delay for ", instance.get_instance_name(), ": ", hold_timing_check_arc.get_data_port(),
                " relative to ", hold_timing_check_arc.get_clock_port(), " would otherwise sum to less than zero with the matching SETUP delay.");
  }
}

TimingCheckArc* SDFWriter::findSDFSetupTimingCheck(TimingCell& timing_cell, TimingCheckArc& hold_timing_check_arc)
{
  for (TimingCheckArc& timing_check_arc : timing_cell.get_sdf_check_arc_list()) {
    if (timing_check_arc.get_check_type() != TimingCheckType::kSetup) {
      continue;
    }
    if (timing_check_arc.get_data_port() == hold_timing_check_arc.get_data_port()
        && timing_check_arc.get_clock_port() == hold_timing_check_arc.get_clock_port()
        && timing_check_arc.get_clock_trans_type() == hold_timing_check_arc.get_clock_trans_type()) {
      return &timing_check_arc;
    }
  }
  return nullptr;
}

TimingArc* SDFWriter::findSDFSetupTimingArc(TimingCheckArc& setup_timing_check_arc, TimingArc& hold_timing_arc,
                                             TransType data_trans_type)
{
  for (TimingArc& timing_arc : setup_timing_check_arc.get_timing_arc_list()) {
    if (timing_arc.get_sdf_cond() != hold_timing_arc.get_sdf_cond()
        || timing_arc.get_check_trans_type() != hold_timing_arc.get_check_trans_type()) {
      continue;
    }
    if (timing_arc.get_check_table_map().empty()) {
      if (data_trans_type == TransType::kRise) {
        return &timing_arc;
      }
      continue;
    }
    if (timing_arc.get_check_table_map().count(data_trans_type) > 0) {
      return &timing_arc;
    }
  }
  return nullptr;
}

TimingCell* SDFWriter::getTimingCell(Instance& instance)
{
  Database& database = STADM.getDatabase();
  std::map<std::string, TimingCell>& timing_cell_map = database.get_timing_library().get_cell_map();
  if (timing_cell_map.count(instance.get_cell_name()) == 0) {
    return nullptr;
  }
  return &timing_cell_map[instance.get_cell_name()];
}

bool SDFWriter::isSDFCellArc(Instance& instance, TimingCellArc& timing_cell_arc)
{
  Database& database = STADM.getDatabase();
  std::string source_pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_cell_arc.get_source_port());
  std::string sink_pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_cell_arc.get_sink_port());
  return database.get_pin_map().count(source_pin_name) > 0 && database.get_pin_map().count(sink_pin_name) > 0;
}

bool SDFWriter::isSDFTimingCheck(Instance& instance, TimingCheckArc& timing_check_arc)
{
  Database& database = STADM.getDatabase();
  std::string data_pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_data_port());
  if (database.get_pin_map().count(data_pin_name) == 0) {
    return false;
  }
  if (timing_check_arc.get_check_type() == TimingCheckType::kWidth || timing_check_arc.get_check_type() == TimingCheckType::kPeriod) {
    return true;
  }
  std::string clock_pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_clock_port());
  return database.get_pin_map().count(clock_pin_name) > 0;
}

SDFDelay SDFWriter::getSDFArcDelay(Arc& arc, TransType input_trans_type)
{
  SDFDelay sdf_delay;
  if (!arc.get_graph_delay_map().empty()) {
    updateSDFDelay(sdf_delay, arc.get_graph_delay_map(), input_trans_type);
  }
  if (!hasSDFDelay(sdf_delay)) {
    updateSDFDelay(sdf_delay, arc.get_input_output_delay_map(), input_trans_type);
  }
  return sdf_delay;
}

SDFDelay SDFWriter::getSDFTimingArcDelay(Arc& arc, TimingArc& timing_arc, TransType input_trans_type)
{
  SDFDelay sdf_delay;
  if (arc.get_timing_arc_delay_map().count(timing_arc.get_arc_idx()) > 0) {
    updateSDFDelay(sdf_delay, arc.get_timing_arc_delay_map()[timing_arc.get_arc_idx()], input_trans_type);
  }
  if (!hasSDFDelay(sdf_delay)) {
    sdf_delay = getSDFArcDelay(arc, input_trans_type);
  }
  return sdf_delay;
}

SDFDelay SDFWriter::getSDFTimingCellArcDelay(TimingCellArc& timing_cell_arc, TimingArc& timing_arc, TransType input_trans_type)
{
  static_cast<void>(input_trans_type);
  SDFDelay sdf_delay;
  for (AnalysisType analysis_type : {AnalysisType::kMin, AnalysisType::kMax}) {
    for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
      if (timing_arc.get_delay_table_map().count(trans_type) == 0) {
        continue;
      }
      double delay = timing_arc.get_delay_table_map()[trans_type].findValue(0.0, 0.0) / timing_arc.get_time_unit_scale();
      sdf_delay.update(analysis_type, trans_type, delay);
    }
  }
  if (!hasSDFDelay(sdf_delay)) {
    sdf_delay.update(AnalysisType::kMin, TransType::kRise, timing_cell_arc.get_delay_min());
    sdf_delay.update(AnalysisType::kMax, TransType::kRise, timing_cell_arc.get_delay_max());
    sdf_delay.update(AnalysisType::kMin, TransType::kFall, timing_cell_arc.get_delay_min());
    sdf_delay.update(AnalysisType::kMax, TransType::kFall, timing_cell_arc.get_delay_max());
  }
  return sdf_delay;
}

void SDFWriter::updateSDFDelay(SDFDelay& sdf_delay, std::map<AnalysisType, std::map<TransType, std::map<TransType, double>>>& delay_map,
                               TransType input_trans_type)
{
  for (std::pair<const AnalysisType, std::map<TransType, std::map<TransType, double>>>& analysis_delay_pair : delay_map) {
    AnalysisType analysis_type = analysis_delay_pair.first;
    if (analysis_type != AnalysisType::kMin && analysis_type != AnalysisType::kMax) {
      continue;
    }
    for (std::pair<const TransType, std::map<TransType, double>>& input_delay_pair : analysis_delay_pair.second) {
      if (input_trans_type != TransType::kNone && input_delay_pair.first != input_trans_type) {
        continue;
      }
      for (std::pair<const TransType, double>& output_delay_pair : input_delay_pair.second) {
        if (output_delay_pair.first == TransType::kRise || output_delay_pair.first == TransType::kFall) {
          sdf_delay.update(analysis_type, output_delay_pair.first, output_delay_pair.second);
        }
      }
    }
  }
}

bool SDFWriter::hasSDFDelay(SDFDelay& sdf_delay)
{
  return sdf_delay.get_rise_min_delay() || sdf_delay.get_rise_max_delay() || sdf_delay.get_fall_min_delay() || sdf_delay.get_fall_max_delay();
}

double SDFWriter::getSDFTimingCheckDelay(Instance& instance, TimingCheckArc& timing_check_arc, TimingArc& timing_arc,
                                          AnalysisType analysis_type, TransType data_trans_type)
{
  if (timing_arc.get_check_table_map().count(data_trans_type) == 0) {
    return timing_check_arc.get_check_time();
  }
  std::string data_pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_data_port());
  double data_slew = getSDFSlew(data_pin_name, analysis_type, data_trans_type);
  double clock_slew = getSDFTimingCheckSlew(instance, timing_check_arc, analysis_type, data_trans_type);
  double delay = timing_arc.get_check_table_map()[data_trans_type].findValue(clock_slew * timing_arc.get_time_unit_scale(),
                                                                              data_slew * timing_arc.get_time_unit_scale());
  return delay / timing_arc.get_time_unit_scale();
}

double SDFWriter::getSDFTimingCheckSlew(Instance& instance, TimingCheckArc& timing_check_arc, AnalysisType analysis_type,
                                        TransType data_trans_type)
{
  if (timing_check_arc.get_check_type() == TimingCheckType::kWidth || timing_check_arc.get_check_type() == TimingCheckType::kPeriod) {
    std::string pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_data_port());
    return getSDFSlew(pin_name, analysis_type, data_trans_type);
  }
  std::string clock_pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_clock_port());
  return getSDFSlew(clock_pin_name, getCaptureAnalysisType(analysis_type), timing_check_arc.get_clock_trans_type());
}

double SDFWriter::getSDFSlew(std::string& pin_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = STADM.getDatabase();
  if (database.get_timing_point_map().count(pin_name) == 0) {
    return 0.0;
  }
  TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
  if (timing_point.get_clock_slew_map().count(analysis_type) > 0 && timing_point.get_clock_slew_map()[analysis_type].count(trans_type) > 0) {
    return timing_point.get_clock_slew_map()[analysis_type][trans_type];
  }
  if (timing_point.get_data_slew_map().count(analysis_type) > 0 && timing_point.get_data_slew_map()[analysis_type].count(trans_type) > 0) {
    return timing_point.get_data_slew_map()[analysis_type][trans_type];
  }
  return 0.0;
}

AnalysisType SDFWriter::getCaptureAnalysisType(AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMax) {
    return AnalysisType::kMin;
  }
  if (analysis_type == AnalysisType::kMin) {
    return AnalysisType::kMax;
  }
  return AnalysisType::kNone;
}

std::string SDFWriter::getSDFTimingCheckName(TimingCheckType timing_check_type)
{
  switch (timing_check_type) {
    case TimingCheckType::kSetup:
      return "SETUP";
    case TimingCheckType::kHold:
      return "HOLD";
    case TimingCheckType::kRecovery:
      return "RECOVERY";
    case TimingCheckType::kRemoval:
      return "REMOVAL";
    default:
      return "";
  }
}

std::string SDFWriter::getSDFPathName(Pin& pin)
{
  if (pin.get_is_port()) {
    return getSDFPortName(pin);
  }
  Database& database = STADM.getDatabase();
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  return STAUTIL.getString(getSDFInstanceName(instance), "/", getSDFPortName(pin));
}

std::string SDFWriter::getSDFPortName(Pin& pin)
{
  return getSDFName(pin.get_pin_name());
}

std::string SDFWriter::getSDFInstanceName(Instance& instance)
{
  return getSDFName(instance.get_instance_name());
}

std::string SDFWriter::getSDFName(std::string& name)
{
  std::string sdf_name;
  for (std::size_t index = 0; index < name.size(); index++) {
    char character = name[index];
    if (character == '\\' && index + 1 < name.size()) {
      sdf_name.push_back(character);
      sdf_name.push_back(name[++index]);
      continue;
    }
    bool is_name_character = std::isalnum(static_cast<unsigned char>(character)) || character == '_';
    if (!is_name_character) {
      sdf_name.push_back('\\');
    }
    sdf_name.push_back(character);
  }
  return sdf_name;
}

std::string SDFWriter::getSDFCondition(TimingArc& timing_arc)
{
  return timing_arc.get_sdf_cond();
}

std::vector<TransType> SDFWriter::getSDFInputTransTypeList(TimingArc& timing_arc)
{
  if (timing_arc.get_sense() == TimingArcSense::kNonUnate) {
    // Non-unate arcs require a distinct SDF IOPATH for each source edge.
    return {TransType::kRise, TransType::kFall};
  }
  return {TransType::kNone};
}

std::string SDFWriter::getSDFEdgeName(TransType trans_type)
{
  if (trans_type == TransType::kFall) {
    return "negedge";
  }
  return "posedge";
}

void SDFWriter::outputSDFDelay(std::ofstream* sdf_file, SDFDelay& sdf_delay)
{
  bool has_rise_delay = sdf_delay.get_rise_min_delay() || sdf_delay.get_rise_max_delay();
  bool has_fall_delay = sdf_delay.get_fall_min_delay() || sdf_delay.get_fall_max_delay();
  if (has_rise_delay) {
    double min_delay = sdf_delay.get_rise_min_delay() ? *sdf_delay.get_rise_min_delay() : *sdf_delay.get_rise_max_delay();
    double max_delay = sdf_delay.get_rise_max_delay() ? *sdf_delay.get_rise_max_delay() : min_delay;
    outputSDFTriple(sdf_file, min_delay, max_delay);
  } else {
    (*sdf_file) << "()";
  }
  if (has_fall_delay) {
    double min_delay = sdf_delay.get_fall_min_delay() ? *sdf_delay.get_fall_min_delay() : *sdf_delay.get_fall_max_delay();
    double max_delay = sdf_delay.get_fall_max_delay() ? *sdf_delay.get_fall_max_delay() : min_delay;
    (*sdf_file) << " ";
    outputSDFTriple(sdf_file, min_delay, max_delay);
  }
}

void SDFWriter::outputSDFTriple(std::ofstream* sdf_file, double min_delay, double max_delay)
{
  (*sdf_file) << "(" << getSDFNumberString(min_delay) << "::" << getSDFNumberString(max_delay) << ")";
}

std::string SDFWriter::getSDFNumberString(double value)
{
  if (std::abs(value) < 5E-13) {
    value = 0.0;
  }
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(10) << value;
  return oss.str();
}

}  // namespace ista
