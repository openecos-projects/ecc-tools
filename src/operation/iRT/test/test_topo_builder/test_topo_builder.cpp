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
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Logger.hpp"
#include "TOPOBuilder.hpp"

#include "utility/logger/Logger.hpp"

namespace {

using irt::PlanarCoord;
using irt::PlanarRect;
using irt::Segment;

constexpr double kInf = std::numeric_limits<double>::infinity();

struct PlotOptions
{
  std::optional<std::filesystem::path> plot_dir;
  bool show_help = false;
};

void printUsage(const char* program)
{
  std::cout << "Usage: " << program << " [--plot-dir <directory>] [--help]\n";
}

bool parseOptions(int argc, char* argv[], PlotOptions& options)
{
  for (int arg_idx = 1; arg_idx < argc; arg_idx++) {
    std::string arg = argv[arg_idx];
    if (arg == "--help") {
      options.show_help = true;
    } else if (arg == "--plot-dir") {
      if (arg_idx + 1 >= argc) {
        std::cerr << "Missing value for --plot-dir\n";
        return false;
      }
      options.plot_dir = argv[++arg_idx];
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      return false;
    }
  }
  return true;
}

bool preparePlotDirectory(const std::filesystem::path& plot_dir)
{
  std::error_code error;
  std::filesystem::create_directories(plot_dir, error);
  if (error) {
    std::cerr << "Failed to create plot directory '" << plot_dir.string() << "': " << error.message() << "\n";
    return false;
  }
  return true;
}
bool check(bool condition, const std::string& case_name)
{
  if (!condition) {
    ECCLOG.warn(ecc::Loc::current(), "Failed: ", case_name);
  }
  return condition;
}

int32_t getDistance(const PlanarCoord& first, const PlanarCoord& second)
{
  return std::abs(first.get_x() - second.get_x()) + std::abs(first.get_y() - second.get_y());
}

double addCost(double first, double second)
{
  return std::isfinite(first) && std::isfinite(second) ? first + second : kInf;
}

class GridCostMap
{
 public:
  explicit GridCostMap(const PlanarRect& region, double default_cost = 1)
      : _region(region),
        _horizontal_cost((region.get_ur_x() - region.get_ll_x()) * (region.get_ur_y() - region.get_ll_y() + 1), default_cost),
        _vertical_cost((region.get_ur_x() - region.get_ll_x() + 1) * (region.get_ur_y() - region.get_ll_y()), default_cost)
  {
  }

  void setHorizontalCost(int32_t x, int32_t y, double cost) { _horizontal_cost[getHorizontalIndex(x, y)] = cost; }
  void setVerticalCost(int32_t x, int32_t y, double cost) { _vertical_cost[getVerticalIndex(x, y)] = cost; }

  double getCost(const PlanarCoord& first, const PlanarCoord& second) const
  {
    if (first == second) {
      return 0;
    }
    double cost = 0;
    if (first.get_y() == second.get_y()) {
      int32_t ll_x = std::min(first.get_x(), second.get_x());
      int32_t ur_x = std::max(first.get_x(), second.get_x());
      for (int32_t x = ll_x; x < ur_x; x++) {
        if (!containsHorizontalEdge(x, first.get_y())) {
          return kInf;
        }
        cost = addCost(cost, _horizontal_cost[getHorizontalIndex(x, first.get_y())]);
      }
      return cost;
    }
    if (first.get_x() == second.get_x()) {
      int32_t ll_y = std::min(first.get_y(), second.get_y());
      int32_t ur_y = std::max(first.get_y(), second.get_y());
      for (int32_t y = ll_y; y < ur_y; y++) {
        if (!containsVerticalEdge(first.get_x(), y)) {
          return kInf;
        }
        cost = addCost(cost, _vertical_cost[getVerticalIndex(first.get_x(), y)]);
      }
      return cost;
    }
    return kInf;
  }

  irt::TBSegmentCostQuery getQuery() const
  {
    return [cost_map = *this](const PlanarCoord& first, const PlanarCoord& second) { return cost_map.getCost(first, second); };
  }

 private:
  bool containsHorizontalEdge(int32_t x, int32_t y) const
  {
    return _region.get_ll_x() <= x && x < _region.get_ur_x() && _region.get_ll_y() <= y && y <= _region.get_ur_y();
  }

  bool containsVerticalEdge(int32_t x, int32_t y) const
  {
    return _region.get_ll_x() <= x && x <= _region.get_ur_x() && _region.get_ll_y() <= y && y < _region.get_ur_y();
  }

  size_t getHorizontalIndex(int32_t x, int32_t y) const
  {
    int32_t width = _region.get_ur_x() - _region.get_ll_x();
    return static_cast<size_t>(y - _region.get_ll_y()) * width + x - _region.get_ll_x();
  }

  size_t getVerticalIndex(int32_t x, int32_t y) const
  {
    int32_t height = _region.get_ur_y() - _region.get_ll_y();
    return static_cast<size_t>(x - _region.get_ll_x()) * height + y - _region.get_ll_y();
  }

