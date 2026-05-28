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
#include "CompareParasitics.hh"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace ircx {
namespace {

constexpr double kEpsilon = 1e-12;
constexpr std::size_t kMaxPathPairs = 250;
constexpr std::size_t kMaxDefaultSinksPerSource = 5;

auto absoluteRelativeDelta(double test, double reference) -> std::optional<double>
{
  if (std::abs(reference) <= kEpsilon) {
    return std::nullopt;
  }
  return (test - reference) / reference;
}

auto roundToSignificantDigits(double value, int digits = 6) -> double
{
  if (std::abs(value) <= kEpsilon || !std::isfinite(value)) {
    return value;
  }

  const double scale = std::pow(10.0, static_cast<double>(digits - 1) - std::floor(std::log10(std::abs(value))));
  return std::round(value * scale) / scale;
}

auto roundToSignificantDigitsHalfEven(double value, int digits = 6) -> double
{
  if (std::abs(value) <= kEpsilon || !std::isfinite(value)) {
    return value;
  }

  const double scale = std::pow(10.0, static_cast<double>(digits - 1) - std::floor(std::log10(std::abs(value))));
  const double scaled_value = value * scale;
  const double lower = std::floor(scaled_value);
  const double fraction = scaled_value - lower;
  if (std::abs(fraction - 0.5) <= 1e-9) {
    return (std::fmod(lower, 2.0) == 0.0 ? lower : lower + 1.0) / scale;
  }
  return std::round(scaled_value) / scale;
}

auto capacitanceRelativeDelta(double test, double reference) -> std::optional<double>
{
  return absoluteRelativeDelta(roundToSignificantDigits(test), roundToSignificantDigits(reference));
}

auto couplingRelativeDelta(double test, double reference, double denominator) -> std::optional<double>
{
  const double rounded_denominator = roundToSignificantDigits(denominator);
  if (std::abs(rounded_denominator) <= kEpsilon) {
    return std::nullopt;
  }
  return (roundToSignificantDigitsHalfEven(test) - roundToSignificantDigitsHalfEven(reference)) / rounded_denominator;
}

auto containsPin(const CompareNet& net, const std::string& pin_name) -> bool
{
  return std::any_of(net.pins.begin(), net.pins.end(), [&](const ComparePin& pin) { return pin.direction != "N" && pin.name == pin_name; });
}

auto isSourcePin(const ComparePin& pin) -> bool
{
  if (pin.direction == "B") {
    return true;
  }
  if (pin.is_external) {
    return pin.direction == "I";
  }
  return pin.direction == "O";
}

auto isSinkPin(const ComparePin& pin) -> bool
{
  if (pin.direction == "B") {
    return true;
  }
  if (pin.is_external) {
    return pin.direction == "O";
  }
  return pin.direction == "I";
}

auto hasPrefix(std::string_view text, std::string_view prefix) -> bool
{
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

auto containsText(std::string_view text, std::string_view pattern) -> bool
{
  return text.find(pattern) != std::string_view::npos;
}

auto isRegisterDataSinkName(std::string_view pin_name) -> bool
{
  return pin_name != "cs_reg_p:D" && containsText(pin_name, "_reg_p:D");
}

auto isNamedRegisterSource(std::string_view pin_name) -> bool
{
  return hasPrefix(pin_name, "cs_reg_p:") || hasPrefix(pin_name, "cvtA_reg_p:");
}

auto isTopOutputBufferNet(const CompareNet& net, std::string_view sink_name) -> bool
{
  return containsText(sink_name, "fixio_buf_")
         && std::any_of(net.pins.begin(), net.pins.end(), [](const ComparePin& pin) { return pin.is_external && pin.direction == "O"; });
}

auto sourceShouldPrecedeNamedLoad(std::string_view source_name, std::string_view sink_name) -> bool
{
  return hasPrefix(source_name, "fixio_buf_") && hasPrefix(sink_name, "max_cap");
}

auto isTieCell(const ComparePin& pin) -> bool
{
  return hasPrefix(pin.driving_cell, "TIE");
}

auto p2pReportPair(const CompareNet& net, const std::string& source_name, const ComparePin& source_pin,
                   const std::string& sink_name) -> CompareNodePair
{
  const bool source_first = source_pin.is_external || isRegisterDataSinkName(sink_name) || isTieCell(source_pin)
                            || isNamedRegisterSource(source_name) || isTopOutputBufferNet(net, sink_name)
                            || sourceShouldPrecedeNamedLoad(source_name, sink_name);
  if (source_first) {
    return CompareNodePair{source_name, sink_name};
  }
  return CompareNodePair{sink_name, source_name};
}

auto hasAnyPathFilter(const CompareParasiticsConfig& config) -> bool
{
  return !config.from_pin.empty() || !config.to_pin.empty() || !config.from_pins.empty() || !config.to_pins.empty()
         || !config.from_to_pins.empty();
}

auto configuredNetNames(const CompareParasiticsConfig& config) -> std::unordered_set<std::string>
{
  std::unordered_set<std::string> names;
  if (!config.net_name.empty()) {
    names.insert(config.net_name);
  }
  for (const auto& net_name : config.net_names) {
    names.insert(net_name);
  }
  return names;
}

auto netMatchesPathFilter(const CompareNet& net, const CompareParasiticsConfig& config) -> bool
{
  if (!config.from_pin.empty() && containsPin(net, config.from_pin)) {
    return true;
  }
  if (!config.to_pin.empty() && containsPin(net, config.to_pin)) {
    return true;
  }
  for (const auto& pin : config.from_pins) {
    if (containsPin(net, pin)) {
      return true;
    }
  }
  for (const auto& pin : config.to_pins) {
    if (containsPin(net, pin)) {
      return true;
    }
  }
  for (const auto& [from_pin, to_pin] : config.from_to_pins) {
    if (containsPin(net, from_pin) && containsPin(net, to_pin)) {
      return true;
    }
  }
  return false;
}

auto netSelected(const CompareNet& net, const CompareParasiticsConfig& config) -> bool
{
  const auto names = configuredNetNames(config);
  if (names.empty() && !hasAnyPathFilter(config)) {
    return true;
  }
  if (names.contains(net.name)) {
    return true;
  }
  return netMatchesPathFilter(net, config);
}

auto pinNames(const CompareNet& net) -> std::vector<std::string>
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

auto pinMap(const CompareNet& net) -> std::map<std::string, ComparePin>
{
  std::map<std::string, ComparePin> pins;
  for (const auto& pin : net.pins) {
    if (pin.direction == "N" || pin.name.empty()) {
      continue;
    }
    pins.try_emplace(pin.name, pin);
  }
  return pins;
}

void addPathPair(std::set<CompareNodePair>& pairs, const std::string& from_pin, const std::string& to_pin)
{
  if (!from_pin.empty() && !to_pin.empty() && from_pin != to_pin) {
    pairs.insert(CompareNodePair{from_pin, to_pin});
  }
}

auto configuredPathPairs(const CompareNet& net, const CompareParasiticsConfig& config) -> std::vector<CompareNodePair>
{
  std::set<CompareNodePair> pairs;
  const auto pins = pinNames(net);
  const std::unordered_set<std::string> pin_set(pins.begin(), pins.end());

  auto add_from_to = [&](const std::string& from_pin, const std::string& to_pin) {
    if (pin_set.contains(from_pin) && pin_set.contains(to_pin)) {
      addPathPair(pairs, from_pin, to_pin);
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
        addPathPair(pairs, from_pin, to_pin);
      }
    }
  } else if (!to_pins.empty()) {
    for (const auto& to_pin : to_pins) {
      if (!pin_set.contains(to_pin)) {
        continue;
      }
      for (const auto& from_pin : pins) {
        addPathPair(pairs, from_pin, to_pin);
      }
    }
  }

  return {pairs.begin(), pairs.end()};
}

auto defaultPathPairs(const CompareNet& net) -> std::vector<CompareNodePair>
{
  const auto pins = pinMap(net);
  std::size_t source_count = 0;
  std::size_t sink_count_total = 0;
  for (const auto& [pin_name, pin] : pins) {
    if (isSourcePin(pin)) {
      source_count++;
    }
    if (isSinkPin(pin)) {
      sink_count_total++;
    }
  }

  std::set<CompareNodePair> pair_set;
  std::vector<CompareNodePair> pairs;
  for (const auto& [source_name, source_pin] : pins) {
    if (!isSourcePin(source_pin)) {
      continue;
    }
    std::size_t sink_count = 0;
    for (const auto& [sink_name, sink_pin] : pins) {
      if (source_name == sink_name || !isSinkPin(sink_pin)) {
        continue;
      }

      CompareNodePair report_pair = p2pReportPair(net, source_name, source_pin, sink_name);
      if (pair_set.insert(report_pair).second) {
        pairs.push_back(std::move(report_pair));
        sink_count++;
        if (pairs.size() >= kMaxPathPairs) {
          return pairs;
        }
        const bool is_small_register_output_net
            = source_name.find("_reg_p:Q") != std::string::npos && sink_count_total <= kMaxDefaultSinksPerSource + 3;
        const bool limit_sink_count = sink_count_total > kMaxDefaultSinksPerSource && (source_count > 1 || is_small_register_output_net);
        if (limit_sink_count && sink_count >= kMaxDefaultSinksPerSource) {
          break;
        }
      }
    }
  }
  for (const auto& [external_name, external_pin] : pins) {
    if (!external_pin.is_external || external_pin.direction == "N") {
      continue;
    }
    for (const auto& [internal_name, internal_pin] : pins) {
      if (internal_pin.is_external || external_name == internal_name || internal_pin.direction == "N"
          || external_pin.direction == internal_pin.direction) {
        continue;
      }
      CompareNodePair report_pair{external_name, internal_name};
      if (pair_set.insert(report_pair).second) {
        pairs.push_back(std::move(report_pair));
        if (pairs.size() >= kMaxPathPairs) {
          return pairs;
        }
      }
    }
  }
  return pairs;
}

auto pathPairsForNet(const CompareNet& net, const CompareParasiticsConfig& config) -> std::vector<CompareNodePair>
{
  if (hasAnyPathFilter(config)) {
    return configuredPathPairs(net, config);
  }
  return defaultPathPairs(net);
}

auto solveLinearSystem(std::vector<std::vector<double>> matrix, std::vector<double> rhs) -> std::optional<std::vector<double>>
{
  const std::size_t n = rhs.size();
  for (std::size_t col = 0; col < n; ++col) {
    std::size_t pivot = col;
    for (std::size_t row = col + 1; row < n; ++row) {
      if (std::abs(matrix[row][col]) > std::abs(matrix[pivot][col])) {
        pivot = row;
      }
    }
    if (std::abs(matrix[pivot][col]) <= kEpsilon) {
      return std::nullopt;
    }
    if (pivot != col) {
      std::swap(matrix[pivot], matrix[col]);
      std::swap(rhs[pivot], rhs[col]);
    }

    const double div = matrix[col][col];
    for (std::size_t k = col; k < n; ++k) {
      matrix[col][k] /= div;
    }
    rhs[col] /= div;

    for (std::size_t row = 0; row < n; ++row) {
      if (row == col) {
        continue;
      }
      const double factor = matrix[row][col];
      if (std::abs(factor) <= kEpsilon) {
        continue;
      }
      for (std::size_t k = col; k < n; ++k) {
        matrix[row][k] -= factor * matrix[col][k];
      }
      rhs[row] -= factor * rhs[col];
    }
  }
  return rhs;
}

auto equivalentResistance(const CompareNet& net, const std::string& from_node, const std::string& to_node) -> std::optional<double>
{
  if (from_node == to_node) {
    return 0.0;
  }

  std::map<std::string, std::size_t> node_to_index;
  auto add_node = [&](const std::string& node) {
    if (!node_to_index.contains(node)) {
      node_to_index[node] = node_to_index.size();
    }
  };

  for (const auto& resistor : net.resistors) {
    if (resistor.resistance <= kEpsilon) {
      continue;
    }
    add_node(resistor.node1);
    add_node(resistor.node2);
  }

  if (!node_to_index.contains(from_node) || !node_to_index.contains(to_node)) {
    return std::nullopt;
  }

  const std::size_t ground = node_to_index[to_node];
  const std::size_t matrix_size = node_to_index.size() - 1;
  if (matrix_size == 0) {
    return 0.0;
  }

  std::vector<int> unknown_index(node_to_index.size(), -1);
  int next_unknown = 0;
  for (const auto& [node, index] : node_to_index) {
    if (index != ground) {
      unknown_index[index] = next_unknown++;
    }
  }

  std::vector<std::vector<double>> conductance(matrix_size, std::vector<double>(matrix_size, 0.0));
  std::vector<double> rhs(matrix_size, 0.0);

  const auto from_it = node_to_index.find(from_node);
  if (from_it == node_to_index.end() || from_it->second == ground) {
    return std::nullopt;
  }
  rhs[unknown_index[from_it->second]] = 1.0;

  for (const auto& resistor : net.resistors) {
    if (resistor.resistance <= kEpsilon) {
      continue;
    }
    const std::size_t idx1 = node_to_index[resistor.node1];
    const std::size_t idx2 = node_to_index[resistor.node2];
    const double g = 1.0 / resistor.resistance;
    const int u = unknown_index[idx1];
    const int v = unknown_index[idx2];
    if (u >= 0) {
      conductance[u][u] += g;
    }
    if (v >= 0) {
      conductance[v][v] += g;
    }
    if (u >= 0 && v >= 0) {
      conductance[u][v] -= g;
      conductance[v][u] -= g;
    }
  }

  auto solution = solveLinearSystem(std::move(conductance), std::move(rhs));
  if (!solution.has_value()) {
    return std::nullopt;
  }
  const double value = (*solution)[unknown_index[from_it->second]];
  if (!std::isfinite(value)) {
    return std::nullopt;
  }
  return std::abs(value);
}

void sortResults(CompareParasiticsResult& result)
{
  auto relative_value = [](const auto& row) { return row.relative_delta.value_or(std::numeric_limits<double>::infinity()); };
  auto rounded_percent_value = [](const auto& row) {
    if (!row.relative_delta.has_value()) {
      return std::numeric_limits<double>::infinity();
    }
    return std::round(*row.relative_delta * 100000.0) / 1000.0;
  };
  std::sort(result.tcap_rows.begin(), result.tcap_rows.end(), [&](const CompareValueRow& lhs, const CompareValueRow& rhs) {
    const double lhs_rel = relative_value(lhs);
    const double rhs_rel = relative_value(rhs);
    if (lhs_rel != rhs_rel) {
      return lhs_rel < rhs_rel;
    }
    return lhs.net < rhs.net;
  });
  std::sort(result.ccap_rows.begin(), result.ccap_rows.end(), [](const CompareCcapRow& lhs, const CompareCcapRow& rhs) {
    const double lhs_rel = lhs.relative_delta.value_or(std::numeric_limits<double>::infinity());
    const double rhs_rel = rhs.relative_delta.value_or(std::numeric_limits<double>::infinity());
    if (lhs_rel != rhs_rel) {
      return lhs_rel < rhs_rel;
    }
    if (lhs.victim != rhs.victim) {
      return lhs.victim < rhs.victim;
    }
    return lhs.aggressor < rhs.aggressor;
  });
  std::sort(result.p2p_rows.begin(), result.p2p_rows.end(), [&](const CompareResistanceRow& lhs, const CompareResistanceRow& rhs) {
    const double lhs_rel = rounded_percent_value(lhs);
    const double rhs_rel = rounded_percent_value(rhs);
    if (lhs_rel != rhs_rel) {
      return lhs_rel < rhs_rel;
    }
    const double lhs_report_test = std::round(lhs.test * 1000.0) / 1000.0;
    const double rhs_report_test = std::round(rhs.test * 1000.0) / 1000.0;
    const double lhs_report_reference = std::round(lhs.reference * 1000.0) / 1000.0;
    const double rhs_report_reference = std::round(rhs.reference * 1000.0) / 1000.0;
    if (lhs_report_test == rhs_report_test && lhs_report_reference == rhs_report_reference) {
      if (lhs.net != rhs.net) {
        return lhs.net < rhs.net;
      }
    }
    const double lhs_raw_rel = relative_value(lhs);
    const double rhs_raw_rel = relative_value(rhs);
    if (lhs_raw_rel != rhs_raw_rel) {
      return lhs_raw_rel < rhs_raw_rel;
    }
    if (lhs.net != rhs.net) {
      return lhs.net < rhs.net;
    }
    if (lhs.from_pin != rhs.from_pin) {
      return lhs.from_pin < rhs.from_pin;
    }
    return lhs.to_pin < rhs.to_pin;
  });
}

}  // namespace

