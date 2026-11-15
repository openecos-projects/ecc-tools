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

#include <cstdio>
#include <cstdlib>

#include <Eigen/Eigenvalues>
#include <Eigen/IterativeLinearSolvers>
#include <Eigen/SparseCholesky>
#include <Eigen/SparseCore>
#include <algorithm>
#include <limits>
#include <cmath>
#include <cstdlib>
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
// ISTA_DRIVE_RES_SERIES=1 enables the physically exact source model: the driver
// resistance becomes a real branch between a new root node and the original
// source, and ArnoldiWaveformSolver::poleResidue drops its rank-1 fold of
// rd*C_total into the reduced [0][0]. Default off keeps every existing result
// bit-identical.
constexpr const char* kSeriesSourceNodeName = "__ista_source_r__";

bool driveResSeriesEnabled()
{
  static const bool enabled = [] {
    const char* e = std::getenv("ISTA_DRIVE_RES_SERIES");
    return e != nullptr && *e != '0';
  }();
  return enabled;
}

// Total-RC time constant below which the lumped small-net response (R*C*ln2)
// replaces the full network solve. Override with the environment variable
// ISTA_SMALL_NET_CUTOFF_PS (ps) for calibration experiments.
double smallNetCutoff()
{
  static const double value = [] {
    const char* text = std::getenv("ISTA_SMALL_NET_CUTOFF_PS");
    if (text == nullptr || *text == '\0')
      return .002;
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    return (end != text && parsed >= 0.) ? parsed * 1e-3 : .002;
  }();
  return value;
}

// reference's reduced small-net capacitance divider. Enabled by default;
// ISTA_REF_BALANCED=0 restores the plain total-RC response for A/B runs.
bool refBalancedEnabled()
{
  static const bool value = [] {
    const char* text = std::getenv("ISTA_REF_BALANCED");
    return text == nullptr || *text != '0';
  }();
  return value;
}
// reference also applies the reduced small-net response to networks with
// parallel edges. Enabled by default; ISTA_LOOPED_SMALLNET=0 restores the
// tree-only guard.
bool loopedSmallNetEnabled()
{
  static const bool value = [] {
    const char* text = std::getenv("ISTA_LOOPED_SMALLNET");
    return text == nullptr || *text != '0';
  }();
  return value;
}

// reference reduces every RC network with an ordinary Euclidean 2-norm Arnoldi
// basis on G^-1 C and keeps at most six vectors; the historical iSTA model is a
// C-orthogonal three-term recurrence capped at five, which is a different basis
// and therefore a different pole/residue set. Default ON. Earlier measurements
// showed it worsening most cases on its own (picorv32a 3.06% -> 7.67%) because the
// conductance it fed into G was not the one the fitter converged to and because the
// driver resistance used the arc's slew derate with a chord clamp in the opposite
// direction from the reference. With ISTA_RD_TWOPASS and the reference rd convention
// both in place the whole 19-case set improves: 5,592 -> 3,297 arcs above 0.1 ps
// (−41%), 18 of 19 cases better or equal, 7 reaching zero. Set ISTA_REF_KRYLOV=0 to
// fall back to the historical three-term recurrence.
bool krylovEnabled()
{
  static const bool enabled = [] {
    const char* e = std::getenv("ISTA_REF_KRYLOV");
    return e == nullptr || *e != '0';
  }();
  return enabled;
}