  PlanarRect _region;
  std::vector<double> _horizontal_cost;
  std::vector<double> _vertical_cost;
};

using CoordKey = std::pair<int32_t, int32_t>;
using EdgeKey = std::pair<CoordKey, CoordKey>;
using CanonicalTopo = std::vector<std::array<int32_t, 4>>;

CoordKey getCoordKey(const PlanarCoord& coord)
{
  return {coord.get_x(), coord.get_y()};
}

CanonicalTopo canonicalizeTopo(const std::vector<Segment<PlanarCoord>>& topo_list)
{
  CanonicalTopo canonical_topo;
  canonical_topo.reserve(topo_list.size());
  for (const Segment<PlanarCoord>& topo : topo_list) {
    CoordKey first = getCoordKey(topo.get_first());
    CoordKey second = getCoordKey(topo.get_second());
    if (second < first) {
      std::swap(first, second);
    }
    canonical_topo.push_back({first.first, first.second, second.first, second.second});
  }
  std::ranges::sort(canonical_topo);
  return canonical_topo;
}

bool isSameStat(const irt::TBRefineStat& first, const irt::TBRefineStat& second)
{
  return first.shifted_edge_num == second.shifted_edge_num && first.refined_steiner_num == second.refined_steiner_num
         && first.attempted_congestion_flute == second.attempted_congestion_flute
         && first.used_congestion_flute == second.used_congestion_flute
         && first.attempted_steiner_refine == second.attempted_steiner_refine && first.used_steiner_refine == second.used_steiner_refine
         && first.used_terminal_mst == second.used_terminal_mst;
}

bool isTopoValid(const std::vector<PlanarCoord>& terminal_list, const std::vector<Segment<PlanarCoord>>& topo_list, const PlanarRect& region)
{
  if (terminal_list.size() <= 1) {
    return topo_list.empty();
  }

  std::set<CoordKey> coord_set;
  std::set<EdgeKey> edge_set;
  auto is_inside = [&](const PlanarCoord& coord) {
    return region.get_ll_x() <= coord.get_x() && coord.get_x() <= region.get_ur_x() && region.get_ll_y() <= coord.get_y()
           && coord.get_y() <= region.get_ur_y();
  };
  for (const Segment<PlanarCoord>& topo : topo_list) {
    if (topo.get_first() == topo.get_second() || !is_inside(topo.get_first()) || !is_inside(topo.get_second())) {
      return false;
    }
    CoordKey first = getCoordKey(topo.get_first());
    CoordKey second = getCoordKey(topo.get_second());
    if (second < first) {
      std::swap(first, second);
    }
    if (!edge_set.emplace(first, second).second) {
      return false;
    }
    coord_set.insert(first);
    coord_set.insert(second);
  }
  for (const PlanarCoord& terminal : terminal_list) {
    if (!coord_set.contains(getCoordKey(terminal))) {
      return false;
    }
  }

  std::map<CoordKey, int32_t> coord_idx_map;
  int32_t coord_idx = 0;
  for (const CoordKey& coord : coord_set) {
    coord_idx_map[coord] = coord_idx++;
  }
  std::vector<int32_t> parent(coord_set.size());
  std::iota(parent.begin(), parent.end(), 0);
  std::function<int32_t(int32_t)> find_root = [&](int32_t idx) {
    if (parent[idx] != idx) {
      parent[idx] = find_root(parent[idx]);
    }
    return parent[idx];
  };
  for (const auto& [first, second] : edge_set) {
    int32_t first_root = find_root(coord_idx_map[first]);
    int32_t second_root = find_root(coord_idx_map[second]);
    if (first_root == second_root) {
      return false;
    }
    parent[first_root] = second_root;
  }
  return !coord_set.empty() && edge_set.size() + 1 == coord_set.size();
}

double getPatternCost(const irt::TBSegmentCostQuery& query, const PlanarCoord& first, const PlanarCoord& second)
{
  PlanarCoord x_bend(second.get_x(), first.get_y());
  PlanarCoord y_bend(first.get_x(), second.get_y());
  return std::min(addCost(query(first, x_bend), query(x_bend, second)), addCost(query(first, y_bend), query(y_bend, second)));
}

double getTopoCost(const std::vector<Segment<PlanarCoord>>& topo_list, const irt::TBSegmentCostQuery& query)
{
  double cost = 0;
  for (const Segment<PlanarCoord>& topo : topo_list) {
    cost = addCost(cost, getPatternCost(query, topo.get_first(), topo.get_second()));
  }
  return cost;
}

int32_t getWireLength(const std::vector<Segment<PlanarCoord>>& topo_list)
{
  int32_t wire_length = 0;
  for (const Segment<PlanarCoord>& topo : topo_list) {
    wire_length += getDistance(topo.get_first(), topo.get_second());
  }
  return wire_length;
}

bool containsCoord(const std::vector<Segment<PlanarCoord>>& topo_list, const PlanarCoord& coord)
{
  return std::ranges::any_of(topo_list, [&](const Segment<PlanarCoord>& topo) { return topo.get_first() == coord || topo.get_second() == coord; });
}

bool isInsideRect(const PlanarRect& rect, const PlanarCoord& coord)
{
  return rect.get_ll_x() <= coord.get_x() && coord.get_x() <= rect.get_ur_x() && rect.get_ll_y() <= coord.get_y()
         && coord.get_y() <= rect.get_ur_y();
}

bool isSameTopo(const std::vector<Segment<PlanarCoord>>& first, const std::vector<Segment<PlanarCoord>>& second)
{
  if (first.size() != second.size()) {
    return false;
  }
  for (size_t i = 0; i < first.size(); i++) {
    if (first[i].get_first() != second[i].get_first() || first[i].get_second() != second[i].get_second()) {
      return false;
    }
  }
  return true;
}

irt::TBTask makeTask(const std::vector<PlanarCoord>& terminal_list, irt::TBSegmentCostQuery query = {}, bool congestion_driven = false)
{
  irt::TBTask task;
  task.set_planar_coord_list(terminal_list);
  task.set_planar_search_region(PlanarRect(0, 0, 49, 49));
  task.set_congestion_driven(congestion_driven);
  if (query) {
    task.set_segment_cost_query(std::move(query));
  }
  return task;
}

std::vector<PlanarCoord> getBaseTerminalList()
{
  return {PlanarCoord(0, 0), PlanarCoord(10, 30), PlanarCoord(30, 10), PlanarCoord(40, 40)};
}

std::vector<PlanarCoord> getMultiHotspotTerminalList()
{
  return {PlanarCoord(0, 0),  PlanarCoord(2, 4),   PlanarCoord(4, 12),  PlanarCoord(8, 7),
          PlanarCoord(8, 34), PlanarCoord(10, 30), PlanarCoord(30, 10), PlanarCoord(40, 40)};
}

GridCostMap getMultiHotspotCostMap()
{
  PlanarRect region(0, 0, 49, 49);
  GridCostMap cost_map(region);
  for (int32_t y = region.get_ll_y(); y <= region.get_ur_y(); y++) {
    if (y <= 10) {
      for (int32_t x = region.get_ll_x(); x < region.get_ur_x(); x++) {
        cost_map.setHorizontalCost(x, y, 50);
      }
    } else if (36 <= y) {
      for (int32_t x = region.get_ll_x(); x < region.get_ur_x(); x++) {
        cost_map.setHorizontalCost(x, y, 20);
      }
    }
  }
  for (int32_t x = 18; x <= 24; x++) {
    for (int32_t y = 18; y < 25; y++) {
      cost_map.setVerticalCost(x, y, 12);
    }
  }
  return cost_map;
}

std::vector<PlanarCoord> getCorridorTerminalList()
{
  return {PlanarCoord(2, 8), PlanarCoord(2, 40), PlanarCoord(24, 2), PlanarCoord(24, 46), PlanarCoord(46, 8), PlanarCoord(46, 40)};
}

GridCostMap getCorridorCostMap()
{
  PlanarRect region(0, 0, 49, 49);
  GridCostMap cost_map(region, kInf);
  for (int32_t y : {8, 24, 40}) {
    for (int32_t x = region.get_ll_x(); x < region.get_ur_x(); x++) {
      cost_map.setHorizontalCost(x, y, 1);
    }
  }
  for (int32_t x : {2, 24, 46}) {
    for (int32_t y = region.get_ll_y(); y < region.get_ur_y(); y++) {
      cost_map.setVerticalCost(x, y, 1);
    }
  }
  return cost_map;
}

std::vector<PlanarCoord> getHighDegreeTerminalList()
{
  std::vector<PlanarCoord> terminal_list;
  terminal_list.reserve(32);
  for (int32_t row = 0; row < 4; row++) {
    for (int32_t column = 0; column < 8; column++) {
      terminal_list.emplace_back(2 + column * 6, 2 + row * 12 + ((column + row) % 3) * 2);
    }
  }
  return terminal_list;
}

GridCostMap getHighDegreeCostMap()
{
  PlanarRect region(0, 0, 49, 49);
  GridCostMap cost_map(region);
  for (int32_t y = region.get_ll_y(); y <= region.get_ur_y(); y++) {
    for (int32_t x = 18; x < 23; x++) {
      cost_map.setHorizontalCost(x, y, 8);
    }
  }
  for (int32_t x = region.get_ll_x(); x <= region.get_ur_x(); x++) {
    for (int32_t y = 20; y < 24; y++) {
      cost_map.setVerticalCost(x, y, 12);
    }
  }
  return cost_map;
}

std::vector<PlanarCoord> getSteinerCoordList(const std::vector<PlanarCoord>& terminal_list,
                                             const std::vector<Segment<PlanarCoord>>& topo_list)
{
  std::set<CoordKey> terminal_set;
  for (const PlanarCoord& terminal : terminal_list) {
    terminal_set.insert(getCoordKey(terminal));
  }
  std::set<CoordKey> steiner_set;
  for (const Segment<PlanarCoord>& topo : topo_list) {
    for (const PlanarCoord& coord : {topo.get_first(), topo.get_second()}) {
      if (!terminal_set.contains(getCoordKey(coord))) {
        steiner_set.insert(getCoordKey(coord));
      }
    }
  }
  std::vector<PlanarCoord> steiner_list;
  steiner_list.reserve(steiner_set.size());
  for (const auto& [x, y] : steiner_set) {
    steiner_list.emplace_back(x, y);
  }
  return steiner_list;
}

GridCostMap getSteinerBlockedCostMap(const std::vector<PlanarCoord>& steiner_list)
{
  PlanarRect region(0, 0, 49, 49);
  GridCostMap cost_map(region);
  for (const PlanarCoord& steiner : steiner_list) {
    if (region.get_ll_x() < steiner.get_x()) {
      cost_map.setHorizontalCost(steiner.get_x() - 1, steiner.get_y(), kInf);
    }
    if (steiner.get_x() < region.get_ur_x()) {
      cost_map.setHorizontalCost(steiner.get_x(), steiner.get_y(), kInf);
    }
    if (region.get_ll_y() < steiner.get_y()) {
      cost_map.setVerticalCost(steiner.get_x(), steiner.get_y() - 1, kInf);
    }
    if (steiner.get_y() < region.get_ur_y()) {
      cost_map.setVerticalCost(steiner.get_x(), steiner.get_y(), kInf);
    }
  }
  return cost_map;
}

std::vector<Segment<PlanarCoord>> getBaseFluteTopoList()
{
  return {{PlanarCoord(0, 0), PlanarCoord(30, 10)},
          {PlanarCoord(10, 30), PlanarCoord(30, 30)},
          {PlanarCoord(40, 40), PlanarCoord(30, 30)},
          {PlanarCoord(30, 10), PlanarCoord(30, 30)}};
}

irt::TBSegmentCostQuery getWireCostQuery()
{
  return [](const PlanarCoord& first, const PlanarCoord& second) { return static_cast<double>(getDistance(first, second)); };
}

bool checkBaseline()
{
  bool passed = true;
  passed = check(RTTB.getPlanarTopoList(makeTask({})).empty(), "empty topology") && passed;
  passed = check(RTTB.getPlanarTopoList(makeTask({PlanarCoord(10, 20)})).empty(), "single-pin topology") && passed;
  passed = check(isSameTopo(RTTB.getPlanarTopoList(makeTask(getBaseTerminalList())), getBaseFluteTopoList()), "default FLUTE topology") && passed;

  irt::TBRefineStat stat;
  std::vector<Segment<PlanarCoord>> topo_list = RTTB.getPlanarTopoList(makeTask(getBaseTerminalList(), getWireCostQuery()), stat);
  passed = check(isSameTopo(topo_list, getBaseFluteTopoList()), "uniform finite cost keeps FLUTE topology") && passed;
  passed = check(stat.shifted_edge_num == 0 && stat.refined_steiner_num == 0, "uniform cost does not refine topology") && passed;
  return passed;
}

bool checkCostDrivenShift()
{
  irt::TBSegmentCostQuery query = [](const PlanarCoord& first, const PlanarCoord& second) {
    double cost = getDistance(first, second);
    if (first.get_x() == second.get_x() && first.get_x() == 30) {
      cost += 100 * getDistance(first, second);
    }
    return cost;
  };
  std::vector<Segment<PlanarCoord>> baseline = RTTB.getPlanarTopoList(makeTask(getBaseTerminalList()));
  irt::TBRefineStat stat;
  std::vector<Segment<PlanarCoord>> refined = RTTB.getPlanarTopoList(makeTask(getBaseTerminalList(), query), stat);

  bool passed = true;
  passed = check(stat.shifted_edge_num > 0, "congested Steiner edge is shifted") && passed;
  passed = check(getTopoCost(refined, query) < getTopoCost(baseline, query), "edge shift lowers topology cost") && passed;
  passed = check(getWireLength(refined) == getWireLength(baseline), "edge shift preserves wire length") && passed;
  passed = check(isSameTopo(refined, RTTB.getPlanarTopoList(makeTask(getBaseTerminalList(), query))), "edge shift is deterministic") && passed;
  return passed;
}

bool checkCongestionFluteGuard()
{
  irt::TBRefineStat low_degree_stat;
  std::vector<Segment<PlanarCoord>> low_degree_topo
      = RTTB.getPlanarTopoList(makeTask({PlanarCoord(0, 0), PlanarCoord(20, 20)}, getWireCostQuery(), true), low_degree_stat);

  irt::TBRefineStat uniform_stat;
  std::vector<Segment<PlanarCoord>> uniform_topo = RTTB.getPlanarTopoList(makeTask(getBaseTerminalList(), getWireCostQuery(), true), uniform_stat);

  bool passed = true;
  passed = check(!low_degree_topo.empty() && !low_degree_stat.attempted_congestion_flute, "two-pin net skips congestion FLUTE") && passed;
  passed = check(uniform_stat.attempted_congestion_flute && !uniform_stat.used_congestion_flute,
                 "uniform cost rejects equal congestion FLUTE candidate")
           && passed;
  passed = check(isSameTopo(uniform_topo, getBaseFluteTopoList()), "uniform congestion FLUTE keeps baseline topology") && passed;
  for (const PlanarCoord& terminal : getBaseTerminalList()) {
    passed = check(containsCoord(uniform_topo, terminal), "congestion FLUTE keeps terminal coordinate") && passed;
  }
  return passed;
}

bool checkCongestionFluteCostGuard()
{
  irt::TBSegmentCostQuery query = [](const PlanarCoord& first, const PlanarCoord& second) {
    double cost = getDistance(first, second);
    if (first.get_y() == second.get_y() && first.get_y() <= 10) {
      cost *= 50;
    }
    return cost;
  };
  std::vector<Segment<PlanarCoord>> normal = RTTB.getPlanarTopoList(makeTask(getBaseTerminalList(), query));
  irt::TBRefineStat stat;
  std::vector<Segment<PlanarCoord>> congestion = RTTB.getPlanarTopoList(makeTask(getBaseTerminalList(), query, true), stat);

  bool passed = true;
  passed = check(stat.attempted_congestion_flute, "costed four-pin net attempts congestion FLUTE") && passed;
  passed = check(stat.used_congestion_flute, "lower-cost congestion FLUTE candidate is selected") && passed;
  passed = check(getTopoCost(congestion, query) < getTopoCost(normal, query), "selected congestion FLUTE lowers topology cost") && passed;
  passed = check(isSameTopo(congestion, RTTB.getPlanarTopoList(makeTask(getBaseTerminalList(), query, true))),
                 "congestion FLUTE is deterministic")
           && passed;
  return passed;
}

bool checkCongestionFluteQueryBound()
{
  int64_t query_num = 0;
  irt::TBSegmentCostQuery query = [&query_num](const PlanarCoord& first, const PlanarCoord& second) {
    query_num++;
    return static_cast<double>(getDistance(first, second));
  };
  std::vector<PlanarCoord> terminal_list
      = {PlanarCoord(0, 0), PlanarCoord(0, 200), PlanarCoord(200, 0), PlanarCoord(200, 200)};
  RTTB.getPlanarTopoList(makeTask(terminal_list, query, true));
  return check(query_num <= 30000, "congestion FLUTE bounds bbox cost queries");
}

bool checkAlignedSegmentQueryOnce()
{
  int64_t query_num = 0;
  irt::TBSegmentCostQuery query = [&query_num](const PlanarCoord& first, const PlanarCoord& second) {
    query_num++;
    return static_cast<double>(getDistance(first, second));
  };
  std::vector<Segment<PlanarCoord>> topo_list
      = RTTB.getPlanarTopoList(makeTask({PlanarCoord(3, 7), PlanarCoord(31, 7)}, query));

  bool passed = true;
  passed = check(topo_list.size() == 1, "aligned two-pin net keeps one topology edge") && passed;
  passed = check(query_num == 1, "aligned topology edge queries segment cost once") && passed;
  return passed;
}

bool checkInfCostPruning()
{
  int64_t query_num = 0;
  irt::TBSegmentCostQuery query = [&query_num](const PlanarCoord&, const PlanarCoord&) {
    query_num++;
    return kInf;
  };
  RTTB.getPlanarTopoList(makeTask({PlanarCoord(3, 7), PlanarCoord(31, 29)}, query));
  return check(query_num == 2, "blocked L-patterns stop after their first segment");
}

irt::TBSegmentCostQuery getBlockedCoordQuery(const PlanarCoord& blocked_coord, bool block_all)
{
  return [blocked_coord, block_all](const PlanarCoord& first, const PlanarCoord& second) {
    if (first == second) {
      return 0.0;
    }
    if (block_all) {
      return kInf;
    }
    if (first.get_y() == second.get_y() && first.get_y() == blocked_coord.get_y()) {
      int32_t ll_x = std::min(first.get_x(), second.get_x());
      int32_t ur_x = std::max(first.get_x(), second.get_x());
      if ((ll_x <= blocked_coord.get_x() - 1 && blocked_coord.get_x() <= ur_x)
          || (ll_x <= blocked_coord.get_x() && blocked_coord.get_x() + 1 <= ur_x)) {
        return kInf;
      }
    }
    if (first.get_x() == second.get_x() && first.get_x() == blocked_coord.get_x()) {
      int32_t ll_y = std::min(first.get_y(), second.get_y());
      int32_t ur_y = std::max(first.get_y(), second.get_y());
      if ((ll_y <= blocked_coord.get_y() - 1 && blocked_coord.get_y() <= ur_y)
          || (ll_y <= blocked_coord.get_y() && blocked_coord.get_y() + 1 <= ur_y)) {
        return kInf;
      }
    }
    return static_cast<double>(getDistance(first, second));
  };
}

GridCostMap getMacroRingCostMap(const PlanarRect& macro, int32_t ring_width, double ring_cost)
{
  const PlanarRect region(0, 0, 49, 49);
  const PlanarRect expanded_macro(macro.get_ll_x() - ring_width, macro.get_ll_y() - ring_width, macro.get_ur_x() + ring_width,
                                  macro.get_ur_y() + ring_width);
  GridCostMap cost_map(region);
  auto getEdgeCost = [&](const PlanarCoord& first, const PlanarCoord& second) {
    if (isInsideRect(macro, first) || isInsideRect(macro, second)) {
      return kInf;
    }
    return isInsideRect(expanded_macro, first) || isInsideRect(expanded_macro, second) ? ring_cost : 1.0;
  };
  for (int32_t y = region.get_ll_y(); y <= region.get_ur_y(); y++) {
    for (int32_t x = region.get_ll_x(); x < region.get_ur_x(); x++) {
      cost_map.setHorizontalCost(x, y, getEdgeCost(PlanarCoord(x, y), PlanarCoord(x + 1, y)));
    }
  }
  for (int32_t x = region.get_ll_x(); x <= region.get_ur_x(); x++) {
    for (int32_t y = region.get_ll_y(); y < region.get_ur_y(); y++) {
      cost_map.setVerticalCost(x, y, getEdgeCost(PlanarCoord(x, y), PlanarCoord(x, y + 1)));
    }
  }
  return cost_map;
}

GridCostMap getBlockedMacroCostMap(const PlanarRect& macro)
{
  return getMacroRingCostMap(macro, 0, 1);
}

bool checkCongestionFluteInfHandling()
{
  std::vector<PlanarCoord> terminal_list = getBaseTerminalList();
  irt::TBRefineStat partial_stat;
  std::vector<Segment<PlanarCoord>> partial_topo
      = RTTB.getPlanarTopoList(makeTask(terminal_list, getBlockedCoordQuery(PlanarCoord(20, 20), false), true), partial_stat);

  irt::TBSegmentCostQuery fully_blocked_query = getBlockedCoordQuery({}, true);
  irt::TBRefineStat fully_blocked_stat;
  std::vector<Segment<PlanarCoord>> fully_blocked_topo
      = RTTB.getPlanarTopoList(makeTask(terminal_list, fully_blocked_query, true), fully_blocked_stat);

  bool passed = true;
  passed = check(partial_stat.attempted_congestion_flute, "partial INF still attempts congestion FLUTE") && passed;
  for (const PlanarCoord& terminal : terminal_list) {
    passed = check(containsCoord(partial_topo, terminal), "partial INF congestion FLUTE keeps terminal") && passed;
  }
  passed = check(fully_blocked_stat.attempted_congestion_flute && !fully_blocked_stat.used_congestion_flute,
                 "fully blocked bbox rejects congestion FLUTE")
           && passed;
  passed = check(fully_blocked_stat.used_terminal_mst, "fully blocked bbox uses terminal MST fallback") && passed;
  passed = check(fully_blocked_topo.size() == terminal_list.size() - 1, "fully blocked fallback builds a tree") && passed;
  for (const Segment<PlanarCoord>& segment : fully_blocked_topo) {
    passed = check(std::ranges::find(terminal_list, segment.get_first()) != terminal_list.end()
                       && std::ranges::find(terminal_list, segment.get_second()) != terminal_list.end(),
                   "fully blocked fallback only uses terminals")
             && passed;
  }
  return passed;
}

bool checkThreePinCongestionAvoidsMacro()
{
  const PlanarRect region(0, 0, 49, 49);
  const PlanarRect macro(4, 0, 16, 8);
  std::vector<PlanarCoord> terminal_list = {PlanarCoord(0, 0), PlanarCoord(10, 20), PlanarCoord(20, 0)};
  PlanarCoord raw_steiner(10, 0);
  std::vector<Segment<PlanarCoord>> raw_topo = RTTB.getPlanarTopoList(makeTask(terminal_list));
  irt::TBSegmentCostQuery query = getBlockedMacroCostMap(macro).getQuery();
  irt::TBRefineStat stat;
  std::vector<Segment<PlanarCoord>> topo_list = RTTB.getPlanarTopoList(makeTask(terminal_list, query, true), stat);
  std::vector<Segment<PlanarCoord>> repeated_topo = RTTB.getPlanarTopoList(makeTask(terminal_list, query, true));

  bool passed = true;
  passed = check(isInsideRect(macro, raw_steiner) && containsCoord(raw_topo, raw_steiner), "full-layer macro contains raw Steiner") && passed;
  passed = check(std::ranges::none_of(terminal_list, [&](const PlanarCoord& terminal) { return isInsideRect(macro, terminal); }),
                 "full-layer macro excludes all terminals")
           && passed;
  passed = check(!std::isfinite(getTopoCost(raw_topo, query)), "full-layer macro blocks normal FLUTE topology") && passed;
  passed = check(stat.attempted_congestion_flute && stat.used_congestion_flute, "three-pin congestion FLUTE is selected") && passed;
  passed = check(stat.shifted_edge_num == 0 && !stat.attempted_steiner_refine && !stat.used_terminal_mst,
                 "three-pin congestion FLUTE avoids fallback")
           && passed;
  passed = check(std::isfinite(getTopoCost(topo_list, query)), "three-pin congestion topology has finite cost") && passed;
  passed = check(!containsCoord(topo_list, raw_steiner), "three-pin congestion topology leaves blocked coordinate") && passed;
  passed = check(std::ranges::none_of(getSteinerCoordList(terminal_list, topo_list),
                                     [&](const PlanarCoord& steiner) { return isInsideRect(macro, steiner); }),
                 "three-pin congestion topology keeps Steiner outside full-layer macro")
           && passed;
  for (const PlanarCoord& terminal : terminal_list) {
    passed = check(containsCoord(topo_list, terminal), "three-pin congestion topology keeps terminal") && passed;
  }
  passed = check(isTopoValid(terminal_list, topo_list, region), "three-pin congestion topology is valid") && passed;
  passed = check(canonicalizeTopo(topo_list) == canonicalizeTopo(repeated_topo), "three-pin congestion topology is deterministic") && passed;
  return passed;
}

bool checkThreePinCongestionOutsidePinBBox()
{
  const PlanarRect region(0, 0, 49, 49);
  std::vector<PlanarCoord> terminal_list = {PlanarCoord(10, 10), PlanarCoord(15, 20), PlanarCoord(20, 10)};
  GridCostMap cost_map(region, kInf);
  for (int32_t x = 10; x < 20; x++) {
    cost_map.setHorizontalCost(x, 9, 1);
  }
  cost_map.setVerticalCost(10, 9, 1);
  cost_map.setVerticalCost(20, 9, 1);
  for (int32_t y = 9; y < 20; y++) {
    cost_map.setVerticalCost(15, y, 1);
  }
  irt::TBSegmentCostQuery query = cost_map.getQuery();
  irt::TBRefineStat stat;
  std::vector<Segment<PlanarCoord>> topo_list = RTTB.getPlanarTopoList(makeTask(terminal_list, query, true), stat);
  std::vector<PlanarCoord> steiner_list = getSteinerCoordList(terminal_list, topo_list);

  bool passed = true;
  passed = check(stat.attempted_congestion_flute && stat.used_congestion_flute, "three-pin congestion searches outside pin bbox") && passed;
  passed = check(!stat.attempted_steiner_refine && !stat.used_terminal_mst, "outside-bbox three-pin congestion avoids fallback")
           && passed;
  passed = check(std::isfinite(getTopoCost(topo_list, query)), "outside-bbox three-pin topology has finite cost") && passed;
  passed = check(std::ranges::any_of(steiner_list, [](const PlanarCoord& steiner) { return steiner.get_y() < 10; }),
                 "three-pin congestion places Steiner outside pin bbox")
           && passed;
  passed = check(isTopoValid(terminal_list, topo_list, region), "outside-bbox three-pin topology is valid") && passed;
  return passed;
}

bool checkNormalDefersBlockedSteiner()
{
  const PlanarRect macro(4, 0, 16, 8);
  std::vector<PlanarCoord> terminal_list = {PlanarCoord(0, 0), PlanarCoord(10, 20), PlanarCoord(20, 0)};
  irt::TBRefineStat stat;
  std::vector<Segment<PlanarCoord>> topo_list = RTTB.getPlanarTopoList(makeTask(terminal_list, getBlockedMacroCostMap(macro).getQuery()), stat);
  bool passed = true;
  passed = check(!stat.attempted_steiner_refine && !stat.used_terminal_mst, "normal mode skips congestion fallback") && passed;
  passed = check(containsCoord(topo_list, PlanarCoord(10, 0)), "normal mode defers blocked Steiner handling") && passed;
  passed = check(!std::isfinite(getTopoCost(topo_list, getBlockedMacroCostMap(macro).getQuery())),
                 "deferred normal topology remains blocked")
           && passed;
  return passed;
}

bool checkMultiHotspotCompetition()
{
  const PlanarRect region(0, 0, 49, 49);
  std::vector<PlanarCoord> terminal_list = getMultiHotspotTerminalList();
  irt::TBSegmentCostQuery query = getMultiHotspotCostMap().getQuery();
  std::vector<Segment<PlanarCoord>> normal_topo = RTTB.getPlanarTopoList(makeTask(terminal_list, query));
  irt::TBRefineStat stat;
  std::vector<Segment<PlanarCoord>> congestion_topo = RTTB.getPlanarTopoList(makeTask(terminal_list, query, true), stat);
  irt::TBRefineStat repeated_stat;
  std::vector<Segment<PlanarCoord>> repeated_topo = RTTB.getPlanarTopoList(makeTask(terminal_list, query, true), repeated_stat);

  bool passed = true;
  passed = check(stat.attempted_congestion_flute && stat.used_congestion_flute, "multi-hotspot selects congestion FLUTE") && passed;
  passed = check(getTopoCost(congestion_topo, query) < getTopoCost(normal_topo, query), "multi-hotspot topology lowers cost") && passed;
  passed = check(isTopoValid(terminal_list, congestion_topo, region), "multi-hotspot topology is valid") && passed;
  passed = check(canonicalizeTopo(congestion_topo) == canonicalizeTopo(repeated_topo), "multi-hotspot topology is deterministic") && passed;
  passed = check(isSameStat(stat, repeated_stat), "multi-hotspot statistics are deterministic") && passed;
  return passed;
}

bool checkFiniteCorridor()
{
  const PlanarRect region(0, 0, 49, 49);
  std::vector<PlanarCoord> terminal_list = getCorridorTerminalList();
  irt::TBSegmentCostQuery query = getCorridorCostMap().getQuery();
  std::vector<Segment<PlanarCoord>> normal_topo = RTTB.getPlanarTopoList(makeTask(terminal_list, query));
  irt::TBRefineStat stat;
  std::vector<Segment<PlanarCoord>> selected_topo = RTTB.getPlanarTopoList(makeTask(terminal_list, query, true), stat);
  std::vector<Segment<PlanarCoord>> repeated_topo = RTTB.getPlanarTopoList(makeTask(terminal_list, query, true));

  bool passed = true;
  passed = check(stat.attempted_congestion_flute, "finite corridor attempts congestion FLUTE") && passed;
  passed = check(std::isfinite(getTopoCost(selected_topo, query)), "finite corridor produces finite topology cost") && passed;
  passed = check(getTopoCost(selected_topo, query) <= getTopoCost(normal_topo, query), "finite corridor does not regress topology cost") && passed;
  passed = check(isTopoValid(terminal_list, selected_topo, region), "finite corridor topology is valid") && passed;
  passed = check(canonicalizeTopo(selected_topo) == canonicalizeTopo(repeated_topo), "finite corridor topology is deterministic") && passed;
  return passed;
}

bool checkHighDegreeSteinerRefine()
{
  const PlanarRect region(0, 0, 49, 49);
  std::vector<PlanarCoord> terminal_list
      = {PlanarCoord(0, 0), PlanarCoord(0, 40), PlanarCoord(10, 15), PlanarCoord(25, 30), PlanarCoord(40, 0), PlanarCoord(40, 40)};
  std::vector<Segment<PlanarCoord>> raw_topo = RTTB.getPlanarTopoList(makeTask(terminal_list));
  std::vector<PlanarCoord> raw_steiner_list = getSteinerCoordList(terminal_list, raw_topo);
  irt::TBSegmentCostQuery query = getSteinerBlockedCostMap(raw_steiner_list).getQuery();
  int64_t query_num = 0;
  irt::TBSegmentCostQuery counted_query = [&query_num, query](const PlanarCoord& first, const PlanarCoord& second) {
    query_num++;
    return query(first, second);
  };
  irt::TBRefineStat stat;
  std::vector<Segment<PlanarCoord>> refined_topo = RTTB.getPlanarTopoList(makeTask(terminal_list, counted_query, true), stat);

  bool passed = true;
  passed = check(raw_steiner_list.size() >= 2, "high-degree case has multiple raw Steiner coordinates") && passed;
  passed = check(stat.attempted_steiner_refine && stat.used_steiner_refine && stat.refined_steiner_num > 0,
                 "high-degree blocked topology uses Steiner refinement")
           && passed;
  passed = check(!stat.used_terminal_mst, "finite high-degree refinement avoids terminal MST") && passed;
  for (const PlanarCoord& raw_steiner : raw_steiner_list) {
    passed = check(!containsCoord(refined_topo, raw_steiner), "high-degree refinement leaves blocked Steiner coordinate") && passed;
  }
  passed = check(std::isfinite(getTopoCost(refined_topo, query)), "high-degree refinement produces finite topology") && passed;
  passed = check(isTopoValid(terminal_list, refined_topo, region), "high-degree refined topology is valid") && passed;
  passed = check(query_num <= 50000, "high-degree refinement bounds cost queries") && passed;
  return passed;
}

bool checkHighDegreeStress()
{
  const PlanarRect region(0, 0, 49, 49);
  std::vector<PlanarCoord> terminal_list = getHighDegreeTerminalList();
  irt::TBSegmentCostQuery base_query = getHighDegreeCostMap().getQuery();
  int64_t query_num = 0;
  irt::TBSegmentCostQuery counted_query = [&query_num, base_query](const PlanarCoord& first, const PlanarCoord& second) {
    query_num++;
    return base_query(first, second);
  };
  irt::TBRefineStat stat;
  std::vector<Segment<PlanarCoord>> topo_list = RTTB.getPlanarTopoList(makeTask(terminal_list, counted_query, true), stat);
  irt::TBRefineStat repeated_stat;
  std::vector<Segment<PlanarCoord>> repeated_topo = RTTB.getPlanarTopoList(makeTask(terminal_list, base_query, true), repeated_stat);

  bool passed = true;
  passed = check(stat.attempted_congestion_flute, "high-degree net attempts congestion FLUTE") && passed;
  passed = check(std::isfinite(getTopoCost(topo_list, base_query)), "high-degree topology has finite cost") && passed;
  passed = check(isTopoValid(terminal_list, topo_list, region), "high-degree topology is valid") && passed;
  passed = check(canonicalizeTopo(topo_list) == canonicalizeTopo(repeated_topo), "high-degree topology is deterministic") && passed;
  passed = check(isSameStat(stat, repeated_stat), "high-degree statistics are deterministic") && passed;
  passed = check(query_num <= 500000, "high-degree topology bounds cost queries") && passed;
  return passed;
}

bool checkPartialLayerMacroKeepsSteiner()
{
  const PlanarRect region(0, 0, 49, 49);
  const PlanarRect macro(18, 20, 36, 36);
  const PlanarCoord steiner(30, 30);
  std::vector<PlanarCoord> terminal_list = getBaseTerminalList();
  irt::TBSegmentCostQuery query = GridCostMap(region).getQuery();
  std::vector<Segment<PlanarCoord>> baseline_topo = RTTB.getPlanarTopoList(makeTask(terminal_list));
  irt::TBRefineStat stat;
  std::vector<Segment<PlanarCoord>> selected_topo = RTTB.getPlanarTopoList(makeTask(terminal_list, query), stat);

  bool passed = true;
  passed = check(isInsideRect(macro, steiner) && containsCoord(baseline_topo, steiner), "partial-layer macro contains raw Steiner") && passed;
  passed = check(std::ranges::none_of(terminal_list, [&](const PlanarCoord& terminal) { return isInsideRect(macro, terminal); }),
                 "partial-layer macro excludes all terminals")
           && passed;
  for (const PlanarCoord& neighbor : {PlanarCoord(steiner.get_x() - 1, steiner.get_y()), PlanarCoord(steiner.get_x() + 1, steiner.get_y()),
                                      PlanarCoord(steiner.get_x(), steiner.get_y() - 1), PlanarCoord(steiner.get_x(), steiner.get_y() + 1)}) {
    passed = check(std::isfinite(query(steiner, neighbor)), "partial-layer macro keeps finite Steiner escape") && passed;
  }
  passed = check(canonicalizeTopo(selected_topo) == canonicalizeTopo(baseline_topo), "partial-layer macro keeps FLUTE topology") && passed;
  passed = check(containsCoord(selected_topo, steiner), "partial-layer macro keeps Steiner coordinate") && passed;
  passed = check(stat.shifted_edge_num == 0 && stat.refined_steiner_num == 0 && !stat.used_terminal_mst,
                 "partial-layer macro does not refine Steiner")
           && passed;
  passed = check(std::isfinite(getTopoCost(selected_topo, query)), "partial-layer macro topology has finite cost") && passed;
  passed = check(isTopoValid(terminal_list, selected_topo, region), "partial-layer macro topology is valid") && passed;
  return passed;
}

bool checkFullLayerMacroCongestionRing()
{
  const PlanarRect region(0, 0, 49, 49);
  const PlanarRect macro(18, 20, 36, 36);
  const PlanarRect expanded_macro(16, 18, 38, 38);
  const PlanarCoord raw_steiner(30, 30);
  std::vector<PlanarCoord> terminal_list = getBaseTerminalList();
  irt::TBSegmentCostQuery query = getMacroRingCostMap(macro, 2, 50).getQuery();
  irt::TBSegmentCostQuery strict_avoid_query = getMacroRingCostMap(macro, 2, kInf).getQuery();
  std::vector<Segment<PlanarCoord>> raw_topo = RTTB.getPlanarTopoList(makeTask(terminal_list));
  irt::TBRefineStat stat;
  std::vector<Segment<PlanarCoord>> selected_topo = RTTB.getPlanarTopoList(makeTask(terminal_list, query, true), stat);
  std::vector<Segment<PlanarCoord>> repeated_topo = RTTB.getPlanarTopoList(makeTask(terminal_list, query, true));

  bool passed = true;
  passed = check(isInsideRect(macro, raw_steiner) && containsCoord(raw_topo, raw_steiner), "full-layer macro contains raw Steiner") && passed;
  passed = check(std::ranges::none_of(terminal_list, [&](const PlanarCoord& terminal) { return isInsideRect(expanded_macro, terminal); }),
                 "full-layer macro ring excludes all terminals")
           && passed;
  passed = check(!std::isfinite(query(PlanarCoord(18, 20), PlanarCoord(19, 20))), "full-layer macro edges are INF") && passed;
  passed = check(query(PlanarCoord(16, 18), PlanarCoord(17, 18)) == 50, "macro congestion ring has high finite cost") && passed;
  passed = check(query(PlanarCoord(0, 0), PlanarCoord(1, 0)) == 1, "edges outside macro ring keep base cost") && passed;
  passed = check(!std::isfinite(getTopoCost(raw_topo, query)), "full-layer macro blocks raw FLUTE topology") && passed;
  passed = check(stat.attempted_congestion_flute, "full-layer macro ring attempts congestion FLUTE") && passed;
  passed = check(stat.used_congestion_flute || stat.shifted_edge_num > 0, "full-layer macro ring selects a cost-driven topology") && passed;
  passed = check(!stat.attempted_steiner_refine && !stat.used_terminal_mst, "full-layer macro ring avoids fallback")
           && passed;
  passed = check(std::isfinite(getTopoCost(selected_topo, query)), "full-layer macro ring topology has finite cost") && passed;
  passed = check(canonicalizeTopo(selected_topo) != canonicalizeTopo(raw_topo), "full-layer macro ring changes raw topology") && passed;
  passed = check(std::isfinite(getTopoCost(selected_topo, strict_avoid_query)), "selected topology can avoid macro congestion ring") && passed;
  passed = check(std::ranges::none_of(getSteinerCoordList(terminal_list, selected_topo),
                                     [&](const PlanarCoord& steiner) { return isInsideRect(expanded_macro, steiner); }),
                 "selected Steiner coordinates avoid macro congestion ring")
           && passed;
  passed = check(isTopoValid(terminal_list, selected_topo, region), "full-layer macro ring topology is valid") && passed;
  passed = check(canonicalizeTopo(selected_topo) == canonicalizeTopo(repeated_topo), "full-layer macro ring topology is deterministic") && passed;
  return passed;
}

struct PlotTopoLayer
{
  std::string name;
  std::vector<Segment<PlanarCoord>> topo_list;
  std::string color;
  bool dashed = false;
};

struct PlotMarker
{
  PlanarCoord coord;
  std::string label;
  std::string color;
};

struct PlotCase
{
  std::string file_name;
  std::string title;
  std::string summary;
  PlanarRect region;
  std::vector<PlanarCoord> terminal_list;
  std::vector<PlanarRect> macro_rect_list;
  std::string macro_label;
  std::vector<PlotTopoLayer> topo_layer_list;
  std::vector<PlotMarker> marker_list;
  irt::TBSegmentCostQuery cost_query;
};

struct PlotSteinerUsage
{
  std::set<CoordKey> used_set;
  std::set<CoordKey> unused_set;
};

PlotSteinerUsage getPlotSteinerUsage(const PlotCase& plot_case)
{
  std::set<CoordKey> terminal_set;
  for (const PlanarCoord& terminal : plot_case.terminal_list) {
    terminal_set.insert(getCoordKey(terminal));
  }

  PlotSteinerUsage usage;
  for (const PlotTopoLayer& layer : plot_case.topo_layer_list) {
    std::set<CoordKey>& target_set = layer.dashed ? usage.unused_set : usage.used_set;
    for (const Segment<PlanarCoord>& topo : layer.topo_list) {
      for (const PlanarCoord& coord : {topo.get_first(), topo.get_second()}) {
        if (!terminal_set.contains(getCoordKey(coord))) {
          target_set.insert(getCoordKey(coord));
        }
      }
    }
  }
  for (const CoordKey& used_coord : usage.used_set) {
    usage.unused_set.erase(used_coord);
  }
  return usage;
}

bool checkSteinerUsageClassification()
{
  PlotCase plot_case;
  plot_case.terminal_list = {PlanarCoord(0, 0), PlanarCoord(4, 4)};
  plot_case.topo_layer_list = {{"raw",
                                {{PlanarCoord(0, 0), PlanarCoord(2, 2)},
                                 {PlanarCoord(2, 2), PlanarCoord(3, 3)},
                                 {PlanarCoord(3, 3), PlanarCoord(4, 4)}},
                                "",
                                true},
                               {"selected",
                                {{PlanarCoord(0, 0), PlanarCoord(2, 2)},
                                 {PlanarCoord(2, 2), PlanarCoord(1, 1)},
                                 {PlanarCoord(1, 1), PlanarCoord(4, 4)}},
                                "",
                                false}};
  PlotSteinerUsage usage = getPlotSteinerUsage(plot_case);
  return check(usage.used_set == std::set<CoordKey>{{1, 1}, {2, 2}} && usage.unused_set == std::set<CoordKey>{{3, 3}},
               "plot distinguishes used and unused Steiner coordinates");
}

struct PlotTransform
{
  double scale = 1;
  double origin_x = 0;
  double origin_y = 0;
  int32_t ll_x = 0;
  int32_t ur_y = 0;

