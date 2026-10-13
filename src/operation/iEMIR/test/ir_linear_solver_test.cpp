#include <Eigen/SparseLU>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "IRLinearSolver.hpp"
#include "ir_solver_fixture.hpp"

using iemir::IRLinearSolver;
using iemir::IRSolverOptions;
using ir_test::Matrix;

namespace {
void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

template <class F>
void rejects(F operation)
{
  bool rejected = false;
  try {
    operation();
  } catch (const std::exception&) {
    rejected = true;
  }
  require(rejected, "invalid input was accepted");
}

Eigen::VectorXd reference(const Matrix& a, const Eigen::VectorXd& b)
{
  Eigen::SparseLU<Matrix> lu;
  lu.compute(a);
  require(lu.info() == Eigen::Success, "reference factorization failed");
  return lu.solve(b);
}

void check_backends()
{
  ir_test::Points points;
  Matrix a = ir_test::mesh(35, points);
  Eigen::VectorXd b = ir_test::load(a.rows());
  auto expected = reference(a, b);
  for (const std::string method : {"hierarchical", "iccg", "sparse_lu", "auto"}) {
    IRSolverOptions options;
    options.method = method;
    options.leaf_size = 32;
    IRLinearSolver solver;
    solver.prepare(a, points, options);
    auto x = solver.solve(b);
    require((x - expected).lpNorm<Eigen::Infinity>() < 1.e-10, "backend differs from SparseLU");
    require((a * x - b).norm() / b.norm() < 1.e-8, "bad true residual");
    if (method == "hierarchical")
      require(solver.stats().partition_count > 1, "no nested partitions built");
    if (method == "iccg")
      require(!solver.stats().fallback, "ordinary mesh unexpectedly fell back");
  }
}

void check_lifecycle()
{
  ir_test::Points points;
  Matrix a = ir_test::mesh(18, points);
  IRSolverOptions options;
  options.method = "hierarchical";
  options.leaf_size = 24;
  IRLinearSolver solver;
  solver.prepare(a, points, options);
  auto b = ir_test::load(a.rows());
  auto first = solver.solve(b);
  auto numeric = solver.stats().numeric_preparations;
  auto symbolic = solver.stats().symbolic_preparations;
  solver.prepare(a, points, options);
  auto second = solver.solve(2 * b);
  require(solver.stats().reused_matrix, "unchanged matrix was not reused");
  require(solver.stats().numeric_preparations == numeric, "repeated RHS rebuilt numeric factors");
  require((second - 2 * first).norm() < 1.e-10, "repeated RHS uses stale solution");
  a.coeffRef(0, 0) += 2;
  solver.prepare(a, points, options);
  auto changed = solver.solve(b);
  require(solver.stats().numeric_preparations == numeric + 1, "matrix values did not invalidate factors");
  require(solver.stats().symbolic_preparations == symbolic, "same pattern lost symbolic reuse");
  require((changed - reference(a, b)).norm() < 1.e-10, "numeric update wrong");
  a.coeffRef(0, 50) = a.coeffRef(50, 0) = -0.25;
  a.coeffRef(0, 0) += 0.25;
  a.coeffRef(50, 50) += 0.25;
  a.makeCompressed();
  solver.prepare(a, points, options);
  require(solver.stats().symbolic_preparations == symbolic + 1, "new edge did not invalidate ordering");
  require((solver.solve(b) - reference(a, b)).norm() < 1.e-10, "pattern update wrong");
  points[0][0] += 1000;
  solver.prepare(a, points, options);
  require(!solver.stats().reused_matrix, "changed coordinates reused stale ordering");
  auto expected = reference(a, b);
  a.resize(0, 0);
  require((solver.solve(b) - expected).norm() < 1.e-10, "solver does not own its matrix");
}

void check_fallback_and_warm_start()
{
  ir_test::Points points;
  Matrix a = ir_test::mesh(30, points);
  auto b = ir_test::load(a.rows());
  IRSolverOptions options;
  options.method = "iccg";
  IRLinearSolver solver;
  solver.prepare(a, points, options);
  auto x = solver.solve(b);
  solver.solve(b);
  require(solver.stats().iterations <= 1, "warm start did not reuse converged solution");
  require(solver.solve(Eigen::VectorXd::Zero(a.rows())).isZero(0), "zero RHS preserved previous voltage");
  options.max_iterations = 1;
  solver.prepare(a, points, options);
  x = solver.solve(b);
  require(solver.stats().fallback, "forced iterative failure did not fall back");
  require((x - reference(a, b)).norm() < 1.e-10, "fallback failed accuracy");
}

void check_ladder_and_rc_operator()
{
  Matrix g(2, 2);
  std::vector<Eigen::Triplet<double>> t{{0, 0, 2}, {0, 1, -1}, {1, 0, -1}, {1, 1, 1}};
  g.setFromTriplets(t.begin(), t.end());
  IRLinearSolver solver;
  solver.prepare(g);
  Eigen::Vector2d b(-1.e-9, -1.e-9);
  Eigen::Vector2d expected(-2.e-9, -3.e-9);
  require((solver.solve(b) - expected).norm() < 1.e-20, "tiny-load ladder inaccurate");
  // Future backward-Euler callers supply the effective SPD operator and RHS.
  Matrix effective = g;
  effective.coeffRef(0, 0) += 0.5;
  effective.coeffRef(1, 1) += 0.25;
  solver.prepare(effective);
  auto preparations = solver.stats().numeric_preparations;
  Eigen::VectorXd previous = Eigen::Vector2d::Zero();
  for (int step = 0; step < 4; ++step) {
    Eigen::Vector2d rhs = b;
    rhs[0] += 0.5 * previous[0];
    rhs[1] += 0.25 * previous[1];
    previous = solver.solve(rhs);
    require((previous - reference(effective, rhs)).norm() < 1.e-20, "effective RC solve mismatch");
  }
  require(solver.stats().numeric_preparations == preparations, "time-step RHS refactorized matrix");
  Matrix empty(0, 0);
  solver.prepare(empty);
  require(solver.solve(Eigen::VectorXd(0)).size() == 0, "empty system failed");
}

void check_components_and_geometry()
{
  // Two separately grounded components, with coincident coordinates across
  // layers and a long cross-partition link inside one component.
  ir_test::Points points;
  Matrix block = ir_test::mesh(12, points);
  const int n = block.rows();
  std::vector<Eigen::Triplet<double>> entries;
  for (int offset : {0, n}) {
    for (int col = 0; col < block.outerSize(); ++col) {
      for (Matrix::InnerIterator it(block, col); it; ++it)
        entries.emplace_back(it.row() + offset, col + offset, it.value());
    }
  }
  entries.emplace_back(0, 0, 0.3);
  entries.emplace_back(n - 1, n - 1, 0.3);
  entries.emplace_back(0, n - 1, -0.3);
  entries.emplace_back(n - 1, 0, -0.3);
  Matrix a(2 * n, 2 * n);
  a.setFromTriplets(entries.begin(), entries.end());
  auto second_points = points;
  points.insert(points.end(), second_points.begin(), second_points.end());
  auto b = ir_test::load(a.rows());
  for (const std::string method : {"hierarchical", "iccg"}) {
    IRSolverOptions options;
    options.method = method;
    options.leaf_size = 12;
    IRLinearSolver solver;
    solver.prepare(a, points, options);
    require((solver.solve(b) - reference(a, b)).norm() < 1.e-10, "separate grounded components failed");
    points.assign(a.rows(), {0, 0});
    solver.prepare(a, points, options);
    require((solver.solve(b) - reference(a, b)).norm() < 1.e-10, "coincident geometry failed");
    solver.prepare(block);
    require((solver.solve(b.head(n)) - reference(block, b.head(n))).norm() < 1.e-10, "changed dimension failed");
  }
}

void check_invalid()
{
  IRLinearSolver solver;
  rejects([&] { solver.solve(Eigen::VectorXd::Ones(1)); });
  Matrix rectangular(2, 3);
  rejects([&] { solver.prepare(rectangular); });
  Matrix singular(2, 2);
  std::vector<Eigen::Triplet<double>> t{{0, 0, 1}, {0, 1, -1}, {1, 0, -1}, {1, 1, 1}};
  singular.setFromTriplets(t.begin(), t.end());
  rejects([&] {
    solver.prepare(singular);
    solver.solve(Eigen::Vector2d(1, 0));
  });
  IRSolverOptions iccg;
  iccg.method = "iccg";
  rejects([&] {
    solver.prepare(singular, {}, iccg);
    solver.solve(Eigen::Vector2d::Zero());
  });
  rejects([&] {
    solver.prepare(singular, {}, iccg);
    solver.solve(Eigen::Vector2d(1, -1));
  });
  Matrix general_spd(2, 2);
  std::vector<Eigen::Triplet<double>> positive{{0, 0, 1}, {1, 0, 2}, {0, 1, 2}, {1, 1, 5}};
  general_spd.setFromTriplets(positive.begin(), positive.end());
  solver.prepare(general_spd, {}, iccg);
  require((solver.solve(Eigen::Vector2d(1, 1)) - Eigen::Vector2d(3, -1)).norm() < 1.e-12, "general SPD operator fallback failed");
  general_spd.coeffRef(1, 1) = 1;
  rejects([&] {
    solver.prepare(general_spd, {}, iccg);
    solver.solve(Eigen::Vector2d::Zero());
  });
  ir_test::Points points;
  Matrix a = ir_test::mesh(4, points);
  a.coeffRef(0, 1) *= 0.5;
  rejects([&] { solver.prepare(a, points); });
  a = ir_test::mesh(4, points);
  a.coeffRef(0, 0) = std::numeric_limits<double>::infinity();
  rejects([&] { solver.prepare(a, points); });
  a = ir_test::mesh(4, points);
  solver.prepare(a, points);
  rejects([&] { solver.solve(Eigen::VectorXd::Zero(1)); });
  Eigen::VectorXd b = ir_test::load(a.rows());
  b[0] = std::numeric_limits<double>::quiet_NaN();
  rejects([&] { solver.solve(b); });
  IRSolverOptions options;
  options.method = "unknown";
  rejects([&] { solver.prepare(a, points, options); });
  options.method = "iccg";
  options.relative_tolerance = -1;
  rejects([&] { solver.prepare(a, points, options); });
}
}  // namespace

int main()
{
  try {
    check_backends();
    check_lifecycle();
    check_fallback_and_warm_start();
    check_ladder_and_rc_operator();
    check_components_and_geometry();
    check_invalid();
    std::cout << "IR solver numerical/lifecycle tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
