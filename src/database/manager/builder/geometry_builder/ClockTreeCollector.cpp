#include "ClockTreeCollector.h"

#include "GeometryStore.h"
#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbUnits.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ecc::geometry {
namespace {

struct PinEntry
{
  idb::IdbPin* pin = nullptr;
  idb::IdbNet* net = nullptr;
  uint32_t node = 0;
  uint32_t clock = 0;
};

class UnionFind
{
 public:
  explicit UnionFind(size_t size) : _parent(size)
  {
    for (size_t index = 0; index < size; ++index) {
      _parent[index] = index;
    }
  }

  uint32_t find(uint32_t value)
  {
    uint32_t root = value;
    while (_parent[root] != root) {
      root = _parent[root];
    }
    while (_parent[value] != value) {
      const uint32_t next = _parent[value];
      _parent[value] = root;
      value = next;
    }
    return root;
  }

  void unite(uint32_t lhs, uint32_t rhs)
  {
    lhs = find(lhs);
    rhs = find(rhs);
    if (lhs != rhs) {
      _parent[rhs] = lhs;
    }
  }

 private:
  std::vector<uint32_t> _parent;
};

std::string lower(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

bool is_input(idb::IdbPin* pin)
{
  if (pin == nullptr || pin->get_term() == nullptr) {
    return false;
  }
  const idb::IdbConnectDirection direction = pin->get_term()->get_direction();
  return direction == idb::IdbConnectDirection::kInput || direction == idb::IdbConnectDirection::kInOut;
}

bool is_output(idb::IdbPin* pin)
{
  if (pin == nullptr || pin->get_term() == nullptr) {
    return false;
  }
  const idb::IdbConnectDirection direction = pin->get_term()->get_direction();
  return direction == idb::IdbConnectDirection::kOutput || direction == idb::IdbConnectDirection::kOutputTriState
         || direction == idb::IdbConnectDirection::kInOut;
}

bool is_inverter_master(const std::string& master)
{
  const std::string name = lower(master);
  return name.find("inv") != std::string::npos || name.find("inverter") != std::string::npos;
}

bool is_icg_master(const std::string& master)
{
  const std::string name = lower(master);
  return name.find("icg") != std::string::npos || name.find("clock_gate") != std::string::npos
         || name.find("clockgate") != std::string::npos;
}

ClockNodeKind classify_instance_pin(idb::IdbPin* pin, const std::string& master)
{
  if (is_output(pin)) {
    if (is_icg_master(master)) {
      return ClockNodeKind::kIcgOutput;
    }
    if (is_inverter_master(master)) {
      return ClockNodeKind::kInverterOutput;
    }
    return ClockNodeKind::kBufferOutput;
  }
  if (pin->is_flip_flop_clk()) {
    return ClockNodeKind::kSink;
  }
  if (is_icg_master(master)) {
    return ClockNodeKind::kIcgInput;
  }
  if (is_inverter_master(master)) {
    return ClockNodeKind::kInverterInput;
  }
  return ClockNodeKind::kBufferInput;
}

std::string pin_key(idb::IdbPin* pin)
{
  if (pin == nullptr) {
    return {};
  }
  if (pin->get_instance() != nullptr) {
    return pin->get_instance()->get_name() + "\x1f" + pin->get_pin_name();
  }
  return std::string("\x1f") + pin->get_pin_name();
}

std::string pin_display_name(idb::IdbPin* pin)
{
  if (pin == nullptr) {
    return {};
  }
  return pin->get_instance() == nullptr ? pin->get_pin_name()
                                        : pin->get_instance()->get_name() + "/" + pin->get_pin_name();
}

using InstanceOwnerIndex = std::unordered_map<uint32_t, uint32_t>;

InstanceOwnerIndex build_instance_owner_index(const GeometryStore& store)
{
  InstanceOwnerIndex index;
  const auto owners = store.owners();
  for (uint32_t owner_index = 0; owner_index < owners.size(); ++owner_index) {
    if (owners[owner_index].type == OwnerType::kInstanceBBox) {
      index.emplace(static_cast<uint32_t>(owners[owner_index].owner_id), owner_index);
    }
  }
  return index;
}

uint32_t find_instance_owner_index(const InstanceOwnerIndex& owners, idb::IdbInstance* instance)
{
  if (instance == nullptr) {
    return UINT32_MAX;
  }
  const auto found = owners.find(static_cast<uint32_t>(instance->get_id()));
  return found == owners.end() ? UINT32_MAX : found->second;
}

bool pin_position(idb::IdbPin* pin, int32_t& x, int32_t& y)
{
  if (pin == nullptr) {
    return false;
  }
  if (pin->get_instance() != nullptr) {
    if (pin->get_instance()->is_unplaced() || pin->get_instance()->get_coordinate() == nullptr) {
      return false;
    }
    idb::IdbCoordinate<int32_t>* coordinate = pin->get_average_coordinate();
    if (coordinate == nullptr || coordinate->is_negative()) {
      return false;
    }
    x = coordinate->get_x();
    y = coordinate->get_y();
    return true;
  }
  if (pin->is_io_pin() && pin->get_location() != nullptr && !pin->get_location()->is_negative()) {
    x = pin->get_location()->get_x();
    y = pin->get_location()->get_y();
    return true;
  }
  return false;
}

uint64_t fingerprint_clocks(const ClockTreeSnapshot& snapshot)
{
  uint64_t hash = 1469598103934665603ULL;
  auto append = [&hash](const void* data, size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t index = 0; index < size; ++index) {
      hash ^= bytes[index];
      hash *= 1099511628211ULL;
    }
  };
  append(snapshot.clocks.data(), snapshot.clocks.size() * sizeof(ClockRecord));
  append(snapshot.nodes.data(), snapshot.nodes.size() * sizeof(ClockNodeRecord));
  append(snapshot.edges.data(), snapshot.edges.size() * sizeof(ClockEdgeRecord));
  append(snapshot.roots.data(), snapshot.roots.size() * sizeof(uint32_t));
  append(snapshot.strings.data(), snapshot.strings.size());
  append(snapshot.timing.data(), snapshot.timing.size() * sizeof(ClockTimingRecord));
  return hash;
}

}  // namespace

