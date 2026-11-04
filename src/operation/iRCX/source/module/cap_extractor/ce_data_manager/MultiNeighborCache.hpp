// Adapted from the frozen paper manuscript rank3 collector (2026-10-09).
#pragma once
#include <atomic>
#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "RankedNeighbors.hpp"
#include "TopoPool.hpp"
namespace ircx::pattern {
class MultiNeighborCache
{
  struct Entry
  {
    ircx::TopoEdge* edge;
    std::int64_t x0, x1;
  };
  using Point = boost::geometry::model::point<std::int64_t, 2, boost::geometry::cs::cartesian>;
  using Box = boost::geometry::model::box<Point>;
  using SpatialValue = std::pair<Box, std::size_t>;
  using SpatialIndex = boost::geometry::index::rtree<SpatialValue, boost::geometry::index::quadratic<16>>;
  struct LayerIndex
  {
    std::vector<std::size_t> ids;
    double max_half_width = 0;
    SpatialIndex spatial;
  };
  std::vector<Entry> entries_;
  std::unordered_map<ircx::TopoEdge*, std::size_t> entry_ids_;
  std::atomic<std::int64_t> spans_{0}, total_dbu_{0}, duplicate_dbu_{0}, overlap_spans_{0}, overlap_dbu_{0}, field_fallback_spans_{0}, field_fallback_dbu_{0};
  std::atomic<std::int64_t> grid_dbu_{0}, grid_width_clamped_dbu_{0}, grid_gap_clamped_dbu_{0};
  std::map<std::pair<int32_t, bool>, LayerIndex> index_;
  std::unordered_map<std::string, std::vector<double>> caps_;
  std::set<std::string> requests_;
  std::mutex mutex_;
  bool active_ = false, collect_ = false, grid_ = false;
  int rank_ = 3;
  static std::size_t owner(const ircx::TopoEdge& e) { return (std::size_t(e.get_is_special_net()) << 32) | std::uint32_t(e.get_net_idx()); }
  static std::filesystem::path outputPath(const char* name)
  {
    const char* dir = std::getenv("IRCX_MULTI_OUTPUT");
    return std::filesystem::path(dir ? dir : ".") / name;
  }

