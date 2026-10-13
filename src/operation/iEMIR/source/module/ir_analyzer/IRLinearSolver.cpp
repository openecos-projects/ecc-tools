// SPDX-License-Identifier: MulanPSL-2.0
#include "IRLinearSolver.hpp"

#include <Eigen/IterativeLinearSolvers>
#include <Eigen/OrderingMethods>
#include <Eigen/SparseCholesky>
#include <Eigen/SparseLU>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <numeric>
#include <stdexcept>

namespace iemir {
namespace {
using Matrix = IRLinearSolver::Matrix;
using Points = IRLinearSolver::Points;
using Permutation = Eigen::PermutationMatrix<Eigen::Dynamic, Eigen::Dynamic, int>;
using Clock = std::chrono::steady_clock;

double elapsed(Clock::time_point start)
{
  return std::chrono::duration<double>(Clock::now() - start).count();
}

bool samePattern(const Matrix& a, const Matrix& b)
{
  return a.rows() == b.rows() && a.cols() == b.cols() && a.nonZeros() == b.nonZeros()
         && std::equal(a.outerIndexPtr(), a.outerIndexPtr() + a.outerSize() + 1, b.outerIndexPtr())
         && std::equal(a.innerIndexPtr(), a.innerIndexPtr() + a.nonZeros(), b.innerIndexPtr());
}

// A symmetric diagonally dominant conductance matrix is SPD when every
// connected component has a strictly dominant (grounded) row. This sufficient
// test is linear in the nonzeros. Uncertain rounding and general SPD operators
// require a positive-pivot LDLT check before CG may use them.
bool certifiedConductance(const Matrix& matrix)
{
  std::vector<bool> anchored(matrix.rows(), false), visited(matrix.rows(), false);
  for (int col = 0; col < matrix.outerSize(); ++col) {
    long double off_diagonal = 0;
    for (Matrix::InnerIterator it(matrix, col); it; ++it) {
      if (it.row() == col)
        continue;
      if (it.value() > 0)
        return false;
      off_diagonal -= static_cast<long double>(it.value());
    }
    long double margin = static_cast<long double>(matrix.coeff(col, col)) - off_diagonal;
    if (margin < 0)
      return false;
    anchored[col] = margin > 0;
  }
  std::vector<int> pending;
  for (int root = 0; root < matrix.rows(); ++root) {
    if (visited[root])
      continue;
    bool grounded = false;
    pending.push_back(root);
    visited[root] = true;
    while (!pending.empty()) {
      int node = pending.back();
      pending.pop_back();
      grounded = grounded || anchored[node];
      for (Matrix::InnerIterator it(matrix, node); it; ++it) {
        if (it.value() != 0 && !visited[it.row()]) {
          visited[it.row()] = true;
          pending.push_back(it.row());
        }
      }
    }
    if (!grounded)
      return false;
  }
  return true;
}

// Interior vertices of each child precede its separator. Sparse LDLT then
// carries out the recursive Schur elimination without explicit dense inverses.
class SeparatorOrdering
{
 public:
  SeparatorOrdering(const Matrix& matrix, const Points& points, int leaf_size, IRSolverStats& stats)
      : _matrix(matrix), _points(points), _leaf_size(leaf_size), _stats(stats), _side(matrix.rows(), -1), _local(matrix.rows(), -1)
  {
  }

  Permutation build()
  {
    std::vector<int> nodes(_matrix.rows());
    std::iota(nodes.begin(), nodes.end(), 0);
    _stats.partition_count = _stats.partition_depth = 0;
    divide(std::move(nodes), 0);
    Permutation p(_matrix.rows());
    for (int i = 0; i < int(_order.size()); ++i)
      p.indices()[_order[i]] = i;
    return p;
  }

