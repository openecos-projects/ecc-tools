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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "RcModelBuilder.hpp"

#include <Eigen/Eigenvalues>
#include <Eigen/IterativeLinearSolvers>
#include <Eigen/SparseCore>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "DmpDelayCalculator.hpp"
#include "TimingTolerance.hpp"

namespace ista {
namespace {
// Ideal resistors identify voltages exactly. Contract their connected
// components before forming conductances, preserving parallel branches and
// every terminal's projection into the resulting network.
bool contractShorts(ParasiticNet& original, ParasiticNet& contracted, std::vector<std::string>& names,
                    std::vector<int32_t>& parents, std::vector<double>& resistances, std::vector<double>& capacitances,
                    std::vector<std::size_t>& terminals)
{
  const auto count = names.size();
  std::map<std::string, std::size_t> indices;
  std::vector<std::size_t> roots(count);
  for (std::size_t i = 0; i < count; ++i) {
    indices[names[i]] = i;
    roots[i] = i;
  }
  const auto root = [&](std::size_t node) {
    while (roots[node] != node) {
      roots[node] = roots[roots[node]];
      node = roots[node];
    }
    return node;
  };
  for (auto& resistor : original.get_resistor_list()) {
    if (!indices.contains(resistor.get_source_node()) || !indices.contains(resistor.get_sink_node()))
      continue;
    if (!(resistor.get_resistance() >= 0.) || !std::isfinite(resistor.get_resistance()))
      return false;
    if (resistor.get_resistance() == 0.) {
      const auto a = root(indices.at(resistor.get_source_node())), b = root(indices.at(resistor.get_sink_node()));
      roots[std::max(a, b)] = std::min(a, b);
    }
  }
  std::vector<double> merged_caps(count);
  for (std::size_t i = 0; i < count; ++i)
    merged_caps[root(i)] += capacitances[i];
  std::vector<std::vector<std::pair<std::size_t, double>>> adjacency(count);
  for (auto& resistor : original.get_resistor_list()) {
    if (!indices.contains(resistor.get_source_node()) || !indices.contains(resistor.get_sink_node()))
      continue;
    const auto a = root(indices.at(resistor.get_source_node())), b = root(indices.at(resistor.get_sink_node()));
    if (a == b)
      continue;
    adjacency[a].emplace_back(b, resistor.get_resistance() * 1e-3);
    adjacency[b].emplace_back(a, resistor.get_resistance() * 1e-3);
    auto branch = resistor;
    branch.set_source_node(names[a]);
    branch.set_sink_node(names[b]);
    contracted.get_resistor_list().push_back(std::move(branch));
  }
  std::vector<std::size_t> order{root(0)};
  std::vector<std::size_t> mapped(count, count);
  mapped[order.front()] = 0;
  parents = {-1};
  resistances = {0.};
  for (std::size_t i = 0; i < order.size(); ++i) {
    for (const auto& [next, resistance] : adjacency[order[i]]) {
      if (mapped[next] != count)
        continue;
      mapped[next] = order.size();
      order.push_back(next);
      parents.push_back(static_cast<int32_t>(i));
      resistances.push_back(resistance);
    }
  }
  for (auto& terminal : terminals) {
    terminal = mapped[root(terminal)];
    if (terminal == count)
      return false;
  }
  std::vector<std::string> merged_names;
  capacitances.clear();
  for (const auto group : order) {
    merged_names.push_back(names[group]);
    capacitances.push_back(merged_caps[group]);
  }
  names = std::move(merged_names);
  return true;
}
}  // namespace


ParasiticDmpModel& RcModelBuilder::dmpModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type, TransType trans_type)
{
  std::string dmp_model_key = getParasiticDmpModelKey(parasitic_net, source_node_name, analysis_type, trans_type);
  if (_parasitic_dmp_model_cache.count(dmp_model_key) == 0) {
    _parasitic_dmp_model_cache[dmp_model_key] = buildParasiticDmpModel(parasitic_net, source_node_name, analysis_type, trans_type);
  }
  return _parasitic_dmp_model_cache[dmp_model_key];
}

