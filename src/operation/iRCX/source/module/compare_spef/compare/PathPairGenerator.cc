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
#include "compare/PathPairGenerator.hh"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "StringUtils.hh"

namespace ircx {
namespace compare_spef {
namespace {

constexpr std::size_t kMaxPathPairs = 250;
constexpr std::size_t kMaxDefaultSinksPerSource = 5;

struct PinCounts
{
  std::size_t sources = 0;
  std::size_t sinks = 0;
};

using PinList = std::vector<const Pin*>;

class PathPairCollector
{
 public:
  explicit PathPairCollector(std::size_t reserve_count)
  {
    _seen.reserve(reserve_count);
    _pairs.reserve(reserve_count);
  }

  auto add(NodePair pair) -> bool
  {
    if (pair.first.empty() || pair.second.empty() || pair.first == pair.second || !_seen.insert(pair).second) {
      return false;
    }
    _pairs.push_back(std::move(pair));
    return true;
  }

  auto full() const -> bool { return _pairs.size() >= kMaxPathPairs; }
  auto size() const -> std::size_t { return _pairs.size(); }

  auto sortedPairs() && -> std::vector<NodePair>
  {
    std::sort(_pairs.begin(), _pairs.end());
    return std::move(_pairs);
  }

  auto pairs() && -> std::vector<NodePair> { return std::move(_pairs); }