 private:
  void leaf(const std::vector<int>& nodes)
  {
    if (nodes.empty())
      return;
    for (int i = 0; i < int(nodes.size()); ++i)
      _local[nodes[i]] = i;
    std::vector<Eigen::Triplet<double>> triplets;
    for (int col : nodes) {
      for (Matrix::InnerIterator it(_matrix, col); it; ++it) {
        if (_local[it.row()] >= 0)
          triplets.emplace_back(_local[it.row()], _local[col], it.value());
      }
    }
    Matrix sub(nodes.size(), nodes.size());
    sub.setFromTriplets(triplets.begin(), triplets.end());
    Permutation inverse;
    Eigen::AMDOrdering<int>{}(sub, inverse);
    for (int i = 0; i < int(nodes.size()); ++i)
      _order.push_back(nodes[inverse.indices()[i]]);
    for (int node : nodes)
      _local[node] = -1;
  }

  void divide(std::vector<int> nodes, int depth)
  {
    _stats.partition_depth = std::max(_stats.partition_depth, depth);
    if (int(nodes.size()) <= _leaf_size || _points.empty()) {
      leaf(nodes);
      return;
    }
    auto low = _points[nodes.front()], high = low;
    for (int node : nodes) {
      for (int axis = 0; axis < 2; ++axis) {
        low[axis] = std::min(low[axis], _points[node][axis]);
        high[axis] = std::max(high[axis], _points[node][axis]);
      }
    }
    int axis = high[0] - low[0] >= high[1] - low[1] ? 0 : 1;
    if (high[axis] == low[axis]) {
      leaf(nodes);
      return;
    }
    std::sort(nodes.begin(), nodes.end(), [&](int a, int b) { return _points[a][axis] != _points[b][axis] ? _points[a][axis] < _points[b][axis] : a < b; });
    const int half = nodes.size() / 2;
    for (int i = 0; i < int(nodes.size()); ++i)
      _side[nodes[i]] = i < half ? 0 : 1;
    std::vector<int> left, right(nodes.begin() + half, nodes.end()), separator;
    for (int i = 0; i < half; ++i) {
      int node = nodes[i];
      bool boundary = false;
      for (Matrix::InnerIterator it(_matrix, node); it; ++it) {
        if (it.value() != 0 && _side[it.row()] == 1) {
          boundary = true;
          break;
        }
      }
      (boundary ? separator : left).push_back(node);
    }
    for (int node : nodes)
      _side[node] = -1;
    // Bad geometry or many long links: use sparse AMD instead of an oversized
    // separator. Both child sets still contain only original sparse entries.
    if (left.empty() || separator.size() > nodes.size() / 3) {
      leaf(nodes);
      return;
    }
    ++_stats.partition_count;
    divide(std::move(left), depth + 1);
    divide(std::move(right), depth + 1);
    leaf(separator);
  }

  const Matrix& _matrix;
  const Points& _points;
  int _leaf_size;
  IRSolverStats& _stats;
  std::vector<int> _side, _local, _order;
};
}  // namespace

struct IRLinearSolver::Impl
{
  Matrix matrix, permuted;
  Points points;
  IRSolverOptions options;
  IRSolverStats stats;
  Permutation permutation;
  Eigen::SimplicialLDLT<Matrix, Eigen::Lower, Eigen::NaturalOrdering<int>> direct;
  Eigen::ConjugateGradient<Matrix, Eigen::Lower | Eigen::Upper, Eigen::IncompleteCholesky<double>> cg;
  Eigen::SparseLU<Matrix> lu;
  Eigen::VectorXd last;
  bool ready = false, direct_symbolic = false, cg_symbolic = false, lu_symbolic = false;
  bool direct_ready = false, force_direct = false;
  std::string selected;

  void prepareDirect()
  {
    if (direct_ready)
      return;
    if (!direct_symbolic) {
      permutation = SeparatorOrdering(matrix, points, options.leaf_size, stats).build();
    }
    permuted = permutation * matrix * permutation.transpose();
    permuted.makeCompressed();
    if (!direct_symbolic) {
      direct.analyzePattern(permuted);
      direct_symbolic = true;
      ++stats.symbolic_preparations;
    }
    direct.factorize(permuted);
    ++stats.numeric_preparations;
    if (direct.info() != Eigen::Success || !direct.vectorD().allFinite() || (direct.vectorD().array() <= 0).any()) {
      throw std::runtime_error("IR operator is singular, indefinite, or failed sparse LDLT factorization");
    }
    direct_ready = true;
  }

