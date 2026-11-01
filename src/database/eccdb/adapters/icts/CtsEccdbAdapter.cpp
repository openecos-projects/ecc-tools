// SPDX-License-Identifier: MulanPSL-2.0

#include "CtsEccdbAdapter.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <set>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "LibParserCpp.hh"
#include "Logger.hh"
#include "context/DatabaseContext.h"
#include "connectivity/ConnectivityService.h"
#include "design/DesignStore.h"
#include "design/Design.hh"
#include "design/Net.hh"
#include "design/Clock.hh"
#include "design/Inst.hh"
#include "design/Pin.hh"
#include "design/netlist/storage/DesignNetlistStorage.h"
#include "geometry/PhysicalDesignService.h"
#include "instance/InstanceService.h"
#include "library/LibraryStore.h"
#include "liberty/Lib.hh"
#include "Wrapper.hh"
#include "salt/base/flute.h"
#include "salt/base/net.h"
#include "salt/base/tree.h"

namespace eccdb::adapter::icts {
namespace {

constexpr double kOppositeSwitchingCouplingFactor = 2.0;

auto isInputLike(LibraryMasterTermDirection direction) -> bool
{
  return direction == LibraryMasterTermDirection::kInput || direction == LibraryMasterTermDirection::kInOut;
}

auto isOutputLike(LibraryMasterTermDirection direction) -> bool
{
  return direction == LibraryMasterTermDirection::kOutput || direction == LibraryMasterTermDirection::kOutputTriState
         || direction == LibraryMasterTermDirection::kInOut;
}

auto isLogicMaster(LibraryCellMasterType type) -> bool
{
  return type == LibraryCellMasterType::kCore || type == LibraryCellMasterType::kCoreFeedThru
         || type == LibraryCellMasterType::kCoreTieHigh || type == LibraryCellMasterType::kCoreTieLow;
}

auto isMacroMaster(LibraryCellMasterType type) -> bool
{
  return type == LibraryCellMasterType::kBlock || type == LibraryCellMasterType::kBlockBlackbox || type == LibraryCellMasterType::kBlockSoft;
}

auto toCtsPinType(const LibraryMasterTerm& term) -> ::icts::PinType
{
  if (term.use == LibraryMasterTermUse::kClock) {
    return ::icts::PinType::kClock;
  }
  if (term.direction == LibraryMasterTermDirection::kOutput || term.direction == LibraryMasterTermDirection::kOutputTriState) {
    return ::icts::PinType::kOut;
  }
  if (term.direction == LibraryMasterTermDirection::kInput) {
    return ::icts::PinType::kIn;
  }
  if (term.direction == LibraryMasterTermDirection::kInOut) {
    return ::icts::PinType::kInOut;
  }
  return ::icts::PinType::kOther;
}

auto toCtsPinType(const DesignBlockTerm& term) -> ::icts::PinType
{
  if (term.use == DesignSignalUse::kClock) {
    return ::icts::PinType::kClock;
  }
  if (term.direction == DesignIoPinDirection::kOutput) {
    return ::icts::PinType::kOut;
  }
  if (term.direction == DesignIoPinDirection::kInput) {
    return ::icts::PinType::kIn;
  }
  if (term.direction == DesignIoPinDirection::kInOut) {
    return ::icts::PinType::kInOut;
  }
  return ::icts::PinType::kOther;
}

auto pgSignalUse(const LibraryMasterTermUse use) -> std::optional<DesignSignalUse>
{
  switch (use) {
    case LibraryMasterTermUse::kPower:
      return DesignSignalUse::kPower;
    case LibraryMasterTermUse::kGround:
      return DesignSignalUse::kGround;
    default:
      return std::nullopt;
  }
}

struct CtsPgTermBinding
{
  std::string master_term_name;
  DesignNetId net;
};

struct CtsPgBindingIndex
{
  using TermKey = std::pair<LibraryMasterTermUse, std::string>;

  std::map<TermKey, DesignNetId> nets_by_term;
  std::set<TermKey> ambiguous_terms;
  std::map<LibraryMasterTermUse, std::vector<DesignNetId>> nets_by_use;
};

struct CtsPgBindingResolution
{
  DesignNetId net;
  bool ambiguous = false;
};

auto buildCtsPgBindingIndex(const DatabaseContext& context) -> CtsPgBindingIndex
{
  CtsPgBindingIndex result;
  const auto& netlist = context.design().netlistStorage();
  const auto& master_terms = context.library()->masterTermStorage();

  netlist.forEachSpecialNet([&](const DesignNetId net, const DesignNet& value) {
    for (const auto term_id : netlist.instanceTerms(net)) {
      const auto& term = netlist.instanceTerm(term_id);
      const auto& master_term = master_terms.masterTerm(term.master_term);
      if (!pgSignalUse(master_term.use).has_value()) {
        continue;
      }
      const CtsPgBindingIndex::TermKey key{master_term.use, master_term.name};
      const auto found = result.nets_by_term.find(key);
      if (found == result.nets_by_term.end()) {
        result.nets_by_term.emplace(key, net);
      } else if (found->second != net) {
        result.ambiguous_terms.insert(key);
      }
    }

    for (const auto use : {LibraryMasterTermUse::kPower, LibraryMasterTermUse::kGround}) {
      const auto expected = pgSignalUse(use);
      if (expected.has_value() && value.use == *expected) {
        auto& candidates = result.nets_by_use[use];
        if (std::find(candidates.begin(), candidates.end(), net) == candidates.end()) {
          candidates.push_back(net);
        }
      }
    }
  });
  return result;
}

auto resolveCtsPgBinding(const CtsPgBindingIndex& index, const LibraryMasterTerm& term) -> CtsPgBindingResolution
{
  const auto expected = pgSignalUse(term.use);
  if (!expected.has_value()) {
    return {};
  }

  const CtsPgBindingIndex::TermKey key{term.use, term.name};
  if (index.ambiguous_terms.contains(key)) {
    return {.ambiguous = true};
  }
  if (const auto found = index.nets_by_term.find(key); found != index.nets_by_term.end()) {
    return {.net = found->second};
  }
  if (const auto found = index.nets_by_use.find(term.use); found != index.nets_by_use.end()) {
    if (found->second.size() == 1U) {
      return {.net = found->second.front()};
    }
    if (!found->second.empty()) {
      return {.ambiguous = true};
    }
  }
  return {};
}

auto classifyInst(const LibraryCellMaster& master, idb::LibCell* liberty_cell) -> ::icts::InstType
{
  if (isMacroMaster(master.type)) {
    return ::icts::InstType::kMacroBlock;
  }
  if (liberty_cell == nullptr) {
    return ::icts::InstType::kUnknown;
  }
  if (liberty_cell->isICG()) {
    return ::icts::InstType::kClockGate;
  }
  if (liberty_cell->isSequentialCell()) {
    return ::icts::InstType::kFlipFlop;
  }
  if (liberty_cell->isBuffer()) {
    return ::icts::InstType::kBuffer;
  }
  if (liberty_cell->isInverter()) {
    return ::icts::InstType::kInverter;
  }
  return ::icts::InstType::kUnknown;
}

auto globMatches(std::string_view pattern, std::string_view value) -> bool
{
  std::size_t pattern_index = 0U;
  std::size_t value_index = 0U;
  std::size_t star_index = std::string_view::npos;
  std::size_t checkpoint = 0U;
  while (value_index < value.size()) {
    if (pattern_index < pattern.size() && (pattern[pattern_index] == '?' || pattern[pattern_index] == value[value_index])) {
      ++pattern_index;
      ++value_index;
    } else if (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
      star_index = pattern_index++;
      checkpoint = value_index;
    } else if (star_index != std::string_view::npos) {
      pattern_index = star_index + 1U;
      value_index = ++checkpoint;
    } else {
      return false;
    }
  }
  while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
    ++pattern_index;
  }
  return pattern_index == pattern.size();
}

auto trimmedName(std::string_view name) -> std::string
{
  while (!name.empty() && (name.front() == '{' || name.front() == '"' || name.front() == ' ' || name.front() == '\t')) {
    name.remove_prefix(1U);
  }
  while (!name.empty() && (name.back() == '}' || name.back() == '"' || name.back() == ' ' || name.back() == '\t')) {
    name.remove_suffix(1U);
  }
  return std::string{name};
}

template <typename Id>
auto idKey(Id id) -> uint64_t
{
  return static_cast<uint64_t>(id.packed());
}

struct NativeEndpoint
{
  bool is_instance = false;
  DesignInstanceTermId instance_term;
  DesignBlockTermId block_term;
  LibraryMasterTermDirection instance_direction = LibraryMasterTermDirection::kNone;
  DesignIoPinDirection block_direction = DesignIoPinDirection::kNone;
  std::string debug_name;
  Point location;
  std::string cell_master;
  std::string port_name;
};

auto collectEndpoints(const PhysicalDesignService& physical, DesignNetId net) -> std::vector<NativeEndpoint>
{
  std::vector<NativeEndpoint> endpoints;
  physical.forEachNetPin(net, PhysicalNetPinVisitor{
                                   .instance_pin = [&](DesignInstancePinId pin, const DesignInstanceTerm&, const DesignInstance& instance,
                                                       const LibraryMasterTerm& term) {
                                     endpoints.push_back(NativeEndpoint{.is_instance = true,
                                                                       .instance_term = pin,
                                                                       .instance_direction = term.direction,
                                                                       .debug_name = instance.name + "/" + term.name});
                                   },
                                   .io_pin = [&](DesignIoPinId pin, const DesignIoPin& term) {
                                     endpoints.push_back(NativeEndpoint{.is_instance = false,
                                                                       .block_term = pin,
                                                                       .block_direction = term.direction,
                                                                       .debug_name = term.name});
                                   }});
  return endpoints;
}

auto resolveDriverIndex(const std::vector<NativeEndpoint>& endpoints) -> std::optional<std::size_t>
{
  std::vector<std::size_t> candidates;
  for (std::size_t index = 0U; index < endpoints.size(); ++index) {
    if (endpoints[index].is_instance && isOutputLike(endpoints[index].instance_direction)) {
      candidates.push_back(index);
    }
  }
  if (candidates.size() == 1U) {
    return candidates.front();
  }
  if (!candidates.empty()) {
    return std::nullopt;
  }
  for (std::size_t index = 0U; index < endpoints.size(); ++index) {
    if (!endpoints[index].is_instance && endpoints[index].block_direction == DesignIoPinDirection::kInput) {
      candidates.push_back(index);
    }
  }
  if (candidates.size() == 1U) {
    return candidates.front();
  }
  if (!candidates.empty()) {
    return std::nullopt;
  }
  for (std::size_t index = 0U; index < endpoints.size(); ++index) {
    if (!endpoints[index].is_instance && endpoints[index].block_direction == DesignIoPinDirection::kInOut) {
      candidates.push_back(index);
    }
  }
  return candidates.size() == 1U ? std::optional<std::size_t>{candidates.front()} : std::nullopt;
}

auto averageInstancePinLocation(const PhysicalDesignService& physical, DesignInstancePinId pin, Point fallback) -> Point
{
  int64_t sum_x = 0;
  int64_t sum_y = 0;
  int64_t count = 0;
  if (physical.isPlaced(physical.design().netlistStorage().instanceTerm(pin).instance)) {
    physical.forEachPlacedInstancePinGeometry(pin, PhysicalGeometryVisitor{
                                                       .rectangle = [&](const PhysicalLayerRect& shape) {
                                                         const auto center = shape.rectangle.center();
                                                         sum_x += center.x;
                                                         sum_y += center.y;
                                                         ++count;
                                                       },
                                                       .polygon = [&](const PhysicalLayerPolygon& shape) {
                                                         for (const auto point : shape.points) {
                                                           sum_x += point.x;
                                                           sum_y += point.y;
                                                           ++count;
                                                         }
                                                       }});
  }
  if (count == 0) {
    return fallback;
  }
  return Point{static_cast<int32_t>(sum_x / count), static_cast<int32_t>(sum_y / count)};
}

// Match iDB's RECT pin-anchor arithmetic: sum master-local rectangle
// endpoints, divide once, then apply the instance placement transform.
// Keep the original placed-center algorithm above for polygon pins: iDB
// decomposes those into rectangles, which is a separate compatibility case.
// This matches direct RECT inputs; expanded PATH/ITERATE geometry no longer
// carries the LEF item provenance needed to reproduce iDB's ignored items.
auto averageInstancePinLocationLikeIdb(const PhysicalDesignService& physical, DesignInstancePinId pin, Point fallback) -> Point
{
  const auto& instance_term = physical.design().netlistStorage().instanceTerm(pin);
  if (!physical.isPlaced(instance_term.instance)) {
    return fallback;
  }

  int64_t sum_x = 0;
  int64_t sum_y = 0;
  int64_t endpoint_count = 0;
  bool has_polygon = false;
  const PhysicalGeometryVisitor visitor{
      .rectangle = [&](const PhysicalLayerRect& shape) {
        const auto& rectangle = shape.rectangle;
        sum_x += static_cast<int64_t>(rectangle.ll_x) + rectangle.ur_x;
        sum_y += static_cast<int64_t>(rectangle.ll_y) + rectangle.ur_y;
        endpoint_count += 2;
      },
      .polygon = [&](const PhysicalLayerPolygon&) { has_polygon = true; }};
  for (const auto master_pin : physical.masterPins(instance_term.master_term)) {
    physical.forEachMasterPinGeometry(master_pin, visitor);
  }
  if (has_polygon) {
    return averageInstancePinLocation(physical, pin, fallback);
  }
  if (endpoint_count == 0) {
    return fallback;
  }

  const Point master_average{static_cast<int32_t>(sum_x / endpoint_count), static_cast<int32_t>(sum_y / endpoint_count)};
  return physical.placeInstanceMasterPoint(instance_term.instance, master_average);
}

auto averageBlockPinLocation(const PhysicalDesignService& physical, DesignBlockTermId pin, Point fallback) -> Point
{
  int64_t sum_x = 0;
  int64_t sum_y = 0;
  int64_t count = 0;
  physical.forEachIoPinGeometry(pin, PhysicalGeometryVisitor{
                                             .rectangle = [&](const PhysicalLayerRect& shape) {
                                               const auto center = shape.rectangle.center();
                                               sum_x += center.x;
                                               sum_y += center.y;
                                               ++count;
                                             },
                                             .polygon = [&](const PhysicalLayerPolygon& shape) {
                                               for (const auto point : shape.points) {
                                                 sum_x += point.x;
                                                 sum_y += point.y;
                                                 ++count;
                                               }
                                             }});
  if (count == 0) {
    return fallback;
  }
  return Point{static_cast<int32_t>(sum_x / count), static_cast<int32_t>(sum_y / count)};
}

class NativeDesignMaterializer
{
 public:
  NativeDesignMaterializer(DatabaseContext& context, CtsLibertyCellLookup liberty_cell_lookup, ::icts::Design& design)
      : _context(context), _physical(context), _liberty_cell_lookup(std::move(liberty_cell_lookup)), _design(design)
  {
  }

