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
#include "CompareParasiticsFlow.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

#include "CompareParasitics.hh"
#include "SpefParser.hh"
#include "log/Log.hh"

namespace ircx {
namespace {

auto trim(std::string_view text) -> std::string_view
{
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.remove_prefix(1);
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.remove_suffix(1);
  }
  return text;
}

auto startsWith(std::string_view text, std::string_view prefix) -> bool
{
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

auto directionName(ista::spef::ConnectionDirection direction) -> std::string
{
  switch (direction) {
    case ista::spef::ConnectionDirection::kInput:
      return "I";
    case ista::spef::ConnectionDirection::kOutput:
      return "O";
    case ista::spef::ConnectionDirection::kInout:
      return "B";
    case ista::spef::ConnectionDirection::kInternal:
      return "N";
    case ista::spef::ConnectionDirection::kUninitialized:
      break;
  }
  return "";
}

void addNetConfigLine(CompareParasiticsConfig& config, std::string_view line)
{
  line = trim(line);
  if (line.empty() || startsWith(line, "//") || startsWith(line, "**")) {
    return;
  }

  constexpr std::string_view net_prefix = "NET:";
  constexpr std::string_view from_prefix = "FROM_PIN:";
  constexpr std::string_view to_prefix = "TO_PIN:";
  constexpr std::string_view from_to_prefix = "FROM_TO_PINS:";

  if (startsWith(line, net_prefix)) {
    const auto value = trim(line.substr(net_prefix.size()));
    if (!value.empty()) {
      config.net_names.emplace_back(value);
    }
    return;
  }
  if (startsWith(line, from_prefix)) {
    const auto value = trim(line.substr(from_prefix.size()));
    if (!value.empty()) {
      config.from_pins.emplace_back(value);
    }
    return;
  }
  if (startsWith(line, to_prefix)) {
    const auto value = trim(line.substr(to_prefix.size()));
    if (!value.empty()) {
      config.to_pins.emplace_back(value);
    }
    return;
  }
  if (startsWith(line, from_to_prefix)) {
    std::istringstream iss(std::string{trim(line.substr(from_to_prefix.size()))});
    std::string from_pin;
    std::string to_pin;
    iss >> from_pin >> to_pin;
    if (!from_pin.empty() && !to_pin.empty()) {
      config.from_to_pins.emplace_back(std::move(from_pin), std::move(to_pin));
    }
  }
}

auto readNetConfig(CompareParasiticsConfig& config) -> bool
{
  if (config.net_config_file.empty()) {
    return true;
  }

  std::ifstream ifs(config.net_config_file);
  if (!ifs.is_open()) {
    LOG_ERROR << "compare_parasitics failed: cannot open -net_config file " << config.net_config_file;
    return false;
  }

  std::string line;
  while (std::getline(ifs, line)) {
    addNetConfigLine(config, line);
  }
  return true;
}

auto validateConfig(const CompareParasiticsConfig& config) -> bool
{
  if (config.test_file.empty() || config.reference_file.empty()) {
    LOG_ERROR << "compare_parasitics requires test and reference SPEF files.";
    return false;
  }
  if (config.match_mode != "name") {
    LOG_ERROR << "compare_parasitics currently supports only -match name for SPEF comparison.";
    return false;
  }
  if (!config.corner.empty()) {
    LOG_ERROR << "compare_parasitics -corner is valid only for GPD comparison, which is not supported yet.";
    return false;
  }
  if (config.compare_delay) {
    LOG_ERROR << "compare_parasitics -d/-delay Elmore delay comparison is not implemented yet.";
    return false;
  }
  if (!config.net_name.empty() && (!config.from_pin.empty() || !config.to_pin.empty())) {
    LOG_ERROR << "compare_parasitics -net cannot be used with -from_pin or -to_pin.";
    return false;
  }
  if (config.tcap_threshold < 0.0 || config.ccap_abs_threshold < 0.0 || config.ccap_rel_threshold < 0.0 || config.res_threshold < 0.0) {
    LOG_ERROR << "compare_parasitics thresholds must be non-negative.";
    return false;
  }
  return true;
}

void rememberNodeNet(CompareParasiticData& data, const std::string& node_name, const std::string& net_name)
{
  if (!node_name.empty() && !net_name.empty()) {
    data.node_to_net.try_emplace(node_name, net_name);
  }
}

auto resolveNodeNet(const CompareParasiticData& data, const std::string& node_name) -> std::string
{
  const auto node_it = data.node_to_net.find(node_name);
  if (node_it != data.node_to_net.end()) {
    return node_it->second;
  }

  const auto colon = node_name.find(':');
  if (colon != std::string::npos) {
    const auto prefix = node_name.substr(0, colon);
    if (data.nets.contains(prefix)) {
      return prefix;
    }
  }

  if (data.nets.contains(node_name)) {
    return node_name;
  }
  return {};
}

void buildNetCouplingCaps(CompareParasiticData& data)
{
  std::set<CompareNodePair> seen_node_pairs;
  for (const auto& [net_name, net] : data.nets) {
    for (const auto& [node_pair, capacitance] : net.coupling_caps) {
      if (!seen_node_pairs.insert(node_pair).second) {
        continue;
      }

      const auto net1 = resolveNodeNet(data, node_pair.first);
      const auto net2 = resolveNodeNet(data, node_pair.second);
      if (net1.empty() || net2.empty() || net1 == net2) {
        continue;
      }
      data.coupling_caps[makeCompareNodePair(net1, net2)] += capacitance;
    }
  }
}

auto loadSpef(const std::string& path, CompareParasiticData& data) -> bool
{
  ista::spef::SpefReader reader;
  if (!reader.read(path)) {
    return false;
  }
  reader.expandName();

  const auto* spef_file = reader.getSpefFile();
  if (spef_file == nullptr) {
    return false;
  }

  data = CompareParasiticData{};
  data.file_name = path;
  data.cap_unit = reader.getSpefCapUnit();
  data.res_unit = reader.getSpefResUnit();

  for (const auto& spef_net : spef_file->nets) {
    CompareNet net;
    net.name = spef_net.name;
    net.total_cap = spef_net.lcap;

    for (const auto& conn : spef_net.conns) {
      ComparePin pin;
      pin.name = conn.pin_port_name;
      pin.direction = directionName(conn.conn_direction);
      pin.driving_cell = conn.driving_cell;
      pin.is_external = conn.conn_type == ista::spef::ConnectionType::kExternal;
      pin.x = conn.coordinate.x;
      pin.y = conn.coordinate.y;
      pin.has_coordinate = conn.coordinate.x >= 0.0 && conn.coordinate.y >= 0.0;
      rememberNodeNet(data, pin.name, net.name);
      net.pins.push_back(std::move(pin));
    }

    for (const auto& cap : spef_net.caps) {
      if (cap.node2.empty()) {
        net.ground_caps[cap.node1] += cap.res_or_cap;
        rememberNodeNet(data, cap.node1, net.name);
      } else {
        rememberNodeNet(data, cap.node1, net.name);
        net.coupling_caps[makeCompareNodePair(cap.node1, cap.node2)] += cap.res_or_cap;
      }
    }

    for (const auto& res : spef_net.ress) {
      if (res.node1.empty() || res.node2.empty()) {
        continue;
      }
      rememberNodeNet(data, res.node1, net.name);
      rememberNodeNet(data, res.node2, net.name);
      net.resistors.push_back(CompareResistor{res.node1, res.node2, res.res_or_cap});
    }

    const std::string net_name = net.name;
    data.net_order.try_emplace(net_name, data.net_order.size());
    data.nets[net_name] = std::move(net);
  }

  buildNetCouplingCaps(data);
  return true;
}

auto ensureOutputDir(const std::string& output_dir) -> bool
{
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  if (ec) {
    LOG_ERROR << "compare_parasitics failed: cannot create output directory " << output_dir << ": " << ec.message();
    return false;
  }
  if (!std::filesystem::is_directory(output_dir, ec)) {
    LOG_ERROR << "compare_parasitics failed: output path is not a directory " << output_dir;
    return false;
  }
  return true;
}

auto openReport(const std::filesystem::path& path) -> std::ofstream
{
  std::ofstream ofs(path);
  ofs << std::fixed << std::setprecision(3);
  return ofs;
}

void writeDouble(std::ostream& os, double value)
{
  if (std::isfinite(value)) {
    os << value;
  } else {
    os << "NA";
  }
}

void writePercent(std::ostream& os, const std::optional<double>& value)
{
  if (value.has_value()) {
    os << (*value * 100.0);
  } else {
    os << "NA";
  }
}

template <typename Row>
auto collectPercentErrors(const std::vector<Row>& rows) -> std::vector<double>
{
  std::vector<double> values;
  values.reserve(rows.size());
  for (const auto& row : rows) {
    if (row.relative_delta.has_value()) {
      values.push_back(*row.relative_delta * 100.0);
    }
  }
  return values;
}

auto mean(const std::vector<double>& values) -> double
{
  if (values.empty()) {
    return 0.0;
  }
  double sum = 0.0;
  for (double value : values) {
    sum += value;
  }
  return sum / static_cast<double>(values.size());
}

auto standardDeviation(const std::vector<double>& values, double mean_value) -> double
{
  if (values.empty()) {
    return 0.0;
  }
  double sum = 0.0;
  for (double value : values) {
    const double delta = value - mean_value;
    sum += delta * delta;
  }
  return std::sqrt(sum / static_cast<double>(values.size()));
}

auto minValue(const std::vector<double>& values) -> double
{
  return values.empty() ? 0.0 : *std::min_element(values.begin(), values.end());
}

auto maxValue(const std::vector<double>& values) -> double
{
  return values.empty() ? 0.0 : *std::max_element(values.begin(), values.end());
}

auto distributionBins(const std::vector<double>& values) -> std::map<int, std::size_t>
{
  std::map<int, std::size_t> bins;
  for (int bin = -32; bin <= 32; bin += 2) {
    bins[bin] = 0;
  }
  for (double value : values) {
    int bin = static_cast<int>(std::round(value / 2.0)) * 2;
    bin = std::clamp(bin, -32, 32);
    bins[bin]++;
  }
  return bins;
}

void writeDistribution(std::ostream& ofs, const std::string& title, const std::string& threshold_label, double threshold,
                       const std::string& count_label, const std::vector<double>& errors)
{
  const double error_mean = mean(errors);
  ofs << "-------------------- " << title << " Distribution --------------------\n";
  ofs << threshold_label << ": " << threshold << '\n';
  ofs << "Min Error: " << minValue(errors) << "% \tMax Error: " << maxValue(errors) << "%\n";
  ofs << "Mean Error: " << error_mean << "% \tStandard dev: " << standardDeviation(errors, error_mean) << "%\n";
  ofs << count_label << ": " << errors.size() << '\n';
  const auto bins = distributionBins(errors);
  for (const auto& [bin, count] : bins) {
    ofs << std::setw(7) << bin << "% " << std::setw(11) << count << '\n';
  }
}

auto writeTcapReport(const std::filesystem::path& output_dir, const CompareParasiticsConfig& config,
                     const CompareParasiticsResult& result) -> bool
{
  const auto report_path = output_dir / "tcap.rpt";
  auto ofs = openReport(report_path);
  if (!ofs.is_open()) {
    LOG_ERROR << "compare_parasitics failed: cannot open report " << report_path;
    return false;
  }
  ofs << config.test_file << '\t' << config.reference_file << "\t%diff\tNetname\n";
  for (const auto& row : result.tcap_rows) {
    ofs << row.test << '\t' << row.reference << '\t';
    writePercent(ofs, row.relative_delta);
    ofs << '\t' << row.net << '\n';
  }
  return true;
}

auto writeCcapReport(const std::filesystem::path& output_dir, const CompareParasiticsConfig& config,
                     const CompareParasiticsResult& result) -> bool
{
  const auto report_path = output_dir / "ccap.rpt";
  auto ofs = openReport(report_path);
  if (!ofs.is_open()) {
    LOG_ERROR << "compare_parasitics failed: cannot open report " << report_path;
    return false;
  }
  ofs << config.test_file << '\t' << config.reference_file << "\t%diff\tVictim\tAggressor\tTCAP Victim\n";
  for (const auto& row : result.ccap_rows) {
    ofs << row.test << '\t' << row.reference << '\t';
    writePercent(ofs, row.relative_delta);
    ofs << '\t' << row.victim << '\t' << row.aggressor << '\t' << row.reference_victim_total_cap << '\n';
  }
  return true;
}

auto writeP2PReport(const std::filesystem::path& output_dir, const CompareParasiticsConfig& config,
                    const CompareParasiticsResult& result) -> bool
{
  const auto report_path = output_dir / "p2p.rpt";
  auto ofs = openReport(report_path);
  if (!ofs.is_open()) {
    LOG_ERROR << "compare_parasitics failed: cannot open report " << report_path;
    return false;
  }
  ofs << config.test_file << '\t' << config.reference_file << "\t%diff\tNetname\tPin1\tPin2\n";
  for (const auto& row : result.p2p_rows) {
    writeDouble(ofs, row.test);
    ofs << '\t';
    writeDouble(ofs, row.reference);
    ofs << '\t';
    writePercent(ofs, row.relative_delta);
    ofs << '\t' << row.net << '\t' << row.from_pin << '\t' << row.to_pin << '\n';
  }
  return true;
}

auto writeMismatchedNets(const std::filesystem::path& output_dir, const CompareParasiticsResult& result) -> bool
{
  const auto report_path = output_dir / "nets.mismatched";
  auto ofs = openReport(report_path);
  if (!ofs.is_open()) {
    LOG_ERROR << "compare_parasitics failed: cannot open report " << report_path;
    return false;
  }
  ofs << "Total# of nets in reference: " << result.summary.reference_net_count << '\n';
  ofs << "Total# of nets in test: " << result.summary.test_net_count << "\n\n";
  ofs << "# of missing nets: " << result.summary.reference_only_net_count << '\n';
  for (const auto& net : result.reference_only_nets) {
    ofs << "\tMIT: " << net << "\t0.000\n";
  }
  ofs << "\n# of extra nets: " << result.summary.test_only_net_count << '\n';
  for (const auto& net : result.test_only_nets) {
    ofs << "\tMIG: " << net << "\t0.000\n";
  }
  return true;
}

auto writeMismatchedCouplings(const std::filesystem::path& output_dir, const CompareParasiticsResult& result) -> bool
{
  const auto report_path = output_dir / "coupling_caps.mismatched";
  auto ofs = openReport(report_path);
  if (!ofs.is_open()) {
    LOG_ERROR << "compare_parasitics failed: cannot open report " << report_path;
    return false;
  }

  auto sorted_reference_only = result.reference_only_couplings;
  auto sorted_test_only = result.test_only_couplings;
  auto reverse_pair_order = [](const CompareCcapMismatch& lhs, const CompareCcapMismatch& rhs) {
    if (lhs.first_external != rhs.first_external) {
      return !lhs.first_external;
    }
    if (lhs.first_order != rhs.first_order) {
      return lhs.first_external ? lhs.first_order < rhs.first_order : lhs.first_order > rhs.first_order;
    }
    if (lhs.first_external && lhs.second_external != rhs.second_external) {
      return !lhs.second_external;
    }
    if (lhs.second_order != rhs.second_order) {
      return lhs.first_external && lhs.second_external ? lhs.second_order < rhs.second_order : lhs.second_order > rhs.second_order;
    }
    if (lhs.report_nets.first != rhs.report_nets.first) {
      return lhs.report_nets.first > rhs.report_nets.first;
    }
    if (lhs.report_nets.second != rhs.report_nets.second) {
      return lhs.report_nets.second > rhs.report_nets.second;
    }
    return lhs.capacitance < rhs.capacitance;
  };
  std::sort(sorted_reference_only.begin(), sorted_reference_only.end(), reverse_pair_order);
  std::sort(sorted_test_only.begin(), sorted_test_only.end(), reverse_pair_order);

  ofs << "Total# of coupling caps in reference: " << result.summary.reference_coupling_count << '\n';
  ofs << "Total# of coupling caps in test: " << result.summary.test_coupling_count << "\n\n";
  ofs << "# of missing coupling caps: " << result.summary.reference_only_coupling_count << '\n';
  for (const auto& mismatch : sorted_reference_only) {
    ofs << "\tMIT: " << mismatch.report_nets.first << '\t' << mismatch.report_nets.second << '\t' << mismatch.capacitance << '\n';
  }
  ofs << "\n# of extra coupling caps: " << result.summary.test_only_coupling_count << '\n';
  for (const auto& mismatch : sorted_test_only) {
    ofs << "\tMIG: " << mismatch.report_nets.first << '\t' << mismatch.report_nets.second << '\t' << mismatch.capacitance << '\n';
  }
  return true;
}

auto writeSummaryReport(const std::filesystem::path& output_dir, const CompareParasiticsConfig& config, const CompareParasiticData& test,
                        const CompareParasiticData& reference, const CompareParasiticsResult& result) -> bool
{
  const auto report_path = output_dir / "summary.rpt";
  auto ofs = openReport(report_path);
  if (!ofs.is_open()) {
    LOG_ERROR << "compare_parasitics failed: cannot open report " << report_path;
    return false;
  }

  const auto tcap_errors = collectPercentErrors(result.tcap_rows);
  const auto ccap_errors = collectPercentErrors(result.ccap_rows);
  const auto p2p_errors = collectPercentErrors(result.p2p_rows);
  const double tcap_mean = mean(tcap_errors);
  const double ccap_mean = mean(ccap_errors);
  const double p2p_mean = mean(p2p_errors);

  ofs << "\n ##########################################################\n";
  ofs << " #                                                        #\n";
  ofs << " #               compare_parasitics Utility               #\n";
  ofs << " #                                                        #\n";
  ofs << " ##########################################################\n\n";
  ofs << "=====================================================================\n\n";
  ofs << "                 compare_parasitics Utility\n\n";
  ofs << "\t(IN)           MY RC FILE : " << config.test_file << '\n';
  ofs << "\t(IN)         GOLD RC FILE : " << config.reference_file << '\n';
  ofs << "\t(OUT)      SUMMARY REPORT : summary.rpt\n";
  ofs << "\t(OUT)    TOTAL CAP REPORT : tcap.rpt\n";
  ofs << "\t(OUT) COUPLING CAP REPORT : ccap.rpt\n";
  ofs << "\t(OUT)      RES P2P REPORT : p2p.rpt\n\n";
  ofs << "=====================================================================\n\n\n";

  ofs << "-------------------- RC Correlation Overview --------------------\n";
  ofs << "Total cap     (C) mean error (abs = " << config.tcap_threshold << "fF): " << tcap_mean << "%\n";
  ofs << "                   std error (abs = " << config.tcap_threshold << "fF): " << standardDeviation(tcap_errors, tcap_mean) << "%\n";
  ofs << "Coupling cap (CC) mean error (abs = " << config.ccap_abs_threshold << "fF, rel = " << config.ccap_rel_threshold
      << "): " << ccap_mean << "%\n";
  ofs << "                   std error (abs = " << config.ccap_abs_threshold << "fF, rel = " << config.ccap_rel_threshold
      << "): " << standardDeviation(ccap_errors, ccap_mean) << "%\n";
  ofs << "Pin-Pin res (P2P) mean error (abs = " << config.res_threshold << "Ohm): " << p2p_mean << "%\n";
  ofs << "                   std error (abs = " << config.res_threshold << "Ohm): " << standardDeviation(p2p_errors, p2p_mean) << "%\n";
  writeDistribution(ofs, "TCAP", "TCAP threshold", config.tcap_threshold, "Number of matched nets", tcap_errors);
  writeDistribution(ofs, "CCAP", "CCAP threshold", config.ccap_abs_threshold, "Number of matched net pairs", ccap_errors);
  writeDistribution(ofs, "P2P", "RES threshold", config.res_threshold, "Number of matched pin pairs", p2p_errors);
  return true;
}

auto writeReports(const CompareParasiticsConfig& config, const CompareParasiticData& test, const CompareParasiticData& reference,
                  const CompareParasiticsResult& result) -> bool
{
  const std::filesystem::path output_dir(config.output_dir);
  const bool ok = writeTcapReport(output_dir, config, result) && writeCcapReport(output_dir, config, result)
                  && writeP2PReport(output_dir, config, result) && writeMismatchedNets(output_dir, result)
                  && writeMismatchedCouplings(output_dir, result) && writeSummaryReport(output_dir, config, test, reference, result);
  if (!ok) {
    LOG_ERROR << "compare_parasitics failed: cannot write one or more reports under " << config.output_dir;
  }
  return ok;
}

}  // namespace

auto CompareParasiticsFlow::run(CompareParasiticsConfig config) -> bool
{
  LOG_INFO << "compare_parasitics begin.";

  if (!readNetConfig(config) || !validateConfig(config) || !ensureOutputDir(config.output_dir)) {
    LOG_INFO << "compare_parasitics end.";
    return false;
  }

  CompareParasiticData test;
  CompareParasiticData reference;
  if (!loadSpef(config.test_file, test)) {
    LOG_ERROR << "compare_parasitics failed: read test SPEF failed: " << config.test_file;
    LOG_INFO << "compare_parasitics end.";
    return false;
  }
  if (!loadSpef(config.reference_file, reference)) {
    LOG_ERROR << "compare_parasitics failed: read reference SPEF failed: " << config.reference_file;
    LOG_INFO << "compare_parasitics end.";
    return false;
  }

  auto result = compareParasitics(test, reference, config);
  if (!writeReports(config, test, reference, result)) {
    LOG_INFO << "compare_parasitics end.";
    return false;
  }

  LOG_INFO << "compare_parasitics wrote reports to " << config.output_dir;
  LOG_INFO << "compare_parasitics end.";
  return true;
}

}  // namespace ircx
