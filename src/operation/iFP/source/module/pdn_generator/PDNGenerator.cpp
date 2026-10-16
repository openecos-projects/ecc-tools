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

  bool clear(const std::string& layer, const PlanarRect& rect, int32_t net, int32_t spacing,
             const PlanarRect* shared_contact = nullptr) const
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
  buildIOCoreConnect(pg_model);
  buildMacroConnect(pg_model);
  trimStripeEnds();
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
    candidate.start = horizontal ? stripe.get_start_x() == core.get_ll_x() : stripe.get_start_y() == core.get_ll_y();
    candidate.end = horizontal ? stripe.get_end_x() == core.get_ur_x() : stripe.get_end_y() == core.get_ur_y();
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
    const int32_t new_begin = candidate.start ? std::max(begin, FPUTIL.alignDown(candidate.first_contact + stripe.get_width() / 2, grid)) : begin;
    const int32_t new_end = candidate.end ? std::min(end, FPUTIL.alignUp(candidate.last_contact - stripe.get_width() / 2, grid)) : end;
    if (new_begin >= new_end) {
      continue;
    }
    // A legal wire may need more metal than its contacts alone. Keep it intact
    // if the proposed rectangle would violate the layer's minimum area.
    const int64_t area = int64_t(new_end - new_begin + stripe.get_width()) * stripe.get_width();
    if (area < findRoutingLayer(stripe.get_layer_name())->get_min_area()) {
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
  FPLOG.info(Loc::current(), "Trimmed ", trimmed, " stripe ends while preserving physical PG contacts.");
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
    const auto net_it = database.get_pg_net_name_to_idx_map().find(segment.get_net_name());
    if (net_it == database.get_pg_net_name_to_idx_map().end()) {
      return;
    }
    if (segment.is_line()) {
      geometry.add(segment.get_layer_name(), segmentRect(segment), net_it->second);
    } else if (segment.get_type() == PGSegmentType::kVia) {
      for (const auto& shape : FPI.getPGViaShapes(segment)) {
        geometry.add(shape.get_layer_name(), shape, net_it->second);
      }
    }
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
    for (const auto& shape : FPI.getPlacedShapes(instance, master.get_obstruction_shapes())) {
      geometry.add(shape.get_layer_name(), shape, -1);
    }
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
  auto snap_inside = [&](int32_t value, int32_t low, int32_t high, int32_t width) {
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

  struct LayerContacts {
    int32_t direct = 0;
    int32_t bridge = 0;
    int32_t candidates = 0;
    std::set<size_t> stripes;
    std::map<std::string, int32_t> rejected;
    int32_t count() const { return direct + bridge; }
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
  };
  std::vector<PortTask> tasks;
  // Separate IO paths must remain distinct outside the source pin/stripe.
  // Treat accepted paths as obstacles even for the same net: this prevents
  // overlapping trunks from inflating the redundant-contact count.
  PGGeometryIndex io_paths(bin_size);

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
      for (auto port_it = ports.begin(); port_it != ports.end();) {
        const bool core_port = std::any_of(port_it->second.begin(), port_it->second.end(),
                                           [](const auto* shape) { return shape->is_core_port(); });
        if (!core_port) {
          port_it = ports.erase(port_it);
        } else {
          ++port_it;
        }
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

      tasks.push_back({&instance, port, net_idx, side, &target_it->second, std::move(rails), {}, {}, 0});
    }
  }

  enum class RouteMode { kDirectExtension, kStraightBridge };
  const auto is_direct = [](RouteMode mode) { return mode == RouteMode::kDirectExtension; };
  const auto route_name = [&](RouteMode mode) { return is_direct(mode) ? "direct_extension" : "straight_bridge"; };

  auto start_at = [&](Side side, const InstancePinShape& pin, int32_t width, int32_t tangent) {
    PlanarCoord start;
    if (side.value == Side::kLeft) {
      start.set_coord(pin.get_ur_x() - width / 2, tangent);
    } else if (side.value == Side::kRight) {
      start.set_coord(pin.get_ll_x() + width / 2, tangent);
    } else if (side.value == Side::kBottom) {
      start.set_coord(tangent, pin.get_ur_y() - width / 2);
    } else {
      start.set_coord(tangent, pin.get_ll_y() + width / 2);
    }
    start.set_x(snap_inside(start.get_x(), pin.get_ll_x(), pin.get_ur_x(), width));
    start.set_y(snap_inside(start.get_y(), pin.get_ll_y(), pin.get_ur_y(), width));
    return start;
  };

  auto bridge_target_point = [](const PGSegment& target, const PlanarCoord& start) {
    // A straight bridge must touch the existing stripe span.  Do not clamp a
    // tangent to a stripe endpoint: that would turn a bridge into an extension.
    if (target.is_horizontal()) {
      const int32_t x_low = target.get_ll_x() + target.get_width() / 2;
      const int32_t x_high = target.get_ur_x() - target.get_width() / 2;
      if (start.get_x() < x_low || start.get_x() > x_high) return PlanarCoord(INT32_MIN, INT32_MIN);
      return PlanarCoord(start.get_x(), target.get_start_y());
    }
    if (target.is_vertical()) {
      const int32_t y_low = target.get_ll_y() + target.get_width() / 2;
      const int32_t y_high = target.get_ur_y() - target.get_width() / 2;
      if (start.get_y() < y_low || start.get_y() > y_high) return PlanarCoord(INT32_MIN, INT32_MIN);
      return PlanarCoord(target.get_start_x(), start.get_y());
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

  auto access_separation = [&](const RoutingLayer& layer, int32_t width, const PlanarCoord& start, const PlanarCoord& end) {
    int32_t separation = std::max(grid, layer.get_io_spacing());
    const int32_t gap = (std::abs(end.get_x() - start.get_x()) + std::abs(end.get_y() - start.get_y())) - width;
    if (gap > 0 && layer.get_min_enclosed_area() > 0) {
      separation = std::max(separation, static_cast<int32_t>((int64_t(layer.get_min_enclosed_area()) + gap - 1) / gap));
    }
    return std::max(separation, configured_bridge_spacing);
  };

  // Route direct extensions before bridges so a bridge cannot occupy a direct
  // extension corridor. Both route classes are single Manhattan segments.
  for (const RouteMode mode : {RouteMode::kDirectExtension, RouteMode::kStraightBridge}) {
    const bool direct = is_direct(mode);
    for (auto& task : tasks) {
      auto& instance = *task.instance;
      const auto& net_name = instance.get_io_cell_net_name();
      const int32_t net_idx = task.net_idx;
      const int32_t port = task.port;
      const Side side = task.side;
      const bool normal_x = side.value == Side::kLeft || side.value == Side::kRight;
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
                          std::vector<PlanarCoord> points, RouteMode route_mode) {
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
        if (points.size() != 2) return reject("NON_STRAIGHT_PATH");
        if (points.front().get_x() != points.back().get_x() && points.front().get_y() != points.back().get_y()) {
          return reject("NON_STRAIGHT_PATH");
        }
        const int32_t tangent = normal_x ? points.front().get_y() : points.front().get_x();
        const int32_t separation = access_separation(*layer, width, points.front(), points.back());
        for (const auto& access : task.access_points[layer_name]) {
          if (std::abs(tangent - access.tangent) < (width + access.width) / 2 + std::max(separation, access.separation)) {
            return reject("ACCESS_SPACING_OR_HOLE_AREA");
          }
        }
        const auto& die = database.get_die();
        std::vector<PGSegment> path;
        for (size_t i = 1; i < points.size(); ++i) {
          const auto& a = points[i - 1];
          const auto& b = points[i];
          if ((a.get_x() != b.get_x() && a.get_y() != b.get_y())
              || a.get_x() % grid || a.get_y() % grid || b.get_x() % grid || b.get_y() % grid) return reject("OFF_GRID_PATH");
          auto line = make_line(net_name, layer_name, width, a, b);
          const auto box = segmentRect(line);
          if (box.get_ll_x() < die.get_ll_x() || box.get_ll_y() < die.get_ll_y()
              || box.get_ur_x() > die.get_ur_x() || box.get_ur_y() > die.get_ur_y()) return reject("OUTSIDE_DIE");
          const int64_t area = int64_t(width) * (std::abs(b.get_x() - a.get_x()) + std::abs(b.get_y() - a.get_y()) + width);
          if (area < layer->get_min_area()) return reject("MIN_AREA");
          if (!geometry.clear(layer_name, box, net_idx, separation)) return reject("FOREIGN_METAL_OR_OBS");
          const auto target_box = segmentRect(target.segment);
          if (!io_paths.clear(layer_name, box, -2, separation, &target_box)) return reject("BRIDGE_BLOCKED_SPACING");
          path.push_back(line);
        }
        auto positive_overlap = [](const PlanarRect& a, const PlanarRect& b) {
          return a.get_ll_x() < b.get_ur_x() && b.get_ll_x() < a.get_ur_x()
                 && a.get_ll_y() < b.get_ur_y() && b.get_ll_y() < a.get_ur_y();
        };
        if (!positive_overlap(segmentRect(path.front()), pin)
            || !positive_overlap(segmentRect(path.back()), segmentRect(target.segment))) return reject("NO_ENDPOINT_OVERLAP");
        for (auto& segment : path) {
          database.get_pg_segment_list().push_back(segment);
          add_segment_geometry(segment);
          io_paths.add(layer_name, segmentRect(segment), -1);
        }
        task.access_points[layer_name].push_back({tangent, width, separation});
        ++task.contacts;
        ++task.routes_per_stripe[target.database_index];
        stats.stripes.insert(target.database_index);
        if (is_direct(route_mode)) { ++stats.direct; ++direct_connections; }
        else { ++stats.bridge; ++bridge_connections; }
        const auto& start = points.front();
        const auto& end = points.back();
        contact_details.push_back(FPUTIL.getString(instance.get_name(), "/", instance.get_io_cell_pin_name(), " PORT ", port,
                                                   " net=", net_name, " mode=", route_name(route_mode), " pin_layer=", pin.get_layer_name(),
                                                   " route_layer=", layer_name, " target_layer=", layer_name,
                                                   " stripe_id=", target.database_index, " width=", width,
                                                   " segments=1 spacing=", separation,
                                                   " start=(", start.get_x(), ",", start.get_y(), ") end=(", end.get_x(), ",", end.get_y(), ")"));
        return true;
      };

      for (const auto& [layer_name, rails] : task.rails) {
        auto found = task.targets->find(layer_name);
        if (found == task.targets->end()) continue;
        auto targets = found->second;
        const auto& first_pin = rails.front();
        const PlanarCoord center((first_pin.get_ll_x() + first_pin.get_ur_x()) / 2, (first_pin.get_ll_y() + first_pin.get_ur_y()) / 2);
        auto distance = [&](const StripeTarget& target) {
          const auto point = bridge_target_point(target.segment, center);
          if (point.get_x() == INT32_MIN || point.get_y() == INT32_MIN) return int64_t(INT64_MAX);
          return int64_t(std::abs(point.get_x() - center.get_x())) + std::abs(point.get_y() - center.get_y());
        };
        std::stable_sort(targets.begin(), targets.end(), [&](const auto& a, const auto& b) { return distance(a) < distance(b); });
        auto* layer = findRoutingLayer(layer_name);
        for (const auto& target : targets) {
          if (!can_add(layer_name, target.database_index)) continue;
          const bool perpendicular = normal_x ? target.segment.is_horizontal() : target.segment.is_vertical();
          if (direct && (!perpendicular || !config.pg_io_core_connect.allow_direct_extension)) continue;
          if (!direct && !config.pg_io_core_connect.allow_bridge) continue;
          for (const auto& pin : rails) {
            const int32_t width = pin_width(pin, layer, target.segment.get_width());
            if (width == 0) { ++task.layers[layer_name].rejected["PIN_NARROWER_THAN_STRIPE"]; continue; }
            const int32_t low = normal_x ? pin.get_ll_y() : pin.get_ll_x();
            const int32_t high = normal_x ? pin.get_ur_y() : pin.get_ur_x();
            std::set<int32_t> tangents;
            const int32_t first = FPUTIL.alignUp(low + width / 2, grid);
            const int32_t last = FPUTIL.alignDown(high - width / 2, grid);
            if (direct) {
              const int32_t coordinate = normal_x ? target.segment.get_start_y() : target.segment.get_start_x();
              if (coordinate >= first && coordinate <= last) tangents.insert(coordinate);
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
              const auto end = direct ? direct_extension_target_point(side, target.segment, start)
                                      : bridge_target_point(target.segment, start);
              if (end.get_x() == INT32_MIN || end.get_y() == INT32_MIN) {
                ++task.layers[layer_name].rejected[direct ? "DIRECT_EXTENSION_STRIPE_NOT_ALIGNED" : "NO_STRAIGHT_PATH"];
                continue;
              }
              if (start.get_x() != end.get_x() && start.get_y() != end.get_y()) {
                ++task.layers[layer_name].rejected[direct ? "DIRECT_EXTENSION_NOT_COLLINEAR" : "NO_STRAIGHT_PATH"];
                continue;
              }
              try_path(pin, target, width, {start, end}, mode);
            }
          }
        }
      }
    }
  }
  std::vector<std::string> layer_details;
  for (auto& task : tasks) {
    const std::string terminal = task.instance->get_name() + "/" + task.instance->get_io_cell_pin_name() + " PORT " + std::to_string(task.port);
    bool complete = true;
    for (const auto& [layer, rails] : task.rails) {
      const auto& stats = task.layers[layer];
      if (config.pg_io_core_connect.require_each_pin_layer && stats.count() == 0) {
        failures.push_back({terminal, "NO_LEGAL_PATH_" + layer});
        complete = false;
      }
      std::string detail = FPUTIL.getString(terminal, " layer=", layer, " rails=", rails.size(), " routes=", stats.count(),
                                            " stripes=", stats.stripes.size(), " direct=", stats.direct, " bridge=", stats.bridge,
                                            " candidates=", stats.candidates);
      for (const auto& [reason, count] : stats.rejected) detail += FPUTIL.getString(" ", reason, "=", count);
      layer_details.push_back(std::move(detail));
    }
    if (task.contacts < config.pg_io_core_connect.min_contacts_per_port) {
      failures.push_back({terminal, "INSUFFICIENT_CONTACTS"});
      complete = false;
    }
    if (complete) ++connected_ports;
  }
  std::set<std::string> unresolved;
  for (const auto& failure : failures) unresolved.insert(failure.terminal);

  FPLOG.info(Loc::current(), "IO core PG: ", connected_ports, " connected ports, ", unresolved.size(),
             " unresolved ports (direct=", direct_connections, ", bridge=", bridge_connections, ").");
  if (!config.pg_temp_directory_path.empty()) {
    std::ofstream report(config.pg_temp_directory_path + "/io_core_connect.rpt");
    report << "Connected ports: " << connected_ports << "\nUnresolved ports: " << unresolved.size() << "\nDirect connections: "
           << direct_connections << "\nBridge connections: " << bridge_connections << "\n";
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
    const auto& master = database.get_cell_master_map().at(instance.get_master_name());
    const bool pad = master.get_pad() || master.get_pad_filler() || master.get_corner();
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
             optional_unresolved.size(), " optional unresolved ports.");
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

// private

PDNGenerator* PDNGenerator::_pg_instance = nullptr;

}  // namespace ifp
