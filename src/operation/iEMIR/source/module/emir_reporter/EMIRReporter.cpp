// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "EMIRReporter.hpp"

#include "DataManager.hpp"
#include "InstancePower.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "PowerEdge.hpp"
#include "PowerEdgeType.hpp"
#include "PowerNet.hpp"
#include "PowerNetType.hpp"
#include "PowerNode.hpp"
#include "PowerVia.hpp"
#include "PowerWireSegment.hpp"
#include "Utility.hpp"

namespace {

struct IRReportRow
{
  double severity = 0.0;
  double voltage = 0.0;
  double ideal_voltage = 0.0;
  std::string net_name;
  double x = 0.0;
  double y = 0.0;
  std::string layer_name;
};

struct EMReportRow
{
  bool is_via = false;
  std::size_t order = 0;
  std::string object_name;
  double first_x = 0.0;
  double first_y = 0.0;
  double second_x = 0.0;
  double second_y = 0.0;
  double em_ratio_percent = 0.0;
  double em_limit = 0.0;
  bool has_em_limit = false;
  double current = 0.0;
  std::string net_name;
  double width = 0.0;
  std::string cut_box;
  std::string direction;
  double blech_length = 0.0;
};

std::string formatFixed(double value, int32_t precision)
{
  if (std::abs(value) < 0.5 * std::pow(10.0, -precision)) {
    value = 0.0;
  }
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(precision) << value;
  return oss.str();
}

std::string formatCompactFixed(double value, int32_t precision)
{
  std::string value_text = formatFixed(value, precision);
  if (value_text.find('.') == std::string::npos) {
    return value_text;
  }
  while (!value_text.empty() && value_text.back() == '0') {
    value_text.pop_back();
  }
  if (!value_text.empty() && value_text.back() == '.') {
    value_text.pop_back();
  }
  if (value_text == "-0") {
    value_text = "0";
  }
  return value_text;
}

std::string formatScientific(double value, int32_t precision)
{
  std::ostringstream oss;
  oss << std::scientific << std::setprecision(precision) << std::abs(value);
  return oss.str();
}

std::string formatPercent(double value)
{
  if (std::abs(value) <= 1e-12) {
    return "0%";
  }
  return formatCompactFixed(value, 6) + "%";
}

}  // namespace