// Builds reference's reduced model:
//   G  : node conductance in mS, including gd = 1/rd on the driver node
//   C  : node capacitance in pF
//   v0 = G^-1 b / ||G^-1 b||_2 with b = gd * e_driver, negated when v0[driver] > 0
//   vk = orthogonalised(G^-1 (C .* v_{k-1})), stop at the first residual < 1e-4,
//        order = k-1 when k > 2 else k, otherwise the six-vector cap
//   A = V^T diag(C) V ,  Gr = V^T G V ,  br = V^T b
// The drive resistance is required: gd enters G and b, so the basis itself
// depends on it. Returns false (leaving the model untouched) when the caller has
// no usable rd or any solve/eigendecomposition input is degenerate, which makes
// poleResidue() fall through to the tridiagonal model.
bool buildKrylovModel(ParasiticArnoldiModel::KrylovModel& reduced, ParasiticNet& parasitic_net,
                        const std::vector<std::string>& node_name_list, const std::vector<double>& capacitance_list,
                        const std::vector<double>& basis_capacitance_list, const std::vector<std::size_t>& term_point_idx_list,
                        double drive_resistance, double basis_drive_resistance = 0.)
{
  const std::size_t node_num = node_name_list.size();
  if (node_num == 0 || capacitance_list.size() != node_num || term_point_idx_list.empty())
    return false;
  if (!(drive_resistance > 0.) || !std::isfinite(drive_resistance))
    return false;
  for (std::size_t term : term_point_idx_list) {
    if (term >= node_num)
      return false;
  }

  std::map<std::string, std::size_t> node_idx_map;
  for (std::size_t node_idx = 0; node_idx < node_num; node_idx++)
    node_idx_map[node_name_list[node_idx]] = node_idx;

  // ISTA_DRIVE_RES_SERIES inserts a synthetic zero-capacitance root above the
  // original source with rd as a real branch. reference's driver model is a
  // Norton source gd = 1/rd at the source node, so keeping the synthetic node
  // would place rd in series with itself and halve the effective source
  // resistance. Build G over the original node space instead: the synthetic node
  // and its branch are dropped and gd lands on the original source, which is the
  // network reference reduces. `shift` is that index offset.
  const bool has_series_root = node_num > 1 && node_name_list.front() == kSeriesSourceNodeName;
  const std::size_t shift = has_series_root ? 1 : 0;
  const std::size_t reduced_node_num = node_num - shift;
  if (reduced_node_num == 0)
    return false;
  for (std::size_t term : term_point_idx_list) {
    if (term < shift)
      return false;
  }

  // Same clamp the loop-aware branch above uses: reference saturates the branch
  // conductance at nextafter(1e5, 0) in its edge accumulator.
  static const double conductance_limit = [] {
    const char* env = std::getenv("ISTA_NO_CONDUCTANCE_CLAMP");
    if (env != nullptr && *env != '0') {
      return std::numeric_limits<double>::infinity();
    }
    return std::nextafter(1E5, 0.);
  }();

  std::vector<Eigen::Triplet<double>> triplet_list;
  triplet_list.reserve(parasitic_net.get_resistor_list().size() * 4 + 2);
  for (ParasiticResistor& parasitic_resistor : parasitic_net.get_resistor_list()) {
    const auto source_it = node_idx_map.find(parasitic_resistor.get_source_node());
    const auto sink_it = node_idx_map.find(parasitic_resistor.get_sink_node());
    if (source_it == node_idx_map.end() || sink_it == node_idx_map.end() || source_it->second == sink_it->second)
      continue;
    // The synthetic rd branch is replaced by gd below, never added twice.
    if (source_it->second < shift || sink_it->second < shift)
      continue;
    if (!(parasitic_resistor.get_resistance() > 0.))
      continue;
    // R is in ohm here, so 1/(R*1e-3) is in mS; same expression as the tree branch.
    const double conductance = std::min(1.0 / (parasitic_resistor.get_resistance() * 1E-3), conductance_limit);
    if (!std::isfinite(conductance))
      continue;
    const std::size_t source_idx = source_it->second - shift;
    const std::size_t sink_idx = sink_it->second - shift;
    triplet_list.emplace_back(source_idx, source_idx, conductance);
    triplet_list.emplace_back(sink_idx, sink_idx, conductance);
    triplet_list.emplace_back(source_idx, sink_idx, -conductance);
    triplet_list.emplace_back(sink_idx, source_idx, -conductance);
  }
  // gd = 1/rd with rd in kOhm is already mS, the same convention poleResidue uses.
  const double driver_conductance = 1.0 / drive_resistance;
  if (!std::isfinite(driver_conductance) || !(driver_conductance > 0.))
    return false;
  triplet_list.emplace_back(0, 0, driver_conductance);

  Eigen::SparseMatrix<double> ref_conductance(reduced_node_num, reduced_node_num);
  ref_conductance.setFromTriplets(triplet_list.begin(), triplet_list.end());
  Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> ref_solver;
  ref_solver.compute(ref_conductance);
  if (ref_solver.info() != Eigen::Success)
    return false;

  const bool own_basis_conductance = !(basis_drive_resistance > 0.) || std::fabs(basis_drive_resistance - drive_resistance) <= 1E-15 * drive_resistance;
  Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> basis_solver;
  const Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>>* recurrence_solver = &ref_solver;
  Eigen::VectorXd recurrence_source = Eigen::VectorXd::Zero(reduced_node_num);
  recurrence_source[0] = driver_conductance;
  if (!own_basis_conductance && std::isfinite(basis_drive_resistance) && basis_drive_resistance > 0.) {
    std::vector<Eigen::Triplet<double>> basis_triplets;
    basis_triplets.reserve(triplet_list.size());
    bool replaced_driver = false;
    for (const auto& entry : triplet_list) {
      if (!replaced_driver && entry.row() == 0 && entry.col() == 0) {
        basis_triplets.emplace_back(0, 0, 1.0 / basis_drive_resistance);
        replaced_driver = true;
      } else {
        basis_triplets.push_back(entry);
      }
    }
    if (!replaced_driver)
      basis_triplets.emplace_back(0, 0, 1.0 / basis_drive_resistance);
    Eigen::SparseMatrix<double> basis_conductance(reduced_node_num, reduced_node_num);
    basis_conductance.setFromTriplets(basis_triplets.begin(), basis_triplets.end());
    basis_solver.compute(basis_conductance);
    if (basis_solver.info() == Eigen::Success) {
      recurrence_solver = &basis_solver;
      recurrence_source[0] = 1.0 / basis_drive_resistance;
    }
  }

  Eigen::VectorXd capacitance_pf(reduced_node_num);
  // capacitance_list is already in pF: buildTree converts the branch resistance to
  // kOhm (R * 1E-3) and the rest of the model multiplies R[kOhm] by C[pF] to get
  // ns, and the reference 2-pole reduction stores V^T diag(c) V in pF from the same list.
  for (std::size_t node_idx = 0; node_idx < reduced_node_num; node_idx++)
    capacitance_pf[static_cast<Eigen::Index>(node_idx)] = capacitance_list[node_idx + shift];
  // reference resolves the basis once per net from the max/rise capacitance vector
  // and reuses it for every corner: its dumped V is bit-identical across rise/fall
  // and min/max while A, Gr and b change per corner. Building V from this corner's
  // capacitance instead loses that, so the two vectors are kept separate.
  Eigen::VectorXd basis_capacitance_pf = capacitance_pf;
  if (basis_capacitance_list.size() == node_num) {
    for (std::size_t node_idx = 0; node_idx < reduced_node_num; node_idx++)
      basis_capacitance_pf[static_cast<Eigen::Index>(node_idx)] = basis_capacitance_list[node_idx + shift];
  }
  Eigen::VectorXd source_vector = Eigen::VectorXd::Zero(reduced_node_num);
  source_vector[0] = driver_conductance;

  const auto solve_basis = [recurrence_solver](const Eigen::VectorXd& rhs, Eigen::VectorXd& solution) {
    solution = recurrence_solver->solve(rhs);
    return solution.allFinite();
  };

  static const int32_t kMaxOrder = [] {
    const char* e = std::getenv("ISTA_ARNOLDI_MAX_ORDER");
    const int32_t cap = (e != nullptr) ? std::atoi(e) : 6;
    return cap > 0 ? cap : 6;
  }();
  static constexpr double kResidualFloor = 1e-4;
  std::vector<Eigen::VectorXd> basis_list;
  basis_list.reserve(static_cast<std::size_t>(kMaxOrder) + 1);
  Eigen::VectorXd first_basis;
  if (!solve_basis(recurrence_source, first_basis))
    return false;
  const double first_norm = first_basis.norm();
  if (!(first_norm > 0.) || !std::isfinite(first_norm))
    return false;
  first_basis /= first_norm;
  // reference flips the sign so the driver component is always negative; for a
  // resistive network G^-1 b is uniform, so the result is exactly -1/sqrt(N).
  if (first_basis[0] > 0.)
    first_basis = -first_basis;
  basis_list.push_back(first_basis);

  {
    static const char* bsd = std::getenv("ISTA_DUMP_BASIS_SPLIT");
    if (bsd != nullptr) {
      if (FILE* sf = std::fopen(bsd, "a"); sf != nullptr) {
        std::fprintf(sf, "BS,%s,own=%d,corner_gd=%.12g,basis_gd=%.12g,split=%d\n", parasitic_net.get_net_name().c_str(),
                     own_basis_conductance ? 1 : 0, driver_conductance,
                     basis_drive_resistance > 0. ? 1.0 / basis_drive_resistance : 0.,
                     (recurrence_solver == &basis_solver) ? 1 : 0);
        std::fclose(sf);
      }
    }
  }
  int32_t order = kMaxOrder;
  for (int32_t step = 1; step <= kMaxOrder; step++) {
    Eigen::VectorXd residual_vector;
    if (!solve_basis(basis_capacitance_pf.cwiseProduct(basis_list[static_cast<std::size_t>(step) - 1]), residual_vector))
      return false;
    // Plain 2-norm Gram-Schmidt against every previous vector: the Euclidean
    // recurrence is the documented difference from the historical basis.
    for (int32_t previous = 0; previous < step; previous++)
      residual_vector -= residual_vector.dot(basis_list[static_cast<std::size_t>(previous)]) * basis_list[static_cast<std::size_t>(previous)];
    const double residual_norm = residual_vector.norm();
    if (!std::isfinite(residual_norm))
      return false;
    if (residual_norm < kResidualFloor) {
      order = (step > 2) ? step - 1 : step;
      break;
    }
    basis_list.push_back(residual_vector / residual_norm);
  }
  if (order <= 0 || basis_list.size() < static_cast<std::size_t>(order))
    return false;

  Eigen::MatrixXd basis(reduced_node_num, order);
  for (int32_t vector_idx = 0; vector_idx < order; vector_idx++)
    basis.col(vector_idx) = basis_list[static_cast<std::size_t>(vector_idx)];
  const Eigen::MatrixXd reduced_a = basis.transpose() * capacitance_pf.asDiagonal() * basis;
  const Eigen::MatrixXd reduced_g = basis.transpose() * ref_conductance * basis;
  const Eigen::VectorXd reduced_b = basis.transpose() * source_vector;
  if (!reduced_a.allFinite() || !reduced_g.allFinite() || !reduced_b.allFinite())
    return false;

  reduced.valid = true;
  reduced.order = order;
  {
    static const bool from_spef_k = [] {
      const char* e = std::getenv("ISTA_ZERO_CAP_FROM_SPEF");
      return e != nullptr && *e != '0';
    }();
    if (from_spef_k) {
      std::vector<bool> zero_terms_k;
      zero_terms_k.reserve(term_point_idx_list.size());
      static const char* zc_dump = std::getenv("ISTA_DUMP_ZEROCAP");
      FILE* zf = zc_dump != nullptr ? std::fopen(zc_dump, "a") : nullptr;
      for (std::size_t term = 0; term < term_point_idx_list.size(); term++) {
        const std::size_t node_idx = term_point_idx_list[term] - shift;
        bool is_zero = false;
        double cap = -1.;
        bool found = false;
        if (node_idx < node_name_list.size()) {
          const auto it = parasitic_net.get_node_map().find(node_name_list[node_idx]);
          if (it != parasitic_net.get_node_map().end()) {
            found = true;
            cap = it->second.get_capacitance();
            is_zero = cap == 0.;
          }
        }
        if (zf != nullptr) {
          std::fprintf(zf, "ZC,%s,%zu,%zu,%d,%.12g,%d,%s\n", parasitic_net.get_net_name().c_str(), term, node_idx, found ? 1 : 0, cap,
                       is_zero ? 1 : 0, node_idx < node_name_list.size() ? node_name_list[node_idx].c_str() : "?");
        }
        zero_terms_k.push_back(is_zero);
      }
      if (zf != nullptr)
        std::fclose(zf);
      reduced.zero_capacitance_terms = std::move(zero_terms_k);
    }
  }
  reduced.v.assign(term_point_idx_list.size(), std::vector<double>(static_cast<std::size_t>(order), 0.));
  for (std::size_t term = 0; term < term_point_idx_list.size(); term++) {
    const std::size_t node_idx = term_point_idx_list[term] - shift;
    for (int32_t vector_idx = 0; vector_idx < order; vector_idx++)
      reduced.v[term][static_cast<std::size_t>(vector_idx)] = basis(static_cast<Eigen::Index>(node_idx), vector_idx);
  }
  reduced.a.assign(static_cast<std::size_t>(order), std::vector<double>(static_cast<std::size_t>(order), 0.));
  reduced.g.assign(static_cast<std::size_t>(order), std::vector<double>(static_cast<std::size_t>(order), 0.));
  reduced.b.assign(static_cast<std::size_t>(order), 0.);
  for (int32_t row = 0; row < order; row++) {
    for (int32_t column = 0; column < order; column++) {
      reduced.a[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] = reduced_a(row, column);
      reduced.g[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] = reduced_g(row, column);
    }
    reduced.b[static_cast<std::size_t>(row)] = reduced_b[row];
  }
  reduced.driver_projection.assign(static_cast<std::size_t>(order), 0.);
  for (int32_t column = 0; column < order; column++)
    reduced.driver_projection[static_cast<std::size_t>(column)]
        = basis_list[static_cast<std::size_t>(column)][0];
  reduced.driver_conductance = driver_conductance;
  {
    // Diagnostic: one line per built model, so a wrong order or a wrong driver
    // conductance can be traced back to a specific net.
    static const char* bd = std::getenv("ISTA_DUMP_REF_K_BUILD");
    if (bd != nullptr) {
      if (FILE* bf = std::fopen(bd, "a"); bf != nullptr) {
        double total_pf = capacitance_pf.sum();
        std::fprintf(bf, "PB,%s,%zu,%zu,%d,gd=%.9g,ctot_pf=%.9g,a00=%.9g,g00=%.9g,b0=%.9g\n", parasitic_net.get_net_name().c_str(),
                     reduced_node_num, term_point_idx_list.size(), order, driver_conductance, total_pf, reduced_a(0, 0),
                     reduced_g(0, 0), reduced_b[0]);
        std::fclose(bf);
      }
    }
  }
  {
    // Full model dump for cross-checking against reference's own A / Gr / b and V.
    static const char* rk = std::getenv("ISTA_DUMP_REF_KRY");
    if (rk != nullptr) {
      if (FILE* rf = std::fopen(rk, "a"); rf != nullptr) {
        std::fprintf(rf, "RKY,%s,%zu,%d,gd=%.12g\n", parasitic_net.get_net_name().c_str(), reduced_node_num, order, driver_conductance);
        for (int32_t row = 0; row < order; row++) {
          std::fprintf(rf, "  A");
          for (int32_t column = 0; column < order; column++) std::fprintf(rf, ",%.12g", reduced_a(row, column));
          std::fprintf(rf, "\n  G");
          for (int32_t column = 0; column < order; column++) std::fprintf(rf, ",%.12g", reduced_g(row, column));
          std::fprintf(rf, "\n");
        }
        std::fprintf(rf, "  b");
        for (int32_t row = 0; row < order; row++) std::fprintf(rf, ",%.12g", reduced_b[row]);
        std::fprintf(rf, "\n  V0");
        for (Eigen::Index row = 0; row < basis.rows(); row++) std::fprintf(rf, ",%.12g", basis(row, 0));
        std::fprintf(rf, "\n");
        std::fclose(rf);
      }
    }
  }
  return true;
}
}  // namespace

