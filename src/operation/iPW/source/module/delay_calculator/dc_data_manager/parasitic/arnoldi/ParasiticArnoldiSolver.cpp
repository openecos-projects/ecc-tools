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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "ParasiticArnoldiSolver.hpp"

#include <Eigen/Eigenvalues>
#include <Eigen/IterativeLinearSolvers>
#include <Eigen/SparseCore>

#include "DataManager.hpp"
#include "Database.hpp"
#include "DelayCache.hpp"
#include "Instance.hpp"
#include "Net.hpp"
#include "OutputLoadEstimator.hpp"
#include "ParasiticGraph.hpp"
#include "ParasiticNet.hpp"
#include "ParasiticNodeName.hpp"
#include "SharedDriverResultCache.hpp"
#include "ParasiticThresholds.hpp"
#include "Pin.hpp"
#include "PWHeader.hpp"
#include "RcTree.hpp"
#include "TimingArc.hpp"

namespace ipw {

ParasiticDelayResult& ParasiticArnoldiSolver::getTimingResult(
    std::string& output_pin,
    TimingArc& timing_arc,
    AnalysisType analysis_type,
    TransType output_trans_type,
    double input_slew,
    double output_load
)
{
  return PWDCACHE.arnoldiTimingResult({output_pin, &timing_arc, analysis_type, output_trans_type, input_slew}, [&] {
    return calcParasiticArnoldiTimingResult(
        output_pin,
        timing_arc,
        analysis_type,
        output_trans_type,
        input_slew,
        output_load
    );
  });
}

ParasiticDelayResult ParasiticArnoldiSolver::calcParasiticArnoldiTimingResult(
    std::string& output_pin,
    TimingArc& timing_arc,
    AnalysisType analysis_type,
    TransType output_trans_type,
    double input_slew,
    double output_load
)
{
  ParasiticDelayResult timing_result;
  Database& database = PWDM.getDatabase();
  if (database.get_pin_map().count(output_pin) == 0) {
    return timing_result;
  }
  Pin& output_pin_data = database.get_pin_map()[output_pin];
  if (output_pin_data.get_net_name().empty()
      || database.get_parasitic_net_map().count(output_pin_data.get_net_name()) == 0) {
    return timing_result;
  }

  ParasiticNet& parasitic_net = database.get_parasitic_net_map()[output_pin_data.get_net_name()];
  std::string source_node_name = getParasiticNodeName(parasitic_net, output_pin);
  if (source_node_name.empty()) {
    return timing_result;
  }

  const ParasiticGraph& source_graph = getParasiticGraph(parasitic_net);
  const uint32_t source_node_id = source_graph.find(source_node_name);
  ParasiticArnoldiModel& arnoldi_model
      = getParasiticArnoldiModel(parasitic_net, source_node_id, analysis_type, output_trans_type);
  if (!arnoldi_model.get_is_valid() || arnoldi_model.get_order() <= 0 || arnoldi_model.get_total_capacitance() <= 0.0) {
    return timing_result;
  }
  double total_capacitance = arnoldi_model.get_total_capacitance();
  double delay_at_total_capacitance
      = calcTimingArcDelayByRawLoad(timing_arc, output_trans_type, input_slew, total_capacitance);
  double delay_at_half_capacitance
      = calcTimingArcDelayByRawLoad(timing_arc, output_trans_type, input_slew, 0.5 * total_capacitance);
  double delay_resistance = 0.0;
  if (total_capacitance > PW_ERROR) {
    delay_resistance = (delay_at_total_capacitance - delay_at_half_capacitance) / (0.5 * total_capacitance);
  }
  if (!(delay_resistance > 0.0)) {
    delay_resistance = 1.0;
  }

  double slew_derate = 1.0;
  double lower_threshold = 0.1;
  double upper_threshold = 0.9;
  double voltage_log = 0.0;
  double min_slew_factor = 0.0;
  double x1 = 0.0;
  double y1 = 0.0;
  calcParasiticArnoldiThreshold(
      timing_arc,
      output_trans_type,
      slew_derate,
      lower_threshold,
      upper_threshold,
      voltage_log,
      min_slew_factor,
      x1,
      y1
  );

  double drive_resistance = calcParasiticArnoldiTableResistance(
      timing_arc,
      output_trans_type,
      input_slew,
      total_capacitance,
      voltage_log,
      slew_derate,
      delay_resistance
  );
  if (!(drive_resistance > 0.0 && drive_resistance < 100.0)) {
    drive_resistance = 1.0;
  }
  bool bad_drive_resistance = drive_resistance < delay_resistance;
  double driver_ramp = calcParasiticArnoldiTableRamp(
      timing_arc,
      output_trans_type,
      input_slew,
      drive_resistance,
      total_capacitance,
      lower_threshold,
      upper_threshold,
      voltage_log,
      slew_derate,
      min_slew_factor
  );
  if (!(driver_ramp > 0.0 && driver_ramp < 100.0)) {
    driver_ramp = 0.5;
  }

  ParasiticArnoldiPoleResidue pole_residue = calcParasiticArnoldiPoleResidue(arnoldi_model, drive_resistance);
  if (!pole_residue.get_is_valid()) {
    return timing_result;
  }

  double effective_capacitance = total_capacitance;
  if (!bad_drive_resistance) {
    for (int32_t iter = 0; iter < 3; iter++) {
      effective_capacitance
          = calcParasiticArnoldiEffectiveCapacitance(driver_ramp, drive_resistance, pole_residue, driver_ramp);
      if (!std::isfinite(effective_capacitance) || effective_capacitance < 1E-8) {
        effective_capacitance = total_capacitance;
      }
      driver_ramp = calcParasiticArnoldiTableRamp(
          timing_arc,
          output_trans_type,
          input_slew,
          drive_resistance,
          effective_capacitance,
          lower_threshold,
          upper_threshold,
          voltage_log,
          slew_derate,
          min_slew_factor
      );
      if (!(driver_ramp > 0.0 && driver_ramp < 100.0)) {
        driver_ramp = 0.5;
      }
    }
  }

  double table_delay = calcTimingArcDelayByRawLoad(timing_arc, output_trans_type, input_slew, effective_capacitance);
  double reference_time
      = solveParasiticArnoldiRampTime(1.0 / (drive_resistance * effective_capacitance), driver_ramp, 0.5);
  std::vector<double> delay_list;
  std::vector<double> slew_list;
  for (std::size_t term_idx = 0; term_idx < arnoldi_model.get_term_node_list().size(); term_idx++) {
    double upper_time = 0.0;
    double middle_time = 0.0;
    double lower_time = 0.0;
    solveParasiticArnoldiWaveformTime(
        driver_ramp,
        pole_residue,
        term_idx,
        upper_threshold,
        upper_time,
        0.5,
        middle_time,
        lower_threshold,
        lower_time
    );
    delay_list.push_back(middle_time + table_delay - reference_time);
    slew_list.push_back((lower_time - upper_time) / slew_derate);
  }
  if (delay_list.empty() || slew_list.empty()) {
    return timing_result;
  }

  timing_result.set_is_valid(true);
  timing_result.set_gate_delay(delay_list.front());
  timing_result.set_driver_slew(slew_list.front());
  bool suppress_min_slew = PWDM.getConfig().min_slew_degradation == 0 && analysis_type == AnalysisType::kMin
                           && arnoldi_model.get_pi_resistance() > kMinSlewResistanceThreshold * drive_resistance;
  const ParasiticGraph& term_graph = getParasiticGraph(parasitic_net);
  for (std::size_t term_idx = 0; term_idx < arnoldi_model.get_term_node_list().size(); term_idx++) {
    std::string& term_node_name = arnoldi_model.get_term_node_list()[term_idx];
    double wire_delay = delay_list[term_idx] - delay_list.front();
    double load_slew = slew_list[term_idx];
    const uint32_t term_node_id = term_graph.find(term_node_name);
    std::string pin_name = term_node_id == kInvalidParasiticNode ? term_node_name : term_graph.pin_name_of(term_node_id);
    if (term_idx > 0) {
      adjustParasiticLoadThreshold(timing_arc, pin_name, output_trans_type, wire_delay, load_slew);
      if (suppress_min_slew && isParasiticLoadSlewCompatible(timing_arc, pin_name, output_trans_type)) {
        // Keep the delay threshold correction from the physical load waveform.
        // Convert the replacement slew to the receiver library independently.
        load_slew = slew_list.front();
        double unused_wire_delay = 0.0;
        adjustParasiticLoadThreshold(timing_arc, pin_name, output_trans_type, unused_wire_delay, load_slew);
      }
    }
    timing_result.get_load_slew_map()[term_node_name] = load_slew;
    timing_result.get_load_slew_map()[pin_name] = load_slew;
  }
  cacheParasiticArnoldiDriverResult(
      output_pin,
      analysis_type,
      output_trans_type,
      timing_result.get_driver_slew(),
      timing_result
  );
  return timing_result;
}

void ParasiticArnoldiSolver::cacheParasiticArnoldiDriverResult(
    std::string& output_pin,
    AnalysisType analysis_type,
    TransType output_trans_type,
    double driver_slew,
    ParasiticDelayResult& timing_result
)
{
  SharedDriverResultCache::getInst().insert(
      SharedDriverResultCache::Kind::kArnoldi,
      {output_pin, analysis_type, output_trans_type, driver_slew},
      timing_result
  );
}

std::optional<double> ParasiticArnoldiSolver::getCachedLoadSlew(
    Arc& arc,
    AnalysisType analysis_type,
    TransType trans_type,
    double input_slew
)
{
  if (!std::isfinite(input_slew)) {
    return std::nullopt;
  }
  ParasiticDelayResult* timing_result = SharedDriverResultCache::getInst().find(
      SharedDriverResultCache::Kind::kArnoldi,
      {arc.get_source_pin(), analysis_type, trans_type, input_slew}
  );
  if (timing_result == nullptr) {
    return std::nullopt;
  }
  std::map<std::string, double>& load_slew_map = timing_result->get_load_slew_map();
  auto load_slew = load_slew_map.find(arc.get_sink_pin());
  if (load_slew == load_slew_map.end()) {
    return std::nullopt;
  }
  return load_slew->second;
}

ParasiticArnoldiModel& ParasiticArnoldiSolver::getParasiticArnoldiModel(
    ParasiticNet& parasitic_net,
    uint32_t source_node_id,
    AnalysisType analysis_type,
    TransType trans_type
)
{
  ParasiticModelKey key{&parasitic_net.get_net_name(), source_node_id, analysis_type, trans_type};
  return PWDCACHE.arnoldiModel(key, [&] {
    return buildParasiticArnoldiModel(parasitic_net, source_node_id, analysis_type, trans_type);
  });
}

ParasiticArnoldiModel ParasiticArnoldiSolver::buildParasiticArnoldiModel(
    ParasiticNet& parasitic_net,
    uint32_t source_node_id,
    AnalysisType analysis_type,
    TransType trans_type
)
{
  ParasiticArnoldiModel arnoldi_model;
  const ParasiticGraph& graph = getParasiticGraph(parasitic_net);
  if (source_node_id >= graph.size()) {
    return arnoldi_model;
  }
  std::vector<uint32_t> node_id_list;
  std::vector<int32_t> parent_idx_list;
  std::vector<double> resistance_list;
  std::vector<double> capacitance_list;
  {
    flattenRcTree(
        parasitic_net,
        source_node_id,
        analysis_type,
        trans_type,
        node_id_list,
        parent_idx_list,
        resistance_list,
        capacitance_list
    );
  }
  if (node_id_list.empty()) {
    return arnoldi_model;
  }

  initParasiticArnoldiTerm(arnoldi_model, graph, node_id_list, source_node_id);
  if (arnoldi_model.get_term_node_list().empty()) {
    return arnoldi_model;
  }

  // 节点 id -> 展平序位置（终端定位由 O(nodes x terms) 的线性名字搜索变成 O(1) 查表）
  std::vector<uint32_t> node_position_list(graph.size(), kInvalidParasiticNode);
  for (std::size_t node_idx = 0; node_idx < node_id_list.size(); node_idx++) {
    node_position_list[node_id_list[node_idx]] = static_cast<uint32_t>(node_idx);
  }
  std::vector<std::size_t> term_point_idx_list;
  term_point_idx_list.reserve(arnoldi_model.get_term_node_list().size());
  for (std::string& term_node_name : arnoldi_model.get_term_node_list()) {
    const uint32_t term_node_id = graph.find(term_node_name);
    if (term_node_id == kInvalidParasiticNode || node_position_list[term_node_id] == kInvalidParasiticNode) {
      term_point_idx_list.clear();
      break;
    }
    term_point_idx_list.push_back(node_position_list[term_node_id]);
  }
  if (term_point_idx_list.size() != arnoldi_model.get_term_node_list().size()) {
    return arnoldi_model;
  }

  updateParasiticArnoldiModel(
      arnoldi_model,
      parasitic_net,
      graph,
      node_id_list,
      parent_idx_list,
      resistance_list,
      capacitance_list,
      term_point_idx_list
  );
  return arnoldi_model;
}

void ParasiticArnoldiSolver::initParasiticArnoldiTerm(
    ParasiticArnoldiModel& arnoldi_model,
    const ParasiticGraph& graph,
    std::vector<uint32_t>& node_id_list,
    uint32_t source_node_id
)
{
  // pin 归属与名字都在 ParasiticGraph 里预解析过，这里不再逐节点走
  // getPinNameByParasiticNodeName() + pin_map 查找。
  arnoldi_model.get_term_node_list().push_back(*graph.node_name[source_node_id]);
  arnoldi_model.get_term_pin_list().push_back(graph.pin_name_of(source_node_id));
  for (uint32_t node_id : node_id_list) {
    if (node_id == source_node_id) {
      continue;
    }
    if (!graph.is_pin(node_id)) {
      continue;
    }
    arnoldi_model.get_term_node_list().push_back(*graph.node_name[node_id]);
    arnoldi_model.get_term_pin_list().push_back(graph.pin_name_of(node_id));
  }
}

void ParasiticArnoldiSolver::updateParasiticArnoldiModel(
    ParasiticArnoldiModel& arnoldi_model,
    ParasiticNet& parasitic_net,
    const ParasiticGraph& graph,
    std::vector<uint32_t>& node_id_list,
    std::vector<int32_t>& parent_idx_list,
    std::vector<double>& resistance_list,
    std::vector<double>& capacitance_list,
    std::vector<std::size_t>& term_point_idx_list
)
{
  double total_capacitance = 0.0;
  for (double capacitance : capacitance_list) {
    total_capacitance += capacitance;
  }
  if (total_capacitance <= 0.0) {
    return;
  }

  std::size_t node_num = capacitance_list.size();
  int32_t max_order = 5;
  int32_t order = std::min(static_cast<int32_t>(node_num), max_order);
  double sqrt_total_capacitance = std::sqrt(total_capacitance);
  std::vector<double> current_basis_list(node_num, 1.0 / sqrt_total_capacitance);
  std::vector<double> previous_basis_list(node_num, 0.0);
  std::vector<double> diagonal_list(order, 0.0);
  std::vector<double> off_diagonal_list(order, 0.0);
  std::vector<std::vector<double>> projection_list(order, std::vector<double>(term_point_idx_list.size(), 0.0));
  int32_t final_order = order;

  std::vector<uint32_t> node_position_list(graph.size(), kInvalidParasiticNode);
  for (std::size_t node_idx = 0; node_idx < node_id_list.size(); node_idx++) {
    node_position_list[node_id_list[node_idx]] = static_cast<uint32_t>(node_idx);
  }
  std::size_t network_resistance_num = 0;
  for (ParasiticResistor& parasitic_resistor : parasitic_net.get_resistor_list()) {
    const uint32_t source_node_id = graph.find(parasitic_resistor.get_source_node());
    const uint32_t sink_node_id = graph.find(parasitic_resistor.get_sink_node());
    if (source_node_id == kInvalidParasiticNode || sink_node_id == kInvalidParasiticNode
        || node_position_list[source_node_id] == kInvalidParasiticNode
        || node_position_list[sink_node_id] == kInvalidParasiticNode || source_node_id == sink_node_id) {
      continue;
    }
    network_resistance_num++;
  }

  bool has_resistance_loop = network_resistance_num + 1 > node_num;
  // The moment recurrence is valid for trees; loops retain waveform slew.
  if (!has_resistance_loop && PWDM.getConfig().min_slew_degradation == 0) {
    ParasiticDmpModel pi_model;
    buildPiModel(pi_model, parent_idx_list, resistance_list, capacitance_list);
    if (pi_model.get_is_valid()) {
      arnoldi_model.set_pi_resistance(pi_model.get_pi_resistance());
    }
  }
  Eigen::SparseMatrix<double> conductance_matrix;
  Eigen::ConjugateGradient<Eigen::SparseMatrix<double>, Eigen::Lower | Eigen::Upper, Eigen::IncompleteCholesky<double>>
      conductance_solver;
  if (has_resistance_loop) {
    std::vector<Eigen::Triplet<double>> conductance_triplet_list;
    for (ParasiticResistor& parasitic_resistor : parasitic_net.get_resistor_list()) {
      const uint32_t source_node_id = graph.find(parasitic_resistor.get_source_node());
      const uint32_t sink_node_id = graph.find(parasitic_resistor.get_sink_node());
      if (source_node_id == kInvalidParasiticNode || sink_node_id == kInvalidParasiticNode
          || node_position_list[source_node_id] == kInvalidParasiticNode
          || node_position_list[sink_node_id] == kInvalidParasiticNode) {
        continue;
      }
      std::size_t source_idx = node_position_list[source_node_id];
      std::size_t sink_idx = node_position_list[sink_node_id];
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
        response_list[node_idx]
            = response_list[parent_idx_list[node_idx]] + resistance_list[node_idx] * current_load_list[node_idx];
      }
    }

    double diagonal = 0.0;
    for (std::size_t node_idx = 1; node_idx < node_num; node_idx++) {
      diagonal += current_basis_list[node_idx] * capacitance_list[node_idx] * response_list[node_idx];
    }
    diagonal_list[order_idx] = diagonal;
    if (order_idx == order - 1) {
      break;
    }
    if (diagonal < PW_ERROR) {
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
    if (off_diagonal < PW_ERROR * PW_ERROR) {
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
  arnoldi_model.set_is_valid(final_order > 0);
  arnoldi_model.set_order(final_order);
  arnoldi_model.set_total_capacitance(total_capacitance);
  arnoldi_model.set_sqrt_total_capacitance(sqrt_total_capacitance);
  arnoldi_model.set_diagonal_list(diagonal_list);
  arnoldi_model.set_off_diagonal_list(off_diagonal_list);
  arnoldi_model.set_projection_list(projection_list);
}

void ParasiticArnoldiSolver::updateParasiticArnoldiProjection(
    ParasiticArnoldiModel& arnoldi_model,
    std::vector<double>& basis_list,
    std::vector<std::size_t>& term_point_idx_list,
    std::size_t order_idx
)
{
  if (arnoldi_model.get_projection_list().size() <= order_idx) {
    arnoldi_model.get_projection_list().resize(order_idx + 1);
  }
  arnoldi_model.get_projection_list()[order_idx].clear();
  for (std::size_t term_point_idx : term_point_idx_list) {
    arnoldi_model.get_projection_list()[order_idx].push_back(basis_list[term_point_idx]);
  }
}

ParasiticDelayResult& ParasiticArnoldiSolver::getParasiticInputPortResult(
    ParasiticNet& parasitic_net,
    const std::string& source_node_name,
    AnalysisType analysis_type,
    TransType trans_type,
    double input_slew
)
{
  const double slew = std::isfinite(input_slew) ? std::abs(input_slew) : 0.0;
  return PWDCACHE.inputPortResult({source_node_name, analysis_type, trans_type, slew}, [&] {
    return calcParasiticInputPortResult(parasitic_net, source_node_name, analysis_type, trans_type, slew);
  });
}

ParasiticDelayResult ParasiticArnoldiSolver::calcParasiticInputPortResult(
    ParasiticNet& parasitic_net,
    const std::string& source_node_name,
    AnalysisType analysis_type,
    TransType trans_type,
    double input_slew
)
{
  ParasiticDelayResult result;
  Database& database = PWDM.getDatabase();
  std::string source_pin_name = getPinNameByParasiticNodeName(source_node_name);
  if (!database.get_pin_map().contains(source_pin_name) || !database.get_pin_map().at(source_pin_name).get_is_port()) {
    return result;
  }
  const ParasiticGraph& source_graph = getParasiticGraph(parasitic_net);
  ParasiticArnoldiModel& model
      = getParasiticArnoldiModel(parasitic_net, source_graph.find(source_node_name), analysis_type, trans_type);
  if (!model.get_is_valid() || model.get_order() <= 0) {
    return result;
  }
  // The port imposes a voltage waveform. Use zero source resistance and the
  // reduced RC response, retaining the fast modes lost by an Elmore estimate.
  int32_t order = model.get_order();
  Eigen::MatrixXd matrix = Eigen::MatrixXd::Zero(order, order);
  for (int32_t index = 0; index < order; ++index) {
    matrix(index, index) = model.get_diagonal_list()[index];
    if (index + 1 < order) {
      matrix(index, index + 1) = matrix(index + 1, index) = model.get_off_diagonal_list()[index];
    }
  }
  Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(matrix);
  if (solver.info() != Eigen::Success || !solver.eigenvalues().allFinite()) {
    return result;
  }
  std::vector<double> time_constants(order);
  double scale = solver.eigenvalues().cwiseAbs().maxCoeff();
  for (int32_t index = 0; index < order; ++index) {
    double value = solver.eigenvalues()[index];
    if (value < -scale * 1E-12) {
      return result;
    }
    time_constants[index] = value > scale * 1E-12 ? value : 0.0;
  }
  double derate = 1.0;
  double lower = 0.1;
  double upper = 0.9;
  double voltage_log = 0.0;
  double min_slew_factor = 0.0;
  double x1 = 0.0;
  double y1 = 0.0;
  calcParasiticArnoldiThreshold(trans_type, derate, lower, upper, voltage_log, min_slew_factor, x1, y1);
  TimingLibrary& library = database.get_timing_library();
  bool is_fall = trans_type == TransType::kFall;
  double source_threshold = getNormalizedThreshold(
      is_fall ? library.get_output_threshold_pct_fall() : library.get_output_threshold_pct_rise()
  );
  double ramp = input_slew * derate / (upper - lower);
  for (std::size_t term_idx = 0; term_idx < model.get_term_node_list().size(); ++term_idx) {
    std::vector<double> weights(order, 0.0);
    for (int32_t index = 0; index < order; ++index) {
      double projection = 0.0;
      for (int32_t basis = 0; basis < order; ++basis) {
        projection += solver.eigenvectors()(basis, index) * model.get_projection_list()[basis][term_idx];
      }
      weights[index] = model.get_sqrt_total_capacitance() * solver.eigenvectors()(0, index) * projection;
    }
    std::string& node = model.get_term_node_list()[term_idx];
    std::string pin_name = getPinNameByParasiticNodeName(node);
    TimingCell* load_cell = getThresholdTimingCell(pin_name);
    double load_threshold = getNormalizedThreshold(
        is_fall ? library.get_input_threshold_pct_fall() : library.get_input_threshold_pct_rise()
    );
    double load_lower = lower;
    double load_upper = upper;
    double load_derate = derate;
    if (load_cell != nullptr) {
      load_threshold = getNormalizedThreshold(getTimingCellInputThreshold(*load_cell, trans_type));
      load_lower = getNormalizedThreshold(getTimingCellSlewLowerThreshold(*load_cell, trans_type));
      load_upper = getNormalizedThreshold(getTimingCellSlewUpperThreshold(*load_cell, trans_type));
      load_derate = load_cell->get_slew_derate_from_library();
    }
    if (!(load_threshold > 0.0 && load_threshold < 1.0 && load_lower > 0.0 && load_upper < 1.0
          && load_upper > load_lower && load_derate > 0.0 && source_threshold > 0.0 && source_threshold < 1.0)) {
      return ParasiticDelayResult();
    }
    double middle_time = calcParasiticInputPortCrossing(
        time_constants,
        weights,
        ramp,
        is_fall ? load_threshold : 1.0 - load_threshold
    );
    double early_time
        = calcParasiticInputPortCrossing(time_constants, weights, ramp, is_fall ? load_upper : 1.0 - load_lower);
    double late_time
        = calcParasiticInputPortCrossing(time_constants, weights, ramp, is_fall ? load_lower : 1.0 - load_upper);
    if (!std::isfinite(middle_time) || !std::isfinite(early_time) || !std::isfinite(late_time)
        || late_time < early_time) {
      return ParasiticDelayResult();
    }
    result.get_load_slew_map()[node] = term_idx == 0 ? input_slew : (late_time - early_time) / load_derate;
    result.get_load_slew_map()[pin_name] = result.get_load_slew_map()[node];
  }
  result.set_is_valid(true);
  return result;
}

double ParasiticArnoldiSolver::calcParasiticInputPortCrossing(
    std::vector<double>& time_constants,
    std::vector<double>& weights,
    double ramp,
    double voltage
)
{
  // Integrate each exponential step response over the source ramp. Remaining
  // voltage is the falling waveform, or one minus the rising waveform.
  double low = 0.0;
  double high = ramp + 64.0 * *std::max_element(time_constants.begin(), time_constants.end());
  for (int32_t iteration = 0; iteration < 64; ++iteration) {
    double time = 0.5 * (low + high);
    double remaining = 0.0;
    for (std::size_t index = 0; index < time_constants.size(); ++index) {
      double tau = time_constants[index];
      double response = 0.0;
      if (tau == 0.0) {
        response = ramp > 0.0 ? std::max(0.0, 1.0 - time / ramp) : 0.0;
      } else if (ramp == 0.0) {
        response = std::exp(-time / tau);
      } else if (time < ramp) {
        response = 1.0 - time / ramp - tau / ramp * std::expm1(-time / tau);
      } else {
        response = -tau / ramp * std::expm1(-ramp / tau) * std::exp(-(time - ramp) / tau);
      }
      remaining += weights[index] * response;
    }
    if (remaining > voltage) {
      low = time;
    } else {
      high = time;
    }
  }
  return 0.5 * (low + high);
}

std::optional<double> ParasiticArnoldiSolver::getInputPortSlew(
    ParasiticNet& parasitic_net,
    const std::string& source_node_name,
    const std::string& sink_node_name,
    AnalysisType analysis_type,
    TransType trans_type,
    double input_slew
)
{
  Database& database = PWDM.getDatabase();
  std::string source_pin_name = getPinNameByParasiticNodeName(source_node_name);
  if (!database.get_pin_map().contains(source_pin_name) || !database.get_pin_map().at(source_pin_name).get_is_port()) {
    return std::nullopt;
  }
  ParasiticDelayResult& result
      = getParasiticInputPortResult(parasitic_net, source_node_name, analysis_type, trans_type, input_slew);
  if (!result.get_is_valid() || !result.get_load_slew_map().contains(sink_node_name)) {
    return std::nullopt;
  }
  double output_slew = result.get_load_slew_map().at(sink_node_name);
  return input_slew < 0.0 ? -output_slew : output_slew;
}

ParasiticArnoldiPoleResidue ParasiticArnoldiSolver::calcParasiticArnoldiPoleResidue(
    ParasiticArnoldiModel& arnoldi_model,
    double drive_resistance
)
{
  ParasiticArnoldiPoleResidue pole_residue;
  if (!arnoldi_model.get_is_valid() || arnoldi_model.get_order() <= 0) {
    return pole_residue;
  }

  std::vector<double> diagonal_list = arnoldi_model.get_diagonal_list();
  std::vector<double> off_diagonal_list = arnoldi_model.get_off_diagonal_list();
  diagonal_list.front() += drive_resistance * arnoldi_model.get_total_capacitance();

  std::vector<double> eigenvalue_list;
  std::vector<std::vector<double>> eigenvector_list;
  if (!solveParasiticArnoldiTridiagonalEigen(diagonal_list, off_diagonal_list, eigenvalue_list, eigenvector_list)) {
    return pole_residue;
  }

  int32_t order = arnoldi_model.get_order();
  std::size_t term_num = arnoldi_model.get_term_node_list().size();
  std::vector<double> pole_list(order, 0.0);
  for (int32_t order_idx = 0; order_idx < order; order_idx++) {
    pole_list[order_idx] = 1.0 / std::max(eigenvalue_list[order_idx], 1E-5);
  }

  std::vector<std::vector<double>> residue_list(term_num, std::vector<double>(order, 0.0));
  for (int32_t order_idx = 0; order_idx < order; order_idx++) {
    double source_projection = arnoldi_model.get_sqrt_total_capacitance() * eigenvector_list[order_idx][0];
    for (std::size_t term_idx = 0; term_idx < term_num; term_idx++) {
      double term_projection = 0.0;
      for (int32_t basis_idx = 0; basis_idx < order; basis_idx++) {
        term_projection
            += eigenvector_list[order_idx][basis_idx] * arnoldi_model.get_projection_list()[basis_idx][term_idx];
      }
      residue_list[term_idx][order_idx] = source_projection * term_projection;
    }
  }

  pole_residue.set_is_valid(true);
  pole_residue.set_order(order);
  pole_residue.set_pole_list(pole_list);
  pole_residue.set_residue_list(residue_list);
  return pole_residue;
}

bool ParasiticArnoldiSolver::solveParasiticArnoldiTridiagonalEigen(
    std::vector<double>& diagonal_list,
    std::vector<double>& off_diagonal_list,
    std::vector<double>& eigenvalue_list,
    std::vector<std::vector<double>>& eigenvector_list
)
{
  std::size_t order = diagonal_list.size();
  if (order == 0 || order > 32) {
    return false;
  }
  eigenvalue_list = diagonal_list;
  eigenvector_list.assign(order, std::vector<double>(order, 0.0));
  for (std::size_t order_idx = 0; order_idx < order; order_idx++) {
    eigenvector_list[order_idx][order_idx] = 1.0;
  }

  std::vector<double> diagonal_off_list(order, 0.0);
  for (std::size_t order_idx = 0; order_idx + 1 < order; order_idx++) {
    diagonal_off_list[order_idx + 1] = off_diagonal_list[order_idx];
  }

  for (int32_t high_idx = static_cast<int32_t>(order) - 1; high_idx >= 1; high_idx--) {
    int32_t iter_num = 0;
    while (std::abs(diagonal_off_list[high_idx]) > 1E-9) {
      int32_t low_idx = 0;
      if (iter_num++ == 20) {
        return false;
      }
      double g = (eigenvalue_list[high_idx - 1] - eigenvalue_list[high_idx]) / (2.0 * diagonal_off_list[high_idx]);
      double r = std::sqrt(1.0 + g * g);
      g = eigenvalue_list[low_idx] - eigenvalue_list[high_idx] + diagonal_off_list[high_idx] / (g + (g < 0.0 ? -r : r));
      double s = 1.0;
      double c = 1.0;
      double p = 0.0;
      int32_t iter_idx = low_idx + 1;
      for (; iter_idx <= high_idx; iter_idx++) {
        double f = s * diagonal_off_list[iter_idx];
        double b = c * diagonal_off_list[iter_idx];
        diagonal_off_list[iter_idx - 1] = r = std::sqrt(f * f + g * g);
        if (r == 0.0) {
          eigenvalue_list[iter_idx - 1] -= p;
          diagonal_off_list[low_idx] = 0.0;
          break;
        }
        s = f / r;
        c = g / r;
        g = eigenvalue_list[iter_idx - 1] - p;
        r = (eigenvalue_list[iter_idx] - g) * s + 2.0 * c * b;
        eigenvalue_list[iter_idx - 1] = g + (p = s * r);
        g = c * r - b;
        for (std::size_t term_idx = 0; term_idx < order; term_idx++) {
          f = eigenvector_list[iter_idx - 1][term_idx];
          eigenvector_list[iter_idx - 1][term_idx] = s * eigenvector_list[iter_idx][term_idx] + c * f;
          eigenvector_list[iter_idx][term_idx] = c * eigenvector_list[iter_idx][term_idx] - s * f;
        }
      }
      if (r == 0.0 && iter_idx <= high_idx) {
        continue;
      }
      eigenvalue_list[high_idx] -= p;
      diagonal_off_list[high_idx] = g;
      diagonal_off_list[low_idx] = 0.0;
    }
  }

  for (std::size_t order_idx = 0; order_idx + 1 < order; order_idx++) {
    std::size_t max_idx = order_idx;
    double max_value = eigenvalue_list[max_idx];
    for (std::size_t next_idx = order_idx + 1; next_idx < order; next_idx++) {
      if (eigenvalue_list[next_idx] > max_value) {
        max_idx = next_idx;
        max_value = eigenvalue_list[next_idx];
      }
    }
    if (max_idx == order_idx) {
      continue;
    }
    std::swap(eigenvalue_list[max_idx], eigenvalue_list[order_idx]);
    for (std::size_t basis_idx = 0; basis_idx < order; basis_idx++) {
      std::swap(eigenvector_list[max_idx][basis_idx], eigenvector_list[order_idx][basis_idx]);
    }
  }
  return true;
}

void ParasiticArnoldiSolver::calcParasiticArnoldiThreshold(
    TimingArc& timing_arc,
    TransType trans_type,
    double& slew_derate,
    double& lower_threshold,
    double& upper_threshold,
    double& voltage_log,
    double& min_slew_factor,
    double& x1,
    double& y1
)
{
  slew_derate = timing_arc.get_slew_derate();
  if (trans_type == TransType::kFall) {
    lower_threshold = timing_arc.get_slew_lower_threshold_pct_fall();
    upper_threshold = timing_arc.get_slew_upper_threshold_pct_fall();
  } else {
    lower_threshold = timing_arc.get_slew_lower_threshold_pct_rise();
    upper_threshold = timing_arc.get_slew_upper_threshold_pct_rise();
  }
  lower_threshold = getNormalizedThreshold(lower_threshold);
  upper_threshold = getNormalizedThreshold(upper_threshold);
  if (!(lower_threshold > 0.01 && upper_threshold < 0.99 && upper_threshold > lower_threshold && slew_derate > 0.0)) {
    lower_threshold = 0.1;
    upper_threshold = 0.9;
    slew_derate = 0.8;
  }
  voltage_log = std::log(upper_threshold / lower_threshold);
  calcParasiticArnoldiThresholdFactor(lower_threshold, upper_threshold, min_slew_factor, x1, y1);
}

void ParasiticArnoldiSolver::calcParasiticArnoldiThreshold(
    TransType trans_type,
    double& slew_derate,
    double& lower_threshold,
    double& upper_threshold,
    double& voltage_log,
    double& min_slew_factor,
    double& x1,
    double& y1
)
{
  Database& database = PWDM.getDatabase();
  TimingLibrary& timing_library = database.get_timing_library();
  slew_derate = timing_library.get_slew_derate_from_library();
  if (trans_type == TransType::kFall) {
    lower_threshold = timing_library.get_slew_lower_threshold_pct_fall();
    upper_threshold = timing_library.get_slew_upper_threshold_pct_fall();
  } else {
    lower_threshold = timing_library.get_slew_lower_threshold_pct_rise();
    upper_threshold = timing_library.get_slew_upper_threshold_pct_rise();
  }
  lower_threshold = getNormalizedThreshold(lower_threshold);
  upper_threshold = getNormalizedThreshold(upper_threshold);
  if (!(lower_threshold > 0.01 && upper_threshold < 0.99 && upper_threshold > lower_threshold && slew_derate > 0.0)) {
    lower_threshold = 0.1;
    upper_threshold = 0.9;
    slew_derate = 0.8;
  }
  voltage_log = std::log(upper_threshold / lower_threshold);
  calcParasiticArnoldiThresholdFactor(lower_threshold, upper_threshold, min_slew_factor, x1, y1);
}

void ParasiticArnoldiSolver::calcParasiticArnoldiThresholdFactor(
    double lower_threshold,
    double upper_threshold,
    double& min_slew_factor,
    double& x1,
    double& y1
)
{
  double upper_log = std::log(1.0 / upper_threshold);
  min_slew_factor = upper_log + calcParasiticArnoldiHInverse((1.0 - upper_threshold) / upper_threshold - upper_log);
  double lower_log = std::log(1.0 / lower_threshold);
  double lower_factor = lower_log + calcParasiticArnoldiHInverse((1.0 - lower_threshold) / lower_threshold - lower_log);
  double lower_ratio = (std::exp(lower_factor) - 1.0) / lower_factor;
  double denominator
      = std::log(lower_ratio / lower_threshold) - calcParasiticArnoldiHInverse((1.0 - upper_threshold) * lower_factor);
  x1 = (upper_threshold - lower_threshold) / denominator;
  y1 = lower_factor * x1;
}

double ParasiticArnoldiSolver::calcParasiticArnoldiHInverse(double value)
{
  double x = 0.0;
  if (value < 1.0) {
    x = std::sqrt(2.0 * value) + 0.4 * value;
    if (value < 1E-4) {
      return x;
    }
  } else {
    x = value + 1.0;
  }
  for (int32_t iter = 0; iter < 4; iter++) {
    double exp_x = std::exp(-x);
    double function_value = x + exp_x - 1.0 - value;
    x += function_value / (exp_x - 1.0);
  }
  return x;
}

double ParasiticArnoldiSolver::calcParasiticArnoldiTableResistance(
    TimingArc& timing_arc,
    TransType output_trans_type,
    double input_slew,
    double total_capacitance,
    double voltage_log,
    double slew_derate,
    double delay_resistance
)
{
  if (!(total_capacitance > 0.0 && voltage_log > 0.0)) {
    return 0.0;
  }
  double table_slew = calcTimingArcSlewByRawLoad(timing_arc, output_trans_type, input_slew, total_capacitance);
  double transition_time = slew_derate * table_slew;
  double drive_resistance = transition_time / (voltage_log * total_capacitance);
  if (delay_resistance > 0.0 && drive_resistance > delay_resistance) {
    drive_resistance = delay_resistance;
  }
  return drive_resistance;
}

double ParasiticArnoldiSolver::calcParasiticArnoldiTableRamp(
    TimingArc& timing_arc,
    TransType output_trans_type,
    double input_slew,
    double drive_resistance,
    double capacitance,
    double lower_threshold,
    double upper_threshold,
    double voltage_log,
    double slew_derate,
    double min_slew_factor
)
{
  if (!(drive_resistance > 0.0 && capacitance > 0.0 && voltage_log > 0.0)) {
    return 0.0;
  }
  double table_slew = calcTimingArcSlewByRawLoad(timing_arc, output_trans_type, input_slew, capacitance);
  double transition_time = slew_derate * table_slew;
  double min_slew = drive_resistance * capacitance * min_slew_factor;
  if (voltage_log * drive_resistance * capacitance >= transition_time) {
    return min_slew;
  }
  double ramp = min_slew + 0.3 * transition_time;
  solveParasiticArnoldiRamp(
      1.0 / (drive_resistance * capacitance),
      transition_time,
      lower_threshold,
      upper_threshold,
      ramp
  );
  return ramp;
}

void ParasiticArnoldiSolver::solveParasiticArnoldiRamp(
    double pole,
    double transition_time,
    double lower_threshold,
    double upper_threshold,
    double& ramp
)
{
  for (int32_t iter = 0; iter < 5; iter++) {
    double lower_pole_time = 0.0;
    double lower_derivative = 0.0;
    double upper_pole_time = 0.0;
    double upper_derivative = 0.0;
    solveParasiticArnoldiRampPoint(pole * ramp, lower_threshold, lower_pole_time, lower_derivative);
    solveParasiticArnoldiRampPoint(pole * ramp, upper_threshold, upper_pole_time, upper_derivative);
    double function_value = (lower_pole_time - upper_pole_time) / pole - transition_time;
    double derivative = lower_derivative - upper_derivative;
    if (std::abs(derivative) < PW_ERROR) {
      return;
    }
    ramp -= function_value / derivative;
    if (std::abs(function_value) < 1E-6) {
      return;
    }
  }
}

double ParasiticArnoldiSolver::solveParasiticArnoldiRampTime(double pole, double ramp, double voltage)
{
  double pole_ramp = pole * ramp;
  if (pole_ramp > 30.0) {
    return (1.0 + pole_ramp * (1.0 - voltage)) / pole;
  }
  double exp_pole_ramp = std::exp(pole_ramp);
  if ((1.0 - pole_ramp * voltage) * exp_pole_ramp >= 1.0) {
    return std::log((exp_pole_ramp - 1.0) / (pole_ramp * voltage)) / pole;
  }
  return calcParasiticArnoldiHInverse((1.0 - voltage) * pole_ramp) / pole;
}

void ParasiticArnoldiSolver::solveParasiticArnoldiRampPoint(
    double pole_ramp,
    double voltage,
    double& pole_time,
    double& derivative
)
{
  if (pole_ramp > 30.0) {
    pole_time = 1.0 + pole_ramp * (1.0 - voltage);
    derivative = 1.0 - voltage;
    return;
  }
  double exp_pole_ramp = std::exp(pole_ramp);
  if ((1.0 - pole_ramp * voltage) * exp_pole_ramp >= 1.0) {
    pole_time = std::log((exp_pole_ramp - 1.0) / (pole_ramp * voltage));
    derivative = exp_pole_ramp / (exp_pole_ramp - 1.0) - 1.0 / pole_ramp;
    return;
  }
  pole_time = calcParasiticArnoldiHInverse((1.0 - voltage) * pole_ramp);
  derivative = (1.0 - voltage) / (pole_time - (1.0 - voltage) * pole_ramp);
}

double ParasiticArnoldiSolver::calcParasiticArnoldiEffectiveCapacitance(
    double driver_ramp,
    double drive_resistance,
    ParasiticArnoldiPoleResidue& pole_residue,
    double effective_capacitance_time
)
{
  if (!(drive_resistance > 0.0) || pole_residue.get_residue_list().empty()) {
    return 0.0;
  }
  std::vector<double>& pole_list = pole_residue.get_pole_list();
  std::vector<double>& residue_list = pole_residue.get_residue_list().front();
  double integrated_current = 0.0;
  for (std::size_t pole_idx = 0; pole_idx < pole_list.size(); pole_idx++) {
    double pole = pole_list[pole_idx];
    double pole_ramp = pole * driver_ramp;
    double pole_time = pole * effective_capacitance_time;
    double charge = 0.0;
    if (effective_capacitance_time <= driver_ramp) {
      double exp_time = pole_time > 40.0 ? 0.0 : std::exp(-pole_time);
      charge = exp_time - 1.0 + pole_time;
    } else {
      double exp_time = (pole_time - pole_ramp) > 40.0 ? 0.0 : std::exp(-(pole_time - pole_ramp));
      double exp_ramp = pole_ramp > 40.0 ? 0.0 : std::exp(-pole_ramp);
      charge = pole_ramp - (1.0 - exp_ramp) * exp_time;
    }
    charge /= pole_ramp * pole;
    integrated_current += residue_list[pole_idx] * charge;
  }
  integrated_current /= drive_resistance;

  double voltage
      = calcParasiticArnoldiWaveformVoltage(effective_capacitance_time, driver_ramp, pole_list, residue_list);
  if (std::abs(1.0 - voltage) < PW_ERROR) {
    return 0.0;
  }
  return integrated_current / (1.0 - voltage);
}

void ParasiticArnoldiSolver::solveParasiticArnoldiWaveformTime(
    double driver_ramp,
    ParasiticArnoldiPoleResidue& pole_residue,
    std::size_t term_idx,
    double voltage,
    double& waveform_time
)
{
  std::vector<double>& pole_list = pole_residue.get_pole_list();
  std::vector<double>& residue_list = pole_residue.get_residue_list()[term_idx];
  int32_t order = pole_residue.get_order();
  while (order > 1 && residue_list[order - 1] < 1E-8 && residue_list[order - 1] > -1E-8) {
    order--;
  }

  int32_t pole_idx = 0;
  if (residue_list[0] < 0.5) {
    for (int32_t idx = 1; idx < order; idx++) {
      if (residue_list[idx] > 0.3 && residue_list[idx] > residue_list[0]) {
        pole_idx = idx;
        break;
      }
    }
  }

  double waveform_at_ramp = 0.0;
  for (int32_t idx = 0; idx < order; idx++) {
    double pole_ramp = pole_list[idx] * driver_ramp;
    waveform_at_ramp += residue_list[idx] * (1.0 - std::exp(-pole_ramp)) / pole_ramp;
  }

  double lower_time = 0.0;
  double upper_time = 0.0;
  double lower_voltage = 0.0;
  double upper_voltage = 0.0;
  if (waveform_at_ramp < voltage) {
    double candidate_time = 0.5 * (1.0 + voltage) * driver_ramp;
    double candidate_voltage
        = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    if (candidate_voltage < voltage) {
      upper_time = candidate_time;
      upper_voltage = candidate_voltage;
      candidate_time = voltage * driver_ramp;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
      lower_time = candidate_time;
      lower_voltage = candidate_voltage;
    } else {
      lower_time = candidate_time;
      lower_voltage = candidate_voltage;
      candidate_time = driver_ramp;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
      while (candidate_voltage > voltage) {
        lower_time = candidate_time;
        lower_voltage = candidate_voltage;
        candidate_time *= 2.0;
        candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
      }
      upper_time = candidate_time;
      upper_voltage = candidate_voltage;
    }
  } else {
    double candidate_time = driver_ramp;
    double candidate_voltage = waveform_at_ramp;
    double search_pole = std::min(pole_list[pole_idx], 10.0);
    while (candidate_voltage >= voltage) {
      lower_time = candidate_time;
      lower_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    }
    upper_time = candidate_time;
    upper_voltage = candidate_voltage;
  }
  waveform_time = solveParasiticArnoldiBracketedTime(
      driver_ramp,
      pole_list,
      residue_list,
      voltage,
      lower_time,
      upper_time,
      lower_voltage,
      upper_voltage
  );
}

void ParasiticArnoldiSolver::solveParasiticArnoldiWaveformTime(
    double driver_ramp,
    ParasiticArnoldiPoleResidue& pole_residue,
    std::size_t term_idx,
    double upper_threshold,
    double& upper_time,
    double mid_threshold,
    double& mid_time,
    double lower_threshold,
    double& lower_time
)
{
  std::vector<double>& source_pole_list = pole_residue.get_pole_list();
  std::vector<double>& source_residue_list = pole_residue.get_residue_list()[term_idx];
  int32_t order = pole_residue.get_order();
  while (order > 1 && source_residue_list[order - 1] < 1E-8 && source_residue_list[order - 1] > -1E-8) {
    order--;
  }
  if (order <= 0) {
    upper_time = 0.0;
    mid_time = 0.0;
    lower_time = 0.0;
    return;
  }

  std::vector<double> pole_list;
  std::vector<double> residue_list;
  for (int32_t idx = 0; idx < order; idx++) {
    pole_list.push_back(source_pole_list[idx]);
    residue_list.push_back(source_residue_list[idx]);
  }

  int32_t pole_idx = 0;
  if (residue_list[0] < 0.5) {
    for (int32_t idx = 1; idx < order; idx++) {
      if (residue_list[idx] > 0.3 && residue_list[idx] > residue_list[0]) {
        pole_idx = idx;
        break;
      }
    }
  }
  double search_pole = std::min(pole_list[pole_idx], 10.0);
  double waveform_at_ramp = 0.0;
  for (int32_t idx = 0; idx < order; idx++) {
    double pole_ramp = pole_list[idx] * driver_ramp;
    waveform_at_ramp += residue_list[idx] * (1.0 - std::exp(-pole_ramp)) / pole_ramp;
  }

  double hi_min_time = 0.0;
  double hi_max_time = 0.0;
  double hi_min_voltage = 0.0;
  double hi_max_voltage = 0.0;
  double mid_min_time = 0.0;
  double mid_max_time = 0.0;
  double mid_min_voltage = 0.0;
  double mid_max_voltage = 0.0;
  double lo_min_time = 0.0;
  double lo_max_time = 0.0;
  double lo_min_voltage = 0.0;
  double lo_max_voltage = 0.0;

  if (waveform_at_ramp < lower_threshold) {
    lo_max_time = driver_ramp;
    lo_max_voltage = waveform_at_ramp;
    double candidate_time = upper_threshold * driver_ramp;
    double candidate_voltage
        = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    if (candidate_voltage < mid_threshold) {
      hi_max_time = candidate_time;
      mid_max_time = candidate_time;
      lo_min_time = candidate_time;
      hi_max_voltage = candidate_voltage;
      mid_max_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      candidate_time = mid_threshold * driver_ramp;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
      if (candidate_voltage > upper_threshold) {
        hi_min_time = candidate_time;
        mid_min_time = candidate_time;
        lo_min_time = candidate_time;
        hi_min_voltage = candidate_voltage;
        mid_min_voltage = candidate_voltage;
        lo_min_voltage = candidate_voltage;
        if (candidate_voltage < mid_threshold) {
          mid_max_time = candidate_time;
          mid_max_voltage = candidate_voltage;
        } else {
          mid_min_time = candidate_time;
          mid_min_voltage = candidate_voltage;
        }
      } else {
        hi_max_time = candidate_time;
        mid_min_time = candidate_time;
        hi_max_voltage = candidate_voltage;
        mid_min_voltage = candidate_voltage;
        candidate_time = lower_threshold * driver_ramp;
        candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
        hi_min_time = candidate_time;
        hi_min_voltage = candidate_voltage;
      }
    } else {
      mid_min_time = candidate_time;
      lo_min_time = candidate_time;
      mid_min_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      mid_max_time = lo_max_time;
      mid_max_voltage = lo_max_voltage;
      if (candidate_voltage > upper_threshold) {
        hi_min_time = mid_min_time;
        hi_min_voltage = mid_min_voltage;
        hi_max_time = mid_max_time;
        hi_max_voltage = mid_max_voltage;
      } else {
        hi_max_time = mid_min_time;
        hi_max_voltage = mid_min_voltage;
        candidate_time = lower_threshold * driver_ramp;
        candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
        hi_min_time = candidate_time;
        hi_min_voltage = candidate_voltage;
      }
    }
  } else if (waveform_at_ramp < mid_threshold) {
    hi_max_time = driver_ramp;
    mid_max_time = driver_ramp;
    lo_min_time = driver_ramp;
    hi_max_voltage = waveform_at_ramp;
    mid_max_voltage = waveform_at_ramp;
    lo_min_voltage = waveform_at_ramp;
    double candidate_time = driver_ramp + 1.6 / search_pole;
    double candidate_voltage
        = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    while (candidate_voltage > lower_threshold) {
      lo_min_time = candidate_time;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    }
    lo_max_time = candidate_time;
    lo_max_voltage = candidate_voltage;
    candidate_time = mid_threshold * driver_ramp;
    candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    mid_min_time = candidate_time;
    mid_min_voltage = candidate_voltage;
    if (candidate_voltage > upper_threshold) {
      hi_min_time = candidate_time;
      hi_min_voltage = candidate_voltage;
    } else {
      hi_max_time = candidate_time;
      hi_max_voltage = candidate_voltage;
      candidate_time = lower_threshold * driver_ramp;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
      hi_min_time = candidate_time;
      hi_min_voltage = candidate_voltage;
    }
  } else if (waveform_at_ramp < upper_threshold) {
    hi_max_time = driver_ramp;
    mid_min_time = driver_ramp;
    lo_min_time = driver_ramp;
    hi_max_voltage = waveform_at_ramp;
    mid_min_voltage = waveform_at_ramp;
    lo_min_voltage = waveform_at_ramp;
    double candidate_time = lower_threshold * driver_ramp;
    double candidate_voltage
        = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    hi_min_time = candidate_time;
    hi_min_voltage = candidate_voltage;
    candidate_time = driver_ramp + 0.7 / search_pole;
    candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    while (candidate_voltage > mid_threshold) {
      mid_min_time = candidate_time;
      lo_min_time = candidate_time;
      mid_min_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      candidate_time += 0.7 / search_pole;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    }
    mid_max_time = candidate_time;
    mid_max_voltage = candidate_voltage;
    if (candidate_voltage < lower_threshold) {
      lo_max_time = candidate_time;
      lo_max_voltage = candidate_voltage;
    } else {
      lo_min_time = candidate_time;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
      while (candidate_voltage > lower_threshold) {
        lo_min_time = candidate_time;
        lo_min_voltage = candidate_voltage;
        candidate_time += 1.0 / search_pole;
        candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
      }
      lo_max_time = candidate_time;
      lo_max_voltage = candidate_voltage;
    }
  } else {
    double candidate_time = driver_ramp;
    double candidate_voltage = waveform_at_ramp;
    hi_min_time = candidate_time;
    mid_min_time = candidate_time;
    lo_min_time = candidate_time;
    hi_min_voltage = candidate_voltage;
    mid_min_voltage = candidate_voltage;
    lo_min_voltage = candidate_voltage;
    while (candidate_voltage > upper_threshold) {
      hi_min_time = candidate_time;
      mid_min_time = candidate_time;
      lo_min_time = candidate_time;
      hi_min_voltage = candidate_voltage;
      mid_min_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    }
    hi_max_time = candidate_time;
    hi_max_voltage = candidate_voltage;
    while (candidate_voltage > mid_threshold) {
      mid_min_time = candidate_time;
      lo_min_time = candidate_time;
      mid_min_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    }
    mid_max_time = candidate_time;
    mid_max_voltage = candidate_voltage;
    while (candidate_voltage > lower_threshold) {
      lo_min_time = candidate_time;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    }
    lo_max_time = candidate_time;
    lo_max_voltage = candidate_voltage;
  }

  upper_time = solveParasiticArnoldiBracketedTime(
      driver_ramp,
      pole_list,
      residue_list,
      upper_threshold,
      hi_min_time,
      hi_max_time,
      hi_min_voltage,
      hi_max_voltage
  );
  mid_time = solveParasiticArnoldiBracketedTime(
      driver_ramp,
      pole_list,
      residue_list,
      mid_threshold,
      mid_min_time,
      mid_max_time,
      mid_min_voltage,
      mid_max_voltage
  );
  lower_time = solveParasiticArnoldiBracketedTime(
      driver_ramp,
      pole_list,
      residue_list,
      lower_threshold,
      lo_min_time,
      lo_max_time,
      lo_min_voltage,
      lo_max_voltage
  );
}

double ParasiticArnoldiSolver::calcParasiticArnoldiWaveformVoltage(
    double time,
    double driver_ramp,
    std::vector<double>& pole_list,
    std::vector<double>& residue_list
)
{
  double voltage = 0.0;
  for (std::size_t pole_idx = 0; pole_idx < pole_list.size(); pole_idx++) {
    double pole_time = pole_list[pole_idx] * time;
    double pole_ramp = pole_list[pole_idx] * driver_ramp;
    double response = 0.0;
    if (time < driver_ramp) {
      response = 1.0 - time / driver_ramp + (1.0 - std::exp(-pole_time)) / pole_ramp;
    } else {
      response = std::exp(pole_ramp - pole_time) * (1.0 - std::exp(-pole_ramp)) / pole_ramp;
    }
    voltage += residue_list[pole_idx] * response;
  }
  return voltage;
}

void ParasiticArnoldiSolver::calcParasiticArnoldiWaveformVoltageAndDerivative(
    double time,
    double driver_ramp,
    std::vector<double>& pole_list,
    std::vector<double>& residue_list,
    double& voltage,
    double& derivative
)
{
  voltage = 0.0;
  derivative = 0.0;
  for (std::size_t pole_idx = 0; pole_idx < pole_list.size(); pole_idx++) {
    double pole = pole_list[pole_idx];
    double pole_time = pole * time;
    double pole_ramp = pole * driver_ramp;
    double response = 0.0;
    double response_derivative = 0.0;
    if (time < driver_ramp) {
      double ramp_response = (1.0 - std::exp(-pole_time)) / pole_ramp;
      response = 1.0 - time / driver_ramp + ramp_response;
      response_derivative = -pole * ramp_response;
    } else {
      response = std::exp(pole_ramp - pole_time) * (1.0 - std::exp(-pole_ramp)) / pole_ramp;
      response_derivative = -pole * response;
    }
    voltage += residue_list[pole_idx] * response;
    derivative += residue_list[pole_idx] * response_derivative;
  }
}

double ParasiticArnoldiSolver::solveParasiticArnoldiBracketedTime(
    double driver_ramp,
    std::vector<double>& pole_list,
    std::vector<double>& residue_list,
    double voltage,
    double lower_time,
    double upper_time,
    double lower_voltage,
    double upper_voltage
)
{
  double lower_function = lower_voltage - voltage;
  double upper_function = upper_voltage - voltage;
  if (lower_function == 0.0) {
    return lower_time;
  }
  if (upper_function == 0.0) {
    return upper_time;
  }

  double result_time = (lower_function * upper_time - upper_function * lower_time) / (lower_function - upper_function);
  double low_time = lower_time;
  double high_time = upper_time;
  if (lower_function < upper_function) {
    low_time = lower_time;
    high_time = upper_time;
    if (0.0 < lower_function) {
      return lower_time;
    }
    if (upper_function < 0.0) {
      return upper_time;
    }
  } else {
    low_time = upper_time;
    high_time = lower_time;
    if (0.0 < upper_function) {
      return upper_time;
    }
    if (lower_function < 0.0) {
      return lower_time;
    }
  }

  double old_delta = std::abs(upper_time - lower_time);
  double delta = old_delta;
  double function_value = 0.0;
  double derivative = 0.0;
  calcParasiticArnoldiWaveformVoltageAndDerivative(
      result_time,
      driver_ramp,
      pole_list,
      residue_list,
      function_value,
      derivative
  );
  function_value -= voltage;
  double last_function_value = 0.0;
  for (int32_t iter = 1; iter < 10; iter++) {
    if ((((result_time - high_time) * derivative - function_value)
             * ((result_time - low_time) * derivative - function_value)
         >= 0.0)
        || (std::abs(2.0 * function_value) > std::abs(old_delta * derivative))) {
      old_delta = delta;
      delta = 0.5 * (high_time - low_time);
      if (last_function_value * function_value > 0.0) {
        if (function_value < 0.0) {
          delta = 0.9348 * (high_time - low_time);
        } else {
          delta = 0.0625 * (high_time - low_time);
        }
      }
      last_function_value = function_value;
      result_time = low_time + delta;
      if (low_time == result_time) {
        return result_time;
      }
    } else {
      old_delta = delta;
      delta = function_value / derivative;
      last_function_value = 0.0;
      double temp_time = result_time;
      result_time -= delta;
      if (temp_time == result_time) {
        return result_time;
      }
    }
    if (std::abs(delta) < 1E-6) {
      return result_time;
    }
    calcParasiticArnoldiWaveformVoltageAndDerivative(
        result_time,
        driver_ramp,
        pole_list,
        residue_list,
        function_value,
        derivative
    );
    function_value -= voltage;
    if (function_value < 0.0) {
      low_time = result_time;
    } else {
      high_time = result_time;
    }
  }
  if (std::abs(function_value) < 1E-6) {
    return result_time;
  }
  return 0.5 * (low_time + high_time);
}

double ParasiticArnoldiSolver::calcTimingArcDelayByRawLoad(
    TimingArc& timing_arc,
    TransType output_trans_type,
    double input_slew,
    double output_load
)
{
  if (timing_arc.get_delay_table_map().count(output_trans_type) == 0) {
    return 0.0;
  }
  double converted_output_load = output_load * timing_arc.get_cap_unit_scale();
  double delay = timing_arc.get_delay_table_map()[output_trans_type].findValue(
      input_slew * timing_arc.get_time_unit_scale(),
      converted_output_load
  );
  return delay / timing_arc.get_time_unit_scale();
}

double ParasiticArnoldiSolver::calcTimingArcSlewByRawLoad(
    TimingArc& timing_arc,
    TransType output_trans_type,
    double input_slew,
    double output_load
)
{
  if (timing_arc.get_slew_table_map().count(output_trans_type) == 0) {
    return input_slew;
  }
  double converted_output_load = output_load * timing_arc.get_cap_unit_scale();
  double slew = timing_arc.get_slew_table_map()[output_trans_type].findValue(
      input_slew * timing_arc.get_time_unit_scale(),
      converted_output_load
  );
  return slew / timing_arc.get_time_unit_scale();
}

}  // namespace ipw