  double getX(int32_t x) const { return origin_x + ((x - ll_x) * scale); }
  double getY(int32_t y) const { return origin_y + ((ur_y - y) * scale); }
};

struct PlotCostEdge
{
  PlanarCoord first;
  PlanarCoord second;
  double cost = 0;
};

std::string escapeXml(const std::string& text)
{
  std::string escaped;
  escaped.reserve(text.size());
  for (char ch : text) {
    switch (ch) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '"': escaped += "&quot;"; break;
      case '\'': escaped += "&apos;"; break;
      default: escaped += ch; break;
    }
  }
  return escaped;
}

std::string formatCost(double cost)
{
  if (!std::isfinite(cost)) {
    return "INF";
  }
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(1) << cost;
  return stream.str();
}

std::vector<PlotCostEdge> getPlotCostEdgeList(const PlotCase& plot_case, double& min_cost, double& max_cost)
{
  min_cost = kInf;
  max_cost = 0;
  std::vector<PlotCostEdge> edge_list;
  const PlanarRect& region = plot_case.region;
  for (int32_t y = region.get_ll_y(); y <= region.get_ur_y(); y++) {
    for (int32_t x = region.get_ll_x(); x < region.get_ur_x(); x++) {
      PlanarCoord first(x, y);
      PlanarCoord second(x + 1, y);
      double cost = plot_case.cost_query(first, second);
      edge_list.push_back({first, second, cost});
      if (std::isfinite(cost)) {
        min_cost = std::min(min_cost, cost);
        max_cost = std::max(max_cost, cost);
      }
    }
  }
  for (int32_t x = region.get_ll_x(); x <= region.get_ur_x(); x++) {
    for (int32_t y = region.get_ll_y(); y < region.get_ur_y(); y++) {
      PlanarCoord first(x, y);
      PlanarCoord second(x, y + 1);
      double cost = plot_case.cost_query(first, second);
      edge_list.push_back({first, second, cost});
      if (std::isfinite(cost)) {
        min_cost = std::min(min_cost, cost);
        max_cost = std::max(max_cost, cost);
      }
    }
  }
  if (!std::isfinite(min_cost)) {
    min_cost = 0;
  }
  max_cost = std::max(max_cost, min_cost);
  return edge_list;
}