  auto materialize(const std::vector<::icts::ClockTraceClockTarget>& targets) -> bool
  {
    _design.clearClocks();
    _design.clearTopologyObjects();
    _inst_by_id.clear();
    _pin_by_instance_term.clear();
    _pin_by_block_term.clear();
    _net_by_id.clear();

    for (const auto& target : targets) {
      const auto net_id = _physical.findRegularNet(target.clock_net_name);
      if (!net_id) {
        ::icts::Logger::getInst().warn(::icts::Loc::current(), "CTS native input: SDC target net \"", target.clock_net_name,
                                       "\" is not in EccDB.");
        return false;
      }
      if (!materializeClock(target.clock_name, target.clock_net_name, net_id)) {
        return false;
      }
    }
    return true;
  }

 private:
  auto materializeClock(const std::string& clock_name, const std::string& clock_net_name, DesignNetId net_id) -> bool
  {
    auto* clock = _design.makeClock(clock_name, clock_net_name);
    if (clock == nullptr) {
      return false;
    }
    clock->set_clock_source(nullptr);
    clock->set_clock_source_net(nullptr);
    clock->set_preclustered_sink_reuse(false);
    clock->clear_preclustered_anchor_input_net_names();
    clock->clear_loads();
    clock->clearMembership();

    auto* net = ensureNet(net_id);
    if (net == nullptr) {
      return false;
    }
    net->set_driver(nullptr);
    net->set_loads({});
    clock->set_clock_source_net(net);

    const auto endpoints = collectEndpoints(_physical, net_id);
    const auto driver_index = resolveDriverIndex(endpoints);
    if (!driver_index.has_value()) {
      ::icts::Logger::getInst().warn(::icts::Loc::current(), "CTS native input: clock net \"", clock_net_name,
                                     "\" has no unique driver.");
      return false;
    }

    std::vector<::icts::Pin*> loads;
    loads.reserve(endpoints.size() - 1U);
    for (std::size_t index = 0U; index < endpoints.size(); ++index) {
      auto* pin = ensurePin(endpoints[index]);
      if (pin == nullptr) {
        return false;
      }
      pin->set_net(net);
      if (index == *driver_index) {
        net->set_driver(pin);
        clock->set_clock_source(pin);
      } else {
        loads.push_back(pin);
      }
    }
    net->set_loads(loads);
    clock->set_loads(loads);
    return true;
  }

  auto ensureNet(DesignNetId net_id) -> ::icts::Net*
  {
    const auto key = idKey(net_id);
    if (const auto iter = _net_by_id.find(key); iter != _net_by_id.end()) {
      return iter->second;
    }
    const auto& net_data = _context.design().netlistStorage().net(net_id);
    auto* net = _design.makeNet(net_data.name);
    if (net != nullptr) {
      _net_by_id.emplace(key, net);
    }
    return net;
  }

  auto ensureInst(DesignInstanceId inst_id) -> ::icts::Inst*
  {
    const auto key = idKey(inst_id);
    if (const auto iter = _inst_by_id.find(key); iter != _inst_by_id.end()) {
      return iter->second;
    }
    const auto& netlist = _context.design().netlistStorage();
    const auto& library = *_context.library();
    const auto& instance = netlist.instance(inst_id);
    const auto& master = library.cellMasterStorage().cellMaster(instance.master);
    auto* inst = _design.makeInst(instance.name);
    if (inst == nullptr) {
      return nullptr;
    }
    inst->set_name(instance.name);
    inst->set_cell_master(master.name);
    inst->set_type(classifyInst(master, _liberty_cell_lookup ? _liberty_cell_lookup(master.name) : nullptr));
    inst->set_location(::icts::Point<int>{instance.origin.x, instance.origin.y});
    _inst_by_id.emplace(key, inst);
    return inst;
  }

  auto ensurePin(const NativeEndpoint& endpoint) -> ::icts::Pin*
  {
    if (endpoint.is_instance) {
      const auto key = idKey(endpoint.instance_term);
      if (const auto iter = _pin_by_instance_term.find(key); iter != _pin_by_instance_term.end()) {
        return iter->second;
      }
      const auto& netlist = _context.design().netlistStorage();
      const auto& library = *_context.library();
      const auto& term = netlist.instanceTerm(endpoint.instance_term);
      const auto& instance = netlist.instance(term.instance);
      const auto& master_term = library.masterTermStorage().masterTerm(term.master_term);
      auto* inst = ensureInst(term.instance);
      if (inst == nullptr) {
        return nullptr;
      }
      auto* pin = _design.makePin(master_term.name);
      if (pin == nullptr) {
        return nullptr;
      }
      pin->set_name(master_term.name);
      pin->set_type(toCtsPinType(master_term));
      const auto location = averageInstancePinLocationLikeIdb(_physical, endpoint.instance_term, instance.origin);
      pin->set_location(::icts::Point<int>{location.x, location.y});
      pin->set_inst(inst);
      pin->set_io(false);
      if (!_design.indexPin(pin)) {
        return nullptr;
      }
      inst->add_pin(pin);
      _pin_by_instance_term.emplace(key, pin);
      return pin;
    }

    const auto key = idKey(endpoint.block_term);
    if (const auto iter = _pin_by_block_term.find(key); iter != _pin_by_block_term.end()) {
      return iter->second;
    }
    const auto& term = _context.design().netlistStorage().blockTerm(endpoint.block_term);
    auto* pin = _design.makePin(term.name);
    if (pin == nullptr) {
      return nullptr;
    }
    pin->set_name(term.name);
    pin->set_type(toCtsPinType(term));
    const auto location = averageBlockPinLocation(_physical, endpoint.block_term, Point{});
    pin->set_location(::icts::Point<int>{location.x, location.y});
    pin->set_inst(nullptr);
    pin->set_io(true);
    if (!_design.indexPin(pin)) {
      return nullptr;
    }
    _pin_by_block_term.emplace(key, pin);
    return pin;
  }

