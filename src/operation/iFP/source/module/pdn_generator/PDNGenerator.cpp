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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "PDNGenerator.hpp"
#include <array>
#include <cmath>
#include <fnmatch.h>
#include <queue>

#include "DataManager.hpp"
#include "FPInterface.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

namespace ifp {

namespace {

// A small per-layer spatial index keeps pin-access checks independent of mesh size.
class PGGeometryIndex
{
 public:
  explicit PGGeometryIndex(int32_t bin_size) : _bin_size(std::max(1, bin_size)) {}

  void add(const std::string& layer, const PlanarRect& rect, int32_t net, int32_t owner = -1)
  {
    visit(rect, [&](int32_t x, int32_t y) { _layers[layer][key(x, y)].push_back({rect, net, owner}); });
  }

  std::set<int32_t> touching(const std::string& layer, const PlanarRect& rect) const
  {
    std::set<int32_t> result;
    auto layer_it = _layers.find(layer);
    if (layer_it == _layers.end()) {
      return result;
    }
    visit(rect, [&](int32_t x, int32_t y) {
      auto bin = layer_it->second.find(key(x, y));
      if (bin == layer_it->second.end()) {
        return;
      }
      for (const auto& obstacle : bin->second) {
        const auto& box = obstacle.rect;
        const int32_t dx = std::min(rect.get_ur_x(), box.get_ur_x()) - std::max(rect.get_ll_x(), box.get_ll_x());
        const int32_t dy = std::min(rect.get_ur_y(), box.get_ur_y()) - std::max(rect.get_ll_y(), box.get_ll_y());
        if (dx >= 0 && dy >= 0 && (dx > 0 || dy > 0)) {
          result.insert(obstacle.net);
        }
      }
    });
    return result;
  }

  bool clear(const std::string& layer, const PlanarRect& rect, int32_t net, int32_t spacing,
             const PlanarRect* shared_contact = nullptr, int32_t ignore_owner = -1) const
  {
    auto layer_it = _layers.find(layer);
    if (layer_it == _layers.end()) {
      return true;
    }
    PlanarRect query(rect.get_ll_x() - spacing, rect.get_ll_y() - spacing, rect.get_ur_x() + spacing, rect.get_ur_y() + spacing);
    bool clear = true;
    visit(query, [&](int32_t x, int32_t y) {
      if (!clear) {
        return;
      }
      auto bin = layer_it->second.find(key(x, y));
      if (bin == layer_it->second.end()) {
        return;
      }
      for (const auto& obstacle : bin->second) {
        const auto& box = obstacle.rect;
        // Only the owning macro's OBS is ignored (the pin is the landing
        // metal); its foreign-net pins still block.  owner == -1 means "no
        // owner", so it must never match the default ignore_owner == -1.
        const bool ignored_obstruction = ignore_owner >= 0 && obstacle.owner == ignore_owner && obstacle.net < 0;
        if (obstacle.net != net && !ignored_obstruction && query.get_ll_x() < box.get_ur_x() && box.get_ll_x() < query.get_ur_x()
            && query.get_ll_y() < box.get_ur_y() && box.get_ll_y() < query.get_ur_y()) {
          // Independent IO paths may meet inside their destination stripe.
          // An overlapping trunk outside that stripe is still rejected.
          if (shared_contact) {
            auto outside_contact = [&](const PlanarRect& r) {
              const int32_t lx = std::max(r.get_ll_x(), shared_contact->get_ll_x());
              const int32_t ly = std::max(r.get_ll_y(), shared_contact->get_ll_y());
              const int32_t ux = std::min(r.get_ur_x(), shared_contact->get_ur_x());
              const int32_t uy = std::min(r.get_ur_y(), shared_contact->get_ur_y());
              if (lx >= ux || ly >= uy) return std::vector<PlanarRect>{r};
              std::vector<PlanarRect> pieces;
              if (r.get_ll_x() < lx) pieces.emplace_back(r.get_ll_x(), r.get_ll_y(), lx, r.get_ur_y());
              if (ux < r.get_ur_x()) pieces.emplace_back(ux, r.get_ll_y(), r.get_ur_x(), r.get_ur_y());
              if (r.get_ll_y() < ly) pieces.emplace_back(lx, r.get_ll_y(), ux, ly);
              if (uy < r.get_ur_y()) pieces.emplace_back(lx, uy, ux, r.get_ur_y());
              return pieces;
            };
            bool separated = true;
            for (const auto& a : outside_contact(rect)) {
              for (const auto& b : outside_contact(box)) {
                if (a.get_ll_x() - spacing < b.get_ur_x() && b.get_ll_x() < a.get_ur_x() + spacing
                    && a.get_ll_y() - spacing < b.get_ur_y() && b.get_ll_y() < a.get_ur_y() + spacing) separated = false;
              }
            }
            if (separated) continue;
          }
          clear = false;
          break;
        }
      }
    });
    return clear;
  }