 public:
  struct Piece
  {
    std::int64_t lo, hi;
    std::vector<ircx::TopoEdge*> neighbors;
    std::string key;
    std::size_t nleft = 0;
  };
  explicit MultiNeighborCache(ircx::TopoPool& pool)
  {
    const char* mode = std::getenv("IRCX_MULTI_MODE");
    if (!mode || std::string(mode) == "off")
      return;
    active_ = true;
    collect_ = std::string(mode) == "collect";
    grid_ = std::string(mode) == "grid";
    if (!collect_ && !grid_ && std::string(mode) != "apply")
      throw std::runtime_error("invalid multi mode");
    const char* rank = std::getenv("IRCX_MULTI_RANK");
    if (rank)
      rank_ = std::stoi(rank);
    if (grid_ && rank_ != 1) {
      throw std::runtime_error("grid mode requires rank 1");
    }
    if (rank_ < 1 || rank_ > 3)
      throw std::runtime_error("invalid neighbor rank");
    auto add = [&](auto& edge) {
      if (edge.get_is_via())
        return;
      auto& r = edge.get_shape();
      auto a = edge.get_line_segment().get_is_horizontal() ? boost::polygon::yl(r) : boost::polygon::xl(r);
      auto b = edge.get_line_segment().get_is_horizontal() ? boost::polygon::yh(r) : boost::polygon::xh(r);
      auto& index = index_[{edge.get_layer_idx(), edge.get_line_segment().get_is_horizontal()}];
      index.max_half_width = std::max(index.max_half_width, double(b - a) / 2);
      index.ids.push_back(entries_.size());
      entry_ids_[&edge] = entries_.size();
      entries_.push_back({&edge, a, b});
    };
    for (auto& e : pool.get_edge_pool()) {
      add(e);
    }
    for (auto& e : pool.get_special_edge_pool()) {
      add(e);
    }
    for (auto& [key, index] : index_)
      std::sort(index.ids.begin(), index.ids.end(),
                [&](auto a, auto b) { return entries_[a].edge->get_line_segment().get_coord() < entries_[b].edge->get_line_segment().get_coord(); });

    for (auto& [key, index] : index_) {
      std::vector<SpatialValue> values;
      values.reserve(index.ids.size());
      for (auto id : index.ids) {
        auto& entry = entries_[id];
        const auto& line = entry.edge->get_line_segment();
        values.push_back({Box(Point(line.get_lower(), entry.x0), Point(line.get_upper(), entry.x1)), id});
      }
      index.spatial = SpatialIndex(values.begin(), values.end());
    }
    if (!collect_ && !grid_) {
      const char* path = std::getenv("IRCX_MULTI_TABLE");
      if (!path)
        throw std::runtime_error("missing multi table");
      std::ifstream in(path);
      if (!in)
        throw std::runtime_error("cannot open multi table");
      for (std::string line; std::getline(in, line);) {
        auto tab = line.find('\t');
        if (tab == std::string::npos)
          throw std::runtime_error("invalid multi row");
        std::istringstream s(line.substr(tab + 1));
        std::vector<double> c;
        double v;
        while (s >> v)
          c.push_back(v);
        if ((!s.eof() || c.empty()) && line.substr(tab + 1) != "F")
          throw std::runtime_error("empty multi coefficients");
        for (double x : c)
          if (!std::isfinite(x) || x < 0)
            throw std::runtime_error("invalid multi coefficient");
        if (!caps_.emplace(line.substr(0, tab), std::move(c)).second)
          throw std::runtime_error("duplicate multi key");
      }
    }
  }
  ~MultiNeighborCache()
  {
    if (!active_)
      return;
    if (collect_) {
      std::ofstream out(outputPath("multi_requests.txt"));
      for (const auto& k : requests_)
        out << k << '\n';
      if (!out)
        std::abort();
    }
    std::ofstream out(outputPath("multi_stats.txt"));
    out << "spans " << spans_ << "\ntotal_dbu " << total_dbu_ << "\nduplicate_dbu " << duplicate_dbu_ << "\noverlap_spans " << overlap_spans_
        << "\noverlap_dbu " << overlap_dbu_ << "\nfield_fallback_spans " << field_fallback_spans_ << "\nfield_fallback_dbu " << field_fallback_dbu_
        << "\ngrid_dbu " << grid_dbu_ << "\ngrid_width_clamped_dbu " << grid_width_clamped_dbu_ << "\ngrid_gap_clamped_dbu " << grid_gap_clamped_dbu_ << '\n';
  }
  void recordGridCoverage(std::int64_t length, bool width_clamped, bool gap_clamped)
  {
    grid_dbu_ += length;
    if (width_clamped)
      grid_width_clamped_dbu_ += length;
    if (gap_clamped)
      grid_gap_clamped_dbu_ += length;
  }
  void recordFieldFallback(std::int64_t length)
  {
    ++field_fallback_spans_;
    field_fallback_dbu_ += length;
  }
  bool active() const { return active_; }
  bool collecting() const { return collect_; }
  bool gridded() const { return grid_; }
  const std::vector<double>& caps(const std::string& key) const
  {
    static const std::vector<double> zero{0.}, fallback{};
    if (key == "__ZERO")
      return zero;
    if (key == "__FALLBACK")
      return fallback;
    auto it = caps_.find(key);
    if (it == caps_.end())
      throw std::runtime_error("missing multi geometry: " + key);
    return it->second;
  }
  std::vector<Piece> pieces(ircx::TopoEdge& master, std::int64_t lo, std::int64_t hi, double micron_per_dbu, const std::string& layer, const std::string& below,
                            const std::string& above)
  {
    ++spans_;
    total_dbu_ += hi - lo;
    auto& r = master.get_shape();
    const auto mx0 = master.get_line_segment().get_is_horizontal() ? boost::polygon::yl(r) : boost::polygon::xl(r),
               mx1 = master.get_line_segment().get_is_horizontal() ? boost::polygon::yh(r) : boost::polygon::xh(r);
    const auto& index = index_.at({master.get_layer_idx(), master.get_line_segment().get_is_horizontal()});
    const double range = 8. / micron_per_dbu;
    std::vector<SpatialValue> candidates;
    index.spatial.query(boost::geometry::index::intersects(Box(Point(lo, mx0 - range), Point(hi, mx1 + range))), std::back_inserter(candidates));
    std::vector<RankedItem> sides[2];
    std::vector<std::pair<std::int64_t, std::int64_t>> masks;
    const auto master_id = entry_ids_.at(&master);
    bool overlap = false;
    for (const auto& candidate : candidates) {
      const auto id = candidate.second;
      const auto& e = entries_[id];
      if (e.edge == &master || e.edge->get_line_segment().get_lower() >= hi || e.edge->get_line_segment().get_upper() <= lo)
        continue;
      int side = e.x1 <= mx0 ? 0 : (e.x0 >= mx1 ? 1 : -1);
      if (side < 0) {
        if (owner(*e.edge) == owner(master) && e.x0 == mx0 && e.x1 == mx1) {
          if (id < master_id)
            masks.emplace_back(std::max<std::int64_t>(lo, e.edge->get_line_segment().get_lower()),
                               std::min<std::int64_t>(hi, e.edge->get_line_segment().get_upper()));
        } else
          overlap = true;
        continue;
      }
      double gap = side == 0 ? mx0 - e.x1 : e.x0 - mx1;
      if (gap > range)
        continue;
      sides[side].push_back({e.edge->get_line_segment().get_lower(), e.edge->get_line_segment().get_upper(), id, owner(*e.edge), e.x0, e.x1});
    }
    if (overlap) {
      ++overlap_spans_;
      overlap_dbu_ += hi - lo;
    }
    std::sort(sides[0].begin(), sides[0].end(), [](auto a, auto b) { return a.x1 != b.x1 ? a.x1 > b.x1 : a.id < b.id; });
    std::sort(sides[1].begin(), sides[1].end(), [](auto a, auto b) { return a.x0 != b.x0 ? a.x0 < b.x0 : a.id < b.id; });
    auto left = rankedRuns(lo, hi, sides[0], rank_), right = rankedRuns(lo, hi, sides[1], rank_);
    std::vector<Piece> out;
    std::size_t i = 0, j = 0;
    while (i < left.size() && j < right.size()) {
      auto a = std::max(left[i].lo, right[j].lo), b = std::min(left[i].hi, right[j].hi);
      if (a < b) {
        Piece piece{a, b, {}, {}};
        std::ostringstream key;
        if (!grid_)
          key << layer << ' ' << below << ' ' << (above.empty() ? "-" : above) << ' ' << 2 * std::llround(1. / micron_per_dbu) << ' ' << 2 * (mx1 - mx0) << ' '
              << left[i].ids.size() << ' ' << right[j].ids.size();
        for (const auto& ids : {left[i].ids, right[j].ids})
          for (auto id : ids) {
            const auto& e = entries_[id];
            if (!grid_)
              key << ' ' << 2 * e.x0 - (std::int64_t(mx0) + mx1) << ' ' << 2 * e.x1 - (std::int64_t(mx0) + mx1);
            piece.neighbors.push_back(e.edge);
          }
        piece.nleft = left[i].ids.size();
        piece.key = overlap ? "__FALLBACK" : (grid_ ? "__GRID" : key.str());
        out.push_back(std::move(piece));
      }
      auto end = std::min(left[i].hi, right[j].hi);
      if (left[i].hi == end)
        ++i;
      if (right[j].hi == end)
        ++j;
    }
    if (!masks.empty()) {
      std::sort(masks.begin(), masks.end());
      std::vector<std::pair<std::int64_t, std::int64_t>> merged;
      for (auto m : masks) {
        if (!merged.empty() && m.first <= merged.back().second)
          merged.back().second = std::max(merged.back().second, m.second);
        else
          merged.push_back(m);
      }
      for (auto m : merged)
        duplicate_dbu_ += m.second - m.first;
      std::vector<Piece> owned;
      for (const auto& part : out) {
        auto cursor = part.lo;
        for (auto m : merged) {
          auto a = std::max(cursor, m.first), b = std::min(part.hi, m.second);
          if (a >= b)
            continue;
          if (cursor < a) {
            auto copy = part;
            copy.lo = cursor;
            copy.hi = a;
            owned.push_back(copy);
          }
          owned.push_back({a, b, {}, "__ZERO"});
          cursor = b;
        }
        if (cursor < part.hi) {
          auto copy = part;
          copy.lo = cursor;
          owned.push_back(copy);
        }
      }
      out = std::move(owned);
    }
    if (collect_) {
      std::lock_guard<std::mutex> lock(mutex_);
      for (const auto& piece : out)
        if (piece.key != "__ZERO" && piece.key != "__FALLBACK")
          requests_.insert(piece.key);
    }
    return out;
  }
};
}  // namespace ircx::pattern