std::string getCostColor(double cost, double min_cost, double max_cost)
{
  if (!std::isfinite(cost)) {
    return "rgb(185,28,28)";
  }
  double range = max_cost - min_cost;
  double ratio = range <= 0 ? 0 : std::log1p(std::max(0.0, cost - min_cost)) / std::log1p(range);
  int32_t red = static_cast<int32_t>(59 + ((220 - 59) * ratio));
  int32_t green = static_cast<int32_t>(130 + ((38 - 130) * ratio));
  int32_t blue = static_cast<int32_t>(246 + ((38 - 246) * ratio));
  return "rgb(" + std::to_string(red) + "," + std::to_string(green) + "," + std::to_string(blue) + ")";
}

PlotTransform getPlotTransform(const PlanarRect& region)
{
  constexpr double canvas_width = 1000;
  constexpr double canvas_height = 760;
  constexpr double left = 80;
  constexpr double right = 60;
  constexpr double top = 90;
  constexpr double bottom = 120;
  double x_span = std::max(1, region.get_ur_x() - region.get_ll_x());
  double y_span = std::max(1, region.get_ur_y() - region.get_ll_y());
  double scale = std::min((canvas_width - left - right) / x_span, (canvas_height - top - bottom) / y_span);
  double plot_width = x_span * scale;
  double plot_height = y_span * scale;
  return {.scale = scale,
          .origin_x = left + ((canvas_width - left - right - plot_width) / 2),
          .origin_y = top + ((canvas_height - top - bottom - plot_height) / 2),
          .ll_x = region.get_ll_x(),
          .ur_y = region.get_ur_y()};
}