 private:
  std::unordered_set<NodePair, NodePairHash> _seen;
  std::vector<NodePair> _pairs;
};

auto isSourcePin(const Pin& pin) -> bool
{
  if (pin.direction == "B") {
    return true;
  }
  if (pin.is_external) {
    return pin.direction == "I";
  }
  return pin.direction == "O";
}

auto isSinkPin(const Pin& pin) -> bool
{
  if (pin.direction == "B") {
    return true;
  }
  if (pin.is_external) {
    return pin.direction == "O";
  }
  return pin.direction == "I";
}

auto isRegisterDataSinkName(std::string_view pin_name) -> bool
{
  return pin_name != "cs_reg_p:D" && string::contains(pin_name, "_reg_p:D");
}

auto isNamedRegisterSource(std::string_view pin_name) -> bool
{
  return string::starts_with(pin_name, "cs_reg_p:") || string::starts_with(pin_name, "cvtA_reg_p:");
}

auto isTopOutputBufferNet(const Net& net, std::string_view sink_name) -> bool
{
  return string::contains(sink_name, "fixio_buf_")
         && std::any_of(net.pins.begin(), net.pins.end(), [](const Pin& pin) { return pin.is_external && pin.direction == "O"; });
}

auto sourceShouldPrecedeNamedLoad(std::string_view source_name, std::string_view sink_name) -> bool
{
  return string::starts_with(source_name, "fixio_buf_") && string::starts_with(sink_name, "max_cap");
}

auto isTieCell(const Pin& pin) -> bool
{
  return string::starts_with(pin.driving_cell, "TIE");
}

auto p2pReportPair(const Net& net, const std::string& source_name, const Pin& source_pin, const std::string& sink_name) -> NodePair
{
  const bool source_first = source_pin.is_external || isRegisterDataSinkName(sink_name) || isTieCell(source_pin)
                            || isNamedRegisterSource(source_name) || isTopOutputBufferNet(net, sink_name)
                            || sourceShouldPrecedeNamedLoad(source_name, sink_name);
  if (source_first) {
    return NodePair{source_name, sink_name};
  }
  return NodePair{sink_name, source_name};
}

auto pinNames(const Net& net) -> std::vector<std::string>
{
  std::vector<std::string> names;
  names.reserve(net.pins.size());
  for (const auto& pin : net.pins) {
    if (pin.direction == "N") {
      continue;
    }
    names.push_back(pin.name);
  }
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  return names;
}

auto pinList(const Net& net) -> PinList
{
  PinList pins;
  pins.reserve(net.pins.size());
  for (const auto& pin : net.pins) {
    if (pin.direction == "N" || pin.name.empty()) {
      continue;
    }
    pins.push_back(&pin);
  }
  std::stable_sort(pins.begin(), pins.end(), [](const Pin* lhs, const Pin* rhs) { return lhs->name < rhs->name; });
  pins.erase(std::unique(pins.begin(), pins.end(), [](const Pin* lhs, const Pin* rhs) { return lhs->name == rhs->name; }), pins.end());
  return pins;
}

auto countPins(const PinList& pins) -> PinCounts
{
  PinCounts counts;
  for (const Pin* pin : pins) {
    if (isSourcePin(*pin)) {
      counts.sources++;
    }
    if (isSinkPin(*pin)) {
      counts.sinks++;
    }
  }
  return counts;
}

auto shouldStopAddingSinks(std::string_view source_name, const PinCounts& counts, std::size_t sink_count) -> bool
{
  const bool is_small_register_output_net = string::contains(source_name, "_reg_p:Q") && counts.sinks <= kMaxDefaultSinksPerSource + 3;
  const bool should_limit = counts.sinks > kMaxDefaultSinksPerSource && (counts.sources > 1 || is_small_register_output_net);
  return should_limit && sink_count >= kMaxDefaultSinksPerSource;
}

auto appendSourceSinkPairs(const Net& net, const PinList& pins, const PinCounts& counts, PathPairCollector& collector) -> bool
{
  for (const Pin* source_pin : pins) {
    if (!isSourcePin(*source_pin)) {
      continue;
    }
    std::size_t sink_count = 0;
    for (const Pin* sink_pin : pins) {
      if (source_pin->name == sink_pin->name || !isSinkPin(*sink_pin)) {
        continue;
      }

      if (collector.add(p2pReportPair(net, source_pin->name, *source_pin, sink_pin->name))) {
        sink_count++;
        if (collector.full()) {
          return true;
        }
        if (shouldStopAddingSinks(source_pin->name, counts, sink_count)) {
          break;
        }
      }
    }
  }
  return false;
}

auto appendExternalInternalPairs(const PinList& pins, PathPairCollector& collector) -> bool
{
  for (const Pin* external_pin : pins) {
    if (!external_pin->is_external || external_pin->direction == "N") {
      continue;
    }
    for (const Pin* internal_pin : pins) {
      if (internal_pin->is_external || external_pin->name == internal_pin->name || internal_pin->direction == "N"
          || external_pin->direction == internal_pin->direction) {
        continue;
      }
      if (collector.add(NodePair{external_pin->name, internal_pin->name}) && collector.full()) {
        return true;
      }
    }
  }
  return false;
}

auto configuredPathPairs(const Net& net, const Config& config) -> std::vector<NodePair>
{
  const auto pins = pinNames(net);
  const std::unordered_set<std::string> pin_set(pins.begin(), pins.end());
  PathPairCollector collector(kMaxPathPairs);

  auto add_from_to = [&](const std::string& from_pin, const std::string& to_pin) {
    if (pin_set.contains(from_pin) && pin_set.contains(to_pin)) {
      collector.add(NodePair{from_pin, to_pin});
    }
  };

  if (!config.from_pin.empty() && !config.to_pin.empty()) {
    add_from_to(config.from_pin, config.to_pin);
  }
  for (const auto& [from_pin, to_pin] : config.from_to_pins) {
    add_from_to(from_pin, to_pin);
  }

  std::vector<std::string> from_pins = config.from_pins;
  std::vector<std::string> to_pins = config.to_pins;
  if (!config.from_pin.empty()) {
    from_pins.push_back(config.from_pin);
  }
  if (!config.to_pin.empty()) {
    to_pins.push_back(config.to_pin);
  }

  if (!from_pins.empty() && !to_pins.empty()) {
    for (const auto& from_pin : from_pins) {
      for (const auto& to_pin : to_pins) {
        add_from_to(from_pin, to_pin);
      }
    }
  } else if (!from_pins.empty()) {
    for (const auto& from_pin : from_pins) {
      if (!pin_set.contains(from_pin)) {
        continue;
      }
      for (const auto& to_pin : pins) {
        collector.add(NodePair{from_pin, to_pin});
      }
    }
  } else if (!to_pins.empty()) {
    for (const auto& to_pin : to_pins) {
      if (!pin_set.contains(to_pin)) {
        continue;
      }
      for (const auto& from_pin : pins) {
        collector.add(NodePair{from_pin, to_pin});
      }
    }
  }

  return std::move(collector).sortedPairs();
}

auto defaultPathPairs(const Net& net) -> std::vector<NodePair>
{
  const auto pins = pinList(net);
  const PinCounts counts = countPins(pins);

  PathPairCollector collector(std::min(kMaxPathPairs, counts.sources * std::max<std::size_t>(counts.sinks, 1)));
  if (appendSourceSinkPairs(net, pins, counts, collector)) {
    return std::move(collector).pairs();
  }
  if (appendExternalInternalPairs(pins, collector)) {
    return std::move(collector).pairs();
  }
  return std::move(collector).pairs();
}

}  // namespace

PathPairGenerator::PathPairGenerator(const Config& config) : _config(config), _net_selector(config)
{
}

auto PathPairGenerator::generate(const Net& net) const -> std::vector<NodePair>
{
  if (_net_selector.hasPathFilter()) {
    return configuredPathPairs(net, _config);
  }
  return defaultPathPairs(net);
}

}  // namespace compare_spef
}  // namespace ircx
