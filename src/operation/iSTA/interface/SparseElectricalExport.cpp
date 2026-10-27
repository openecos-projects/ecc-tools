// SPDX-License-Identifier: MulanPSL-2.0
#include "SparseElectricalExport.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Database.hpp"
#include "NetThresholdAdjustment.hpp"
#include "SparseTimingSnapshot.hpp"
#include "TimingData.hpp"

namespace ista {
namespace {
using Pins = std::pair<std::string_view, std::string_view>;
struct PinsHash
{
  std::size_t operator()(const Pins& pins) const
  {
    return std::hash<std::string_view>{}(pins.first) ^ (std::hash<std::string_view>{}(pins.second) << 1);
  }
};
struct PhysicalArc
{
  int32_t source_role;
  int32_t sink_role;
  int32_t level;
};
struct Work
{
  int32_t base;
  bool cell;
  PhysicalArc physical;
};
}  // namespace

void appendSparseElectricalEdges(const TimingData& snapshot, Database& database, SparseTimingSnapshot& view)
{
  std::unordered_map<Pins, PhysicalArc, PinsHash> active;
  int32_t max_level = 0;
  for (auto& arc : database.graph().arcs()) {
    if (arc.get_is_disable_arc() || arc.get_is_loop_disable())
      continue;
    const bool cell = arc.get_type() == ArcType::kCell;
    PhysicalArc physical{!cell && database.graph().driverPoints().contains(arc.get_source_pin()),
                         cell && database.graph().driverPoints().contains(arc.get_sink_pin()),
                         database.graph().sinkPoint(arc).get_level()};
    max_level = std::max(max_level, physical.level);
    active.emplace(Pins{arc.get_source_pin(), arc.get_sink_pin()}, physical);
  }
  // Bucket by the electrical graph's existing topological level. No per-pin
  // global scan and no new numerical timing pass is needed.
  std::vector<std::vector<Work>> levels(max_level + 1);
  auto enqueue = [&](const auto& arcs, bool cell) {
    for (std::size_t base = 0; base < arcs.size(); ++base) {
      const auto& arc = arcs[base];
      const auto found = active.find({arc.source_pin, arc.sink_pin});
      if (found != active.end())
        levels[found->second.level].push_back({static_cast<int32_t>(base), cell, found->second});
    }
  };
  enqueue(snapshot.graph_arcs, true);
  enqueue(snapshot.net_arcs, false);
  std::unordered_set<int32_t> covered_cell, covered_net;
  for (const auto& edge : view.cell_edges)
    covered_cell.insert(edge.base_arc);
  for (const auto& edge : view.net_edges)
    covered_net.insert(edge.base_arc);
  std::map<std::pair<std::string_view, int32_t>, int32_t> representative;
  int32_t next_group = 0;
  for (std::size_t index = 0; index < view.states.size(); ++index) {
    const auto& state = view.states[index];
    // Borrow stable snapshot/network names, never strings in the growing vector.
    const auto pin = database.network().pins().find(state.pin);
    if (pin != database.network().pins().end())
      representative.try_emplace({pin->first, state.role}, index);
    next_group = std::max(next_group, state.slew_group + 1);
  }
  std::array<int32_t, 2> electrical_tags{-1, -1};
  int32_t electrical_launch = -1;
  auto destination = [&](const std::string& pin, int32_t role, int32_t level) {
    const auto name = database.network().pins().find(pin);
    const auto key = std::pair<std::string_view, int32_t>{name->first, role};
    if (const auto found = representative.find(key); found != representative.end())
      return found->second;
    if (electrical_launch < 0) {
      electrical_launch = view.launch_tags.size();
      view.launch_tags.push_back("electrical_only");
    }
    if (electrical_tags[role] < 0) {
      electrical_tags[role] = view.tags.size();
      view.tags.push_back((role == 0 ? "L\x1f" : "D\x1f") + std::string("electrical_only"));
    }
    SparseTimingState state;
    state.pin = pin;
    state.domain = std::find(view.domains.begin(), view.domains.end(), "") - view.domains.begin();
    state.tag = electrical_tags[role];
    state.launch_tag = electrical_launch;
    state.source_type = 0;  // No launch domain, arrival transition or data seed.
    state.role = role;
    state.level = level;
    state.slew_group = next_group++;
    const int32_t index = view.states.size();
    view.states.push_back(std::move(state));
    representative.emplace(key, index);
    return index;
  };
  for (const auto& level : levels) {
    for (const auto& work : level) {
      if (work.cell && covered_cell.contains(work.base))
        continue;
      if (!work.cell && covered_net.contains(work.base))
        continue;
      const auto& source_pin = work.cell ? snapshot.graph_arcs[work.base].source_pin : snapshot.net_arcs[work.base].source_pin;
      const auto& sink_pin = work.cell ? snapshot.graph_arcs[work.base].sink_pin : snapshot.net_arcs[work.base].sink_pin;
      const auto source = representative.find({source_pin, work.physical.source_role});
      if (source == representative.end())
        continue;
      uint8_t mask = 0;
      if (work.cell) {
        const auto& arc = snapshot.graph_arcs[work.base];
        if (arc.arc_type != 1 || arc.library_arc_id < 0)
          continue;
        mask = (arc.sense == 1 ? 9 : arc.sense == -1 ? 6 : 15) & (arc.timing_type == 1 ? 3 : arc.timing_type == -1 ? 12 : 15);
        for (int input = 0; input < 2; ++input)
          for (int output = 0; output < 2; ++output)
            if (!database.graph().simValues().allowsTransition(source_pin, input == 0 ? TransType::kRise : TransType::kFall,
                                                                work.physical.source_role == 1)
                || !database.graph().simValues().allowsTransition(sink_pin, output == 0 ? TransType::kRise : TransType::kFall,
                                                                   work.physical.sink_role == 1))
              mask &= ~(uint8_t{1} << (2 * input + output));
        if (mask == 0)
          continue;
      }
      const int32_t sink = destination(sink_pin, work.physical.sink_role, work.physical.level);
      if (!work.cell && (view.states[sink].seed[0] || view.states[sink].seed[1]))
        continue;  // Ideal clock and input-delay boundaries retain their slew.
      if (view.states[source->second].level >= view.states[sink].level) {
        view.unsupported.push_back("electrical edge is not a forward DAG edge: " + source_pin + " -> " + sink_pin);
        continue;
      }
      SparseTimingEdge edge{work.base, source->second, sink, mask};
      if (work.cell) {
        view.slew_only_cell_edges.push_back(edge);
      } else {
        // Zero transition mask means slew transport with no arrival transport.
        view.net_edges.push_back(edge);
        view.net_threshold_adjustments.push_back(netThresholdAdjustment(database, source_pin, sink_pin));
      }
    }
  }
}
}  // namespace ista
