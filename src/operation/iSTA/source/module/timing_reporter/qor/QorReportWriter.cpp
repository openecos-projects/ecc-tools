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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "QorReportWriter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <ios>
#include <set>
#include <sstream>
#include <string>

#include "ReportFormatter.hpp"
#include "ReportPathFilter.hpp"
#include "ReportPathSelector.hpp"
#include "TimingTolerance.hpp"
#include "Utility.hpp"
#include "network/Network.hpp"

namespace ista {
void QorReportWriter::write()
{
  auto summary = _qor_summary_builder.build(_report_path_selector);
  auto& [setup_wns_map, setup_tns_map, setup_nvp_map, setup_frequency_map, hold_wns_map, hold_tns_map, hold_nvp_map, group_set, total_setup_tns,
         total_setup_nvp, total_hold_tns, total_hold_nvp, setup_group_list, hold_group_list, fanout_violations, capacitance_violations, transition_violations,
         cell_area, leaf_cell_k]
      = summary;
  int32_t max_group_length = 20;
  for (std::string group_name : group_set) {
    max_group_length = std::max(max_group_length, static_cast<int32_t>(group_name.length()) + 2);
  }
  std::string bar(max_group_length + 71, '-');

  std::string worst_setup_wns = getQorNilString(10);
  if (!setup_group_list.empty()) {
    worst_setup_wns = getQorDoubleString(setup_wns_map[setup_group_list.front()], 10, 3);
  }
  std::string worst_hold_wns = getQorNilString(10);
  if (!hold_group_list.empty()) {
    worst_hold_wns = getQorDoubleString(hold_wns_map[hold_group_list.front()], 10, 3);
  }

  std::string worst_frequency = getQorFrequencyString(0.0);
  if (!setup_group_list.empty() && setup_frequency_map.count(setup_group_list.front()) > 0) {
    worst_frequency = getQorFrequencyString(setup_frequency_map[setup_group_list.front()]);
  }

  auto output_qor_summary_json = [&]() {
    auto json_file = STAUTIL.openOutputFile(getQorSummaryJsonFilePath());
    auto output_double = [&json_file](double value, int32_t precision) {
      if (!std::isfinite(value)) {
        json_file << "null";
        return;
      }
      if (std::fabs(value) < ista::kTimingTolerance) {
        value = 0.0;
      }
      json_file << std::fixed << std::setprecision(precision) << value;
    };
    auto output_frequency = [&](double frequency) {
      if (frequency <= ista::kTimingTolerance) {
        json_file << "null";
        return;
      }
      output_double(frequency, 0);
    };
    auto output_setup = [&](const std::string& group_name) {
      auto wns = setup_wns_map.find(group_name);
      if (wns == setup_wns_map.end()) {
        json_file << "null";
        return;
      }
      json_file << "{\"wns\":";
      output_double(wns->second, 3);
      json_file << ",\"tns\":";
      output_double(setup_tns_map[group_name], 1);
      json_file << ",\"nvp\":" << setup_nvp_map[group_name] << ",\"frequency_mhz\":";
      output_frequency(setup_frequency_map[group_name]);
      json_file << "}";
    };
    auto output_hold = [&](const std::string& group_name) {
      auto wns = hold_wns_map.find(group_name);
      if (wns == hold_wns_map.end()) {
        json_file << "null";
        return;
      }
      json_file << "{\"wns\":";
      output_double(wns->second, 3);
      json_file << ",\"tns\":";
      output_double(hold_tns_map[group_name], 1);
      json_file << ",\"nvp\":" << hold_nvp_map[group_name] << "}";
    };
    auto output_path_group = [&](const std::string& group_name) {
      json_file << "{\"name\":\"" << escapeJsonString(group_name) << "\",\"setup\":";
      output_setup(group_name);
      json_file << ",\"hold\":";
      output_hold(group_name);
      json_file << "}";
    };

    json_file << "{\n  \"path_groups\": [";
    bool first_group = true;
    for (const std::string& group_name : setup_group_list) {
      if (!first_group) {
        json_file << ",";
      }
      output_path_group(group_name);
      first_group = false;
    }
    for (const std::string& group_name : hold_group_list) {
      if (setup_wns_map.count(group_name) > 0) {
        continue;
      }
      if (!first_group) {
        json_file << ",";
      }
      output_path_group(group_name);
      first_group = false;
    }
    json_file << "],\n  \"summary\": {\"setup\":";
    if (setup_group_list.empty()) {
      json_file << "null";
    } else {
      json_file << "{\"wns\":";
      output_double(setup_wns_map[setup_group_list.front()], 3);
      json_file << ",\"tns\":";
      output_double(total_setup_tns, 1);
      json_file << ",\"nvp\":" << total_setup_nvp << ",\"frequency_mhz\":";
      output_frequency(setup_frequency_map[setup_group_list.front()]);
      json_file << "}";
    }
    json_file << ",\"hold\":";
    if (hold_group_list.empty()) {
      json_file << "null";
    } else {
      json_file << "{\"wns\":";
      output_double(hold_wns_map[hold_group_list.front()], 3);
      json_file << ",\"tns\":";
      output_double(total_hold_tns, 1);
      json_file << ",\"nvp\":" << total_hold_nvp << "}";
    }
    json_file << "},\n  \"design_statistics\": {\"cap\":" << capacitance_violations << ",\"fanout\":" << fanout_violations
              << ",\"tran\":" << transition_violations << ",\"tdrc\":" << (fanout_violations + capacitance_violations + transition_violations)
              << ",\"cella\":" << cell_area << ",\"bufs\":null,\"leafs_k\":" << leaf_cell_k << ",\"tnets_k\":null,\"ctbuf\":null,\"regs\":null}\n}\n";
    STAUTIL.finishOutputFile(json_file);
  };

  if (_config.output_timing_reports == 0) {
    if (_config.output_timing_features != 0) {
      output_qor_summary_json();
    }
    return;
  }

  std::string report_file_path = getQorSummaryReportFilePath();
  auto report_file = STAUTIL.openOutputFile(report_file_path);
  report_file << std::left << std::setw(max_group_length) << "Path Group" << std::right << std::setw(11) << "WNS" << std::setw(11) << "TNS" << std::setw(8)
              << "NVP" << std::setw(10) << "FREQ" << "    " << std::setw(8) << "WNS(H)" << std::setw(11) << "TNS(H)" << std::setw(8) << "NVP(H)" << "\n";
  report_file << bar << "\n";

  std::set<std::string> printed_group_set;
  for (std::string& group_name : setup_group_list) {
    report_file << std::left << std::setw(max_group_length) << group_name << std::right << " " << getQorDoubleString(setup_wns_map[group_name], 10, 3) << " "
                << getQorDoubleString(setup_tns_map[group_name], 10, 1) << " " << getQorIntString(setup_nvp_map[group_name], 7) << " "
                << getQorFrequencyString(setup_frequency_map[group_name]) << " ";
    if (hold_wns_map.count(group_name) > 0) {
      report_file << getQorDoubleString(hold_wns_map[group_name], 10, 3) << " " << getQorDoubleString(hold_tns_map[group_name], 10, 1) << " "
                  << getQorIntString(hold_nvp_map[group_name], 7);
    } else {
      report_file << getQorNilString(10) << " " << getQorNilString(10) << " " << getQorNilString(7);
    }
    report_file << "\n";
    printed_group_set.insert(group_name);
  }

  for (std::string& group_name : hold_group_list) {
    if (printed_group_set.count(group_name) > 0) {
      continue;
    }
    report_file << std::left << std::setw(max_group_length) << group_name << std::right << " " << getQorNilString(10) << " " << getQorNilString(10) << " "
                << getQorNilString(7) << " " << getQorNilString(10) << " " << getQorDoubleString(hold_wns_map[group_name], 10, 3) << " "
                << getQorDoubleString(hold_tns_map[group_name], 10, 1) << " " << getQorIntString(hold_nvp_map[group_name], 7) << "\n";
  }

  report_file << bar << "\n";
  report_file << std::left << std::setw(max_group_length) << "Summary" << std::right << " " << worst_setup_wns << " "
              << getQorDoubleString(total_setup_tns, 10, 1) << " " << getQorIntString(total_setup_nvp, 7) << " " << worst_frequency << " " << worst_hold_wns
              << " " << getQorDoubleString(total_hold_tns, 10, 1) << " " << getQorIntString(total_hold_nvp, 7) << "\n";
  report_file << bar << "\n";
  int32_t drc_column_width = std::max(7, max_group_length - 13);
  report_file << std::setw(7) << "CAP" << std::setw(8) << "FANOUT" << std::setw(8) << "TRAN" << std::setw(drc_column_width + 1) << "TDRC" << std::setw(11)
              << "CELLA" << std::setw(8) << "BUFS" << std::setw(10) << "LEAFS" << std::setw(12) << "TNETS" << std::setw(11) << "CTBUF" << std::setw(8) << "REGS"
              << "\n";
  report_file << bar << "\n";
  report_file << std::setw(7) << capacitance_violations << std::setw(8) << fanout_violations << std::setw(8) << transition_violations
              << std::setw(drc_column_width + 1) << (fanout_violations + capacitance_violations + transition_violations) << std::setw(11) << cell_area
              << getQorNilString(7) << "K" << getQorKString(leaf_cell_k, 10) << getQorNilString(11) << "K" << getQorNilString(11) << getQorNilString(8) << "\n";
  report_file << bar << "\n";
  report_file << "\n";
  report_file << "NVP    - No. of Violating Paths\n";
  report_file << "FREQ   - Estimated Frequency, not accurate in some cases, multi/half-cycle, etc\n";
  report_file << "WNS(H) - Hold WNS\n";
  report_file << "TNS(H) - Hold TNS\n";
  report_file << "NVP(H) - Hold NVP\n";
  STAUTIL.finishOutputFile(report_file);
  if (_config.output_timing_features != 0) {
    output_qor_summary_json();
  }
}

std::string QorReportWriter::getQorSummaryReportFilePath()
{
  return STAUTIL.getString(_config.tr_temp_directory_path, "qor_summary.rpt");
}

std::string QorReportWriter::getQorSummaryJsonFilePath()
{
  return STAUTIL.getString(_config.tr_temp_directory_path, "qor_summary.json");
}

std::string QorReportWriter::getQorDoubleString(double value, int32_t width, int32_t precision)
{
  if (std::fabs(value) < ista::kTimingTolerance) {
    value = 0.0;
  }
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(precision) << std::setw(width) << value;
  return oss.str();
}

std::string QorReportWriter::getQorIntString(int32_t value, int32_t width)
{
  std::ostringstream oss;
  oss << std::setw(width) << value;
  return oss.str();
}

std::string QorReportWriter::getQorNilString(int32_t width)
{
  std::ostringstream oss;
  oss << std::setw(width) << "~";
  return oss.str();
}

std::string QorReportWriter::getQorFrequencyString(double frequency)
{
  if (frequency <= ista::kTimingTolerance) {
    return getQorNilString(10);
  }
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(0) << std::setw(7) << frequency << "MHz";
  return oss.str();
}

std::string QorReportWriter::getQorKString(int32_t value, int32_t width)
{
  std::ostringstream oss;
  oss << std::setw(width - 1) << value << "K";
  return oss.str();
}
}  // namespace ista