std::string RcModelBuilder::getParasiticDmpModelKey(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                                    TransType trans_type)
{
  std::stringstream key_stream;
  key_stream << parasitic_net.get_net_name() << "|" << source_node_name << "|" << static_cast<int32_t>(analysis_type) << "|"
             << static_cast<int32_t>(trans_type);
  return key_stream.str();
}

ParasiticDmpModel& RcModelBuilder::dmpModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type, TransType trans_type)
{
  std::string dmp_model_key = getParasiticDmpModelKey(parasitic_net, source_node_name, analysis_type, trans_type);
  if (_parasitic_dmp_model_cache.count(dmp_model_key) == 0) {
    _parasitic_dmp_model_cache[dmp_model_key] = buildParasiticDmpModel(parasitic_net, source_node_name, analysis_type, trans_type);
  }
  return _parasitic_dmp_model_cache[dmp_model_key];
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
                                                    TransType trans_type, double drive_resistance, double reduced_drive_resistance,
                                                    double basis_drive_resistance)
{
  ArnoldiModelKey arnoldi_model_key
      = getParasiticArnoldiModelKey(parasitic_net, source_node_name, analysis_type, trans_type, drive_resistance, reduced_drive_resistance,
                                    basis_drive_resistance);
  if (_parasitic_arnoldi_model_cache.count(arnoldi_model_key) == 0) {
    _parasitic_arnoldi_model_cache[arnoldi_model_key]
        = buildParasiticArnoldiModel(parasitic_net, source_node_name, analysis_type, trans_type, drive_resistance, reduced_drive_resistance,
                                     basis_drive_resistance);
  }
  return _parasitic_arnoldi_model_cache[arnoldi_model_key];
}