void appendPolyline(std::ostream& stream, const std::vector<PlanarCoord>& coord_list, const PlotTransform& transform, const std::string& color,
                    double width, bool dashed)
{
  if (coord_list.size() <= 1) {
    return;
  }
  stream << "<polyline points=\"";
  for (const PlanarCoord& coord : coord_list) {
    stream << transform.getX(coord.get_x()) << "," << transform.getY(coord.get_y()) << " ";
  }
  stream << "\" fill=\"none\" stroke=\"" << color << "\" stroke-width=\"" << width
         << "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"";
  if (dashed) {
    stream << " stroke-dasharray=\"8 6\"";
  }
  stream << "/>\n";
}

bool writePlotSvg(const std::filesystem::path& plot_dir, const PlotCase& plot_case)
{
  std::filesystem::path output_path = plot_dir / plot_case.file_name;
  std::ofstream stream(output_path);
  if (!stream) {
    std::cerr << "Failed to open plot file '" << output_path.string() << "'\n";
    return false;
  }

  PlotTransform transform = getPlotTransform(plot_case.region);
  double min_cost = 0;
  double max_cost = 0;
  std::vector<PlotCostEdge> cost_edge_list = getPlotCostEdgeList(plot_case, min_cost, max_cost);
  const PlanarRect& region = plot_case.region;

  stream << std::fixed << std::setprecision(2);
  stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1000 760\" role=\"img\">\n"
         << "<rect width=\"1000\" height=\"760\" fill=\"#ffffff\"/>\n"
         << "<text x=\"500\" y=\"34\" text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"22\" font-weight=\"600\">"
         << escapeXml(plot_case.title) << "</text>\n"
         << "<text x=\"500\" y=\"60\" text-anchor=\"middle\" font-family=\"monospace\" font-size=\"13\" fill=\"#374151\">"
         << escapeXml(plot_case.summary) << "</text>\n";

  if ((region.get_ur_x() - region.get_ll_x()) <= 100 && (region.get_ur_y() - region.get_ll_y()) <= 100) {
    for (int32_t x = region.get_ll_x(); x <= region.get_ur_x(); x++) {
      stream << "<line x1=\"" << transform.getX(x) << "\" y1=\"" << transform.getY(region.get_ll_y()) << "\" x2=\""
             << transform.getX(x) << "\" y2=\"" << transform.getY(region.get_ur_y())
             << "\" stroke=\"#e5e7eb\" stroke-width=\"0.7\"/>\n";
    }
    for (int32_t y = region.get_ll_y(); y <= region.get_ur_y(); y++) {
      stream << "<line x1=\"" << transform.getX(region.get_ll_x()) << "\" y1=\"" << transform.getY(y) << "\" x2=\""
             << transform.getX(region.get_ur_x()) << "\" y2=\"" << transform.getY(y)
             << "\" stroke=\"#e5e7eb\" stroke-width=\"0.7\"/>\n";
    }
  }

  for (const PlotCostEdge& edge : cost_edge_list) {
    bool is_inf = !std::isfinite(edge.cost);
    stream << "<line x1=\"" << transform.getX(edge.first.get_x()) << "\" y1=\"" << transform.getY(edge.first.get_y()) << "\" x2=\""
           << transform.getX(edge.second.get_x()) << "\" y2=\"" << transform.getY(edge.second.get_y()) << "\" stroke=\""
           << getCostColor(edge.cost, min_cost, max_cost) << "\" stroke-width=\"" << (is_inf ? 4.0 : 2.2)
           << "\" stroke-opacity=\"" << (is_inf ? 0.85 : 0.32) << "\"/>\n";
  }

  for (const PlanarRect& macro : plot_case.macro_rect_list) {
    double x = transform.getX(macro.get_ll_x());
    double y = transform.getY(macro.get_ur_y());
    double width = transform.getX(macro.get_ur_x()) - x;
    double height = transform.getY(macro.get_ll_y()) - y;
    stream << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << width << "\" height=\"" << height
           << "\" fill=\"#6b7280\" fill-opacity=\"0.12\" stroke=\"#4b5563\" stroke-width=\"2.5\" stroke-dasharray=\"6 4\"/>\n";
  }

  for (const PlotTopoLayer& layer : plot_case.topo_layer_list) {
    for (const Segment<PlanarCoord>& topo : layer.topo_list) {
      appendPolyline(stream, {topo.get_first(), topo.get_second()}, transform, layer.color, layer.dashed ? 3.0 : 4.5, layer.dashed);
    }
  }

  PlotSteinerUsage steiner_usage = getPlotSteinerUsage(plot_case);
  for (const auto& [x, y] : steiner_usage.unused_set) {
    stream << "<circle cx=\"" << transform.getX(x) << "\" cy=\"" << transform.getY(y)
           << "\" r=\"5\" fill=\"#ffffff\" stroke=\"#9ca3af\" stroke-width=\"2\" stroke-dasharray=\"3 2\"/>\n";
  }
  for (const auto& [x, y] : steiner_usage.used_set) {
    stream << "<rect x=\"" << transform.getX(x) - 4.5 << "\" y=\"" << transform.getY(y) - 4.5
           << "\" width=\"9\" height=\"9\" fill=\"#ffffff\" stroke=\"#111827\" stroke-width=\"2\"/>\n";
  }
  for (const PlanarCoord& terminal : plot_case.terminal_list) {
    stream << "<circle cx=\"" << transform.getX(terminal.get_x()) << "\" cy=\"" << transform.getY(terminal.get_y())
           << "\" r=\"5.5\" fill=\"#111827\" stroke=\"#ffffff\" stroke-width=\"1.5\"/>\n";
  }
  for (const PlotMarker& marker : plot_case.marker_list) {
    double x = transform.getX(marker.coord.get_x());
    double y = transform.getY(marker.coord.get_y());
    stream << "<line x1=\"" << x - 7 << "\" y1=\"" << y - 7 << "\" x2=\"" << x + 7 << "\" y2=\"" << y + 7
           << "\" stroke=\"" << marker.color << "\" stroke-width=\"3\"/>\n"
           << "<line x1=\"" << x - 7 << "\" y1=\"" << y + 7 << "\" x2=\"" << x + 7 << "\" y2=\"" << y - 7
           << "\" stroke=\"" << marker.color << "\" stroke-width=\"3\"/>\n"
           << "<text x=\"" << x + 10 << "\" y=\"" << y - 10 << "\" font-family=\"sans-serif\" font-size=\"12\" fill=\""
           << marker.color << "\">" << escapeXml(marker.label) << "</text>\n";
  }

  double legend_x = 80;
  double legend_y = 690;
  for (const PlotTopoLayer& layer : plot_case.topo_layer_list) {
    stream << "<line x1=\"" << legend_x << "\" y1=\"" << legend_y << "\" x2=\"" << legend_x + 34 << "\" y2=\"" << legend_y
           << "\" stroke=\"" << layer.color << "\" stroke-width=\"4\"";
    if (layer.dashed) {
      stream << " stroke-dasharray=\"8 6\"";
    }
    stream << "/>\n<text x=\"" << legend_x + 42 << "\" y=\"" << legend_y + 4
           << "\" font-family=\"sans-serif\" font-size=\"13\" fill=\"#111827\">" << escapeXml(layer.name) << "</text>\n";
    legend_x += 180;
  }
  stream << "<circle cx=\"" << legend_x << "\" cy=\"" << legend_y << "\" r=\"5\" fill=\"#111827\"/>\n"
         << "<text x=\"" << legend_x + 12 << "\" y=\"" << legend_y + 4
         << "\" font-family=\"sans-serif\" font-size=\"13\">terminal</text>\n";
  legend_x += 130;
  stream << "<rect x=\"" << legend_x - 4.5 << "\" y=\"" << legend_y - 4.5
         << "\" width=\"9\" height=\"9\" fill=\"#ffffff\" stroke=\"#111827\" stroke-width=\"2\"/>\n"
         << "<text x=\"" << legend_x + 12 << "\" y=\"" << legend_y + 4
         << "\" font-family=\"sans-serif\" font-size=\"13\">used Steiner</text>\n";
  legend_x += 160;
  stream << "<circle cx=\"" << legend_x << "\" cy=\"" << legend_y
         << "\" r=\"5\" fill=\"#ffffff\" stroke=\"#9ca3af\" stroke-width=\"2\" stroke-dasharray=\"3 2\"/>\n"
         << "<text x=\"" << legend_x + 12 << "\" y=\"" << legend_y + 4
         << "\" font-family=\"sans-serif\" font-size=\"13\">unused Steiner</text>\n";
  if (!plot_case.macro_rect_list.empty()) {
    legend_x = 80;
    legend_y = 724;
    stream << "<rect x=\"" << legend_x - 7 << "\" y=\"" << legend_y - 5
           << "\" width=\"14\" height=\"10\" fill=\"#6b7280\" fill-opacity=\"0.12\" stroke=\"#4b5563\" stroke-width=\"2\"/>\n"
           << "<text x=\"" << legend_x + 12 << "\" y=\"" << legend_y + 4
           << "\" font-family=\"sans-serif\" font-size=\"13\">" << escapeXml(plot_case.macro_label) << "</text>\n";
  }
  stream << "<text x=\"" << (plot_case.macro_rect_list.empty() ? 80 : 260)
         << "\" y=\"728\" font-family=\"sans-serif\" font-size=\"12\" fill=\"#4b5563\">"
         << "Edge heatmap: blue=low cost, red=high cost, dark red=INF. Lines connect abstract topology endpoints directly."
         << "</text>\n</svg>\n";
  if (!stream) {
    std::cerr << "Failed to write plot file '" << output_path.string() << "'\n";
    return false;
  }
  return true;
}

