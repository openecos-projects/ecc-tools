#pragma once

#include <Eigen/Sparse>
#include <array>
#include <vector>

namespace ir_test {
using Matrix = Eigen::SparseMatrix<double>;
using Points = std::vector<std::array<double, 2>>;

inline Matrix mesh(int side, Points& points)
{
  const int n = side * side;
  points.resize(n);
  std::vector<Eigen::Triplet<double>> entries;
  auto edge = [&](int a, int b, double g) {
    entries.emplace_back(a, a, g);
    entries.emplace_back(b, b, g);
    entries.emplace_back(a, b, -g);
    entries.emplace_back(b, a, -g);
  };
  for (int y = 0; y < side; ++y) {
    for (int x = 0; x < side; ++x) {
      int i = y * side + x;
      points[i] = {double(x), double(y)};
      if (x)
        edge(i, i - 1, 1.0 + (i % 13) * 0.05);
      if (y)
        edge(i, i - side, 0.7 + (i % 7) * 0.07);
      if (y == 0)
        entries.emplace_back(i, i, 2.0);  // explicit ideal-source contacts
    }
  }
  Matrix a(n, n);
  a.setFromTriplets(entries.begin(), entries.end());
  return a;
}

inline Eigen::VectorXd load(int n, int phase = 0)
{
  return Eigen::VectorXd::NullaryExpr(n, [phase](int i) { return -1.e-6 * (1 + ((i + phase) % 7)); });
}
}  // namespace ir_test
