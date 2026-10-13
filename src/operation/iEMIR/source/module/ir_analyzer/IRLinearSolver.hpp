// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <Eigen/SparseCore>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace iemir {

struct IRSolverOptions
{
  std::string method = "auto";  // auto, hierarchical, iccg, sparse_lu
  double relative_tolerance = 1.e-10;
  double absolute_tolerance = 1.e-14;
  int max_iterations = 2000;
  int leaf_size = 256;
  bool operator==(const IRSolverOptions&) const = default;
};

struct IRSolverStats
{
  std::string method;
  bool reused_matrix = false;
  bool reused_symbolic = false;
  bool fallback = false;
  int iterations = 0;
  int partition_count = 0;
  int partition_depth = 0;
  std::uint64_t symbolic_preparations = 0;
  std::uint64_t numeric_preparations = 0;
  std::uint64_t solves = 0;
  double prepare_seconds = 0;
  double solve_seconds = 0;
  double relative_residual = 0;
  double max_abs_residual = 0;
};

// Owns the effective SPD operator and its preparation. Call prepare when the
// network/operator changes; call solve for each current vector or time step.
// Same-pattern numeric changes reuse symbolic work, identical operators reuse
// all factors. No capacitive/inductive model or time integration is implied.
// Instances are independent, but concurrent calls on one instance are not safe.
class IRLinearSolver
{
 public:
  using Matrix = Eigen::SparseMatrix<double>;
  using Points = std::vector<std::array<double, 2>>;
  IRLinearSolver();
  ~IRLinearSolver();
  IRLinearSolver(const IRLinearSolver&) = delete;
  IRLinearSolver& operator=(const IRLinearSolver&) = delete;

  void prepare(const Matrix& matrix, const Points& points = {}, const IRSolverOptions& options = {});
  Eigen::VectorXd solve(const Eigen::VectorXd& rhs, const Eigen::VectorXd* initial_guess = nullptr);
  const IRSolverStats& stats() const;
  static void validateOptions(const IRSolverOptions& options);

 private:
  struct Impl;
  std::unique_ptr<Impl> _impl;
};
}  // namespace iemir