bool generatePlots(const std::filesystem::path& plot_dir)
{
  std::error_code error;
  std::filesystem::remove(plot_dir / "index.html", error);
  if (error) {
    std::cerr << "Failed to remove legacy plot index: " << error.message() << "\n";
    return false;
  }

  const PlanarRect region(0, 0, 49, 49);
  const std::vector<PlanarCoord> base_terminal_list = getBaseTerminalList();
  irt::TBSegmentCostQuery wire_query = getWireCostQuery();
  std::vector<PlotCase> plot_case_list;

  std::vector<Segment<PlanarCoord>> baseline = RTTB.getPlanarTopoList(makeTask(base_terminal_list));
  plot_case_list.push_back({.file_name = "01_baseline.svg",
                            .title = "Baseline FLUTE",
                            .summary = "wire_length=" + std::to_string(getWireLength(baseline)) + ", estimated_cost="
                                       + formatCost(getTopoCost(baseline, wire_query)),
                            .region = region,
                            .terminal_list = base_terminal_list,
                            .topo_layer_list = {{"baseline", baseline, "#6b7280", false}},
                            .cost_query = wire_query});

  irt::TBSegmentCostQuery shift_query = [](const PlanarCoord& first, const PlanarCoord& second) {
    double cost = getDistance(first, second);
    if (first.get_x() == second.get_x() && first.get_x() == 30) {
      cost += 100 * getDistance(first, second);
    }
    return cost;
  };
  irt::TBRefineStat shift_stat;
  std::vector<Segment<PlanarCoord>> shifted = RTTB.getPlanarTopoList(makeTask(base_terminal_list, shift_query), shift_stat);
  plot_case_list.push_back({.file_name = "02_cost_shift.svg",
                            .title = "Cost-driven Steiner shift",
                            .summary = "baseline_cost=" + formatCost(getTopoCost(baseline, shift_query)) + ", refined_cost="
                                       + formatCost(getTopoCost(shifted, shift_query)) + ", shifted_edges="
                                       + std::to_string(shift_stat.shifted_edge_num),
                            .region = region,
                            .terminal_list = base_terminal_list,
                            .topo_layer_list = {{"baseline", baseline, "#6b7280", true}, {"refined", shifted, "#2563eb", false}},
                            .cost_query = shift_query});

  irt::TBSegmentCostQuery congestion_query = [](const PlanarCoord& first, const PlanarCoord& second) {
    double cost = getDistance(first, second);
    if (first.get_y() == second.get_y() && first.get_y() <= 10) {
      cost *= 50;
    }
    return cost;
  };
  std::vector<Segment<PlanarCoord>> normal = RTTB.getPlanarTopoList(makeTask(base_terminal_list, congestion_query));
  irt::TBRefineStat congestion_stat;
  std::vector<Segment<PlanarCoord>> congestion
      = RTTB.getPlanarTopoList(makeTask(base_terminal_list, congestion_query, true), congestion_stat);
  plot_case_list.push_back({.file_name = "03_congestion_flute.svg",
                            .title = "Normal vs congestion FLUTE",
                            .summary = "normal_cost=" + formatCost(getTopoCost(normal, congestion_query)) + ", congestion_cost="
                                       + formatCost(getTopoCost(congestion, congestion_query)) + ", attempted="
                                       + std::to_string(congestion_stat.attempted_congestion_flute) + ", used="
                                       + std::to_string(congestion_stat.used_congestion_flute),
                            .region = region,
                            .terminal_list = base_terminal_list,
                            .topo_layer_list = {{"normal", normal, "#6b7280", true}, {"congestion", congestion, "#16a34a", false}},
                            .cost_query = congestion_query});

  PlanarCoord blocked_coord(20, 20);
  irt::TBSegmentCostQuery blocked_query = getBlockedCoordQuery(blocked_coord, false);
  std::vector<Segment<PlanarCoord>> blocked_normal = RTTB.getPlanarTopoList(makeTask(base_terminal_list, blocked_query));
  irt::TBRefineStat blocked_stat;
  std::vector<Segment<PlanarCoord>> blocked_congestion
      = RTTB.getPlanarTopoList(makeTask(base_terminal_list, blocked_query, true), blocked_stat);
  plot_case_list.push_back({.file_name = "04_inf_handling.svg",
                            .title = "INF edge handling",
                            .summary = "normal_cost=" + formatCost(getTopoCost(blocked_normal, blocked_query)) + ", selected_cost="
                                       + formatCost(getTopoCost(blocked_congestion, blocked_query)) + ", congestion_used="
                                       + std::to_string(blocked_stat.used_congestion_flute),
                            .region = region,
                            .terminal_list = base_terminal_list,
                            .topo_layer_list = {{"normal", blocked_normal, "#6b7280", true},
                                                {"selected", blocked_congestion, "#16a34a", false}},
                            .marker_list = {{blocked_coord, "blocked coordinate", "#dc2626"}},
                            .cost_query = blocked_query});

  std::vector<PlanarCoord> three_pin_terminal_list = {PlanarCoord(0, 0), PlanarCoord(10, 20), PlanarCoord(20, 0)};
  PlanarCoord raw_steiner(10, 0);
  PlanarRect three_pin_macro(4, 0, 16, 8);
  std::vector<Segment<PlanarCoord>> raw_three_pin_topo = RTTB.getPlanarTopoList(makeTask(three_pin_terminal_list));
  irt::TBSegmentCostQuery three_pin_query = getBlockedMacroCostMap(three_pin_macro).getQuery();
  irt::TBRefineStat three_pin_stat;
  std::vector<Segment<PlanarCoord>> congestion_topo
      = RTTB.getPlanarTopoList(makeTask(three_pin_terminal_list, three_pin_query, true), three_pin_stat);
  plot_case_list.push_back({.file_name = "05_three_pin_congestion.svg",
                            .title = "Three-pin congestion FLUTE",
                            .summary = "attempted=" + std::to_string(three_pin_stat.attempted_congestion_flute) + ", used="
                                       + std::to_string(three_pin_stat.used_congestion_flute) + ", cost="
                                       + formatCost(getTopoCost(congestion_topo, three_pin_query)) + ", mst="
                                       + std::to_string(three_pin_stat.used_terminal_mst),
                            .region = PlanarRect(0, 0, 24, 24),
                            .terminal_list = three_pin_terminal_list,
                            .macro_rect_list = {three_pin_macro},
                            .macro_label = "full-layer macro",
                            .topo_layer_list = {{"raw", raw_three_pin_topo, "#6b7280", true}, {"congestion", congestion_topo, "#2563eb", false}},
                            .marker_list = {{raw_steiner, "raw blocked Steiner", "#dc2626"}},
                            .cost_query = three_pin_query});

  std::vector<PlanarCoord> hotspot_terminal_list = getMultiHotspotTerminalList();
  irt::TBSegmentCostQuery hotspot_query = getMultiHotspotCostMap().getQuery();
  std::vector<Segment<PlanarCoord>> hotspot_normal = RTTB.getPlanarTopoList(makeTask(hotspot_terminal_list, hotspot_query));
  irt::TBRefineStat hotspot_stat;
  std::vector<Segment<PlanarCoord>> hotspot_congestion
      = RTTB.getPlanarTopoList(makeTask(hotspot_terminal_list, hotspot_query, true), hotspot_stat);
  plot_case_list.push_back({.file_name = "06_multi_hotspot.svg",
                            .title = "Multi-hotspot topology competition",
                            .summary = "normal_cost=" + formatCost(getTopoCost(hotspot_normal, hotspot_query)) + ", selected_cost="
                                       + formatCost(getTopoCost(hotspot_congestion, hotspot_query)) + ", congestion_used="
                                       + std::to_string(hotspot_stat.used_congestion_flute),
                            .region = region,
                            .terminal_list = hotspot_terminal_list,
                            .topo_layer_list = {{"normal", hotspot_normal, "#6b7280", true},
                                                {"congestion", hotspot_congestion, "#16a34a", false}},
                            .cost_query = hotspot_query});

  std::vector<PlanarCoord> corridor_terminal_list = getCorridorTerminalList();
  irt::TBSegmentCostQuery corridor_query = getCorridorCostMap().getQuery();
  std::vector<Segment<PlanarCoord>> corridor_normal = RTTB.getPlanarTopoList(makeTask(corridor_terminal_list, corridor_query));
  irt::TBRefineStat corridor_stat;
  std::vector<Segment<PlanarCoord>> corridor_selected
      = RTTB.getPlanarTopoList(makeTask(corridor_terminal_list, corridor_query, true), corridor_stat);
  plot_case_list.push_back({.file_name = "07_finite_corridor.svg",
                            .title = "Finite corridors through INF field",
                            .summary = "normal_cost=" + formatCost(getTopoCost(corridor_normal, corridor_query)) + ", selected_cost="
                                       + formatCost(getTopoCost(corridor_selected, corridor_query)) + ", congestion_used="
                                       + std::to_string(corridor_stat.used_congestion_flute),
                            .region = region,
                            .terminal_list = corridor_terminal_list,
                            .topo_layer_list = {{"normal", corridor_normal, "#6b7280", true},
                                                {"selected", corridor_selected, "#16a34a", false}},
                            .cost_query = corridor_query});

  std::vector<PlanarCoord> refine_terminal_list
      = {PlanarCoord(0, 0), PlanarCoord(0, 40), PlanarCoord(10, 15), PlanarCoord(25, 30), PlanarCoord(40, 0), PlanarCoord(40, 40)};
  std::vector<Segment<PlanarCoord>> refine_raw = RTTB.getPlanarTopoList(makeTask(refine_terminal_list));
  std::vector<PlanarCoord> multi_raw_steiner_list = getSteinerCoordList(refine_terminal_list, refine_raw);
  irt::TBSegmentCostQuery refine_query = getSteinerBlockedCostMap(multi_raw_steiner_list).getQuery();
  irt::TBRefineStat refine_stat;
  std::vector<Segment<PlanarCoord>> refined_topo = RTTB.getPlanarTopoList(makeTask(refine_terminal_list, refine_query, true), refine_stat);
  std::vector<PlotMarker> refine_marker_list;
  for (const PlanarCoord& steiner : multi_raw_steiner_list) {
    refine_marker_list.push_back({steiner, "blocked Steiner", "#dc2626"});
  }
  plot_case_list.push_back({.file_name = "08_high_degree_refine.svg",
                            .title = "High-degree Steiner refinement",
                            .summary = "attempted=" + std::to_string(refine_stat.attempted_steiner_refine) + ", used="
                                       + std::to_string(refine_stat.used_steiner_refine) + ", refined="
                                       + std::to_string(refine_stat.refined_steiner_num) + ", cost="
                                       + formatCost(getTopoCost(refined_topo, refine_query)),
                            .region = region,
                            .terminal_list = refine_terminal_list,
                            .topo_layer_list = {{"raw", refine_raw, "#6b7280", true}, {"refined", refined_topo, "#2563eb", false}},
                            .marker_list = std::move(refine_marker_list),
                            .cost_query = refine_query});

  std::vector<PlanarCoord> stress_terminal_list = getHighDegreeTerminalList();
  irt::TBSegmentCostQuery stress_query = getHighDegreeCostMap().getQuery();
  std::vector<Segment<PlanarCoord>> stress_normal = RTTB.getPlanarTopoList(makeTask(stress_terminal_list, stress_query));
  irt::TBRefineStat stress_stat;
  std::vector<Segment<PlanarCoord>> stress_congestion
      = RTTB.getPlanarTopoList(makeTask(stress_terminal_list, stress_query, true), stress_stat);
  plot_case_list.push_back({.file_name = "09_high_degree_stress.svg",
                            .title = "High-degree deterministic stress",
                            .summary = "pins=" + std::to_string(stress_terminal_list.size()) + ", cost="
                                       + formatCost(getTopoCost(stress_congestion, stress_query)) + ", shifted="
                                       + std::to_string(stress_stat.shifted_edge_num) + ", congestion_used="
                                       + std::to_string(stress_stat.used_congestion_flute),
                            .region = region,
                            .terminal_list = stress_terminal_list,
                            .topo_layer_list = {{"normal", stress_normal, "#6b7280", true},
                                                {"selected", stress_congestion, "#7c3aed", false}},
                            .cost_query = stress_query});

  PlanarRect partial_macro(18, 20, 36, 36);
  PlanarCoord macro_steiner(30, 30);
  irt::TBSegmentCostQuery partial_macro_query = GridCostMap(region).getQuery();
  irt::TBRefineStat partial_macro_stat;
  std::vector<Segment<PlanarCoord>> partial_macro_topo
      = RTTB.getPlanarTopoList(makeTask(base_terminal_list, partial_macro_query), partial_macro_stat);
  plot_case_list.push_back({.file_name = "10_partial_layer_macro.svg",
                            .title = "Partial-layer macro keeps Steiner",
                            .summary = "steiner=(" + std::to_string(macro_steiner.get_x()) + "," + std::to_string(macro_steiner.get_y())
                                       + "), finite_escape=1, shifted=" + std::to_string(partial_macro_stat.shifted_edge_num) + ", refined="
                                       + std::to_string(partial_macro_stat.refined_steiner_num),
                            .region = region,
                            .terminal_list = base_terminal_list,
                            .macro_rect_list = {partial_macro},
                            .macro_label = "partial-layer macro",
                            .topo_layer_list = {{"selected", partial_macro_topo, "#0f766e", false}},
                            .cost_query = partial_macro_query});

  PlanarRect full_layer_macro(18, 20, 36, 36);
  PlanarCoord blocked_macro_steiner(30, 30);
  irt::TBSegmentCostQuery macro_ring_query = getMacroRingCostMap(full_layer_macro, 2, 50).getQuery();
  irt::TBRefineStat macro_ring_stat;
  std::vector<Segment<PlanarCoord>> macro_ring_topo
      = RTTB.getPlanarTopoList(makeTask(base_terminal_list, macro_ring_query, true), macro_ring_stat);
  plot_case_list.push_back({.file_name = "11_full_layer_macro_ring.svg",
                            .title = "Full-layer macro with congestion ring",
                            .summary = "attempted=" + std::to_string(macro_ring_stat.attempted_congestion_flute) + ", used="
                                       + std::to_string(macro_ring_stat.used_congestion_flute) + ", shifted="
                                       + std::to_string(macro_ring_stat.shifted_edge_num) + ", cost="
                                       + formatCost(getTopoCost(macro_ring_topo, macro_ring_query)) + ", refined="
                                       + std::to_string(macro_ring_stat.refined_steiner_num),
                            .region = region,
                            .terminal_list = base_terminal_list,
                            .macro_rect_list = {full_layer_macro},
                            .macro_label = "full-layer macro",
                            .topo_layer_list = {{"raw", baseline, "#6b7280", true}, {"selected", macro_ring_topo, "#0f766e", false}},
                            .marker_list = {{blocked_macro_steiner, "raw blocked Steiner", "#dc2626"}},
                            .cost_query = macro_ring_query});

  bool generated = true;
  for (const PlotCase& plot_case : plot_case_list) {
    generated = writePlotSvg(plot_dir, plot_case) && generated;
  }
  if (generated) {
    std::cout << "Generated TOPOBuilder plots in " << std::filesystem::absolute(plot_dir).string() << "\n";
  }
  return generated;
}

}  // namespace