std::string RcModelBuilder::getParasiticDmpModelKey(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                                    TransType trans_type)
{
  std::stringstream key_stream;
  key_stream << parasitic_net.get_net_name() << "|" << source_node_name << "|" << static_cast<int32_t>(analysis_type) << "|"
             << static_cast<int32_t>(trans_type);
  return key_stream.str();
}

ParasiticDmpModel RcModelBuilder::buildParasiticDmpModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                                         TransType trans_type)
{
  ParasiticDmpModel dmp_model;
  std::vector<std::string> node_name_list;
  std::vector<int32_t> parent_idx_list;
  std::vector<double> resistance_list;
  std::vector<double> capacitance_list;
  _rc.buildTree(parasitic_net, source_node_name, analysis_type, trans_type, node_name_list, parent_idx_list, resistance_list, capacitance_list);
  if (node_name_list.empty()) {
    return dmp_model;
  }

  buildParasiticDmpPiModel(dmp_model, parent_idx_list, resistance_list, capacitance_list);
  if (!dmp_model.get_is_valid()) {
    return dmp_model;
  }
  buildParasiticDmpLoadModelList(dmp_model, node_name_list, parent_idx_list, resistance_list, capacitance_list, source_node_name);
  return dmp_model;
}

void RcModelBuilder::buildParasiticDmpPiModel(ParasiticDmpModel& dmp_model, std::vector<int32_t>& parent_idx_list, std::vector<double>& resistance_list,
                                              std::vector<double>& capacitance_list)
{
  if (parent_idx_list.size() != resistance_list.size() || resistance_list.size() != capacitance_list.size() || capacitance_list.empty()) {
    return;
  }

  std::vector<double> admittance1_list = capacitance_list;
  std::vector<double> admittance2_list(capacitance_list.size(), 0.0);
  std::vector<double> admittance3_list(capacitance_list.size(), 0.0);
  for (std::size_t node_idx = capacitance_list.size() - 1; node_idx > 0; node_idx--) {
    int32_t parent_idx = parent_idx_list[node_idx];
    if (parent_idx < 0) {
      return;
    }
    double resistance = resistance_list[node_idx];
    double admittance1 = admittance1_list[node_idx];
    double admittance2 = admittance2_list[node_idx];
    double admittance3 = admittance3_list[node_idx];
    admittance1_list[parent_idx] += admittance1;
    admittance2_list[parent_idx] += admittance2 - resistance * admittance1 * admittance1;
    admittance3_list[parent_idx]
        += admittance3 - 2.0 * resistance * admittance1 * admittance2 + resistance * resistance * admittance1 * admittance1 * admittance1;
  }

  double admittance1 = admittance1_list.front();
  double admittance2 = admittance2_list.front();
  double admittance3 = admittance3_list.front();
  double driver_capacitance = 0.0;
  double pi_resistance = 0.0;
  double load_capacitance = 0.0;
  if (admittance2 == 0.0 && admittance3 == 0.0) {
    load_capacitance = admittance1;
  } else {
    if (admittance3 == 0.0 || admittance2 == 0.0) {
      return;
    }
    load_capacitance = admittance2 * admittance2 / admittance3;
    driver_capacitance = admittance1 - load_capacitance;
    pi_resistance = -admittance3 * admittance3 / (admittance2 * admittance2 * admittance2);
  }
  if (!std::isfinite(driver_capacitance) || !std::isfinite(pi_resistance) || !std::isfinite(load_capacitance) || driver_capacitance < 0.0 || pi_resistance < 0.0
      || load_capacitance < 0.0 || driver_capacitance + load_capacitance <= 0.0) {
    return;
  }

  dmp_model.set_is_valid(true);
  dmp_model.set_driver_capacitance(driver_capacitance);
  dmp_model.set_pi_resistance(pi_resistance);
  dmp_model.set_load_capacitance(load_capacitance);
}

