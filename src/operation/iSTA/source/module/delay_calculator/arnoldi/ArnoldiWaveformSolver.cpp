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
#include "ArnoldiWaveformSolver.hpp"

#include <Eigen/Eigenvalues>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

#include "ThresholdNormalizer.hpp"
#include "TimingTolerance.hpp"

namespace ista {
namespace {
// Integral from 0 to x of (1 - exp(-u)). Direct subtraction loses the
// charge of slow modes when their time constant greatly exceeds the ramp.
double integratedDecay(double x)
{
  if (x < 1e-3) {
    return x * x * (.5 + x * (-1. / 6. + x * (1. / 24. + x * (-1. / 120. + x / 720.))));
  }
  return x + std::expm1(-x);
}
}  // namespace

std::optional<InputPortDelayResult> ArnoldiWaveformSolver::inputPortResponse(const ParasiticArnoldiModel& model, TransType transition, double input_slew,
                                                                             const WaveformThresholds& source, const std::vector<WaveformThresholds>& loads)
{
  const auto term_count = model.get_term_node_list().size();
  // This path consumes the tridiagonal reduction only; the reference Krylov model is a
  // dense one and belongs to poleResidue(). get_order() can report the reference order,
  // so the tridiagonal size is derived from the arrays themselves.
  const auto model_order = std::min<int32_t>(model.get_order(), static_cast<int32_t>(model.get_diagonal_list().size()));
  if (!model.get_is_valid() || model_order <= 0 || !std::isfinite(input_slew) || input_slew < 0.0 || loads.size() != term_count
      || model.get_diagonal_list().size() < static_cast<std::size_t>(model_order)
      || model.get_off_diagonal_list().size() < static_cast<std::size_t>(model_order - 1)
      || model.get_projection_list().size() < static_cast<std::size_t>(model_order)) {
    return std::nullopt;
  }
  for (int32_t basis = 0; basis < model_order; ++basis) {
    if (model.get_projection_list()[basis].size() < term_count) {
      return std::nullopt;
    }
  }
  if (!(source.lower > 0.0 && source.upper < 1.0 && source.upper > source.lower && source.derate > 0.0 && source.delay > 0.0 && source.delay < 1.0)) {
    return std::nullopt;
  }
  // The port imposes a voltage waveform. Use zero source resistance and the
  // reduced RC response, retaining the fast modes lost by an Elmore estimate.
  int32_t order = model_order;
  Eigen::MatrixXd matrix = Eigen::MatrixXd::Zero(order, order);
  for (int32_t index = 0; index < order; ++index) {
    matrix(index, index) = model.get_diagonal_list()[index];
    if (index + 1 < order) {
      matrix(index, index + 1) = matrix(index + 1, index) = model.get_off_diagonal_list()[index];
    }
  }
  Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(matrix);
  if (solver.info() != Eigen::Success || !solver.eigenvalues().allFinite()) {
    return std::nullopt;
  }
  std::vector<double> time_constants(order);
  double scale = solver.eigenvalues().cwiseAbs().maxCoeff();
  for (int32_t index = 0; index < order; ++index) {
    double value = solver.eigenvalues()[index];
    if (value < -scale * 1E-12) {
      return std::nullopt;
    }
    time_constants[index] = value > scale * 1E-12 ? value : 0.0;
  }
  const bool is_fall = transition == TransType::kFall;
  double ramp = input_slew * source.derate / (source.upper - source.lower);
  double source_time = (is_fall ? 1.0 - source.delay : source.delay) * ramp;
  InputPortDelayResult result;
  result.delays.resize(term_count);
  result.slews.resize(term_count);
  for (std::size_t term_idx = 0; term_idx < term_count; ++term_idx) {
    std::vector<double> weights(order, 0.0);
    const bool algebraic = term_idx < model.zeroCapacitanceTerms().size() && model.zeroCapacitanceTerms()[term_idx];
    for (int32_t index = 0; index < order; ++index) {
      double projection = 0.0;
      for (int32_t basis = 0; basis < order; ++basis) {
        projection += solver.eigenvectors()(basis, index) * model.get_projection_list()[basis][term_idx];
      }
      if (algebraic) {
        if (model.responseProjection().size() < static_cast<std::size_t>(order))
          return std::nullopt;
        projection = 0.;
        for (int32_t basis = 0; basis < order; ++basis) {
          if (model.responseProjection()[basis].size() <= term_idx)
            return std::nullopt;
          projection += solver.eigenvectors()(basis, index) * model.responseProjection()[basis][term_idx];
        }
        projection = time_constants[index] > 0. ? projection / time_constants[index] : 0.;
      }
      weights[index] = model.get_sqrt_total_capacitance() * solver.eigenvectors()(0, index) * projection;
    }
    auto terminal_constants = time_constants;
    if (algebraic) {
      terminal_constants.push_back(0.);
      weights.push_back(1. - std::accumulate(weights.begin(), weights.end(), 0.));
    }
    const auto& load = loads[term_idx];
    if (!(load.delay > 0.0 && load.delay < 1.0 && load.lower > 0.0 && load.upper < 1.0 && load.upper > load.lower && load.derate > 0.0)) {
      return std::nullopt;
    }
    double middle_time = inputPortCrossing(terminal_constants, weights, ramp, is_fall ? load.delay : 1.0 - load.delay);
    double early_time = inputPortCrossing(terminal_constants, weights, ramp, is_fall ? load.upper : 1.0 - load.lower);
    double late_time = inputPortCrossing(terminal_constants, weights, ramp, is_fall ? load.lower : 1.0 - load.upper);
    if (!std::isfinite(middle_time) || !std::isfinite(early_time) || !std::isfinite(late_time) || late_time < early_time) {
      return std::nullopt;
    }
    result.delays[term_idx] = term_idx == 0 ? 0.0 : middle_time - source_time;
    result.slews[term_idx] = term_idx == 0 ? input_slew : (late_time - early_time) / load.derate;
    {
      static const char* tw = std::getenv("ISTA_DUMP_TERMW");
      if (tw != nullptr) {
        if (FILE* fp = std::fopen(tw, "a"); fp != nullptr) {
          std::fprintf(fp, "TW,%p,%zu,%d,%.15g,%.15g,%.15g,%.15g,%.15g,%zu", static_cast<const void*>(&model), term_idx,
                       is_fall ? 1 : 0, ramp, middle_time, early_time, late_time, result.slews[term_idx],
                       time_constants.size());
          for (std::size_t index = 0; index < weights.size(); ++index)
            std::fprintf(fp, ",%.15g", weights[index]);
          std::fprintf(fp, ",TC");
          for (std::size_t index = 0; index < time_constants.size(); ++index)
            std::fprintf(fp, ",%.15g", time_constants[index]);
          std::fprintf(fp, "\n");
          std::fclose(fp);
        }
      }
    }
  }
  return result;
}

ParasiticArnoldiPoleResidue ArnoldiWaveformSolver::poleResidue(const ParasiticArnoldiModel& arnoldi_model, double drive_resistance)
{
  ParasiticArnoldiPoleResidue pole_residue;
  if (!arnoldi_model.get_is_valid() || arnoldi_model.get_order() <= 0) {
    return pole_residue;
  }

  std::vector<double> diagonal_list = arnoldi_model.get_diagonal_list();
  std::vector<double> off_diagonal_list = arnoldi_model.get_off_diagonal_list();
  // The reference Krylov model overrides get_order() with its own reduced order.
  // The tridiagonal fallback below must therefore size itself from the lists it
  // was actually built with instead of from the reported order.
  const int32_t legacy_order = std::min<int32_t>(std::max<int32_t>(arnoldi_model.get_order(), 0), static_cast<int32_t>(diagonal_list.size()));
  if (arnoldi_model.twoPole().valid && drive_resistance > 0. && std::isfinite(drive_resistance)) {
    const auto& reduced = arnoldi_model.twoPole();
    const double gd = 1. / drive_resistance;
    const double a00 = reduced.a00;
    const double a01 = reduced.a01;
    const double a11 = reduced.a11;
    const double det_a = a00 * a11 - a01 * a01;
    if (det_a > 0.) {
      const double c00 = gd * reduced.u0 * reduced.u0;
      const double c01 = gd * reduced.u0 * reduced.u1;
      const double c11 = reduced.c11 + gd * reduced.u1 * reduced.u1;
      // M = A^-1 C, formed explicitly because both are 2x2.
      const double m00 = (a11 * c00 - a01 * c01) / det_a;
      const double m01 = (a11 * c01 - a01 * c11) / det_a;
      const double m10 = (a00 * c01 - a01 * c00) / det_a;
      const double m11 = (a00 * c11 - a01 * c01) / det_a;
      const double trace = m00 + m11;
      const double det_m = m00 * m11 - m01 * m10;
      const double disc = 0.25 * trace * trace - det_m;
      if (disc >= 0.) {
        const double root = std::sqrt(disc);
        const double w_pair[2] = {0.5 * trace - root, 0.5 * trace + root};
        if (w_pair[0] > 0. && w_pair[1] > 0.) {
          const std::vector<double>& term_v1 = reduced.term_v1;
          const std::size_t term_num = arnoldi_model.get_term_node_list().size();
          const bool shape_ok = term_v1.size() == term_num;
          if (shape_ok) {
            std::vector<double> poles(2, 0.);
            std::vector<std::vector<double>> residues(term_num, std::vector<double>(2, 0.));
            bool usable = true;
            for (int k = 0; k < 2 && usable; k++) {
              const double w = w_pair[k];
              double vec0 = m01;
              double vec1 = w - m00;
              if (std::fabs(vec0) + std::fabs(vec1) < 1e-300) {
                vec0 = w - m11;
                vec1 = m10;
              }
              const double scale = std::sqrt(vec0 * (a00 * vec0 + a01 * vec1) + vec1 * (a01 * vec0 + a11 * vec1));
              if (!(std::fabs(scale) > 0.) || !std::isfinite(scale)) {
                usable = false;
                break;
              }
              const double p0 = vec0 / scale;
              const double p1 = vec1 / scale;
              const double pb = p0 * (gd * reduced.u0) + p1 * (gd * reduced.u1);
              // voltageAt builds exp(-pole*time), so pole_list carries rates, which is
              // also what reference stores in P[+0x90] (min w = P[+0x98] = slowest rate).
              poles[k] = w;
              for (std::size_t term = 0; term < term_num; term++) {
                const double pv = p0 * reduced.u0 + p1 * term_v1[term];
                residues[term][k] = pb * pv / w;
              }
            }
            if (usable) {
              {
                static const char* p2d = std::getenv("ISTA_DUMP_REF_2POLES");
                static FILE* p2f = p2d != nullptr ? std::fopen(p2d, "a") : static_cast<FILE*>(nullptr);
                if (p2f != nullptr) {
                  std::fprintf(p2f, "P2,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g\n", w_pair[0],
                               w_pair[1], gd, reduced.c11, a00, a11, reduced.u0, reduced.u1, reduced.term_v1[0],
                               reduced.term_v1[term_num > 4 ? 4 : 0], residues[0][0], residues[0][1]);
                }
              }
              pole_residue.set_is_valid(true);
              pole_residue.set_order(2);
              pole_residue.set_pole_list(poles);
              pole_residue.set_residue_list(residues);
              return pole_residue;
            }
          }
        }
      }
    }
  }
  // reference's general Krylov model, built by
  // RcModelBuilder::updateParasiticArnoldiModel when ISTA_REF_KRYLOV is on and the
  // caller supplies a drive resistance. The reduced system is
  //   A = V^T diag(c) V ,  Gr = V^T G V ,  br = V^T b ,  b = gd * e_driver
  // with an ordinary 2-norm Arnoldi basis, gd = 1/rd in mS, c in pF, so the
  // eigenvalues of A^-1 Gr are rates in 1/ns. With A = L L^T,
  //   B = L^-1 Gr L^-T ,  (w, Q) = eigh(B) ,  p_k = L^-T q_k  (p_k^T A p_k = 1)
  // give the modal expansion of every terminal transfer function,
  //   c_k[i] = (p_k . br)(p_k . V_i) / w_k ,  sum_k c_k[i] = 1 ,
  // which is precisely the coefficient pair voltageAt() consumes (rates in
  // pole_list, expansion weights in residue_list). Any failure leaves the model
  // untouched so the historical tridiagonal path below still runs.
  {
    static const char* pd = std::getenv("ISTA_DUMP_PATH");
    if (pd != nullptr) {
      if (FILE* f = std::fopen(pd, "a"); f != nullptr) {
        extern thread_local const char* g_dump_out_pin;
        std::fprintf(f, "PATH,%s,rd=%.15g,krylov_valid=%d,krylov_order=%d,order=%d,diag_n=%zu,Ctot=%.15g,sqrtCtot=%.15g\n",
                     g_dump_out_pin, drive_resistance, arnoldi_model.krylov().valid ? 1 : 0,
                     static_cast<int>(arnoldi_model.krylov().order), static_cast<int>(arnoldi_model.get_order()),
                     arnoldi_model.get_diagonal_list().size(), arnoldi_model.get_total_capacitance(),
                     arnoldi_model.get_sqrt_total_capacitance());
        std::fclose(f);
      }
    }
  }
  if (arnoldi_model.krylov().valid && arnoldi_model.krylov().order > 0) {
    const auto& reduced = arnoldi_model.krylov();
    const int32_t krylov_order = reduced.order;
    const std::size_t krylov_size = static_cast<std::size_t>(krylov_order);
    const std::size_t term_num = arnoldi_model.get_term_node_list().size();
    bool shape_ok = reduced.a.size() == krylov_size && reduced.g.size() == krylov_size && reduced.b.size() == krylov_size
                    && reduced.v.size() == term_num;
    for (std::size_t row = 0; row < krylov_size && shape_ok; row++)
      shape_ok = reduced.a[row].size() == krylov_size && reduced.g[row].size() == krylov_size;
    for (std::size_t term = 0; term < term_num && shape_ok; term++)
      shape_ok = reduced.v[term].size() == krylov_size;
    {
      static const char* sh = std::getenv("ISTA_DUMP_SHAPE");
      if (sh != nullptr) {
        extern thread_local const char* g_dump_out_pin;
        if (FILE* f = std::fopen(sh, "a"); f != nullptr) {
          std::fprintf(f, "SH,%s,krylov_size=%zu,term_num=%zu,a=%zu,g=%zu,b=%zu,v=%zu,shape_ok=%d\n", g_dump_out_pin,
                       krylov_size, term_num, reduced.a.size(), reduced.g.size(), reduced.b.size(), reduced.v.size(),
                       shape_ok ? 1 : 0);
          std::fclose(f);
        }
      }
    }
    if (shape_ok) {
      static const bool rd_live = [] {
        const char* e = std::getenv("ISTA_KRYLOV_LIVE_RD");
        return e == nullptr || *e != '0';
      }();
      const bool can_move_gd = rd_live && reduced.driver_conductance > 0. && std::isfinite(reduced.driver_conductance)
                               && drive_resistance > 0. && std::isfinite(drive_resistance)
                               && reduced.driver_projection.size() == krylov_size;
      const double gd_build = reduced.driver_conductance;
      const double gd_now = drive_resistance > 0. && std::isfinite(drive_resistance) ? 1. / drive_resistance : gd_build;
      const double gd_delta = can_move_gd ? gd_now - gd_build : 0.;
      const double gd_ratio = can_move_gd ? gd_now / gd_build : 1.;
      Eigen::MatrixXd krylov_a(krylov_order, krylov_order);
      Eigen::MatrixXd krylov_g(krylov_order, krylov_order);
      Eigen::VectorXd krylov_b(krylov_order);
      for (int32_t row = 0; row < krylov_order; row++) {
        for (int32_t column = 0; column < krylov_order; column++) {
          krylov_a(row, column) = reduced.a[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
          double g_entry = reduced.g[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
          if (can_move_gd)
            g_entry += gd_delta * reduced.driver_projection[static_cast<std::size_t>(row)] * reduced.driver_projection[static_cast<std::size_t>(column)];
          krylov_g(row, column) = g_entry;
        }
        krylov_b[row] = reduced.b[static_cast<std::size_t>(row)] * gd_ratio;
      }
      bool usable = krylov_a.allFinite() && krylov_g.allFinite() && krylov_b.allFinite();
      Eigen::LLT<Eigen::MatrixXd> cholesky;
      if (usable) {
        cholesky.compute(krylov_a);
        usable = cholesky.info() == Eigen::Success;
      }
      if (usable) {
        const Eigen::MatrixXd lower = cholesky.matrixL();
        const Eigen::MatrixXd upper = lower.transpose();
        const Eigen::MatrixXd inverse_g = lower.triangularView<Eigen::Lower>().solve(krylov_g);
        // B = L^-1 Gr L^-T; eigh only reads one triangle, so round-off asymmetry is harmless.
        const Eigen::MatrixXd reduced_g = lower.triangularView<Eigen::Lower>().solve(inverse_g.transpose()).transpose();
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eigen_solver(reduced_g);
        usable = eigen_solver.info() == Eigen::Success && eigen_solver.eigenvalues().allFinite();
        if (usable) {
          // p_k = L^-T q_k normalises the modes so that p_k^T A p_k = 1.
          const Eigen::MatrixXd modes = upper.triangularView<Eigen::Upper>().solve(eigen_solver.eigenvectors());
          usable = modes.allFinite();
          std::vector<double> poles(krylov_size, 0.);
          std::vector<std::vector<double>> residues(term_num, std::vector<double>(krylov_size, 0.));
          for (int32_t mode = 0; mode < krylov_order && usable; mode++) {
            const double rate = eigen_solver.eigenvalues()[static_cast<Eigen::Index>(mode)];
            if (!(rate > 0.) || !std::isfinite(rate)) {
              usable = false;
              break;
            }
            // voltageAt evaluates exp(-pole*time), so pole_list carries the rates.
            poles[static_cast<std::size_t>(mode)] = rate;
            const double source_projection = modes.col(mode).dot(krylov_b);
            for (std::size_t term = 0; term < term_num; term++) {
              double term_projection = 0.;
              for (int32_t basis_idx = 0; basis_idx < krylov_order; basis_idx++)
                term_projection += modes(basis_idx, mode) * reduced.v[term][static_cast<std::size_t>(basis_idx)];
              residues[term][static_cast<std::size_t>(mode)] = source_projection * term_projection / rate;
            }
          }
          if (usable) {
            {
              // Diagnostic: compare this reduction against reference's dumped
              // poles and against an independent build of the same model.
              static const char* pk = std::getenv("ISTA_DUMP_REF_K");
              if (pk != nullptr) {
                extern thread_local const char* g_dump_out_pin;
                if (FILE* pf = std::fopen(pk, "a"); pf != nullptr) {
                  std::fprintf(pf, "PK,%s,%d,rd=%.9g,gd=%.9g", g_dump_out_pin, static_cast<int>(krylov_order), drive_resistance,
                               1. / drive_resistance);
                  for (std::size_t mode = 0; mode < krylov_size; mode++) std::fprintf(pf, ",w=%.9g", poles[mode]);
                  for (std::size_t term = 0; term < term_num; term++) {
                    double sum = 0.;
                    for (std::size_t mode = 0; mode < krylov_size; mode++) sum += residues[term][mode];
                    std::fprintf(pf, ",s%d=%.9g", static_cast<int>(term), sum);
                  }
                  std::fprintf(pf, "\n");
                  std::fclose(pf);
                }
              }
            }
            pole_residue.set_is_valid(true);
            pole_residue.set_order(krylov_order);
            static const bool krylov_algebraic = [] {
              const char* e = std::getenv("ISTA_KRYLOV_ALGEBRAIC");
              return e == nullptr || *e != '0';
            }();
            const auto& zero_terms = arnoldi_model.krylov().zero_capacitance_terms.empty()
                                         ? arnoldi_model.zeroCapacitanceTerms()
                                         : arnoldi_model.krylov().zero_capacitance_terms;
            const bool any_zero = std::find(zero_terms.begin(), zero_terms.end(), true) != zero_terms.end();
            if (krylov_algebraic && any_zero) {
              poles.push_back(std::numeric_limits<double>::infinity());
              for (std::size_t term = 0; term < residues.size(); ++term) {
                double direct = 0.;
                if (term < zero_terms.size() && zero_terms[term]) {
                  double sum = 0.;
                  for (double r : residues[term]) sum += r;
                  direct = 1. - sum;
                }
                residues[term].push_back(direct);
              }
            }
            pole_residue.set_pole_list(poles);
            pole_residue.set_residue_list(residues);
            {
              static const char* al = std::getenv("ISTA_DUMP_ALGEBRAIC");
              if (al != nullptr) {
                extern thread_local const char* g_dump_out_pin;
                if (FILE* af = std::fopen(al, "a"); af != nullptr) {
                  const auto& q = arnoldi_model.zeroCapacitanceTerms();
                  const auto& tpl_a = arnoldi_model.get_term_pin_list();
                  int32_t nalg = 0;
                  for (std::size_t t = 0; t < q.size(); ++t)
                    if (q[t]) ++nalg;
                  std::fprintf(af, "ALG,%s,order=%d,terms=%zu,algebraic=%d", g_dump_out_pin, krylov_order, q.size(), nalg);
                  for (std::size_t t = 0; t < q.size(); ++t) {
                    if (!q[t]) continue;
                    std::fprintf(af, ",%s", t < tpl_a.size() ? tpl_a[t].c_str() : "?");
                  }
                  std::fprintf(af, "\n");
                  std::fclose(af);
                }
              }
            }
            return pole_residue;
          }
        }
      }
    }
  }
  // reference treats rd as an external source impedance (report_driver_model,
  // Tool Commands U-2022.12 p.1483). iSTA instead folds rd*C_total into the
  // reduced Arnoldi [0][0], which is a rank-1 modification of the reduced
  // matrix rather than of the original network. Scaling this
  // term is the only knob that changes how rd enters the model, so sweep it to
  // test whether the fold-in is the residual slew error.
  static const double rdc_scale = [] {
    const char* e = std::getenv("ISTA_ARNOLDI_RDC_SCALE");
    return e != nullptr ? std::atof(e) : 1.0;
  }();
  // ISTA_DRIVE_RES_SERIES=1 means the caller already placed rd as a real branch
  // of the original RC tree, so folding rd*C_total in again would double count
  // it. The fold also cannot represent the distributed interaction between rd
  // and the network R, which is the residual this mode removes.
  static const bool rd_is_series = [] {
    const char* e = std::getenv("ISTA_DRIVE_RES_SERIES");
    return e != nullptr && *e != '0';
  }();
  const double rd_c_term = rd_is_series ? 0. : rdc_scale * drive_resistance * arnoldi_model.get_total_capacitance();
  diagonal_list.front() += rd_c_term;
  {
    static const char* pr = std::getenv("ISTA_DUMP_TAU");
    static const double want_rd = [] {
      const char* e = std::getenv("ISTA_DUMP_TAU_RD");
      return e != nullptr ? std::atof(e) : -1.0;
    }();
    const bool want = pr != nullptr
        && (want_rd < 0. || std::abs(drive_resistance - want_rd) < 1e-4);
    static FILE* tf = [] {
      const char* q = std::getenv("ISTA_DUMP_TAU");
      return q != nullptr ? std::fopen(q, "a") : static_cast<FILE*>(nullptr);
    }();
    if (want && tf != nullptr) {
      FILE* const fp = tf;
      std::fprintf(fp, "TAU,rd=%.9f,Ctot=%.12f,rdC=%.12f,diag0_before=%.12f,diag0_after=%.12f,nord=%d,tid=%p\n",
                   drive_resistance, arnoldi_model.get_total_capacitance(), rd_c_term,
                   diagonal_list.front() - rd_c_term, diagonal_list.front(),
                   static_cast<int>(arnoldi_model.get_order()),
                   static_cast<const void*>(&arnoldi_model));
      for (int32_t q = 0; q < legacy_order && q < 8; ++q)
        std::fprintf(fp, "TAUD,%d,%.12e,%.12e\n", q, diagonal_list[q],
                     q + 1 < legacy_order ? off_diagonal_list[q] : 0.0);
    }
  }

  std::vector<double> eigenvalue_list;
  std::vector<std::vector<double>> eigenvector_list;
  if (!solveTridiagonal(diagonal_list, off_diagonal_list, eigenvalue_list, eigenvector_list)) {
    return pole_residue;
  }

  int32_t order = legacy_order;
  std::size_t term_num = arnoldi_model.get_term_node_list().size();
  std::vector<double> pole_list(order, 0.0);
  for (int32_t order_idx = 0; order_idx < order; order_idx++) {
    // Positive time constants have no absolute floor: clamping a fast pole
    // changes the current integral and can multiply the effective capacitance.
    pole_list[order_idx] = eigenvalue_list[order_idx] > 0. ? 1. / eigenvalue_list[order_idx] : std::numeric_limits<double>::infinity();
  }

  std::vector<std::vector<double>> residue_list(term_num, std::vector<double>(order, 0.0));
  for (int32_t order_idx = 0; order_idx < order; order_idx++) {
    double source_projection = arnoldi_model.get_sqrt_total_capacitance() * eigenvector_list[order_idx][0];
    for (std::size_t term_idx = 0; term_idx < term_num; term_idx++) {
      double term_projection = 0.0;
      for (int32_t basis_idx = 0; basis_idx < order; basis_idx++) {
        term_projection += eigenvector_list[order_idx][basis_idx] * arnoldi_model.get_projection_list()[basis_idx][term_idx];
      }
      const auto& algebraic = arnoldi_model.zeroCapacitanceTerms();
      if (term_idx < algebraic.size() && algebraic[term_idx]) {
        // Reconstruct an algebraic node from A v = lambda v. Its voltage
        // cannot be recovered from the C-orthogonal projection alone.
        term_projection = drive_resistance * source_projection;
        for (int32_t basis_idx = 0; basis_idx < order; ++basis_idx)
          term_projection += eigenvector_list[order_idx][basis_idx] * arnoldi_model.responseProjection()[basis_idx][term_idx];
        term_projection = eigenvalue_list[order_idx] > 0. ? term_projection / eigenvalue_list[order_idx] : 0.;
      }
      residue_list[term_idx][order_idx] = source_projection * term_projection;
    }
  }

  const auto& algebraic = arnoldi_model.zeroCapacitanceTerms();
  if (std::find(algebraic.begin(), algebraic.end(), true) != algebraic.end()) {
    // An infinite pole represents instantaneous feedthrough exactly. Adding
    // fictitious capacitance would alter the physical load and its time scale.
    pole_list.push_back(std::numeric_limits<double>::infinity());
    for (size_t term = 0; term < term_num; ++term) {
      double direct = 0.;
      if (term < algebraic.size() && algebraic[term])
        direct = 1. - std::accumulate(residue_list[term].begin(), residue_list[term].end(), 0.);
      residue_list[term].push_back(direct);
    }
    ++order;
  }
  pole_residue.set_is_valid(true);
  pole_residue.set_order(order);
  pole_residue.set_pole_list(pole_list);
  pole_residue.set_residue_list(residue_list);
  return pole_residue;
}

bool ArnoldiWaveformSolver::solveTridiagonal(const std::vector<double>& diagonal_list, const std::vector<double>& off_diagonal_list,
                                             std::vector<double>& eigenvalue_list, std::vector<std::vector<double>>& eigenvector_list)
{
  const std::size_t order = diagonal_list.size();
  if (order == 0 || order > 32 || off_diagonal_list.size() + 1 < order) {
    return false;
  }
  Eigen::MatrixXd matrix = Eigen::MatrixXd::Zero(order, order);
  for (std::size_t i = 0; i < order; ++i) {
    matrix(i, i) = diagonal_list[i];
    if (i + 1 < order)
      matrix(i, i + 1) = matrix(i + 1, i) = off_diagonal_list[i];
  }
  if (!matrix.allFinite())
    return false;
  Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(matrix);
  if (solver.info() != Eigen::Success || !solver.eigenvalues().allFinite())
    return false;
  const double tolerance = 64. * std::numeric_limits<double>::epsilon() * solver.eigenvalues().cwiseAbs().maxCoeff();
  eigenvalue_list.resize(order);
  eigenvector_list.assign(order, std::vector<double>(order));
  for (std::size_t i = 0; i < order; ++i) {
    const auto column = order - 1 - i;
    const double value = solver.eigenvalues()[column];
    if (value < -tolerance)
      return false;
    eigenvalue_list[i] = std::max(value, 0.);
    for (std::size_t j = 0; j < order; ++j)
      eigenvector_list[i][j] = solver.eigenvectors()(j, column);
  }
  return true;
}

void ArnoldiWaveformSolver::thresholds(const TimingArc& timing_arc, TransType trans_type, double& slew_derate, double& lower_threshold, double& upper_threshold,
                                       double& voltage_log, double& min_slew_factor, double& x1, double& y1)
{
  slew_derate = timing_arc.get_slew_derate();
  if (trans_type == TransType::kFall) {
    lower_threshold = timing_arc.get_slew_lower_threshold_pct_fall();
    upper_threshold = timing_arc.get_slew_upper_threshold_pct_fall();
  } else {
    lower_threshold = timing_arc.get_slew_lower_threshold_pct_rise();
    upper_threshold = timing_arc.get_slew_upper_threshold_pct_rise();
  }
  lower_threshold = normalizedThreshold(lower_threshold);
  upper_threshold = normalizedThreshold(upper_threshold);
  if (!(lower_threshold > 0.01 && upper_threshold < 0.99 && upper_threshold > lower_threshold && slew_derate > 0.0)) {
    lower_threshold = 0.1;
    upper_threshold = 0.9;
    slew_derate = 0.8;
  }
  voltage_log = std::log(upper_threshold / lower_threshold);
  thresholdFactor(lower_threshold, upper_threshold, min_slew_factor, x1, y1);
}

void ArnoldiWaveformSolver::thresholds(const LibertyLibrary& timing_library, TransType trans_type, double& slew_derate, double& lower_threshold,
                                       double& upper_threshold, double& voltage_log, double& min_slew_factor, double& x1, double& y1)
{
  slew_derate = timing_library.get_slew_derate_from_library();
  if (trans_type == TransType::kFall) {
    lower_threshold = timing_library.get_slew_lower_threshold_pct_fall();
    upper_threshold = timing_library.get_slew_upper_threshold_pct_fall();
  } else {
    lower_threshold = timing_library.get_slew_lower_threshold_pct_rise();
    upper_threshold = timing_library.get_slew_upper_threshold_pct_rise();
  }
  lower_threshold = normalizedThreshold(lower_threshold);
  upper_threshold = normalizedThreshold(upper_threshold);
  if (!(lower_threshold > 0.01 && upper_threshold < 0.99 && upper_threshold > lower_threshold && slew_derate > 0.0)) {
    lower_threshold = 0.1;
    upper_threshold = 0.9;
    slew_derate = 0.8;
  }
  voltage_log = std::log(upper_threshold / lower_threshold);
  thresholdFactor(lower_threshold, upper_threshold, min_slew_factor, x1, y1);
}

void ArnoldiWaveformSolver::thresholdFactor(double lower_threshold, double upper_threshold, double& min_slew_factor, double& x1, double& y1)
{
  double upper_log = std::log(1.0 / upper_threshold);
  min_slew_factor = upper_log + hInverse((1.0 - upper_threshold) / upper_threshold - upper_log);
  double lower_log = std::log(1.0 / lower_threshold);
  double lower_factor = lower_log + hInverse((1.0 - lower_threshold) / lower_threshold - lower_log);
  double lower_ratio = (std::exp(lower_factor) - 1.0) / lower_factor;
  double denominator = std::log(lower_ratio / lower_threshold) - hInverse((1.0 - upper_threshold) * lower_factor);
  x1 = (upper_threshold - lower_threshold) / denominator;
  y1 = lower_factor * x1;
}

double ArnoldiWaveformSolver::hInverse(double value)
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

void ArnoldiWaveformSolver::solveRamp(double pole, double transition_time, double lower_threshold, double upper_threshold, double& ramp)
{
  for (int32_t iter = 0; iter < 5; iter++) {
    double lower_pole_time = 0.0;
    double lower_derivative = 0.0;
    double upper_pole_time = 0.0;
    double upper_derivative = 0.0;
    rampPoint(pole * ramp, lower_threshold, lower_pole_time, lower_derivative);
    rampPoint(pole * ramp, upper_threshold, upper_pole_time, upper_derivative);
    double function_value = (lower_pole_time - upper_pole_time) / pole - transition_time;
    double derivative = lower_derivative - upper_derivative;
    if (std::abs(derivative) < ista::kTimingTolerance) {
      return;
    }
    ramp -= function_value / derivative;
    if (std::abs(function_value) < 1E-6) {
      return;
    }
  }
}

double ArnoldiWaveformSolver::rampTime(double pole, double ramp, double voltage)
{
  double pole_ramp = pole * ramp;
  if (pole_ramp > 30.0) {
    return (1.0 + pole_ramp * (1.0 - voltage)) / pole;
  }
  double exp_pole_ramp = std::exp(pole_ramp);
  if ((1.0 - pole_ramp * voltage) * exp_pole_ramp >= 1.0) {
    return std::log((exp_pole_ramp - 1.0) / (pole_ramp * voltage)) / pole;
  }
  return hInverse((1.0 - voltage) * pole_ramp) / pole;
}

void ArnoldiWaveformSolver::rampPoint(double pole_ramp, double voltage, double& pole_time, double& derivative)
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
  pole_time = hInverse((1.0 - voltage) * pole_ramp);
  derivative = (1.0 - voltage) / (pole_time - (1.0 - voltage) * pole_ramp);
}

std::optional<double> ArnoldiWaveformSolver::crossingCapacitance(double driver_ramp, double drive_resistance, double total_capacitance, double crossing_time,
                                                                 double voltage)
{
  if (!(driver_ramp > 0. && drive_resistance > 0. && total_capacitance > 0. && crossing_time > 0. && voltage > 0. && voltage < 1.)
      || !std::isfinite(driver_ramp) || !std::isfinite(drive_resistance) || !std::isfinite(total_capacitance) || !std::isfinite(crossing_time))
    return std::nullopt;
  const double ideal_time = driver_ramp * (1. - voltage);
  const double tolerance = 1e-10 * std::max(crossing_time, 1e-6);
  if (crossing_time < ideal_time - tolerance)
    return std::nullopt;
  {
    static const char* ceff_dump = std::getenv("ISTA_DUMP_CEFF_PATH");
    if (ceff_dump != nullptr) {
      const double lump_time = rampTime(1. / (drive_resistance * total_capacitance), driver_ramp, voltage);
      if (FILE* ceff_fp = std::fopen(ceff_dump, "a"); ceff_fp != nullptr) {
        std::fprintf(ceff_fp, "%.12g,%.12g,%.12g,%.12g,%d\n", crossing_time, lump_time, total_capacitance, driver_ramp,
                     crossing_time >= lump_time ? 1 : 0);
        std::fclose(ceff_fp);
      }
    }
  }
  if (crossing_time >= rampTime(1. / (drive_resistance * total_capacitance), driver_ramp, voltage))
    return total_capacitance;
  double lower = 0., upper = total_capacitance;
  double capacitance = std::clamp((crossing_time - ideal_time) / drive_resistance, total_capacitance * 1e-12, total_capacitance);
  for (int iteration = 0; iteration < 40; ++iteration) {
    const double tau = drive_resistance * capacitance;
    const double ramp_ratio = driver_ramp / tau;
    double normalized_time = 0., ramp_derivative = 0.;
    rampPoint(ramp_ratio, voltage, normalized_time, ramp_derivative);
    const double residual = tau * normalized_time - crossing_time;
    if (std::abs(residual) <= tolerance)
      return capacitance;
    if (residual < 0.)
      lower = capacitance;
    else
      upper = capacitance;
    // t = R*C*f(ramp/(R*C)); differentiate with respect to capacitance.
    const double derivative = drive_resistance * (normalized_time - ramp_ratio * ramp_derivative);
    const double candidate = derivative > 0. ? capacitance - residual / derivative : 0.;
    capacitance = std::isfinite(candidate) && candidate > lower && candidate < upper ? candidate : lower + .5 * (upper - lower);
  }
  return std::nullopt;
}

double ArnoldiWaveformSolver::effectiveCapacitance(double driver_ramp, double drive_resistance, const ParasiticArnoldiPoleResidue& pole_residue,
                                                   double effective_capacitance_time)
{
  if (!(drive_resistance > 0.0 && driver_ramp > 0.0 && effective_capacitance_time >= 0.0) || pole_residue.get_residue_list().empty()) {
    return 0.0;
  }
  const std::vector<double>& pole_list = pole_residue.get_pole_list();
  const std::vector<double>& residue_list = pole_residue.get_residue_list().front();
  double integrated_current = 0.0;
  double voltage_change = 0.0;
  for (std::size_t pole_idx = 0; pole_idx < pole_list.size(); pole_idx++) {
    double pole = pole_list[pole_idx];
    if (std::isinf(pole)) {
      // Feedthrough changes voltage without drawing stored charge.
      voltage_change += residue_list[pole_idx] * std::min(effective_capacitance_time / driver_ramp, 1.);
      continue;
    }
    double pole_ramp = pole * driver_ramp;
    double pole_time = pole * effective_capacitance_time;
    double integral = 0.0;
    if (effective_capacitance_time <= driver_ramp) {
      integral = integratedDecay(pole_time);
    } else {
      integral = integratedDecay(pole_ramp) + std::expm1(-pole_ramp) * std::expm1(-(pole_time - pole_ramp));
    }
    const double change = integral / pole_ramp;
    voltage_change += residue_list[pole_idx] * change;
    integrated_current += residue_list[pole_idx] * change / pole;
  }
  integrated_current /= drive_resistance;

  if (!(voltage_change > 0.0)) {
    return 0.0;
  }
  return integrated_current / voltage_change;
}

void ArnoldiWaveformSolver::crossingTime(double driver_ramp, const ParasiticArnoldiPoleResidue& pole_residue, std::size_t term_idx, double voltage,
                                         double& waveform_time)
{
  waveform_time = std::numeric_limits<double>::quiet_NaN();
  if (!(voltage > 0. && voltage < 1. && driver_ramp > 0.) || term_idx >= pole_residue.get_residue_list().size())
    return;
  const auto& poles = pole_residue.get_pole_list();
  const auto& residues = pole_residue.get_residue_list()[term_idx];
  double longest = 0.;
  for (double pole : poles) {
    if (!(pole > 0.))
      return;
    if (std::isfinite(pole))
      longest = std::max(longest, 1. / pole);
  }
  double low = 0., high = driver_ramp + 64. * longest;
  if (!std::isfinite(high) || !(voltageAt(low, driver_ramp, poles, residues) >= voltage) || !(voltageAt(high, driver_ramp, poles, residues) <= voltage))
    return;

  {
    const auto ref_voltage = [&](double time) { return 1. - voltageAt(time, driver_ramp, poles, residues); };
    const auto ref_slope = [&](double time) {
      double value = 0., derivative = 0.;
      voltageDerivative(time, driver_ramp, poles, residues, value, derivative);
      return -derivative;
    };

    const double target = 1. - voltage;
    double sum_signed = 0., sum_abs = 0.;
    for (double residue : residues) {
      sum_signed += residue;
      sum_abs += std::fabs(residue);
    }
    double slowest = std::numeric_limits<double>::infinity();
    for (double pole : poles) {
      if (std::isfinite(pole))
        slowest = std::min(slowest, pole);
    }
    const double denominator = sum_signed - target;
    const double ratio = sum_abs / denominator;
    bool solved = false;
    int iterations_used = -1;
    double initial_residual = -1.;
    double initial_guess = 0.;
    if (slowest > 0. && std::isfinite(slowest) && denominator > 0. && sum_abs > 0. && ratio > 0. && std::isfinite(ratio)) {
      const double probe = ref_voltage(driver_ramp);
      {
        // Diagnostic for the "whole net clamps to 1 fs" family: reference truncates
        // at x = driver_ramp when v(T) >= target, which makes every sink share the
        // driver time and the net delay collapse to zero. Log probe vs target to see
        // whether iSTA sits on the other side of that comparison.
        static const char* pd = std::getenv("ISTA_DUMP_PROBE");
        static FILE* pf = pd != nullptr ? std::fopen(pd, "a") : static_cast<FILE*>(nullptr);
        if (pf != nullptr) {
          std::fprintf(pf, "PR,%.15g,%.15g,%.15g,%zu,%.15g\n", probe, target, driver_ramp, term_idx,
                       [&] { double s = 0.; for (double r : residues) s += r; return s; }());
          std::fflush(pf);
        }
      }
      double guess = 0.;
      if (probe >= target) {
        guess = driver_ramp;
      } else if (!(probe > 0.)) {
        guess = std::log(ratio) / slowest;
      } else {
        const double exponent = driver_ramp * slowest;
        if (exponent <= 100.) {
          guess = std::log(ratio * (std::exp(exponent) - 1.) / exponent) / slowest;
        } else {
          guess = (std::log(ratio) + exponent - std::log(exponent)) / slowest;
        }
      }
      // The Newton exit threshold is reference's own 1e-3 NORMALIZED VOLTAGE residual.
      // Leaving it loose is only correct if reference really does stop there; measured
      // against reference it does NOT: on picorv32a net1103 the tightly converged
      // crossing reproduces reference's whole net-delay range to 0.017 ps
      // (maxRise 0.5113..63.7409 versus 0.510..63.758, maxFall 0.4370..65.6683 versus
      // 0.436..65.6220), while stopping at 1e-3 lands 0.19 ps away on the same net.
      // The voltage residual converts to time through the slew, so 1e-3 is worth about
      // 0.2 ps here. ISTA_CROSSING_TOL selects the exit residual; any value <= 0 keeps
      // the historical 1e-3.
      static const double crossing_tol = [] {
        const char* e = std::getenv("ISTA_CROSSING_TOL");
        return e != nullptr ? std::atof(e) : 1e-3;
      }();
      // A non-positive value drives the Newton to full convergence instead of
      // bailing out: the bracket safeguard then lands on the same root the tight
      // bisection below would find.
      const double newton_tol = crossing_tol > 0. ? crossing_tol : 1e-300;
      double value = ref_voltage(guess);
      initial_residual = std::fabs(target - value);
      initial_guess = guess;
      if (std::isfinite(guess) && std::isfinite(value)) {
        if (std::fabs(target - value) < newton_tol) {
          waveform_time = guess;
          solved = true;
          iterations_used = 0;
        } else {
          double bracket_low = 0., bracket_high = guess;
          double point = guess, point_value = value;
          for (int iteration = 0; iteration < 99; ++iteration) {
            const double slope = ref_slope(point);
            if (!std::isfinite(slope) || slope == 0.)
              break;
            const double step = (target - point_value) / slope;
            if (!std::isfinite(step))
              break;
            if (step == 0.) {
              waveform_time = 0.;
              solved = true;
              break;
            }
            double candidate = point + step;
            if (candidate > bracket_high || candidate < bracket_low)
              candidate = bracket_low + .5 * (bracket_high - bracket_low);
            const double candidate_value = ref_voltage(candidate);
            if (!std::isfinite(candidate_value))
              break;
            if (target <= candidate_value)
              bracket_high = candidate;
            else
              bracket_low = candidate;
            if (std::fabs(target - candidate_value) < newton_tol) {
              waveform_time = candidate;
              solved = true;
              iterations_used = iteration + 1;
              break;
            }
            point = candidate;
            point_value = candidate_value;
          }
        }
      }
    }
    {
      // Diagnostic: separates "reference solver answered" from "fell back to the tight
      // bisection". The two produce different residuals, so a large fallback rate means
      // the reference-compatible path is not actually in control.
      static const char* cf = std::getenv("ISTA_DUMP_CROSSING_STATS");
      static FILE* fp = cf != nullptr ? std::fopen(cf, "a") : static_cast<FILE*>(nullptr);
      if (fp != nullptr) {
        std::fprintf(fp, "C,%d,%d,%d,%d,%.6g,%.12g,%.12g,%.12g\n", solved ? 1 : 0, ratio > 0. ? 1 : 0,
                     slowest > 0. ? 1 : 0, iterations_used, ratio, initial_residual, initial_guess, target);
        std::fprintf(fp, "D,%.12g,%.12g,%zu,%d,%.12g,%.12g\n", driver_ramp, slowest, residues.size(), solved ? 1 : 0,
                     sum_signed, residues.empty() ? 0. : residues[0]);
        std::fflush(fp);
      }
    }
    if (solved)
      return;
  }

  // Keep a genuine voltage bracket for arbitrary receiver thresholds. The old
  // midpoint-oriented starting guesses could invert it for 10/90% crossings.
  for (int iteration = 0; iteration < 80; ++iteration) {
    const double mid = low + .5 * (high - low);
    const double value = voltageAt(mid, driver_ramp, poles, residues);
    if (!std::isfinite(value))
      return;
    if (value > voltage)
      low = mid;
    else
      high = mid;
  }
  waveform_time = low + .5 * (high - low);
}

void ArnoldiWaveformSolver::crossingTime(double driver_ramp, const ParasiticArnoldiPoleResidue& pole_residue, std::size_t term_idx, double upper_threshold,
                                         double& upper_time, double mid_threshold, double& mid_time, double lower_threshold, double& lower_time)
{
  const std::vector<double>& source_pole_list = pole_residue.get_pole_list();
  const std::vector<double>& source_residue_list = pole_residue.get_residue_list()[term_idx];
  int32_t order = pole_residue.get_order();
  static const double trunc_eps = [] {
    const char* env = std::getenv("ISTA_TRUNC_EPS");
    return env != nullptr ? std::atof(env) : 1E-8;
  }();
  while (order > 1 && source_residue_list[order - 1] < trunc_eps && source_residue_list[order - 1] > -trunc_eps) {
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
    double candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    if (candidate_voltage < mid_threshold) {
      hi_max_time = candidate_time;
      mid_max_time = candidate_time;
      lo_min_time = candidate_time;
      hi_max_voltage = candidate_voltage;
      mid_max_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      candidate_time = mid_threshold * driver_ramp;
      candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
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
        candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
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
        candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
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
    double candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    while (candidate_voltage > lower_threshold) {
      lo_min_time = candidate_time;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    }
    lo_max_time = candidate_time;
    lo_max_voltage = candidate_voltage;
    candidate_time = mid_threshold * driver_ramp;
    candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    mid_min_time = candidate_time;
    mid_min_voltage = candidate_voltage;
    if (candidate_voltage > upper_threshold) {
      hi_min_time = candidate_time;
      hi_min_voltage = candidate_voltage;
    } else {
      hi_max_time = candidate_time;
      hi_max_voltage = candidate_voltage;
      candidate_time = lower_threshold * driver_ramp;
      candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
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
    double candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    hi_min_time = candidate_time;
    hi_min_voltage = candidate_voltage;
    candidate_time = driver_ramp + 0.7 / search_pole;
    candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    while (candidate_voltage > mid_threshold) {
      mid_min_time = candidate_time;
      lo_min_time = candidate_time;
      mid_min_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      candidate_time += 0.7 / search_pole;
      candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
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
      candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
      while (candidate_voltage > lower_threshold) {
        lo_min_time = candidate_time;
        lo_min_voltage = candidate_voltage;
        candidate_time += 1.0 / search_pole;
        candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
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
      candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    }
    hi_max_time = candidate_time;
    hi_max_voltage = candidate_voltage;
    while (candidate_voltage > mid_threshold) {
      mid_min_time = candidate_time;
      lo_min_time = candidate_time;
      mid_min_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    }
    mid_max_time = candidate_time;
    mid_max_voltage = candidate_voltage;
    while (candidate_voltage > lower_threshold) {
      lo_min_time = candidate_time;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    }
    lo_max_time = candidate_time;
    lo_max_voltage = candidate_voltage;
  }

  upper_time = bracketedTime(driver_ramp, pole_list, residue_list, upper_threshold, hi_min_time, hi_max_time, hi_min_voltage, hi_max_voltage);
  mid_time = bracketedTime(driver_ramp, pole_list, residue_list, mid_threshold, mid_min_time, mid_max_time, mid_min_voltage, mid_max_voltage);
  lower_time = bracketedTime(driver_ramp, pole_list, residue_list, lower_threshold, lo_min_time, lo_max_time, lo_min_voltage, lo_max_voltage);

  {
    static const bool ref_crossing = [] {
      const char* env = std::getenv("ISTA_REF_CROSSING");
      return env == nullptr ? true : std::atoi(env) != 0;
    }();
    if (ref_crossing) {
      double reference_mid = std::numeric_limits<double>::quiet_NaN();
      crossingTime(driver_ramp, pole_residue, term_idx, mid_threshold, reference_mid);
      if (std::isfinite(reference_mid))
        mid_time = reference_mid;
    }
  }
}

double ArnoldiWaveformSolver::voltageAt(double time, double driver_ramp, const std::vector<double>& pole_list, const std::vector<double>& residue_list)
{
  double voltage = 0.0;
  for (std::size_t pole_idx = 0; pole_idx < pole_list.size(); pole_idx++) {
    if (std::isinf(pole_list[pole_idx])) {
      voltage += residue_list[pole_idx] * std::max(0., 1. - time / driver_ramp);
      continue;
    }
    double pole_time = pole_list[pole_idx] * time;
    double pole_ramp = pole_list[pole_idx] * driver_ramp;
    double response = 0.0;
    if (time < driver_ramp) {
      response = 1.0 - time / driver_ramp - std::expm1(-pole_time) / pole_ramp;
    } else {
      response = -std::exp(pole_ramp - pole_time) * std::expm1(-pole_ramp) / pole_ramp;
    }
    voltage += residue_list[pole_idx] * response;
  }
  return voltage;
}

void ArnoldiWaveformSolver::voltageDerivative(double time, double driver_ramp, const std::vector<double>& pole_list, const std::vector<double>& residue_list,
                                              double& voltage, double& derivative)
{
  voltage = 0.0;
  derivative = 0.0;
  for (std::size_t pole_idx = 0; pole_idx < pole_list.size(); pole_idx++) {
    double pole = pole_list[pole_idx];
    if (std::isinf(pole)) {
      voltage += residue_list[pole_idx] * std::max(0., 1. - time / driver_ramp);
      if (time < driver_ramp)
        derivative -= residue_list[pole_idx] / driver_ramp;
      continue;
    }
    double pole_time = pole * time;
    double pole_ramp = pole * driver_ramp;
    double response = 0.0;
    double response_derivative = 0.0;
    if (time < driver_ramp) {
      double ramp_response = -std::expm1(-pole_time) / pole_ramp;
      response = 1.0 - time / driver_ramp + ramp_response;
      response_derivative = -pole * ramp_response;
    } else {
      response = -std::exp(pole_ramp - pole_time) * std::expm1(-pole_ramp) / pole_ramp;
      response_derivative = -pole * response;
    }
    voltage += residue_list[pole_idx] * response;
    derivative += residue_list[pole_idx] * response_derivative;
  }
}

double ArnoldiWaveformSolver::bracketedTime(double driver_ramp, const std::vector<double>& pole_list, const std::vector<double>& residue_list, double voltage,
                                            double lower_time, double upper_time, double lower_voltage, double upper_voltage)
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
  voltageDerivative(result_time, driver_ramp, pole_list, residue_list, function_value, derivative);
  function_value -= voltage;
  double last_function_value = 0.0;
  for (int32_t iter = 1; iter < 10; iter++) {
    if ((((result_time - high_time) * derivative - function_value) * ((result_time - low_time) * derivative - function_value) >= 0.0)
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
    voltageDerivative(result_time, driver_ramp, pole_list, residue_list, function_value, derivative);
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

double ArnoldiWaveformSolver::inputPortCrossing(const std::vector<double>& time_constants, const std::vector<double>& weights, double ramp, double voltage)
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

}  // namespace ista