int main(int argc, char* argv[])
{
  PlotOptions options;
  if (!parseOptions(argc, argv, options)) {
    printUsage(argv[0]);
    return 2;
  }
  if (options.show_help) {
    printUsage(argv[0]);
    return 0;
  }
  if (options.plot_dir.has_value() && !preparePlotDirectory(*options.plot_dir)) {
    return 2;
  }

  irt::Logger::initInst();
  irt::TOPOBuilder::initInst();
  RTTB.init();

  bool passed = true;
  passed = checkBaseline() && passed;
  passed = checkCostDrivenShift() && passed;
  passed = checkCongestionFluteGuard() && passed;
  passed = checkCongestionFluteCostGuard() && passed;
  passed = checkCongestionFluteQueryBound() && passed;
  passed = checkAlignedSegmentQueryOnce() && passed;
  passed = checkInfCostPruning() && passed;
  passed = checkCongestionFluteInfHandling() && passed;
  passed = checkThreePinCongestionAvoidsMacro() && passed;
  passed = checkThreePinCongestionOutsidePinBBox() && passed;
  passed = checkNormalDefersBlockedSteiner() && passed;
  passed = checkMultiHotspotCompetition() && passed;
  passed = checkFiniteCorridor() && passed;
  passed = checkHighDegreeSteinerRefine() && passed;
  passed = checkHighDegreeStress() && passed;
  passed = checkPartialLayerMacroKeepsSteiner() && passed;
  passed = checkFullLayerMacroCongestionRing() && passed;
  passed = checkSteinerUsageClassification() && passed;
  if (options.plot_dir.has_value()) {
    passed = generatePlots(*options.plot_dir) && passed;
  }

  RTTB.destroy();
  irt::TOPOBuilder::destroyInst();
  irt::Logger::destroyInst();
  return passed ? 0 : 1;
}