void RcModelBuilder::buildParasiticDmpLoadModelList(ParasiticDmpModel& dmp_model, std::vector<std::string>& node_name_list,
                                                    std::vector<int32_t>& parent_idx_list, std::vector<double>& resistance_list,
                                                    std::vector<double>& capacitance_list, std::string& source_node_name)
{
  Database& database = _database;
  std::vector<std::vector<double>> moment_list(4, std::vector<double>(node_name_list.size(), 0.0));
  for (int32_t moment_idx = 1; moment_idx < 4; moment_idx++) {
    std::vector<double> branch_current_list(node_name_list.size(), 0.0);
    for (std::size_t node_idx = 0; node_idx < node_name_list.size(); node_idx++) {
      double previous_moment = moment_idx == 1 ? 1.0 : moment_list[moment_idx - 1][node_idx];
      branch_current_list[node_idx] = capacitance_list[node_idx] * previous_moment;
    }
    for (std::size_t node_idx = node_name_list.size() - 1; node_idx > 0; node_idx--) {
      int32_t parent_idx = parent_idx_list[node_idx];
      if (parent_idx < 0) {
        return;
      }
      branch_current_list[parent_idx] += branch_current_list[node_idx];
    }
    moment_list[moment_idx].front() = 0.0;
    for (std::size_t node_idx = 1; node_idx < node_name_list.size(); node_idx++) {
      int32_t parent_idx = parent_idx_list[node_idx];
      moment_list[moment_idx][node_idx] = moment_list[moment_idx][parent_idx] - resistance_list[node_idx] * branch_current_list[node_idx];
    }
  }

  for (std::size_t node_idx = 0; node_idx < node_name_list.size(); node_idx++) {
    std::string& node_name = node_name_list[node_idx];
    if (node_name == source_node_name) {
      continue;
    }
    std::string pin_name = _rc.pinName(node_name);
    if (database.network().pins().count(pin_name) == 0) {
      continue;
    }
    ParasiticDmpLoadModel load_model = buildParasiticDmpLoadModel(moment_list[1][node_idx], moment_list[2][node_idx], moment_list[3][node_idx]);
    if (load_model.get_is_valid()) {
      dmp_model.get_load_model_map()[node_name] = load_model;
    }
  }
}

ParasiticDmpLoadModel RcModelBuilder::buildParasiticDmpLoadModel(double moment1, double moment2, double moment3)
{
  ParasiticDmpLoadModel load_model;
  if (!std::isfinite(moment1) || !std::isfinite(moment2) || !std::isfinite(moment3) || moment1 == 0.0) {
    return load_model;
  }

  double pole1 = moment3 == 0.0 ? 0.0 : -moment2 / moment3;
  double ratio_denominator = moment2 == 0.0 || moment3 == 0.0 ? 0.0 : moment1 / moment2 - moment2 / moment3;
  double pole2 = 0.0;
  if (pole1 > 0.0 && ratio_denominator != 0.0 && moment2 != 0.0) {
    pole2 = pole1 * (1.0 / moment1 - moment1 / moment2) / ratio_denominator;
  }
  if (!(pole1 > 0.0) || !(pole2 > 0.0) || pole1 == pole2 || ratio_denominator == 0.0 || !std::isfinite(pole1) || !std::isfinite(pole2)) {
    pole1 = -1.0 / moment1;
    if (!(pole1 > 0.0) || !std::isfinite(pole1)) {
      return load_model;
    }
    load_model.set_is_valid(true);
    load_model.get_pole_list().push_back(pole1);
    load_model.get_residue_list().push_back(1.0);
    return load_model;
  }

  double residue1 = pole1 * pole1 * (1.0 + moment1 * pole2) / (pole1 - pole2);
  double residue2 = -pole2 * pole2 * (1.0 + moment1 * pole1) / (pole1 - pole2);
  if (!std::isfinite(residue1) || !std::isfinite(residue2)) {
    return load_model;
  }
  if (residue1 < 0.0 && residue2 > 0.0) {
    std::swap(pole1, pole2);
    std::swap(residue1, residue2);
  }
  load_model.set_is_valid(true);
  load_model.get_pole_list().push_back(pole1);
  load_model.get_pole_list().push_back(pole2);
  load_model.get_residue_list().push_back(residue1);
  load_model.get_residue_list().push_back(residue2);
  return load_model;
}

ParasiticArnoldiModel& RcModelBuilder::arnoldiModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                                    TransType trans_type)
{
  ArnoldiModelKey arnoldi_model_key = getParasiticArnoldiModelKey(parasitic_net, source_node_name, analysis_type, trans_type);
  if (_parasitic_arnoldi_model_cache.count(arnoldi_model_key) == 0) {
    _parasitic_arnoldi_model_cache[arnoldi_model_key] = buildParasiticArnoldiModel(parasitic_net, source_node_name, analysis_type, trans_type);
  }
  return _parasitic_arnoldi_model_cache[arnoldi_model_key];
}