std::optional<ClockTreeSnapshot> collect_clock_tree_snapshot(idb::IdbDesign& design, idb::IdbLayout& layout,
                                                             const GeometryStore& store,
                                                             const ClockTreeCollectOptions& options)
{
  if (design.get_net_list() == nullptr) {
    return std::nullopt;
  }

  ClockTreeSnapshot snapshot;
  snapshot.meta.stage = static_cast<uint16_t>(options.stage);
  snapshot.meta.analysis_source = static_cast<uint16_t>(options.analysis_source);
  snapshot.meta.temperature_c = options.temperature_c;
  if (design.get_units() != nullptr) {
    snapshot.meta.dbu_per_micron = static_cast<uint32_t>(design.get_units()->get_micron_dbu());
  } else if (layout.get_units() != nullptr) {
    snapshot.meta.dbu_per_micron = static_cast<uint32_t>(layout.get_units()->get_micron_dbu());
  }

  std::vector<PinEntry> pins;
  std::unordered_map<std::string, uint32_t> node_by_pin;
  std::vector<ClockEdgeRecord> net_edges;
  const InstanceOwnerIndex instance_owners = build_instance_owner_index(store);

  for (idb::IdbNet* net : design.get_net_list()->get_net_list()) {
    if (net == nullptr || !net->is_clock()) {
      continue;
    }
    std::vector<idb::IdbPin*> net_pins;
    if (net->get_io_pins() != nullptr) {
      net_pins.insert(net_pins.end(), net->get_io_pins()->get_pin_list().begin(),
                      net->get_io_pins()->get_pin_list().end());
    }
    if (net->get_instance_pin_list() != nullptr) {
      net_pins.insert(net_pins.end(), net->get_instance_pin_list()->get_pin_list().begin(),
                      net->get_instance_pin_list()->get_pin_list().end());
    }
    if (net_pins.empty()) {
      continue;
    }

    for (idb::IdbPin* pin : net_pins) {
      const std::string key = pin_key(pin);
      auto [iter, inserted] = node_by_pin.emplace(key, static_cast<uint32_t>(pins.size()));
      if (inserted) {
        pins.push_back(PinEntry{pin, net, static_cast<uint32_t>(pins.size()), 0});
      }
      pins[iter->second].net = net;
    }

    idb::IdbPin* driver = nullptr;
    for (idb::IdbPin* pin : net_pins) {
      if (is_output(pin) && pin->get_instance() != nullptr) {
        driver = pin;
        break;
      }
    }
    if (driver == nullptr) {
      for (idb::IdbPin* pin : net_pins) {
        if (is_output(pin) || pin->is_primary_input() || pin->is_io_pin()) {
          driver = pin;
          break;
        }
      }
    }
    if (driver == nullptr) {
      driver = net_pins.front();
    }
    const uint32_t driver_node = node_by_pin.at(pin_key(driver));
    for (idb::IdbPin* pin : net_pins) {
      const uint32_t sink_node = node_by_pin.at(pin_key(pin));
      if (sink_node == driver_node) {
        continue;
      }
      ClockEdgeRecord edge;
      edge.from_node = driver_node;
      edge.to_node = sink_node;
      edge.kind = static_cast<uint8_t>(ClockEdgeKind::kNet);
      edge.object_ref = net->get_id();
      set_string_reference(snapshot, edge.net_offset, edge.net_size, net->get_net_name());
      net_edges.push_back(edge);
    }
  }

  if (pins.empty()) {
    snapshot.meta.status = static_cast<uint16_t>(ClockTreeStatus::kNoClocks);
    set_string_reference(snapshot, snapshot.meta.diagnostics_offset, snapshot.meta.diagnostics_size,
                         "no clock networks were found in the stage design");
    snapshot.meta.string_size = snapshot.strings.size();
    return snapshot;
  }

  UnionFind components(pins.size());
  for (const ClockEdgeRecord& edge : net_edges) {
    components.unite(edge.from_node, edge.to_node);
  }

  std::map<std::string, std::pair<idb::IdbInstance*, std::vector<uint32_t>>> clocked_instances;
  for (uint32_t index = 0; index < pins.size(); ++index) {
    if (pins[index].pin != nullptr && pins[index].pin->get_instance() != nullptr) {
      idb::IdbInstance* instance = pins[index].pin->get_instance();
      auto& entry = clocked_instances[instance->get_name()];
      entry.first = instance;
      entry.second.push_back(index);
    }
  }
  std::vector<ClockEdgeRecord> cell_edges;
  for (const auto& [instance_name, record] : clocked_instances) {
    (void) instance_name;
    idb::IdbInstance* instance = record.first;
    const std::vector<uint32_t>& node_ids = record.second;
    std::vector<uint32_t> inputs;
    std::vector<uint32_t> outputs;
    for (uint32_t node_id : node_ids) {
      if (is_input(pins[node_id].pin)) {
        inputs.push_back(node_id);
      }
      if (is_output(pins[node_id].pin)) {
        outputs.push_back(node_id);
      }
    }
    const std::string master = instance->get_cell_master() == nullptr ? "" : instance->get_cell_master()->get_name();
    for (uint32_t input : inputs) {
      for (uint32_t output : outputs) {
        components.unite(input, output);
        ClockEdgeRecord edge;
        edge.from_node = input;
        edge.to_node = output;
        edge.kind = static_cast<uint8_t>(ClockEdgeKind::kCell);
        edge.polarity = is_inverter_master(master) ? 1 : 0;
        edge.object_ref = instance->get_id();
        set_string_reference(snapshot, edge.cell_offset, edge.cell_size, master);
        cell_edges.push_back(edge);
      }
    }
  }

  auto edge_pin_key = [&pins](const ClockEdgeRecord& edge) {
    return std::pair<std::string, std::string>{pin_key(pins[edge.from_node].pin), pin_key(pins[edge.to_node].pin)};
  };
  std::sort(net_edges.begin(), net_edges.end(), [&](const ClockEdgeRecord& lhs, const ClockEdgeRecord& rhs) {
    return edge_pin_key(lhs) < edge_pin_key(rhs);
  });
  std::sort(cell_edges.begin(), cell_edges.end(), [&](const ClockEdgeRecord& lhs, const ClockEdgeRecord& rhs) {
    return edge_pin_key(lhs) < edge_pin_key(rhs);
  });

  std::map<uint32_t, std::vector<uint32_t>> component_nodes;
  for (uint32_t index = 0; index < pins.size(); ++index) {
    component_nodes[components.find(index)].push_back(index);
  }

  std::vector<std::vector<uint32_t>> ordered_components;
  ordered_components.reserve(component_nodes.size());
  for (auto& [root, node_ids] : component_nodes) {
    (void)root;
    std::sort(node_ids.begin(), node_ids.end(), [&](uint32_t lhs, uint32_t rhs) {
      return pin_key(pins[lhs].pin) < pin_key(pins[rhs].pin);
    });
    ordered_components.push_back(std::move(node_ids));
  }
  std::sort(ordered_components.begin(), ordered_components.end(), [&](const auto& lhs, const auto& rhs) {
    return pin_key(pins[lhs.front()].pin) < pin_key(pins[rhs.front()].pin);
  });

  std::vector<ClockNodeRecord> nodes;
  std::vector<ClockEdgeRecord> edges;
  std::vector<ClockTimingRecord> timing;

  for (uint32_t clock_index = 0; clock_index < ordered_components.size(); ++clock_index) {
    const std::vector<uint32_t>& component = ordered_components[clock_index];
    ClockRecord clock;
    clock.clock_id = clock_index;
    const std::string clock_name = pin_display_name(pins[component.front()].pin);
    clock.name_offset = add_clock_tree_string(snapshot, clock_name);
    clock.name_size = static_cast<uint32_t>(clock_name.size());
    clock.flags = kClockNameValid | kClockTopologyValid;
    clock.source_edge = static_cast<uint8_t>(ClockEdgeTransition::kRise);
    clock.node_first = static_cast<uint32_t>(nodes.size());
    clock.node_count = static_cast<uint32_t>(component.size());
    clock.root_node_first = static_cast<uint32_t>(snapshot.roots.size());
    std::unordered_map<uint32_t, uint32_t> local_to_snapshot;
    for (uint32_t pin_index : component) {
      idb::IdbPin* pin = pins[pin_index].pin;
      const std::string instance_name = pin->get_instance() == nullptr ? "" : pin->get_instance()->get_name();
      const std::string pin_name = pin->get_pin_name();
      const std::string master =
          pin->get_instance() == nullptr || pin->get_instance()->get_cell_master() == nullptr
              ? ""
              : pin->get_instance()->get_cell_master()->get_name();

      ClockNodeRecord node;
      node.node_id = static_cast<uint32_t>(nodes.size());
      node.clock_id = clock_index;
      node.kind = static_cast<uint8_t>(pin->get_instance() == nullptr
                                           ? (pin->is_primary_input() ? ClockNodeKind::kRootPort
                                                                      : ClockNodeKind::kOtherBoundary)
                                           : classify_instance_pin(pin, master));
      if (pin->get_instance() != nullptr) {
        node.owner_index = find_instance_owner_index(instance_owners, pin->get_instance());
      }
      set_string_reference(snapshot, node.instance_offset, node.instance_size, instance_name);
      set_string_reference(snapshot, node.pin_offset, node.pin_size, pin_name);
      set_string_reference(snapshot, node.master_offset, node.master_size, master);
      int32_t x = 0;
      int32_t y = 0;
      if (pin_position(pin, x, y)) {
        node.x = x;
        node.y = y;
        node.flags |= kClockNodePositionValid;
      }
      if (node.owner_index != UINT32_MAX) {
        node.flags |= kClockNodeOwnerValid;
      }
      if (node.kind == static_cast<uint8_t>(ClockNodeKind::kSink)) {
        node.flags |= kClockNodeSink;
        ++clock.sink_count;
      }
      local_to_snapshot[pin_index] = node.node_id;
      nodes.push_back(node);
    }

    std::vector<uint32_t> component_roots;
    std::vector<uint32_t> incoming(component.size(), 0);
    for (const ClockEdgeRecord& edge : net_edges) {
      auto from = local_to_snapshot.find(edge.from_node);
      auto to = local_to_snapshot.find(edge.to_node);
      if (from == local_to_snapshot.end() || to == local_to_snapshot.end()) {
        continue;
      }
      ++incoming[to->second - clock.node_first];
      ClockEdgeRecord mapped = edge;
      mapped.edge_id = static_cast<uint32_t>(edges.size());
      mapped.from_node = from->second;
      mapped.to_node = to->second;
      edges.push_back(mapped);
    }
    for (const ClockEdgeRecord& edge : cell_edges) {
      auto from = local_to_snapshot.find(edge.from_node);
      auto to = local_to_snapshot.find(edge.to_node);
      if (from == local_to_snapshot.end() || to == local_to_snapshot.end()) {
        continue;
      }
      ++incoming[to->second - clock.node_first];
      ClockEdgeRecord mapped = edge;
      mapped.edge_id = static_cast<uint32_t>(edges.size());
      mapped.from_node = from->second;
      mapped.to_node = to->second;
      edges.push_back(mapped);
    }

    for (uint32_t index = 0; index < component.size(); ++index) {
      const uint32_t pin_index = component[index];
      const uint32_t node_id = local_to_snapshot[pin_index];
      const bool is_root_pin = pins[pin_index].pin->get_instance() == nullptr && is_input(pins[pin_index].pin);
      if (is_root_pin || incoming[index] == 0) {
        component_roots.push_back(node_id);
        nodes[node_id].flags |= kClockNodeRoot;
      }
    }
    if (component_roots.empty()) {
      component_roots.push_back(local_to_snapshot[component.front()]);
    }
    clock.root_node_first = static_cast<uint32_t>(snapshot.roots.size());
    clock.root_node_count = static_cast<uint32_t>(component_roots.size());
    std::sort(component_roots.begin(), component_roots.end());
    snapshot.roots.insert(snapshot.roots.end(), component_roots.begin(), component_roots.end());
    clock.flags |= kClockRootsValid;

    snapshot.clocks.push_back(clock);
  }

  // Assign edge ranges per clock from the nodes they connect.
  std::vector<uint32_t> edge_count_by_clock(snapshot.clocks.size(), 0);
  for (const ClockEdgeRecord& edge : edges) {
    const uint32_t clock_id = nodes[edge.from_node].clock_id;
    ++edge_count_by_clock[clock_id];
  }
  uint32_t edge_cursor = 0;
  for (uint32_t clock_index = 0; clock_index < snapshot.clocks.size(); ++clock_index) {
    snapshot.clocks[clock_index].edge_first = edge_cursor;
    snapshot.clocks[clock_index].edge_count = edge_count_by_clock[clock_index];
    edge_cursor += edge_count_by_clock[clock_index];
  }

  snapshot.nodes = std::move(nodes);
  snapshot.edges = std::move(edges);
  snapshot.timing = std::move(timing);
  snapshot.meta.status = static_cast<uint16_t>(ClockTreeStatus::kTopologyOnly);
  set_string_reference(snapshot, snapshot.meta.rc_corner_offset, snapshot.meta.rc_corner_size, options.rc_corner);
  set_string_reference(snapshot, snapshot.meta.liberty_corner_offset, snapshot.meta.liberty_corner_size,
                       options.liberty_corner);
  set_string_reference(snapshot, snapshot.meta.diagnostics_offset, snapshot.meta.diagnostics_size,
                       options.timing_unavailable_reason);
  snapshot.meta.node_count = snapshot.nodes.size();
  snapshot.meta.edge_count = snapshot.edges.size();
  snapshot.meta.timing_count = snapshot.timing.size();
  snapshot.meta.clock_count = snapshot.clocks.size();
  snapshot.meta.root_count = snapshot.roots.size();
  snapshot.meta.producer_fingerprint = fingerprint_clocks(snapshot);
  snapshot.meta.string_size = snapshot.strings.size();
  return snapshot;
}

}  // namespace ecc::geometry