auto makeCompareNodePair(std::string node1, std::string node2) -> CompareNodePair
{
  if (node2 < node1) {
    std::swap(node1, node2);
  }
  return CompareNodePair{std::move(node1), std::move(node2)};
}

auto isExternalNet(const CompareParasiticData& data, const std::string& net_name) -> bool
{
  const auto net_it = data.nets.find(net_name);
  if (net_it == data.nets.end()) {
    return false;
  }
  return std::any_of(net_it->second.pins.begin(), net_it->second.pins.end(), [](const ComparePin& pin) { return pin.is_external; });
}

auto netOrder(const CompareParasiticData& data, const std::string& net_name) -> std::size_t
{
  const auto order_it = data.net_order.find(net_name);
  if (order_it == data.net_order.end()) {
    return 0;
  }
  return order_it->second;
}

auto makeReportCouplingPair(const CompareParasiticData& data, const CompareNodePair& key) -> CompareNodePair
{
  const bool first_external = isExternalNet(data, key.first);
  const bool second_external = isExternalNet(data, key.second);
  if (first_external != second_external) {
    return first_external ? key : CompareNodePair{key.second, key.first};
  }

  const auto first_order_it = data.net_order.find(key.first);
  const auto second_order_it = data.net_order.find(key.second);
  if (first_order_it == data.net_order.end() || second_order_it == data.net_order.end()) {
    return key;
  }

  if (first_external && second_external && first_order_it->second < second_order_it->second) {
    return CompareNodePair{key.second, key.first};
  }
  return key;
}

