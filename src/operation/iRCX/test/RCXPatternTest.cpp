#include <cassert>
#include <cstdlib>
#include <fstream>
#include <random>

#include "GridNeighborhood.hpp"
#include "PatternCrossLayers.hpp"
#include "GridSides.hpp"
#include "grid/PatternGrid.hpp"
#include "ReciprocalCap.hpp"
using namespace ircx;
TopoEdge edge(int net, int x0, int y0, int x1, int y1)
{
  TopoEdge e(net);
  e.set_layer_idx(1);
  e.set_shape(GTLRectInt(x0, y0, x1, y1));
  return e;
}
int main()
{
  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
  std::istringstream grid_data("P M1 SUBSTRATE - 1\n-1 -1 8 0 0\n-1 1 5 0 3\n-1 3 6 0 2\n1 -1 5 3 0\n1 1 2 3 3\n1 3 3 3 2\n3 -1 6 2 0\n3 1 3 2 3\n3 3 4 2 2\n");
  pattern::PatternGrid grid;
  grid.read(grid_data);
  auto isolated = grid.query("M1", "SUBSTRATE", "", 1, -1, -1);
  assert(isolated.ground == 8 && isolated.left == 0 && isolated.right == 0);
  auto asymmetric = grid.query("M1", "SUBSTRATE", "", 1, 2, 1);
  assert(asymmetric.ground == 2.5 && asymmetric.left == 2.5 && asymmetric.right == 3);
  auto single = grid.query("M1", "SUBSTRATE", "", 1, -1, 2);
  assert(single.ground == 5.5 && single.left == 0 && single.right == 2.5);
  auto clamped = grid.query("M1", "SUBSTRATE", "", 2, -1, 4);
  assert(clamped.width_clamped && clamped.gap_clamped && clamped.ground == 6 && clamped.right == 2);
  assert(!isolated.width_clamped && !isolated.gap_clamped);
  std::string grid_error;
  auto missing_grid = grid.queryChecked("missing", "SUBSTRATE", "", 1, -1, -1, grid_error);
  assert(!missing_grid && !grid_error.empty());
  auto invalid_coordinate = grid.queryChecked("M1", "SUBSTRATE", "", std::numeric_limits<double>::quiet_NaN(), -1, -1, grid_error);
  assert(!invalid_coordinate && !grid_error.empty());
  auto valid_grid = grid.queryChecked("M1", "SUBSTRATE", "", 1, -1, -1, grid_error);
  assert(valid_grid && grid_error.empty() && valid_grid->ground == isolated.ground);
  for (const char* text : {"P M1 SUBSTRATE - 1 extra\n",
                           "IRCX_PATTERN_GRID_V2\nP M2 M1 M3 1\n"
                           "-1 -1 1 0 0 2 3\n-1 1 1 0 4 2 3\n1 -1 1 4 0 2 3\n1 1 1 4 4 2 3\n",
                           "P M1 SUBSTRATE - 1\n-1 -1 1 0 0 0\n",
                           "P M1 SUBSTRATE - 1\n-1 -1 1 0 0 0 1 2\n",
                           "P M1 SUBSTRATE - 1\n-1 -1 1 0 0 2 0\n"}) {
    std::istringstream bad_header(text);
    bool header_rejected = false;
    try {
      pattern::PatternGrid invalid;
      invalid.read(bad_header);
    } catch (const std::runtime_error&) {
      header_rejected = true;
    }
    assert(header_rejected);
  }
  CrossLayerOverlap near, far;
  near.set_start_coord(0);
  near.set_end_coord(100);
  near.set_below_layer_idx(2);
  near.set_above_layer_idx(6);
  far.set_start_coord(0);
  far.set_end_coord(100);
  far.set_below_layer_idx(1);
  far.set_above_layer_idx(8);
  const auto layers = pattern::crossLayers({near, far}, 0, 100);
  assert(layers.first == 2 && layers.second == 6);
  assert(pattern::crossLayers({near}, 0, 101).second == -1);
  TopoPool pool;
  pool.add_net({}, {edge(0, 0, 0, 1000, 101)});
  pool.add_net({}, {edge(1, 200, 201, 800, 301)});
  pool.add_net({}, {edge(2, 0, 401, 1000, 501)});
  pool.add_net({}, {edge(3, 200, 601, 800, 701)});
  pool.add_net({}, {edge(4, 0, 801, 1000, 901)});
  pattern::GridNeighborhood cache(pool);
  auto p = cache.pieces(pool.get_edge(0), 0, 1000, .001);
  assert(p.size() == 3 && p[0].lo == 0 && p[0].hi == 200 && p[1].hi == 800 && p[2].hi == 1000);
  assert(p[0].neighbors == std::vector<TopoEdge*>{&pool.get_edge(2)});
  assert(p[1].neighbors == std::vector<TopoEdge*>{&pool.get_edge(1)});
  assert(p[2].neighbors == p[0].neighbors);
  TopoPool duplicate;
  duplicate.add_net({}, {edge(0, 0, 400, 1000, 500)});
  duplicate.add_net({}, {edge(1, 0, 200, 600, 300), edge(1, 400, 200, 1000, 300)});
  pattern::GridNeighborhood canonical(duplicate);
  long owned_length = 0, masked = 0;
  for (int i = 1; i <= 2; ++i)
    for (auto& part : canonical.pieces(duplicate.get_edge(i), i == 1 ? 0 : 400, i == 1 ? 600 : 1000, .001)) {
      if (part.duplicate)
        masked += part.hi - part.lo;
      else
        owned_length += part.hi - part.lo;
    }
  assert(owned_length == 1000 && masked == 200);
  auto owned = canonical.pieces(duplicate.get_edge(0), 0, 1000, .001);
  auto sides = pattern::gridSides(duplicate.get_edge(0), owned.front(), .001);
  assert(sides.lower == &duplicate.get_edge(1) && sides.upper == nullptr);
  assert(sides.lower_spacing == .1);
  TopoPool mixed;
  mixed.add_net({}, {edge(0, 0, 200, 1000, 300), edge(0, 0, 400, 1000, 500)});
  mixed.add_special_edge_list({edge(0, 0, 0, 1000, 100)});
  pattern::GridNeighborhood mixed_cache(mixed);
  const auto mixed_parts = mixed_cache.pieces(mixed.get_edge(0), 0, 1000, .001);
  const auto mixed_sides = pattern::gridSides(mixed.get_edge(0), mixed_parts.front(), .001);
  assert(mixed_sides.lower == &mixed.get_special_edge_pool()[0]);
  assert(mixed_sides.upper == &mixed.get_edge(1));
  // Compare the spatial-index selection with independently enumerated slices.
  std::mt19937 random(17);
  for (int trial = 0; trial < 30; ++trial) {
    TopoPool sample;
    sample.add_net({}, {edge(0, 0, 0, 1000, 101)});
    std::vector<int> boundaries{0, 1000};
    for (int i = 1; i <= 12; ++i) {
      int lo = int(random() % 800), hi = std::min(1000, lo + 101 + int(random() % 150));
      int y = i <= 6 ? -200 * i : 200 * (i - 6);
      sample.add_net({}, {edge(i, lo, y, hi, y + 101)});
      boundaries.push_back(lo);
      boundaries.push_back(hi);
    }
    pattern::GridNeighborhood spatial(sample);
    auto actual = spatial.pieces(sample.get_edge(0), 0, 1000, .001);
    std::sort(boundaries.begin(), boundaries.end());
    for (std::size_t i = 1; i < boundaries.size(); ++i) {
      const double midpoint = (boundaries[i - 1] + boundaries[i]) / 2.;
      if (boundaries[i - 1] == boundaries[i])
        continue;
      std::vector<std::pair<int, TopoEdge*>> left, right;
      for (auto& e : sample.get_edge_pool()) {
        auto& line = e.get_line_segment();
        if (e.get_net_idx() == 0 || line.get_lower() > midpoint || line.get_upper() <= midpoint)
          continue;
        int y = boost::polygon::yl(e.get_shape());
        if (y < 0)
          left.emplace_back(-y - 101, &e);
        else
          right.emplace_back(y - 101, &e);
      }
      std::sort(left.begin(), left.end());
      std::sort(right.begin(), right.end());
      std::vector<TopoEdge*> want;
      for (std::size_t n = 0; n < std::min<std::size_t>(1, left.size()); ++n)
        want.push_back(left[n].second);
      for (std::size_t n = 0; n < std::min<std::size_t>(1, right.size()); ++n)
        want.push_back(right[n].second);
      auto run = std::find_if(actual.begin(), actual.end(), [&](auto& r) { return r.lo <= midpoint && midpoint < r.hi; });
      assert(run != actual.end() && run->neighbors == want);
    }
  }
  pattern::ReciprocalCap reciprocal;
  reciprocal.add(true, 0, 10, 20);
  reciprocal.add(false, 5, 15, 40);
  assert(reciprocal.value() == 45);  // 5*2 + 5*(2+4)/2 + 5*4
  pattern::ReciprocalCap zero;
  zero.add(true, 0, 10, 20);
  zero.add(false, 0, 10, 0);
  assert(zero.value() == 10);
  // Optional directional fields divide the aggregate ground capacitance.
  std::istringstream directional(" \tP M2 M1 M3 1 \r\n"
                                "-1 -1 8 0 0 3 5\n-1 1 8 0 2 3 5\n"
                                "1 -1 8 2 0 3 5\n1 1 8 2 2 3 5\n"
                                "P M2 M1 M3 3\n-1 -1 16 0 0 6 10\n-1 1 16 0 4 6 10\n"
                                "1 -1 16 4 0 6 10\n1 1 16 4 4 6 10\n");
  grid.read(directional);
  const auto sided = grid.query("M2", "M1", "M3", 1, -1, 1);
  assert(sided.ground == 8 && sided.ground_low == 3 && sided.ground_high == 5);
  assert(sided.left == 0 && sided.right == 2);
  const auto interpolated = grid.query("M2", "M1", "M3", 2, -1, -1);
  assert(interpolated.ground == 12 && interpolated.ground_low == 4.5 && interpolated.ground_high == 7.5);
  assert(interpolated.left == 0 && interpolated.right == 0);
  std::istringstream aggregate("P M1 SUBSTRATE - 1\n-1 -1 8 0 0\n-1 1 5 0 3\n1 -1 5 3 0\n1 1 2 3 3\n");
  grid.read(aggregate);
  const auto reloaded = grid.query("M1", "SUBSTRATE", "", 1, -1, -1);
  assert(reloaded.ground == 8 && reloaded.ground_low == 4 && reloaded.ground_high == 4);
}