RcModelBuilder::ArnoldiModelKey RcModelBuilder::getParasiticArnoldiModelKey(ParasiticNet& parasitic_net, std::string& source_node_name,
                                                                            AnalysisType analysis_type, TransType trans_type, double drive_resistance,
                                                                            double reduced_drive_resistance, double basis_drive_resistance)
{
  // Both source resistances alter the model: drive_resistance becomes a real
  // branch of the tree under ISTA_DRIVE_RES_SERIES, and
  // reduced_drive_resistance becomes the Krylov model's Norton conductance, which
  // changes the basis itself. Quantize both to 1 mOhm to keep the bucket count
  // bounded.
  int64_t rd_bucket = 0;
  if (drive_resistance > 0. && std::isfinite(drive_resistance))
    rd_bucket = static_cast<int64_t>(std::llround(drive_resistance * 1E6));
  int64_t reduced_rd_bucket = 0;
  if (reduced_drive_resistance > 0. && std::isfinite(reduced_drive_resistance))
    reduced_rd_bucket = static_cast<int64_t>(std::llround(reduced_drive_resistance * 1E6));
  int64_t basis_rd_bucket = 0;
  if (basis_drive_resistance > 0. && std::isfinite(basis_drive_resistance))
    basis_rd_bucket = static_cast<int64_t>(std::llround(basis_drive_resistance * 1E6));
  return std::make_tuple(parasitic_net.get_net_name(), source_node_name, analysis_type, trans_type, rd_bucket, reduced_rd_bucket, basis_rd_bucket);
}