  Eigen::VectorXd directSolve(const Eigen::VectorXd& b)
  {
    Eigen::VectorXd permuted_rhs = permutation * b;
    Eigen::VectorXd result = direct.solve(permuted_rhs);
    if (direct.info() != Eigen::Success)
      throw std::runtime_error("IR sparse LDLT solve failed");
    return permutation.transpose() * result;
  }

  bool acceptable(const Eigen::VectorXd& x, const Eigen::VectorXd& b)
  {
    if (!x.allFinite())
      return false;
    Eigen::VectorXd residual = matrix * x - b;
    double norm = residual.stableNorm();
    stats.relative_residual = norm / std::max(b.stableNorm(), 1.e-300);
    stats.max_abs_residual = residual.size() ? residual.cwiseAbs().maxCoeff() : 0;
    return residual.allFinite() && std::isfinite(norm) && norm <= options.absolute_tolerance + options.relative_tolerance * b.stableNorm();
  }
};

IRLinearSolver::IRLinearSolver() : _impl(std::make_unique<Impl>())
{
}
IRLinearSolver::~IRLinearSolver() = default;
const IRSolverStats& IRLinearSolver::stats() const
{
  return _impl->stats;
}

void IRLinearSolver::validateOptions(const IRSolverOptions& options)
{
  if (options.method != "auto" && options.method != "hierarchical" && options.method != "iccg" && options.method != "sparse_lu")
    throw std::invalid_argument("IR solver must be auto, hierarchical, iccg, or sparse_lu");
  if (!std::isfinite(options.relative_tolerance) || options.relative_tolerance <= 0 || options.relative_tolerance >= 1
      || !std::isfinite(options.absolute_tolerance) || options.absolute_tolerance < 0 || options.max_iterations < 1 || options.leaf_size < 2)
    throw std::invalid_argument("Invalid IR solver tolerance, iteration limit, or partition size");
}

void IRLinearSolver::prepare(const Matrix& input, const Points& points, const IRSolverOptions& options)
{
  auto start = Clock::now();
  auto& s = *_impl;
  bool was_ready = s.ready;
  s.ready = false;  // a failed preparation must not leave a stale usable operator
  validateOptions(options);
  if (input.rows() != input.cols())
    throw std::invalid_argument("IR operator must be square");
  if (!points.empty() && points.size() != std::size_t(input.rows()))
    throw std::invalid_argument("IR coordinate count mismatch");
  for (auto point : points) {
    if (!std::isfinite(point[0]) || !std::isfinite(point[1]))
      throw std::invalid_argument("Nonfinite IR coordinates");
  }
  Matrix matrix = input;
  matrix.makeCompressed();
  for (int col = 0; col < matrix.outerSize(); ++col) {
    if (!std::isfinite(matrix.coeff(col, col)) || matrix.coeff(col, col) <= 0)
      throw std::invalid_argument("IR operator requires a positive finite diagonal");
    for (Matrix::InnerIterator it(matrix, col); it; ++it) {
      if (!std::isfinite(it.value()))
        throw std::invalid_argument("Nonfinite IR conductance");
      double other = matrix.coeff(col, it.row());
      if (std::abs(it.value() - other) > 1.e-13 * std::max(std::abs(it.value()), std::abs(other)))
        throw std::invalid_argument("IR operator must be fully stored and symmetric");
    }
  }
  bool same_structure = was_ready && options == s.options && points == s.points && samePattern(matrix, s.matrix);
  bool identical = same_structure && std::equal(matrix.valuePtr(), matrix.valuePtr() + matrix.nonZeros(), s.matrix.valuePtr());
  s.stats.reused_matrix = identical;
  s.stats.reused_symbolic = same_structure;
  if (identical) {
    s.ready = true;
    s.stats.prepare_seconds = elapsed(start);
    return;
  }
  if (!same_structure) {
    s.direct_symbolic = s.cg_symbolic = s.lu_symbolic = false;
    s.last.resize(0);
    s.stats.partition_count = s.stats.partition_depth = 0;
  }
  s.matrix = std::move(matrix);
  s.points = points;
  s.options = options;
  s.direct_ready = s.force_direct = false;
  s.stats.fallback = false;
  // Measured local meshes favor reusable LDLT, including large meshes. Keep
  // ICCG explicit until a graph-aware selection policy has benchmark evidence.
  s.selected = options.method == "auto" ? "hierarchical" : options.method;
  s.stats.method = s.selected;
  if (s.matrix.rows()) {
    if (s.selected == "iccg") {
      if (!certifiedConductance(s.matrix))
        s.prepareDirect();
      s.cg.setTolerance(options.relative_tolerance);
      s.cg.setMaxIterations(options.max_iterations);
      if (!s.cg_symbolic) {
        s.cg.analyzePattern(s.matrix);
        s.cg_symbolic = true;
        ++s.stats.symbolic_preparations;
      }
      s.cg.factorize(s.matrix);
      ++s.stats.numeric_preparations;
      if (s.cg.info() != Eigen::Success) {
        s.force_direct = s.stats.fallback = true;
        s.prepareDirect();
      }
    } else if (s.selected == "sparse_lu") {
      if (!s.lu_symbolic) {
        s.lu.analyzePattern(s.matrix);
        s.lu_symbolic = true;
        ++s.stats.symbolic_preparations;
      }
      s.lu.factorize(s.matrix);
      ++s.stats.numeric_preparations;
      if (s.lu.info() != Eigen::Success)
        throw std::runtime_error("IR SparseLU factorization failed");
    } else {
      s.prepareDirect();
    }
  }
  s.ready = true;
  s.stats.prepare_seconds = elapsed(start);
}

Eigen::VectorXd IRLinearSolver::solve(const Eigen::VectorXd& rhs, const Eigen::VectorXd* initial_guess)
{
  auto start = Clock::now();
  auto& s = *_impl;
  if (!s.ready)
    throw std::logic_error("IR solver is not prepared");
  if (rhs.size() != s.matrix.rows() || !rhs.allFinite())
    throw std::invalid_argument("Invalid IR current vector");
  if (initial_guess && (initial_guess->size() != rhs.size() || !initial_guess->allFinite()))
    throw std::invalid_argument("Invalid IR voltage initial guess");
  s.stats.iterations = 0;
  s.stats.method = s.force_direct ? "hierarchical" : s.selected;
  Eigen::VectorXd x;
  if (rhs.size() == 0 || rhs.isZero(0)) {
    x = Eigen::VectorXd::Zero(rhs.size());
  } else if (s.selected == "iccg" && !s.force_direct) {
    Eigen::VectorXd guess = initial_guess ? *initial_guess : s.last.size() == rhs.size() ? s.last : Eigen::VectorXd::Zero(rhs.size());
    x = s.cg.solveWithGuess(rhs, guess);
    s.stats.iterations = s.cg.iterations();
    if (s.cg.info() != Eigen::Success || !s.acceptable(x, rhs)) {
      s.force_direct = s.stats.fallback = true;
      s.prepareDirect();
      x = s.directSolve(rhs);
      s.stats.method = "hierarchical";
    }
  } else if (s.selected == "sparse_lu") {
    x = s.lu.solve(rhs);
    if (s.lu.info() != Eigen::Success)
      throw std::runtime_error("IR SparseLU solve failed");
  } else {
    x = s.directSolve(rhs);
  }
  for (int refinement = 0; !s.acceptable(x, rhs) && refinement < 3; ++refinement) {
    if (s.selected == "iccg" && !s.force_direct) {
      s.force_direct = s.stats.fallback = true;
      s.prepareDirect();
      s.stats.method = "hierarchical";
    }
    Eigen::VectorXd residual = rhs - s.matrix * x;
    Eigen::VectorXd correction = s.selected == "sparse_lu" ? Eigen::VectorXd(s.lu.solve(residual)) : s.directSolve(residual);
    x += correction;
  }
  if (!s.acceptable(x, rhs))
    throw std::runtime_error("IR solve failed true-residual validation");
  s.last = x;
  ++s.stats.solves;
  s.stats.solve_seconds = elapsed(start);
  return x;
}
}  // namespace iemir
