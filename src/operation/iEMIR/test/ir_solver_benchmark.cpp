// SPDX-License-Identifier: MulanPSL-2.0
#include <Eigen/SparseLU>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#include "IRLinearSolver.hpp"
#include "ir_solver_fixture.hpp"

int main(int argc, char** argv)
{
  try {
    Eigen::setNbThreads(1);  // stable single-thread solver comparisons
    int side = argc > 1 ? std::stoi(argv[1]) : 100;
    int steps = argc > 2 ? std::stoi(argv[2]) : 10;
    if (side < 2 || steps < 1)
      throw std::invalid_argument("side >= 2 and steps >= 1 required");
    iemir::IRLinearSolver::Points points;
    auto matrix = ir_test::mesh(side, points);
    std::vector<Eigen::VectorXd> rhs, reference;
    for (int i = 0; i < steps; ++i)
      rhs.push_back(ir_test::load(matrix.rows(), i));
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();
    for (const auto& b : rhs) {
      Eigen::SparseLU<iemir::IRLinearSolver::Matrix> lu;
      lu.compute(matrix);
      if (lu.info() != Eigen::Success)
        throw std::runtime_error("reference factorization failed");
      reference.emplace_back(lu.solve(b));
      if (lu.info() != Eigen::Success)
        throw std::runtime_error("reference solve failed");
    }
    double baseline = std::chrono::duration<double>(Clock::now() - start).count();
    std::cout << "nodes,steps,method,prepare_s,solve_total_s,total_s,speedup_vs_rebuild_lu,max_voltage_error_v,max_relative_residual,iterations,fallback,"
                 "numeric_preparations\n";
    std::cout << std::setprecision(10) << matrix.rows() << ',' << steps << ",rebuild_sparse_lu,0,0," << baseline << ",1,0,0,0,0," << steps << '\n';
    for (std::string method : {"sparse_lu", "hierarchical", "iccg", "auto"}) {
      iemir::IRSolverOptions options;
      options.method = method;
      iemir::IRLinearSolver solver;
      solver.prepare(matrix, points, options);
      double setup = solver.stats().prepare_seconds, solve = 0, error = 0, residual = 0;
      int iterations = 0;
      for (int i = 0; i < steps; ++i) {
        auto x = solver.solve(rhs[i]);
        error = std::max(error, (x - reference[i]).cwiseAbs().maxCoeff());
        residual = std::max(residual, solver.stats().relative_residual);
        iterations += solver.stats().iterations;
        solve += solver.stats().solve_seconds;
      }
      if (error > 1.e-8 || residual > 1.e-7)
        throw std::runtime_error("benchmark numerical mismatch");
      std::cout << matrix.rows() << ',' << steps << ',' << method << ',' << setup << ',' << solve << ',' << setup + solve << ',' << baseline / (setup + solve)
                << ',' << error << ',' << residual << ',' << iterations << ',' << solver.stats().fallback << ',' << solver.stats().numeric_preparations << '\n';
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
