// Adapted from the validated paper geometry grid.
#pragma once
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace ircx::pattern {
struct GridCaps
{
  double ground = 0, left = 0, right = 0, ground_low = 0, ground_high = 0;
  bool width_clamped = false, gap_clamped = false;
};
inline GridCaps mix(GridCaps a, GridCaps b, double t)
{
  return {a.ground + (b.ground - a.ground) * t, a.left + (b.left - a.left) * t, a.right + (b.right - a.right) * t,
          a.ground_low + (b.ground_low - a.ground_low) * t, a.ground_high + (b.ground_high - a.ground_high) * t};
}
class PatternGrid
{
  struct Grid
  {
    std::map<std::pair<double, double>, GridCaps> data;
    std::vector<double> left, right;
  };
  std::map<std::string, std::map<double, Grid>> groups_;
  static std::string key(const std::string& m, const std::string& b, const std::string& u) { return m + "|" + b + "|" + (u.empty() ? "-" : u); }
  static auto bracket(const std::vector<double>& axis, double x, bool absent)
  {
    std::size_t first = absent ? 1 : 0;
    if (absent && x < 0)
      return std::tuple<double, double, double>{axis.front(), axis.front(), 0.};
    if (first >= axis.size())
      throw std::runtime_error("no present-side samples");
    if (x <= axis[first])
      return std::tuple<double, double, double>{axis[first], axis[first], 0.};
    if (x >= axis.back())
      return std::tuple<double, double, double>{axis.back(), axis.back(), 0.};
    auto hi = std::lower_bound(axis.begin() + first, axis.end(), x);
    auto lo = hi - 1;
    return std::tuple<double, double, double>{*lo, *hi, (x - *lo) / (*hi - *lo)};
  }
  static GridCaps spatial(const Grid& g, double l, double r)
  {
    auto [l0, l1, tl] = bracket(g.left, l, true);
    auto [r0, r1, tr] = bracket(g.right, r, true);
    return mix(mix(g.data.at({l0, r0}), g.data.at({l1, r0}), tl), mix(g.data.at({l0, r1}), g.data.at({l1, r1}), tl), tr);
  }

 public:
  PatternGrid() = default;
  explicit PatternGrid(const std::string& path)
  {
    std::ifstream in(path);
    if (!in)
      throw std::runtime_error("cannot open pattern grid: " + path);
    read(in);
  }
  void read(std::istream& in)
  {
    groups_.clear();
    Grid* current = nullptr;
    for (std::string line; std::getline(in, line);) {
      if (line.empty() || line[0] == '#')
        continue;
      std::istringstream row(line);
      if (line[0] == 'P') {
        std::string p, m, b, u;
        double w = 0;
        if (!(row >> p >> m >> b >> u >> w) || w <= 0 || !std::isfinite(w))
          throw std::runtime_error("invalid grid header");
        row >> std::ws;
        if (!row.eof())
          throw std::runtime_error("extra grid header fields");
        auto& widths = groups_[key(m, b, u)];
        if (widths.count(w))
          throw std::runtime_error("duplicate grid header");
        current = &widths[w];
      } else {
        double l = 0, r = 0;
        GridCaps c;
        if (!current || !(row >> l >> r >> c.ground >> c.left >> c.right))
          throw std::runtime_error("invalid grid row");
        c.ground_low = c.ground_high = c.ground / 2;
        row >> std::ws;
        if (!row.eof()) {
          if (!(row >> c.ground_low >> c.ground_high))
            throw std::runtime_error("invalid directional ground components");
          if (!std::isfinite(c.ground_low) || !std::isfinite(c.ground_high) || c.ground_low < 0 || c.ground_high < 0
              || std::abs(c.ground_low + c.ground_high - c.ground) > 1e-9 * std::max(1., c.ground))
            throw std::runtime_error("directional ground components do not sum to ground");
        }
        row >> std::ws;
        if (!row.eof())
          throw std::runtime_error("extra grid row fields");
        for (double x : {l, r, c.ground, c.left, c.right})
          if (!std::isfinite(x))
            throw std::runtime_error("nonfinite grid row");
        if (c.ground < 0 || c.left < 0 || c.right < 0 || (l < 0 && l != -1) || (r < 0 && r != -1) || (l < 0 && c.left != 0) || (r < 0 && c.right != 0))
          throw std::runtime_error("invalid grid capacitance or absent-side encoding");
        if (!current->data.emplace(std::make_pair(l, r), c).second)
          throw std::runtime_error("duplicate grid row");
      }
    }
    if (groups_.empty())
      throw std::runtime_error("empty grid");
    for (auto& [name, widths] : groups_)
      for (auto& [width, g] : widths) {
        std::set<double> l, r;
        for (const auto& [coordinate, value] : g.data) {
          l.insert(coordinate.first);
          r.insert(coordinate.second);
        }
        if (l.size() < 2 || r.size() < 2 || *l.begin() != -1 || *r.begin() != -1 || g.data.size() != l.size() * r.size())
          throw std::runtime_error("incomplete Cartesian grid");
        g.left.assign(l.begin(), l.end());
        g.right.assign(r.begin(), r.end());
      }
  }
  GridCaps query(const std::string& m, const std::string& b, const std::string& u, double w, double l, double r) const
  {
    const auto& widths = groups_.at(key(m, b, u));
    auto high = widths.lower_bound(w), low = high;
    if (high == widths.end()) {
      high = std::prev(widths.end());
      low = high;
    } else if (high != widths.begin() && high->first != w) {
      low = std::prev(high);
    }
    const double t = low == high ? 0. : (w - low->first) / (high->first - low->first);
    auto caps = mix(spatial(low->second, l, r), spatial(high->second, l, r), t);
    auto outside = [](double value, double lo, double hi) {
      const double tolerance = 1e-9 * std::max({1., std::abs(value), std::abs(lo), std::abs(hi)});
      return value < lo - tolerance || value > hi + tolerance;
    };
    caps.width_clamped = outside(w, widths.begin()->first, widths.rbegin()->first);
    for (const auto* grid : {&low->second, &high->second}) {
      caps.gap_clamped |= (l >= 0 && outside(l, grid->left[1], grid->left.back()));
      caps.gap_clamped |= (r >= 0 && outside(r, grid->right[1], grid->right.back()));
    }
    return caps;
  }
};
}  // namespace ircx::pattern