  DatabaseContext& _context;
  PhysicalDesignService _physical;
  CtsLibertyCellLookup _liberty_cell_lookup;
  ::icts::Design& _design;
  std::unordered_map<uint64_t, ::icts::Inst*> _inst_by_id;
  std::unordered_map<uint64_t, ::icts::Pin*> _pin_by_instance_term;
  std::unordered_map<uint64_t, ::icts::Pin*> _pin_by_block_term;
  std::unordered_map<uint64_t, ::icts::Net*> _net_by_id;
};

auto findRoutingLayer(const PhysicalDesignService& physical, int routing_layer) -> const TechRoutingLayer*
{
  if (routing_layer <= 0) {
    return nullptr;
  }
  const TechRoutingLayer* matching_level = nullptr;
  const TechRoutingLayer* matching_sequence = nullptr;
  physical.forEachRoutingLayer([&](TechRoutingLayerId, uint32_t sequence_index, uint32_t routing_level, const TechLayerInfo&, const TechRoutingLayer& layer) {
    if (routing_level == static_cast<uint32_t>(routing_layer)) {
      matching_level = &layer;
    }
    if (sequence_index + 1U == static_cast<uint32_t>(routing_layer)) {
      matching_sequence = &layer;
    }
  });
  return matching_level != nullptr ? matching_level : matching_sequence;
}

auto wireWidthUm(const PhysicalDesignService& physical, const TechRoutingLayer& layer, std::optional<double> requested_width) -> std::optional<double>
{
  if (requested_width.has_value()) {
    return std::isfinite(*requested_width) && *requested_width > 0.0 ? requested_width : std::nullopt;
  }
  const auto dbu_per_um = physical.databaseUnitsPerMicron();
  if (dbu_per_um <= 0 || layer.width <= 0) {
    return std::nullopt;
  }
  return static_cast<double>(layer.width) / static_cast<double>(dbu_per_um);
}

auto clockNets(const ::icts::Design& design, const std::vector<::icts::Clock*>& clocks) -> std::vector<::icts::Net*>
{
  std::vector<::icts::Net*> nets;
  std::unordered_set<const ::icts::Net*> seen;
  const auto add = [&](::icts::Net* net) {
    if (net != nullptr && seen.insert(net).second) {
      nets.push_back(net);
    }
  };
  for (auto* clock : clocks) {
    if (clock == nullptr) {
      continue;
    }
    add(clock->get_clock_source_net());
    for (auto* net : clock->get_nets()) {
      add(net);
    }
  }
  // A direct native clock has only its source net in Clock membership.  The
  // full local Design is still the authoritative set after synthesis.
  for (auto* net : design.get_nets()) {
    add(net);
  }
  return nets;
}

auto nativeIsCombinationalArc(idb::LibArc* arc) -> bool
{
  if (arc == nullptr || arc->isDisableArc() != 0U || arc->isDelayArc() == 0U) {
    return false;
  }
  const auto timing_type = arc->get_timing_type();
  return timing_type == idb::LibArc::TimingType::kDefault || timing_type == idb::LibArc::TimingType::kComb
         || timing_type == idb::LibArc::TimingType::kCombRise || timing_type == idb::LibArc::TimingType::kCombFall;
}

auto nativeFirstEnabledArc(idb::LibArcSet* arc_set) -> idb::LibArc*
{
  if (arc_set == nullptr) {
    return nullptr;
  }
  idb::LibArc* first = nullptr;
  for (const auto& holder : arc_set->get_arcs()) {
    auto* arc = holder.get();
    if (arc == nullptr || arc->isDisableArc() != 0U) {
      continue;
    }
    if (first == nullptr) {
      first = arc;
    }
    if (arc->get_when().empty()) {
      return arc;
    }
  }
  return first;
}

auto nativeTransition(idb::LibArc* arc) -> ::icts::WrapperTimingTransition
{
  return arc != nullptr && (arc->isFallingTriggerArc() != 0U || arc->isFallingEdgeCheck() != 0U)
             ? ::icts::WrapperTimingTransition::kFall
             : ::icts::WrapperTimingTransition::kRise;
}

auto appendNativeTimingNode(const NativeEndpoint& endpoint, idb::LibCell* lib_cell, ::icts::WrapperTimingGraph& graph,
                            std::unordered_map<std::string, std::size_t>& node_index) -> bool
{
  if (endpoint.debug_name.empty()) {
    return false;
  }
  if (node_index.contains(endpoint.debug_name)) {
    return true;
  }

  ::icts::WrapperTimingNode node;
  node.pin_name = endpoint.debug_name;
  node.inst_name = endpoint.is_instance ? endpoint.debug_name.substr(0, endpoint.debug_name.find('/')) : std::string{};
  node.cell_master = endpoint.cell_master;
  node.port_name = endpoint.port_name;
  node.top_level = !endpoint.is_instance;
  node.x_dbu = endpoint.location.x;
  node.y_dbu = endpoint.location.y;

  if (node.top_level) {
    node.input = endpoint.block_direction == DesignIoPinDirection::kInput || endpoint.block_direction == DesignIoPinDirection::kInOut;
    node.output = endpoint.block_direction == DesignIoPinDirection::kOutput || endpoint.block_direction == DesignIoPinDirection::kInOut;
    if (!node.input && !node.output) {
      graph.status = ::icts::WrapperTimingGraphStatus::kConnectivityInvalid;
      graph.diagnostic = "native_top_level_pin_direction_unavailable:" + endpoint.debug_name;
      return false;
    }
    node_index.emplace(node.pin_name, graph.nodes.size());
    graph.nodes.push_back(std::move(node));
    return true;
  }

  if (lib_cell == nullptr) {
    graph.status = ::icts::WrapperTimingGraphStatus::kLibraryUnavailable;
    graph.diagnostic = "native_signal_pin_liberty_model_unavailable:" + endpoint.debug_name + ":" + endpoint.cell_master;
    return false;
  }
  auto* lib_port = lib_cell->get_cell_port_or_port_bus(endpoint.port_name.c_str());
  if (lib_port == nullptr) {
    graph.status = ::icts::WrapperTimingGraphStatus::kLibraryUnavailable;
    graph.diagnostic = "native_signal_pin_liberty_port_unavailable:" + endpoint.debug_name + ":" + endpoint.port_name;
    return false;
  }
  auto* owner_lib = lib_cell->get_owner_lib();
  if (owner_lib == nullptr || !owner_lib->has_time_unit() || !owner_lib->has_cap_unit()) {
    graph.status = ::icts::WrapperTimingGraphStatus::kLibraryUnavailable;
    graph.diagnostic = "native_signal_pin_liberty_units_unavailable:" + endpoint.debug_name;
    return false;
  }

  node.logic_function = lib_port->get_func_expr_str();
  node.clock_pin = lib_port->isClock() || lib_port->get_clock_gate_clock_pin();
  node.input = lib_port->isInput() != 0U;
  node.output = lib_port->isOutput() != 0U;
  if (node.input) {
    for (const auto& [analysis_index, analysis] : {std::pair{0U, idb::AnalysisMode::kMin}, std::pair{1U, idb::AnalysisMode::kMax}}) {
      for (const auto& [transition_index, transition] : {std::pair{0U, idb::TransType::kRise}, std::pair{1U, idb::TransType::kFall}}) {
        auto cap = lib_port->get_port_cap(analysis, transition);
        if (!cap.has_value() && lib_port->has_port_cap()) {
          cap = lib_port->get_port_cap();
        }
        if (!cap.has_value() || !std::isfinite(*cap) || *cap < 0.0) {
          graph.status = ::icts::WrapperTimingGraphStatus::kLibraryUnavailable;
          graph.diagnostic = "native_signal_pin_capacitance_unavailable:" + endpoint.debug_name;
          return false;
        }
        node.input_cap_pf_by_timing.at(analysis_index).at(transition_index) = *cap;
      }
    }
    node.input_cap_pf = node.input_cap_pf_by_timing.at(1U).at(0U);
    node.input_cap_profile_available = true;
  }
  auto slew = lib_port->get_port_slew_limit(idb::AnalysisMode::kMax);
  if ((!slew.has_value() || !std::isfinite(*slew) || *slew <= 0.0) && owner_lib != nullptr) {
    slew = owner_lib->get_default_max_transition();
  }
  if (slew.has_value() && std::isfinite(*slew) && *slew > 0.0) {
    node.max_slew_ns = *slew;
  }
  node.slew_limit_from_master = true;
  node_index.emplace(node.pin_name, graph.nodes.size());
  graph.nodes.push_back(std::move(node));
  return true;
}

auto makeNativeRoutingLayer(TechRoutingLayerId layer_id, uint32_t routing_level, const TechLayerInfo& info,
                            const TechRoutingLayer& layer, int32_t dbu_per_um) -> std::optional<::icts::WrapperSignalRoutingLayer>
{
  if (dbu_per_um <= 0 || layer.width <= 0 || (layer.direction != TechRoutingDirection::kHorizontal && layer.direction != TechRoutingDirection::kVertical)
      || !std::isfinite(layer.resistance) || layer.resistance <= 0.0 || !std::isfinite(layer.capacitance) || layer.capacitance < 0.0
      || !std::isfinite(layer.edge_capacitance) || layer.edge_capacitance < 0.0) {
    return std::nullopt;
  }
  const double width_um = static_cast<double>(layer.width) / static_cast<double>(dbu_per_um);
  const double resistance_per_um = layer.resistance / width_um;
  const double capacitance_per_um = layer.capacitance * width_um + 2.0 * layer.edge_capacitance;
  if (!std::isfinite(width_um) || width_um <= 0.0 || !std::isfinite(resistance_per_um) || resistance_per_um <= 0.0
      || !std::isfinite(capacitance_per_um) || capacitance_per_um <= 0.0) {
    return std::nullopt;
  }
  return ::icts::WrapperSignalRoutingLayer{.name = info.name,
                                           .id = static_cast<int32_t>(layer_id.packed()),
                                           .order = routing_level,
                                           .direction = layer.direction == TechRoutingDirection::kHorizontal
                                                            ? ::icts::WrapperRoutingDirection::kHorizontal
                                                            : ::icts::WrapperRoutingDirection::kVertical,
                                           .width_dbu = layer.width,
                                           .width_um = width_um,
                                           .sheet_resistance_ohm_per_square = layer.resistance,
                                           .area_capacitance_pf_per_um2 = layer.capacitance,
                                           .edge_capacitance_pf_per_um = layer.edge_capacitance,
                                           .resistance_ohm_per_um = resistance_per_um,
                                           .capacitance_pf_per_um = capacitance_per_um};
}

auto makeNativeRoutingAuthority(const PhysicalDesignService& physical, int32_t dbu_per_um, std::string_view anchor_name,
                                std::optional<uint32_t> anchor_level)
    -> ::icts::WrapperSignalRoutingAuthority
{
  struct Candidate
  {
    TechRoutingLayerId id;
    uint32_t sequence = 0U;
    uint32_t level = 0U;
    const TechLayerInfo* info = nullptr;
    const TechRoutingLayer* layer = nullptr;
  };
  std::vector<Candidate> candidates;
  physical.forEachRoutingLayer([&](TechRoutingLayerId id, uint32_t sequence, uint32_t level, const TechLayerInfo& info, const TechRoutingLayer& layer) {
    candidates.push_back(Candidate{.id = id, .sequence = sequence, .level = level, .info = &info, .layer = &layer});
  });
  std::ranges::sort(candidates, [](const Candidate& lhs, const Candidate& rhs) { return std::tie(lhs.sequence, lhs.level) < std::tie(rhs.sequence, rhs.level); });

  ::icts::WrapperSignalRoutingAuthority result;
  result.policy_version = "eccdb-tech-routing-rc-v1";
  if (candidates.empty()) {
    result.status = ::icts::WrapperSignalRoutingStatus::kLayoutUnavailable;
    result.diagnostic = "EccDB has no routing layers";
    return result;
  }
  std::size_t anchor_index = 0U;
  if (anchor_level.has_value() || !anchor_name.empty()) {
    auto iter = candidates.end();
    // A named platform anchor is authoritative.  The numeric iCTS value
    // belongs to the CTS characterization space and is only a fallback for
    // callers that have no technology-layer name.  Prefer the name even
    // when both are supplied; otherwise sky130's numeric value 4 can select
    // li1 instead of the configured met1 anchor.
    if (!anchor_name.empty()) {
      iter = std::ranges::find_if(candidates, [&](const Candidate& candidate) {
        return candidate.info != nullptr && candidate.info->name == anchor_name;
      });
    } else if (anchor_level.has_value()) {
      // Routing levels exclude CUT layers.  A legacy ordinal is only useful
      // when the technology has no exact routing-level match; combining both
      // predicates in one search can select an earlier sequence match over a
      // later, exact routing level.
      iter = std::ranges::find_if(candidates, [&](const Candidate& candidate) { return candidate.level == *anchor_level; });
      if (iter == candidates.end()) {
        iter = std::ranges::find_if(candidates, [&](const Candidate& candidate) { return candidate.sequence + 1U == *anchor_level; });
      }
    }
    if (iter == candidates.end()) {
      result.status = ::icts::WrapperSignalRoutingStatus::kAnchorUnavailable;
      result.anchor_name = anchor_name.empty() ? std::string{} : std::string(anchor_name);
      result.anchor_order = anchor_level.value_or(0U);
      result.diagnostic = "configured signal-routing anchor is absent from EccDB technology";
      return result;
    }
    anchor_index = static_cast<std::size_t>(std::distance(candidates.begin(), iter));
  }
  result.anchor_name = candidates[anchor_index].info == nullptr ? std::string{} : candidates[anchor_index].info->name;
  result.anchor_order = candidates[anchor_index].level;
  for (std::size_t index = anchor_index; index < candidates.size(); ++index) {
    const auto& candidate = candidates[index];
    if (candidate.info == nullptr || candidate.layer == nullptr) {
      continue;
    }
    const auto layer = makeNativeRoutingLayer(candidate.id, candidate.level, *candidate.info, *candidate.layer, dbu_per_um);
    if (!layer.has_value()) {
      continue;
    }
    if (layer->direction == ::icts::WrapperRoutingDirection::kHorizontal && !result.horizontal.has_value()) {
      result.horizontal = layer;
    } else if (layer->direction == ::icts::WrapperRoutingDirection::kVertical && !result.vertical.has_value()) {
      result.vertical = layer;
    }
    if (result.horizontal.has_value() && result.vertical.has_value()) {
      result.status = ::icts::WrapperSignalRoutingStatus::kComplete;
      return result;
    }
  }
  result.status = ::icts::WrapperSignalRoutingStatus::kDirectionalLayerUnavailable;
  result.diagnostic = "no valid horizontal/vertical EccDB routing-layer pair exists at or above anchor";
  return result;
}

auto buildNativeNetWireModel(const std::vector<NativeEndpoint>& endpoints, std::size_t driver_index,
                             const std::vector<std::size_t>& load_indices, const ::icts::WrapperSignalRoutingAuthority& authority,
                             int32_t dbu_per_um, ::icts::WrapperTimingNet& result) -> bool
{
  if (!authority.complete() || !authority.horizontal.has_value() || !authority.vertical.has_value() || dbu_per_um <= 0 || load_indices.empty()
      || driver_index >= endpoints.size()) {
    return false;
  }
  const auto& horizontal = *authority.horizontal;
  const auto& vertical = *authority.vertical;
  std::vector<std::pair<int32_t, int32_t>> locations;
  locations.reserve(load_indices.size() + 1U);
  locations.emplace_back(endpoints[driver_index].location.x, endpoints[driver_index].location.y);
  for (const auto index : load_indices) {
    if (index >= endpoints.size()) {
      return false;
    }
    locations.emplace_back(endpoints[index].location.x, endpoints[index].location.y);
  }
  std::ranges::sort(locations);
  locations.erase(std::ranges::unique(locations).begin(), locations.end());
  const auto driver_location = std::pair(endpoints[driver_index].location.x, endpoints[driver_index].location.y);
  const auto driver_iter = std::ranges::find(locations, driver_location);
  if (driver_iter == locations.end()) {
    return false;
  }
  std::ranges::rotate(locations, driver_iter);
  int64_t horizontal_dbu = 0;
  int64_t vertical_dbu = 0;
  bool valid = true;
  const auto append_segment = [&](int32_t source_x, int32_t source_y, int32_t target_x, int32_t target_y) {
    const auto dx = std::abs(static_cast<int64_t>(source_x) - static_cast<int64_t>(target_x));
    const auto dy = std::abs(static_cast<int64_t>(source_y) - static_cast<int64_t>(target_y));
    if (horizontal_dbu > std::numeric_limits<int64_t>::max() - dx || vertical_dbu > std::numeric_limits<int64_t>::max() - dy) {
      valid = false;
      return;
    }
    const double h_um = static_cast<double>(dx) / static_cast<double>(dbu_per_um);
    const double v_um = static_cast<double>(dy) / static_cast<double>(dbu_per_um);
    const double resistance = h_um * horizontal.resistance_ohm_per_um + v_um * vertical.resistance_ohm_per_um;
    const double capacitance = h_um * horizontal.capacitance_pf_per_um + v_um * vertical.capacitance_pf_per_um;
    if (resistance > 0.0 || capacitance > 0.0) {
      result.rc_segments.push_back(::icts::WrapperTimingRcSegment{.begin_x_dbu = source_x,
                                                                   .begin_y_dbu = source_y,
                                                                   .end_x_dbu = target_x,
                                                                   .end_y_dbu = target_y,
                                                                   .resistance_ohm = resistance,
                                                                   .capacitance_pf = capacitance});
    }
    horizontal_dbu += dx;
    vertical_dbu += dy;
  };
  if (locations.size() == 2U) {
    append_segment(locations[0].first, locations[0].second, locations[1].first, locations[1].second);
  } else if (locations.size() > 2U) {
    std::vector<std::shared_ptr<salt::Pin>> pins;
    pins.reserve(locations.size());
    for (std::size_t index = 0U; index < locations.size(); ++index) {
      pins.push_back(std::make_shared<salt::Pin>(locations[index].first, locations[index].second, static_cast<int>(index), 0.0));
    }
    salt::Net net;
    net.init(0, result.net_name, pins);
    salt::Tree tree;
    salt::FluteBuilder builder;
    builder.Run(net, tree);
    tree.UpdateId();
    if (tree.source == nullptr) {
      return false;
    }
    salt::TreeNode::preOrder(tree.source, [&](const std::shared_ptr<salt::TreeNode>& node) {
      if (node == nullptr || node == tree.source || node->parent == nullptr) {
        if (node != nullptr && node != tree.source && node->parent == nullptr) {
          valid = false;
        }
        return;
      }
      append_segment(node->loc.x, node->loc.y, node->parent->loc.x, node->parent->loc.y);
    });
  }
  if (!valid || horizontal_dbu > std::numeric_limits<int64_t>::max() - vertical_dbu) {
    return false;
  }
  result.total_wirelength_dbu = horizontal_dbu + vertical_dbu;
  const double h_um = static_cast<double>(horizontal_dbu) / static_cast<double>(dbu_per_um);
  const double v_um = static_cast<double>(vertical_dbu) / static_cast<double>(dbu_per_um);
  result.wire_resistance_ohm = h_um * horizontal.resistance_ohm_per_um + v_um * vertical.resistance_ohm_per_um;
  result.wire_cap_pf = h_um * horizontal.capacitance_pf_per_um + v_um * vertical.capacitance_pf_per_um;
  result.physically_zero_length = result.total_wirelength_dbu == 0;
  return std::isfinite(result.wire_resistance_ohm) && result.wire_resistance_ohm >= 0.0 && std::isfinite(result.wire_cap_pf)
         && result.wire_cap_pf >= 0.0;
}

}  // namespace