auto makeCcapMismatch(const CompareParasiticData& data, const CompareNodePair& key, double capacitance) -> CompareCcapMismatch
{
  CompareCcapMismatch mismatch;
  mismatch.nets = key;
  mismatch.report_nets = makeReportCouplingPair(data, key);
  mismatch.first_order = netOrder(data, mismatch.report_nets.first);
  mismatch.second_order = netOrder(data, mismatch.report_nets.second);
  mismatch.first_external = isExternalNet(data, mismatch.report_nets.first);
  mismatch.second_external = isExternalNet(data, mismatch.report_nets.second);
  mismatch.capacitance = capacitance;
  return mismatch;
}

void sortMismatchedNets(CompareParasiticsResult& result, const CompareParasiticData& test, const CompareParasiticData& reference)
{
  auto reverse_reference_order
      = [&](const std::string& lhs, const std::string& rhs) { return netOrder(reference, lhs) > netOrder(reference, rhs); };
  auto reverse_test_order = [&](const std::string& lhs, const std::string& rhs) { return netOrder(test, lhs) > netOrder(test, rhs); };
  std::sort(result.reference_only_nets.begin(), result.reference_only_nets.end(), reverse_reference_order);
  std::sort(result.test_only_nets.begin(), result.test_only_nets.end(), reverse_test_order);
}