RcModelBuilder::ArnoldiModelKey RcModelBuilder::getParasiticArnoldiModelKey(ParasiticNet& parasitic_net, std::string& source_node_name,
                                                                            AnalysisType analysis_type, TransType trans_type)
{
  return std::make_tuple(parasitic_net.get_net_name(), source_node_name, analysis_type, trans_type);
}

ParasiticArnoldiModel RcModelBuilder::buildParasiticArnoldiModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                                                 TransType trans_type)
{
  ParasiticArnoldiModel arnoldi_model;
  std::vector<std::string> node_name_list;
  std::vector<int32_t> parent_idx_list;
  std::vector<double> resistance_list;
  std::vector<double> capacitance_list;
  _rc.buildTree(parasitic_net, source_node_name, analysis_type, trans_type, node_name_list, parent_idx_list, resistance_list, capacitance_list);
  if (node_name_list.empty()) {
    return arnoldi_model;
  }

  initParasiticArnoldiTerm(arnoldi_model, node_name_list, source_node_name);
  if (arnoldi_model.get_term_node_list().empty()) {
    return arnoldi_model;
  }

  std::vector<std::size_t> term_point_idx_list;
  for (std::string& term_node_name : arnoldi_model.get_term_node_list()) {
    for (std::size_t node_idx = 0; node_idx < node_name_list.size(); node_idx++) {
      if (node_name_list[node_idx] == term_node_name) {
        term_point_idx_list.push_back(node_idx);
        break;
      }
    }
  }
  if (term_point_idx_list.size() != arnoldi_model.get_term_node_list().size()) {
    return arnoldi_model;
  }

  const bool has_short = std::any_of(parasitic_net.get_resistor_list().begin(), parasitic_net.get_resistor_list().end(),
                                     [](const auto& resistor) { return resistor.get_resistance() == 0.; });
  if (has_short) {
    ParasiticNet contracted;
    if (!contractShorts(parasitic_net, contracted, node_name_list, parent_idx_list, resistance_list, capacitance_list, term_point_idx_list))
      return arnoldi_model;
    updateParasiticArnoldiModel(arnoldi_model, contracted, node_name_list, parent_idx_list, resistance_list, capacitance_list, term_point_idx_list);
  } else {
    updateParasiticArnoldiModel(arnoldi_model, parasitic_net, node_name_list, parent_idx_list, resistance_list, capacitance_list, term_point_idx_list);
  }
  return arnoldi_model;
}

void RcModelBuilder::initParasiticArnoldiTerm(ParasiticArnoldiModel& arnoldi_model, std::vector<std::string>& node_name_list, std::string& source_node_name)
{
  Database& database = _database;
  arnoldi_model.get_term_node_list().push_back(source_node_name);
  arnoldi_model.get_term_pin_list().push_back(_rc.pinName(source_node_name));
  arnoldi_model.get_term_index_map()[source_node_name] = 0;
  arnoldi_model.get_term_index_map()[arnoldi_model.get_term_pin_list().front()] = 0;
  for (std::string& node_name : node_name_list) {
    if (node_name == source_node_name) {
      continue;
    }
    std::string pin_name = _rc.pinName(node_name);
    if (database.network().pins().count(pin_name) == 0) {
      continue;
    }
    std::size_t term_idx = arnoldi_model.get_term_node_list().size();
    arnoldi_model.get_term_node_list().push_back(node_name);
    arnoldi_model.get_term_pin_list().push_back(pin_name);
    arnoldi_model.get_term_index_map()[node_name] = term_idx;
    arnoldi_model.get_term_index_map()[pin_name] = term_idx;
  }
}