namespace iemir {

// public

void EMIRReporter::initInst()
{
  if (_er_instance == nullptr) {
    _er_instance = new EMIRReporter();
  }
}

EMIRReporter& EMIRReporter::getInst()
{
  if (_er_instance == nullptr) {
    EMIRLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_er_instance;
}

void EMIRReporter::destroyInst()
{
  if (_er_instance != nullptr) {
    delete _er_instance;
    _er_instance = nullptr;
  }
}

// function

void EMIRReporter::report()
{
  Monitor monitor;
  EMIRLOG.info(Loc::current(), "Starting...");

  ERModel er_model = initERModel();
  outputIRReport(er_model);
  outputEMReport(er_model);
  outputResNetworkReport();

  EMIRLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

EMIRReporter* EMIRReporter::_er_instance = nullptr;

ERModel EMIRReporter::initERModel()
{
  ERModel er_model;
  buildIRReportFilePath(er_model);
  buildEMReportFilePath(er_model);
  return er_model;
}

void EMIRReporter::buildIRReportFilePath(ERModel& er_model)
{
  er_model.set_ir_report_file_path(EMIRUTIL.getString(EMIRDM.getConfig().er_temp_directory_path, "ir.rpt"));
}

void EMIRReporter::buildEMReportFilePath(ERModel& er_model)
{
  er_model.set_em_report_file_path(EMIRUTIL.getString(EMIRDM.getConfig().er_temp_directory_path, "em.rpt"));
}

void EMIRReporter::outputIRReport(ERModel& er_model)
{
  outputIRReportFile(er_model.get_ir_report_file_path());
  outputIRReportFile(buildDesignReportFilePath(".ir.worst"));
}

void EMIRReporter::outputIRReportFile(const std::string& report_file_path)
{
  std::ofstream* ir_report_file = EMIRUTIL.getOutputFileStream(report_file_path);
  outputIRReportHeader(ir_report_file);
  outputIRReportRows(ir_report_file);
  EMIRUTIL.closeFileStream(ir_report_file);
}

void EMIRReporter::outputIRReportHeader(std::ofstream* ir_report_file)
{
  (*ir_report_file) << "# iEMIR RedHawk-compatible static IR report\n";
  (*ir_report_file) << "Design : " << EMIRDM.getDatabase().get_design_name() << "\n\n";
  (*ir_report_file) << "#Report locations (x, y) with worst voltage_drops/ground_bounces\n";
  (*ir_report_file) << "#voltage #ideal_volt   #net      #x_y_location     #layer_name\n";
}

void EMIRReporter::outputIRReportRows(std::ofstream* ir_report_file)
{
  std::vector<IRReportRow> ir_report_row_list;
  for (std::pair<const std::string, PowerGraph>& power_graph_pair : EMIRDM.getDatabase().get_power_graph_map()) {
    PowerGraph& power_graph = power_graph_pair.second;
    double ideal_voltage = power_graph.get_source_voltage();
    if (power_graph.get_net_type() == PowerNetType::kPower && ideal_voltage <= EMIR_ERROR) {
      ideal_voltage = getSupplyVoltage(power_graph);
    }
    for (PowerNode& power_node : power_graph.get_node_list()) {
      IRReportRow ir_report_row;
      ir_report_row.severity = getIRSeverity(power_graph, power_node);
      ir_report_row.voltage = power_node.get_voltage();
      ir_report_row.ideal_voltage = ideal_voltage;
      ir_report_row.net_name = power_graph.get_net_name();
      ir_report_row.x = getMicronValue(power_node.get_x());
      ir_report_row.y = getMicronValue(power_node.get_y());
      ir_report_row.layer_name = getLayerName(power_graph, power_node.get_layer_idx());
      ir_report_row_list.push_back(ir_report_row);
    }
  }
  std::sort(ir_report_row_list.begin(), ir_report_row_list.end(), [](const IRReportRow& lhs, const IRReportRow& rhs) {
    if (std::abs(lhs.severity - rhs.severity) > 1e-15) {
      return lhs.severity > rhs.severity;
    }
    if (lhs.net_name != rhs.net_name) {
      return lhs.net_name < rhs.net_name;
    }
    if (lhs.layer_name != rhs.layer_name) {
      return lhs.layer_name < rhs.layer_name;
    }
    if (std::abs(lhs.x - rhs.x) > 1e-9) {
      return lhs.x < rhs.x;
    }
    return lhs.y < rhs.y;
  });
  for (IRReportRow& ir_report_row : ir_report_row_list) {
    (*ir_report_file) << std::setw(13) << formatFixed(ir_report_row.voltage, 9) << " " << std::setw(13)
                      << formatFixed(ir_report_row.ideal_voltage, 9) << " " << std::setw(8) << ir_report_row.net_name << " ("
                      << std::setw(10) << formatFixed(ir_report_row.x, 3) << "," << std::setw(10) << formatFixed(ir_report_row.y, 3)
                      << ")  " << ir_report_row.layer_name << "\n";
  }
}

double EMIRReporter::getTotalPower(PowerGraph& power_graph)
{
  double total_power = 0.0;
  Database& database = EMIRDM.getDatabase();
  for (std::pair<const uint64_t, std::vector<std::size_t>>& instance_node_pair : power_graph.get_instance_node_id_list_map()) {
    if (database.get_instance_power_map().count(instance_node_pair.first) == 0) {
      continue;
    }
    int32_t power_graph_num = getInstancePowerGraphNum(instance_node_pair.first, power_graph.get_net_type());
    if (power_graph_num <= 0) {
      EMIRLOG.error(Loc::current(), "The instance power graph mapping is invalid!");
    }
    total_power += database.get_instance_power_map()[instance_node_pair.first].get_total_power() / power_graph_num;
  }
  return total_power;
}

double EMIRReporter::getSupplyVoltage(PowerGraph& power_graph)
{
  if (power_graph.get_net_type() == PowerNetType::kPower) {
    return power_graph.get_source_voltage();
  }
  Database& database = EMIRDM.getDatabase();
  double supply_voltage = 0.0;
  for (std::pair<const uint64_t, std::vector<std::size_t>>& instance_node_pair : power_graph.get_instance_node_id_list_map()) {
    if (database.get_instance_power_map().count(instance_node_pair.first) == 0) {
      continue;
    }
    double instance_voltage = database.get_instance_power_map()[instance_node_pair.first].get_voltage();
    if (instance_voltage <= EMIR_ERROR) {
      continue;
    }
    if (supply_voltage <= EMIR_ERROR) {
      supply_voltage = instance_voltage;
    } else if (std::abs(supply_voltage - instance_voltage) > EMIR_ERROR) {
      EMIRLOG.error(Loc::current(), "The power graph contains inconsistent supply voltages!");
    }
  }
  return supply_voltage;
}

int32_t EMIRReporter::getInstancePowerGraphNum(uint64_t instance_id, PowerNetType power_net_type)
{
  int32_t power_graph_num = 0;
  for (std::pair<const std::string, PowerGraph>& power_graph_pair : EMIRDM.getDatabase().get_power_graph_map()) {
    PowerGraph& power_graph = power_graph_pair.second;
    if (power_graph.get_net_type() == power_net_type && power_graph.get_instance_node_id_list_map().count(instance_id) != 0) {
      power_graph_num++;
    }
  }
  return power_graph_num;
}

double EMIRReporter::getWorstVoltage(PowerGraph& power_graph)
{
  if (power_graph.get_node_list().empty()) {
    return 0.0;
  }
  double worst_voltage = power_graph.get_source_voltage();
  for (PowerNode& power_node : power_graph.get_node_list()) {
    if (power_graph.get_net_type() == PowerNetType::kGround) {
      worst_voltage = std::max(worst_voltage, power_node.get_voltage());
    } else {
      worst_voltage = std::min(worst_voltage, power_node.get_voltage());
    }
  }
  return worst_voltage;
}

double EMIRReporter::getAverageVoltage(PowerGraph& power_graph)
{
  if (power_graph.get_node_list().empty()) {
    return 0.0;
  }
  double total_voltage = 0.0;
  for (PowerNode& power_node : power_graph.get_node_list()) {
    total_voltage += power_node.get_voltage();
  }
  return total_voltage / power_graph.get_node_list().size();
}

double EMIRReporter::getWorstIRDrop(PowerGraph& power_graph)
{
  if (power_graph.get_net_type() == PowerNetType::kGround) {
    return getWorstVoltage(power_graph);
  }
  return power_graph.get_source_voltage() - getWorstVoltage(power_graph);
}

double EMIRReporter::getAverageIRDrop(PowerGraph& power_graph)
{
  if (power_graph.get_net_type() == PowerNetType::kGround) {
    return getAverageVoltage(power_graph);
  }
  return power_graph.get_source_voltage() - getAverageVoltage(power_graph);
}

double EMIRReporter::getPercentageDrop(PowerGraph& power_graph)
{
  double supply_voltage = getSupplyVoltage(power_graph);
  if (supply_voltage <= EMIR_ERROR) {
    return 0.0;
  }
  return 100.0 * getWorstIRDrop(power_graph) / supply_voltage;
}

void EMIRReporter::outputEMReport(ERModel& er_model)
{
  outputEMReportFile(er_model.get_em_report_file_path(), false);
  outputEMReportFile(buildDesignReportFilePath(".em"), false);
  outputEMReportFile(buildDesignReportFilePath(".em.worst"), true);
}

void EMIRReporter::outputEMReportFile(const std::string& report_file_path, bool worst_only)
{
  std::ofstream* em_report_file = EMIRUTIL.getOutputFileStream(report_file_path);
  outputEMReportHeader(em_report_file, worst_only);
  outputEMReportRows(em_report_file, worst_only);
  EMIRUTIL.closeFileStream(em_report_file);
}

void EMIRReporter::outputEMReportHeader(std::ofstream* em_report_file, bool worst_only)
{
  (*em_report_file) << "# iEMIR RedHawk-compatible static EM/current report\n";
  (*em_report_file) << "Design : " << EMIRDM.getDatabase().get_design_name() << "\n\n";
  (*em_report_file) << "# EM MODE is AVG\n";
  if (worst_only) {
    (*em_report_file)
        << "# This file reports EM/current rows in decreasing order. Unit used for coordinates and dimensions is um.\n\n";
    (*em_report_file) << "# For wires: #layer #end-to-end_coordinates #EM_Ratio #net #width #blech_length #current\n";
    (*em_report_file) << "# For vias: #via_name #x-y_coordinates #EM_Ratio #current #net #cut_box #direction #blech_length\n\n";
  } else {
    (*em_report_file) << "# This file reports the EM values and currents. Unit used for coordinates and dimensions is um.\n";
    (*em_report_file) << "# EM_Ratio is calculated from solved current and loaded RedHawk EM limits when available.\n\n";
    (*em_report_file) << "# For wires: #layer #end-to-end_coordinates #EM_Ratio #Current_value #net #width #blech_length\n";
    (*em_report_file) << "# For vias:  #via_name #x-y_coordinates #EM_Ratio #Current_value #net #via_cut_bounding_box #Direction #blech_length\n\n";
  }
}

void EMIRReporter::outputEMReportRows(std::ofstream* em_report_file, bool worst_only)
{
  std::vector<EMReportRow> em_report_row_list;
  std::size_t order = 0;
  for (std::pair<const std::string, PowerGraph>& power_graph_pair : EMIRDM.getDatabase().get_power_graph_map()) {
    PowerGraph& power_graph = power_graph_pair.second;
    for (PowerEdge& power_edge : power_graph.get_edge_list()) {
      if (!power_edge.get_is_em_checkable()) {
        continue;
      }
      if (power_edge.get_first_node_id() >= power_graph.get_node_list().size()
          || power_edge.get_second_node_id() >= power_graph.get_node_list().size()) {
        continue;
      }
      PowerNode& first_power_node = power_graph.get_node_list()[power_edge.get_first_node_id()];
      PowerNode& second_power_node = power_graph.get_node_list()[power_edge.get_second_node_id()];
      EMReportRow em_report_row;
      em_report_row.order = order++;
      em_report_row.is_via = power_edge.get_type() == PowerEdgeType::kVia;
      em_report_row.object_name = em_report_row.is_via ? getViaName(power_graph, power_edge)
                                                       : (power_edge.get_layer_name().empty() ? getLayerName(power_graph, power_edge.get_layer_idx())
                                                                                              : power_edge.get_layer_name());
      em_report_row.first_x = getMicronValue(first_power_node.get_x());
      em_report_row.first_y = getMicronValue(first_power_node.get_y());
      em_report_row.second_x = getMicronValue(second_power_node.get_x());
      em_report_row.second_y = getMicronValue(second_power_node.get_y());
      if (em_report_row.is_via) {
        auto [x, y] = getViaLocation(power_graph, power_edge);
        em_report_row.first_x = em_report_row.second_x = x;
        em_report_row.first_y = em_report_row.second_y = y;
      }
      em_report_row.em_ratio_percent = getEMRatioPercent(power_edge);
      em_report_row.em_limit = power_edge.get_em_limit();
      em_report_row.has_em_limit = power_edge.get_has_em_limit();
      em_report_row.current = std::abs(power_edge.get_current());
      em_report_row.net_name = power_graph.get_net_name();
      em_report_row.width = getMicronValue(power_edge.get_width());
      em_report_row.cut_box = getViaCutBox(power_graph, power_edge);
      em_report_row.direction = getViaDirection(power_graph, power_edge);
      em_report_row_list.push_back(em_report_row);
    }
  }
  if (worst_only) {
    std::sort(em_report_row_list.begin(), em_report_row_list.end(), [](const EMReportRow& lhs, const EMReportRow& rhs) {
      if (std::abs(lhs.em_ratio_percent - rhs.em_ratio_percent) > 1e-15) {
        return lhs.em_ratio_percent > rhs.em_ratio_percent;
      }
      if (std::abs(lhs.current - rhs.current) > 1e-18) {
        return lhs.current > rhs.current;
      }
      return lhs.order < rhs.order;
    });
  }
  for (EMReportRow& em_report_row : em_report_row_list) {
    if (em_report_row.is_via) {
      double x = em_report_row.first_x;
      double y = em_report_row.first_y;
      (*em_report_file) << "via " << em_report_row.object_name << "  (" << formatFixed(x, 3) << "," << formatFixed(y, 3) << ")\t"
                        << formatPercent(em_report_row.em_ratio_percent) << " " << formatScientific(em_report_row.current, 6) << " "
                        << em_report_row.net_name << "  " << em_report_row.cut_box << " " << em_report_row.direction << "  "
                        << formatFixed(em_report_row.blech_length, 3) << "\n";
      continue;
    }
    (*em_report_file) << em_report_row.object_name << "  (" << formatFixed(em_report_row.first_x, 3) << "," << formatFixed(em_report_row.first_y, 3)
                      << " " << formatFixed(em_report_row.second_x, 3) << "," << formatFixed(em_report_row.second_y, 3) << ")\t"
                      << formatPercent(em_report_row.em_ratio_percent);
    if (worst_only) {
      (*em_report_file) << "  " << em_report_row.net_name << "  " << formatCompactFixed(em_report_row.width, 6) << "  "
                        << formatFixed(em_report_row.blech_length, 3) << "  " << formatScientific(em_report_row.current, 6) << "\n";
    } else {
      (*em_report_file) << "\t" << formatScientific(em_report_row.current, 6) << "  " << em_report_row.net_name << "  "
                        << formatCompactFixed(em_report_row.width, 6) << "  " << formatFixed(em_report_row.blech_length, 3) << "\n";
    }
  }
}

void EMIRReporter::outputResNetworkReport()
{
  outputResNetworkReportFile(buildDesignReportFilePath(".res_network"));
  outputResNetworkReportFile(EMIRUTIL.getString(EMIRDM.getConfig().er_temp_directory_path, "res_network.rpt"));
}

void EMIRReporter::outputResNetworkReportFile(const std::string& report_file_path)
{
  std::ofstream* res_network_report_file = EMIRUTIL.getOutputFileStream(report_file_path);
  outputResNetworkReportHeader(res_network_report_file);
  outputResNetworkReportRows(res_network_report_file);
  EMIRUTIL.closeFileStream(res_network_report_file);
}

void EMIRReporter::outputResNetworkReportHeader(std::ofstream* res_network_report_file)
{
  (*res_network_report_file) << "# iEMIR RedHawk-compatible resistance/current/EM network report\n";
  (*res_network_report_file) << "Design : " << EMIRDM.getDatabase().get_design_name() << "\n\n";
  (*res_network_report_file) << "# For Wire-segments\n";
  (*res_network_report_file)
      << "# <ID> <layer> <net> <start(x y) end(x y) (um)> <direction(l|r|u|d)> <current(A)> <em_limit(A)> <em(%)> <resistance(ohm)> "
         "<width(um)> <rule-name>\n";
  (*res_network_report_file) << "# For Vias\n";
  (*res_network_report_file)
      << "# <ID> <layer> <via_name> <net> <coord(x y) (um)> <cut_#> <cut_area(um^2)> <resistance(ohm)> <current_dir(u|d)> "
         "<current(A)> <em_limit(A)> <em%> <rule-name>\n\n";
}

void EMIRReporter::outputResNetworkReportRows(std::ofstream* res_network_report_file)
{
  for (std::pair<const std::string, PowerGraph>& power_graph_pair : EMIRDM.getDatabase().get_power_graph_map()) {
    PowerGraph& power_graph = power_graph_pair.second;
    for (PowerEdge& power_edge : power_graph.get_edge_list()) {
      if (power_edge.get_first_node_id() >= power_graph.get_node_list().size()
          || power_edge.get_second_node_id() >= power_graph.get_node_list().size()) {
        continue;
      }
      PowerNode& first_power_node = power_graph.get_node_list()[power_edge.get_first_node_id()];
      PowerNode& second_power_node = power_graph.get_node_list()[power_edge.get_second_node_id()];
      if (power_edge.get_type() == PowerEdgeType::kWire) {
        std::string layer_name = power_edge.get_layer_name().empty() ? getLayerName(power_graph, power_edge.get_layer_idx()) : power_edge.get_layer_name();
        (*res_network_report_file)
            << power_edge.get_edge_id() << " " << layer_name << " " << power_graph.get_net_name() << " (" << formatFixed(getMicronValue(first_power_node.get_x()), 3)
            << " " << formatFixed(getMicronValue(first_power_node.get_y()), 3) << ") (" << formatFixed(getMicronValue(second_power_node.get_x()), 3)
            << " " << formatFixed(getMicronValue(second_power_node.get_y()), 3) << ") " << getWireDirection(power_graph, power_edge) << " "
            << formatScientific(power_edge.get_current(), 6) << " " << formatScientific(power_edge.get_em_limit(), 6) << " "
            << formatCompactFixed(power_edge.get_em_ratio_percent(), 6) << " " << formatScientific(power_edge.get_resistance(), 6) << " "
            << formatCompactFixed(getMicronValue(power_edge.get_width()), 6) << " "
            << (power_edge.get_em_rule_name().empty() ? "NA" : power_edge.get_em_rule_name()) << "\n";
      } else if (power_edge.get_type() == PowerEdgeType::kVia) {
        auto [x, y] = getViaLocation(power_graph, power_edge);
        std::string via_rule_name = power_edge.get_em_rule_name().empty() ? getViaName(power_graph, power_edge) : power_edge.get_em_rule_name();
        std::string direction = getViaDirection(power_graph, power_edge);
        if (direction == "UP") {
          direction = "u";
        } else if (direction == "DN") {
          direction = "d";
        }
        (*res_network_report_file)
            << power_edge.get_edge_id() << " " << via_rule_name << " " << getViaName(power_graph, power_edge) << " " << power_graph.get_net_name()
            << " (" << formatFixed(x, 3) << " " << formatFixed(y, 3) << ") " << power_edge.get_cut_num() << " "
            << formatCompactFixed(power_edge.get_cut_area_um2(), 9) << " " << formatScientific(power_edge.get_resistance(), 6) << " "
            << direction << " " << formatScientific(power_edge.get_current(), 6) << " " << formatScientific(power_edge.get_em_limit(), 6) << " "
            << formatCompactFixed(power_edge.get_em_ratio_percent(), 6) << " " << via_rule_name << "\n";
      }
    }
  }
}

double EMIRReporter::getMaxCurrent(PowerGraph& power_graph)
{
  double max_current = 0.0;
  for (PowerEdge& power_edge : power_graph.get_edge_list()) {
    max_current = std::max(max_current, std::abs(power_edge.get_current()));
  }
  return max_current;
}

double EMIRReporter::getAverageCurrent(PowerGraph& power_graph)
{
  if (power_graph.get_edge_list().empty()) {
    return 0.0;
  }
  double total_current = 0.0;
  for (PowerEdge& power_edge : power_graph.get_edge_list()) {
    total_current += std::abs(power_edge.get_current());
  }
  return total_current / power_graph.get_edge_list().size();
}

std::string EMIRReporter::buildDesignReportFilePath(const std::string& suffix)
{
  return EMIRUTIL.getString(EMIRDM.getConfig().er_temp_directory_path, EMIRDM.getDatabase().get_design_name(), suffix);
}

PowerVia* EMIRReporter::getPowerVia(PowerGraph& power_graph, PowerEdge& power_edge)
{
  if (power_edge.get_type() != PowerEdgeType::kVia || power_edge.get_first_node_id() >= power_graph.get_node_list().size()
      || power_edge.get_second_node_id() >= power_graph.get_node_list().size()) {
    return nullptr;
  }
  Database& database = EMIRDM.getDatabase();
  auto power_net_iter = database.get_power_net_map().find(power_graph.get_net_name());
  if (power_net_iter == database.get_power_net_map().end()) {
    return nullptr;
  }
  PowerNode& first_power_node = power_graph.get_node_list()[power_edge.get_first_node_id()];
  PowerNode& second_power_node = power_graph.get_node_list()[power_edge.get_second_node_id()];
  int32_t bottom_layer_idx = std::min(first_power_node.get_layer_idx(), second_power_node.get_layer_idx());
  int32_t top_layer_idx = std::max(first_power_node.get_layer_idx(), second_power_node.get_layer_idx());
  PowerNode& bottom_power_node = first_power_node.get_layer_idx() == bottom_layer_idx ? first_power_node : second_power_node;
  PowerNode& top_power_node = first_power_node.get_layer_idx() == top_layer_idx ? first_power_node : second_power_node;
  for (PowerVia& power_via : power_net_iter->second.get_via_list()) {
    if (power_via.get_bottom_layer_idx() == bottom_layer_idx && power_via.get_top_layer_idx() == top_layer_idx
        && power_via.get_bottom_x() == bottom_power_node.get_x() && power_via.get_bottom_y() == bottom_power_node.get_y()
        && power_via.get_top_x() == top_power_node.get_x() && power_via.get_top_y() == top_power_node.get_y()) {
      return &power_via;
    }
  }
  return nullptr;
}

std::pair<double, double> EMIRReporter::getViaLocation(PowerGraph& power_graph, PowerEdge& power_edge)
{
  // Electrical landing points can move to a connected wire junction. Reports
  // identify the physical via by its original location, without moving nodes.
  if (PowerVia* power_via = getPowerVia(power_graph, power_edge); power_via != nullptr) {
    return {getMicronValue(power_via->get_x()), getMicronValue(power_via->get_y())};
  }
  // Graphs constructed without physical via data retain the endpoint fallback.
  PowerNode& first = power_graph.get_node_list()[power_edge.get_first_node_id()];
  PowerNode& second = power_graph.get_node_list()[power_edge.get_second_node_id()];
  return {(getMicronValue(first.get_x()) + getMicronValue(second.get_x())) / 2.0,
          (getMicronValue(first.get_y()) + getMicronValue(second.get_y())) / 2.0};
}

std::string EMIRReporter::getLayerName(PowerGraph& power_graph, int32_t layer_idx)
{
  Database& database = EMIRDM.getDatabase();
  auto power_net_iter = database.get_power_net_map().find(power_graph.get_net_name());
  if (power_net_iter != database.get_power_net_map().end()) {
    for (PowerWireSegment& power_wire_segment : power_net_iter->second.get_wire_segment_list()) {
      if (power_wire_segment.get_layer_idx() == layer_idx && !power_wire_segment.get_layer_name().empty()) {
        return power_wire_segment.get_layer_name();
      }
    }
    for (PowerVia& power_via : power_net_iter->second.get_via_list()) {
      if (power_via.get_bottom_layer_idx() == layer_idx && !power_via.get_bottom_layer_name().empty()) {
        return power_via.get_bottom_layer_name();
      }
      if (power_via.get_top_layer_idx() == layer_idx && !power_via.get_top_layer_name().empty()) {
        return power_via.get_top_layer_name();
      }
    }
  }
  return EMIRUTIL.getString("LAYER", layer_idx);
}

std::string EMIRReporter::getViaName(PowerGraph& power_graph, PowerEdge& power_edge)
{
  if (!power_edge.get_via_name().empty()) {
    return power_edge.get_via_name();
  }
  PowerVia* power_via = getPowerVia(power_graph, power_edge);
  if (power_via != nullptr && !power_via->get_via_name().empty()) {
    return power_via->get_via_name();
  }
  if (power_edge.get_first_node_id() >= power_graph.get_node_list().size() || power_edge.get_second_node_id() >= power_graph.get_node_list().size()) {
    return "VIA_UNKNOWN";
  }
  PowerNode& first_power_node = power_graph.get_node_list()[power_edge.get_first_node_id()];
  PowerNode& second_power_node = power_graph.get_node_list()[power_edge.get_second_node_id()];
  int32_t bottom_layer_idx = std::min(first_power_node.get_layer_idx(), second_power_node.get_layer_idx());
  int32_t top_layer_idx = std::max(first_power_node.get_layer_idx(), second_power_node.get_layer_idx());
  return EMIRUTIL.getString("VIA_", getLayerName(power_graph, bottom_layer_idx), "_", getLayerName(power_graph, top_layer_idx));
}

std::string EMIRReporter::getViaCutBox(PowerGraph& power_graph, PowerEdge& power_edge)
{
  if (power_edge.get_cut_low_x() != 0 || power_edge.get_cut_low_y() != 0 || power_edge.get_cut_high_x() != 0 || power_edge.get_cut_high_y() != 0) {
    return EMIRUTIL.getString(formatFixed(getMicronValue(power_edge.get_cut_low_x()), 3), " ",
                              formatFixed(getMicronValue(power_edge.get_cut_low_y()), 3), " ",
                              formatFixed(getMicronValue(power_edge.get_cut_high_x()), 3), " ",
                              formatFixed(getMicronValue(power_edge.get_cut_high_y()), 3));
  }
  PowerVia* power_via = getPowerVia(power_graph, power_edge);
  if (power_via == nullptr) {
    return "0.000 0.000 0.000 0.000";
  }
  return EMIRUTIL.getString(formatFixed(getMicronValue(power_via->get_cut_low_x()), 3), " ",
                            formatFixed(getMicronValue(power_via->get_cut_low_y()), 3), " ",
                            formatFixed(getMicronValue(power_via->get_cut_high_x()), 3), " ",
                            formatFixed(getMicronValue(power_via->get_cut_high_y()), 3));
}

std::string EMIRReporter::getViaDirection(PowerGraph& power_graph, PowerEdge& power_edge)
{
  if (power_edge.get_first_node_id() >= power_graph.get_node_list().size() || power_edge.get_second_node_id() >= power_graph.get_node_list().size()) {
    return "NA";
  }
  PowerNode& first_power_node = power_graph.get_node_list()[power_edge.get_first_node_id()];
  PowerNode& second_power_node = power_graph.get_node_list()[power_edge.get_second_node_id()];
  bool current_from_first = first_power_node.get_voltage() >= second_power_node.get_voltage();
  int32_t from_layer_idx = current_from_first ? first_power_node.get_layer_idx() : second_power_node.get_layer_idx();
  int32_t to_layer_idx = current_from_first ? second_power_node.get_layer_idx() : first_power_node.get_layer_idx();
  if (to_layer_idx > from_layer_idx) {
    return "UP";
  }
  if (to_layer_idx < from_layer_idx) {
    return "DN";
  }
  return "NA";
}

std::string EMIRReporter::getWireDirection(PowerGraph& power_graph, PowerEdge& power_edge)
{
  if (power_edge.get_first_node_id() >= power_graph.get_node_list().size() || power_edge.get_second_node_id() >= power_graph.get_node_list().size()) {
    return "n";
  }
  PowerNode& first_power_node = power_graph.get_node_list()[power_edge.get_first_node_id()];
  PowerNode& second_power_node = power_graph.get_node_list()[power_edge.get_second_node_id()];
  bool current_from_first = first_power_node.get_voltage() >= second_power_node.get_voltage();
  PowerNode& from_node = current_from_first ? first_power_node : second_power_node;
  PowerNode& to_node = current_from_first ? second_power_node : first_power_node;
  if (std::abs(from_node.get_x() - to_node.get_x()) >= std::abs(from_node.get_y() - to_node.get_y())) {
    return to_node.get_x() >= from_node.get_x() ? "r" : "l";
  }
  return to_node.get_y() >= from_node.get_y() ? "u" : "d";
}

double EMIRReporter::getDBUToMicronRatio()
{
  int32_t micron_dbu = EMIRDM.getDatabase().get_micron_dbu();
  if (micron_dbu <= 0) {
    return 1.0;
  }
  return 1.0 / static_cast<double>(micron_dbu);
}

double EMIRReporter::getMicronValue(int32_t dbu_value)
{
  return static_cast<double>(dbu_value) * getDBUToMicronRatio();
}

double EMIRReporter::getIRSeverity(PowerGraph& power_graph, PowerNode& power_node)
{
  if (power_graph.get_net_type() == PowerNetType::kGround) {
    return std::abs(power_node.get_voltage() - power_graph.get_source_voltage());
  }
  return std::abs(power_graph.get_source_voltage() - power_node.get_voltage());
}

double EMIRReporter::getEMRatioPercent(PowerEdge& power_edge)
{
  if (power_edge.get_em_ratio_percent() <= 0.0) {
    return 0.0;
  }
  return power_edge.get_em_ratio_percent();
}

}  // namespace iemir