 private:
  struct Obstacle { PlanarRect rect; int32_t net; int32_t owner = -1; };
  static uint64_t key(int32_t x, int32_t y) { return (uint64_t(uint32_t(x)) << 32) | uint32_t(y); }
  int32_t bin(int32_t coord) const { return coord >= 0 ? coord / _bin_size : (coord + 1) / _bin_size - 1; }
  template <typename Fn> void visit(const PlanarRect& rect, Fn fn) const
  {
    for (int32_t x = bin(rect.get_ll_x()); x <= bin(rect.get_ur_x()); ++x) {
      for (int32_t y = bin(rect.get_ll_y()); y <= bin(rect.get_ur_y()); ++y) {
        fn(x, y);
      }
    }
  }
  int32_t _bin_size;
  std::map<std::string, std::unordered_map<uint64_t, std::vector<Obstacle>>> _layers;
};

PlanarRect segmentRect(const PGSegment& segment)
{
  return {segment.get_ll_x(), segment.get_ll_y(), segment.get_ur_x(), segment.get_ur_y()};
}

// segmentRect() expands both axes by half the wire width, which also extends
// an axis-aligned wire past its endpoints.  Clearance checks need the real
// wire body: the width applies only perpendicular to the wire.
PlanarRect wireRect(const PGSegment& segment)
{
  if (segment.is_horizontal()) {
    return {std::min(segment.get_start_x(), segment.get_end_x()), segment.get_start_y() - segment.get_width() / 2,
            std::max(segment.get_start_x(), segment.get_end_x()), segment.get_start_y() + segment.get_width() / 2};
  }
  if (segment.is_vertical()) {
    return {segment.get_start_x() - segment.get_width() / 2, std::min(segment.get_start_y(), segment.get_end_y()),
            segment.get_start_x() + segment.get_width() / 2, std::max(segment.get_start_y(), segment.get_end_y())};
  }
  return segmentRect(segment);
}

// Shared by the macro and IO-core geometry indexes: a line uses the real wire
// body, a via contributes every layer shape of its stack.
int32_t addPGSegmentGeometry(PGGeometryIndex& geometry, const Database& database, PGSegment& segment)
{
  auto net_iter = database.get_pg_net_name_to_idx_map().find(segment.get_net_name());
  if (net_iter == database.get_pg_net_name_to_idx_map().end()) {
    return -1;
  }
  const int32_t net = net_iter->second;
  if (segment.is_line()) {
    geometry.add(segment.get_layer_name(), wireRect(segment), net);
  } else if (segment.get_type() == PGSegmentType::kVia) {
    for (const auto& shape : FPI.getPGViaShapes(segment)) {
      geometry.add(shape.get_layer_name(), shape, net);
    }
  }
  return net;
}

struct ManhattanPathCandidate
{
  PlanarCoord bend;
  bool horizontal_first = true;
};

std::array<ManhattanPathCandidate, 2> makeManhattanPathCandidates(const PlanarCoord& start, const PlanarCoord& end)
{
  return {{{PlanarCoord(end.get_x(), start.get_y()), true}, {PlanarCoord(start.get_x(), end.get_y()), false}}};
}

const PlanarRect& getMacroPGBlockageRect(const Instance& instance)
{
  return instance.get_placement_halo_rect();
}

}  // namespace

// public

void PDNGenerator::initInst()
{
  if (_pg_instance == nullptr) {
    _pg_instance = new PDNGenerator();
  }
}

PDNGenerator& PDNGenerator::getInst()
{
  if (_pg_instance == nullptr) {
    FPLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_pg_instance;
}

void PDNGenerator::destroyInst()
{
  if (_pg_instance != nullptr) {
    delete _pg_instance;
    _pg_instance = nullptr;
  }
}

// function

void PDNGenerator::generate()
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  _placed_macro_list.clear();
  _macro_top_layer_order.clear();
  _rejected_via_count = 0;
  _grown_via_counts.clear();
  for (Instance& instance : FPDM.getDatabase().get_instance_list()) {
    if (instance.get_macro() && instance.get_placed()) {
      _placed_macro_list.push_back(&instance);
    }
  }

  PGModel pg_model;
  generatePDN(pg_model);
  if (_rejected_via_count != 0) {
    FPLOG.warn(Loc::current(), "Rejected ", _rejected_via_count, " PG via candidates without a complete legal stack.");
  }
  for (const auto& [layers, count] : _grown_via_counts) {
    FPLOG.warn(Loc::current(), "Enlarged ", count, " PG via(s) between ", layers.first, " and ", layers.second,
               " beyond the stripe overlap; check the stripe width against the via rule.");
  }
  outputPowerLocation(pg_model);

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PDNGenerator::generatePDN(PGModel& pg_model)
{
  buildPGNet(pg_model);
  buildRail(pg_model);
  buildStripe(pg_model);
  addUncoveredEdgeStripes(pg_model);
  alignStripeSegmentList();
  buildRing(pg_model);
  initPowerLayerPair(pg_model);
  pruneDeadEndStripes();
  buildLayerConnect(pg_model);
  buildMacroConnect(pg_model);
  trimStripeEnds();
  // Route IO-core connections against the final stripe geometry.  The trim
  // pass may move stripe endpoints to preserve macro and PG contacts.
  buildIOCoreConnect(pg_model);
  mergeIoWireSegmentList();
  repairPGConnectivity(pg_model);
  mergeOverlappingViaStacks();
  trimDanglingFollowPins();
  validatePGConnectivity(pg_model);
}

void PDNGenerator::pruneDeadEndStripes()
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");
  auto& database = FPDM.getDatabase();
  auto& segments = database.get_pg_segment_list();
  if (segments.empty()) {
    FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
    return;
  }

  auto overlaps = [](const PlanarRect& first, const PlanarRect& second) {
    return first.get_ll_x() <= second.get_ur_x() && second.get_ll_x() <= first.get_ur_x()
           && first.get_ll_y() <= second.get_ur_y() && second.get_ll_y() <= first.get_ur_y();
  };

  // PG pin shapes of placed macros/pads.  Standard-cell PG pins are on the
  // lower layers and are irrelevant to a MET4/T4M2/RDL stripe.
  std::map<std::string, std::vector<InstancePinShape>> pin_shapes_by_layer;
  for (auto& instance : database.get_instance_list()) {
    if (!instance.get_placed() || instance.is_delete_instance()) {
      continue;
    }
    auto master_it = database.get_cell_master_map().find(instance.get_master_name());
    if (master_it == database.get_cell_master_map().end()) {
      continue;
    }
    const CellMaster& master = master_it->second;
    if (!instance.get_macro() && !master.get_pad() && !master.get_pad_filler() && !master.get_corner()) {
      continue;
    }
    for (const auto& shape : FPI.getPlacedShapes(instance, master.get_pg_pin_shapes())) {
      pin_shapes_by_layer[shape.get_layer_name()].push_back(shape);
    }
  }

  std::map<std::pair<std::string, std::string>, std::vector<size_t>> lines_by_net_layer;
  for (size_t index = 0; index < segments.size(); ++index) {
    if (segments[index].is_line()) {
      lines_by_net_layer[{segments[index].get_net_name(), segments[index].get_layer_name()}].push_back(index);
    }
  }

  const int32_t dbu = std::max(1, database.get_micron_dbu());
  const int64_t max_dead_end_length = static_cast<int64_t>(10) * dbu;
  std::vector<bool> remove(segments.size(), false);
  std::ofstream report;
  const Config& config = FPDM.getConfig();
  if (!config.pg_temp_directory_path.empty()) {
    report.open(config.pg_temp_directory_path + "/dead_end_stripes.rpt");
    report << "net layer direction fixed_coord begin end length\n";
  }

  int32_t pruned = 0;
  for (size_t index = 0; index < segments.size(); ++index) {
    const PGSegment& stripe = segments[index];
    if (stripe.get_type() != PGSegmentType::kStripe) {
      continue;
    }
    const bool horizontal = stripe.is_horizontal();
    const int32_t begin = horizontal ? stripe.get_start_x() : stripe.get_start_y();
    const int32_t end = horizontal ? stripe.get_end_x() : stripe.get_end_y();
    const int64_t length = std::abs(static_cast<int64_t>(end) - begin);
    if (length >= max_dead_end_length) {
      continue;
    }

    const PlanarRect stripe_rect = wireRect(stripe);
    bool has_contact = false;
    auto pin_iter = pin_shapes_by_layer.find(stripe.get_layer_name());
    if (pin_iter != pin_shapes_by_layer.end()) {
      for (const auto& shape : pin_iter->second) {
        if (overlaps(stripe_rect, shape)) {
          has_contact = true;
          break;
        }
      }
    }
    if (!has_contact) {
      auto line_iter = lines_by_net_layer.find({stripe.get_net_name(), stripe.get_layer_name()});
      if (line_iter != lines_by_net_layer.end()) {
        for (size_t other_index : line_iter->second) {
          if (other_index == index) {
            continue;
          }
          if (overlaps(stripe_rect, wireRect(segments[other_index]))) {
            has_contact = true;
            break;
          }
        }
      }
    }
    if (!has_contact) {
      for (const auto& layer_pair : config.pg_layer_pair_list) {
        std::string other_layer;
        if (layer_pair.get_first_layer_name() == stripe.get_layer_name()) {
          other_layer = layer_pair.get_second_layer_name();
        } else if (layer_pair.get_second_layer_name() == stripe.get_layer_name()) {
          other_layer = layer_pair.get_first_layer_name();
        } else {
          continue;
        }
        auto other_iter = lines_by_net_layer.find({stripe.get_net_name(), other_layer});
        if (other_iter == lines_by_net_layer.end()) {
          continue;
        }
        for (size_t other_index : other_iter->second) {
          if (overlaps(stripe_rect, wireRect(segments[other_index]))) {
            has_contact = true;
            break;
          }
        }
        if (has_contact) {
          break;
        }
      }
    }
    if (has_contact) {
      continue;
    }

    remove[index] = true;
    ++pruned;
    if (report) {
      report << stripe.get_net_name() << ' ' << stripe.get_layer_name() << ' ' << (horizontal ? "H" : "V") << ' '
             << (horizontal ? stripe.get_start_y() : stripe.get_start_x()) << ' ' << begin << ' ' << end << ' ' << length << '\n';
    }
  }

  if (pruned == 0) {
    FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
    return;
  }
  std::vector<PGSegment> kept;
  kept.reserve(segments.size() - pruned);
  for (size_t index = 0; index < segments.size(); ++index) {
    if (!remove[index]) {
      kept.push_back(std::move(segments[index]));
    }
  }
  segments = std::move(kept);
  FPLOG.info(Loc::current(), "Pruned ", pruned, " dead-end stripe segment(s) without a pin or same-layer connection.");
  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PDNGenerator::trimStripeEnds()
{
  auto& database = FPDM.getDatabase();
  auto& segments = database.get_pg_segment_list();
  struct Candidate {
    size_t index;
    bool start = false;
    bool end = false;
    int32_t first_contact = INT32_MAX;
    int32_t last_contact = INT32_MIN;
  };
  std::vector<Candidate> candidates;
  using Key = std::tuple<std::string, std::string, bool>;
  std::map<Key, std::map<int32_t, std::vector<size_t>>> by_coord;
  int32_t max_half_width = 0;
  // Keep core-boundary stripe ends reachable for IO power cells.  Otherwise
  // trimStripeEnds can move the end to a macro pin via, and the IO-core
  // connector is then forced to land on the macro edge.
  const Config& config = FPDM.getConfig();
  const bool keep_io_core_ends = config.pg_io_core_connect.enabled;
  struct IoExtent {
    int32_t low;
    int32_t high;
  };
  std::vector<IoExtent> left_io_extents, right_io_extents, bottom_io_extents, top_io_extents;
  if (keep_io_core_ends) {
    const auto& die = database.get_die();
    for (const Instance& instance : database.get_instance_list()) {
      if (!instance.get_placed() || instance.is_delete_instance() || instance.get_io_cell_net_name().empty()) {
        continue;
      }
      const auto& box = instance.get_bounding_rect();
      const int32_t left = std::abs(box.get_ll_x() - die.get_ll_x());
      const int32_t right = std::abs(die.get_ur_x() - box.get_ur_x());
      const int32_t bottom = std::abs(box.get_ll_y() - die.get_ll_y());
      const int32_t top = std::abs(die.get_ur_y() - box.get_ur_y());
      const int32_t min_distance = std::min({left, right, bottom, top});
      if (min_distance == left) {
        left_io_extents.push_back({box.get_ll_y(), box.get_ur_y()});
      } else if (min_distance == right) {
        right_io_extents.push_back({box.get_ll_y(), box.get_ur_y()});
      } else if (min_distance == bottom) {
        bottom_io_extents.push_back({box.get_ll_x(), box.get_ur_x()});
      } else {
        top_io_extents.push_back({box.get_ll_x(), box.get_ur_x()});
      }
    }
  }
  auto has_io_overlap = [](const std::vector<IoExtent>& extents, int32_t coordinate, int32_t half) {
    for (const auto& extent : extents) {
      if (coordinate + half > extent.low && extent.high > coordinate - half) {
        return true;
      }
    }
    return false;
  };
  for (size_t i = 0; i < segments.size(); ++i) {
    const auto& stripe = segments[i];
    if (!stripe.get_generated() || stripe.get_type() != PGSegmentType::kStripe) {
      continue;
    }
    const bool horizontal = stripe.is_horizontal();
    const int32_t half = stripe.get_width() / 2;
    Candidate candidate{i};
    const auto& core = database.get_core();
    // Ring-connected stripes extend outside the core and are not selected here.
    bool core_start = horizontal ? stripe.get_start_x() == core.get_ll_x() : stripe.get_start_y() == core.get_ll_y();
    bool core_end = horizontal ? stripe.get_end_x() == core.get_ur_x() : stripe.get_end_y() == core.get_ur_y();
    if (keep_io_core_ends
        && std::find(config.pg_io_core_connect.pin_layers.begin(), config.pg_io_core_connect.pin_layers.end(), stripe.get_layer_name())
               != config.pg_io_core_connect.pin_layers.end()) {
      const int32_t tangent = horizontal ? stripe.get_start_y() : stripe.get_start_x();
      if (horizontal) {
        core_start = core_start && !has_io_overlap(left_io_extents, tangent, half);
        core_end = core_end && !has_io_overlap(right_io_extents, tangent, half);
      } else {
        core_start = core_start && !has_io_overlap(bottom_io_extents, tangent, half);
        core_end = core_end && !has_io_overlap(top_io_extents, tangent, half);
      }
    }
    candidate.start = core_start;
    candidate.end = core_end;
    for (auto* instance : _placed_macro_list) {
      if (findRoutingLayer(stripe.get_layer_name())->get_order() > getMacroTopLayerOrder(*instance)) {
        continue;
      }
      const auto& halo = instance->get_routing_halo_rect();
      if (horizontal ? (stripe.get_ur_y() <= halo.get_ll_y() || halo.get_ur_y() <= stripe.get_ll_y())
                     : (stripe.get_ur_x() <= halo.get_ll_x() || halo.get_ur_x() <= stripe.get_ll_x())) {
        continue;
      }
      candidate.start |= horizontal ? stripe.get_start_x() == halo.get_ur_x() + half : stripe.get_start_y() == halo.get_ur_y() + half;
      candidate.end |= horizontal ? stripe.get_end_x() == halo.get_ll_x() - half : stripe.get_end_y() == halo.get_ll_y() - half;
    }
    if (candidate.start || candidate.end) {
      by_coord[{stripe.get_net_name(), stripe.get_layer_name(), horizontal}][horizontal ? stripe.get_start_y() : stripe.get_start_x()]
          .push_back(candidates.size());
      candidates.push_back(candidate);
      max_half_width = std::max(max_half_width, half);
    }
  }
  if (candidates.empty()) {
    return;
  }
  // A terminal only matters when a candidate stripe coordinate lies near the
  // instance.  This avoids transforming every PG pin shape in the design.
  auto near_candidate = [&](const std::string& net_name, const std::string& layer_name, const PlanarRect& box) {
    for (const bool horizontal : {true, false}) {
      auto found = by_coord.find({net_name, layer_name, horizontal});
      if (found == by_coord.end()) {
        continue;
      }
      const int32_t low = horizontal ? box.get_ll_y() : box.get_ll_x();
      const int32_t high = horizontal ? box.get_ur_y() : box.get_ur_x();
      auto it = found->second.lower_bound(low - max_half_width);
      if (it != found->second.end() && it->first <= high + max_half_width) {
        return true;
      }
    }
    return false;
  };

  // Freeze contacts before editing geometry, preserving same-net joins on both axes.
  auto contact = [&](const std::string& net, const std::string& layer, const PlanarRect& rect, size_t source, const PGSegment* via) {
    for (bool horizontal : {true, false}) {
      auto found = by_coord.find({net, layer, horizontal});
      if (found == by_coord.end()) {
        continue;
      }
      const int32_t low = horizontal ? rect.get_ll_y() : rect.get_ll_x();
      const int32_t high = horizontal ? rect.get_ur_y() : rect.get_ur_x();
      for (auto it = found->second.lower_bound(low - max_half_width); it != found->second.end() && it->first <= high + max_half_width; ++it) {
        for (size_t id : it->second) {
          auto& candidate = candidates[id];
          const auto& stripe = segments[candidate.index];
          const int32_t dx = std::min(stripe.get_ur_x(), rect.get_ur_x()) - std::max(stripe.get_ll_x(), rect.get_ll_x());
          const int32_t dy = std::min(stripe.get_ur_y(), rect.get_ur_y()) - std::max(stripe.get_ll_y(), rect.get_ll_y());
          if (candidate.index == source || dx < 0 || dy < 0 || (dx == 0 && dy == 0)) {
            continue;
          }
          int32_t first = horizontal ? std::max(stripe.get_ll_x(), rect.get_ll_x()) : std::max(stripe.get_ll_y(), rect.get_ll_y());
          int32_t last = horizontal ? std::min(stripe.get_ur_x(), rect.get_ur_x()) : std::min(stripe.get_ur_y(), rect.get_ur_y());
          if (via != nullptr) {
            // Retain the via center as well as its actual metal enclosure.
            const int32_t center = horizontal ? via->get_start_x() : via->get_start_y();
            first = std::min(first, center - stripe.get_width() / 2);
            last = std::max(last, center + stripe.get_width() / 2);
          }
          candidate.first_contact = std::min(candidate.first_contact, first);
          candidate.last_contact = std::max(candidate.last_contact, last);
        }
      }
    }
  };
  for (size_t i = 0; i < segments.size(); ++i) {
    auto& segment = segments[i];
    if (segment.is_line()) {
      contact(segment.get_net_name(), segment.get_layer_name(), segmentRect(segment), i, nullptr);
    } else if (segment.get_type() == PGSegmentType::kVia) {
      for (const auto& shape : FPI.getPGViaShapes(segment)) {
        contact(segment.get_net_name(), shape.get_layer_name(), shape, i, &segment);
      }
    }
  }
  for (const auto& net : database.get_pg_net_list()) {
    for (const auto& [instance_name, pin_name] : net.get_instance_pins()) {
      const auto& instance = database.get_instance_list()[database.get_instance_name_to_idx_map().at(instance_name)];
      if (!instance.get_placed() || instance.is_delete_instance()) {
        continue;
      }
      const auto& master = database.get_cell_master_map().at(instance.get_master_name());
      // IO power pins are handled by buildIOCoreConnect.  Do not let an
      // unselected package-facing PORT keep a stripe endpoint alive after the
      // dedicated same-layer connector has decided that it is inaccessible.
      if (FPDM.getConfig().pg_io_core_connect.enabled && instance.get_io_cell_net_name() == net.get_name()
          && instance.get_io_cell_pin_name() == pin_name
          && std::find(FPDM.getConfig().pg_io_core_connect.nets.begin(), FPDM.getConfig().pg_io_core_connect.nets.end(), net.get_name())
                 != FPDM.getConfig().pg_io_core_connect.nets.end()) {
        continue;
      }
      std::vector<InstancePinShape> shapes;
      const PlanarRect& instance_box = instance.get_bounding_rect();
      for (const auto& shape : master.get_pg_pin_shapes()) {
        if (shape.get_pin_name() != pin_name || !near_candidate(net.get_name(), shape.get_layer_name(), instance_box)) {
          continue;
        }
        shapes.push_back(shape);
      }
      if (shapes.empty()) {
        continue;
      }
      for (const auto& shape : FPI.getPlacedShapes(instance, shapes)) {
        contact(net.get_name(), shape.get_layer_name(), shape, segments.size(), nullptr);
      }
    }
  }
  for (const auto& net : database.get_pg_net_list()) {
    for (const auto& [name, direction] : net.get_io_pin_name_to_direction_map()) {
      auto found = database.get_io_pin_name_to_idx_map().find(name);
      if (found == database.get_io_pin_name_to_idx_map().end()) {
        continue;
      }
      const auto& pin = database.get_io_pin_list()[found->second];
      if (!pin.get_placed()) {
        continue;
      }
      if (pin.is_updated()) {
        // Both IO placers emit N-oriented, relative new ports. Imported port_list
        // rectangles are already absolute (including the original orientation).
        for (const auto& port : pin.get_new_port_list()) {
          const int32_t x = port.get_placed() ? port.get_x() : pin.get_x();
          const int32_t y = port.get_placed() ? port.get_y() : pin.get_y();
          contact(net.get_name(), port.get_layer_name(),
                  PlanarRect(port.get_ll_x() + x, port.get_ll_y() + y, port.get_ur_x() + x, port.get_ur_y() + y),
                  segments.size(), nullptr);
        }
      } else {
        for (const auto& port : pin.get_port_list()) {
          contact(net.get_name(), port.get_layer_name(), port, segments.size(), nullptr);
        }
      }
    }
  }
  int32_t trimmed = 0;
  std::ofstream report;
  if (!FPDM.getConfig().pg_temp_directory_path.empty()) {
    report.open(FPDM.getConfig().pg_temp_directory_path + "/stripe_trim.rpt");
    report << "net layer direction fixed_coord old_begin old_end new_begin new_end (DBU)\n";
  }
  const int32_t grid = std::max(1, database.get_manufacture_grid());
  for (const auto& candidate : candidates) {
    if (candidate.first_contact == INT32_MAX) {
      FPLOG.warn(Loc::current(), "PG stripe has no physical contact; retained on '", segments[candidate.index].get_net_name(), "'.");
      continue;
    }
    auto& stripe = segments[candidate.index];
    const bool horizontal = stripe.is_horizontal();
    const int32_t begin = horizontal ? stripe.get_start_x() : stripe.get_start_y();
    const int32_t end = horizontal ? stripe.get_end_x() : stripe.get_end_y();
    int32_t new_begin = candidate.start ? std::max(begin, FPUTIL.alignDown(candidate.first_contact + stripe.get_width() / 2, grid)) : begin;
    int32_t new_end = candidate.end ? std::min(end, FPUTIL.alignUp(candidate.last_contact - stripe.get_width() / 2, grid)) : end;
    if (new_begin >= new_end) {
      continue;
    }
    // A legal wire may need more metal than its contacts alone.  Grow the
    // trimmed span just enough to satisfy the layer minimum width, using the
    // room left by the original segment instead of retaining the whole stripe.
    const RoutingLayer* stripe_layer = findRoutingLayer(stripe.get_layer_name());
    const int32_t min_length = stripe_layer != nullptr ? std::max(stripe_layer->get_min_width(), 1) : 1;
    if (new_end - new_begin < min_length) {
      int32_t remaining = min_length - (new_end - new_begin);
      const int32_t use_after = std::min(remaining, end - new_end);
      new_end += use_after;
      remaining -= use_after;
      if (remaining > 0) {
        const int32_t use_before = std::min(remaining, new_begin - begin);
        new_begin -= use_before;
        remaining -= use_before;
      }
      if (remaining > 0) {
        FPLOG.warn(Loc::current(), "Retained PG stripe on '", stripe.get_net_name(), "' to preserve minimum width.");
        continue;
      }
    }
    const int64_t area = int64_t(new_end - new_begin + stripe.get_width()) * stripe.get_width();
    if (stripe_layer != nullptr && area < stripe_layer->get_min_area()) {
      FPLOG.warn(Loc::current(), "Retained PG stripe on '", stripe.get_net_name(), "' to preserve minimum area.");
      continue;
    }
    trimmed += (new_begin != begin) + (new_end != end);
    if (report && (new_begin != begin || new_end != end)) {
      report << stripe.get_net_name() << ' ' << stripe.get_layer_name() << ' ' << (horizontal ? "H" : "V") << ' '
             << (horizontal ? stripe.get_start_y() : stripe.get_start_x()) << ' '
             << begin << ' ' << end << ' ' << new_begin << ' ' << new_end << '\n';
    }
    if (horizontal) {
      stripe.set_start_x(new_begin);
      stripe.set_end_x(new_end);
    } else {
      stripe.set_start_y(new_begin);
      stripe.set_end_y(new_end);
    }
  }
  if (trimmed > 0) {
    // Existing vias must be re-checked against the moved stripe endpoints.
    FPI.resetPGViaAdjustment();
  }
  FPLOG.info(Loc::current(), "Trimmed ", trimmed, " stripe ends while preserving physical PG contacts.");
}

void PDNGenerator::trimDanglingFollowPins()
{
  Database& database = FPDM.getDatabase();
  std::vector<PGSegment>& segments = database.get_pg_segment_list();
  const int32_t grid = std::max(1, database.get_manufacture_grid());

  using LayerRectMap = std::map<std::string, std::vector<PlanarRect>>;
  std::map<std::string, LayerRectMap> pin_rects;
  std::map<std::string, const PGNet*> pg_nets;
  for (const PGNet& pg_net : database.get_pg_net_list()) {
    pg_nets[pg_net.get_name()] = &pg_net;
    for (const auto& [instance_name, pin_name] : pg_net.get_instance_pins()) {
      auto instance_iter = database.get_instance_name_to_idx_map().find(instance_name);
      if (instance_iter == database.get_instance_name_to_idx_map().end()) {
        continue;
      }
      const Instance& instance = database.get_instance_list()[instance_iter->second];
      if (!instance.get_placed() || instance.is_delete_instance()) {
        continue;
      }
      auto master_iter = database.get_cell_master_map().find(instance.get_master_name());
      if (master_iter == database.get_cell_master_map().end()) {
        continue;
      }
      std::vector<InstancePinShape> pin_shapes;
      for (const auto& shape : master_iter->second.get_pg_pin_shapes()) {
        if (shape.get_pin_name() == pin_name) {
          pin_shapes.push_back(shape);
        }
      }
      for (const auto& shape : FPI.getPlacedShapes(instance, pin_shapes)) {
        pin_rects[pg_net.get_name()][shape.get_layer_name()].emplace_back(shape.get_ll_x(), shape.get_ll_y(), shape.get_ur_x(), shape.get_ur_y());
      }
    }
  }

  std::map<std::string, LayerRectMap> via_rects;
  for (const PGSegment& via : segments) {
    if (via.get_type() != PGSegmentType::kVia || !via.get_generated() || via.get_via_width() <= 0 || via.get_via_height() <= 0) {
      continue;
    }
    const int32_t half_width = via.get_via_width() / 2;
    const int32_t half_height = via.get_via_height() / 2;
    const PlanarRect via_rect(via.get_start_x() - half_width, via.get_start_y() - half_height, via.get_start_x() + half_width,
                              via.get_start_y() + half_height);
    for (const std::string* layer_name : {&via.get_bottom_layer_name(), &via.get_top_layer_name()}) {
      if (!layer_name->empty()) {
        via_rects[via.get_net_name()][*layer_name].push_back(via_rect);
      }
    }
  }

  int32_t trimmed = 0;
  for (size_t index = 0; index < segments.size(); ++index) {
    PGSegment& rail = segments[index];
    if (rail.get_type() != PGSegmentType::kFollowPin || !rail.is_horizontal()) {
      continue;
    }
    const int32_t width = rail.get_width();
    const int32_t low = std::min(rail.get_start_x(), rail.get_end_x());
    const int32_t high = std::max(rail.get_start_x(), rail.get_end_x());
    const PlanarRect rail_rect(low, rail.get_start_y() - width / 2, high, rail.get_start_y() + width / 2);
    int32_t connection_low = INT32_MAX;
    int32_t connection_high = INT32_MIN;
    auto add_connection_interval = [&](int32_t first, int32_t second) {
      connection_low = std::min(connection_low, std::min(first, second));
      connection_high = std::max(connection_high, std::max(first, second));
    };
    auto add_rect_connection = [&](const PlanarRect& rect) {
      const int32_t overlap_x = std::min(rail_rect.get_ur_x(), rect.get_ur_x()) - std::max(rail_rect.get_ll_x(), rect.get_ll_x());
      const int32_t overlap_y = std::min(rail_rect.get_ur_y(), rect.get_ur_y()) - std::max(rail_rect.get_ll_y(), rect.get_ll_y());
      if (overlap_x >= 0 && overlap_y >= 0) {
        add_connection_interval(rect.get_ll_x(), rect.get_ur_x());
      }
    };

    auto pin_iter = pin_rects.find(rail.get_net_name());
    if (pin_iter != pin_rects.end()) {
      auto layer_iter = pin_iter->second.find(rail.get_layer_name());
      if (layer_iter != pin_iter->second.end()) {
        for (const PlanarRect& rect : layer_iter->second) {
          add_rect_connection(rect);
        }
      }
    }
    auto via_iter = via_rects.find(rail.get_net_name());
    if (via_iter != via_rects.end()) {
      auto layer_iter = via_iter->second.find(rail.get_layer_name());
      if (layer_iter != via_iter->second.end()) {
        for (const PlanarRect& rect : layer_iter->second) {
          add_rect_connection(rect);
        }
      }
    }
    for (size_t other_index = 0; other_index < segments.size(); ++other_index) {
      if (other_index == index) {
        continue;
      }
      const PGSegment& other = segments[other_index];
      if (!other.is_line() || other.get_net_name() != rail.get_net_name() || other.get_layer_name() != rail.get_layer_name()) {
        continue;
      }
      add_rect_connection(wireRect(other));
    }
    if (connection_low == INT32_MAX) {
      continue;
    }

    int32_t new_low = std::max(low, connection_low);
    int32_t new_high = std::min(high, connection_high);
    new_low = std::max(low, FPUTIL.alignDown(new_low, grid));
    new_high = std::min(high, FPUTIL.alignUp(new_high, grid));
    if (new_low >= new_high || (new_low == low && new_high == high)) {
      continue;
    }
    const bool ascending = rail.get_start_x() <= rail.get_end_x();
    rail.set_start_x(ascending ? new_low : new_high);
    rail.set_end_x(ascending ? new_high : new_low);
    ++trimmed;
  }
  if (trimmed > 0) {
    FPLOG.info(Loc::current(), "Trimmed ", trimmed, " dangling follow-pin rail end(s).");
  }
}

void PDNGenerator::buildPGNet(PGModel& pg_model)
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  Database& database = FPDM.getDatabase();
  Config& config = FPDM.getConfig();
  database.get_pg_net_list().clear();
  database.get_pg_net_name_to_idx_map().clear();
  for (PGGlobalConnect& pg_connect : FPDM.getConfig().pg_connect_list) {
    if (pg_connect.get_net_name().empty() || pg_connect.get_pin_name().empty() || pg_connect.get_net_type() == PGNetType::kNone) {
      FPLOG.error(Loc::current(), "PG connection requires a net, terminal and POWER/GROUND type.");
    }
    std::map<std::string, int32_t>::iterator pg_net_iter = database.get_pg_net_name_to_idx_map().find(pg_connect.get_net_name());
    if (pg_net_iter == database.get_pg_net_name_to_idx_map().end()) {
      PGNet pg_net;
      pg_net.set_name(pg_connect.get_net_name());
      pg_net.set_type(pg_connect.get_net_type());
      int32_t pg_net_idx = static_cast<int32_t>(database.get_pg_net_list().size());
      database.get_pg_net_list().push_back(pg_net);
      database.get_pg_net_name_to_idx_map()[pg_connect.get_net_name()] = pg_net_idx;
    }
    PGNet& pg_net = getPGNet(pg_connect.get_net_name());
    if (pg_net.get_type() != pg_connect.get_net_type()) {
      FPLOG.error(Loc::current(), "Conflicting POWER/GROUND types for '", pg_net.get_name(), "'.");
    }
  }

  using Terminal = std::pair<std::string, std::string>;
  std::map<Terminal, std::string> bindings;
  auto bind = [&](const Instance& instance, const std::string& pin_name, const std::string& net_name) {
    if (instance.is_delete_instance()) {
      return;
    }
    auto master = database.get_cell_master_map().find(instance.get_master_name());
    if (master != database.get_cell_master_map().end()) {
      auto pin_type = master->second.get_pg_pin_types().find(pin_name);
      if (pin_type == master->second.get_pg_pin_types().end() || pin_type->second != getPGNet(net_name).get_type()) {
        FPLOG.error(Loc::current(), "PG type mismatch or missing terminal '", instance.get_name(), "/", pin_name, "' on '", net_name, "'.");
      }
    }
    auto [iter, inserted] = bindings.emplace(Terminal{instance.get_name(), pin_name}, net_name);
    if (!inserted && iter->second != net_name) {
      FPLOG.error(Loc::current(), "Conflicting PG nets '", iter->second, "' and '", net_name, "' for '", instance.get_name(), "/", pin_name, "'.");
    }
  };

  // Import explicit netlist connections before applying global rules.
  for (Net& net : database.get_net_list()) {
    if (!database.get_pg_net_name_to_idx_map().contains(net.get_name())) {
      continue;
    }
    PGNet& pg_net = getPGNet(net.get_name());
    net.set_pdn(true);
    net.set_power(pg_net.get_type() == PGNetType::kPower);
    net.set_ground(pg_net.get_type() == PGNetType::kGround);
    for (const NetPin& pin : net.get_net_pin_list()) {
      if (pin.get_io()) {
        pg_net.add_io_pin(pin.get_pin_name(), IOPinDirection::kInOut);
      } else {
        auto instance = database.get_instance_name_to_idx_map().find(pin.get_instance_name());
        if (instance == database.get_instance_name_to_idx_map().end()) {
          FPLOG.error(Loc::current(), "Unknown instance '", pin.get_instance_name(), "' on PG net '", net.get_name(), "'.");
        }
        bind(database.get_instance_list()[instance->second], pin.get_pin_name(), net.get_name());
      }
    }
  }
  std::map<std::string, std::vector<const PGGlobalConnect*>> rules_by_pin;
  for (const PGGlobalConnect& rule : config.pg_connect_list) {
    rules_by_pin[rule.get_pin_name()].push_back(&rule);
  }
  auto matches = [](const std::string& pattern, const std::string& value) {
    return pattern == "*" || fnmatch(pattern.c_str(), value.c_str(), 0) == 0;
  };
  for (const Instance& instance : database.get_instance_list()) {
    if (instance.is_delete_instance()) {
      continue;
    }
    if (!instance.get_io_cell_net_name().empty()) {
      if (!database.get_pg_net_name_to_idx_map().contains(instance.get_io_cell_net_name())) {
        FPLOG.error(Loc::current(), "Generated supply cell '", instance.get_name(), "' uses undeclared PG net '", instance.get_io_cell_net_name(), "'.");
      }
      bind(instance, instance.get_io_cell_pin_name(), instance.get_io_cell_net_name());
    }
    auto master_iter = database.get_cell_master_map().find(instance.get_master_name());
    if (master_iter == database.get_cell_master_map().end()) {
      for (const auto& pin_shape : instance.get_pin_shape_list()) {
        auto rule_iter = rules_by_pin.find(pin_shape.get_pin_name());
        if (rule_iter == rules_by_pin.end()) {
          continue;
        }
        for (const PGGlobalConnect* rule : rule_iter->second) {
          if (matches(rule->get_instance_pattern(), instance.get_name()) && matches(rule->get_master_pattern(), instance.get_master_name())) {
            bind(instance, pin_shape.get_pin_name(), rule->get_net_name());
          }
        }
      }
      continue;
    }
    const auto& pin_types = master_iter->second.get_pg_pin_types();
    for (const auto& pin_entry : pin_types) {
      const std::string& pin_name = pin_entry.first;
      auto rule_iter = rules_by_pin.find(pin_name);
      if (rule_iter == rules_by_pin.end()) {
        continue;
      }
      for (const PGGlobalConnect* rule : rule_iter->second) {
        if (matches(rule->get_instance_pattern(), instance.get_name()) && matches(rule->get_master_pattern(), instance.get_master_name())) {
          bind(instance, pin_name, rule->get_net_name());
        }
      }
    }
  }
  for (const auto& [terminal, net_name] : bindings) {
    getPGNet(net_name).add_instance_pin(terminal.first, terminal.second);
  }
  for (const PGGlobalConnect& rule : config.pg_connect_list) {
    // Scoped rules stay explicit, so DEF reimport cannot widen their domain.
    if (rule.get_instance_pattern() == "*" && rule.get_master_pattern() == "*") {
      auto& names = getPGNet(rule.get_net_name()).get_instance_pin_name_list();
      if (std::find(names.begin(), names.end(), rule.get_pin_name()) == names.end()) {
        names.push_back(rule.get_pin_name());
      }
    }
  }
  auto core_net = [&](const std::string& requested, PGNetType type) {
    if (!requested.empty()) {
      if (!database.get_pg_net_name_to_idx_map().contains(requested) || getPGNet(requested).get_type() != type) {
        FPLOG.error(Loc::current(), "Invalid core PG net '", requested, "'.");
      }
      return requested;
    }
    std::string result;
    for (const PGNet& net : database.get_pg_net_list()) {
      if (net.get_type() == type) {
        if (!result.empty() && (!config.pg_rail_list.empty() || !config.pg_stripe_list.empty())) {
          FPLOG.error(Loc::current(), "Multiple PG domains require explicit core_power_net and core_ground_net.");
        }
        result = net.get_name();
      }
    }
    return result;
  };
  pg_model.set_default_power_net_name(core_net(config.core_power_net, PGNetType::kPower));
  pg_model.set_default_ground_net_name(core_net(config.core_ground_net, PGNetType::kGround));
  if ((!config.pg_rail_list.empty() || !config.pg_stripe_list.empty())
      && (pg_model.get_default_power_net_name().empty() || pg_model.get_default_ground_net_name().empty())) {
    FPLOG.error(Loc::current(), "Rails and stripes require both core POWER and GROUND nets.");
  }
  resolveIOCoreConnectDefaults(pg_model);
  if (config.pg_io_core_connect.enabled) {
    for (const auto& net_name : config.pg_io_core_connect.nets) {
      auto net_it = database.get_pg_net_name_to_idx_map().find(net_name);
      if (net_it == database.get_pg_net_name_to_idx_map().end()) {
        FPLOG.error(Loc::current(), "io_core_connect refers to undeclared PG net '", net_name, "'.");
      }
      if (net_name != pg_model.get_default_power_net_name() && net_name != pg_model.get_default_ground_net_name()) {
        FPLOG.error(Loc::current(), "io_core_connect net '", net_name, "' is not the resolved core POWER/GROUND net.");
      }
    }
    for (const auto& layer_name : config.pg_io_core_connect.pin_layers) {
      if (findRoutingLayer(layer_name) == nullptr) {
        FPLOG.error(Loc::current(), "io_core_connect refers to unknown pin layer '", layer_name, "'.");
      }
    }
  }
  for (const auto& net_name : config.pg_required_net_list) {
    if (!database.get_pg_net_name_to_idx_map().contains(net_name)) {
      FPLOG.error(Loc::current(), "required_nets refers to undeclared PG net '", net_name, "'.");
    }
  }
  FPLOG.info(Loc::current(), "Resolved ", bindings.size(), " instance PG terminals on ", database.get_pg_net_list().size(), " nets.");

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PDNGenerator::resolveIOCoreConnectDefaults(PGModel& pg_model)
{
  auto& config = FPDM.getConfig();
  if (!config.pg_io_core_connect.enabled) {
    return;
  }

  auto& options = config.pg_io_core_connect;
  if (options.nets.empty()) {
    const auto& power_net = pg_model.get_default_power_net_name();
    const auto& ground_net = pg_model.get_default_ground_net_name();
    if (power_net.empty() || ground_net.empty()) {
      FPLOG.error(Loc::current(), "io_core_connect requires resolvable core POWER and GROUND nets when 'nets' is omitted.");
    }
    options.nets.push_back(power_net);
    if (ground_net != power_net) {
      options.nets.push_back(ground_net);
    }
  }

  if (options.pin_layers.empty()) {
    std::set<std::string> stripe_layers;
    for (const auto& stripe : config.pg_stripe_list) {
      if (!stripe.get_layer_name().empty()) {
        stripe_layers.insert(stripe.get_layer_name());
      }
    }
    options.pin_layers.assign(stripe_layers.begin(), stripe_layers.end());
  }

  if (config.pg_required_net_list.empty()) {
    config.pg_required_net_list = options.nets;
  }
}

PGNet& PDNGenerator::getPGNet(std::string net_name)
{
  Database& database = FPDM.getDatabase();
  return database.get_pg_net_list()[database.get_pg_net_name_to_idx_map()[net_name]];
}

void PDNGenerator::buildRail(PGModel& pg_model)
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  Database& database = FPDM.getDatabase();
  for (PGRail& pg_rail : FPDM.getConfig().pg_rail_list) {
    RoutingLayer* routing_layer = findRoutingLayer(pg_rail.get_layer_name());
    if (routing_layer == nullptr || routing_layer->get_prefer_direction() != Direction::kHorizontal) {
      continue;
    }

    int32_t width = FPUTIL.transMicronToDBU(pg_rail.get_width_micron(), database.get_micron_dbu());
    if (width <= 0) {
      continue;
    }
    for (const PGLayerPair& layer_pair : FPDM.getConfig().pg_layer_pair_list) {
      std::string other_layer_name;
      if (layer_pair.get_first_layer_name() == routing_layer->get_name()) {
        other_layer_name = layer_pair.get_second_layer_name();
      } else if (layer_pair.get_second_layer_name() == routing_layer->get_name()) {
        other_layer_name = layer_pair.get_first_layer_name();
      } else {
        continue;
      }
      width = std::max(width, FPI.getPGViaMinSpan(routing_layer->get_name(), other_layer_name));
    }
    width = FPUTIL.alignUp(width, std::max(1, database.get_manufacture_grid()));

    for (Row& row : database.get_row_list()) {
      std::string bottom_net_name = row.get_orient() == PlacementOrientation::kN || row.get_orient() == PlacementOrientation::kFN
                                        ? pg_model.get_default_ground_net_name()
                                        : pg_model.get_default_power_net_name();
      addLineSegment(bottom_net_name, routing_layer->get_name(), PGSegmentType::kFollowPin, width, row.get_ll_x(), row.get_y(), row.get_ur_x(), row.get_y());
    }

    for (Row& row : database.get_row_list()) {
      std::string top_net_name = row.get_orient() == PlacementOrientation::kN || row.get_orient() == PlacementOrientation::kFN
                                     ? pg_model.get_default_power_net_name()
                                     : pg_model.get_default_ground_net_name();
      std::vector<std::pair<int32_t, int32_t>> covered_interval_list;
      for (PGSegment& pg_segment : database.get_pg_segment_list()) {
        if (pg_segment.get_type() != PGSegmentType::kFollowPin || !pg_segment.is_horizontal() || pg_segment.get_layer_name() != routing_layer->get_name()
            || pg_segment.get_net_name() != top_net_name || pg_segment.get_start_y() != row.get_ur_y()) {
          continue;
        }
        int32_t start_x = std::max(row.get_ll_x(), std::min(pg_segment.get_start_x(), pg_segment.get_end_x()));
        int32_t end_x = std::min(row.get_ur_x(), std::max(pg_segment.get_start_x(), pg_segment.get_end_x()));
        if (start_x < end_x) {
          covered_interval_list.emplace_back(start_x, end_x);
        }
      }
      std::sort(covered_interval_list.begin(), covered_interval_list.end());

      int32_t current_x = row.get_ll_x();
      for (std::pair<int32_t, int32_t>& covered_interval : covered_interval_list) {
        if (covered_interval.second <= current_x) {
          continue;
        }
        if (current_x < covered_interval.first) {
          addLineSegment(top_net_name, routing_layer->get_name(), PGSegmentType::kFollowPin, width, current_x, row.get_ur_y(), covered_interval.first,
                         row.get_ur_y());
        }
        current_x = std::max(current_x, covered_interval.second);
        if (row.get_ur_x() <= current_x) {
          break;
        }
      }
      if (current_x < row.get_ur_x()) {
        addLineSegment(top_net_name, routing_layer->get_name(), PGSegmentType::kFollowPin, width, current_x, row.get_ur_y(), row.get_ur_x(), row.get_ur_y());
      }
    }
  }
  mergeRailSegmentList();

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PDNGenerator::mergeRailSegmentList()
{
  Database& database = FPDM.getDatabase();
  auto& segments = database.get_pg_segment_list();
  if (segments.empty()) {
    return;
  }

  using RailKey = std::tuple<std::string, std::string, int32_t, int32_t>;
  struct RailGroup
  {
    PGSegment prototype;
    std::vector<std::pair<int32_t, int32_t>> intervals;
  };
  std::map<RailKey, RailGroup> groups;
  std::vector<PGSegment> merged_pg_segment_list;
  merged_pg_segment_list.reserve(segments.size());
  for (const PGSegment& segment : segments) {
    if (segment.get_type() != PGSegmentType::kFollowPin || !segment.is_horizontal()) {
      merged_pg_segment_list.push_back(segment);
      continue;
    }
    const RailKey key{segment.get_net_name(), segment.get_layer_name(), segment.get_width(), segment.get_start_y()};
    auto [iter, inserted] = groups.try_emplace(key);
    if (inserted) {
      iter->second.prototype = segment;
    }
    iter->second.intervals.emplace_back(std::min(segment.get_start_x(), segment.get_end_x()),
                                        std::max(segment.get_start_x(), segment.get_end_x()));
  }

  for (auto& [key, group] : groups) {
    auto& intervals = group.intervals;
    std::sort(intervals.begin(), intervals.end(),
              [](const std::pair<int32_t, int32_t>& first, const std::pair<int32_t, int32_t>& second) { return first.first < second.first; });
    int32_t low = intervals.front().first;
    int32_t high = intervals.front().second;
    auto emit = [&](int32_t begin, int32_t end) {
      PGSegment merged = group.prototype;
      merged.set_start_coord(begin, group.prototype.get_start_y());
      merged.set_end_coord(end, group.prototype.get_start_y());
      merged_pg_segment_list.push_back(merged);
    };
    for (size_t index = 1; index < intervals.size(); ++index) {
      const auto [next_low, next_high] = intervals[index];
      // segmentRect() expands a rail by half its width on both sides; two
      // rails merge when those expanded extents still overlap.
      if (static_cast<int64_t>(next_low) < static_cast<int64_t>(high) + group.prototype.get_width()) {
        high = std::max(high, next_high);
      } else {
        emit(low, high);
        low = next_low;
        high = next_high;
      }
    }
    emit(low, high);
  }
  segments = std::move(merged_pg_segment_list);
}

void PDNGenerator::mergeIoWireSegmentList()
{
  Database& database = FPDM.getDatabase();
  std::vector<PGSegment>& segments = database.get_pg_segment_list();
  std::vector<size_t> io_indices;
  io_indices.reserve(segments.size());
  for (size_t index = 0; index < segments.size(); ++index) {
    if (segments[index].get_type() == PGSegmentType::kIoWire && segments[index].is_line()) {
      io_indices.push_back(index);
    }
  }
  if (io_indices.size() < 2) {
    return;
  }

  // A direct IO extension is emitted as start->io_inner->end.  Merge the
  // collinear pieces so the DEF carries one wire per connection.
  using IoKey = std::tuple<std::string, std::string, int32_t, bool, int32_t>;
  struct IoGroup
  {
    PGSegment prototype;
    bool horizontal = false;
    std::vector<std::pair<int32_t, int32_t>> intervals;
  };
  std::map<IoKey, IoGroup> groups;
  for (size_t index : io_indices) {
    const PGSegment& segment = segments[index];
    const bool horizontal = segment.is_horizontal();
    const int32_t fixed = horizontal ? segment.get_start_y() : segment.get_start_x();
    const IoKey key{segment.get_net_name(), segment.get_layer_name(), segment.get_width(), horizontal, fixed};
    auto [iter, inserted] = groups.try_emplace(key);
    if (inserted) {
      iter->second.prototype = segment;
      iter->second.horizontal = horizontal;
    }
    iter->second.intervals.emplace_back(
        horizontal ? std::min(segment.get_start_x(), segment.get_end_x()) : std::min(segment.get_start_y(), segment.get_end_y()),
        horizontal ? std::max(segment.get_start_x(), segment.get_end_x()) : std::max(segment.get_start_y(), segment.get_end_y()));
  }

  std::vector<PGSegment> merged;
  merged.reserve(io_indices.size());
  for (auto& [key, group] : groups) {
    auto& intervals = group.intervals;
    std::sort(intervals.begin(), intervals.end(),
              [](const std::pair<int32_t, int32_t>& first, const std::pair<int32_t, int32_t>& second) { return first.first < second.first; });
    int32_t low = intervals.front().first;
    int32_t high = intervals.front().second;
    auto emit = [&](int32_t begin, int32_t end) {
      PGSegment segment = group.prototype;
      if (group.horizontal) {
        segment.set_start_coord(begin, group.prototype.get_start_y());
        segment.set_end_coord(end, group.prototype.get_start_y());
      } else {
        segment.set_start_coord(group.prototype.get_start_x(), begin);
        segment.set_end_coord(group.prototype.get_start_x(), end);
      }
      merged.push_back(segment);
    };
    for (size_t index = 1; index < intervals.size(); ++index) {
      const auto [next_low, next_high] = intervals[index];
      if (next_low <= high) {
        high = std::max(high, next_high);
      } else {
        emit(low, high);
        low = next_low;
        high = next_high;
      }
    }
    emit(low, high);
  }

  std::vector<bool> erased(segments.size(), false);
  for (size_t index : io_indices) {
    erased[index] = true;
  }
  std::vector<PGSegment> rebuilt;
  rebuilt.reserve(segments.size() - io_indices.size() + merged.size());
  for (size_t index = 0; index < segments.size(); ++index) {
    if (!erased[index]) {
      rebuilt.push_back(std::move(segments[index]));
    }
  }
  for (PGSegment& segment : merged) {
    rebuilt.push_back(std::move(segment));
  }
  segments = std::move(rebuilt);
}

void PDNGenerator::mergeOverlappingViaStacks()
{
  std::vector<PGSegment>& segments = FPDM.getDatabase().get_pg_segment_list();

  using ViaKey = std::tuple<std::string, std::string, std::string, std::string>;
  std::map<ViaKey, std::vector<size_t>> groups;
  for (size_t index = 0; index < segments.size(); ++index) {
    const PGSegment& segment = segments[index];
    if (segment.get_type() != PGSegmentType::kVia || !segment.get_generated() || segment.get_via_width() <= 0
        || segment.get_via_height() <= 0) {
      continue;
    }
    groups[{segment.get_net_name(), segment.get_bottom_layer_name(), segment.get_top_layer_name(), segment.get_cut_layer_name()}].push_back(index);
  }

  std::vector<bool> removed(segments.size(), false);
  int32_t merged_count = 0;
  auto via_rect = [&](size_t index) {
    const PGSegment& via = segments[index];
    const int32_t half_width = via.get_via_width() / 2;
    const int32_t half_height = via.get_via_height() / 2;
    return PlanarRect(via.get_start_x() - half_width, via.get_start_y() - half_height, via.get_start_x() + half_width,
                      via.get_start_y() + half_height);
  };
  auto contains_x = [](const PlanarRect& outer, const PlanarRect& inner) {
    return outer.get_ll_x() <= inner.get_ll_x() && inner.get_ur_x() <= outer.get_ur_x();
  };
  auto contains_y = [](const PlanarRect& outer, const PlanarRect& inner) {
    return outer.get_ll_y() <= inner.get_ll_y() && inner.get_ur_y() <= outer.get_ur_y();
  };
  auto should_merge = [&](const PlanarRect& first, const PlanarRect& second) {
    const int32_t overlap_x = std::min(first.get_ur_x(), second.get_ur_x()) - std::max(first.get_ll_x(), second.get_ll_x());
    const int32_t overlap_y = std::min(first.get_ur_y(), second.get_ur_y()) - std::max(first.get_ll_y(), second.get_ll_y());
    if (overlap_x <= 0 || overlap_y <= 0) {
      return false;
    }
    return contains_x(first, second) || contains_x(second, first) || contains_y(first, second) || contains_y(second, first);
  };
  auto cell_of = [](int32_t coordinate) {
    constexpr int32_t kCellSize = 2000;
    return coordinate >= 0 ? coordinate / kCellSize : (coordinate + 1) / kCellSize - 1;
  };

  for (auto& [key, indices] : groups) {
    if (indices.size() < 2) {
      continue;
    }
    bool changed = true;
    for (int32_t round = 0; changed && round < 8; ++round) {
      changed = false;
      std::map<std::pair<int32_t, int32_t>, std::vector<size_t>> grid;
      for (size_t index : indices) {
        if (removed[index]) {
          continue;
        }
        const PlanarRect rect = via_rect(index);
        for (int32_t x = cell_of(rect.get_ll_x()); x <= cell_of(rect.get_ur_x()); ++x) {
          for (int32_t y = cell_of(rect.get_ll_y()); y <= cell_of(rect.get_ur_y()); ++y) {
            grid[{x, y}].push_back(index);
          }
        }
      }
      for (auto& [cell, cell_indices] : grid) {
        for (size_t first_pos = 0; first_pos < cell_indices.size(); ++first_pos) {
          const size_t first = cell_indices[first_pos];
          if (removed[first]) {
            continue;
          }
          for (size_t second_pos = first_pos + 1; second_pos < cell_indices.size(); ++second_pos) {
            const size_t second = cell_indices[second_pos];
            if (removed[second] || first == second) {
              continue;
            }
            const PlanarRect first_rect = via_rect(first);
            const PlanarRect second_rect = via_rect(second);
            if (!should_merge(first_rect, second_rect)) {
              continue;
            }
            const PlanarRect merged_rect(std::min(first_rect.get_ll_x(), second_rect.get_ll_x()),
                                         std::min(first_rect.get_ll_y(), second_rect.get_ll_y()),
                                         std::max(first_rect.get_ur_x(), second_rect.get_ur_x()),
                                         std::max(first_rect.get_ur_y(), second_rect.get_ur_y()));
            const int32_t x = (merged_rect.get_ll_x() + merged_rect.get_ur_x()) / 2;
            const int32_t y = (merged_rect.get_ll_y() + merged_rect.get_ur_y()) / 2;
            segments[first].set_start_coord(x, y);
            segments[first].set_end_coord(x, y);
            segments[first].set_via_width(merged_rect.get_width());
            segments[first].set_via_height(merged_rect.get_height());
            removed[second] = true;
            ++merged_count;
            changed = true;
          }
        }
      }
    }
  }

  if (merged_count == 0) {
    return;
  }
  std::vector<PGSegment> kept;
  kept.reserve(segments.size() - merged_count);
  for (size_t index = 0; index < segments.size(); ++index) {
    if (!removed[index]) {
      kept.push_back(std::move(segments[index]));
    }
  }
  segments = std::move(kept);
  FPLOG.info(Loc::current(), "Merged ", merged_count, " overlapping PG via stack(s).");
}

RoutingLayer* PDNGenerator::findRoutingLayer(std::string layer_name)
{
  Database& database = FPDM.getDatabase();
  std::map<std::string, int32_t>::iterator routing_layer_iter = database.get_routing_layer_name_to_idx_map().find(layer_name);
  if (routing_layer_iter == database.get_routing_layer_name_to_idx_map().end()) {
    return nullptr;
  }
  return &database.get_routing_layer_list()[routing_layer_iter->second];
}

void PDNGenerator::addLineSegment(std::string net_name, std::string layer_name, PGSegmentType segment_type, int32_t width, int32_t start_x, int32_t start_y,
                                  int32_t end_x, int32_t end_y)
{
  if (width <= 0 || (start_x == end_x && start_y == end_y)) {
    return;
  }

  std::vector<std::pair<int32_t, int32_t>> blockage_interval_list = getMacroBlockageIntervalList(layer_name, width, start_x, start_y, end_x, end_y);
  if (blockage_interval_list.empty()) {
    addUnblockedLineSegment(net_name, layer_name, segment_type, width, start_x, start_y, end_x, end_y);
    return;
  }

  bool horizontal = start_y == end_y;
  int32_t line_begin = horizontal ? std::min(start_x, end_x) : std::min(start_y, end_y);
  int32_t line_end = horizontal ? std::max(start_x, end_x) : std::max(start_y, end_y);
  int32_t current_coord = line_begin;
  for (std::pair<int32_t, int32_t>& blockage_interval : blockage_interval_list) {
    if (blockage_interval.second <= current_coord) {
      continue;
    }
    if (blockage_interval.first > current_coord) {
      if (horizontal) {
        addUnblockedLineSegment(net_name, layer_name, segment_type, width, current_coord, start_y, blockage_interval.first, start_y);
      } else {
        addUnblockedLineSegment(net_name, layer_name, segment_type, width, start_x, current_coord, start_x, blockage_interval.first);
      }
    }
    current_coord = std::max(current_coord, blockage_interval.second);
    if (current_coord >= line_end) {
      return;
    }
  }
  if (current_coord < line_end) {
    if (horizontal) {
      addUnblockedLineSegment(net_name, layer_name, segment_type, width, current_coord, start_y, line_end, start_y);
    } else {
      addUnblockedLineSegment(net_name, layer_name, segment_type, width, start_x, current_coord, start_x, line_end);
    }
  }
}

void PDNGenerator::addUnblockedLineSegment(std::string net_name, std::string layer_name, PGSegmentType segment_type, int32_t width, int32_t start_x,
                                           int32_t start_y, int32_t end_x, int32_t end_y)
{
  if (start_x == end_x && start_y == end_y) {
    return;
  }
  PGSegment pg_segment;
  pg_segment.set_net_name(net_name);
  pg_segment.set_layer_name(layer_name);
  pg_segment.set_type(segment_type);
  pg_segment.set_width(width);
  pg_segment.set_start_coord(start_x, start_y);
  pg_segment.set_end_coord(end_x, end_y);
  pg_segment.set_generated(true);
  FPDM.getDatabase().get_pg_segment_list().push_back(pg_segment);
}

std::vector<std::pair<int32_t, int32_t>> PDNGenerator::getMacroBlockageIntervalList(std::string layer_name, int32_t width, int32_t start_x, int32_t start_y,
                                                                                    int32_t end_x, int32_t end_y)
{
  RoutingLayer* routing_layer = findRoutingLayer(layer_name);
  std::vector<std::pair<int32_t, int32_t>> blockage_interval_list;
  bool horizontal = start_y == end_y;
  int32_t line_begin = horizontal ? std::min(start_x, end_x) : std::min(start_y, end_y);
  int32_t line_end = horizontal ? std::max(start_x, end_x) : std::max(start_y, end_y);
  int32_t line_coord = horizontal ? start_y : start_x;
  int32_t half_width = width / 2;
  for (Instance* instance_ptr : _placed_macro_list) {
    Instance& instance = *instance_ptr;
    if (routing_layer->get_order() > getMacroTopLayerOrder(instance)) {
      continue;
    }
    const PlanarRect& pg_blockage_rect = getMacroPGBlockageRect(instance);
    if (horizontal) {
      if (line_coord + half_width <= pg_blockage_rect.get_ll_y() || pg_blockage_rect.get_ur_y() <= line_coord - half_width) {
        continue;
      }
      int32_t start_coord = std::max(line_begin, pg_blockage_rect.get_ll_x());
      int32_t end_coord = std::min(line_end, pg_blockage_rect.get_ur_x());
      if (start_coord < end_coord) {
        blockage_interval_list.emplace_back(start_coord, end_coord);
      }
    } else {
      if (line_coord + half_width <= pg_blockage_rect.get_ll_x() || pg_blockage_rect.get_ur_x() <= line_coord - half_width) {
        continue;
      }
      int32_t start_coord = std::max(line_begin, pg_blockage_rect.get_ll_y());
      int32_t end_coord = std::min(line_end, pg_blockage_rect.get_ur_y());
      if (start_coord < end_coord) {
        blockage_interval_list.emplace_back(start_coord, end_coord);
      }
    }
  }
  std::sort(blockage_interval_list.begin(), blockage_interval_list.end(),
            [](const std::pair<int32_t, int32_t>& first, const std::pair<int32_t, int32_t>& second) { return first.first < second.first; });
  return blockage_interval_list;
}

int32_t PDNGenerator::getMacroTopLayerOrder(Instance& instance)
{
  auto iter = _macro_top_layer_order.find(&instance);
  if (iter != _macro_top_layer_order.end()) {
    return iter->second;
  }
  int32_t top_layer_order = -1;
  for (InstancePinShape& pin_shape : instance.get_pin_shape_list()) {
    RoutingLayer* routing_layer = findRoutingLayer(pin_shape.get_layer_name());
    if (routing_layer != nullptr) {
      top_layer_order = std::max(top_layer_order, routing_layer->get_order());
    }
  }
  _macro_top_layer_order[&instance] = top_layer_order;
  return top_layer_order;
}

void PDNGenerator::buildStripe(PGModel& pg_model)
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  Core& core = FPDM.getDatabase().get_core();
  for (PGStripe& pg_stripe : FPDM.getConfig().pg_stripe_list) {
    RoutingLayer* routing_layer = findRoutingLayer(pg_stripe.get_layer_name());
    if (routing_layer == nullptr) {
      continue;
    }

    int32_t width = FPUTIL.transMicronToDBU(pg_stripe.get_width_micron(), FPDM.getDatabase().get_micron_dbu());
    int32_t pitch = FPUTIL.transMicronToDBU(pg_stripe.get_pitch_micron(), FPDM.getDatabase().get_micron_dbu());
    int32_t offset = FPUTIL.transMicronToDBU(pg_stripe.get_offset_micron(), FPDM.getDatabase().get_micron_dbu());
    if (width <= 0 || pitch <= 0) {
      continue;
    }

    int32_t line_begin = routing_layer->get_prefer_direction() == Direction::kHorizontal ? core.get_ll_y() : core.get_ll_x();
    int32_t line_end = routing_layer->get_prefer_direction() == Direction::kHorizontal ? core.get_ur_y() : core.get_ur_x();
    int32_t start = line_begin + offset + width / 2;
    int32_t half_width = width / 2;
    int32_t half_pitch = pitch / 2;
    int32_t track_pitch = routing_layer->get_prefer_track_pitch();
    int32_t track_offset = routing_layer->get_prefer_track_offset();
    for (int32_t coord = start; coord <= line_end; coord += pitch) {
      int32_t power_coord = coord;
      if (width <= track_pitch && track_pitch > 0) {
        power_coord = (power_coord - track_offset) / track_pitch * track_pitch + track_offset;
      }
      if (power_coord - half_width < line_begin) {
        continue;
      }
      if (power_coord + half_width > line_end) {
        break;
      }
      if (routing_layer->get_prefer_direction() == Direction::kHorizontal) {
        addLineSegment(pg_model.get_default_power_net_name(), routing_layer->get_name(), PGSegmentType::kStripe, width, core.get_ll_x(), power_coord,
                       core.get_ur_x(), power_coord);
      } else {
        addLineSegment(pg_model.get_default_power_net_name(), routing_layer->get_name(), PGSegmentType::kStripe, width, power_coord, core.get_ll_y(),
                       power_coord, core.get_ur_y());
      }

      int32_t ground_coord = power_coord + half_pitch;
      if (ground_coord + half_width > line_end) {
        continue;
      }
      if (routing_layer->get_prefer_direction() == Direction::kHorizontal) {
        addLineSegment(pg_model.get_default_ground_net_name(), routing_layer->get_name(), PGSegmentType::kStripe, width, core.get_ll_x(), ground_coord,
                       core.get_ur_x(), ground_coord);
      } else {
        addLineSegment(pg_model.get_default_ground_net_name(), routing_layer->get_name(), PGSegmentType::kStripe, width, ground_coord, core.get_ll_y(),
                       ground_coord, core.get_ur_y());
      }
    }
  }

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PDNGenerator::addUncoveredEdgeStripes(PGModel& pg_model)
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  Database& database = FPDM.getDatabase();
  const Config& config = FPDM.getConfig();
  std::vector<PGSegment>& segments = database.get_pg_segment_list();
  const Core& core = database.get_core();
  const int32_t grid = std::max(1, database.get_manufacture_grid());
  const int32_t dbu = std::max(1, database.get_micron_dbu());

  std::vector<std::string> net_names;
  for (const std::string* net_name : {&pg_model.get_default_power_net_name(), &pg_model.get_default_ground_net_name()}) {
    if (!net_name->empty() && std::find(net_names.begin(), net_names.end(), *net_name) == net_names.end()) {
      net_names.push_back(*net_name);
    }
  }
  if (net_names.empty()) {
    FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
    return;
  }

  int32_t added = 0;
  for (const PGStripe& pg_stripe : config.pg_stripe_list) {
    RoutingLayer* routing_layer = findRoutingLayer(pg_stripe.get_layer_name());
    if (routing_layer == nullptr || routing_layer->get_prefer_direction() != Direction::kVertical) {
      continue;
    }
    const std::string& layer_name = routing_layer->get_name();
    const int32_t width = FPUTIL.transMicronToDBU(pg_stripe.get_width_micron(), dbu);
    const int32_t pitch = FPUTIL.transMicronToDBU(pg_stripe.get_pitch_micron(), dbu);
    if (width <= 0 || pitch <= 0) {
      continue;
    }
    const int32_t band = std::max(1, pitch / 2);
    const int32_t spacing = std::max(0, routing_layer->get_spacing());
    const int32_t min_width = std::max(1, routing_layer->get_min_width());

    std::set<std::string> reachable_layers{layer_name};
    int32_t via_span = 0;
    for (const PGLayerPair& layer_pair : config.pg_layer_pair_list) {
      std::string other_layer_name;
      if (layer_pair.get_first_layer_name() == layer_name) {
        other_layer_name = layer_pair.get_second_layer_name();
      } else if (layer_pair.get_second_layer_name() == layer_name) {
        other_layer_name = layer_pair.get_first_layer_name();
      } else {
        continue;
      }
      reachable_layers.insert(other_layer_name);
      via_span = std::max(via_span, FPI.getPGViaMinSpan(layer_name, other_layer_name));
    }
    const int32_t required_width = std::max(min_width, via_span);

    for (const bool left_edge : {true, false}) {
      const int32_t band_low = left_edge ? core.get_ll_x() : core.get_ur_x() - band;
      const int32_t band_high = left_edge ? core.get_ll_x() + band : core.get_ur_x();
      for (const std::string& net_name : net_names) {
        bool has_uncovered_rail = false;
        for (const PGSegment& rail : segments) {
          if (rail.get_type() != PGSegmentType::kFollowPin || rail.get_net_name() != net_name
              || !reachable_layers.contains(rail.get_layer_name())) {
            continue;
          }
          if (rail.get_ll_x() < band_low || rail.get_ur_x() > band_high) {
            continue;
          }
          bool covered = false;
          for (const PGSegment& stripe : segments) {
            if (stripe.get_type() != PGSegmentType::kStripe || stripe.get_net_name() != net_name || !stripe.is_vertical()
                || !reachable_layers.contains(stripe.get_layer_name())) {
              continue;
            }
            const PlanarRect overlap = getOverlapRect(segmentRect(rail), segmentRect(stripe));
            if (overlap.get_width() > 0 && overlap.get_height() > 0) {
              covered = true;
              break;
            }
          }
          if (!covered) {
            has_uncovered_rail = true;
            break;
          }
        }
        if (!has_uncovered_rail) {
          continue;
        }

        int32_t nearest_other_edge = left_edge ? INT32_MAX : INT32_MIN;
        for (const PGSegment& other : segments) {
          if (other.get_layer_name() != layer_name || !other.is_vertical() || other.get_net_name() == net_name) {
            continue;
          }
          nearest_other_edge = left_edge ? std::min(nearest_other_edge, other.get_ll_x()) : std::max(nearest_other_edge, other.get_ur_x());
        }
        int32_t available = width;
        if (left_edge && nearest_other_edge != INT32_MAX) {
          available = nearest_other_edge - core.get_ll_x() - spacing;
        } else if (!left_edge && nearest_other_edge != INT32_MIN) {
          available = core.get_ur_x() - nearest_other_edge - spacing;
        }
        int32_t candidate_width = std::min(std::max(width, required_width), available);
        candidate_width = candidate_width / grid * grid;
        if (candidate_width < required_width) {
          FPLOG.warn(Loc::current(), "Cannot add uncovered ", net_name, " edge stripe on ", layer_name, " at ", (left_edge ? "left" : "right"),
                     " core edge: available width ", available, " < required ", required_width, " DBU.");
          continue;
        }

        const int32_t x = left_edge ? core.get_ll_x() + candidate_width / 2 : core.get_ur_x() - candidate_width / 2;
        const PlanarRect candidate(x - candidate_width / 2, core.get_ll_y(), x + candidate_width / 2, core.get_ur_y());
        bool clear = true;
        for (const PGSegment& other : segments) {
          if (other.get_layer_name() != layer_name || !other.is_line() || other.get_net_name() == net_name) {
            continue;
          }
          const PlanarRect other_rect = wireRect(other);
          if (candidate.get_ll_x() - spacing < other_rect.get_ur_x() && other_rect.get_ll_x() < candidate.get_ur_x() + spacing
              && candidate.get_ll_y() < other_rect.get_ur_y() && other_rect.get_ll_y() < candidate.get_ur_y()) {
            clear = false;
            break;
          }
        }
        if (!clear) {
          FPLOG.warn(Loc::current(), "Cannot add uncovered ", net_name, " edge stripe on ", layer_name, " at ", (left_edge ? "left" : "right"),
                     " core edge: no legal spacing against existing stripes.");
          continue;
        }

        const size_t before = segments.size();
        addLineSegment(net_name, layer_name, PGSegmentType::kStripe, candidate_width, x, core.get_ll_y(), x, core.get_ur_y());
        if (segments.size() == before) {
          FPLOG.warn(Loc::current(), "Uncovered ", net_name, " edge stripe on ", layer_name, " at ", (left_edge ? "left" : "right"),
                     " core edge was fully blocked by placements.");
          continue;
        }
        ++added;
        FPLOG.info(Loc::current(), "Added uncovered ", net_name, " edge stripe on ", layer_name, " at ", (left_edge ? "left" : "right"), " core edge: x=",
                   x, " width=", candidate_width, " DBU.");
      }
    }
  }

  FPLOG.info(Loc::current(), "Added ", added, " uncovered core-edge stripe(s).");
  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PDNGenerator::alignStripeSegmentList()
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  for (PGSegment& pg_segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (pg_segment.get_type() != PGSegmentType::kStripe) {
      continue;
    }
    alignStripeSegment(pg_segment);
    extendStripeToRail(pg_segment);
  }

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PDNGenerator::alignStripeSegment(PGSegment& stripe_segment)
{
  RoutingLayer* routing_layer = findRoutingLayer(stripe_segment.get_layer_name());
  if (routing_layer == nullptr) {
    return;
  }

  bool vertical = stripe_segment.is_vertical();
  const int32_t min_length = std::max(routing_layer->get_min_width(), 1);
  for (Instance* instance_ptr : _placed_macro_list) {
    Instance& instance = *instance_ptr;
    if (routing_layer->get_order() > getMacroTopLayerOrder(instance)) {
      continue;
    }

    const PlanarRect& pg_blockage_rect = getMacroPGBlockageRect(instance);
    if (vertical) {
      if (stripe_segment.get_ur_x() <= pg_blockage_rect.get_ll_x() || pg_blockage_rect.get_ur_x() <= stripe_segment.get_ll_x()) {
        continue;
      }
      if (stripe_segment.get_start_y() == pg_blockage_rect.get_ur_y()) {
        int32_t rail_coord = getClosestRailEdgeCoord(stripe_segment, instance, true);
        if (rail_coord != INT32_MAX && rail_coord <= stripe_segment.get_end_y()) {
          int32_t cross_stripe_coord = getClosestCrossStripeEdgeCoord(stripe_segment, instance, rail_coord, true);
          if (cross_stripe_coord != INT32_MAX) {
            rail_coord = cross_stripe_coord;
          }
          const int32_t max_start = stripe_segment.get_end_y() - min_length;
          if (max_start >= stripe_segment.get_start_y()) {
            stripe_segment.set_start_y(std::min(rail_coord, max_start));
          }
        }
      }
      if (stripe_segment.get_end_y() == pg_blockage_rect.get_ll_y()) {
        int32_t rail_coord = getClosestRailEdgeCoord(stripe_segment, instance, false);
        if (rail_coord != INT32_MAX && stripe_segment.get_start_y() <= rail_coord) {
          int32_t cross_stripe_coord = getClosestCrossStripeEdgeCoord(stripe_segment, instance, rail_coord, false);
          if (cross_stripe_coord != INT32_MAX) {
            rail_coord = cross_stripe_coord;
          }
          const int32_t min_end = stripe_segment.get_start_y() + min_length;
          if (min_end <= stripe_segment.get_end_y()) {
            stripe_segment.set_end_y(std::max(rail_coord, min_end));
          }
        }
      }
    } else {
      if (stripe_segment.get_ur_y() <= pg_blockage_rect.get_ll_y() || pg_blockage_rect.get_ur_y() <= stripe_segment.get_ll_y()) {
        continue;
      }
      if (stripe_segment.get_start_x() == pg_blockage_rect.get_ur_x()) {
        int32_t rail_coord = getClosestRailEdgeCoord(stripe_segment, instance, true);
        if (rail_coord != INT32_MAX && rail_coord <= stripe_segment.get_end_x()) {
          int32_t cross_stripe_coord = getClosestCrossStripeEdgeCoord(stripe_segment, instance, rail_coord, true);
          if (cross_stripe_coord != INT32_MAX) {
            rail_coord = cross_stripe_coord;
          }
          const int32_t max_start = stripe_segment.get_end_x() - min_length;
          if (max_start >= stripe_segment.get_start_x()) {
            stripe_segment.set_start_x(std::min(rail_coord, max_start));
          }
        }
      }
      if (stripe_segment.get_end_x() == pg_blockage_rect.get_ll_x()) {
        int32_t rail_coord = getClosestRailEdgeCoord(stripe_segment, instance, false);
        if (rail_coord != INT32_MAX && stripe_segment.get_start_x() <= rail_coord) {
          int32_t cross_stripe_coord = getClosestCrossStripeEdgeCoord(stripe_segment, instance, rail_coord, false);
          if (cross_stripe_coord != INT32_MAX) {
            rail_coord = cross_stripe_coord;
          }
          const int32_t min_end = stripe_segment.get_start_x() + min_length;
          if (min_end <= stripe_segment.get_end_x()) {
            stripe_segment.set_end_x(std::max(rail_coord, min_end));
          }
        }
      }
    }
  }
}

int32_t PDNGenerator::getClosestRailEdgeCoord(PGSegment& stripe_segment, Instance& instance, bool high_side)
{
  const PlanarRect& pg_blockage_rect = getMacroPGBlockageRect(instance);
  bool vertical = stripe_segment.is_vertical();
  int32_t closest_rail_edge_coord = INT32_MAX;
  int32_t closest_distance = INT32_MAX;
  for (PGSegment& rail_segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (rail_segment.get_type() != PGSegmentType::kFollowPin || rail_segment.get_net_name() != stripe_segment.get_net_name()) {
      continue;
    }
    if (vertical) {
      if (!rail_segment.is_horizontal() || stripe_segment.get_ur_x() <= rail_segment.get_ll_x() || rail_segment.get_ur_x() <= stripe_segment.get_ll_x()) {
        continue;
      }
      if (high_side) {
        if (rail_segment.get_ll_y() < pg_blockage_rect.get_ur_y()) {
          continue;
        }
        int32_t distance = rail_segment.get_ll_y() - pg_blockage_rect.get_ur_y();
        if (distance < closest_distance) {
          closest_distance = distance;
          closest_rail_edge_coord = rail_segment.get_ll_y();
        }
      } else {
        if (pg_blockage_rect.get_ll_y() < rail_segment.get_ur_y()) {
          continue;
        }
        int32_t distance = pg_blockage_rect.get_ll_y() - rail_segment.get_ur_y();
        if (distance < closest_distance) {
          closest_distance = distance;
          closest_rail_edge_coord = rail_segment.get_ur_y();
        }
      }
    } else {
      if (!rail_segment.is_vertical() || stripe_segment.get_ur_y() <= rail_segment.get_ll_y() || rail_segment.get_ur_y() <= stripe_segment.get_ll_y()) {
        continue;
      }
      if (high_side) {
        if (rail_segment.get_ll_x() < pg_blockage_rect.get_ur_x()) {
          continue;
        }
        int32_t distance = rail_segment.get_ll_x() - pg_blockage_rect.get_ur_x();
        if (distance < closest_distance) {
          closest_distance = distance;
          closest_rail_edge_coord = rail_segment.get_ll_x();
        }
      } else {
        if (pg_blockage_rect.get_ll_x() < rail_segment.get_ur_x()) {
          continue;
        }
        int32_t distance = pg_blockage_rect.get_ll_x() - rail_segment.get_ur_x();
        if (distance < closest_distance) {
          closest_distance = distance;
          closest_rail_edge_coord = rail_segment.get_ur_x();
        }
      }
    }
  }
  return closest_rail_edge_coord;
}

int32_t PDNGenerator::getClosestCrossStripeEdgeCoord(PGSegment& stripe_segment, Instance& instance, int32_t rail_coord, bool high_side)
{
  const PlanarRect& pg_blockage_rect = getMacroPGBlockageRect(instance);
  bool vertical = stripe_segment.is_vertical();
  int32_t half_width = stripe_segment.get_width() / 2;
  int32_t stripe_coord_begin = vertical ? stripe_segment.get_start_y() : stripe_segment.get_start_x();
  int32_t stripe_coord_end = vertical ? stripe_segment.get_end_y() : stripe_segment.get_end_x();
  int32_t stripe_min_coord = std::min(stripe_coord_begin, stripe_coord_end);
  int32_t stripe_max_coord = std::max(stripe_coord_begin, stripe_coord_end);
  int32_t high_bound = rail_coord == INT32_MAX ? stripe_max_coord : std::min(rail_coord, stripe_max_coord);
  int32_t low_bound = rail_coord == INT32_MAX ? stripe_min_coord : std::max(rail_coord, stripe_min_coord);
  int32_t closest_full_overlap_coord = INT32_MAX;
  int32_t closest_full_overlap_gap_distance = INT32_MAX;
  int32_t closest_full_overlap_extension_distance = INT32_MAX;
  for (PGSegment& cross_stripe : FPDM.getDatabase().get_pg_segment_list()) {
    if (cross_stripe.get_type() != PGSegmentType::kStripe || cross_stripe.get_net_name() != stripe_segment.get_net_name()
        || (vertical && !cross_stripe.is_horizontal()) || (!vertical && !cross_stripe.is_vertical())) {
      continue;
    }

    bool connect_layers = false;
    for (PGLayerPair& pg_layer_pair : FPDM.getConfig().pg_layer_pair_list) {
      if ((pg_layer_pair.get_first_layer_name() == stripe_segment.get_layer_name() && pg_layer_pair.get_second_layer_name() == cross_stripe.get_layer_name())
          || (pg_layer_pair.get_second_layer_name() == stripe_segment.get_layer_name()
              && pg_layer_pair.get_first_layer_name() == cross_stripe.get_layer_name())) {
        connect_layers = true;
        break;
      }
    }
    if (!connect_layers) {
      continue;
    }

    int32_t full_overlap_coord = INT32_MAX;
    int32_t gap_distance = INT32_MAX;
    bool full_overlap_valid = false;
    if (vertical) {
      int32_t stripe_ll_x = stripe_segment.get_start_x() - half_width;
      int32_t stripe_ur_x = stripe_segment.get_start_x() + half_width;
      int32_t cross_begin_x = std::min(cross_stripe.get_start_x(), cross_stripe.get_end_x());
      int32_t cross_end_x = std::max(cross_stripe.get_start_x(), cross_stripe.get_end_x());
      bool full_width_overlap = cross_begin_x <= stripe_ll_x && stripe_ur_x <= cross_end_x;
      bool positive_width_overlap = std::max(stripe_ll_x, cross_stripe.get_ll_x()) < std::min(stripe_ur_x, cross_stripe.get_ur_x());
      if (!positive_width_overlap) {
        continue;
      }
      if (high_side) {
        full_overlap_coord = cross_stripe.get_ll_y();
        gap_distance = std::max(full_overlap_coord - pg_blockage_rect.get_ur_y(), 0);
        full_overlap_valid = full_width_overlap && pg_blockage_rect.get_ur_y() <= full_overlap_coord && full_overlap_coord <= high_bound;
      } else {
        full_overlap_coord = cross_stripe.get_ur_y();
        gap_distance = std::max(pg_blockage_rect.get_ll_y() - full_overlap_coord, 0);
        full_overlap_valid = full_width_overlap && low_bound <= full_overlap_coord && full_overlap_coord <= pg_blockage_rect.get_ll_y();
      }
    } else {
      int32_t stripe_ll_y = stripe_segment.get_start_y() - half_width;
      int32_t stripe_ur_y = stripe_segment.get_start_y() + half_width;
      int32_t cross_begin_y = std::min(cross_stripe.get_start_y(), cross_stripe.get_end_y());
      int32_t cross_end_y = std::max(cross_stripe.get_start_y(), cross_stripe.get_end_y());
      bool full_width_overlap = cross_begin_y <= stripe_ll_y && stripe_ur_y <= cross_end_y;
      bool positive_width_overlap = std::max(stripe_ll_y, cross_stripe.get_ll_y()) < std::min(stripe_ur_y, cross_stripe.get_ur_y());
      if (!positive_width_overlap) {
        continue;
      }
      if (high_side) {
        full_overlap_coord = cross_stripe.get_ll_x();
        gap_distance = std::max(full_overlap_coord - pg_blockage_rect.get_ur_x(), 0);
        full_overlap_valid = full_width_overlap && pg_blockage_rect.get_ur_x() <= full_overlap_coord && full_overlap_coord <= high_bound;
      } else {
        full_overlap_coord = cross_stripe.get_ur_x();
        gap_distance = std::max(pg_blockage_rect.get_ll_x() - full_overlap_coord, 0);
        full_overlap_valid = full_width_overlap && low_bound <= full_overlap_coord && full_overlap_coord <= pg_blockage_rect.get_ll_x();
      }
    }

    if (full_overlap_valid) {
      int32_t extension_distance = std::abs(full_overlap_coord - (high_side ? high_bound : low_bound));
      if (gap_distance < closest_full_overlap_gap_distance
          || (gap_distance == closest_full_overlap_gap_distance && extension_distance < closest_full_overlap_extension_distance)) {
        closest_full_overlap_gap_distance = gap_distance;
        closest_full_overlap_extension_distance = extension_distance;
        closest_full_overlap_coord = full_overlap_coord;
      }
    }
  }
  return closest_full_overlap_coord;
}

void PDNGenerator::extendStripeToRail(PGSegment& stripe_segment)
{
  std::set<std::string> connected_layer_name_set;
  for (PGLayerPair& layer_pair : FPDM.getConfig().pg_layer_pair_list) {
    if (layer_pair.get_first_layer_name() == stripe_segment.get_layer_name()) {
      connected_layer_name_set.insert(layer_pair.get_second_layer_name());
    } else if (layer_pair.get_second_layer_name() == stripe_segment.get_layer_name()) {
      connected_layer_name_set.insert(layer_pair.get_first_layer_name());
    }
  }

  for (PGSegment& rail_segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (rail_segment.get_type() != PGSegmentType::kFollowPin || rail_segment.get_net_name() != stripe_segment.get_net_name()
        || !connected_layer_name_set.contains(rail_segment.get_layer_name()) || rail_segment.is_vertical() == stripe_segment.is_vertical()) {
      continue;
    }
    PlanarRect stripe_rect(stripe_segment.get_ll_x(), stripe_segment.get_ll_y(), stripe_segment.get_ur_x(), stripe_segment.get_ur_y());
    PlanarRect rail_rect(rail_segment.get_ll_x(), rail_segment.get_ll_y(), rail_segment.get_ur_x(), rail_segment.get_ur_y());
    PlanarRect overlap_rect = getOverlapRect(stripe_rect, rail_rect);
    if (overlap_rect.get_width() <= 0 || overlap_rect.get_height() <= 0) {
      continue;
    }

    // Boundary rails straddle stripe endpoints. Cover their full width before sizing the via.
    PGSegment extended_segment = stripe_segment;
    if (stripe_segment.is_vertical()) {
      int32_t low_coord = std::min(stripe_segment.get_ll_y(), rail_segment.get_ll_y());
      int32_t high_coord = std::max(stripe_segment.get_ur_y(), rail_segment.get_ur_y());
      if (low_coord == stripe_segment.get_ll_y() && high_coord == stripe_segment.get_ur_y()) {
        continue;
      }
      bool ascending = stripe_segment.get_start_y() < stripe_segment.get_end_y();
      extended_segment.set_start_y(ascending ? low_coord : high_coord);
      extended_segment.set_end_y(ascending ? high_coord : low_coord);
    } else {
      int32_t low_coord = std::min(stripe_segment.get_ll_x(), rail_segment.get_ll_x());
      int32_t high_coord = std::max(stripe_segment.get_ur_x(), rail_segment.get_ur_x());
      if (low_coord == stripe_segment.get_ll_x() && high_coord == stripe_segment.get_ur_x()) {
        continue;
      }
      bool ascending = stripe_segment.get_start_x() < stripe_segment.get_end_x();
      extended_segment.set_start_x(ascending ? low_coord : high_coord);
      extended_segment.set_end_x(ascending ? high_coord : low_coord);
    }
    if (!getMacroBlockageIntervalList(extended_segment.get_layer_name(), extended_segment.get_width(), extended_segment.get_start_x(),
                                      extended_segment.get_start_y(), extended_segment.get_end_x(), extended_segment.get_end_y())
             .empty()) {
      continue;
    }
    stripe_segment = extended_segment;
  }
}

PlanarRect PDNGenerator::getOverlapRect(PlanarRect first_rect, PlanarRect second_rect)
{
  PlanarRect overlap_rect;
  overlap_rect.set_rect(std::max(first_rect.get_ll_x(), second_rect.get_ll_x()), std::max(first_rect.get_ll_y(), second_rect.get_ll_y()),
                        std::min(first_rect.get_ur_x(), second_rect.get_ur_x()), std::min(first_rect.get_ur_y(), second_rect.get_ur_y()));
  return overlap_rect;
}

void PDNGenerator::initPowerLayerPair(PGModel& pg_model)
{
  std::map<int32_t, std::string> stripe_layer_map;
  for (PGSegment& pg_segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (pg_segment.get_type() != PGSegmentType::kStripe) {
      continue;
    }
    RoutingLayer* routing_layer = findRoutingLayer(pg_segment.get_layer_name());
    if (routing_layer != nullptr) {
      stripe_layer_map[routing_layer->get_order()] = routing_layer->get_name();
    }
  }
  if (stripe_layer_map.size() < 2) {
    return;
  }
  std::map<int32_t, std::string>::reverse_iterator layer_iter = stripe_layer_map.rbegin();
  pg_model.get_power_layer_pair().set_second_layer_name(layer_iter->second);
  layer_iter++;
  pg_model.get_power_layer_pair().set_first_layer_name(layer_iter->second);
}

void PDNGenerator::buildRing(PGModel& pg_model)
{
  auto& database = FPDM.getDatabase();
  const auto& ring = FPDM.getConfig().pg_ring;
  if (ring.nets.empty()) {
    return;
  }
  auto* horizontal = findRoutingLayer(ring.horizontal_layer);
  auto* vertical = findRoutingLayer(ring.vertical_layer);
  const int32_t dbu = database.get_micron_dbu();
  const int32_t grid = std::max(1, database.get_manufacture_grid());
  const int32_t width = FPUTIL.transMicronToDBU(ring.width_micron, dbu);
  const int32_t spacing = FPUTIL.transMicronToDBU(ring.spacing_micron, dbu);
  const int32_t offset = FPUTIL.transMicronToDBU(ring.offset_micron, dbu);
  if (horizontal == nullptr || vertical == nullptr || horizontal == vertical || horizontal->get_prefer_direction() != Direction::kHorizontal
      || vertical->get_prefer_direction() != Direction::kVertical || width <= 0 || offset < 0 || spacing <= 0
      || width < std::max(horizontal->get_min_width(), vertical->get_min_width())
      || spacing < std::max(horizontal->get_spacing(), vertical->get_spacing()) || width % (2 * grid) || spacing % grid || offset % grid) {
    FPLOG.error(Loc::current(), "Invalid PG ring layers, width, spacing, offset or manufacturing grid.");
  }
  const auto& core = database.get_core();
  const auto& die = database.get_die();
  std::set<std::string> names;
  std::vector<PGSegment> ring_segments;
  for (size_t index = 0; index < ring.nets.size(); ++index) {
    const auto& net = ring.nets[index];
    if (!database.get_pg_net_name_to_idx_map().contains(net) || !names.insert(net).second) {
      FPLOG.error(Loc::current(), "Unknown or duplicate PG ring net '", net, "'.");
    }
    const int32_t distance = offset + width / 2 + index * (width + spacing);
    const int32_t lx = core.get_ll_x() - distance;
    const int32_t ly = core.get_ll_y() - distance;
    const int32_t ux = core.get_ur_x() + distance;
    const int32_t uy = core.get_ur_y() + distance;
    if (lx - width / 2 < die.get_ll_x() || ly - width / 2 < die.get_ll_y() || ux + width / 2 > die.get_ur_x()
        || uy + width / 2 > die.get_ur_y()) {
      FPLOG.error(Loc::current(), "PG ring '", net, "' does not fit between core and die.");
    }
    for (auto [x, y] : {std::pair{lx, ly}, {lx, uy}, {ux, ly}, {ux, uy}}) {
      if (!addViaSegment(pg_model, net, ring.horizontal_layer, ring.vertical_layer, "", x, y, width, width)) {
        FPLOG.error(Loc::current(), "Incomplete PG ring corner via for '", net, "'.");
      }
    }
    const size_t begin = database.get_pg_segment_list().size();
    addUnblockedLineSegment(net, ring.horizontal_layer, PGSegmentType::kRing, width, lx, ly, ux, ly);
    addUnblockedLineSegment(net, ring.horizontal_layer, PGSegmentType::kRing, width, lx, uy, ux, uy);
    addUnblockedLineSegment(net, ring.vertical_layer, PGSegmentType::kRing, width, lx, ly, lx, uy);
    addUnblockedLineSegment(net, ring.vertical_layer, PGSegmentType::kRing, width, ux, ly, ux, uy);
    ring_segments.insert(ring_segments.end(), database.get_pg_segment_list().begin() + begin, database.get_pg_segment_list().end());
  }
  // Extend only mesh ends reaching the core boundary; retain macro clearance at internal ends.
  for (auto& stripe : database.get_pg_segment_list()) {
    if (stripe.get_type() != PGSegmentType::kStripe) {
      continue;
    }
    for (const auto& side : ring_segments) {
      if (stripe.get_net_name() != side.get_net_name() || stripe.is_horizontal() == side.is_horizontal()) {
        continue;
      }
      if (stripe.is_horizontal() && stripe.get_layer_name() == ring.horizontal_layer) {
        if (stripe.get_start_x() <= core.get_ll_x() && side.get_start_x() < core.get_ll_x()) {
          stripe.set_start_x(side.get_start_x());
        }
        if (stripe.get_end_x() >= core.get_ur_x() && side.get_start_x() > core.get_ur_x()) {
          stripe.set_end_x(side.get_start_x());
        }
      } else if (stripe.is_vertical() && stripe.get_layer_name() == ring.vertical_layer) {
        if (stripe.get_start_y() <= core.get_ll_y() && side.get_start_y() < core.get_ll_y()) {
          stripe.set_start_y(side.get_start_y());
        }
        if (stripe.get_end_y() >= core.get_ur_y() && side.get_start_y() > core.get_ur_y()) {
          stripe.set_end_y(side.get_start_y());
        }
      }
    }
  }
}

void PDNGenerator::buildLayerConnect(PGModel& pg_model)
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  Database& database = FPDM.getDatabase();
  auto layer_pairs = FPDM.getConfig().pg_layer_pair_list;
  const auto& ring = FPDM.getConfig().pg_ring;
  if (!ring.nets.empty()) {
    PGLayerPair pair;
    pair.set_first_layer_name(ring.horizontal_layer);
    pair.set_second_layer_name(ring.vertical_layer);
    layer_pairs.push_back(pair);
  }
  // Index line segments once.  Store indices: addViaSegment() appends vias to
  // the same vector, so raw pointers could be invalidated by reallocation.
  auto& pg_segments = database.get_pg_segment_list();
  std::map<std::pair<std::string, std::string>, std::vector<size_t>> by_net_layer;
  for (size_t index = 0; index < pg_segments.size(); ++index) {
    const PGSegment& segment = pg_segments[index];
    if (segment.is_line()) {
      by_net_layer[{segment.get_net_name(), segment.get_layer_name()}].push_back(index);
    }
  }

  std::set<std::pair<std::string, std::string>> seen_pairs;
  for (PGLayerPair& pg_layer_pair : layer_pairs) {
    RoutingLayer* first_layer = findRoutingLayer(pg_layer_pair.get_first_layer_name());
    RoutingLayer* second_layer = findRoutingLayer(pg_layer_pair.get_second_layer_name());
    if (first_layer == nullptr || second_layer == nullptr || first_layer->get_prefer_direction() == second_layer->get_prefer_direction()) {
      continue;
    }
    RoutingLayer* bottom_layer = first_layer;
    RoutingLayer* top_layer = second_layer;
    if (top_layer->get_order() < bottom_layer->get_order()) {
      std::swap(bottom_layer, top_layer);
    }
    const std::string bottom_name = bottom_layer->get_name();
    const std::string top_name = top_layer->get_name();
    if (!seen_pairs.insert({bottom_name, top_name}).second) {
      continue;
    }

    for (PGNet& pg_net : database.get_pg_net_list()) {
      auto bottom_iter = by_net_layer.find({pg_net.get_name(), bottom_name});
      auto top_iter = by_net_layer.find({pg_net.get_name(), top_name});
      if (bottom_iter == by_net_layer.end() || top_iter == by_net_layer.end()) {
        continue;
      }
      for (size_t bottom_index : bottom_iter->second) {
        const PGSegment& bottom_segment = pg_segments[bottom_index];
        PlanarRect bottom_rect(bottom_segment.get_ll_x(), bottom_segment.get_ll_y(), bottom_segment.get_ur_x(), bottom_segment.get_ur_y());
        for (size_t top_index : top_iter->second) {
          const PGSegment& top_segment = pg_segments[top_index];
          PlanarRect top_rect(top_segment.get_ll_x(), top_segment.get_ll_y(), top_segment.get_ur_x(), top_segment.get_ur_y());
          PlanarRect overlap_rect = getOverlapRect(bottom_rect, top_rect);
          if (overlap_rect.get_width() <= 0 || overlap_rect.get_height() <= 0) {
            continue;
          }
          addViaSegment(pg_model, pg_net.get_name(), bottom_name, top_name, "",
                        (overlap_rect.get_ll_x() + overlap_rect.get_ur_x()) / 2, (overlap_rect.get_ll_y() + overlap_rect.get_ur_y()) / 2,
                        overlap_rect.get_width(), overlap_rect.get_height());
          int32_t x = (overlap_rect.get_ll_x() + overlap_rect.get_ur_x()) / 2;
          int32_t y = (overlap_rect.get_ll_y() + overlap_rect.get_ur_y()) / 2;
          if (bottom_layer->get_name() == pg_model.get_power_layer_pair().get_first_layer_name()
              && top_layer->get_name() == pg_model.get_power_layer_pair().get_second_layer_name() && bottom_segment.get_type() == PGSegmentType::kStripe
              && top_segment.get_type() == PGSegmentType::kStripe) {
            pg_model.get_power_net_coord_map()[pg_net.get_name()].emplace(x, y);
          }
        }
      }
    }
  }

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

bool PDNGenerator::addViaSegment(PGModel& pg_model, std::string net_name, std::string bottom_layer_name, std::string top_layer_name, std::string cut_layer_name,
                                 int32_t x, int32_t y, int32_t width, int32_t height)
{
  // The overlap of a narrow rail and a stripe is often smaller than the
  // minimum via cut.  Grow only the deficient axis; the via metal itself is
  // the cut array plus each layer's LEF enclosure, not the full overlap.
  const auto [min_width, min_height] = FPI.getPGViaMinSize(bottom_layer_name, top_layer_name);
  const bool via_rule_available = min_width > 0 && min_height > 0;
  const int32_t via_width = std::max({width, 1, min_width});
  const int32_t via_height = std::max({height, 1, min_height});
  if (via_width > width || via_height > height) {
    ++_grown_via_counts[{bottom_layer_name, top_layer_name}];
  }
  std::string via_key = FPUTIL.getString(net_name, "|", bottom_layer_name, "|", top_layer_name, "|", cut_layer_name, "|", x, "|", y, "|", via_width, "|",
                                         via_height);
  if (pg_model.get_via_key_set().contains(via_key)) {
    return true;
  }

  PGSegment pg_segment;
  pg_segment.set_net_name(net_name);
  pg_segment.set_type(PGSegmentType::kVia);
  pg_segment.set_bottom_layer_name(bottom_layer_name);
  pg_segment.set_top_layer_name(top_layer_name);
  pg_segment.set_cut_layer_name(cut_layer_name);
  pg_segment.set_start_coord(x, y);
  pg_segment.set_via_width(via_width);
  pg_segment.set_via_height(via_height);
  pg_segment.set_generated(true);
  // Unit-level PDN fixtures may not load an IDB layout.  In a real flow a
  // positive minimum span means technology rules are available, so enforce a
  // complete via stack; otherwise retain the geometry-only fallback.
  if (via_rule_available && FPI.getPGViaStack(pg_segment).empty()) {
    FPLOG.warn(Loc::current(), "Rejected PG via ", bottom_layer_name, " -> ", top_layer_name, " net=", net_name, " at (", x, ",", y,
               ") size ", via_width, "x", via_height, ".");
    ++_rejected_via_count;
    return false;
  }
  pg_model.get_via_key_set().insert(via_key);
  FPDM.getDatabase().get_pg_segment_list().push_back(pg_segment);
  return true;
}

void PDNGenerator::buildIOCoreConnect(PGModel& pg_model)
{
  Monitor monitor;
  auto& database = FPDM.getDatabase();
  auto& config = FPDM.getConfig();
  if (!config.pg_io_core_connect.enabled || database.get_pg_segment_list().empty()) {
    return;
  }

  FPLOG.info(Loc::current(), "Starting dedicated IO core PG connection...");
  FPI.adjustPGLineSegmentListByViaEnclosure();
  const int32_t grid = std::max(1, database.get_manufacture_grid());
  const int32_t configured_bridge_spacing = config.pg_io_core_connect.bridge_min_spacing_micron > 0.0
                                                ? static_cast<int32_t>(std::ceil(
                                                      config.pg_io_core_connect.bridge_min_spacing_micron
                                                      * static_cast<double>(database.get_micron_dbu())))
                                                : 0;
  const int32_t bin_size = 64 * database.get_micron_dbu();
  PGGeometryIndex geometry(bin_size);
  std::map<std::pair<std::string, std::string>, int32_t> bindings;
  for (size_t net_idx = 0; net_idx < database.get_pg_net_list().size(); ++net_idx) {
    for (const auto& terminal : database.get_pg_net_list()[net_idx].get_instance_pins()) {
      bindings[terminal] = static_cast<int32_t>(net_idx);
    }
  }

  auto is_io_core_net = [&](const std::string& net_name) {
    return std::find(config.pg_io_core_connect.nets.begin(), config.pg_io_core_connect.nets.end(), net_name)
           != config.pg_io_core_connect.nets.end();
  };
  auto snap = [&](int32_t value) { return FPUTIL.alignDown(value, grid); };
  auto add_segment_geometry = [&](PGSegment& segment) {
    addPGSegmentGeometry(geometry, database, segment);
  };
  for (auto& segment : database.get_pg_segment_list()) {
    add_segment_geometry(segment);
  }

  struct StripeTarget {
    PGSegment segment;
    size_t database_index = 0;
  };
  using StripeMap = std::map<std::string, std::vector<StripeTarget>>;
  std::map<int32_t, StripeMap> stripe_targets;
  for (size_t index = 0; index < database.get_pg_segment_list().size(); ++index) {
    const auto& segment = database.get_pg_segment_list()[index];
    if (segment.get_type() != PGSegmentType::kStripe || !is_io_core_net(segment.get_net_name())) {
      continue;
    }
    const auto net_it = database.get_pg_net_name_to_idx_map().find(segment.get_net_name());
    if (net_it == database.get_pg_net_name_to_idx_map().end()) {
      continue;
    }
    stripe_targets[net_it->second][segment.get_layer_name()].push_back({segment, index});
  }

  struct Side {
    enum Value { kLeft, kRight, kBottom, kTop } value = kBottom;
  };
  auto get_side = [&](const Instance& instance) {
    const auto& box = instance.get_bounding_rect();
    const auto& die = database.get_die();
    const int32_t left = std::abs(box.get_ll_x() - die.get_ll_x());
    const int32_t right = std::abs(die.get_ur_x() - box.get_ur_x());
    const int32_t bottom = std::abs(box.get_ll_y() - die.get_ll_y());
    const int32_t top = std::abs(die.get_ur_y() - box.get_ur_y());
    const int32_t min_distance = std::min({left, right, bottom, top});
    if (min_distance == left) return Side{Side::kLeft};
    if (min_distance == right) return Side{Side::kRight};
    if (min_distance == bottom) return Side{Side::kBottom};
    return Side{Side::kTop};
  };
  // A pad pin that reaches the core-facing edge is legal landing metal there.
  // Its OBS band sits under/adjacent to that pin and is not a routing
  // blockage for the IO extension; only package-side pin layers, which stop
  // well short of the core edge, need the OBS check.
  auto pin_reaches_core_edge = [&](const Instance& instance, const std::vector<InstancePinShape>& placed_pins,
                                   const std::string& layer_name, Side side) {
    const auto& box = instance.get_bounding_rect();
    const int32_t tolerance = std::max(1, static_cast<int32_t>(3 * database.get_micron_dbu()));
    for (const auto& shape : placed_pins) {
      if (shape.get_layer_name() != layer_name) {
        continue;
      }
      int32_t core_edge = 0;
      int32_t pin_edge = 0;
      switch (side.value) {
        case Side::kLeft:
          core_edge = box.get_ur_x();
          pin_edge = shape.get_ur_x();
          break;
        case Side::kRight:
          core_edge = box.get_ll_x();
          pin_edge = shape.get_ll_x();
          break;
        case Side::kBottom:
          core_edge = box.get_ur_y();
          pin_edge = shape.get_ur_y();
          break;
        case Side::kTop:
          core_edge = box.get_ll_y();
          pin_edge = shape.get_ll_y();
          break;
      }
      if (std::abs(core_edge - pin_edge) <= tolerance) {
        return true;
      }
    }
    return false;
  };

  std::map<std::string, std::vector<InstancePinShape>> placed_shapes;
  for (auto& instance : database.get_instance_list()) {
    if (!instance.get_placed() || instance.is_delete_instance()) {
      continue;
    }
    const auto master_it = database.get_cell_master_map().find(instance.get_master_name());
    if (master_it == database.get_cell_master_map().end()) {
      continue;
    }
    const auto& master = master_it->second;
    if (!instance.get_macro() && !master.get_pad() && !master.get_pad_filler() && !master.get_corner()) {
      continue;
    }
    auto& shapes = placed_shapes[instance.get_name()];
    shapes = FPI.getPlacedShapes(instance, master.get_pg_pin_shapes());
    for (const auto& shape : shapes) {
      const auto binding = bindings.find({instance.get_name(), shape.get_pin_name()});
      geometry.add(shape.get_layer_name(), shape, binding == bindings.end() ? -1 : binding->second);
    }
    // Index obstructions for core macros and IO pads alike.  For an IO pad,
    // obstruction_shapes also carries the non-PG terminal shapes; only the
    // LEF OBS (empty pin_name) is a blockage.  Indexing it makes the path
    // clearance keep the layer spacing from the pad OBS instead of routing
    // the IO extension through it.  Layers whose pin already reaches the core
    // edge are exempted because the pin strip itself is legal metal.
    const bool io_master = master.get_pad() || master.get_pad_filler() || master.get_corner();
    const Side side = get_side(instance);
    for (const auto& shape : FPI.getPlacedShapes(instance, master.get_obstruction_shapes())) {
      if (io_master && !shape.get_pin_name().empty()) {
        continue;
      }
      if (io_master && pin_reaches_core_edge(instance, shapes, shape.get_layer_name(), side)) {
        continue;
      }
      geometry.add(shape.get_layer_name(), shape, -1);
    }
  }
  // For an IO-to-core connection, "outermost" means the rail at the
  // core-facing edge of the IO cell.  The package-facing rail can be wide,
  // but it is commonly separated from the core rail by another PG pin and is
  // therefore not an access point for the core mesh.
  auto core_facing_coordinate = [&](const InstancePinShape& shape, Side side) {
    switch (side.value) {
      case Side::kLeft: return shape.get_ur_x();
      case Side::kRight: return -shape.get_ll_x();
      case Side::kBottom: return shape.get_ur_y();
      case Side::kTop: return -shape.get_ll_y();
    }
    return 0;
  };
  auto pin_width = [&](const InstancePinShape& pin, const RoutingLayer* layer, int32_t target_width) {
    const int32_t span = std::min(pin.get_width(), pin.get_height());
    // Preserve the stripe width; a narrower pin aperture is not a legal
    // access for this stripe and must not silently shrink the connector.
    if (target_width > span) {
      return 0;
    }
    // The target Stripe width is already generated on the technology grid;
    // preserve it exactly so the exported IOWIRE has the same cross-section.
    const int32_t width = target_width;
    return width >= layer->get_min_width() ? width : 0;
  };
  auto outermost_rail_outside_core = [&](Side side, const std::map<std::string, std::vector<InstancePinShape>>& rails,
                                         const StripeMap& targets) {
    const auto& core = database.get_core();
    const bool normal_x = side.value == Side::kLeft || side.value == Side::kRight;
    const int32_t core_low = normal_x ? core.get_ll_y() : core.get_ll_x();
    const int32_t core_high = normal_x ? core.get_ur_y() : core.get_ur_x();
    for (const auto& [layer_name, layer_rails] : rails) {
      const auto target_it = targets.find(layer_name);
      if (target_it == targets.end() || target_it->second.empty()) {
        continue;
      }
      const auto* layer = findRoutingLayer(layer_name);
      if (layer == nullptr) {
        continue;
      }
      // A fractured pin exposes several rails on the outermost edge.  A narrow
      // sliver that cannot host a wide stripe must not force the whole cell
      // onto the Manhattan policy.  "Outside core" is therefore decided by the
      // rail's tangential range, not by whether a particular stripe width
      // fits; a width mismatch only makes the layer unavailable.
      bool layer_reaches_core = false;
      bool layer_has_core_access = false;
      for (const auto& rail : layer_rails) {
        const int32_t rail_low = normal_x ? rail.get_ll_y() : rail.get_ll_x();
        const int32_t rail_high = normal_x ? rail.get_ur_y() : rail.get_ur_x();
        if (std::max(rail_low, core_low) <= std::min(rail_high, core_high)) {
          layer_reaches_core = true;
        }
        for (const auto& target : target_it->second) {
          const int32_t width = target.segment.get_width();
          if (width < layer->get_min_width() || width > std::min(rail.get_width(), rail.get_height())) {
            continue;
          }
          const int32_t first = FPUTIL.alignUp(rail_low + width / 2, grid);
          const int32_t last = FPUTIL.alignDown(rail_high - width / 2, grid);
          const int32_t overlap_low = std::max(first, core_low);
          const int32_t overlap_high = std::min(last, core_high);
          if (overlap_low <= overlap_high && FPUTIL.alignUp(overlap_low, grid) <= FPUTIL.alignDown(overlap_high, grid)) {
            layer_has_core_access = true;
            break;
          }
        }
        if (layer_has_core_access) {
          break;
        }
      }
      if (!layer_reaches_core) {
        return true;
      }
      if (!layer_has_core_access) {
        continue;
      }
    }
    return false;
  };
  auto make_line = [&](const std::string& net, const std::string& layer, int32_t width, const PlanarCoord& start,
                       const PlanarCoord& end) {
    PGSegment line;
    line.set_net_name(net);
    line.set_layer_name(layer);
    line.set_type(PGSegmentType::kIoWire);
    line.set_width(width);
    line.set_start_coord(start.get_x(), start.get_y());
    line.set_end_coord(end.get_x(), end.get_y());
    line.set_generated(true);
    return line;
  };

  struct Failure {
    std::string terminal;
    std::string reason;
  };
  std::vector<Failure> failures;
  std::vector<std::string> contact_details;
  int32_t connected_ports = 0;
  int32_t direct_connections = 0;
  int32_t bridge_connections = 0;
  int32_t bend_connections = 0;
  int32_t detour_connections = 0;
  int32_t layer_change_connections = 0;
  int32_t manhattan_direct_connections = 0;
  int32_t manhattan_bridge_connections = 0;
  int32_t pin_cross_section_failures = 0;
  int32_t stripe_cross_section_failures = 0;
  int32_t io_inner_boundary_failures = 0;
  int32_t stripe_inner_boundary_failures = 0;
  int32_t detour_endpoint_failures = 0;
  int32_t non_straight_failures = 0;

  struct LayerContacts {
    int32_t direct = 0;
    int32_t bridge = 0;
    int32_t bend = 0;
    int32_t detour = 0;
    int32_t candidates = 0;
    std::set<size_t> stripes;
    std::map<std::string, int32_t> rejected;
    int32_t count() const { return direct + bridge + detour; }
  };
  struct AccessPoint { int32_t tangent; int32_t width; int32_t separation; };
  struct PortTask {
    Instance* instance;
    int32_t port;
    int32_t net_idx;
    Side side;
    StripeMap* targets;
    std::map<std::string, std::vector<InstancePinShape>> rails;
    std::map<std::string, LayerContacts> layers;
    std::map<size_t, int32_t> routes_per_stripe;
    int32_t contacts = 0;
    std::map<std::string, std::vector<AccessPoint>> access_points;
    bool requires_manhattan = false;
  };
  std::vector<PortTask> tasks;
  // Separate IO paths must remain distinct outside the source pin/stripe.
  // Treat accepted paths as obstacles even for the same net: this prevents
  // overlapping trunks from inflating the redundant-contact count.
  PGGeometryIndex io_paths(bin_size);
  // IO cell blockages are not PG metal.  Keep routes out of the pad body so
  // the T4M2 access cannot cross the cell's own OBS near the core boundary.
  for (const auto& instance : database.get_instance_list()) {
    if (!instance.get_placed() || instance.is_delete_instance()) {
      continue;
    }
    const auto master_it = database.get_cell_master_map().find(instance.get_master_name());
    if (master_it == database.get_cell_master_map().end()) {
      continue;
    }
    const auto& master = master_it->second;
    if (!master.get_pad() && !master.get_pad_filler() && !master.get_corner()) {
      continue;
    }
    for (const auto& shape : FPI.getPlacedShapes(instance, master.get_obstruction_shapes())) {
      if (shape.get_layer_name() == "T4M2") {
        io_paths.add(shape.get_layer_name(), shape, -1);
      }
    }
  }

  auto positive_overlap = [](const PlanarRect& first, const PlanarRect& second) {
    return first.get_ll_x() < second.get_ur_x() && second.get_ll_x() < first.get_ur_x()
           && first.get_ll_y() < second.get_ur_y() && second.get_ll_y() < first.get_ur_y();
  };
  auto covers_pin_cross_section = [&](const PlanarRect& wire, const InstancePinShape& pin, Side side) {
    if (!positive_overlap(wire, pin)) {
      return false;
    }
    const bool normal_x = side.value == Side::kLeft || side.value == Side::kRight;
    // The wire leaves the pin toward the core.  It must reach the core-facing
    // edge and overlap the pin; covering the package-side rail extent is not
    // required and would make identical connections different lengths.
    if (normal_x) {
      if (side.value == Side::kLeft) {
        return wire.get_ur_x() >= pin.get_ur_x() && wire.get_ll_x() < pin.get_ur_x();
      }
      return wire.get_ll_x() <= pin.get_ll_x() && wire.get_ur_x() > pin.get_ll_x();
    }
    if (side.value == Side::kBottom) {
      return wire.get_ur_y() >= pin.get_ur_y() && wire.get_ll_y() < pin.get_ur_y();
    }
    return wire.get_ll_y() <= pin.get_ll_y() && wire.get_ur_y() > pin.get_ll_y();
  };
  auto covers_stripe_cross_section = [&](const PlanarRect& wire, const PGSegment& stripe) {
    const auto stripe_box = segmentRect(stripe);
    if (!positive_overlap(wire, stripe_box)) {
      return false;
    }
    // The wire width must cover the complete stripe width.  The longitudinal
    // overlap is intentionally positive, so an endpoint that only touches a
    // stripe edge is rejected.
    if (stripe.is_horizontal()) {
      return wire.get_ll_y() <= stripe_box.get_ll_y() && wire.get_ur_y() >= stripe_box.get_ur_y();
    }
    if (stripe.is_vertical()) {
      return wire.get_ll_x() <= stripe_box.get_ll_x() && wire.get_ur_x() >= stripe_box.get_ur_x();
    }
    return false;
  };
  // The exported IOWIRE terminates on the target stripe boundary or its
  // centerline.  Contact coverage therefore treats the wire as extending half
  // its width past that endpoint, matching the PGSegment bounding-box semantics
  // the IO connector was validated against before endpoints stopped inflating.
  auto contact_box = [](const PGSegment& segment) {
    const int32_t half_width = segment.get_width() / 2;
    return PlanarRect(std::min(segment.get_start_x(), segment.get_end_x()) - half_width, std::min(segment.get_start_y(), segment.get_end_y()) - half_width,
                      std::max(segment.get_start_x(), segment.get_end_x()) + half_width, std::max(segment.get_start_y(), segment.get_end_y()) + half_width);
  };

  for (auto& instance : database.get_instance_list()) {
    const std::string& net_name = instance.get_io_cell_net_name();
    if (!instance.get_placed() || instance.is_delete_instance() || net_name.empty() || !is_io_core_net(net_name)) {
      continue;
    }
    const auto net_it = database.get_pg_net_name_to_idx_map().find(net_name);
    if (net_it == database.get_pg_net_name_to_idx_map().end()) {
      continue;
    }
    const int32_t net_idx = net_it->second;
    const auto target_it = stripe_targets.find(net_idx);
    const auto shape_it = placed_shapes.find(instance.get_name());
    const std::string terminal = instance.get_name() + "/" + instance.get_io_cell_pin_name();
    if (target_it == stripe_targets.end() || target_it->second.empty()) {
      failures.push_back({terminal, "NO_SAME_NET_STRIPE"});
      continue;
    }
    if (shape_it == placed_shapes.end()) {
      failures.push_back({terminal, "NO_ROUTING_SHAPE"});
      continue;
    }
    const auto binding = bindings.find({instance.get_name(), instance.get_io_cell_pin_name()});
    if (binding == bindings.end() || binding->second != net_idx) {
      failures.push_back({terminal, "NET_BINDING_MISMATCH"});
      continue;
    }

    std::map<int32_t, std::vector<const InstancePinShape*>> ports;
    for (const auto& shape : shape_it->second) {
      if (shape.get_pin_name() == instance.get_io_cell_pin_name()) {
        ports[shape.get_port_idx()].push_back(&shape);
      }
    }
    bool has_core_port = false;
    for (const auto& [port, port_shapes] : ports) {
      if (std::any_of(port_shapes.begin(), port_shapes.end(), [](const auto* shape) { return shape->is_core_port(); })) {
        has_core_port = true;
        break;
      }
    }
    if (has_core_port) {
      // Core ports are the preferred access.  A non-core port may still carry
      // a layer the core ports do not (for example the package-side T4M2 rail
      // of an IO power pad); merge those layers into the core port instead of
      // dropping the whole port.
      std::set<std::string> core_layers;
      int32_t core_port_idx = -1;
      for (const auto& [port, port_shapes] : ports) {
        const bool core_port = std::any_of(port_shapes.begin(), port_shapes.end(),
                                           [](const auto* shape) { return shape->is_core_port(); });
        if (!core_port) {
          continue;
        }
        if (core_port_idx < 0) {
          core_port_idx = port;
        }
        for (const auto* shape : port_shapes) {
          core_layers.insert(shape->get_layer_name());
        }
      }
      for (auto port_it = ports.begin(); port_it != ports.end();) {
        const bool core_port = std::any_of(port_it->second.begin(), port_it->second.end(),
                                           [](const auto* shape) { return shape->is_core_port(); });
        if (core_port) {
          ++port_it;
          continue;
        }
        std::vector<const InstancePinShape*> kept;
        for (const auto* shape : port_it->second) {
          if (!core_layers.contains(shape->get_layer_name())) {
            kept.push_back(shape);
          }
        }
        if (!kept.empty() && core_port_idx >= 0) {
          auto& core_shapes = ports[core_port_idx];
          core_shapes.insert(core_shapes.end(), kept.begin(), kept.end());
        }
        port_it = ports.erase(port_it);
      }
    }
    if (ports.empty()) {
      failures.push_back({terminal, "NO_ROUTING_SHAPE"});
      continue;
    }

    const Side side = get_side(instance);
    const bool normal_x = side.value == Side::kLeft || side.value == Side::kRight;
    for (const auto& [port, shapes] : ports) {
      std::map<std::string, std::vector<const InstancePinShape*>> layer_shapes;
      for (const auto* shape : shapes) {
        if (findRoutingLayer(shape->get_layer_name()) == nullptr) continue;
        if (!config.pg_io_core_connect.pin_layers.empty()
            && std::find(config.pg_io_core_connect.pin_layers.begin(), config.pg_io_core_connect.pin_layers.end(), shape->get_layer_name())
                   == config.pg_io_core_connect.pin_layers.end()) {
          continue;
        }
        layer_shapes[shape->get_layer_name()].push_back(shape);
      }
      if (layer_shapes.empty()) {
        failures.push_back({terminal + " PORT " + std::to_string(port), "PIN_LAYER_UNAVAILABLE"});
        continue;
      }
      // LEF can fracture one rail into several abutting RECTs. Keep every
      // rectangle on the core-facing edge and merge only exact rectangular
      // unions along the tangent; never bridge a hole with a bounding box.
      std::map<std::string, std::vector<InstancePinShape>> rails;
      for (auto& [layer, candidates] : layer_shapes) {
        int32_t outer_edge = INT32_MIN;
        for (const auto* shape : candidates) outer_edge = std::max(outer_edge, core_facing_coordinate(*shape, side));
        auto normal_interval = [&](const InstancePinShape& shape) {
          return normal_x ? std::pair{shape.get_ll_x(), shape.get_ur_x()} : std::pair{shape.get_ll_y(), shape.get_ur_y()};
        };
        auto tangent_low = [&](const InstancePinShape& shape) { return normal_x ? shape.get_ll_y() : shape.get_ll_x(); };
        auto tangent_high = [&](const InstancePinShape& shape) { return normal_x ? shape.get_ur_y() : shape.get_ur_x(); };
        std::sort(candidates.begin(), candidates.end(), [&](const auto* a, const auto* b) {
          return std::pair{normal_interval(*a), tangent_low(*a)} < std::pair{normal_interval(*b), tangent_low(*b)};
        });
        for (const auto* shape : candidates) {
          if (config.pg_io_core_connect.select_outermost_pin && core_facing_coordinate(*shape, side) != outer_edge) continue;
          auto& merged = rails[layer];
          if (!merged.empty() && normal_interval(merged.back()) == normal_interval(*shape)
              && tangent_low(*shape) <= tangent_high(merged.back())) {
            if (normal_x) merged.back().set_ur_y(std::max(merged.back().get_ur_y(), shape->get_ur_y()));
            else merged.back().set_ur_x(std::max(merged.back().get_ur_x(), shape->get_ur_x()));
          } else {
            merged.push_back(*shape);
          }
        }
        candidates.clear();
        for (const auto& rail : rails[layer]) candidates.push_back(&rail);
      }
      std::set<std::string> required_layers;
      for (const auto& [layer, candidates] : layer_shapes) {
        if (!candidates.empty()) required_layers.insert(layer);
      }
      if (required_layers.empty()) {
        failures.push_back({terminal + " PORT " + std::to_string(port), "PIN_LAYER_UNAVAILABLE"});
        continue;
      }
      if (config.pg_io_core_connect.max_contacts_per_port > 0
          && config.pg_io_core_connect.max_contacts_per_port
                 < (config.pg_io_core_connect.require_each_pin_layer ? static_cast<int32_t>(required_layers.size()) : 1)) {
        failures.push_back({terminal + " PORT " + std::to_string(port), "CONTACT_LIMIT"});
        continue;
      }

      bool requires_manhattan = outermost_rail_outside_core(side, rails, target_it->second);
      for (const auto& task : tasks) {
        if (task.instance == &instance && task.requires_manhattan) {
          requires_manhattan = true;
          break;
        }
      }
      tasks.push_back({&instance, port, net_idx, side, &target_it->second, std::move(rails), {}, {}, 0, {}, requires_manhattan});
      if (requires_manhattan) {
        for (auto& task : tasks) {
          if (task.instance == &instance) task.requires_manhattan = true;
        }
      }
    }
  }

  enum class RouteMode {
    kDirectExtension,
    kStraightBridge,
    kManhattanDirectExtension,
    kManhattanBridge,
    kDetourEndOn,
    kDetourLayerChange
  };
  const auto is_direct = [](RouteMode mode) {
    return mode == RouteMode::kDirectExtension || mode == RouteMode::kManhattanDirectExtension
           || mode == RouteMode::kDetourEndOn || mode == RouteMode::kDetourLayerChange;
  };
  const auto is_bend = [](RouteMode mode) {
    return mode == RouteMode::kManhattanDirectExtension || mode == RouteMode::kManhattanBridge;
  };
  const auto is_detour = [](RouteMode mode) {
    return mode == RouteMode::kDetourEndOn || mode == RouteMode::kDetourLayerChange;
  };
  const auto is_layer_change = [](RouteMode mode) { return mode == RouteMode::kDetourLayerChange; };
  const auto route_name = [](RouteMode mode) {
    switch (mode) {
      case RouteMode::kDirectExtension: return "direct_extension";
      case RouteMode::kStraightBridge: return "straight_bridge";
      case RouteMode::kManhattanDirectExtension: return "manhattan_direct_extension";
      case RouteMode::kManhattanBridge: return "manhattan_bridge";
      case RouteMode::kDetourEndOn: return "detour_end_on";
      case RouteMode::kDetourLayerChange: return "detour_layer_change";
    }
    return "unknown";
  };

  auto snap_tangent_inside = [&](int32_t value, int32_t low, int32_t high, int32_t width) {
    const int32_t min_center = low + width / 2;
    const int32_t max_center = high - width / 2;
    if (min_center > max_center) {
      return INT32_MIN;
    }
    int32_t result = snap(value);
    if (result < min_center) result = FPUTIL.alignUp(min_center, grid);
    if (result > max_center) result = FPUTIL.alignDown(max_center, grid);
    return result >= min_center && result <= max_center ? result : INT32_MIN;
  };

  auto start_at = [&](Side side, const InstancePinShape& pin, int32_t width, int32_t tangent) {
    PlanarCoord start;
    const bool normal_x = side.value == Side::kLeft || side.value == Side::kRight;
    const int32_t tangent_coord = normal_x ? snap_tangent_inside(tangent, pin.get_ll_y(), pin.get_ur_y(), width)
                                           : snap_tangent_inside(tangent, pin.get_ll_x(), pin.get_ur_x(), width);
    if (tangent_coord == INT32_MIN) {
      return PlanarCoord(INT32_MIN, INT32_MIN);
    }

    // Start one wire width inside the core-facing edge.  Using the package
    // edge would make the intra-pin stub depend on each rail's package-side
    // extent, so identical connections would get different lengths.
    if (side.value == Side::kLeft) {
      start.set_coord(pin.get_ur_x() - width, tangent_coord);
    } else if (side.value == Side::kRight) {
      start.set_coord(pin.get_ll_x() + width, tangent_coord);
    } else if (side.value == Side::kBottom) {
      start.set_coord(tangent_coord, pin.get_ur_y() - width);
    } else {
      start.set_coord(tangent_coord, pin.get_ll_y() + width);
    }
    return start;
  };

  auto inner_boundary_point = [](Side side, const PlanarRect& box, int32_t tangent) {
    // The inner boundary is the side of the IO cell/stripe facing the core.
    // The point is kept on that boundary instead of on the centerline.  A
    // preceding collinear segment preserves the complete source cross-section
    // while making the boundary explicit in the exported DEF geometry.
    if (side.value == Side::kLeft) return PlanarCoord(box.get_ur_x(), tangent);
    if (side.value == Side::kRight) return PlanarCoord(box.get_ll_x(), tangent);
    if (side.value == Side::kBottom) return PlanarCoord(tangent, box.get_ur_y());
    return PlanarCoord(tangent, box.get_ll_y());
  };

  auto bridge_target_inner_boundary = [](Side side, const PGSegment& target, const PlanarCoord& start) {
    // A straight bridge must touch the existing stripe span.  Do not clamp a
    // tangent to a stripe endpoint: that would turn a bridge into an extension.
    if (target.is_horizontal()) {
      const int32_t x_low = target.get_ll_x() + target.get_width() / 2;
      const int32_t x_high = target.get_ur_x() - target.get_width() / 2;
      if (start.get_x() < x_low || start.get_x() > x_high) return PlanarCoord(INT32_MIN, INT32_MIN);
      return side.value == Side::kBottom ? PlanarCoord(start.get_x(), target.get_ur_y())
                                          : PlanarCoord(start.get_x(), target.get_ll_y());
    }
    if (target.is_vertical()) {
      const int32_t y_low = target.get_ll_y() + target.get_width() / 2;
      const int32_t y_high = target.get_ur_y() - target.get_width() / 2;
      if (start.get_y() < y_low || start.get_y() > y_high) return PlanarCoord(INT32_MIN, INT32_MIN);
      return side.value == Side::kLeft ? PlanarCoord(target.get_ur_x(), start.get_y())
                                       : PlanarCoord(target.get_ll_x(), start.get_y());
    }
    return PlanarCoord(INT32_MIN, INT32_MIN);
  };

  auto snap_clamped = [&](int32_t value, int32_t low, int32_t high) {
    if (low > high) {
      return INT32_MIN;
    }
    int32_t result = snap(std::clamp(value, low, high));
    if (result < low) result = FPUTIL.alignUp(low, grid);
    if (result > high) result = FPUTIL.alignDown(high, grid);
    return result >= low && result <= high ? result : INT32_MIN;
  };

  auto manhattan_target_inner_boundary = [&](Side side, const PGSegment& target, const PlanarCoord& start) {
    const int32_t half_width = target.get_width() / 2;
    if (target.is_horizontal()) {
      if (side.value == Side::kBottom || side.value == Side::kTop) {
        const int32_t tangent = snap_clamped(start.get_x(), target.get_ll_x() + half_width, target.get_ur_x() - half_width);
        if (tangent == INT32_MIN) return PlanarCoord(INT32_MIN, INT32_MIN);
        const int32_t normal = side.value == Side::kBottom ? target.get_ur_y() : target.get_ll_y();
        return PlanarCoord(tangent, normal);
      }
      const int32_t tangent = snap_clamped((target.get_ll_y() + target.get_ur_y()) / 2,
                                           target.get_ll_y() + half_width, target.get_ur_y() - half_width);
      if (tangent == INT32_MIN) return PlanarCoord(INT32_MIN, INT32_MIN);
      const int32_t normal = side.value == Side::kLeft ? target.get_start_x() : target.get_end_x();
      return PlanarCoord(normal, tangent);
    }
    if (target.is_vertical()) {
      if (side.value == Side::kLeft || side.value == Side::kRight) {
        const int32_t tangent = snap_clamped(start.get_y(), target.get_ll_y() + half_width, target.get_ur_y() - half_width);
        if (tangent == INT32_MIN) return PlanarCoord(INT32_MIN, INT32_MIN);
        const int32_t normal = side.value == Side::kLeft ? target.get_ur_x() : target.get_ll_x();
        return PlanarCoord(normal, tangent);
      }
      const int32_t tangent = snap_clamped((target.get_ll_x() + target.get_ur_x()) / 2,
                                           target.get_ll_x() + half_width, target.get_ur_x() - half_width);
      if (tangent == INT32_MIN) return PlanarCoord(INT32_MIN, INT32_MIN);
      const int32_t normal = side.value == Side::kBottom ? target.get_start_y() : target.get_end_y();
      return PlanarCoord(tangent, normal);
    }
    return PlanarCoord(INT32_MIN, INT32_MIN);
  };

  auto direct_extension_target_point = [](Side side, const PGSegment& target, const PlanarCoord& start) {
    // Continue the stripe from the end nearest the IO edge.  The pin tangent
    // only needs to be in the stripe cross-section.
    const bool normal_x = side.value == Side::kLeft || side.value == Side::kRight;
    if (target.is_horizontal() && normal_x) {
      return PlanarCoord(side.value == Side::kLeft ? target.get_start_x() : target.get_end_x(), start.get_y());
    }
    if (target.is_vertical() && !normal_x) {
      return PlanarCoord(start.get_x(), side.value == Side::kBottom ? target.get_start_y() : target.get_end_y());
    }
    return PlanarCoord(INT32_MIN, INT32_MIN);
  };

  auto manhattan_direct_extension_target_point = [](Side side, const PGSegment& target) {
    const bool normal_x = side.value == Side::kLeft || side.value == Side::kRight;
    if (target.is_horizontal() && normal_x) {
      return PlanarCoord(side.value == Side::kLeft ? target.get_start_x() : target.get_end_x(), target.get_start_y());
    }
    if (target.is_vertical() && !normal_x) {
      return PlanarCoord(target.get_start_x(), side.value == Side::kBottom ? target.get_start_y() : target.get_end_y());
    }
    return PlanarCoord(INT32_MIN, INT32_MIN);
  };

  auto stripe_end_on_point = [](Side side, const PGSegment& target) {
    // Endpoint of a stripe on the side facing the IO edge.  A detour enters
    // this endpoint from the routing margin instead of crossing the stripe.
    if (target.is_vertical()) {
      return PlanarCoord(target.get_start_x(),
                         side.value == Side::kBottom ? target.get_start_y() : target.get_end_y());
    }
    if (target.is_horizontal()) {
      return PlanarCoord(side.value == Side::kLeft ? target.get_start_x() : target.get_end_x(),
                         target.get_start_y());
    }
    return PlanarCoord(INT32_MIN, INT32_MIN);
  };

  auto detour_corridor = [&](Side side, int32_t width) {
    // Keep the lateral leg just outside the core.  All stripes start at the
    // core boundary, so moving laterally in this corridor cannot cross a
    // stripe of either net.
    const auto& core = database.get_core();
    // Keep one full wire width between the corridor and the core boundary so
    // a detour cannot touch an existing route running along the boundary.
    const int32_t margin = std::max(2 * width, 4 * grid);
    switch (side.value) {
      case Side::kBottom: return core.get_ll_y() - margin;
      case Side::kTop: return core.get_ur_y() + margin;
      case Side::kLeft: return core.get_ll_x() - margin;
      case Side::kRight: return core.get_ur_x() + margin;
    }
    return INT32_MIN;
  };

  auto detour_lateral_layer = [&](const std::string& target_layer) -> std::string {
    auto* target = findRoutingLayer(target_layer);
    if (target == nullptr) {
      return {};
    }
    const RoutingLayer* best = nullptr;
    for (const auto& layer : database.get_routing_layer_list()) {
      if (layer.get_order() < target->get_order()
          && (best == nullptr || layer.get_order() > best->get_order())) {
        best = &layer;
      }
    }
    return best == nullptr ? std::string() : best->get_name();
  };

  auto access_separation = [&](const RoutingLayer& layer, int32_t width, const PlanarCoord& start, const PlanarCoord& end) {
    int32_t separation = std::max(grid, layer.get_io_spacing());
    const int32_t gap = (std::abs(end.get_x() - start.get_x()) + std::abs(end.get_y() - start.get_y())) - width;
    if (gap > 0 && layer.get_min_enclosed_area() > 0) {
      separation = std::max(separation, static_cast<int32_t>((int64_t(layer.get_min_enclosed_area()) + gap - 1) / gap));
    }
    return std::max(separation, configured_bridge_spacing);
  };

  // Normal cells use direct extensions and straight bridges.  A cell whose
  // outermost rail has no legal access point in the core range uses the
  // corresponding Manhattan form for every connection.
  for (const RouteMode mode : {RouteMode::kDirectExtension, RouteMode::kStraightBridge,
                               RouteMode::kManhattanDirectExtension, RouteMode::kManhattanBridge,
                               RouteMode::kDetourEndOn, RouteMode::kDetourLayerChange}) {
    const bool direct = is_direct(mode);
    const bool detour = is_detour(mode);
    for (auto& task : tasks) {
      auto& instance = *task.instance;
      const auto& net_name = instance.get_io_cell_net_name();
      const int32_t net_idx = task.net_idx;
      const int32_t port = task.port;
      const Side side = task.side;
      const bool normal_x = side.value == Side::kLeft || side.value == Side::kRight;
      if ((is_bend(mode) || detour) != task.requires_manhattan) continue;
      auto can_add = [&](const std::string& layer, size_t stripe_id) {
        const auto& options = config.pg_io_core_connect;
        if (options.max_routes_per_stripe > 0 && task.routes_per_stripe[stripe_id] >= options.max_routes_per_stripe) return false;
        int32_t missing_other_layers = 0;
        for (const auto& [name, shapes] : task.rails) {
          if (name != layer && task.layers[name].count() == 0) ++missing_other_layers;
        }
        if (!options.require_each_pin_layer) missing_other_layers = 0;
        if (options.max_contacts_per_port > 0 && task.contacts + missing_other_layers >= options.max_contacts_per_port) return false;
        return options.connect_all_legal_stripes || task.contacts < options.min_contacts_per_port
               || (options.require_each_pin_layer && task.layers[layer].count() == 0);
      };
      auto try_path = [&](const InstancePinShape& pin, const StripeTarget& target, int32_t width,
                          std::vector<PlanarCoord> points, RouteMode route_mode, const std::string& lateral_layer) {
        const auto& layer_name = target.segment.get_layer_name();
        auto& stats = task.layers[layer_name];
        if (!can_add(layer_name, target.database_index)) return false;
        ++stats.candidates;
        auto reject = [&](const std::string& reason) { ++stats.rejected[reason]; return false; };
        auto* layer = findRoutingLayer(layer_name);
        if (pin.get_layer_name() != layer_name) return reject("SAME_LAYER_REQUIRED");
        if (width != target.segment.get_width() || width < layer->get_min_width()
            || (layer->get_max_width() > 0 && width > layer->get_max_width())) return reject("ILLEGAL_WIDTH");
        // An odd/off-grid width cannot be repaired by silently narrowing it.
        if (width % (2 * grid) != 0) return reject("OFF_GRID_WIDTH");
        points.erase(std::unique(points.begin(), points.end()), points.end());
        if (points.size() < 2) return reject("ALREADY_TOUCHING");
        const bool bent = is_bend(route_mode);
        const bool detour = is_detour(route_mode);
        const bool layer_change = is_layer_change(route_mode) && !lateral_layer.empty();
        const int32_t tangent = normal_x ? points.front().get_y() : points.front().get_x();
        const auto point_on_segment = [](const PlanarCoord& point, const PlanarCoord& first, const PlanarCoord& second) {
          if (first.get_x() == second.get_x()) {
            return point.get_x() == first.get_x() && point.get_y() >= std::min(first.get_y(), second.get_y())
                   && point.get_y() <= std::max(first.get_y(), second.get_y());
          }
          if (first.get_y() == second.get_y()) {
            return point.get_y() == first.get_y() && point.get_x() >= std::min(first.get_x(), second.get_x())
                   && point.get_x() <= std::max(first.get_x(), second.get_x());
          }
          return false;
        };
        const auto point_on_path = [&](const PlanarCoord& expected) {
          for (size_t index = 1; index < points.size(); ++index) {
            if (point_on_segment(expected, points[index - 1], points[index])) return true;
          }
          return false;
        };
        const auto expected_io_inner = inner_boundary_point(side, pin, tangent);
        if (!point_on_path(expected_io_inner)) {
          ++io_inner_boundary_failures;
          return reject("IO_INNER_BOUNDARY_NOT_REACHED");
        }
        if (detour) {
          if (points.size() != 4) return reject("INVALID_DETOUR_PATH");
          const auto expected_end = stripe_end_on_point(side, target.segment);
          if (expected_end.get_x() == INT32_MIN || expected_end.get_y() == INT32_MIN
              || points.back().get_x() != expected_end.get_x() || points.back().get_y() != expected_end.get_y()) {
            ++detour_endpoint_failures;
            return reject("DETOUR_ENDPOINT_NOT_REACHED");
          }
        } else if (!is_direct(route_mode)) {
          const auto expected_stripe_inner = bent ? manhattan_target_inner_boundary(side, target.segment, points.front())
                                                  : inner_boundary_point(side, segmentRect(target.segment), tangent);
          if (expected_stripe_inner.get_x() == INT32_MIN || expected_stripe_inner.get_y() == INT32_MIN) {
            ++stripe_inner_boundary_failures;
            return reject("STRIPE_INNER_BOUNDARY_NOT_REACHED");
          }
          if (points.back().get_x() != expected_stripe_inner.get_x()
              || points.back().get_y() != expected_stripe_inner.get_y()) {
            ++stripe_inner_boundary_failures;
            return reject("STRIPE_INNER_BOUNDARY_NOT_REACHED");
          }
        }
        if (detour) {
          for (size_t i = 1; i < points.size(); ++i) {
            if (points[i - 1] == points[i]) return reject("INVALID_DETOUR_PATH");
            if (points[i - 1].get_x() != points[i].get_x() && points[i - 1].get_y() != points[i].get_y()) {
              ++non_straight_failures;
              return reject("NON_STRAIGHT_PATH");
            }
          }
          const bool first_horizontal = points[0].get_y() == points[1].get_y();
          const bool second_horizontal = points[1].get_y() == points[2].get_y();
          const bool third_horizontal = points[2].get_y() == points[3].get_y();
          if (first_horizontal == second_horizontal || second_horizontal == third_horizontal) {
            ++non_straight_failures;
            return reject("NON_STRAIGHT_PATH");
          }
        } else if (bent) {
          // 3- or 4-point Manhattan path: every leg must be axis-aligned and
          // consecutive legs must alternate orientation.
          if (points.size() < 3) return reject("INVALID_MANHATTAN_PATH");
          bool previous_horizontal = false;
          for (size_t i = 1; i < points.size(); ++i) {
            const auto& previous = points[i - 1];
            const auto& current = points[i];
            const bool horizontal = previous.get_y() == current.get_y();
            const bool vertical = previous.get_x() == current.get_x();
            if (horizontal == vertical) {
              return reject("INVALID_MANHATTAN_PATH");
            }
            if (i > 1 && horizontal == previous_horizontal) {
              ++non_straight_failures;
              return reject("NON_STRAIGHT_PATH");
            }
            previous_horizontal = horizontal;
          }
        } else {
          const bool horizontal = points.front().get_y() == points.back().get_y();
          const bool vertical = points.front().get_x() == points.back().get_x();
          if (!horizontal && !vertical) {
            ++non_straight_failures;
            return reject("NON_STRAIGHT_PATH");
          }
          const int32_t first_axis = horizontal ? points.front().get_x() : points.front().get_y();
          const int32_t last_axis = horizontal ? points.back().get_x() : points.back().get_y();
          const int32_t direction = last_axis > first_axis ? 1 : (last_axis < first_axis ? -1 : 0);
          if (direction == 0) return reject("ALREADY_TOUCHING");
          for (size_t i = 1; i < points.size(); ++i) {
            const auto& previous = points[i - 1];
            const auto& current = points[i];
            if ((horizontal && current.get_y() != previous.get_y())
                || (vertical && current.get_x() != previous.get_x())) {
              ++non_straight_failures;
              return reject("NON_STRAIGHT_PATH");
            }
            const int32_t previous_axis = horizontal ? previous.get_x() : previous.get_y();
            const int32_t current_axis = horizontal ? current.get_x() : current.get_y();
            if ((direction > 0 && current_axis <= previous_axis) || (direction < 0 && current_axis >= previous_axis)) {
              return reject("BACKTRACKING_PATH");
            }
          }
        }
        const int32_t separation = access_separation(*layer, width, points.front(), points.back());
        for (const auto& access : task.access_points[layer_name]) {
          if (std::abs(tangent - access.tangent) < (width + access.width) / 2 + std::max(separation, access.separation)) {
            return reject("ACCESS_SPACING_OR_HOLE_AREA");
          }
        }
        const auto& die = database.get_die();
        auto* lateral_layer_ptr = layer_change ? findRoutingLayer(lateral_layer) : layer;
        if (lateral_layer_ptr == nullptr) return reject("DETOUR_VIA_FAILED");
        std::vector<PGSegment> path;
        for (size_t i = 1; i < points.size(); ++i) {
          const bool use_lateral = layer_change && i >= 2;
          const std::string& seg_layer_name = use_lateral ? lateral_layer : layer_name;
          auto* seg_layer = use_lateral ? lateral_layer_ptr : layer;
          const auto& a = points[i - 1];
          const auto& b = points[i];
          if ((a.get_x() != b.get_x() && a.get_y() != b.get_y())
              || a.get_x() % grid || a.get_y() % grid || b.get_x() % grid || b.get_y() % grid) return reject("OFF_GRID_PATH");
          auto line = make_line(net_name, seg_layer_name, width, a, b);
          const auto box = wireRect(line);
          if (box.get_ll_x() < die.get_ll_x() || box.get_ll_y() < die.get_ll_y()
              || box.get_ur_x() > die.get_ur_x() || box.get_ur_y() > die.get_ur_y()) return reject("OUTSIDE_DIE");
          const int64_t area = int64_t(width) * (std::abs(b.get_x() - a.get_x()) + std::abs(b.get_y() - a.get_y()) + width);
          if (area < seg_layer->get_min_area()) return reject("MIN_AREA");
          if (!geometry.clear(seg_layer_name, box, net_idx, separation)) return reject("FOREIGN_METAL_OR_OBS");
          const auto target_box = segmentRect(target.segment);
          // Only the final target-layer leg may share the target stripe metal.
          if (!io_paths.clear(seg_layer_name, box, -2, separation, use_lateral ? nullptr : &target_box)) {
            return reject("BRIDGE_BLOCKED_SPACING");
          }
          path.push_back(line);
        }
        const auto pin_contact_box = segmentRect(path.front());
        if (!covers_pin_cross_section(pin_contact_box, pin, side)) {
          ++pin_cross_section_failures;
          return reject("PIN_CROSS_SECTION_NOT_COVERED");
        }
        const auto stripe_contact_box = contact_box(path.back());
        const auto target_box = segmentRect(target.segment);
        if (detour) {
          if (!positive_overlap(stripe_contact_box, target_box)) {
            ++stripe_cross_section_failures;
            return reject("DETOUR_ENDPOINT_NOT_REACHED");
          }
        } else if (!covers_stripe_cross_section(stripe_contact_box, target.segment)) {
          ++stripe_cross_section_failures;
          return reject("STRIPE_CROSS_SECTION_NOT_COVERED");
        }
        std::string via_bottom;
        std::string via_top;
        if (layer_change) {
          if (layer->get_order() < lateral_layer_ptr->get_order()) {
            via_bottom = layer_name;
            via_top = lateral_layer;
          } else {
            via_bottom = lateral_layer;
            via_top = layer_name;
          }
          auto make_via = [&](const PlanarCoord& coord) {
            PGSegment via;
            via.set_net_name(net_name);
            via.set_type(PGSegmentType::kVia);
            via.set_bottom_layer_name(via_bottom);
            via.set_top_layer_name(via_top);
            via.set_start_coord(coord.get_x(), coord.get_y());
            via.set_via_width(width);
            via.set_via_height(width);
            via.set_generated(true);
            return via;
          };
          auto escape_via = make_via(points[1]);
          auto end_via = make_via(points.back());
          if (FPI.getPGViaStack(escape_via).empty() || FPI.getPGViaStack(end_via).empty()) {
            return reject("DETOUR_VIA_FAILED");
          }
          for (auto* via : {&escape_via, &end_via}) {
            for (const auto& shape : FPI.getPGViaShapes(*via)) {
              auto* via_layer = findRoutingLayer(shape.get_layer_name());
              if (via_layer == nullptr
                  || !geometry.clear(shape.get_layer_name(), shape, net_idx, std::max(0, via_layer->get_spacing()))) {
                return reject("DETOUR_VIA_FAILED");
              }
            }
          }
          if (!addViaSegment(pg_model, net_name, via_bottom, via_top, "", points[1].get_x(), points[1].get_y(), width, width)
              || !addViaSegment(pg_model, net_name, via_bottom, via_top, "", points.back().get_x(), points.back().get_y(), width, width)) {
            return reject("DETOUR_VIA_FAILED");
          }
        }
        for (auto& segment : path) {
          database.get_pg_segment_list().push_back(segment);
          add_segment_geometry(segment);
          io_paths.add(segment.get_layer_name(), wireRect(segment), -1);
        }
        task.access_points[layer_name].push_back({tangent, width, separation});
        ++task.contacts;
        ++task.routes_per_stripe[target.database_index];
        stats.stripes.insert(target.database_index);
        if (detour) {
          ++stats.detour;
          ++detour_connections;
          if (layer_change) {
            ++layer_change_connections;
          }
        } else if (is_direct(route_mode)) {
          ++stats.direct;
          ++direct_connections;
          if (is_bend(route_mode)) {
            ++stats.bend;
            ++bend_connections;
            ++manhattan_direct_connections;
          }
        } else {
          ++stats.bridge;
          ++bridge_connections;
          if (is_bend(route_mode)) {
            ++stats.bend;
            ++bend_connections;
            ++manhattan_bridge_connections;
          }
        }
        const auto& start = points.front();
        const auto& end = points.back();
        const auto& pin_box = pin_contact_box;
        const auto& stripe_box = stripe_contact_box;
        contact_details.push_back(FPUTIL.getString(instance.get_name(), "/", instance.get_io_cell_pin_name(), " PORT ", port,
                                                   " net=", net_name, " mode=", route_name(route_mode), " pin_layer=", pin.get_layer_name(),
                                                   " route_layer=", layer_name, " target_layer=", layer_name,
                                                   " lateral_layer=", layer_change ? lateral_layer : std::string("NA"),
                                                   " stripe_id=", target.database_index, " width=", width,
                                                   " segments=", path.size(), " spacing=", separation,
                                                   " start=(", start.get_x(), ",", start.get_y(), ") end=(", end.get_x(), ",", end.get_y(), ")",
                                                   " io_inner=(", expected_io_inner.get_x(), ",", expected_io_inner.get_y(), ")",
                                                   " stripe_inner=", is_direct(route_mode) ? std::string("NA") : FPUTIL.getString("(", end.get_x(), ",", end.get_y(), ")"),
                                                   " pin_box=", pin_box.get_ll_x(), ",", pin_box.get_ll_y(), ",", pin_box.get_ur_x(), ",", pin_box.get_ur_y(),
                                                   " stripe_box=", stripe_box.get_ll_x(), ",", stripe_box.get_ll_y(), ",", stripe_box.get_ur_x(), ",", stripe_box.get_ur_y()));
        return true;
      };

      // Distance along the IO normal only.  Stripes at different tangential
      // positions are all at the same depth and must stay available; the
      // filter only removes stripes that are deeper inside the core.
      auto target_inner_distance = [&](const InstancePinShape& pin, const StripeTarget& target) {
        const int32_t pin_core = normal_x ? (side.value == Side::kLeft ? pin.get_ur_x() : pin.get_ll_x())
                                          : (side.value == Side::kBottom ? pin.get_ur_y() : pin.get_ll_y());
        auto normal_gap = [&](const PlanarCoord& point) {
          if (point.get_x() == INT32_MIN || point.get_y() == INT32_MIN) {
            return int64_t(INT64_MAX);
          }
          return normal_x ? int64_t(std::abs(point.get_x() - pin_core)) : int64_t(std::abs(point.get_y() - pin_core));
        };
        const PlanarCoord center((pin.get_ll_x() + pin.get_ur_x()) / 2, (pin.get_ll_y() + pin.get_ur_y()) / 2);
        const auto point = bridge_target_inner_boundary(side, target.segment, center);
        const int64_t gap = normal_gap(point);
        if (gap != INT64_MAX) {
          return gap;
        }
        const auto bent = manhattan_target_inner_boundary(side, target.segment, center);
        return normal_gap(bent);
      };

      for (const auto& [layer_name, rails] : task.rails) {
        auto found = task.targets->find(layer_name);
        if (found == task.targets->end()) continue;
        // A detour is only needed when the regular direct/bridge forms failed
        // to produce any contact on this pin layer.
        if (detour && task.layers[layer_name].count() > 0) continue;
        auto targets = found->second;
        const auto& first_pin = rails.front();
        const PlanarCoord center((first_pin.get_ll_x() + first_pin.get_ur_x()) / 2, (first_pin.get_ll_y() + first_pin.get_ur_y()) / 2);
        auto distance = [&](const StripeTarget& target) {
          const auto point = bridge_target_inner_boundary(side, target.segment, center);
          if (point.get_x() != INT32_MIN && point.get_y() != INT32_MIN) {
            return int64_t(std::abs(point.get_x() - center.get_x())) + std::abs(point.get_y() - center.get_y());
          }
          const auto bent_point = manhattan_target_inner_boundary(side, target.segment, center);
          if (bent_point.get_x() == INT32_MIN || bent_point.get_y() == INT32_MIN) return int64_t(INT64_MAX);
          return int64_t(std::abs(bent_point.get_x() - center.get_x())) + std::abs(bent_point.get_y() - center.get_y());
        };
        std::stable_sort(targets.begin(), targets.end(), [&](const auto& a, const auto& b) { return distance(a) < distance(b); });
        // Each pin may only connect to its nearest stripe.  Without this, a
        // blocked near stripe would fall back to a stripe deep inside the core
        // (for example a MET4 stripe on the far side of a macro).
        std::map<const InstancePinShape*, int64_t> nearest_target_distance;
        for (const auto& pin : rails) {
          int64_t best = INT64_MAX;
          for (const auto& candidate : targets) {
            best = std::min(best, target_inner_distance(pin, candidate));
          }
          nearest_target_distance[&pin] = best;
        }
        auto* layer = findRoutingLayer(layer_name);
        for (const auto& target : targets) {
          if (detour && task.layers[layer_name].count() > 0) break;
          if (!can_add(layer_name, target.database_index)) continue;
          const bool perpendicular = normal_x ? target.segment.is_horizontal() : target.segment.is_vertical();
          if (direct && (!perpendicular || !config.pg_io_core_connect.allow_direct_extension)) continue;
          if (!config.pg_io_core_connect.allow_bridge && !direct) continue;
          // Bridges are reserved for stripes parallel to the IO edge.  The
          // Manhattan variant changes only the path geometry; it does not
          // change the direct-vs-bridge route intent.
          if (!direct && perpendicular) continue;
          for (const auto& pin : rails) {
            if (detour && task.layers[layer_name].count() > 0) break;
            if (target_inner_distance(pin, target) > nearest_target_distance.at(&pin)) {
              continue;
            }
            const int32_t width = pin_width(pin, layer, target.segment.get_width());
            if (width == 0) { ++task.layers[layer_name].rejected["PIN_NARROWER_THAN_STRIPE"]; continue; }
            const int32_t low = normal_x ? pin.get_ll_y() : pin.get_ll_x();
            const int32_t high = normal_x ? pin.get_ur_y() : pin.get_ur_x();
            std::set<int32_t> tangents;
            const int32_t first = FPUTIL.alignUp(low + width / 2, grid);
            const int32_t last = FPUTIL.alignDown(high - width / 2, grid);
            if (direct) {
              const int32_t coordinate = normal_x ? target.segment.get_start_y() : target.segment.get_start_x();
              if (is_bend(mode) || detour) {
                // Preserve the direct-extension intent even when the stripe
                // tangent is outside the IO rail.  The detour/Manhattan leg
                // moves from the nearest legal pin access point to that stripe.
                if (first <= last) {
                  const int32_t clamped = std::clamp(coordinate, first, last);
                  const int32_t access = snap_clamped(clamped, first, last);
                  if (access != INT32_MIN) tangents.insert(access);
                }
              } else if (coordinate >= first && coordinate <= last) {
                tangents.insert(coordinate);
              }
            } else {
              const auto start = start_at(side, pin, width, first);
              if (start.get_x() == INT32_MIN || start.get_y() == INT32_MIN) continue;
              const int32_t separation = std::max({grid, layer->get_io_spacing(), configured_bridge_spacing});
              const int32_t pitch = FPUTIL.alignUp(width + separation, grid);
              for (int64_t coordinate = first; coordinate <= last; coordinate += pitch) tangents.insert(static_cast<int32_t>(coordinate));

            }
            for (const int32_t tangent : tangents) {
              if (!can_add(layer_name, target.database_index)) break;
              const auto start = start_at(side, pin, width, tangent);
              if (start.get_x() == INT32_MIN || start.get_y() == INT32_MIN) continue;
              const auto io_inner = inner_boundary_point(side, pin, normal_x ? start.get_y() : start.get_x());
              const auto end = detour ? stripe_end_on_point(side, target.segment)
                                      : (direct ? (is_bend(mode) ? manhattan_direct_extension_target_point(side, target.segment)
                                                                 : direct_extension_target_point(side, target.segment, start))
                                                : (is_bend(mode) ? manhattan_target_inner_boundary(side, target.segment, start)
                                                                 : bridge_target_inner_boundary(side, target.segment, start)));
              if (end.get_x() == INT32_MIN || end.get_y() == INT32_MIN) {
                ++task.layers[layer_name].rejected[direct ? "DIRECT_EXTENSION_STRIPE_NOT_ALIGNED"
                                                         : (is_bend(mode) ? "NO_MANHATTAN_PATH" : "NO_STRAIGHT_PATH")];
                continue;
              }
              if (!is_bend(mode) && !detour
                  && ((start.get_x() != io_inner.get_x() && start.get_y() != io_inner.get_y())
                      || (io_inner.get_x() != end.get_x() && io_inner.get_y() != end.get_y()))) {
                ++task.layers[layer_name].rejected[direct ? "DIRECT_EXTENSION_NOT_COLLINEAR" : "NO_STRAIGHT_PATH"];
                continue;
              }
              if (detour) {
                const int32_t corridor = detour_corridor(side, width);
                if (corridor == INT32_MIN) continue;
                std::vector<PlanarCoord> points;
                if (normal_x) {
                  points = {start, PlanarCoord(corridor, start.get_y()), PlanarCoord(corridor, end.get_y()), end};
                } else {
                  points = {start, PlanarCoord(start.get_x(), corridor), PlanarCoord(end.get_x(), corridor), end};
                }
                std::vector<std::string> lateral_layers;
                if (is_layer_change(mode)) {
                  const std::string first = detour_lateral_layer(layer_name);
                  if (!first.empty()) lateral_layers.push_back(first);
                  const std::string second = detour_lateral_layer(first);
                  if (!second.empty()) lateral_layers.push_back(second);
                } else {
                  lateral_layers.emplace_back();
                }
                bool added = false;
                for (const auto& lateral_layer : lateral_layers) {
                  if (try_path(pin, target, width, points, mode, lateral_layer)) {
                    added = true;
                    break;
                  }
                }
                if (added) break;
              } else if (is_bend(mode)) {
                // Keep the core-facing boundary in the path.  The first form
                // leaves the pin along the normal through io_inner, jogs
                // laterally, then returns to the normal; the second form's
                // first normal leg passes through io_inner on its own.
                const PlanarCoord lateral_after_inner(end.get_x(), io_inner.get_y());
                const PlanarCoord lateral_after_start(start.get_x(), end.get_y());
                const std::vector<std::vector<PlanarCoord>> candidates = {
                    {start, io_inner, lateral_after_inner, end},
                    {start, lateral_after_start, end},
                };
                bool added = false;
                for (const auto& points : candidates) {
                  if (std::adjacent_find(points.begin(), points.end()) != points.end()) {
                    continue;
                  }
                  if (try_path(pin, target, width, points, mode, std::string())) {
                    added = true;
                    break;
                  }
                }
                if (!added) {
                  // An aligned start/end collapses every corner onto an
                  // endpoint; the straight form is the only legal geometry.
                  const PlanarCoord straight_end = direct ? direct_extension_target_point(side, target.segment, start)
                                                          : bridge_target_inner_boundary(side, target.segment, start);
                  if (straight_end.get_x() != INT32_MIN && straight_end.get_y() != INT32_MIN) {
                    try_path(pin, target, width, {start, io_inner, straight_end},
                             direct ? RouteMode::kDirectExtension : RouteMode::kStraightBridge, std::string());
                  }
                }
              } else {
                try_path(pin, target, width, std::vector<PlanarCoord>{start, io_inner, end}, mode, std::string());
              }
            }
          }
        }
      }
    }
  }
  std::vector<std::string> layer_details;
  int32_t ports_with_any_contact = 0;
  for (auto& task : tasks) {
    const std::string terminal = task.instance->get_name() + "/" + task.instance->get_io_cell_pin_name() + " PORT " + std::to_string(task.port);
    bool complete = true;
    for (const auto& [layer, rails] : task.rails) {
      const auto& stats = task.layers[layer];
      if (config.pg_io_core_connect.require_each_pin_layer && stats.count() == 0) {
        failures.push_back({terminal, "NO_LEGAL_PATH_" + layer});
        complete = false;
      }
      std::string detail = FPUTIL.getString(terminal, " policy=", task.requires_manhattan ? "manhattan" : "straight",
                                            " layer=", layer, " rails=", rails.size(), " routes=", stats.count(),
                                            " stripes=", stats.stripes.size(), " direct=", stats.direct, " bridge=", stats.bridge,
                                            " bend=", stats.bend, " detour=", stats.detour,
                                            " candidates=", stats.candidates);
      for (const auto& [reason, count] : stats.rejected) detail += FPUTIL.getString(" ", reason, "=", count);
      layer_details.push_back(std::move(detail));
    }
    if (task.contacts < config.pg_io_core_connect.min_contacts_per_port) {
      failures.push_back({terminal, "INSUFFICIENT_CONTACTS"});
      complete = false;
    }
    if (task.contacts > 0) ++ports_with_any_contact;
    if (complete) ++connected_ports;
  }
  std::set<std::string> unresolved;
  for (const auto& failure : failures) unresolved.insert(failure.terminal);
  std::set<std::string> manhattan_instances;
  for (const auto& task : tasks) {
    if (task.requires_manhattan) manhattan_instances.insert(task.instance->get_name());
  }

  FPLOG.info(Loc::current(), "IO core PG: ", connected_ports, " connected ports, ", unresolved.size(),
             " unresolved ports, ", ports_with_any_contact, " ports with any contact (direct=", direct_connections,
             ", bridge=", bridge_connections, ", bend=", bend_connections, ", detour=", detour_connections,
             ", layer_change=", layer_change_connections, ", manhattan_cells=", manhattan_instances.size(), ").");
  if (!config.pg_temp_directory_path.empty()) {
    std::ofstream report(config.pg_temp_directory_path + "/io_core_connect.rpt");
    report << "Connected ports: " << connected_ports << "\nUnresolved ports: " << unresolved.size()
           << "\nPorts with any contact: " << ports_with_any_contact << "\nDirect connections: "
           << direct_connections << "\nBridge connections: " << bridge_connections << "\n"
           << "Manhattan connections: " << bend_connections << "\n"
           << "Detour connections: " << detour_connections << "\n"
           << "Detour layer-change connections: " << layer_change_connections << "\n"
           << "Manhattan direct-extension connections: " << manhattan_direct_connections << "\n"
           << "Manhattan bridge connections: " << manhattan_bridge_connections << "\n"
           << "Manhattan policy IO cells: " << manhattan_instances.size() << "\n"
           << "Pin cross-section failures: " << pin_cross_section_failures << "\n"
           << "Stripe cross-section failures: " << stripe_cross_section_failures << "\n"
           << "IO inner-boundary failures: " << io_inner_boundary_failures << "\n"
           << "Stripe inner-boundary failures: " << stripe_inner_boundary_failures << "\n"
           << "Detour endpoint failures: " << detour_endpoint_failures << "\n"
           << "Non-straight path failures: " << non_straight_failures << "\n";
    for (const auto& failure : failures) report << failure.terminal << " " << failure.reason << "\n";
    report << "[layers]\n";
    for (const auto& detail : layer_details) report << detail << "\n";
    report << "[contacts: DBU]\n";
    for (const auto& contact : contact_details) report << contact << "\n";
  }
  if (!failures.empty()) {
    if (config.pg_io_core_connect.fail_on_unresolved) {
      FPLOG.error(Loc::current(), "IO core PG incomplete: ", unresolved.size(), " ports. See io_core_connect.rpt.");
    } else {
      FPLOG.warn(Loc::current(), "IO core PG incomplete: ", unresolved.size(), " ports; continuing. See io_core_connect.rpt.");
    }
  }
  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PDNGenerator::buildMacroConnect(PGModel& pg_model)
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");
  auto& database = FPDM.getDatabase();
  auto& config = FPDM.getConfig();
  if (database.get_pg_segment_list().empty()) {
    return;
  }
  // Validate against the geometry that will actually be exported.
  FPI.adjustPGLineSegmentListByViaEnclosure();
  PGGeometryIndex geometry(64 * database.get_micron_dbu());
  std::map<std::pair<std::string, std::string>, int32_t> bindings;
  for (size_t n = 0; n < database.get_pg_net_list().size(); ++n) {
    for (const auto& terminal : database.get_pg_net_list()[n].get_instance_pins()) {
      bindings[terminal] = n;
    }
  }
  std::map<std::string, Instance*> instances;
  std::map<std::string, std::vector<InstancePinShape>> placed_shapes;
  std::map<std::string, int32_t> instance_owner;
  int32_t next_owner = 0;
  for (auto& instance : database.get_instance_list()) {
    if (!instance.get_placed() || instance.is_delete_instance()) {
      continue;
    }
    const auto master_iter = database.get_cell_master_map().find(instance.get_master_name());
    const CellMaster* master = master_iter == database.get_cell_master_map().end() ? nullptr : &master_iter->second;
    if (!instance.get_macro() && (master == nullptr || (!master->get_pad() && !master->get_pad_filler() && !master->get_corner()))) {
      continue;
    }
    instances[instance.get_name()] = &instance;
    const int32_t owner = next_owner++;
    instance_owner[instance.get_name()] = owner;
    auto& shapes = placed_shapes[instance.get_name()];
    if (master != nullptr) {
      shapes = FPI.getPlacedShapes(instance, master->get_pg_pin_shapes());
    } else {
      // Lightweight PDN fixtures may provide placed macro pin shapes directly
      // without constructing a CellMaster/IDB database.
      shapes = instance.get_pin_shape_list();
    }
    for (const auto& shape : shapes) {
      auto binding = bindings.find({instance.get_name(), shape.get_pin_name()});
      geometry.add(shape.get_layer_name(), shape, binding == bindings.end() ? -1 : binding->second, owner);
    }
    if (master != nullptr) {
      for (const auto& shape : FPI.getPlacedShapes(instance, master->get_obstruction_shapes())) {
        geometry.add(shape.get_layer_name(), shape, -1, owner);
      }
    }
  }
  std::map<int32_t, std::vector<PGSegment>> targets;
  // A LEF PORT is internally connected. Abutting ports on the same net/layer
  // carry that connection through IO fillers and corners without redundant straps.
  using PortKey = std::tuple<std::string, std::string, int32_t>;
  std::map<PortKey, int32_t> port_ids;
  std::vector<int32_t> parent;
  std::vector<int32_t> port_nets;
  auto root = [&](int32_t node) {
    while (parent[node] != node) {
      parent[node] = parent[parent[node]];
      node = parent[node];
    }
    return node;
  };
  PGGeometryIndex port_geometry(64 * database.get_micron_dbu());
  for (const auto& [instance, shapes] : placed_shapes) {
    for (const auto& shape : shapes) {
      auto binding = bindings.find({instance, shape.get_pin_name()});
      if (binding == bindings.end()) {
        continue;
      }
      auto [entry, inserted] = port_ids.emplace(PortKey{instance, shape.get_pin_name(), shape.get_port_idx()}, parent.size());
      const int32_t id = entry->second;
      if (inserted) {
        parent.push_back(id);
        port_nets.push_back(binding->second);
      }
      for (int32_t neighbor : port_geometry.touching(shape.get_layer_name(), shape)) {
        if (port_nets[neighbor] == port_nets[id]) {
          parent[root(neighbor)] = root(id);
        }
      }
      port_geometry.add(shape.get_layer_name(), shape, id);
    }
  }
  std::set<int32_t> connected_components;
  auto index_segment = [&](PGSegment& segment) {
    const int32_t net = addPGSegmentGeometry(geometry, database, segment);
    if (net >= 0 && segment.is_line() && segment.get_type() != PGSegmentType::kFollowPin) {
      targets[net].push_back(segment);
    }
  };
  for (auto& segment : database.get_pg_segment_list()) {
    index_segment(segment);
  }
  struct Candidate {
    int64_t distance;
    size_t target;
    int32_t width;
    PlanarCoord start;
    PlanarCoord end;
    const InstancePinShape* pin;
    bool operator<(const Candidate& other) const { return distance < other.distance; }
  };
  const int32_t grid = std::max(1, database.get_manufacture_grid());
  auto snap = [&](int32_t value) { return FPUTIL.alignDown(value, grid); };
  std::map<std::pair<std::string, std::string>, int32_t> min_via_cache;
  auto minVia = [&](const std::string& bottom, const std::string& top) {
    const auto key = std::make_pair(bottom, top);
    auto it = min_via_cache.find(key);
    if (it != min_via_cache.end()) {
      return it->second;
    }
    const int32_t span = FPI.getPGViaMinSpan(bottom, top);
    min_via_cache[key] = span;
    return span;
  };
  // Innovus-style block-pin connection: when an upper-layer stripe crosses a
  // macro PG pin, drop a via at the crossing instead of routing around the
  // macro.  The pin itself is the landing metal, so its own OBS is ignored.
  std::set<std::string> crossing_via_keys;
  int32_t crossing_via_count = 0;
  auto tryCrossingVias = [&](const std::vector<const InstancePinShape*>& shapes, int32_t net_idx, int32_t owner) {
    const std::string& net = database.get_pg_net_list()[net_idx].get_name();
    int32_t legal_via_count = 0;
    for (const auto* pin : shapes) {
      auto* pin_layer = findRoutingLayer(pin->get_layer_name());
      if (pin_layer == nullptr) {
        continue;
      }
      const PlanarRect pin_rect(pin->get_ll_x(), pin->get_ll_y(), pin->get_ur_x(), pin->get_ur_y());
      struct Hit
      {
        int32_t layer_order;
        int32_t coord;
        size_t target;
      };
      std::vector<Hit> hits;
      for (size_t t = 0; t < targets[net_idx].size(); ++t) {
        const auto& target = targets[net_idx][t];
        auto* target_layer = findRoutingLayer(target.get_layer_name());
        if (target_layer == nullptr || target_layer->get_order() <= pin_layer->get_order()) {
          continue;
        }
        const PlanarRect overlap = getOverlapRect(pin_rect, segmentRect(target));
        if (overlap.get_width() <= 0 || overlap.get_height() <= 0) {
          continue;
        }
        const int32_t coord = target.is_vertical() ? target.get_start_x() : target.get_start_y();
        hits.push_back({target_layer->get_order(), coord, t});
      }
      std::sort(hits.begin(), hits.end(), [](const Hit& lhs, const Hit& rhs) {
        if (lhs.layer_order != rhs.layer_order) {
          return lhs.layer_order < rhs.layer_order;
        }
        if (lhs.coord != rhs.coord) {
          return lhs.coord < rhs.coord;
        }
        return lhs.target < rhs.target;
      });
      // Connect every crossing on the lowest upper layer.  Climbing to a
      // higher layer from the same pin would stack vias through metal that may
      // not cross the pin, and stopping after the first crossing would leave
      // the macro tied to only one stripe.
      const int32_t lowest_layer_order = hits.empty() ? -1 : hits.front().layer_order;
      for (const Hit& hit : hits) {
        if (hit.layer_order != lowest_layer_order) {
          break;
        }
        const auto& target = targets[net_idx][hit.target];
        const std::string& target_layer = target.get_layer_name();
        const int32_t rule_min_span = minVia(pin->get_layer_name(), target_layer);
        const bool via_rule_available = rule_min_span > 0;
        const int32_t min_span = via_rule_available ? rule_min_span : grid;
        const PlanarRect overlap = getOverlapRect(pin_rect, segmentRect(target));
        const int32_t pin_span = std::min(pin->get_width(), pin->get_height());
        int32_t span = FPUTIL.alignDown(std::min({overlap.get_width(), overlap.get_height(), pin_span - 2 * grid}), 2 * grid);
        if (span < min_span) {
          continue;
        }
        const int32_t x = FPUTIL.alignDown((overlap.get_ll_x() + overlap.get_ur_x()) / 2, grid);
        const int32_t y = FPUTIL.alignDown((overlap.get_ll_y() + overlap.get_ur_y()) / 2, grid);
        PGSegment via;
        via.set_net_name(net);
        via.set_type(PGSegmentType::kVia);
        via.set_bottom_layer_name(pin->get_layer_name());
        via.set_top_layer_name(target_layer);
        via.set_start_coord(x, y);
        via.set_end_coord(x, y);
        via.set_via_width(span);
        via.set_via_height(span);
        via.set_generated(true);
        const auto via_shapes = FPI.getPGViaShapes(via);
        if (via_shapes.empty() && via_rule_available) {
          continue;
        }
        bool legal = true;
        for (const auto& shape : via_shapes) {
          auto* layer = findRoutingLayer(shape.get_layer_name());
          if (layer == nullptr
              || !geometry.clear(shape.get_layer_name(), shape, net_idx, std::max(0, layer->get_spacing()), nullptr, owner)) {
            legal = false;
            break;
          }
        }
        if (!legal) {
          continue;
        }
        const std::string key = FPUTIL.getString(net, "|", pin->get_layer_name(), "|", target_layer, "|", x, "|", y, "|", span);
        if (crossing_via_keys.insert(key).second) {
          database.get_pg_segment_list().push_back(via);
          index_segment(via);
          ++crossing_via_count;
        }
        // Deduplicated vias share the same landing geometry and still connect
        // the port, so they count toward the port's connectivity.
        ++legal_via_count;
      }
    }
    return legal_via_count;
  };
  int32_t connected = 0;
  struct Pending {
    int32_t component;
    bool required;
    std::string terminal;
  };
  std::vector<Pending> pending;
  std::vector<std::string> unresolved;
  std::vector<std::string> optional_unresolved;
  for (const auto& [terminal, net_idx] : bindings) {
    auto instance_it = instances.find(terminal.first);
    if (instance_it == instances.end()) {
      continue;
    }
    auto& instance = *instance_it->second;
    const auto master_it = database.get_cell_master_map().find(instance.get_master_name());
    const bool pad = master_it != database.get_cell_master_map().end()
                     && (master_it->second.get_pad() || master_it->second.get_pad_filler() || master_it->second.get_corner());
    const auto& net_name = database.get_pg_net_list()[net_idx].get_name();
    // The dedicated connector owns the definition-file supply pin only.
    if (config.pg_io_core_connect.enabled && instance.get_io_cell_net_name() == net_name
        && instance.get_io_cell_pin_name() == terminal.second
        && std::find(config.pg_io_core_connect.nets.begin(), config.pg_io_core_connect.nets.end(), net_name)
               != config.pg_io_core_connect.nets.end()) {
      continue;
    }
    const bool required_net = config.pg_required_net_list.empty()
                              || std::find(config.pg_required_net_list.begin(), config.pg_required_net_list.end(), net_name)
                                     != config.pg_required_net_list.end();
    const bool required_terminal = required_net && (!config.pg_io_core_connect.enabled || !pad);
    if ((!instance.get_macro() && !pad) || (pad && !config.pg_connect_pads)) {
      continue;
    }
    // Different PORTs are weakly connected in LEF: each needs its own access.
    std::map<int32_t, std::vector<const InstancePinShape*>> ports;
    for (const auto& shape : placed_shapes.at(terminal.first)) {
      if (shape.get_pin_name() == terminal.second) {
        ports[shape.get_port_idx()].push_back(&shape);
      }
    }
    if (ports.empty()) {
      (required_terminal ? unresolved : optional_unresolved).push_back(terminal.first + "/" + terminal.second + " (no routing shape)");
      continue;
    }
    for (const auto& [port, shapes] : ports) {
      const int32_t component = root(port_ids.at({terminal.first, terminal.second, port}));
      if (connected_components.contains(component)) {
        ++connected;
        continue;
      }
      auto owner_it = instance_owner.find(terminal.first);
      // tryCrossingVias already applies every crossing on the lowest upper
      // layer before returning, so any legal via means the port is tied to
      // that layer and the per-port fallback route is unnecessary.
      const int32_t crossing_vias = owner_it == instance_owner.end() ? 0 : tryCrossingVias(shapes, net_idx, owner_it->second);
      if (crossing_vias > 0) {
        connected_components.insert(component);
        ++connected;
        continue;
      }
      // Keep the nearest candidates per target layer.  A single layer (for
      // example a T4M2 stripe crossing the macro) must not crowd out reachable
      // MET4/MET5 targets with larger via rules.
      constexpr int32_t kCandidatesPerLayer = 64;
      std::map<std::string, std::priority_queue<Candidate>> candidates_by_layer;
      for (const auto* pin : shapes) {
        auto* pin_layer = findRoutingLayer(pin->get_layer_name());
        if (pin_layer == nullptr) {
          continue;
        }
        const int32_t pin_span = std::min(pin->get_width(), pin->get_height());
        for (size_t t = 0; t < targets[net_idx].size(); ++t) {
          const auto& target = targets[net_idx][t];
          // Narrow access metal inside a pin aperture instead of expanding it into adjacent OBS.
          const int32_t aperture = pin_span - 2 * std::max(0, pin_layer->get_spacing());
          int32_t width = FPUTIL.alignDown(std::min(aperture >= pin_layer->get_min_width() ? aperture : pin_span, target.get_width()), 2 * grid);
          if (width < pin_layer->get_min_width() || width <= 0) {
            continue;
          }
          // A macro pin cannot host a via landing outside its own metal; skip
          // target layers whose minimum via does not fit the pin aperture.
          if (instance.get_macro()) {
            const int32_t min_via = minVia(pin->get_layer_name(), target.get_layer_name());
            if (min_via > 0 && pin_span < min_via) {
              continue;
            }
          }
          auto& candidates = candidates_by_layer[target.get_layer_name()];
          // Sample both edges as well as the center: the center can be blocked by an OBS.
          for (int32_t px : {pin->get_ll_x() + width / 2, (pin->get_ll_x() + pin->get_ur_x()) / 2, pin->get_ur_x() - width / 2}) {
            for (int32_t py : {pin->get_ll_y() + width / 2, (pin->get_ll_y() + pin->get_ur_y()) / 2, pin->get_ur_y() - width / 2}) {
              px = snap(px);
              py = snap(py);
              if (px - width / 2 < pin->get_ll_x() || py - width / 2 < pin->get_ll_y()) {
                continue;
              }
              int32_t tx = snap(std::clamp(px, std::min(target.get_start_x(), target.get_end_x()),
                                        std::max(target.get_start_x(), target.get_end_x())));
              int32_t ty = snap(std::clamp(py, std::min(target.get_start_y(), target.get_end_y()),
                                        std::max(target.get_start_y(), target.get_end_y())));
              Candidate candidate{int64_t(std::abs(tx - px)) + std::abs(ty - py), t, width, {px, py}, {tx, ty}, pin};
              candidates.push(candidate);
              if (candidates.size() > kCandidatesPerLayer) {
                candidates.pop();
              }
            }
          }
        }
      }
      std::vector<Candidate> ordered;
      for (auto& [layer, candidates] : candidates_by_layer) {
        while (!candidates.empty()) {
          ordered.push_back(candidates.top());
          candidates.pop();
        }
      }
      std::sort(ordered.begin(), ordered.end(), [](const Candidate& lhs, const Candidate& rhs) { return lhs.distance < rhs.distance; });
      bool success = false;
      for (const Candidate& candidate : ordered) {
        if (success) {
          break;
        }
        const auto target = targets[net_idx][candidate.target];
        const auto& pin_layer = candidate.pin->get_layer_name();
        const auto& net = database.get_pg_net_list()[net_idx].get_name();
        std::vector<std::string> access_layers{pin_layer};
        for (const auto& name : config.pg_access_layer_list) {
          if (findRoutingLayer(name) == nullptr) {
            FPLOG.error(Loc::current(), "Unknown PG access layer '", name, "'.");
          }
          if (std::find(access_layers.begin(), access_layers.end(), name) == access_layers.end()) {
            access_layers.push_back(name);
          }
        }
        if (std::find(access_layers.begin(), access_layers.end(), target.get_layer_name()) == access_layers.end()) {
          access_layers.push_back(target.get_layer_name());
        }
        for (const auto& layer : access_layers) {
          const int32_t route_width = std::max(candidate.width, FPUTIL.alignUp(findRoutingLayer(layer)->get_min_width(), 2 * grid));
          for (const auto& manhattan : makeManhattanPathCandidates(candidate.start, candidate.end)) {
            const PlanarCoord& bend = manhattan.bend;
            std::vector<PGSegment> path;
            for (auto [a, b] : {std::pair{candidate.start, bend}, std::pair{bend, candidate.end}}) {
              if (a.get_x() == b.get_x() && a.get_y() == b.get_y()) {
                continue;
              }
              PGSegment line;
              line.set_net_name(net);
              line.set_layer_name(layer);
              line.set_type(pad ? PGSegmentType::kIoWire : PGSegmentType::kBlockWire);
              line.set_width(route_width);
              line.set_start_coord(a.get_x(), a.get_y());
              line.set_end_coord(b.get_x(), b.get_y());
              line.set_generated(true);
              path.push_back(line);
            }
            bool legal = true;
            for (const auto& line : path) {
              if (!geometry.clear(layer, wireRect(line), net_idx, std::max(0, findRoutingLayer(layer)->get_spacing()))) {
                legal = false;
                break;
              }
            }
            auto append_via = [&](const std::string& from, const std::string& to, const PlanarCoord& coord) {
              if (from == to) {
                return true;
              }
              const int32_t rule_min_span = minVia(from, to);
              const bool via_rule_available = rule_min_span > 0;
              const int32_t via_span = std::max(route_width, via_rule_available ? rule_min_span : 1);
              PGSegment via;
              via.set_net_name(net);
              via.set_type(PGSegmentType::kVia);
              via.set_bottom_layer_name(from);
              via.set_top_layer_name(to);
              via.set_start_coord(coord.get_x(), coord.get_y());
              via.set_end_coord(coord.get_x(), coord.get_y());
              via.set_via_width(via_span);
              via.set_via_height(via_span);
              via.set_generated(true);
              const auto via_shapes = FPI.getPGViaShapes(via);
              if (via_shapes.empty() && via_rule_available) {
                return false;
              }
              for (const auto& shape : via_shapes) {
                auto* routing_layer = findRoutingLayer(shape.get_layer_name());
                if (routing_layer == nullptr || !geometry.clear(shape.get_layer_name(), shape, net_idx, std::max(0, routing_layer->get_spacing()))) {
                  return false;
                }
              }
              path.push_back(via);
              return true;
            };
            legal = legal && append_via(pin_layer, layer, candidate.start) && append_via(layer, target.get_layer_name(), candidate.end);
            if (!legal) {
              continue;
            }
            // Commit a complete path only after every wire and every via enclosure passes.
            for (auto& segment : path) {
              database.get_pg_segment_list().push_back(segment);
              index_segment(segment);
            }
            success = true;
            break;
          }
          if (success) {
            break;
          }
        }
      }
      if (success) {
        connected_components.insert(component);
        ++connected;
      } else {
        pending.push_back({component, required_terminal, terminal.first + "/" + terminal.second + " PORT " + std::to_string(port)});
      }
    }
  }
  for (const auto& item : pending) {
    if (connected_components.contains(item.component)) {
      ++connected;
    } else {
      (item.required ? unresolved : optional_unresolved).push_back(item.terminal);
    }
  }
  FPLOG.info(Loc::current(), "PG access: ", connected, " connected ports, ", unresolved.size(), " required unresolved ports, ",
             optional_unresolved.size(), " optional unresolved ports; crossing vias: ", crossing_via_count, ".");
  if (!config.pg_temp_directory_path.empty()) {
    std::ofstream report(config.pg_temp_directory_path + "/pg_connectivity.rpt");
    report << "Connected ports: " << connected << "\nUnresolved ports: " << unresolved.size()
           << "\nOptional unresolved ports: " << optional_unresolved.size() << "\n";
    for (const auto& terminal : unresolved) {
      report << terminal << "\n";
    }
    if (!optional_unresolved.empty()) {
      report << "[optional]\n";
      for (const auto& terminal : optional_unresolved) report << terminal << "\n";
    }
  }
  for (size_t i = 0; i < std::min<size_t>(unresolved.size(), 20); ++i) {
    FPLOG.warn(Loc::current(), "No legal PG access for ", unresolved[i], ".");
  }
  if (!unresolved.empty() && config.pg_require_connected_pins) {
    FPLOG.error(Loc::current(), "PG access incomplete: ", unresolved.size(), " ports. See pg_connectivity.rpt.");
  }
  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

#if 1  // output


bool PDNGenerator::isLayerPairConfigured(const std::string& first_layer_name, const std::string& second_layer_name) const
{
  for (const PGLayerPair& layer_pair : FPDM.getConfig().pg_layer_pair_list) {
    if ((layer_pair.get_first_layer_name() == first_layer_name && layer_pair.get_second_layer_name() == second_layer_name)
        || (layer_pair.get_first_layer_name() == second_layer_name && layer_pair.get_second_layer_name() == first_layer_name)) {
      return true;
    }
  }
  return false;
}

bool PDNGenerator::isRailConnectedToStripe(const PGSegment& rail)
{
  if (!rail.is_line()) {
    return true;
  }
  PlanarRect rail_rect = wireRect(rail);
  for (const PGLayerPair& layer_pair : FPDM.getConfig().pg_layer_pair_list) {
    std::string stripe_layer_name;
    if (layer_pair.get_first_layer_name() == rail.get_layer_name()) {
      stripe_layer_name = layer_pair.get_second_layer_name();
    } else if (layer_pair.get_second_layer_name() == rail.get_layer_name()) {
      stripe_layer_name = layer_pair.get_first_layer_name();
    } else {
      continue;
    }
    for (const PGSegment& stripe : FPDM.getDatabase().get_pg_segment_list()) {
      if (!stripe.is_line() || stripe.get_net_name() != rail.get_net_name() || stripe.get_layer_name() != stripe_layer_name) {
        continue;
      }
      PlanarRect overlap_rect = getOverlapRect(rail_rect, wireRect(stripe));
      if (overlap_rect.get_width() > 0 && overlap_rect.get_height() > 0) {
        return true;
      }
    }
  }
  return false;
}

std::vector<const PGSegment*> PDNGenerator::getIsolatedRailList(const std::string& net_name)
{
  std::vector<const PGSegment*> isolated_rail_list;
  for (const PGSegment& pg_segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (pg_segment.get_type() != PGSegmentType::kFollowPin || pg_segment.get_net_name() != net_name) {
      continue;
    }
    if (!isRailConnectedToStripe(pg_segment)) {
      isolated_rail_list.push_back(&pg_segment);
    }
  }
  return isolated_rail_list;
}

std::string PDNGenerator::getRepairStripeLayerName()
{
  std::string repair_layer_name;
  int32_t repair_layer_order = INT32_MAX;
  for (const PGStripe& pg_stripe : FPDM.getConfig().pg_stripe_list) {
    RoutingLayer* routing_layer = findRoutingLayer(pg_stripe.get_layer_name());
    if (routing_layer == nullptr || routing_layer->get_prefer_direction() != Direction::kVertical) {
      continue;
    }
    if (routing_layer->get_order() < repair_layer_order) {
      repair_layer_order = routing_layer->get_order();
      repair_layer_name = routing_layer->get_name();
    }
  }
  if (repair_layer_name.empty()) {
    for (const PGStripe& pg_stripe : FPDM.getConfig().pg_stripe_list) {
      RoutingLayer* routing_layer = findRoutingLayer(pg_stripe.get_layer_name());
      if (routing_layer == nullptr) {
        continue;
      }
      if (routing_layer->get_order() < repair_layer_order) {
        repair_layer_order = routing_layer->get_order();
        repair_layer_name = routing_layer->get_name();
      }
    }
  }
  return repair_layer_name;
}

std::string PDNGenerator::getRepairTopLayerName(const std::string& repair_layer_name)
{
  RoutingLayer* repair_layer = findRoutingLayer(repair_layer_name);
  if (repair_layer == nullptr) {
    return "";
  }
  std::string top_layer_name;
  int32_t top_layer_order = -1;
  for (const PGStripe& pg_stripe : FPDM.getConfig().pg_stripe_list) {
    RoutingLayer* routing_layer = findRoutingLayer(pg_stripe.get_layer_name());
    if (routing_layer == nullptr || routing_layer->get_order() <= repair_layer->get_order()
        || routing_layer->get_prefer_direction() != Direction::kHorizontal
        || !isLayerPairConfigured(repair_layer_name, routing_layer->get_name())) {
      continue;
    }
    if (routing_layer->get_order() > top_layer_order) {
      top_layer_order = routing_layer->get_order();
      top_layer_name = routing_layer->get_name();
    }
  }
  if (top_layer_name.empty()) {
    for (const PGStripe& pg_stripe : FPDM.getConfig().pg_stripe_list) {
      RoutingLayer* routing_layer = findRoutingLayer(pg_stripe.get_layer_name());
      if (routing_layer == nullptr || routing_layer->get_order() <= repair_layer->get_order()
          || !isLayerPairConfigured(repair_layer_name, routing_layer->get_name())) {
        continue;
      }
      if (routing_layer->get_order() > top_layer_order) {
        top_layer_order = routing_layer->get_order();
        top_layer_name = routing_layer->get_name();
      }
    }
  }
  return top_layer_name;
}

int32_t PDNGenerator::getRepairStripeWidth(const std::string& layer_name)
{
  int32_t micron_dbu = FPDM.getDatabase().get_micron_dbu();
  for (const PGStripe& pg_stripe : FPDM.getConfig().pg_stripe_list) {
    if (pg_stripe.get_layer_name() != layer_name) {
      continue;
    }
    int32_t width = FPUTIL.transMicronToDBU(pg_stripe.get_width_micron(), micron_dbu);
    if (width > 0) {
      return width;
    }
  }
  RoutingLayer* routing_layer = findRoutingLayer(layer_name);
  return routing_layer == nullptr ? 0 : std::max(routing_layer->get_min_width(), 1);
}

bool PDNGenerator::isRepairSegmentLegal(const std::string& net_name, const std::string& layer_name, int32_t width, int32_t x, int32_t y1, int32_t y2)
{
  if (y1 == y2 || width <= 0) {
    return false;
  }
  if (!getMacroBlockageIntervalList(layer_name, width, x, y1, x, y2).empty()) {
    return false;
  }
  RoutingLayer* routing_layer = findRoutingLayer(layer_name);
  int32_t spacing = routing_layer == nullptr ? 0 : std::max(0, routing_layer->get_spacing());
  PlanarRect repair_rect(x - width / 2, std::min(y1, y2), x + width / 2, std::max(y1, y2));
  for (const PGSegment& pg_segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (!pg_segment.is_line() || pg_segment.get_net_name() == net_name || pg_segment.get_layer_name() != layer_name) {
      continue;
    }
    PlanarRect segment_rect = wireRect(pg_segment);
    if (repair_rect.get_ll_x() - spacing < segment_rect.get_ur_x() && segment_rect.get_ll_x() < repair_rect.get_ur_x() + spacing
        && repair_rect.get_ll_y() < segment_rect.get_ur_y() && segment_rect.get_ll_y() < repair_rect.get_ur_y()) {
      return false;
    }
  }
  return true;
}

bool PDNGenerator::findRepairTopStripeY(const std::string& net_name, const std::string& top_layer_name, int32_t x, int32_t y1, int32_t y2, int32_t& top_y)
{
  int32_t center_y = (y1 + y2) / 2;
  std::vector<std::pair<int32_t, int32_t>> candidate_list;
  for (const PGSegment& pg_segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (pg_segment.get_type() != PGSegmentType::kStripe || pg_segment.get_net_name() != net_name || pg_segment.get_layer_name() != top_layer_name
        || !pg_segment.is_horizontal()) {
      continue;
    }
    PlanarRect stripe_rect = wireRect(pg_segment);
    if (stripe_rect.get_ll_x() > x || x > stripe_rect.get_ur_x()) {
      continue;
    }
    candidate_list.emplace_back(pg_segment.get_start_y(), std::abs(pg_segment.get_start_y() - center_y));
  }
  std::sort(candidate_list.begin(), candidate_list.end(), [](const auto& first, const auto& second) {
    if (first.second != second.second) {
      return first.second < second.second;
    }
    return first.first < second.first;
  });
  if (candidate_list.empty()) {
    return false;
  }
  top_y = candidate_list.front().first;
  return true;
}

void PDNGenerator::repairPGConnectivity(PGModel& pg_model)
{
  const std::string repair_layer_name = getRepairStripeLayerName();
  const std::string top_layer_name = getRepairTopLayerName(repair_layer_name);
  const int32_t repair_width = getRepairStripeWidth(repair_layer_name);
  RoutingLayer* repair_layer = findRoutingLayer(repair_layer_name);
  if (repair_layer_name.empty() || top_layer_name.empty() || repair_width <= 0 || repair_layer == nullptr) {
    return;
  }

  struct RailGroup
  {
    int32_t ll_x = 0;
    int32_t ur_x = 0;
    int32_t ll_y = 0;
    int32_t ur_y = 0;
  };

  Database& database = FPDM.getDatabase();
  int32_t max_group_gap = 20 * std::max(1, database.get_micron_dbu());
  int32_t track_pitch = repair_layer->get_prefer_track_pitch();
  int32_t track_offset = repair_layer->get_prefer_track_offset();

  for (PGNet& pg_net : database.get_pg_net_list()) {
    std::vector<const PGSegment*> isolated_rail_list = getIsolatedRailList(pg_net.get_name());
    if (isolated_rail_list.empty()) {
      continue;
    }
    if (!isLayerPairConfigured(isolated_rail_list.front()->get_layer_name(), repair_layer_name)) {
      continue;
    }

    std::sort(isolated_rail_list.begin(), isolated_rail_list.end(), [](const PGSegment* first, const PGSegment* second) {
      if (first->get_ll_x() != second->get_ll_x()) {
        return first->get_ll_x() < second->get_ll_x();
      }
      return first->get_ll_y() < second->get_ll_y();
    });

    std::vector<RailGroup> rail_group_list;
    for (const PGSegment* rail : isolated_rail_list) {
      PlanarRect rail_rect = wireRect(*rail);
      bool merged = false;
      for (RailGroup& rail_group : rail_group_list) {
        if (rail_rect.get_ll_x() <= rail_group.ur_x && rail_group.ll_x <= rail_rect.get_ur_x()
            && rail_rect.get_ll_y() <= rail_group.ur_y + max_group_gap && rail_group.ll_y <= rail_rect.get_ur_y() + max_group_gap) {
          rail_group.ll_x = std::min(rail_group.ll_x, rail_rect.get_ll_x());
          rail_group.ur_x = std::max(rail_group.ur_x, rail_rect.get_ur_x());
          rail_group.ll_y = std::min(rail_group.ll_y, rail_rect.get_ll_y());
          rail_group.ur_y = std::max(rail_group.ur_y, rail_rect.get_ur_y());
          merged = true;
          break;
        }
      }
      if (!merged) {
        rail_group_list.push_back({rail_rect.get_ll_x(), rail_rect.get_ur_x(), rail_rect.get_ll_y(), rail_rect.get_ur_y()});
      }
    }

    for (const RailGroup& rail_group : rail_group_list) {
      int32_t min_x = rail_group.ll_x + repair_width / 2;
      int32_t max_x = rail_group.ur_x - repair_width / 2;
      if (min_x > max_x) {
        continue;
      }

      std::vector<int32_t> candidate_x_list;
      int32_t center_x = (rail_group.ll_x + rail_group.ur_x) / 2;
      center_x = std::max(min_x, std::min(max_x, center_x));
      candidate_x_list.push_back(center_x);
      const int32_t repair_grid = std::max(1, database.get_manufacture_grid());
      const int32_t repair_step = std::max(repair_width, repair_grid);
      for (int32_t candidate_x = min_x; candidate_x <= max_x; candidate_x += repair_step) {
        candidate_x_list.push_back(candidate_x);
      }
      candidate_x_list.push_back(max_x);
      if (track_pitch > 0) {
        int32_t aligned_x = track_offset + (center_x - track_offset) / track_pitch * track_pitch;
        int32_t max_track_num = (max_x - min_x) / track_pitch + 1;
        for (int32_t track_idx = 0; track_idx <= max_track_num; track_idx++) {
          int32_t left_x = aligned_x - track_idx * track_pitch;
          int32_t right_x = aligned_x + track_idx * track_pitch;
          if (left_x >= min_x && left_x <= max_x) {
            candidate_x_list.push_back(left_x);
          }
          if (right_x >= min_x && right_x <= max_x) {
            candidate_x_list.push_back(right_x);
          }
          if (left_x < min_x && right_x > max_x) {
            break;
          }
        }
      }
      std::sort(candidate_x_list.begin(), candidate_x_list.end(), [center_x](int32_t first, int32_t second) {
        const int32_t first_distance = std::abs(first - center_x);
        const int32_t second_distance = std::abs(second - center_x);
        if (first_distance != second_distance) {
          return first_distance < second_distance;
        }
        return first < second;
      });
      candidate_x_list.erase(std::unique(candidate_x_list.begin(), candidate_x_list.end()), candidate_x_list.end());

      bool repaired = false;
      for (int32_t repair_x : candidate_x_list) {
        int32_t top_y = 0;
        if (!findRepairTopStripeY(pg_net.get_name(), top_layer_name, repair_x, rail_group.ll_y, rail_group.ur_y, top_y)) {
          continue;
        }
        int32_t top_stripe_half_width = getRepairStripeWidth(top_layer_name) / 2;
        int32_t repair_y1 = std::min(rail_group.ll_y, std::min(rail_group.ur_y, top_y - top_stripe_half_width));
        int32_t repair_y2 = std::max(rail_group.ll_y, std::max(rail_group.ur_y, top_y + top_stripe_half_width));
        if (!isRepairSegmentLegal(pg_net.get_name(), repair_layer_name, repair_width, repair_x, repair_y1, repair_y2)) {
          continue;
        }
        addUnblockedLineSegment(pg_net.get_name(), repair_layer_name, PGSegmentType::kStripe, repair_width, repair_x, repair_y1, repair_x, repair_y2);
        repaired = true;
        break;
      }
      if (!repaired) {
        FPLOG.warn(Loc::current(), "Failed to repair isolated PDN rail for net '", pg_net.get_name(), "' at x=[", rail_group.ll_x, ", ", rail_group.ur_x, "].");
      }
    }
  }

  buildLayerConnect(pg_model);
}

void PDNGenerator::validatePGConnectivity(PGModel& pg_model)
{
  Database& database = FPDM.getDatabase();
  int32_t isolated_rail_num = 0;
  for (const PGNet& pg_net : database.get_pg_net_list()) {
    isolated_rail_num += static_cast<int32_t>(getIsolatedRailList(pg_net.get_name()).size());
  }
  if (isolated_rail_num > 0) {
    FPLOG.warn(Loc::current(), "Found ", isolated_rail_num, " PDN follow-pin rail segment(s) without a same-net stripe connection.");
  }

  int32_t missing_rail_num = 0;
  std::string first_missing_instance_name;
  for (const Instance& instance : database.get_instance_list()) {
    if (!instance.get_placed() || instance.get_macro() || instance.is_delete_instance() || !instance.is_new_instance() || instance.get_width() <= 0
        || instance.get_height() <= 0) {
      continue;
    }
    auto master_iter = database.get_cell_master_map().find(instance.get_master_name());
    if (master_iter == database.get_cell_master_map().end()) {
      continue;
    }
    const CellMaster& master = master_iter->second;
    if (master.get_pad() || master.get_pad_filler() || master.get_corner()) {
      continue;
    }
    const Row* instance_row = nullptr;
    for (const Row& row : database.get_row_list()) {
      if (row.get_y() == instance.get_y()) {
        instance_row = &row;
        break;
      }
    }
    if (instance_row == nullptr) {
      continue;
    }
    bool flat_row = instance_row->get_orient() == PlacementOrientation::kN || instance_row->get_orient() == PlacementOrientation::kFN;
    const std::string& bottom_net_name = flat_row ? pg_model.get_default_ground_net_name() : pg_model.get_default_power_net_name();
    const std::string& top_net_name = flat_row ? pg_model.get_default_power_net_name() : pg_model.get_default_ground_net_name();
    int32_t instance_ll_x = instance.get_x();
    int32_t instance_ur_x = instance.get_x() + instance.get_width();
    auto has_rail = [&](const std::string& net_name, int32_t y_coord) {
      for (const PGSegment& pg_segment : database.get_pg_segment_list()) {
        if (pg_segment.get_type() != PGSegmentType::kFollowPin || pg_segment.get_net_name() != net_name || pg_segment.get_start_y() != y_coord) {
          continue;
        }
        PlanarRect rail_rect = wireRect(pg_segment);
        if (rail_rect.get_ll_x() <= instance_ll_x && instance_ur_x <= rail_rect.get_ur_x()) {
          return true;
        }
      }
      return false;
    };
    if (!has_rail(bottom_net_name, instance_row->get_y()) || !has_rail(top_net_name, instance_row->get_ur_y())) {
      missing_rail_num++;
      if (first_missing_instance_name.empty()) {
        first_missing_instance_name = instance.get_name();
      }
    }
  }
  if (missing_rail_num > 0) {
    FPLOG.warn(Loc::current(), "Found ", missing_rail_num, " placed phy cell(s) without complete PDN rail coverage, first: ", first_missing_instance_name, ".");
  }
}

void PDNGenerator::outputPowerLocation(PGModel& pg_model)
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  int32_t micron_dbu = FPDM.getDatabase().get_micron_dbu();
  std::ofstream* power_file = FPUTIL.getOutputFileStream(FPUTIL.getString(FPDM.getConfig().pg_temp_directory_path, "power.ploc"));
  power_file->precision(std::numeric_limits<double>::max_digits10);
  FPUTIL.pushStream(power_file, "# name x_um y_um layer net\n");
  for (auto& [net_name, coord_set] : pg_model.get_power_net_coord_map()) {
    int32_t source_idx = 0;
    for (const auto& [x, y] : coord_set) {
      FPUTIL.pushStream(power_file, net_name, "_", source_idx++, " ", x / 1.0 / micron_dbu, " ", y / 1.0 / micron_dbu, " ",
                        pg_model.get_power_layer_pair().get_second_layer_name(), " ", net_name, "\n");
    }
  }
  FPUTIL.closeFileStream(power_file);

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

#endif

// private

PDNGenerator* PDNGenerator::_pg_instance = nullptr;

}  // namespace ifp
