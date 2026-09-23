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
#include "IRAnalyzer.hpp"

#include <Eigen/Sparse>
#include <Eigen/SparseLU>

#include "DataManager.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "PowerEdge.hpp"
#include "PowerNode.hpp"
#include "Utility.hpp"

namespace iemir {

// public

void IRAnalyzer::initInst()
{
  if (_ia_instance == nullptr) {
    _ia_instance = new IRAnalyzer();
  }
}

IRAnalyzer& IRAnalyzer::getInst()
{
  if (_ia_instance == nullptr) {
    EMIRLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_ia_instance;
}

void IRAnalyzer::destroyInst()
{
  if (_ia_instance != nullptr) {
    delete _ia_instance;
    _ia_instance = nullptr;
  }
}

// function

void IRAnalyzer::analyze()
{
  Monitor monitor;
  EMIRLOG.info(Loc::current(), "Starting...");

  std::string diagnostics_path = EMIRUTIL.getString(EMIRDM.getConfig().ia_temp_directory_path, "solver_diagnostics.csv");
  std::ofstream diagnostics_file(diagnostics_path);
  if (!diagnostics_file) {
    EMIRLOG.error(Loc::current(), "Cannot create IR solver diagnostics: ", diagnostics_path);
  }
  diagnostics_file << "net,net_type,node_count,edge_count,source_count,pin_mapped_current_a,generic_current_a,pin_current_coverage_pct,"
                      "load_current_a,source_current_a,current_balance_error_a,"
                      "current_balance_relative,max_abs_residual_a,l2_residual_a,relative_l2_residual,"
                      "source_node_load_current_a,total_load_current_a\n";
  analyzePowerGraphList(diagnostics_file);
  diagnostics_file.close();
  EMIRLOG.info(Loc::current(), "IR solver diagnostics: ", diagnostics_path);

  EMIRLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

IRAnalyzer* IRAnalyzer::_ia_instance = nullptr;

void IRAnalyzer::analyzePowerGraphList(std::ofstream& diagnostics_file)
{
  for (std::pair<const std::string, PowerGraph>& power_graph_pair : EMIRDM.getDatabase().get_power_graph_map()) {
    analyzePowerGraph(power_graph_pair.second, diagnostics_file);
  }
}

void IRAnalyzer::analyzePowerGraph(PowerGraph& power_graph, std::ofstream& diagnostics_file)
{
  IAModel ia_model = initIAModel(power_graph);
  solveNodeVoltage(power_graph, ia_model, diagnostics_file);
}

IAModel IRAnalyzer::initIAModel(PowerGraph& power_graph)
{
  IAModel ia_model;
  buildSourceVoltage(power_graph, ia_model);
  buildNodeMatrixIndex(power_graph, ia_model);
  buildNodeCurrentMap(power_graph, ia_model);
  return ia_model;
}

void IRAnalyzer::buildSourceVoltage(PowerGraph& power_graph, IAModel& ia_model)
{
  if (power_graph.get_net_type() == PowerNetType::kGround) {
    ia_model.set_source_voltage(0.0);
    power_graph.set_source_voltage(0.0);
    return;
  }
  double source_voltage = 0.0;
  Database& database = EMIRDM.getDatabase();
  for (std::pair<const uint64_t, std::vector<std::size_t>>& instance_node_pair : power_graph.get_instance_node_id_list_map()) {
    if (database.get_instance_power_map().count(instance_node_pair.first) == 0) {
      continue;
    }
    InstancePower& instance_power = database.get_instance_power_map()[instance_node_pair.first];
    if (instance_power.get_voltage() <= EMIR_ERROR) {
      continue;
    }
    if (source_voltage <= EMIR_ERROR) {
      source_voltage = instance_power.get_voltage();
    } else if (std::abs(source_voltage - instance_power.get_voltage()) > EMIR_ERROR) {
      EMIRLOG.error(Loc::current(), "The power graph contains inconsistent source voltages!");
    }
  }
  if (source_voltage <= EMIR_ERROR) {
    EMIRLOG.error(Loc::current(), "The power graph source voltage is unavailable!");
  }
  ia_model.set_source_voltage(source_voltage);
  power_graph.set_source_voltage(source_voltage);
}

void IRAnalyzer::buildNodeMatrixIndex(PowerGraph& power_graph, IAModel& ia_model)
{
  for (std::size_t source_node_id : power_graph.get_source_node_id_list()) {
    ia_model.get_source_node_id_set().insert(source_node_id);
  }
  for (PowerNode& power_node : power_graph.get_node_list()) {
    if (ia_model.get_source_node_id_set().count(power_node.get_node_id()) != 0) {
      continue;
    }
    std::size_t matrix_idx = ia_model.get_matrix_idx_to_node_id_list().size();
    ia_model.get_node_id_to_matrix_idx_map()[power_node.get_node_id()] = matrix_idx;
    ia_model.get_matrix_idx_to_node_id_list().push_back(power_node.get_node_id());
  }
}

void IRAnalyzer::buildNodeCurrentMap(PowerGraph& power_graph, IAModel& ia_model)
{
  Database& database = EMIRDM.getDatabase();
  for (std::pair<const uint64_t, std::vector<std::size_t>>& instance_node_pair : power_graph.get_instance_node_id_list_map()) {
    if (database.get_instance_power_map().count(instance_node_pair.first) == 0) {
      continue;
    }
    buildInstanceNodeCurrent(power_graph, instance_node_pair.first, database.get_instance_power_map()[instance_node_pair.first], ia_model);
  }
}

void IRAnalyzer::buildInstanceNodeCurrent(PowerGraph& power_graph, uint64_t instance_id, InstancePower& instance_power, IAModel& ia_model)
{
  double total_power = instance_power.get_total_power();
  if (!instance_power.get_has_average_current() && instance_power.get_voltage() <= EMIR_ERROR) {
    EMIRLOG.error(Loc::current(), "The instance power voltage is invalid!");
  }
  double instance_current = instance_power.get_has_average_current() ? instance_power.get_average_current()
                                                                    : total_power / instance_power.get_voltage();
  // PT-PX currents for small blocks commonly fall below the generic 1e-6
  // geometry/voltage tolerance. Only an actual zero means there is no load.
  if (instance_current == 0.0) {
    return;
  }
  int32_t power_graph_num = getInstancePowerGraphNum(instance_id, power_graph.get_net_type());
  if (power_graph_num <= 0) {
    EMIRLOG.error(Loc::current(), "The instance power graph mapping is invalid!");
  }
  double current_sign = power_graph.get_net_type() == PowerNetType::kPower ? -1.0 : 1.0;
  auto distribute_current = [&ia_model](const std::set<std::size_t>& node_ids, const std::map<std::size_t, double>& weights,
                                        double current) {
    double total_weight = 0.0;
    for (std::size_t node_id : node_ids) {
      auto weight = weights.find(node_id);
      if (weight != weights.end()) {
        total_weight += weight->second;
      }
    }
    for (std::size_t node_id : node_ids) {
      auto weight = weights.find(node_id);
      double fraction = total_weight > 0.0 ? (weight == weights.end() ? 0.0 : weight->second / total_weight) : 1.0 / node_ids.size();
      ia_model.get_node_current_map()[node_id] += current * fraction;
    }
  };
  if (power_graph.get_net_type() == PowerNetType::kPower && !instance_power.get_average_current_by_pin_name_map().empty()) {
    double mapped_pin_current = 0.0;
    for (const std::pair<const std::string, double>& pin_current_pair : instance_power.get_average_current_by_pin_name_map()) {
      auto key = std::make_pair(instance_id, pin_current_pair.first);
      auto pin_node_iter = power_graph.get_instance_pin_node_id_list_map().find(key);
      if (pin_node_iter == power_graph.get_instance_pin_node_id_list_map().end()) {
        continue;
      }
      std::set<std::size_t> pin_node_id_set;
      for (std::size_t node_id : pin_node_iter->second) {
        pin_node_id_set.insert(node_id);
      }
      if (pin_node_id_set.empty()) {
        continue;
      }
      distribute_current(pin_node_id_set, power_graph.get_instance_pin_node_weight_map()[key], current_sign * pin_current_pair.second);
      mapped_pin_current += pin_current_pair.second;
    }
    if (mapped_pin_current > 0.0) {
      ia_model.add_pin_mapped_current(mapped_pin_current);
      double relative_unmapped = std::abs(instance_current - mapped_pin_current) / std::max(std::abs(instance_current), 1.0e-30);
      if (relative_unmapped > 1.0e-4) {
        EMIRLOG.warn(Loc::current(), "PG-pin current mapping is incomplete for instance id ", instance_id, ": mapped=", mapped_pin_current,
                     " A, total=", instance_current, " A");
        instance_current -= mapped_pin_current;
      } else {
        return;
      }
    }
  }
  std::set<std::size_t> node_id_set;
  for (std::size_t node_id : power_graph.get_instance_node_id_list_map()[instance_id]) {
    node_id_set.insert(node_id);
  }
  if (node_id_set.empty()) {
    return;
  }
  ia_model.add_generic_current(instance_current / power_graph_num);
  // A load at an ideal source is supplied locally. Keep its original share;
  // the Dirichlet elimination below omits it from the unknown-node RHS.
  // Removing source nodes before normalization transfers that load elsewhere.
  distribute_current(node_id_set, power_graph.get_instance_node_weight_map()[instance_id], current_sign * instance_current / power_graph_num);
}

int32_t IRAnalyzer::getInstancePowerGraphNum(uint64_t instance_id, PowerNetType power_net_type)
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

void IRAnalyzer::solveNodeVoltage(PowerGraph& power_graph, IAModel& ia_model, std::ofstream& diagnostics_file)
{
  std::size_t matrix_size = ia_model.get_matrix_idx_to_node_id_list().size();
  std::vector<double> node_voltage_list(matrix_size, ia_model.get_source_voltage());
  if (matrix_size == 0) {
    updatePowerNodeVoltage(power_graph, ia_model, node_voltage_list);
    return;
  }

  Eigen::SparseMatrix<double> conductance_matrix(matrix_size, matrix_size);
  Eigen::VectorXd current_vector = Eigen::VectorXd::Zero(matrix_size);
  for (std::pair<const std::size_t, double>& node_current_pair : ia_model.get_node_current_map()) {
    if (ia_model.get_node_id_to_matrix_idx_map().count(node_current_pair.first) != 0) {
      current_vector[ia_model.get_node_id_to_matrix_idx_map()[node_current_pair.first]] += node_current_pair.second;
    }
  }

  std::vector<Eigen::Triplet<double>> conductance_triplet_list;
  for (PowerEdge& power_edge : power_graph.get_edge_list()) {
    if (power_edge.get_resistance() <= EMIR_ERROR) {
      EMIRLOG.error(Loc::current(), "The power edge resistance is invalid!");
    }
    std::size_t first_node_id = power_edge.get_first_node_id();
    std::size_t second_node_id = power_edge.get_second_node_id();
    bool first_is_source = ia_model.get_source_node_id_set().count(first_node_id) != 0;
    bool second_is_source = ia_model.get_source_node_id_set().count(second_node_id) != 0;
    double conductance = 1.0 / power_edge.get_resistance();

    if (!first_is_source) {
      std::size_t first_matrix_idx = ia_model.get_node_id_to_matrix_idx_map()[first_node_id];
      conductance_triplet_list.emplace_back(first_matrix_idx, first_matrix_idx, conductance);
      if (second_is_source) {
        current_vector[first_matrix_idx] += conductance * ia_model.get_source_voltage();
      } else {
        std::size_t second_matrix_idx = ia_model.get_node_id_to_matrix_idx_map()[second_node_id];
        conductance_triplet_list.emplace_back(first_matrix_idx, second_matrix_idx, -conductance);
      }
    }
    if (!second_is_source) {
      std::size_t second_matrix_idx = ia_model.get_node_id_to_matrix_idx_map()[second_node_id];
      conductance_triplet_list.emplace_back(second_matrix_idx, second_matrix_idx, conductance);
      if (!first_is_source) {
        std::size_t first_matrix_idx = ia_model.get_node_id_to_matrix_idx_map()[first_node_id];
        conductance_triplet_list.emplace_back(second_matrix_idx, first_matrix_idx, -conductance);
      } else {
        current_vector[second_matrix_idx] += conductance * ia_model.get_source_voltage();
      }
    }
  }
  conductance_matrix.setFromTriplets(conductance_triplet_list.begin(), conductance_triplet_list.end());

  Eigen::SparseLU<Eigen::SparseMatrix<double>> conductance_solver;
  conductance_solver.compute(conductance_matrix);
  if (conductance_solver.info() != Eigen::Success) {
    EMIRLOG.error(Loc::current(), "The power conductance matrix factorization failed!");
  }
  Eigen::VectorXd voltage_vector = conductance_solver.solve(current_vector);
  if (conductance_solver.info() != Eigen::Success) {
    EMIRLOG.error(Loc::current(), "The power conductance matrix solve failed!");
  }
  Eigen::VectorXd residual_vector = conductance_matrix * voltage_vector - current_vector;
  double max_abs_residual = residual_vector.size() == 0 ? 0.0 : residual_vector.cwiseAbs().maxCoeff();
  double l2_residual = residual_vector.norm();
  double relative_l2_residual = l2_residual / std::max(current_vector.norm(), 1.0e-30);

  double load_current = 0.0;
  double source_node_load_current = 0.0;
  for (const std::pair<const std::size_t, double>& node_current_pair : ia_model.get_node_current_map()) {
    if (ia_model.get_node_id_to_matrix_idx_map().count(node_current_pair.first) != 0) {
      load_current += node_current_pair.second;
    } else if (ia_model.get_source_node_id_set().count(node_current_pair.first) != 0) {
      source_node_load_current += node_current_pair.second;
    }
  }
  double current_balance_error = residual_vector.sum();
  double source_current = -load_current + current_balance_error;
  double current_balance_relative = std::abs(current_balance_error) / std::max(std::abs(load_current), 1.0e-30);
  double classified_current = ia_model.get_pin_mapped_current() + ia_model.get_generic_current();
  double pin_current_coverage = classified_current > 1.0e-30 ? 100.0 * ia_model.get_pin_mapped_current() / classified_current : 0.0;
  diagnostics_file << std::setprecision(12) << power_graph.get_net_name() << ','
                   << (power_graph.get_net_type() == PowerNetType::kPower ? "POWER" : "GROUND") << ',' << power_graph.get_node_list().size()
                   << ',' << power_graph.get_edge_list().size() << ',' << ia_model.get_source_node_id_set().size() << ','
                   << ia_model.get_pin_mapped_current() << ',' << ia_model.get_generic_current() << ',' << pin_current_coverage << ',' << load_current << ','
                   << source_current << ',' << current_balance_error << ',' << current_balance_relative << ',' << max_abs_residual << ','
                   << l2_residual << ',' << relative_l2_residual << ',' << source_node_load_current << ','
                   << load_current + source_node_load_current << '\n';
  EMIRLOG.info(Loc::current(), "IR solve ", power_graph.get_net_name(), ": relative_residual=", relative_l2_residual,
               ", current_balance=", current_balance_relative, ", load_current=", load_current, " A");
  if (!std::isfinite(relative_l2_residual) || !std::isfinite(current_balance_relative) || relative_l2_residual > 1.0e-6
      || current_balance_relative > 1.0e-4) {
    EMIRLOG.error(Loc::current(), "IR solve failed residual/current-balance validation for net ", power_graph.get_net_name());
  } else if (current_balance_relative > 1.0e-6) {
    EMIRLOG.warn(Loc::current(), "IR solve current-balance error exceeds 1 ppm for net ", power_graph.get_net_name(), ": ",
                 current_balance_relative);
  }

  for (std::size_t matrix_idx = 0; matrix_idx < matrix_size; matrix_idx++) {
    node_voltage_list[matrix_idx] = voltage_vector[matrix_idx];
  }
  updatePowerNodeVoltage(power_graph, ia_model, node_voltage_list);
}

void IRAnalyzer::updatePowerNodeVoltage(PowerGraph& power_graph, IAModel& ia_model, std::vector<double>& node_voltage_list)
{
  for (PowerNode& power_node : power_graph.get_node_list()) {
    if (ia_model.get_source_node_id_set().count(power_node.get_node_id()) != 0) {
      power_node.set_voltage(ia_model.get_source_voltage());
    } else {
      std::size_t matrix_idx = ia_model.get_node_id_to_matrix_idx_map()[power_node.get_node_id()];
      power_node.set_voltage(node_voltage_list[matrix_idx]);
    }
    if (ia_model.get_node_current_map().count(power_node.get_node_id()) != 0) {
      power_node.set_current(ia_model.get_node_current_map()[power_node.get_node_id()]);
    } else {
      power_node.set_current(0.0);
    }
  }
}

}  // namespace iemir