void RcModelBuilder::updateParasiticArnoldiModel(ParasiticArnoldiModel& arnoldi_model, ParasiticNet& parasitic_net, std::vector<std::string>& node_name_list,
                                                 std::vector<int32_t>& parent_idx_list, std::vector<double>& resistance_list,
                                                 std::vector<double>& capacitance_list, std::vector<std::size_t>& term_point_idx_list)
{
  double total_capacitance = 0.0;
  for (double capacitance : capacitance_list) {
    total_capacitance += capacitance;
  }
  if (total_capacitance <= 0.0) {
    return;
  }

  std::size_t node_num = capacitance_list.size();
  constexpr int32_t max_order = 5;
  int32_t order = std::min(static_cast<int32_t>(node_num), max_order);
  double sqrt_total_capacitance = std::sqrt(total_capacitance);
  std::vector<double> current_basis_list(node_num, 1.0 / sqrt_total_capacitance);
  std::vector<double> previous_basis_list(node_num, 0.0);
  std::vector<double> diagonal_list(order, 0.0);
  std::vector<double> off_diagonal_list(order, 0.0);
  std::vector<std::vector<double>> projection_list(order, std::vector<double>(term_point_idx_list.size(), 0.0));
  int32_t final_order = order;
  std::vector<bool> zero_capacitance_terms;
  for (auto node : term_point_idx_list)
    zero_capacitance_terms.push_back(capacitance_list[node] == 0.);
  std::vector<std::vector<double>> response_projection(order, std::vector<double>(term_point_idx_list.size()));

  std::map<std::string, std::size_t> node_idx_map;
  for (std::size_t node_idx = 0; node_idx < node_name_list.size(); node_idx++) {
    node_idx_map[node_name_list[node_idx]] = node_idx;
  }
  std::size_t network_resistance_num = 0;
  for (ParasiticResistor& parasitic_resistor : parasitic_net.get_resistor_list()) {
    if (node_idx_map.count(parasitic_resistor.get_source_node()) == 0 || node_idx_map.count(parasitic_resistor.get_sink_node()) == 0
        || parasitic_resistor.get_source_node() == parasitic_resistor.get_sink_node()) {
      continue;
    }
    network_resistance_num++;
  }

  bool has_resistance_loop = network_resistance_num + 1 > node_num;
  Eigen::SparseMatrix<double> conductance_matrix;
  Eigen::ConjugateGradient<Eigen::SparseMatrix<double>, Eigen::Lower | Eigen::Upper, Eigen::IncompleteCholesky<double>> conductance_solver;
  if (has_resistance_loop) {
    std::vector<Eigen::Triplet<double>> conductance_triplet_list;
    for (ParasiticResistor& parasitic_resistor : parasitic_net.get_resistor_list()) {
      if (node_idx_map.count(parasitic_resistor.get_source_node()) == 0 || node_idx_map.count(parasitic_resistor.get_sink_node()) == 0) {
        continue;
      }
      std::size_t source_idx = node_idx_map[parasitic_resistor.get_source_node()];
      std::size_t sink_idx = node_idx_map[parasitic_resistor.get_sink_node()];
      if (source_idx == sink_idx) {
        continue;
      }
      double conductance = 1.0 / (parasitic_resistor.get_resistance() * 1E-3);
      if (source_idx > 0) {
        conductance_triplet_list.emplace_back(source_idx - 1, source_idx - 1, conductance);
      }
      if (sink_idx > 0) {
        conductance_triplet_list.emplace_back(sink_idx - 1, sink_idx - 1, conductance);
      }
      if (source_idx > 0 && sink_idx > 0) {
        conductance_triplet_list.emplace_back(source_idx - 1, sink_idx - 1, -conductance);
        conductance_triplet_list.emplace_back(sink_idx - 1, source_idx - 1, -conductance);
      }
    }
    conductance_matrix.resize(node_num - 1, node_num - 1);
    conductance_matrix.setFromTriplets(conductance_triplet_list.begin(), conductance_triplet_list.end());
    conductance_solver.compute(conductance_matrix);
    has_resistance_loop = conductance_solver.info() == Eigen::Success;
  }

  for (int32_t order_idx = 0; order_idx < order; order_idx++) {
    updateParasiticArnoldiProjection(arnoldi_model, current_basis_list, term_point_idx_list, order_idx);
    if (arnoldi_model.get_projection_list().empty()) {
      arnoldi_model.set_projection_list(projection_list);
    } else {
      projection_list = arnoldi_model.get_projection_list();
    }

    std::vector<double> response_list(node_num, 0.0);
    if (has_resistance_loop) {
      Eigen::VectorXd current_load_vector(node_num - 1);
      for (std::size_t node_idx = 1; node_idx < node_num; node_idx++) {
        current_load_vector[node_idx - 1] = capacitance_list[node_idx] * current_basis_list[node_idx];
      }
      Eigen::VectorXd response_vector = conductance_solver.solve(current_load_vector);
      for (std::size_t node_idx = 1; node_idx < node_num; node_idx++) {
        response_list[node_idx] = response_vector[node_idx - 1];
      }
    } else {
      std::vector<double> current_load_list(node_num, 0.0);
      for (int32_t node_idx = static_cast<int32_t>(node_num) - 1; node_idx > 0; node_idx--) {
        current_load_list[node_idx] += capacitance_list[node_idx] * current_basis_list[node_idx];
        current_load_list[parent_idx_list[node_idx]] += current_load_list[node_idx];
      }
      current_load_list[0] += capacitance_list[0] * current_basis_list[0];
      for (std::size_t node_idx = 1; node_idx < node_num; node_idx++) {
        response_list[node_idx] = response_list[parent_idx_list[node_idx]] + resistance_list[node_idx] * current_load_list[node_idx];
      }
    }

    for (size_t term = 0; term < term_point_idx_list.size(); ++term)
      response_projection[order_idx][term] = response_list[term_point_idx_list[term]];

    double diagonal = 0.0;
    for (std::size_t node_idx = 1; node_idx < node_num; node_idx++) {
      diagonal += current_basis_list[node_idx] * capacitance_list[node_idx] * response_list[node_idx];
    }
    diagonal_list[order_idx] = diagonal;
    if (order_idx == order - 1) {
      break;
    }
    if (diagonal < ista::kTimingTolerance) {
      final_order = order_idx + 1;
      break;
    }

    for (std::size_t node_idx = 0; node_idx < node_num; node_idx++) {
      response_list[node_idx] -= diagonal * current_basis_list[node_idx];
      if (order_idx > 0) {
        response_list[node_idx] -= off_diagonal_list[order_idx - 1] * previous_basis_list[node_idx];
      }
    }

    double off_diagonal = 0.0;
    for (std::size_t node_idx = 0; node_idx < node_num; node_idx++) {
      off_diagonal += capacitance_list[node_idx] * response_list[node_idx] * response_list[node_idx];
    }
    if (off_diagonal < ista::kTimingTolerance * ista::kTimingTolerance) {
      final_order = order_idx + 1;
      break;
    }
    off_diagonal = std::sqrt(off_diagonal);
    off_diagonal_list[order_idx] = off_diagonal;

    previous_basis_list = current_basis_list;
    for (std::size_t node_idx = 0; node_idx < node_num; node_idx++) {
      current_basis_list[node_idx] = response_list[node_idx] / off_diagonal;
    }
  }

  diagonal_list.resize(final_order);
  if (final_order > 1) {
    off_diagonal_list.resize(final_order - 1);
  } else {
    off_diagonal_list.clear();
  }
  projection_list.resize(final_order);
  response_projection.resize(final_order);
  arnoldi_model.setZeroCapacitanceTerms(std::move(zero_capacitance_terms));
  arnoldi_model.setResponseProjection(std::move(response_projection));
  arnoldi_model.set_is_valid(final_order > 0);
  arnoldi_model.set_order(final_order);
  arnoldi_model.set_total_capacitance(total_capacitance);
  arnoldi_model.set_sqrt_total_capacitance(sqrt_total_capacitance);
  arnoldi_model.set_diagonal_list(diagonal_list);
  arnoldi_model.set_off_diagonal_list(off_diagonal_list);
  arnoldi_model.set_projection_list(projection_list);
}

void RcModelBuilder::updateParasiticArnoldiProjection(ParasiticArnoldiModel& arnoldi_model, std::vector<double>& basis_list,
                                                      std::vector<std::size_t>& term_point_idx_list, std::size_t order_idx)
{
  if (arnoldi_model.get_projection_list().size() <= order_idx) {
    arnoldi_model.get_projection_list().resize(order_idx + 1);
  }
  arnoldi_model.get_projection_list()[order_idx].clear();
  for (std::size_t term_point_idx : term_point_idx_list) {
    arnoldi_model.get_projection_list()[order_idx].push_back(basis_list[term_point_idx]);
  }
}

}  // namespace ista
