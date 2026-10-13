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

  void add(const std::string& layer, const PlanarRect& rect, int32_t net)
  {
    visit(rect, [&](int32_t x, int32_t y) { _layers[layer][key(x, y)].push_back({rect, net}); });
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

  bool clear(const std::string& layer, const PlanarRect& rect, int32_t net, int32_t spacing) const
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
        if (obstacle.net != net && query.get_ll_x() < box.get_ur_x() && box.get_ll_x() < query.get_ur_x()
            && query.get_ll_y() < box.get_ur_y() && box.get_ll_y() < query.get_ur_y()) {
          clear = false;
          break;
        }
      }
    });
    return clear;
  }

 private:
  struct Obstacle { PlanarRect rect; int32_t net; };
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

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PDNGenerator::generatePDN(PGModel& pg_model)
{
  buildPGNet(pg_model);
  buildRail(pg_model);
  buildStripe(pg_model);
  alignStripeSegmentList();
  buildRing(pg_model);
  buildLayerConnect(pg_model);
  buildMacroConnect(pg_model);
  trimMacroStripeEnds();
}

void PDNGenerator::trimMacroStripeEnds()
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
  for (size_t i = 0; i < segments.size(); ++i) {
    const auto& stripe = segments[i];
    if (!stripe.get_generated() || stripe.get_type() != PGSegmentType::kStripe) {
      continue;
    }
    const bool horizontal = stripe.is_horizontal();
    const int32_t half = stripe.get_width() / 2;
    Candidate candidate{i};
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

  // Index only the clipped stripes; scan contacts once, before changing any geometry.
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
      std::vector<InstancePinShape> shapes;
      for (const auto& shape : master.get_pg_pin_shapes()) {
        if (shape.get_pin_name() == pin_name
            && (by_coord.contains({net.get_name(), shape.get_layer_name(), true}) || by_coord.contains({net.get_name(), shape.get_layer_name(), false}))) {
          shapes.push_back(shape);
        }
      }
      for (const auto& shape : FPI.getPlacedShapes(instance, shapes)) {
        contact(net.get_name(), shape.get_layer_name(), shape, segments.size(), nullptr);
      }
    }
  }
  int32_t trimmed = 0;
  const int32_t grid = std::max(1, database.get_manufacture_grid());
  for (const auto& candidate : candidates) {
    if (candidate.first_contact == INT32_MAX) {
      FPLOG.warn(Loc::current(), "Clipped PG stripe has no physical contact on '", segments[candidate.index].get_net_name(), "'.");
      continue;
    }
    auto& stripe = segments[candidate.index];
    const bool horizontal = stripe.is_horizontal();
    const int32_t begin = horizontal ? stripe.get_start_x() : stripe.get_start_y();
    const int32_t end = horizontal ? stripe.get_end_x() : stripe.get_end_y();
    const int32_t new_begin = candidate.start ? std::max(begin, FPUTIL.alignDown(candidate.first_contact + stripe.get_width() / 2, grid)) : begin;
    const int32_t new_end = candidate.end ? std::min(end, FPUTIL.alignUp(candidate.last_contact - stripe.get_width() / 2, grid)) : end;
    if (new_begin >= new_end) {
      continue;
    }
    trimmed += (new_begin != begin) + (new_end != end);
    if (horizontal) {
      stripe.set_start_x(new_begin);
      stripe.set_end_x(new_end);
    } else {
      stripe.set_start_y(new_begin);
      stripe.set_end_y(new_end);
    }
  }
  FPLOG.info(Loc::current(), "Trimmed ", trimmed, " macro stripe ends while preserving physical PG contacts.");
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
    if (master == database.get_cell_master_map().end()) {
      FPLOG.error(Loc::current(), "Unknown PG cell master '", instance.get_master_name(), "'.");
    }
    auto pin_type = master->second.get_pg_pin_types().find(pin_name);
    if (pin_type == master->second.get_pg_pin_types().end() || pin_type->second != getPGNet(net_name).get_type()) {
      FPLOG.error(Loc::current(), "PG type mismatch or missing terminal '", instance.get_name(), "/", pin_name, "' on '", net_name, "'.");
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
    const auto& pin_types = database.get_cell_master_map().at(instance.get_master_name()).get_pg_pin_types();
    for (const PGGlobalConnect& rule : config.pg_connect_list) {
      if (pin_types.contains(rule.get_pin_name()) && fnmatch(rule.get_instance_pattern().c_str(), instance.get_name().c_str(), 0) == 0
          && fnmatch(rule.get_master_pattern().c_str(), instance.get_master_name().c_str(), 0) == 0) {
        bind(instance, rule.get_pin_name(), rule.get_net_name());
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
  FPLOG.info(Loc::current(), "Resolved ", bindings.size(), " instance PG terminals on ", database.get_pg_net_list().size(), " nets.");

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
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
  std::vector<PGSegment> merged_pg_segment_list;
  for (PGSegment& pg_segment : database.get_pg_segment_list()) {
    if (pg_segment.get_type() != PGSegmentType::kFollowPin || !pg_segment.is_horizontal()) {
      merged_pg_segment_list.push_back(pg_segment);
      continue;
    }

    PGSegment merged_rail_segment = pg_segment;
    for (int32_t segment_idx = 0; segment_idx < static_cast<int32_t>(merged_pg_segment_list.size());) {
      PGSegment& rail_segment = merged_pg_segment_list[segment_idx];
      if (rail_segment.get_type() != PGSegmentType::kFollowPin || !rail_segment.is_horizontal()
          || rail_segment.get_net_name() != merged_rail_segment.get_net_name() || rail_segment.get_layer_name() != merged_rail_segment.get_layer_name()
          || rail_segment.get_width() != merged_rail_segment.get_width() || rail_segment.get_start_y() != merged_rail_segment.get_start_y()) {
        segment_idx++;
        continue;
      }

      int32_t rail_start_x = std::min(rail_segment.get_start_x(), rail_segment.get_end_x());
      int32_t rail_end_x = std::max(rail_segment.get_start_x(), rail_segment.get_end_x());
      int32_t merged_start_x = std::min(merged_rail_segment.get_start_x(), merged_rail_segment.get_end_x());
      int32_t merged_end_x = std::max(merged_rail_segment.get_start_x(), merged_rail_segment.get_end_x());
      if (rail_segment.get_ur_x() <= merged_rail_segment.get_ll_x() || merged_rail_segment.get_ur_x() <= rail_segment.get_ll_x()) {
        segment_idx++;
        continue;
      }

      merged_rail_segment.set_start_coord(std::min(rail_start_x, merged_start_x), merged_rail_segment.get_start_y());
      merged_rail_segment.set_end_coord(std::max(rail_end_x, merged_end_x), merged_rail_segment.get_start_y());
      merged_pg_segment_list.erase(merged_pg_segment_list.begin() + segment_idx);
    }
    merged_pg_segment_list.push_back(merged_rail_segment);
  }
  database.set_pg_segment_list(merged_pg_segment_list);
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
    PlanarRect& routing_halo_rect = instance.get_routing_halo_rect();
    if (horizontal) {
      if (line_coord + half_width <= routing_halo_rect.get_ll_y() || routing_halo_rect.get_ur_y() <= line_coord - half_width) {
        continue;
      }
      int32_t start_coord = std::max(line_begin, routing_halo_rect.get_ll_x() - half_width);
      int32_t end_coord = std::min(line_end, routing_halo_rect.get_ur_x() + half_width);
      if (start_coord < end_coord) {
        blockage_interval_list.emplace_back(start_coord, end_coord);
      }
    } else {
      if (line_coord + half_width <= routing_halo_rect.get_ll_x() || routing_halo_rect.get_ur_x() <= line_coord - half_width) {
        continue;
      }
      int32_t start_coord = std::max(line_begin, routing_halo_rect.get_ll_y() - half_width);
      int32_t end_coord = std::min(line_end, routing_halo_rect.get_ur_y() + half_width);
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

void PDNGenerator::alignStripeSegmentList()
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  for (PGSegment& pg_segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (pg_segment.get_type() != PGSegmentType::kStripe) {
      continue;
    }
    alignStripeSegment(pg_segment);
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
  int32_t half_width = stripe_segment.get_width() / 2;
  for (Instance* instance_ptr : _placed_macro_list) {
    Instance& instance = *instance_ptr;
    if (routing_layer->get_order() > getMacroTopLayerOrder(instance)) {
      continue;
    }

    PlanarRect& routing_halo_rect = instance.get_routing_halo_rect();
    if (vertical) {
      if (stripe_segment.get_ur_x() <= routing_halo_rect.get_ll_x() || routing_halo_rect.get_ur_x() <= stripe_segment.get_ll_x()) {
        continue;
      }
      if (stripe_segment.get_start_y() == routing_halo_rect.get_ur_y() + half_width) {
        int32_t rail_coord = getClosestRailEdgeCoord(stripe_segment, instance, true);
        if (rail_coord != INT32_MAX && rail_coord <= stripe_segment.get_end_y()) {
          int32_t cross_stripe_coord = getClosestCrossStripeEdgeCoord(stripe_segment, instance, rail_coord, true);
          if (cross_stripe_coord != INT32_MAX) {
            rail_coord = cross_stripe_coord;
          }
          stripe_segment.set_start_y(rail_coord);
        }
      }
      if (stripe_segment.get_end_y() == routing_halo_rect.get_ll_y() - half_width) {
        int32_t rail_coord = getClosestRailEdgeCoord(stripe_segment, instance, false);
        if (rail_coord != INT32_MAX && stripe_segment.get_start_y() <= rail_coord) {
          int32_t cross_stripe_coord = getClosestCrossStripeEdgeCoord(stripe_segment, instance, rail_coord, false);
          if (cross_stripe_coord != INT32_MAX) {
            rail_coord = cross_stripe_coord;
          }
          stripe_segment.set_end_y(rail_coord);
        }
      }
    } else {
      if (stripe_segment.get_ur_y() <= routing_halo_rect.get_ll_y() || routing_halo_rect.get_ur_y() <= stripe_segment.get_ll_y()) {
        continue;
      }
      if (stripe_segment.get_start_x() == routing_halo_rect.get_ur_x() + half_width) {
        int32_t rail_coord = getClosestRailEdgeCoord(stripe_segment, instance, true);
        if (rail_coord != INT32_MAX && rail_coord <= stripe_segment.get_end_x()) {
          int32_t cross_stripe_coord = getClosestCrossStripeEdgeCoord(stripe_segment, instance, rail_coord, true);
          if (cross_stripe_coord != INT32_MAX) {
            rail_coord = cross_stripe_coord;
          }
          stripe_segment.set_start_x(rail_coord);
        }
      }
      if (stripe_segment.get_end_x() == routing_halo_rect.get_ll_x() - half_width) {
        int32_t rail_coord = getClosestRailEdgeCoord(stripe_segment, instance, false);
        if (rail_coord != INT32_MAX && stripe_segment.get_start_x() <= rail_coord) {
          int32_t cross_stripe_coord = getClosestCrossStripeEdgeCoord(stripe_segment, instance, rail_coord, false);
          if (cross_stripe_coord != INT32_MAX) {
            rail_coord = cross_stripe_coord;
          }
          stripe_segment.set_end_x(rail_coord);
        }
      }
    }
  }
}

int32_t PDNGenerator::getClosestRailEdgeCoord(PGSegment& stripe_segment, Instance& instance, bool high_side)
{
  PlanarRect& routing_halo_rect = instance.get_routing_halo_rect();
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
        if (rail_segment.get_ll_y() < routing_halo_rect.get_ur_y()) {
          continue;
        }
        int32_t distance = rail_segment.get_ll_y() - routing_halo_rect.get_ur_y();
        if (distance < closest_distance) {
          closest_distance = distance;
          closest_rail_edge_coord = rail_segment.get_ll_y();
        }
      } else {
        if (routing_halo_rect.get_ll_y() < rail_segment.get_ur_y()) {
          continue;
        }
        int32_t distance = routing_halo_rect.get_ll_y() - rail_segment.get_ur_y();
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
        if (rail_segment.get_ll_x() < routing_halo_rect.get_ur_x()) {
          continue;
        }
        int32_t distance = rail_segment.get_ll_x() - routing_halo_rect.get_ur_x();
        if (distance < closest_distance) {
          closest_distance = distance;
          closest_rail_edge_coord = rail_segment.get_ll_x();
        }
      } else {
        if (routing_halo_rect.get_ll_x() < rail_segment.get_ur_x()) {
          continue;
        }
        int32_t distance = routing_halo_rect.get_ll_x() - rail_segment.get_ur_x();
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
  PlanarRect& routing_halo_rect = instance.get_routing_halo_rect();
  bool vertical = stripe_segment.is_vertical();
  int32_t half_width = stripe_segment.get_width() / 2;
  int32_t closest_full_overlap_coord = INT32_MAX;
  int32_t closest_full_overlap_gap_distance = INT32_MAX;
  int32_t closest_full_overlap_extension_distance = INT32_MAX;
  int32_t closest_contact_coord = INT32_MAX;
  int32_t closest_contact_gap_distance = INT32_MAX;
  int32_t closest_contact_extension_distance = INT32_MAX;
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
    int32_t contact_coord = INT32_MAX;
    int32_t gap_distance = INT32_MAX;
    bool full_overlap_valid = false;
    bool contact_valid = false;
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
        contact_coord = cross_stripe.get_ur_y();
        gap_distance = std::max(rail_coord - cross_stripe.get_ur_y(), 0);
        full_overlap_valid = full_width_overlap && routing_halo_rect.get_ur_y() <= full_overlap_coord && full_overlap_coord <= rail_coord;
        contact_valid = routing_halo_rect.get_ur_y() <= contact_coord && contact_coord <= rail_coord;
      } else {
        full_overlap_coord = cross_stripe.get_ur_y();
        contact_coord = cross_stripe.get_ll_y();
        gap_distance = std::max(cross_stripe.get_ll_y() - rail_coord, 0);
        full_overlap_valid = full_width_overlap && rail_coord <= full_overlap_coord && full_overlap_coord <= routing_halo_rect.get_ll_y();
        contact_valid = rail_coord <= contact_coord && contact_coord <= routing_halo_rect.get_ll_y();
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
        contact_coord = cross_stripe.get_ur_x();
        gap_distance = std::max(rail_coord - cross_stripe.get_ur_x(), 0);
        full_overlap_valid = full_width_overlap && routing_halo_rect.get_ur_x() <= full_overlap_coord && full_overlap_coord <= rail_coord;
        contact_valid = routing_halo_rect.get_ur_x() <= contact_coord && contact_coord <= rail_coord;
      } else {
        full_overlap_coord = cross_stripe.get_ur_x();
        contact_coord = cross_stripe.get_ll_x();
        gap_distance = std::max(cross_stripe.get_ll_x() - rail_coord, 0);
        full_overlap_valid = full_width_overlap && rail_coord <= full_overlap_coord && full_overlap_coord <= routing_halo_rect.get_ll_x();
        contact_valid = rail_coord <= contact_coord && contact_coord <= routing_halo_rect.get_ll_x();
      }
    }

    // Repair only the small gaps that buildLayerConnect already treats as overlaps because it expands line endpoints by half the width.
    contact_valid = contact_valid && 0 < gap_distance && gap_distance < half_width;
    if (full_overlap_valid) {
      int32_t extension_distance = std::abs(full_overlap_coord - rail_coord);
      if (gap_distance < closest_full_overlap_gap_distance
          || (gap_distance == closest_full_overlap_gap_distance && extension_distance < closest_full_overlap_extension_distance)) {
        closest_full_overlap_gap_distance = gap_distance;
        closest_full_overlap_extension_distance = extension_distance;
        closest_full_overlap_coord = full_overlap_coord;
      }
    }
    if (contact_valid) {
      int32_t extension_distance = std::abs(contact_coord - rail_coord);
      if (gap_distance < closest_contact_gap_distance
          || (gap_distance == closest_contact_gap_distance && extension_distance < closest_contact_extension_distance)) {
        closest_contact_gap_distance = gap_distance;
        closest_contact_extension_distance = extension_distance;
        closest_contact_coord = contact_coord;
      }
    }
  }
  return closest_full_overlap_coord != INT32_MAX ? closest_full_overlap_coord : closest_contact_coord;
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

    std::vector<PGSegment> pg_segment_list = database.get_pg_segment_list();
    for (PGNet& pg_net : database.get_pg_net_list()) {
      std::vector<PGSegment> bottom_segment_list;
      std::vector<PGSegment> top_segment_list;
      for (PGSegment& pg_segment : pg_segment_list) {
        if (!pg_segment.is_line() || pg_segment.get_net_name() != pg_net.get_name()) {
          continue;
        }
        if (pg_segment.get_layer_name() == bottom_layer->get_name()) {
          bottom_segment_list.push_back(pg_segment);
        } else if (pg_segment.get_layer_name() == top_layer->get_name()) {
          top_segment_list.push_back(pg_segment);
        }
      }
      for (PGSegment& bottom_segment : bottom_segment_list) {
        PlanarRect bottom_rect(bottom_segment.get_ll_x(), bottom_segment.get_ll_y(), bottom_segment.get_ur_x(), bottom_segment.get_ur_y());
        for (PGSegment& top_segment : top_segment_list) {
          PlanarRect top_rect(top_segment.get_ll_x(), top_segment.get_ll_y(), top_segment.get_ur_x(), top_segment.get_ur_y());
          PlanarRect overlap_rect = getOverlapRect(bottom_rect, top_rect);
          if (overlap_rect.get_width() <= 0 || overlap_rect.get_height() <= 0) {
            continue;
          }
          addViaSegment(pg_model, pg_net.get_name(), bottom_layer->get_name(), top_layer->get_name(), "",
                        (overlap_rect.get_ll_x() + overlap_rect.get_ur_x()) / 2, (overlap_rect.get_ll_y() + overlap_rect.get_ur_y()) / 2,
                        overlap_rect.get_width(), overlap_rect.get_height());
        }
      }
    }
  }

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

PlanarRect PDNGenerator::getOverlapRect(PlanarRect first_rect, PlanarRect second_rect)
{
  PlanarRect overlap_rect;
  overlap_rect.set_rect(std::max(first_rect.get_ll_x(), second_rect.get_ll_x()), std::max(first_rect.get_ll_y(), second_rect.get_ll_y()),
                        std::min(first_rect.get_ur_x(), second_rect.get_ur_x()), std::min(first_rect.get_ur_y(), second_rect.get_ur_y()));
  return overlap_rect;
}

bool PDNGenerator::addViaSegment(PGModel& pg_model, std::string net_name, std::string bottom_layer_name, std::string top_layer_name, std::string cut_layer_name,
                                 int32_t x, int32_t y, int32_t width, int32_t height)
{
  std::string via_key = FPUTIL.getString(net_name, "|", bottom_layer_name, "|", top_layer_name, "|", cut_layer_name, "|", x, "|", y, "|", width, "|", height);
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
  pg_segment.set_via_width(std::max(width, 1));
  pg_segment.set_via_height(std::max(height, 1));
  pg_segment.set_generated(true);
  if (FPI.getPGViaStack(pg_segment).empty()) {
    ++_rejected_via_count;
    return false;
  }
  pg_model.get_via_key_set().insert(via_key);
  FPDM.getDatabase().get_pg_segment_list().push_back(pg_segment);
  return true;
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
  for (auto& instance : database.get_instance_list()) {
    if (!instance.get_placed() || instance.is_delete_instance()) {
      continue;
    }
    const auto& master = database.get_cell_master_map().at(instance.get_master_name());
    if (!instance.get_macro() && !master.get_pad() && !master.get_pad_filler() && !master.get_corner()) {
      continue;
    }
    instances[instance.get_name()] = &instance;
    auto& shapes = placed_shapes[instance.get_name()];
    shapes = FPI.getPlacedShapes(instance, master.get_pg_pin_shapes());
    for (const auto& shape : shapes) {
      auto binding = bindings.find({instance.get_name(), shape.get_pin_name()});
      geometry.add(shape.get_layer_name(), shape, binding == bindings.end() ? -1 : binding->second);
    }
    for (const auto& shape : FPI.getPlacedShapes(instance, master.get_obstruction_shapes())) {
      geometry.add(shape.get_layer_name(), shape, -1);
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
    const int32_t net = database.get_pg_net_name_to_idx_map().at(segment.get_net_name());
    if (segment.is_line()) {
      geometry.add(segment.get_layer_name(), segmentRect(segment), net);
      if (segment.get_type() != PGSegmentType::kFollowPin) {
        targets[net].push_back(segment);
      }
    } else if (segment.get_type() == PGSegmentType::kVia) {
      for (const auto& shape : FPI.getPGViaShapes(segment)) {
        geometry.add(shape.get_layer_name(), shape, net);
      }
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
  int32_t connected = 0;
  std::vector<std::pair<int32_t, std::string>> pending;
  std::vector<std::string> unresolved;
  for (const auto& [terminal, net_idx] : bindings) {
    auto instance_it = instances.find(terminal.first);
    if (instance_it == instances.end()) {
      continue;
    }
    auto& instance = *instance_it->second;
    const auto& master = database.get_cell_master_map().at(instance.get_master_name());
    const bool pad = master.get_pad() || master.get_pad_filler() || master.get_corner();
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
      unresolved.push_back(terminal.first + "/" + terminal.second + " (no routing shape)");
      continue;
    }
    for (const auto& [port, shapes] : ports) {
      const int32_t component = root(port_ids.at({terminal.first, terminal.second, port}));
      if (connected_components.contains(component)) {
        ++connected;
        continue;
      }
      std::priority_queue<Candidate> candidates;
      for (const auto* pin : shapes) {
        auto* pin_layer = findRoutingLayer(pin->get_layer_name());
        if (pin_layer == nullptr) {
          continue;
        }
        for (size_t t = 0; t < targets[net_idx].size(); ++t) {
          const auto& target = targets[net_idx][t];
          const int32_t pin_span = std::min(pin->get_width(), pin->get_height());
          // Narrow access metal inside a pin aperture instead of expanding it into adjacent OBS.
          const int32_t aperture = pin_span - 2 * std::max(0, pin_layer->get_spacing());
          int32_t width = FPUTIL.alignDown(std::min(aperture >= pin_layer->get_min_width() ? aperture : pin_span, target.get_width()), 2 * grid);
          if (width < pin_layer->get_min_width() || width <= 0) {
            continue;
          }
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
              if (candidates.size() > 256) {
                candidates.pop();
              }
            }
          }
        }
      }
      std::vector<Candidate> ordered;
      while (!candidates.empty()) {
        ordered.push_back(candidates.top());
        candidates.pop();
      }
      bool success = false;
      for (auto candidate = ordered.rbegin(); candidate != ordered.rend() && !success; ++candidate) {
        const auto target = targets[net_idx][candidate->target];
        const auto& pin_layer = candidate->pin->get_layer_name();
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
          const int32_t route_width = std::max(candidate->width, FPUTIL.alignUp(findRoutingLayer(layer)->get_min_width(), 2 * grid));
          for (bool horizontal_first : {true, false}) {
            const PlanarCoord bend(horizontal_first ? candidate->end.get_x() : candidate->start.get_x(),
                                   horizontal_first ? candidate->start.get_y() : candidate->end.get_y());
            std::vector<PGSegment> path;
            for (auto [a, b] : {std::pair{candidate->start, bend}, std::pair{bend, candidate->end}}) {
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
              if (!geometry.clear(layer, segmentRect(line), net_idx, std::max(0, findRoutingLayer(layer)->get_spacing()))) {
                legal = false;
                break;
              }
            }
            auto append_via = [&](const std::string& from, const std::string& to, const PlanarCoord& coord) {
              if (from == to) {
                return true;
              }
              PGSegment via;
              via.set_net_name(net);
              via.set_type(PGSegmentType::kVia);
              via.set_bottom_layer_name(from);
              via.set_top_layer_name(to);
              via.set_start_coord(coord.get_x(), coord.get_y());
              via.set_end_coord(coord.get_x(), coord.get_y());
              via.set_via_width(route_width);
              via.set_via_height(route_width);
              via.set_generated(true);
              const auto via_shapes = FPI.getPGViaShapes(via);
              if (via_shapes.empty()) {
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
            legal = legal && append_via(pin_layer, layer, candidate->start) && append_via(layer, target.get_layer_name(), candidate->end);
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
        pending.emplace_back(component, terminal.first + "/" + terminal.second + " PORT " + std::to_string(port));
      }
    }
  }
  for (const auto& [component, terminal] : pending) {
    if (connected_components.contains(component)) {
      ++connected;
    } else {
      unresolved.push_back(terminal);
    }
  }
  FPLOG.info(Loc::current(), "PG access: ", connected, " connected ports, ", unresolved.size(), " unresolved ports.");
  if (!config.pg_temp_directory_path.empty()) {
    std::ofstream report(config.pg_temp_directory_path + "/pg_connectivity.rpt");
    report << "Connected ports: " << connected << "\nUnresolved ports: " << unresolved.size() << "\n";
    for (const auto& terminal : unresolved) {
      report << terminal << "\n";
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

// private

PDNGenerator* PDNGenerator::_pg_instance = nullptr;

}  // namespace ifp