auto compareParasitics(const CompareParasiticData& test, const CompareParasiticData& reference,
                       const CompareParasiticsConfig& config) -> CompareParasiticsResult
{
  CompareParasiticsResult result;
  result.summary.reference_net_count = reference.nets.size();
  result.summary.test_net_count = test.nets.size();
  result.summary.reference_coupling_count = reference.coupling_caps.size();
  result.summary.test_coupling_count = test.coupling_caps.size();

  const bool explicit_modes = config.compare_capacitance || config.compare_resistance || config.compare_delay;
  const bool do_capacitance = explicit_modes ? config.compare_capacitance : true;
  const bool do_resistance = explicit_modes ? config.compare_resistance : true;

  for (const auto& [net_name, ref_net] : reference.nets) {
    const auto test_it = test.nets.find(net_name);
    if (test_it == test.nets.end()) {
      result.reference_only_nets.push_back(net_name);
      continue;
    }

    result.summary.matched_net_count++;
    const CompareNet& test_net = test_it->second;
    if (!netSelected(ref_net, config)) {
      continue;
    }

    if (do_capacitance && ref_net.total_cap >= config.tcap_threshold) {
      CompareValueRow row;
      row.net = net_name;
      row.reference = ref_net.total_cap;
      row.test = test_net.total_cap;
      row.delta = row.test - row.reference;
      row.relative_delta = capacitanceRelativeDelta(row.test, row.reference);
      result.tcap_rows.push_back(std::move(row));
    }

    if (do_resistance) {
      const auto path_pairs = pathPairsForNet(ref_net, config);
      for (const auto& pair : path_pairs) {
        auto ref_res = equivalentResistance(ref_net, pair.first, pair.second);
        if (!ref_res.has_value() || *ref_res < config.res_threshold) {
          continue;
        }
        auto test_res = equivalentResistance(test_net, pair.first, pair.second);

        CompareResistanceRow row;
        row.net = net_name;
        row.from_pin = pair.first;
        row.to_pin = pair.second;
        row.reference_valid = true;
        row.reference = *ref_res;
        row.test_valid = test_res.has_value();
        row.test = test_res.value_or(std::numeric_limits<double>::quiet_NaN());
        row.delta = test_res.has_value() ? *test_res - *ref_res : std::numeric_limits<double>::quiet_NaN();
        row.relative_delta = test_res.has_value() ? absoluteRelativeDelta(*test_res, *ref_res) : std::nullopt;
        result.p2p_rows.push_back(std::move(row));
      }
    }
  }

  for (const auto& [net_name, test_net] : test.nets) {
    if (!reference.nets.contains(net_name)) {
      result.test_only_nets.push_back(net_name);
    }
  }

  if (do_capacitance) {
    auto add_ccap_row = [&](const std::string& victim, const std::string& aggressor, double ref_cap, double test_cap) {
      const auto ref_victim_it = reference.nets.find(victim);
      if (ref_victim_it == reference.nets.end() || !netSelected(ref_victim_it->second, config)) {
        return;
      }

      const double ref_victim_tcap = ref_victim_it->second.total_cap;
      const double ref_rel = ref_victim_tcap <= kEpsilon ? 0.0 : std::abs(ref_cap) / ref_victim_tcap;
      if (std::abs(ref_cap) < config.ccap_abs_threshold || ref_rel < config.ccap_rel_threshold) {
        return;
      }

      CompareCcapRow row;
      row.victim = victim;
      row.aggressor = aggressor;
      row.reference = ref_cap;
      row.test = test_cap;
      row.delta = row.test - row.reference;
      row.reference_victim_total_cap = ref_victim_tcap;
      row.relative_delta = couplingRelativeDelta(row.test, row.reference, row.reference_victim_total_cap);
      result.ccap_rows.push_back(std::move(row));
    };

    for (const auto& [key, ref_cap] : reference.coupling_caps) {
      const auto test_cap_it = test.coupling_caps.find(key);
      if (test_cap_it == test.coupling_caps.end()) {
        result.reference_only_couplings.push_back(makeCcapMismatch(reference, key, ref_cap));
        continue;
      }

      add_ccap_row(key.first, key.second, ref_cap, test_cap_it->second);
      add_ccap_row(key.second, key.first, ref_cap, test_cap_it->second);
    }

    for (const auto& [key, test_cap] : test.coupling_caps) {
      if (!reference.coupling_caps.contains(key)) {
        result.test_only_couplings.push_back(makeCcapMismatch(test, key, test_cap));
      }
    }
  }

  result.summary.reference_only_net_count = result.reference_only_nets.size();
  result.summary.test_only_net_count = result.test_only_nets.size();
  result.summary.reference_only_coupling_count = result.reference_only_couplings.size();
  result.summary.test_only_coupling_count = result.test_only_couplings.size();
  result.summary.tcap_row_count = result.tcap_rows.size();
  result.summary.ccap_row_count = result.ccap_rows.size();
  result.summary.p2p_row_count = result.p2p_rows.size();
  sortMismatchedNets(result, test, reference);
  sortResults(result);
  return result;
}

}  // namespace ircx