CtsEccdbAdapter::CtsEccdbAdapter(DatabaseContext& context, CtsLibertyCellLookup liberty_cell_lookup, std::string sdc_path)
    : _context(&context), _liberty_cell_lookup(std::move(liberty_cell_lookup)), _sdc_path(std::move(sdc_path))
{
}

void CtsEccdbAdapter::setLibertyCellLookup(CtsLibertyCellLookup liberty_cell_lookup)
{
  _liberty_cell_lookup = std::move(liberty_cell_lookup);
}

::icts::SdcClockData CtsEccdbAdapter::readClockData() const
{
  return ::icts::SdcClockReader(_sdc_path).readClockData();
}

idb::LibCell* CtsEccdbAdapter::findLibertyCell(const std::string& cell_master) const
{
  return _liberty_cell_lookup ? _liberty_cell_lookup(cell_master) : nullptr;
}

bool CtsEccdbAdapter::isDesignReady() const noexcept
{
  return _context != nullptr && _context->technology() != nullptr && _context->library() != nullptr;
}

bool CtsEccdbAdapter::isLayoutReady() const noexcept
{
  return isDesignReady();
}

std::optional<int32_t> CtsEccdbAdapter::queryDbUnit() const
{
  if (!isDesignReady()) {
    return std::nullopt;
  }
  try {
    const auto dbu = PhysicalDesignService{*_context}.databaseUnitsPerMicron();
    return dbu > 0 ? std::optional<int32_t>{dbu} : std::nullopt;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<bool> CtsEccdbAdapter::withinCore(int32_t point_x, int32_t point_y) const
{
  if (_context == nullptr) {
    return std::nullopt;
  }
  const auto& floorplan = _context->design().floorplanStorage();
  if (!floorplan.hasCoreArea()) {
    return std::nullopt;
  }
  try {
    return floorplan.coreBounds().contains(Point{point_x, point_y});
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<::icts::WrapperCellGeometry> CtsEccdbAdapter::queryInstGeometry(const std::string& inst_name) const
{
  if (!isDesignReady() || inst_name.empty()) {
    return std::nullopt;
  }
  try {
    const auto& netlist = _context->design().netlistStorage();
    const auto id = netlist.findInstance(inst_name);
    if (!id) {
      return std::nullopt;
    }
    const auto& instance = netlist.instance(id);
    const auto& master = _context->library()->cellMasterStorage().cellMaster(instance.master);
    return ::icts::WrapperCellGeometry{.name = instance.name,
                                       .cell_master = master.name,
                                       .origin = ::icts::Point<int>{instance.origin.x, instance.origin.y},
                                       .width_dbu = static_cast<int32_t>(master.width),
                                       .height_dbu = static_cast<int32_t>(master.height)};
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::vector<::icts::WrapperCellGeometry> CtsEccdbAdapter::collectLogicCellGeometries() const
{
  std::vector<::icts::WrapperCellGeometry> geometries;
  if (!isDesignReady()) {
    return geometries;
  }
  const auto& library = *_context->library();
  _context->design().netlistStorage().forEachInstance(
      [&](DesignInstanceId, const DesignInstance& instance, std::span<const DesignInstancePinId>) {
        const auto& master = library.cellMasterStorage().cellMaster(instance.master);
        if (!isLogicMaster(master.type) || instance.source == DesignInstanceSource::kTiming) {
          return;
        }
        geometries.push_back(::icts::WrapperCellGeometry{.name = instance.name,
                                                          .cell_master = master.name,
                                                          .origin = ::icts::Point<int>{instance.origin.x, instance.origin.y},
                                                          .width_dbu = static_cast<int32_t>(master.width),
                                                          .height_dbu = static_cast<int32_t>(master.height)});
      });
  return geometries;
}

std::optional<double> CtsEccdbAdapter::queryCellHeightUm(const std::string& cell_master) const
{
  if (!isDesignReady() || cell_master.empty()) {
    return std::nullopt;
  }
  const auto dbu = queryDbUnit();
  if (!dbu.has_value() || *dbu <= 0) {
    return std::nullopt;
  }
  const auto id = _context->library()->cellMasterStorage().findCellMaster(cell_master);
  if (!id) {
    return std::nullopt;
  }
  const auto height = _context->library()->cellMasterStorage().cellMaster(id).height;
  const auto value = static_cast<double>(height) / static_cast<double>(*dbu);
  return std::isfinite(value) && value > 0.0 ? std::optional<double>{value} : std::nullopt;
}

std::optional<double> CtsEccdbAdapter::queryCellAreaUm2(const std::string& cell_master) const
{
  if (!isDesignReady() || cell_master.empty()) {
    return std::nullopt;
  }
  const auto dbu = queryDbUnit();
  if (!dbu.has_value() || *dbu <= 0) {
    return std::nullopt;
  }
  const auto id = _context->library()->cellMasterStorage().findCellMaster(cell_master);
  if (!id) {
    return std::nullopt;
  }
  const auto& master = _context->library()->cellMasterStorage().cellMaster(id);
  const auto scale = static_cast<double>(*dbu);
  const auto value = (static_cast<double>(master.width) * static_cast<double>(master.height)) / (scale * scale);
  return std::isfinite(value) && value > 0.0 ? std::optional<double>{value} : std::nullopt;
}

std::optional<double> CtsEccdbAdapter::querySupplyVoltage() const
{
  if (!isDesignReady() || !_liberty_cell_lookup) {
    return std::nullopt;
  }
  const PhysicalDesignService physical{*_context};
  std::optional<double> voltage;
  std::size_t found_cells = 0U;
  bool invalid = false;
  physical.forEachMaster([&](LibraryCellMasterId, const LibraryCellMaster& master) {
    if (!isLogicMaster(master.type)) {
      return;
    }
    auto* cell = _liberty_cell_lookup(master.name);
    if (cell == nullptr || cell->get_owner_lib() == nullptr) {
      return;
    }
    const auto candidate = cell->get_owner_lib()->get_nom_voltage();
    if (!std::isfinite(candidate) || candidate <= 0.0) {
      invalid = true;
      return;
    }
    ++found_cells;
    if (voltage.has_value() && *voltage != candidate) {
      invalid = true;
      return;
    }
    voltage = candidate;
  });
  return !invalid && found_cells > 0U ? voltage : std::nullopt;
}

std::optional<::icts::SdcUnits> CtsEccdbAdapter::queryLibertyUnits() const
{
  if (!isDesignReady() || !_liberty_cell_lookup) {
    return std::nullopt;
  }
  const PhysicalDesignService physical{*_context};
  std::optional<::icts::SdcUnits> units;
  bool invalid = false;
  physical.forEachMaster([&](LibraryCellMasterId, const LibraryCellMaster& master) {
    if (!isLogicMaster(master.type)) {
      return;
    }
    auto* cell = _liberty_cell_lookup(master.name);
    auto* library = cell == nullptr ? nullptr : cell->get_owner_lib();
    if (library == nullptr || !library->has_cap_unit() || !library->has_time_unit()) {
      invalid = true;
      return;
    }
    const ::icts::SdcUnits candidate{.time_unit_ns = library->convert_time_unit_to_ns(1.0),
                                     .capacitance_unit_pf = idb::ConvertCapUnit(library->get_cap_unit(), idb::CapacitiveUnit::kPF, 1.0)};
    if (!std::isfinite(candidate.time_unit_ns) || candidate.time_unit_ns <= 0.0 || !std::isfinite(candidate.capacitance_unit_pf)
        || candidate.capacitance_unit_pf <= 0.0) {
      invalid = true;
      return;
    }
    if (units.has_value() && (units->time_unit_ns != candidate.time_unit_ns || units->capacitance_unit_pf != candidate.capacitance_unit_pf)) {
      invalid = true;
      return;
    }
    units = candidate;
  });
  return !invalid ? units : std::nullopt;
}

std::optional<double> CtsEccdbAdapter::queryWireResistance(int routing_layer, double length_um,
                                                            std::optional<double> wire_width_um) const
{
  if (!isDesignReady() || routing_layer <= 0 || !std::isfinite(length_um) || length_um < 0.0) {
    return std::nullopt;
  }
  if (length_um == 0.0) {
    return 0.0;
  }
  try {
    const PhysicalDesignService physical{*_context};
    const auto* layer = findRoutingLayer(physical, routing_layer);
    if (layer == nullptr) {
      return std::nullopt;
    }
    const auto width_um = wireWidthUm(physical, *layer, wire_width_um);
    if (!width_um.has_value()) {
      return std::nullopt;
    }
    const auto value = layer->resistance * length_um / *width_um;
    return std::isfinite(value) && value >= 0.0 ? std::optional<double>{value} : std::nullopt;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<::icts::Wrapper::WireCapacitanceProfile> CtsEccdbAdapter::queryWireCapacitanceProfile(
    int routing_layer, double length_um, std::optional<double> wire_width_um) const
{
  if (!isDesignReady() || routing_layer <= 0 || !std::isfinite(length_um) || length_um < 0.0) {
    return std::nullopt;
  }
  if (length_um == 0.0) {
    return ::icts::Wrapper::WireCapacitanceProfile{.timing_coupling_factor = kOppositeSwitchingCouplingFactor};
  }
  try {
    const PhysicalDesignService physical{*_context};
    const auto* layer = findRoutingLayer(physical, routing_layer);
    if (layer == nullptr) {
      return std::nullopt;
    }
    const auto width_um = wireWidthUm(physical, *layer, wire_width_um);
    if (!width_um.has_value()) {
      return std::nullopt;
    }
    const auto area_cap_pf = layer->capacitance * length_um * *width_um;
    const auto edge_cap_pf = layer->edge_capacitance * 2.0 * (length_um + *width_um);
    const auto profile = ::icts::Wrapper::WireCapacitanceProfile{.area_cap_pf = area_cap_pf,
                                                                  .edge_cap_pf = edge_cap_pf,
                                                                  .ground_cap_pf = area_cap_pf,
                                                                  .coupling_cap_pf = edge_cap_pf,
                                                                  .timing_coupling_factor = kOppositeSwitchingCouplingFactor,
                                                                  .total_cap_pf = area_cap_pf + edge_cap_pf,
                                                                  .timing_effective_cap_pf = area_cap_pf + kOppositeSwitchingCouplingFactor * edge_cap_pf};
    const auto finite_non_negative = [](double value) { return std::isfinite(value) && value >= 0.0; };
    return finite_non_negative(profile.area_cap_pf) && finite_non_negative(profile.edge_cap_pf)
                   && finite_non_negative(profile.total_cap_pf) && finite_non_negative(profile.timing_effective_cap_pf)
               ? std::optional<::icts::Wrapper::WireCapacitanceProfile>{profile}
               : std::nullopt;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

::icts::WrapperTimingGraph CtsEccdbAdapter::collectTimingGraph(std::string_view routing_anchor_name,
                                                               std::optional<unsigned> routing_anchor_level) const
{
  const auto graph_start = std::chrono::steady_clock::now();
  ::icts::WrapperTimingGraph result;
  if (!isDesignReady()) {
    result.status = ::icts::WrapperTimingGraphStatus::kDesignUnavailable;
    result.diagnostic = "native EccDB context, technology, or library is unavailable";
    return result;
  }
  if (!_liberty_cell_lookup) {
    result.status = ::icts::WrapperTimingGraphStatus::kLibraryUnavailable;
    result.diagnostic = "native Liberty cell lookup is unavailable";
    return result;
  }

  const auto dbu = queryDbUnit();
  if (!dbu.has_value() || *dbu <= 0) {
    result.status = ::icts::WrapperTimingGraphStatus::kUnsupported;
    result.diagnostic = "native EccDB DBU-per-micron is unavailable";
    return result;
  }
  const PhysicalDesignService physical{*_context};
  const auto native_routing_anchor_level = routing_anchor_level.has_value()
                                               ? std::optional<uint32_t>{static_cast<uint32_t>(*routing_anchor_level)}
                                               : std::nullopt;
  result.signal_routing_authority = makeNativeRoutingAuthority(physical, *dbu, routing_anchor_name, native_routing_anchor_level);
  if (!result.signal_routing_authority.complete()) {
    result.status = ::icts::WrapperTimingGraphStatus::kUnsupported;
    result.diagnostic = "native signal routing authority unavailable:" + result.signal_routing_authority.diagnostic;
    return result;
  }

  const auto& netlist = _context->design().netlistStorage();
  const auto& library = *_context->library();
  std::unordered_map<std::string, std::size_t> node_index;
  double logic_rc_runtime_s = 0.0;

  auto collect_timing_endpoints = [&](DesignNetId net_id) {
    std::vector<NativeEndpoint> endpoints;
    physical.forEachNetPin(net_id, PhysicalNetPinVisitor{
                                             .instance_pin = [&](DesignInstancePinId pin, const DesignInstanceTerm&, const DesignInstance& instance,
                                                                 const LibraryMasterTerm& term) {
                                               NativeEndpoint endpoint;
                                               endpoint.is_instance = true;
                                               endpoint.instance_term = pin;
                                               endpoint.instance_direction = term.direction;
                                               endpoint.debug_name = instance.name + "/" + term.name;
                                               endpoint.cell_master = library.cellMasterStorage().cellMaster(instance.master).name;
                                               endpoint.port_name = term.name;
                                               endpoint.location = averageInstancePinLocationLikeIdb(physical, pin, instance.origin);
                                               endpoints.push_back(std::move(endpoint));
                                             },
                                             .io_pin = [&](DesignIoPinId pin, const DesignIoPin& term) {
                                               NativeEndpoint endpoint;
                                               endpoint.is_instance = false;
                                               endpoint.block_term = pin;
                                               endpoint.block_direction = term.direction;
                                               endpoint.debug_name = term.name;
                                               endpoint.port_name = term.name;
                                               endpoint.location = averageBlockPinLocation(physical, pin, Point{});
                                               endpoints.push_back(std::move(endpoint));
                                             }});
    std::ranges::sort(endpoints, [](const NativeEndpoint& lhs, const NativeEndpoint& rhs) { return lhs.debug_name < rhs.debug_name; });
    return endpoints;
  };

  physical.forEachRegularNet([&](DesignNetId net_id, const DesignNet& net) {
    if (result.status != ::icts::WrapperTimingGraphStatus::kDesignUnavailable && result.status != ::icts::WrapperTimingGraphStatus::kComplete
        && !result.diagnostic.empty()) {
      return;
    }
    if (net.use == DesignSignalUse::kPower || net.use == DesignSignalUse::kGround) {
      return;
    }
    auto endpoints = collect_timing_endpoints(net_id);
    if (endpoints.size() < 2U) {
      return;
    }
    const auto driver_index = resolveDriverIndex(endpoints);
    if (!driver_index.has_value()) {
      result.status = ::icts::WrapperTimingGraphStatus::kConnectivityInvalid;
      result.diagnostic = "native signal net has no unique driver pin:" + net.name;
      return;
    }
    const auto driver_name = endpoints[*driver_index].debug_name;
    auto* driver_cell = endpoints[*driver_index].is_instance ? _liberty_cell_lookup(endpoints[*driver_index].cell_master) : nullptr;
    if (!appendNativeTimingNode(endpoints[*driver_index], driver_cell, result, node_index)) {
      if (result.diagnostic.empty()) {
        result.status = ::icts::WrapperTimingGraphStatus::kConnectivityInvalid;
        result.diagnostic = "native driver pin cannot be represented:" + driver_name;
      }
      return;
    }
    std::vector<std::size_t> load_indices;
    load_indices.reserve(endpoints.size() - 1U);
    ::icts::WrapperTimingNet timing_net;
    timing_net.net_name = net.name;
    timing_net.driver_pin = driver_name;
    for (std::size_t index = 0U; index < endpoints.size(); ++index) {
      if (index == *driver_index) {
        continue;
      }
      auto* load_cell = endpoints[index].is_instance ? _liberty_cell_lookup(endpoints[index].cell_master) : nullptr;
      if (!appendNativeTimingNode(endpoints[index], load_cell, result, node_index)) {
        if (result.diagnostic.empty()) {
          result.status = ::icts::WrapperTimingGraphStatus::kConnectivityInvalid;
          result.diagnostic = "native load pin cannot be represented:" + endpoints[index].debug_name;
        }
        return;
      }
      timing_net.load_pins.push_back(endpoints[index].debug_name);
      load_indices.push_back(index);
    }
    if (load_indices.empty()) {
      return;
    }
    std::ranges::sort(timing_net.load_pins);
    timing_net.load_pins.erase(std::ranges::unique(timing_net.load_pins).begin(), timing_net.load_pins.end());
    const auto logic_rc_start = std::chrono::steady_clock::now();
    if (!buildNativeNetWireModel(endpoints, *driver_index, load_indices, result.signal_routing_authority, *dbu, timing_net)) {
      result.status = ::icts::WrapperTimingGraphStatus::kUnsupported;
      result.diagnostic = "native signal net RC construction failed:" + net.name;
      return;
    }
    logic_rc_runtime_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - logic_rc_start).count();
    result.nets.push_back(std::move(timing_net));
  });

  if (!result.diagnostic.empty() && result.status != ::icts::WrapperTimingGraphStatus::kComplete
      && result.status != ::icts::WrapperTimingGraphStatus::kDesignUnavailable) {
    return result;
  }

  physical.forEachInstance([&](DesignInstanceId instance_id, const DesignInstance& instance, std::span<const DesignInstanceTermId>) {
    const auto& master = library.cellMasterStorage().cellMaster(instance.master);
    auto* lib_cell = _liberty_cell_lookup(master.name);
    if (lib_cell == nullptr) {
      return;
    }
    for (const auto& arc_set_holder : lib_cell->get_cell_arcs()) {
      auto* arc_set = arc_set_holder.get();
      auto* arc = nativeFirstEnabledArc(arc_set);
      if (arc == nullptr) {
        continue;
      }
      const auto input_port = std::string(arc->get_src_port());
      const auto output_port = std::string(arc->get_snk_port());
      const auto input_term = netlist.findInstanceTerm(instance_id, input_port);
      const auto output_term = netlist.findInstanceTerm(instance_id, output_port);
      const auto input_name = instance.name + "/" + input_port;
      const auto output_name = instance.name + "/" + output_port;
      bool positive = false;
      bool negative = false;
      for (const auto& candidate_holder : arc_set->get_arcs()) {
        auto* candidate = candidate_holder.get();
        if (candidate == nullptr || candidate->isDisableArc() != 0U) {
          continue;
        }
        positive = positive || candidate->isPositiveArc() != 0U;
        negative = negative || candidate->isNegativeArc() != 0U;
      }
      const auto timing_type = arc->get_timing_type();
      if (nativeIsCombinationalArc(arc)) {
        if (!node_index.contains(input_name) && !node_index.contains(output_name)) {
          continue;
        }
        if (!input_term || !output_term || !netlist.instanceTerm(input_term).net || !netlist.instanceTerm(output_term).net
            || !node_index.contains(input_name) || !node_index.contains(output_name)) {
          ++result.unsupported_arc_count;
          continue;
        }
        result.arcs.push_back(::icts::WrapperTimingArc{.inst_name = instance.name,
                                                       .cell_master = master.name,
                                                       .input_port = input_port,
                                                       .output_port = output_port,
                                                       .input_pin = input_name,
                                                       .output_pin = output_name,
                                                       .positive_unate = positive,
                                                       .negative_unate = negative,
                                                       .clock_gate_boundary = lib_cell->isICG()});
        continue;
      }
      if ((timing_type == idb::LibArc::TimingType::kRisingEdge || timing_type == idb::LibArc::TimingType::kFallingEdge) && node_index.contains(output_name)) {
        if (!input_term || !output_term || input_name.empty() || output_name.empty()) {
          ++result.unsupported_arc_count;
          continue;
        }
        result.launches.push_back(::icts::WrapperTimingLaunch{.inst_name = instance.name,
                                                              .cell_master = master.name,
                                                              .clock_port = input_port,
                                                              .output_port = output_port,
                                                              .clock_pin = input_name,
                                                              .output_pin = output_name,
                                                              .clock_transition = nativeTransition(arc)});
        continue;
      }
      if ((arc->isSetupArc() != 0U || arc->isHoldArc() != 0U) && node_index.contains(output_name)) {
        if (!input_term || !output_term || input_name.empty() || output_name.empty()) {
          ++result.unsupported_arc_count;
          continue;
        }
        result.checks.push_back(::icts::WrapperTimingCheck{.inst_name = instance.name,
                                                            .cell_master = master.name,
                                                            .clock_port = input_port,
                                                            .data_port = output_port,
                                                            .clock_pin = input_name,
                                                            .data_pin = output_name,
                                                            .kind = arc->isSetupArc() != 0U ? ::icts::WrapperTimingCheckKind::kSetup
                                                                                            : ::icts::WrapperTimingCheckKind::kHold,
                                                            .clock_transition = nativeTransition(arc),
                                                            .clock_gating = lib_cell->isICG()});
      }
    }
  });

  std::ranges::sort(result.nodes, {}, &::icts::WrapperTimingNode::pin_name);
  std::ranges::sort(result.nets, {}, &::icts::WrapperTimingNet::net_name);
  std::ranges::sort(result.arcs, [](const auto& lhs, const auto& rhs) {
    return std::tie(lhs.inst_name, lhs.input_port, lhs.output_port, lhs.input_pin, lhs.output_pin, lhs.cell_master, lhs.positive_unate,
                    lhs.negative_unate, lhs.clock_gate_boundary)
           < std::tie(rhs.inst_name, rhs.input_port, rhs.output_port, rhs.input_pin, rhs.output_pin, rhs.cell_master, rhs.positive_unate,
                      rhs.negative_unate, rhs.clock_gate_boundary);
  });
  std::vector<::icts::WrapperTimingArc> merged_arcs;
  merged_arcs.reserve(result.arcs.size());
  for (auto& arc : result.arcs) {
    const bool same_edge = !merged_arcs.empty()
                           && std::tie(merged_arcs.back().inst_name, merged_arcs.back().input_port, merged_arcs.back().output_port,
                                       merged_arcs.back().input_pin, merged_arcs.back().output_pin, merged_arcs.back().cell_master)
                                  == std::tie(arc.inst_name, arc.input_port, arc.output_port, arc.input_pin, arc.output_pin, arc.cell_master);
    if (!same_edge) {
      merged_arcs.push_back(std::move(arc));
      continue;
    }
    merged_arcs.back().positive_unate = merged_arcs.back().positive_unate || arc.positive_unate;
    merged_arcs.back().negative_unate = merged_arcs.back().negative_unate || arc.negative_unate;
    merged_arcs.back().clock_gate_boundary = merged_arcs.back().clock_gate_boundary || arc.clock_gate_boundary;
  }
  result.arcs = std::move(merged_arcs);
  std::ranges::sort(result.launches, [](const auto& lhs, const auto& rhs) {
    return std::tie(lhs.clock_pin, lhs.output_pin, lhs.cell_master, lhs.clock_port, lhs.output_port, lhs.clock_transition)
           < std::tie(rhs.clock_pin, rhs.output_pin, rhs.cell_master, rhs.clock_port, rhs.output_port, rhs.clock_transition);
  });
  result.launches.erase(std::ranges::unique(result.launches, {}, [](const auto& launch) {
                             return std::tie(launch.clock_pin, launch.output_pin, launch.clock_transition);
                           }).begin(),
                        result.launches.end());
  std::ranges::sort(result.checks, [](const auto& lhs, const auto& rhs) {
    return std::tie(lhs.clock_pin, lhs.data_pin, lhs.kind, lhs.clock_transition, lhs.cell_master, lhs.clock_port, lhs.data_port)
           < std::tie(rhs.clock_pin, rhs.data_pin, rhs.kind, rhs.clock_transition, rhs.cell_master, rhs.clock_port, rhs.data_port);
  });
  result.checks.erase(std::ranges::unique(result.checks, {}, [](const auto& check) {
                          return std::tie(check.clock_pin, check.data_pin, check.kind, check.clock_transition);
                        }).begin(),
                     result.checks.end());
  if (result.unsupported_arc_count > 0U) {
    result.status = ::icts::WrapperTimingGraphStatus::kUnsupported;
    result.diagnostic = "native Liberty arcs cannot be joined to live EccDB instance terms";
    return result;
  }
  result.status = ::icts::WrapperTimingGraphStatus::kComplete;
  const auto total_runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - graph_start).count();
  result.logic_rc_construction_runtime_s = logic_rc_runtime_s;
  result.graph_construction_runtime_s = std::max(0.0, total_runtime_s - logic_rc_runtime_s);
  return result;
}

::icts::ClockTraceBuild CtsEccdbAdapter::traceSdcClocks(const ::icts::SdcClockTraceInput& input) const
{
  ::icts::ClockTraceBuild build;
  if (!isDesignReady() || input.clock_data == nullptr) {
    build.status = ::icts::ClockTraceBuildStatusCode::kAmbiguousOwnership;
    build.message = "native_clock_trace_requires_eccdb_context_and_sdc_data";
    return build;
  }

  const auto& netlist = _context->design().netlistStorage();
  const PhysicalDesignService physical{*_context};
  std::map<std::string, std::vector<DesignNetId>> nets_by_clock;
  std::map<std::string, std::string> owner_by_net_name;

  const auto resolve_ref = [&](const ::icts::SdcObjectRef& ref) {
    std::vector<DesignNetId> result;
    std::unordered_set<uint64_t> resolved_net_ids;
    const auto pattern = trimmedName(ref.pattern);
    if (pattern.empty()) {
      return result;
    }
    const auto append = [&result, &resolved_net_ids](DesignNetId net) {
      if (net && resolved_net_ids.insert(net.packed()).second) {
        result.push_back(net);
      }
    };
    switch (ref.kind) {
      case ::icts::SdcObjectKind::kNet:
        if (const auto net = physical.findRegularNet(pattern); net) {
          append(net);
        } else {
          physical.forEachRegularNet([&](DesignNetId id, const DesignNet& net) {
            if (globMatches(pattern, net.name)) {
              append(id);
            }
          });
        }
        break;
      case ::icts::SdcObjectKind::kPort:
        netlist.forEachBlockTerm([&](DesignBlockTermId, const DesignBlockTerm& port) {
          if (globMatches(pattern, port.name)) {
            append(port.net);
          }
        });
        break;
      case ::icts::SdcObjectKind::kPin:
        netlist.forEachInstance([&](DesignInstanceId instance_id, const DesignInstance& instance, std::span<const DesignInstancePinId>) {
          for (const auto term_id : netlist.instanceTerms(instance_id)) {
            const auto& term = netlist.instanceTerm(term_id);
            const auto& master_term = _context->library()->masterTermStorage().masterTerm(term.master_term);
            const auto slash_name = instance.name + "/" + master_term.name;
            const auto colon_name = instance.name + ":" + master_term.name;
            if (globMatches(pattern, slash_name) || globMatches(pattern, colon_name)) {
              append(term.net);
            }
          }
        });
        break;
      case ::icts::SdcObjectKind::kClock:
        if (const auto iter = nets_by_clock.find(pattern); iter != nets_by_clock.end()) {
          for (const auto net : iter->second) {
            append(net);
          }
        }
        break;
      case ::icts::SdcObjectKind::kUnknown:
        if (const auto net = physical.findRegularNet(pattern); net) {
          append(net);
        }
        break;
    }
    return result;
  };

  for (const auto& declaration : input.clock_data->clocks) {
    if (declaration.is_virtual || declaration.clock_name.empty()) {
      continue;
    }
    std::vector<DesignNetId> resolved_nets;
    std::unordered_set<uint64_t> resolved_net_ids;
    const auto& refs = declaration.targets.empty() ? declaration.generated_sources : declaration.targets;
    for (const auto& ref : refs) {
      for (const auto net : resolve_ref(ref)) {
        if (resolved_net_ids.insert(net.packed()).second) {
          resolved_nets.push_back(net);
        }
      }
    }

    if (resolved_nets.empty()) {
      build.summary.records.push_back(::icts::ClockTraceRecord{.clock_name = declaration.clock_name,
                                                                 .status = "rejected",
                                                                 .target_kind = "native",
                                                                 .reason = "sdc_target_not_found_in_eccdb",
                                                                 .clock_kind = declaration.kind == ::icts::SdcClockDecl::Kind::kGenerated ? "generated" : "primary",
                                                                 .master_clock_name = declaration.master_clock_name.empty() ? "n/a" : declaration.master_clock_name});
      continue;
    }

    nets_by_clock[declaration.clock_name] = resolved_nets;
    for (const auto net : resolved_nets) {
      const auto& net_data = netlist.net(net);
      const auto owner = owner_by_net_name.find(net_data.name);
      if (owner != owner_by_net_name.end() && owner->second != declaration.clock_name) {
        build.status = ::icts::ClockTraceBuildStatusCode::kAmbiguousOwnership;
        build.message = "native_clock_trace_ambiguous_net:" + net_data.name;
        build.summary.records.push_back(::icts::ClockTraceRecord{.clock_name = declaration.clock_name,
                                                                   .net_name = net_data.name,
                                                                   .status = "ambiguous",
                                                                   .target_kind = "native",
                                                                   .reason = build.message,
                                                                   .clock_kind = declaration.kind == ::icts::SdcClockDecl::Kind::kGenerated ? "generated" : "primary",
                                                                   .master_clock_name = declaration.master_clock_name.empty() ? "n/a" : declaration.master_clock_name});
        continue;
      }
      owner_by_net_name.emplace(net_data.name, declaration.clock_name);
      build.output.clock_targets.push_back(::icts::ClockTraceClockTarget{.clock_name = declaration.clock_name,
                                                                            .clock_net_name = net_data.name});
      build.summary.records.push_back(::icts::ClockTraceRecord{.clock_name = declaration.clock_name,
                                                                 .net_name = net_data.name,
                                                                 .status = "accepted",
                                                                 .target_kind = "native",
                                                                 .reason = "sdc_target_resolved_in_eccdb",
                                                                 .clock_kind = declaration.kind == ::icts::SdcClockDecl::Kind::kGenerated ? "generated" : "primary",
                                                                 .master_clock_name = declaration.master_clock_name.empty() ? "n/a" : declaration.master_clock_name,
                                                                 .dominance = "native_direct"});
    }
  }
  return build;
}

bool CtsEccdbAdapter::readTraceClockTargets(::icts::Design& design, const std::vector<::icts::ClockTraceClockTarget>& clock_targets)
{
  if (!isDesignReady()) {
    return false;
  }
  try {
    NativeDesignMaterializer materializer{*_context, _liberty_cell_lookup, design};
    return materializer.materialize(clock_targets);
  } catch (const std::exception& error) {
    ::icts::Logger::getInst().warn(::icts::Loc::current(), "CTS native input failed while materializing EccDB clock objects: ", error.what());
    return false;
  }
}

::icts::WrapperWriteSummary CtsEccdbAdapter::writeClocks(::icts::Design& design, const std::vector<::icts::Clock*>& clocks)
{
  ::icts::WrapperWriteSummary summary;
  if (!isDesignReady()) {
    summary.reason = "eccdb_context_not_ready";
    return summary;
  }

  const auto nets = clockNets(design, clocks);
  if (nets.empty()) {
    summary.success = true;
    return summary;
  }

  auto& netlist = _context->design().netlistStorage();
  auto& library = *_context->library();
  const auto& masters = library.cellMasterStorage();
  const auto& master_terms = library.masterTermStorage();
  const auto pg_binding_index = buildCtsPgBindingIndex(*_context);

  struct InstancePlan
  {
    ::icts::Inst* cts = nullptr;
    LibraryCellMasterId master;
    std::vector<CtsPgTermBinding> pg_bindings;
  };
  std::vector<InstancePlan> instances;
  instances.reserve(design.get_insts().size());
  for (auto* cts_instance : design.get_insts()) {
    if (cts_instance == nullptr || cts_instance->get_name().empty() || cts_instance->get_cell_master().empty()) {
      summary.reason = "invalid_cts_instance";
      return summary;
    }
    const auto master = masters.findCellMaster(cts_instance->get_cell_master());
    if (!master) {
      summary.reason = "eccdb_master_not_found:" + cts_instance->get_cell_master();
      return summary;
    }
    const auto existing = netlist.findInstance(cts_instance->get_name());
    bool writes_instance = true;
    if (existing) {
      const auto& current = netlist.instance(existing);
      if (current.source != DesignInstanceSource::kTiming && current.master != master) {
        summary.reason = "non_timing_instance_master_conflict:" + cts_instance->get_name();
        return summary;
      }
      writes_instance = current.source == DesignInstanceSource::kTiming;
    }

    std::vector<CtsPgTermBinding> pg_bindings;
    if (writes_instance) {
      for (const auto master_term_id : master_terms.masterTerms(master)) {
        const auto& master_term = master_terms.masterTerm(master_term_id);
        const auto resolved = resolveCtsPgBinding(pg_binding_index, master_term);
        if (resolved.ambiguous) {
          summary.reason = "ambiguous_cts_pg_binding:" + cts_instance->get_name() + "/" + master_term.name;
          return summary;
        }
        if (resolved.net) {
          pg_bindings.push_back(CtsPgTermBinding{.master_term_name = master_term.name, .net = resolved.net});
        }
      }
      if (existing) {
        for (const auto& binding : pg_bindings) {
          const auto term = netlist.findInstanceTerm(existing, binding.master_term_name);
          if (!term) {
            summary.reason = "cts_pg_term_missing:" + cts_instance->get_name() + "/" + binding.master_term_name;
            return summary;
          }
          const auto source = netlist.instanceTerm(term).special_net;
          if (source && source != binding.net) {
            summary.reason = "cts_pg_existing_binding_conflict:" + cts_instance->get_name() + "/" + binding.master_term_name;
            return summary;
          }
        }
      }
    }
    instances.push_back(InstancePlan{.cts = cts_instance, .master = master, .pg_bindings = std::move(pg_bindings)});
  }

  std::unordered_map<const ::icts::Pin*, const ::icts::Net*> owner_by_pin;
  for (auto* cts_net : nets) {
    if (cts_net == nullptr || cts_net->get_name().empty() || cts_net->get_driver() == nullptr) {
      summary.reason = "invalid_cts_net";
      summary.failed_net = cts_net == nullptr ? std::string{} : cts_net->get_name();
      return summary;
    }
    const auto claim = [&](const ::icts::Pin* pin) {
      return pin != nullptr && owner_by_pin.emplace(pin, cts_net).second;
    };
    if (!claim(cts_net->get_driver())) {
      summary.reason = "cts_pin_drives_multiple_clock_nets";
      summary.failed_net = cts_net->get_name();
      return summary;
    }
    for (const auto* pin : cts_net->get_loads()) {
      if (!claim(pin)) {
        summary.reason = "cts_pin_belongs_to_multiple_clock_nets";
        summary.failed_net = cts_net->get_name();
        return summary;
      }
    }
  }

  try {
    InstanceService instance_service{*_context};
    ConnectivityService connectivity_service{*_context};

    for (const auto& plan : instances) {
      const auto existing = netlist.findInstance(plan.cts->get_name());
      if (existing) {
        const auto& current = netlist.instance(existing);
        const auto is_timing_instance = current.source == DesignInstanceSource::kTiming;
        if (!is_timing_instance) {
          continue;
        }
        if (current.master != plan.master) {
          instance_service.swapMasterCompatible(existing, plan.master);
        }
        if (is_timing_instance) {
          instance_service.place(existing, Point{plan.cts->get_location().get_x(), plan.cts->get_location().get_y()}, DesignOrientation::kN,
                                 DesignPlacementStatus::kPlaced);
        }
      } else {
        static_cast<void>(instance_service.create(DesignInstance{.name = plan.cts->get_name(),
                                                                   .master = plan.master,
                                                                   .origin = Point{plan.cts->get_location().get_x(), plan.cts->get_location().get_y()},
                                                                   .orientation = DesignOrientation::kN,
                                                                   .placement_status = DesignPlacementStatus::kPlaced,
                                                                   .source = DesignInstanceSource::kTiming}));
        ++summary.inserted_inst_count;
      }
    }

    // Clock-tree construction only describes signal pins.  Reattach the
    // physical power/ground terms explicitly from the existing EccDB PG
    // bindings, rather than assuming fixed rail or term names.
    for (const auto& plan : instances) {
      if (plan.pg_bindings.empty()) {
        continue;
      }
      const auto instance = netlist.findInstance(plan.cts->get_name());
      if (!instance || netlist.instance(instance).source != DesignInstanceSource::kTiming) {
        continue;
      }
      for (const auto& binding : plan.pg_bindings) {
        const auto term = netlist.findInstanceTerm(instance, binding.master_term_name);
        if (!term) {
          throw std::logic_error("CTS PG term disappeared during instance writeback");
        }
        const auto source = netlist.instanceTerm(term).special_net;
        if (source && source != binding.net) {
          throw std::logic_error("CTS PG binding changed during instance writeback");
        }
        connectivity_service.connect(term, binding.net);
      }
    }

    for (auto* cts_net : nets) {
      auto net = netlist.findRegularNet(cts_net->get_name());
      if (!net) {
        net = connectivity_service.createNet(DesignNet{.name = cts_net->get_name(), .use = DesignSignalUse::kClock, .source = DesignNetSource::kTiming});
        ++summary.inserted_net_count;
      } else {
        auto properties = netlist.net(net);
        properties.use = DesignSignalUse::kClock;
        connectivity_service.setNetProperties(net, std::move(properties));
      }

      std::vector<DesignInstanceTermId> instance_terms;
      std::vector<DesignBlockTermId> block_terms;
      std::vector<DesignNetId> allowed_source_nets;
      const auto append_pin = [&](const ::icts::Pin* pin) {
        if (pin == nullptr) {
          throw std::invalid_argument("clock net contains null CTS pin");
        }
        if (const auto* cts_instance = pin->get_inst(); cts_instance != nullptr) {
          const auto instance = netlist.findInstance(cts_instance->get_name());
          const auto term = netlist.findInstanceTerm(instance, pin->get_name());
          if (!instance || !term) {
            throw std::invalid_argument("CTS instance pin is not present in EccDB");
          }
          instance_terms.push_back(term);
          if (const auto source = netlist.instanceTerm(term).net; source && source != net) {
            allowed_source_nets.push_back(source);
          }
        } else {
          const auto term = netlist.findBlockTerm(pin->get_name());
          if (!term) {
            throw std::invalid_argument("CTS top-level pin is not present in EccDB");
          }
          block_terms.push_back(term);
          if (const auto source = netlist.blockTerm(term).net; source && source != net) {
            allowed_source_nets.push_back(source);
          }
        }
      };
      append_pin(cts_net->get_driver());
      for (const auto* load : cts_net->get_loads()) {
        append_pin(load);
      }
      std::sort(instance_terms.begin(), instance_terms.end(), [](const auto lhs, const auto rhs) { return lhs.packed() < rhs.packed(); });
      instance_terms.erase(std::unique(instance_terms.begin(), instance_terms.end()), instance_terms.end());
      std::sort(block_terms.begin(), block_terms.end(), [](const auto lhs, const auto rhs) { return lhs.packed() < rhs.packed(); });
      block_terms.erase(std::unique(block_terms.begin(), block_terms.end()), block_terms.end());
      std::sort(allowed_source_nets.begin(), allowed_source_nets.end(), [](const auto lhs, const auto rhs) { return lhs.packed() < rhs.packed(); });
      allowed_source_nets.erase(std::unique(allowed_source_nets.begin(), allowed_source_nets.end()), allowed_source_nets.end());
      connectivity_service.replaceNetConnections(net, instance_terms, block_terms, allowed_source_nets);
    }
  } catch (const std::exception& error) {
    summary.reason = error.what();
    return summary;
  }

  summary.success = true;
  return summary;
}

}  // namespace eccdb::adapter::icts