ParasiticArnoldiModel RcModelBuilder::buildParasiticArnoldiModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                                                 TransType trans_type, double drive_resistance, double reduced_drive_resistance,
                                                                 double basis_drive_resistance)
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

  {
    // Diagnostic only: compare the tree model's summed capacitance against the
    // SPEF net so a mismatch is attributable to buildTree rather than to the
    // downstream solver. ISTA_DUMP_MODEL_CAP gates the log.
    static const char* mc = std::getenv("ISTA_DUMP_MODEL_CAP");
    if (mc != nullptr) {
      double sum_c = 0.;
      for (double cv : capacitance_list) sum_c += cv;
      double net_c = 0.;
      for (auto& kv : parasitic_net.get_node_map()) net_c += kv.second.get_capacitance();
      if (FILE* fp = std::fopen(mc, "a"); fp != nullptr) {
        std::fprintf(fp, "MC,%s,%s,%zu,%zu,%.12g,%.12g,%zu\n", parasitic_net.get_net_name().c_str(),
                     source_node_name.c_str(), node_name_list.size(),
                     parasitic_net.get_node_map().size(), sum_c, net_c,
                     parasitic_net.get_resistor_list().size());
        std::fclose(fp);
      }
    }
  }
  if (driveResSeriesEnabled() && drive_resistance > 0. && std::isfinite(drive_resistance) && node_name_list.size() > 1
      && node_name_list[0] != kSeriesSourceNodeName) {
    // Insert the driver resistance as a real branch above the original source.
    // buildTree emits the root at index 0 with parent -1, and every node's
    // parent index is smaller than its own, so the new root must occupy index 0
    // and shift everything by one to preserve that ordering invariant.
    // Capacitance of the new node is 0: total load is unchanged, which keeps
    // this purely a source-impedance change. resistance_list is in kOhm.
    node_name_list.insert(node_name_list.begin(), kSeriesSourceNodeName);
    capacitance_list.insert(capacitance_list.begin(), 0.);
    resistance_list.insert(resistance_list.begin(), drive_resistance);
    parent_idx_list.insert(parent_idx_list.begin(), -1);
    for (std::size_t node_idx = 1; node_idx < parent_idx_list.size(); ++node_idx) {
      if (parent_idx_list[node_idx] >= 0)
        parent_idx_list[node_idx] += 1;
      else
        parent_idx_list[node_idx] = 0;  // the old root now hangs from the new one
    }
    // The old root had resistance 0 (nothing above it); its branch is now rd.
    resistance_list[1] = drive_resistance;
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

  // reference resolves the Krylov basis once per net from the max/rise capacitance
  // and reuses it for all four corners, so the basis vector is built from that
  // corner's load rather than this one's. buildTree's topology is corner
  // independent (the tree is cached per net), so re-walking it only re-evaluates
  // the pin capacitances.
  std::vector<double> basis_capacitance_list = capacitance_list;
  if (krylovEnabled() && (analysis_type != AnalysisType::kMax || trans_type != TransType::kRise)) {
    std::vector<std::string> basis_node_list;
    std::vector<int32_t> basis_parent_list;
    std::vector<double> basis_resistance_list;
    std::vector<double> shared_capacitance_list;
    _rc.buildTree(parasitic_net, source_node_name, AnalysisType::kMax, TransType::kRise, basis_node_list, basis_parent_list, basis_resistance_list,
                  shared_capacitance_list);
    if (basis_node_list == node_name_list && shared_capacitance_list.size() == node_name_list.size()) {
      // The series root carries zero capacitance, so the shift applied above only
      // has to be mirrored for the synthetic node.
      if (!node_name_list.empty() && node_name_list.front() == kSeriesSourceNodeName) {
        shared_capacitance_list.insert(shared_capacitance_list.begin(), 0.);
      }
      if (shared_capacitance_list.size() == node_name_list.size())
        basis_capacitance_list = std::move(shared_capacitance_list);
    }
  }

  updateParasiticArnoldiModel(arnoldi_model, parasitic_net, node_name_list, parent_idx_list, resistance_list, capacitance_list,
                              basis_capacitance_list, term_point_idx_list, drive_resistance, reduced_drive_resistance,
                              basis_drive_resistance, analysis_type, trans_type);
  // Summed resistance is a tree approximation, so parallel edges and loops keep
  // the conductance-aware model built above. reference still applies its
  // reduced small-net response to those networks, so the lumped branch below
  // is allowed for them as well; ISTA_LOOPED_SMALLNET=0 restores the old guard.
  const bool tree_like = parasitic_net.get_resistor_list().size() + 1 == node_name_list.size();
  if (!tree_like && !loopedSmallNetEnabled())
    return arnoldi_model;
  const auto net = _database.network().nets().find(parasitic_net.get_net_name());
  if (!arnoldi_model.get_is_valid() || net == _database.network().nets().end() || net->second.get_driver_pin_list().size() != 1
      || net->second.get_driver_pin_list().front() != _rc.pinName(source_node_name))
    return arnoldi_model;
  const auto& loads = net->second.get_load_pin_list();
  if (loads.empty() || loads.size() + 1 != arnoldi_model.get_term_pin_list().size())
    return arnoldi_model;
  for (const auto& pin : loads)
    if (pin == _rc.pinName(source_node_name) || !arnoldi_model.get_term_index_map().contains(pin))
      return arnoldi_model;
  double wire_capacitance = 0.;
  for (const auto& [name, node] : parasitic_net.get_node_map())
    wire_capacitance += node.get_capacitance();
  const double resistance = _rc.totalResistance(parasitic_net) * 1e-3;
  {
    static const double rd_floor = [] {
      const char* e = std::getenv("ISTA_RD_LESS_THAN_RNET");
      return e != nullptr ? std::atof(e) : 0.0;
    }();
    if (rd_floor > 0. && std::isfinite(resistance) && resistance > 0. && std::isfinite(drive_resistance) && drive_resistance > 0.) {
      double rd = drive_resistance;
      if (rd < rd_floor * resistance)
        rd += rd;
      else
        rd += rd_floor * resistance;
      drive_resistance = rd;
    }
  }
  // Fanout ceiling for the small-net reduction branch. reference does not
  // publish the limit; it is exposed so the modelled fanout range can be swept
  // against reference instead of being fixed at the historical value.
  static const std::size_t fanout_limit = [] {
    const char* e = std::getenv("ISTA_FANOUT_LIMIT");
    return e != nullptr ? static_cast<std::size_t>(std::atoi(e)) : std::size_t{5};
  }();
  const double count = static_cast<double>(loads.size());
  PinCapacitanceCalculator pin_capacitance(_database);
  std::array<double, 2> constants, reduced_constants, total_constants;
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    double pin_load = pin_capacitance.driverCapacitance(_rc.pinName(source_node_name), analysis_type, edge);
    for (auto pin : loads)
      pin_load += pin_capacitance.pinCapacitance(pin, analysis_type, edge);
    if (!std::isfinite(pin_load) || pin_load < 0.)
      return arnoldi_model;
    // The enhanced balanced range is limited to five receivers. Other
    // networks use total RC for the ordinary low-RC cutoff and response.
    // The enhancement also bounds the reduction to 10% of total RC.
    const double total_load = wire_capacitance + pin_load;
    const double reduced_load = std::max(wire_capacitance / (count + 1.) + pin_load / count, .1 * total_load);
    // Ordinary total RC keeps priority: when the network already fits below the
    // small-net cutoff the plain total-RC value is the reference and the
    // enhanced fanout reduction only extends that range (see reduced_load).
    // Round 101 measured REF2022 directly: a small net's reported delay is
    // ln2 * R * (Cw/(n+1) + Cp/n) even when the total RC is already below the
    // 2 ps cutoff, so the spread form is the response value for n <= 5.
    // The plain total-RC constant is kept separately because the balanced flag
    // (source-correction policy) must not be decided by the smaller value.
    // reference's "Balanced case tree" response, which report_delay_calculation
    // prints verbatim for the constant-driven fanout cone:
    //   RC delay: (r_wire/load_count) * (c_pin + c_wire/load_count)
    // Algebraically R * (n*c_pin + c_wire) / n^2 == R * totalC / n^2.  Both
    // r_wire and c_wire are byte-identical to this builder's resistance and
    // wire_capacitance, and n*c_pin equals pin_load, so the printed form
    // collapses onto the total RC divided by n^2.  ArnoldiResponseCalculator
    // multiplies the constant by ln2, hence the divisor below.
    static const bool balanced_tree = [] {
      const char* e = std::getenv("ISTA_REF_BALANCED_TREE");
      return e == nullptr ? true : std::atoi(e) != 0;
    }();
    // Only the constant-driven cone takes this branch: reference announces
    // "RC delay calculation is being skipped because the driver from-pin is
    // constant" for exactly that population and still emits a number, while
    // every other net keeps the full RC solve.
    bool constant_driver = false;
    if (balanced_tree) {
      const auto& sim = _database.graph().simValues();
      for (bool as_driver : {true, false}) {
        const auto logic = sim.value(net->second.get_driver_pin_list().front(), as_driver);
        if (logic.has_value()
            && (*logic == TimingCaseValue::kZero || *logic == TimingCaseValue::kOne || *logic == TimingCaseValue::kStatic))
          constant_driver = true;
      }
    }
    double response_load = total_load;
    if (refBalancedEnabled() && loads.size() <= fanout_limit)
      response_load = wire_capacitance / (count + 1.) + pin_load / count;
    else if (constant_driver && count > 0.)
      response_load = total_load / (count * count * std::log(2.));
    constants[edge == TransType::kRise ? 0 : 1] = resistance * response_load;
    reduced_constants[edge == TransType::kRise ? 0 : 1] = resistance * reduced_load;
    total_constants[edge == TransType::kRise ? 0 : 1] = resistance * total_load;
    arnoldi_model.setTotalTimeConstants(total_constants);
    {
      static const bool ref_lumped = [] {
        const char* e = std::getenv("ISTA_REF_LUMPED_LN2");
        return e == nullptr ? true : std::atoi(e) != 0;
      }();
      if (ref_lumped && !constant_driver && loads.size() > fanout_limit)
        arnoldi_model.setLumpedMode(1);
    }
    {
      static const char* rc_dump = std::getenv("ISTA_DUMP_NET_RC");
      if (rc_dump != nullptr) {
        if (FILE* rf = std::fopen(rc_dump, "a"); rf != nullptr) {
          std::fprintf(rf, "%s,%d,%d,%zu,%.12g,%.12g,%.12g,%.12g\n", parasitic_net.get_net_name().c_str(),
                       static_cast<int>(analysis_type), static_cast<int>(edge), loads.size(), resistance,
                       total_load, resistance * total_load, wire_capacitance);
          std::fclose(rf);
        }
      }
    }
  }
  // Ordinary total RC has priority. The enhanced model extends the small-net
  // range; applying it inside the ordinary range understates every wire delay.
  const bool balanced
      = _config.small_rc_fanout_reduction != 0 && std::max(total_constants[0], total_constants[1]) >= smallNetCutoff() && loads.size() <= fanout_limit;
  if (balanced)
    constants = reduced_constants;
  // Both low-RC models use a 2 ps cutoff. Select both edges together;
  // never replace a partially annotated
  // or multi-driver network with this approximation.
  if (std::isfinite(resistance) && resistance > 0. && constants[0] >= 0. && constants[1] >= 0. && std::max(constants[0], constants[1]) < smallNetCutoff()) {
    const std::unordered_set<std::string> connected(node_name_list.begin(), node_name_list.end());
    for (const auto& [name, node] : parasitic_net.get_node_map())
      if (!connected.contains(name) || !std::isfinite(node.get_capacitance()) || node.get_capacitance() < 0.)
        return arnoldi_model;
    for (auto& resistor : parasitic_net.get_resistor_list())
      if (!connected.contains(resistor.get_source_node()) || !connected.contains(resistor.get_sink_node()) || !std::isfinite(resistor.get_resistance())
          || resistor.get_resistance() < 0.)
        return arnoldi_model;
    static const int force_corr = [] {
      const char* e = std::getenv("ISTA_FORCE_SMALLNET_CORR");
      return e != nullptr ? std::atoi(e) : 2;
    }();
    static const double corr_rc_max = [] {
      const char* e = std::getenv("ISTA_SMALLNET_CORR_RC_MAX_PS");
      return e != nullptr ? std::atof(e) * 1e-3 : 5.0e-3;
    }();
    // 1 = always, 2 = only when tau <= corr_rc_max
    bool apply_corr = balanced;
    if (force_corr == 1) {
      apply_corr = true;
    } else if (force_corr == 2) {
      const double tau_max = std::max(constants[0], constants[1]);
      apply_corr = (corr_rc_max >= 0.0) ? (tau_max <= corr_rc_max) : true;
    }
    static const int corr_fanout = [] {
      const char* e = std::getenv("ISTA_SMALLNET_CORR_FANOUT");
      return e != nullptr ? std::atoi(e) : static_cast<int>(fanout_limit);
    }();
    if (corr_fanout >= 0 && static_cast<int>(loads.size()) > corr_fanout)
      apply_corr = false;
    arnoldi_model.setSmallNetTimeConstants(constants, apply_corr);
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
                                                 std::vector<double>& capacitance_list, std::vector<double>& basis_capacitance_list,
                                                 std::vector<std::size_t>& term_point_idx_list, double drive_resistance,
                                                 double reduced_drive_resistance, double basis_drive_resistance, AnalysisType analysis_type,
                                                 TransType trans_type)
{
  double total_capacitance = 0.0;
  for (double capacitance : capacitance_list) {
    total_capacitance += capacitance;
  }
  if (total_capacitance <= 0.0) {
    return;
  }

  std::size_t node_num = capacitance_list.size();
  // The reduced response must be able to separate the receiver terminals of a
  // multi-fanout net; a fixed small order cannot, and the per-terminal slew
  // then collapses onto the driver value.
  static const int32_t max_order = [] {
    const char* e = std::getenv("ISTA_ARNOLDI_MAX_ORDER");
    return e != nullptr ? static_cast<int32_t>(std::atoi(e)) : int32_t{6};
  }();
  int32_t order = std::min(static_cast<int32_t>(node_num), max_order);
  double sqrt_total_capacitance = std::sqrt(total_capacitance);
  std::vector<double> current_basis_list(node_num, 1.0 / sqrt_total_capacitance);
  std::vector<double> previous_basis_list(node_num, 0.0);
  std::vector<double> diagonal_list(order, 0.0);
  std::vector<double> off_diagonal_list(order, 0.0);
  std::vector<std::vector<double>> projection_list(order, std::vector<double>(term_point_idx_list.size(), 0.0));
  int32_t final_order = order;
  std::vector<bool> zero_capacitance_terms;
  {
    static const bool from_spef = [] {
      const char* e = std::getenv("ISTA_ZERO_CAP_FROM_SPEF");
      return e != nullptr && *e != '0';
    }();
    const std::size_t shift_zero = (node_num > 0 && !node_name_list.empty() && node_name_list.front() == kSeriesSourceNodeName) ? 1 : 0;
    for (auto node : term_point_idx_list) {
      if (!from_spef) {
        zero_capacitance_terms.push_back(capacitance_list[node] == 0.);
        continue;
      }
      const std::size_t bare = node >= shift_zero ? node - shift_zero : node;
      bool is_zero = false;
      if (bare < node_name_list.size()) {
        const auto it = parasitic_net.get_node_map().find(node_name_list[bare]);
        if (it != parasitic_net.get_node_map().end())
          is_zero = it->second.get_capacitance() == 0.;
      }
      zero_capacitance_terms.push_back(is_zero);
    }
  }
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
      static const double conductance_limit = [] {
        const char* env = std::getenv("ISTA_NO_CONDUCTANCE_CLAMP");
        if (env != nullptr && *env != '0') {
          return std::numeric_limits<double>::infinity();
        }
        return std::nextafter(1E5, 0.);
      }();
      const double raw_conductance = 1.0 / (parasitic_resistor.get_resistance() * 1E-3);
      double conductance = std::min(raw_conductance, conductance_limit);
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

    static const bool ref_two_pole = [] {
      const char* env = std::getenv("ISTA_REF_2POLE");
      return env != nullptr && *env != '0';
    }();
    if (ref_two_pole && order_idx == 0) {
      // reference resolves the reduced basis once per net and then reuses it for every
      // corner: its dumped M_V is bit-identical across rise/fall and min/max, while
      // A/C/b all change. Probing 16 combinations of (reference corner, c corner) on
      // net _04446_ shows the basis matches the max/rise capacitance vector to 3.5e-6
      // and any min-corner vector to only 1.5e-2, so the basis is built from the
      // max/rise pin load and cached. Reproducing that here keeps iSTA's v1 equal to
      // reference's for all four corners instead of only for max/rise.
      // The basis is built from this corner's capacitance. reference's dumped M_V is
      // identical across corners, and probing 16 (corner, c-corner) combinations on
      // net _04446_ showed its v1 matches the max/rise pin load (3.5e-6) far better
      // than any min-corner load (1.5e-2). Forcing that load here is worse in practice
      // (picorv32a INTERCONNECT 27.9% vs 5.8%) because A and c11 stay per-corner, so
      // the reduced model would mix two capacitance vectors; keeping them consistent
      // matters more than matching the basis in isolation.
      std::vector<double> basis_capacitance = capacitance_list;
      static const bool ref_shared_basis = [] {
        const char* env = std::getenv("ISTA_REF_SHARED_BASIS");
        return env != nullptr && *env != '0';
      }();
      if (ref_shared_basis) {
        // buildTree charges the root with driverCapacitance and every other node with
        // pinCapacitance, so the two must be re-evaluated through the same accessor.
        for (std::size_t node = 0; node < node_num; node++) {
          std::string node_name = node_name_list[node];
          std::string pin_name = _rc.pinName(node_name);
          if (pin_name.empty() || _database.network().pins().count(pin_name) == 0) {
            continue;
          }
          const double shared = node == 0 ? _rc.pinCapacitance().driverCapacitance(pin_name, AnalysisType::kMax, TransType::kRise)
                                          : _rc.pinCapacitance().pinCapacitance(pin_name, AnalysisType::kMax, TransType::kRise);
          const double local = node == 0 ? _rc.pinCapacitance().driverCapacitance(pin_name, analysis_type, trans_type)
                                         : _rc.pinCapacitance().pinCapacitance(pin_name, analysis_type, trans_type);
          basis_capacitance[node] += shared - local;
        }
      }
      double mean = 0.;
      for (double value : response_list) {
        mean += value;
      }
      mean /= static_cast<double>(node_num);
      double norm = 0.;
      for (double value : response_list) {
        norm += (value - mean) * (value - mean);
      }
      norm = std::sqrt(norm);
      if (norm > 0. && std::isfinite(norm)) {
        std::vector<double> v1(node_num, 0.);
        for (std::size_t node = 0; node < node_num; node++) {
          v1[node] = (response_list[node] - mean) / norm;
        }
        double sum_c = 0.;
        double sum_cv1 = 0.;
        double sum_cv1sq = 0.;
        for (std::size_t node = 0; node < node_num; node++) {
          // A is per corner (reference's A[0][0] tracks each corner's total load), so the
          // quadratic forms use this corner's capacitance even when the basis does not.
          sum_c += capacitance_list[node];
          sum_cv1 += capacitance_list[node] * v1[node];
          sum_cv1sq += capacitance_list[node] * v1[node] * v1[node];
        }
        ParasiticArnoldiModel::TwoPoleModel reduced;
        reduced.valid = true;
        reduced.u0 = -1. / std::sqrt(static_cast<double>(node_num));
        reduced.u1 = v1[0];
        reduced.a00 = sum_c / static_cast<double>(node_num);
        reduced.a01 = reduced.u0 * sum_cv1;
        reduced.a11 = sum_cv1sq;
        for (std::size_t node = 1; node < node_num; node++) {
          const int32_t parent = parent_idx_list[node];
          const double branch = resistance_list[node];
          if (parent < 0 || !(branch > 0.)) {
            continue;
          }
          const double drop = v1[node] - v1[static_cast<std::size_t>(parent)];
          reduced.c11 += drop * drop / branch;
        }
        reduced.term_v1.reserve(term_point_idx_list.size());
        for (auto term : term_point_idx_list) {
          reduced.term_v1.push_back(v1[term]);
        }
        {
          static const char* two_pole_dump = std::getenv("ISTA_DUMP_REF_2");
          static FILE* two_pole_fp = two_pole_dump != nullptr ? std::fopen(two_pole_dump, "a") : static_cast<FILE*>(nullptr);
          if (two_pole_fp != nullptr) {
            std::fprintf(two_pole_fp, "PT2,%s,%zu,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g\n", parasitic_net.get_net_name().c_str(), node_num,
                         reduced.a00, reduced.a01, reduced.a11, reduced.c11, reduced.u0, reduced.u1, sum_c, drive_resistance);
          }
        }
        arnoldi_model.setTwoPole(std::move(reduced));
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
  {
    static const char* order_dump = std::getenv("ISTA_DUMP_ARNOLDI_ORDER");
    if (order_dump != nullptr) {
      if (FILE* order_fp = std::fopen(order_dump, "a"); order_fp != nullptr) {
        std::fprintf(order_fp, "%zu,%d,%d\n", node_num, max_order, final_order);
        std::fclose(order_fp);
      }
    }
  }
  arnoldi_model.set_order(final_order);
  arnoldi_model.set_total_capacitance(total_capacitance);
  arnoldi_model.set_sqrt_total_capacitance(sqrt_total_capacitance);
  arnoldi_model.set_diagonal_list(diagonal_list);
  arnoldi_model.set_off_diagonal_list(off_diagonal_list);
  arnoldi_model.set_projection_list(projection_list);

  // reference's model replaces the whole tridiagonal reduction: its A and Gr are
  // full order x order matrices, so the diagonal/off-diagonal pair above cannot
  // represent it. The reduced model travels separately and poleResidue() prefers
  // it whenever it is valid. get_order() then reports reference's order, which is
  // what the pole/residue consumers size themselves from.
  // reduced_drive_resistance is the driver resistance the caller resolved for the
  // reduction; it falls back to drive_resistance so the series-insertion mode
  // still produces a model.
  if (krylovEnabled()) {
    const double krylov_rd = (reduced_drive_resistance > 0. && std::isfinite(reduced_drive_resistance)) ? reduced_drive_resistance : drive_resistance;
    ParasiticArnoldiModel::KrylovModel reduced;
    if (buildKrylovModel(reduced, parasitic_net, node_name_list, capacitance_list, basis_capacitance_list, term_point_idx_list, krylov_rd,
                           basis_drive_resistance)) {
      arnoldi_model.set_order(reduced.order);
      arnoldi_model.setKrylov(std::move(reduced));
    }
  }
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
