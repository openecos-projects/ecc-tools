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
#include "TimingReporter.hpp"

#include "DataManager.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

#include <cstdint>

namespace ista {

namespace {

std::string escapeJsonString(const std::string& value)
{
  static constexpr char kHexDigits[] = "0123456789abcdef";

  std::string escaped;
  escaped.reserve(value.size());
  for (unsigned char character : value) {
    switch (character) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\b':
        escaped += "\\b";
        break;
      case '\f':
        escaped += "\\f";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (character < 0x20) {
          escaped += "\\u00";
          escaped += kHexDigits[character >> 4];
          escaped += kHexDigits[character & 0x0f];
        } else {
          escaped += static_cast<char>(character);
        }
    }
  }
  return escaped;
}

void outputJsonNumber(std::ofstream* json_file, double value)
{
  if (!std::isfinite(value)) {
    (*json_file) << "null";
    return;
  }
  if (std::fabs(value) < STA_ERROR) {
    value = 0.0;
  }
  (*json_file) << std::fixed << std::setprecision(10) << value;
}

std::string stableTimingPathId(const std::string& value)
{
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char character : value) {
    hash ^= character;
    hash *= 1099511628211ULL;
  }
  std::ostringstream stream;
  stream << "timing_path_" << std::hex << hash;
  return stream.str();
}

std::string normalizeTimingReportOption(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  std::replace(value.begin(), value.end(), '-', '_');
  return value;
}

std::vector<DelayType> getReportDelayTypeList()
{
  const std::string delay_type = normalizeTimingReportOption(STADM.getConfig().timing_report_delay_type);
  if (delay_type == "max" || delay_type == "setup" || delay_type.empty()) {
    return {DelayType::kMax};
  }
  if (delay_type == "min" || delay_type == "hold") {
    return {DelayType::kMin};
  }
  if (delay_type == "max_min" || delay_type == "min_max" || delay_type == "all") {
    return {DelayType::kMax, DelayType::kMin};
  }

  STALOG.warn(Loc::current(), "Unrecognized timing report delay_type='", STADM.getConfig().timing_report_delay_type,
              "', use default 'max'.");
  return {DelayType::kMax};
}

std::vector<StartEndType> getReportStartEndTypeList()
{
  const std::string start_end_type = normalizeTimingReportOption(STADM.getConfig().timing_report_start_end_type);
  if (start_end_type.empty() || start_end_type == "all" || start_end_type == "none") {
    return {StartEndType::kNone};
  }
  if (start_end_type == "all_separate" || start_end_type == "separate") {
    return {StartEndType::kInToOut, StartEndType::kInToReg, StartEndType::kRegToOut, StartEndType::kRegToReg};
  }
  if (start_end_type == "in_to_out" || start_end_type == "in2out") {
    return {StartEndType::kInToOut};
  }
  if (start_end_type == "in_to_reg" || start_end_type == "in2reg") {
    return {StartEndType::kInToReg};
  }
  if (start_end_type == "reg_to_out" || start_end_type == "reg2out") {
    return {StartEndType::kRegToOut};
  }
  if (start_end_type == "reg_to_reg" || start_end_type == "reg2reg") {
    return {StartEndType::kRegToReg};
  }

  STALOG.warn(Loc::current(), "Unrecognized timing report start_end_type='",
              STADM.getConfig().timing_report_start_end_type, "', use default 'all'.");
  return {StartEndType::kNone};
}

bool hasImplicitReportSlackLesserThan()
{
  return !STADM.getConfig().has_timing_report_slack_lesser_than && !STADM.getConfig().has_timing_report_slack_greater_than
         && (STADM.getConfig().path_report_number > 1 || STADM.getConfig().endpoint_path_report_number > 1);
}

}  // namespace

// public

void TimingReporter::initInst()
{
  if (_tr_instance == nullptr) {
    _tr_instance = new TimingReporter();
  }
}

TimingReporter& TimingReporter::getInst()
{
  if (_tr_instance == nullptr) {
    STALOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_tr_instance;
}

void TimingReporter::destroyInst()
{
  if (_tr_instance != nullptr) {
    delete _tr_instance;
    _tr_instance = nullptr;
  }
}

// function

void TimingReporter::report()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  reportTiming();

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

TimingReporter* TimingReporter::_tr_instance = nullptr;

void TimingReporter::reportTiming()
{
  outputTimingReportList();
}

void TimingReporter::outputTimingReportList()
{
  const bool output_reports = STADM.getConfig().output_timing_reports != 0;
  const bool output_features = STADM.getConfig().output_timing_features != 0;
  if (output_reports) {
    for (DelayType delay_type : getReportDelayTypeList()) {
      for (StartEndType start_end_type : getReportStartEndTypeList()) {
        outputTimingReport(delay_type, start_end_type);
      }
    }
  }
  if (output_reports || output_features) {
    outputQorSummaryReport();
  }
  if (output_features) {
    outputTimingPathsJson();
  }
}

void TimingReporter::outputTimingReport(DelayType delay_type, StartEndType start_end_type)
{
  std::string report_file_path = getReportFilePath(delay_type, start_end_type);
  std::ofstream* report_file = STAUTIL.getOutputFileStream(report_file_path);
  outputReportHeader(report_file, delay_type, start_end_type);
  outputPathGroupList(report_file, delay_type, start_end_type);
  outputReportFooter(report_file);
  STAUTIL.closeFileStream(report_file);
}

std::string TimingReporter::getReportFilePath(DelayType delay_type, StartEndType start_end_type)
{
  if (start_end_type == StartEndType::kNone) {
    return STAUTIL.getString(STADM.getConfig().tr_temp_directory_path, "timing_", GetDelayTypeName()(delay_type), ".rpt");
  }
  return STAUTIL.getString(STADM.getConfig().tr_temp_directory_path, "timing_", GetDelayTypeName()(delay_type), "_",
                           GetStartEndTypeReportName()(start_end_type), ".rpt");
}

void TimingReporter::outputReportHeader(std::ofstream* report_file, DelayType delay_type, StartEndType start_end_type)
{
  Database& database = STADM.getDatabase();
  (*report_file) << "****************************************\n";
  (*report_file) << "Design : " << database.get_design_name() << "\n";
  (*report_file) << "DelayType : " << GetDelayTypeName()(delay_type) << "\n";
  (*report_file) << "StartEndType : "
                 << (start_end_type == StartEndType::kNone ? "all" : GetStartEndTypeName()(start_end_type)) << "\n";
  (*report_file) << "SlackLesserThan : ";
  if (STADM.getConfig().has_timing_report_slack_lesser_than) {
    (*report_file) << getNumberString(STADM.getConfig().timing_report_slack_lesser_than);
  } else if (hasImplicitReportSlackLesserThan()) {
    (*report_file) << getNumberString(0.0);
  } else {
    (*report_file) << "infinity";
  }
  (*report_file) << "\n";
  (*report_file) << "SlackGreaterThan : ";
  if (STADM.getConfig().has_timing_report_slack_greater_than) {
    (*report_file) << getNumberString(STADM.getConfig().timing_report_slack_greater_than);
  } else {
    (*report_file) << "-infinity";
  }
  (*report_file) << "\n";
  (*report_file) << "Nworst : " << STADM.getConfig().endpoint_path_report_number << "\n";
  (*report_file) << "MaxPaths : " << STADM.getConfig().path_report_number << "\n";
  (*report_file) << "SortBy : slack\n";
  (*report_file) << "****************************************\n\n";
}

void TimingReporter::outputPathGroupList(std::ofstream* report_file, DelayType delay_type, StartEndType start_end_type)
{
  std::vector<std::pair<std::string, TimingPath*>> timing_path_list = getReportTimingPathList(delay_type, start_end_type);
  if (timing_path_list.empty()) {
    (*report_file) << "No constrained paths.\n\n";
    return;
  }
  for (std::pair<std::string, TimingPath*>& timing_path_pair : timing_path_list) {
    outputTimingPath(report_file, *timing_path_pair.second, timing_path_pair.first, delay_type);
  }
}

void TimingReporter::outputReportFooter(std::ofstream* report_file)
{
  (*report_file) << "1\n";
}

std::vector<std::pair<std::string, TimingPath*>> TimingReporter::getReportTimingPathList(DelayType delay_type,
                                                                                        StartEndType start_end_type)
{
  int32_t path_report_number = STADM.getConfig().path_report_number;
  std::vector<std::pair<std::string, TimingPath*>> sorted_timing_path_list = getSortedReportTimingPathList(delay_type, start_end_type);
  std::vector<std::pair<std::string, TimingPath*>> report_timing_path_list;
  for (std::pair<std::string, TimingPath*>& timing_path_pair : sorted_timing_path_list) {
    if (static_cast<int32_t>(report_timing_path_list.size()) >= path_report_number) {
      break;
    }
    report_timing_path_list.push_back(timing_path_pair);
  }
  return report_timing_path_list;
}

std::vector<std::pair<std::string, TimingPath*>> TimingReporter::getSortedReportTimingPathList(DelayType delay_type,
                                                                                               StartEndType start_end_type)
{
  Database& database = STADM.getDatabase();
  std::vector<std::pair<std::string, TimingPath*>> timing_path_list;
  for (TimingPathGroup& timing_path_group : database.get_timing_path_group_list()) {
    std::string& group_name = timing_path_group.get_group_name();
    for (auto& [end_point, timing_path_end] : timing_path_group.get_timing_path_end_map()) {
      for (TimingPath& timing_path : timing_path_end.get_timing_path_list()) {
        if (isMatchAnalysisType(timing_path, delay_type) && isMatchStartEndType(timing_path, start_end_type)
            && isMatchReportSlack(timing_path)) {
          timing_path_list.emplace_back(group_name, &timing_path);
        }
      }
    }
  }

  std::sort(timing_path_list.begin(), timing_path_list.end(),
            [](const std::pair<std::string, TimingPath*>& left, const std::pair<std::string, TimingPath*>& right) {
              if (std::fabs(left.second->get_slack() - right.second->get_slack()) > STA_ERROR) {
                return left.second->get_slack() < right.second->get_slack();
              }
              if (left.first != right.first) {
                return left.first < right.first;
              }
              if (left.second->get_start_point() != right.second->get_start_point()) {
                return left.second->get_start_point() < right.second->get_start_point();
              }
              return left.second->get_end_point() < right.second->get_end_point();
            });

  std::map<std::string, int32_t> endpoint_path_count_map;
  std::vector<std::pair<std::string, TimingPath*>> endpoint_limited_timing_path_list;
  for (std::pair<std::string, TimingPath*>& timing_path_pair : timing_path_list) {
    std::string& end_point = timing_path_pair.second->get_end_point();
    if (endpoint_path_count_map[end_point] >= STADM.getConfig().endpoint_path_report_number) {
      continue;
    }
    endpoint_path_count_map[end_point]++;
    endpoint_limited_timing_path_list.push_back(timing_path_pair);
  }
  return endpoint_limited_timing_path_list;
}

std::vector<TimingPath*> TimingReporter::getSortedTimingPathList(TimingPathGroup& timing_path_group, DelayType delay_type,
                                                                 StartEndType start_end_type)
{
  std::vector<TimingPath*> timing_path_list;
  for (auto& [end_point, timing_path_end] : timing_path_group.get_timing_path_end_map()) {
    for (TimingPath& timing_path : timing_path_end.get_timing_path_list()) {
      if (isMatchAnalysisType(timing_path, delay_type) && isMatchStartEndType(timing_path, start_end_type)) {
        timing_path_list.push_back(&timing_path);
      }
    }
  }
  timing_path_list = getEndpointWorstTimingPathList(timing_path_list);
  std::sort(timing_path_list.begin(), timing_path_list.end(), [](TimingPath* left, TimingPath* right) { return left->get_slack() < right->get_slack(); });
  return timing_path_list;
}

std::vector<TimingPath*> TimingReporter::getEndpointWorstTimingPathList(std::vector<TimingPath*>& timing_path_list)
{
  std::map<std::string, TimingPath*> end_point_timing_path_map;
  for (TimingPath* timing_path : timing_path_list) {
    std::string& end_point = timing_path->get_end_point();
    if (end_point_timing_path_map.count(end_point) == 0 || timing_path->get_slack() < end_point_timing_path_map[end_point]->get_slack()) {
      end_point_timing_path_map[end_point] = timing_path;
    }
  }

  std::vector<TimingPath*> endpoint_worst_timing_path_list;
  for (std::pair<const std::string, TimingPath*>& timing_path_pair : end_point_timing_path_map) {
    endpoint_worst_timing_path_list.push_back(timing_path_pair.second);
  }
  return endpoint_worst_timing_path_list;
}

bool TimingReporter::isMatchReportSlack(TimingPath& timing_path)
{
  double slack = timing_path.get_slack();
  if (STADM.getConfig().has_timing_report_slack_lesser_than && !(slack < STADM.getConfig().timing_report_slack_lesser_than)) {
    return false;
  }
  if (STADM.getConfig().has_timing_report_slack_greater_than && !(slack > STADM.getConfig().timing_report_slack_greater_than)) {
    return false;
  }
  if (hasImplicitReportSlackLesserThan() && !(slack < 0.0)) {
    return false;
  }
  return true;
}

void TimingReporter::outputQorSummaryReport()
{
  Database& database = STADM.getDatabase();
  std::map<std::string, double> setup_wns_map;
  std::map<std::string, double> setup_tns_map;
  std::map<std::string, int32_t> setup_nvp_map;
  std::map<std::string, double> setup_frequency_map;
  std::map<std::string, double> hold_wns_map;
  std::map<std::string, double> hold_tns_map;
  std::map<std::string, int32_t> hold_nvp_map;

  for (TimingPathGroup& timing_path_group : database.get_timing_path_group_list()) {
    std::string& group_name = timing_path_group.get_group_name();
    std::vector<TimingPath*> setup_timing_path_list = getQorTimingPathList(timing_path_group, DelayType::kMax);
    if (!setup_timing_path_list.empty()) {
      setup_wns_map[group_name] = setup_timing_path_list.front()->get_slack();
      setup_tns_map[group_name] = 0.0;
      setup_nvp_map[group_name] = 0;
      setup_frequency_map[group_name] = getQorFrequency(*setup_timing_path_list.front());
      for (TimingPath* timing_path : setup_timing_path_list) {
        if (timing_path->get_slack() < 0.0) {
          setup_tns_map[group_name] += timing_path->get_slack();
          setup_nvp_map[group_name]++;
        }
      }
    }

    std::vector<TimingPath*> hold_timing_path_list = getQorTimingPathList(timing_path_group, DelayType::kMin);
    if (!hold_timing_path_list.empty()) {
      hold_wns_map[group_name] = hold_timing_path_list.front()->get_slack();
      hold_tns_map[group_name] = 0.0;
      hold_nvp_map[group_name] = 0;
      for (TimingPath* timing_path : hold_timing_path_list) {
        if (timing_path->get_slack() < 0.0) {
          hold_tns_map[group_name] += timing_path->get_slack();
          hold_nvp_map[group_name]++;
        }
      }
    }
  }

  std::set<std::string> group_set;
  for (std::pair<const std::string, double>& setup_wns_pair : setup_wns_map) {
    group_set.insert(setup_wns_pair.first);
  }
  for (std::pair<const std::string, double>& hold_wns_pair : hold_wns_map) {
    group_set.insert(hold_wns_pair.first);
  }

  int32_t max_group_length = 20;
  for (std::string group_name : group_set) {
    max_group_length = std::max(max_group_length, static_cast<int32_t>(group_name.length()) + 2);
  }
  std::string bar(max_group_length + 71, '-');

  double total_setup_tns = 0.0;
  int32_t total_setup_nvp = 0;
  double total_hold_tns = 0.0;
  int32_t total_hold_nvp = 0;
  for (std::string group_name : group_set) {
    if (setup_tns_map.count(group_name) > 0) {
      total_setup_tns += setup_tns_map[group_name];
    }
    if (setup_nvp_map.count(group_name) > 0) {
      total_setup_nvp += setup_nvp_map[group_name];
    }
    if (hold_tns_map.count(group_name) > 0) {
      total_hold_tns += hold_tns_map[group_name];
    }
    if (hold_nvp_map.count(group_name) > 0) {
      total_hold_nvp += hold_nvp_map[group_name];
    }
  }

  std::string worst_setup_wns = getQorNilString(10);
  std::vector<std::string> setup_group_list = getQorSortedGroupList(setup_wns_map);
  if (!setup_group_list.empty()) {
    worst_setup_wns = getQorDoubleString(setup_wns_map[setup_group_list.front()], 10, 3);
  }
  std::string worst_hold_wns = getQorNilString(10);
  std::vector<std::string> hold_group_list = getQorSortedGroupList(hold_wns_map);
  if (!hold_group_list.empty()) {
    worst_hold_wns = getQorDoubleString(hold_wns_map[hold_group_list.front()], 10, 3);
  }

  std::string worst_frequency = getQorFrequencyString(0.0);
  if (!setup_group_list.empty() && setup_frequency_map.count(setup_group_list.front()) > 0) {
    worst_frequency = getQorFrequencyString(setup_frequency_map[setup_group_list.front()]);
  }

  const int32_t cell_area = getQorCellArea();
  const int32_t leaf_cell_k = getQorLeafCellK();
  auto output_qor_summary_json = [&]() {
    std::ofstream* json_file = STAUTIL.getOutputFileStream(getQorSummaryJsonFilePath());
    auto output_double = [json_file](double value, int32_t precision) {
      if (!std::isfinite(value)) {
        (*json_file) << "null";
        return;
      }
      if (std::fabs(value) < STA_ERROR) {
        value = 0.0;
      }
      (*json_file) << std::fixed << std::setprecision(precision) << value;
    };
    auto output_frequency = [&](double frequency) {
      if (frequency <= STA_ERROR) {
        (*json_file) << "null";
        return;
      }
      output_double(frequency, 0);
    };
    auto output_setup = [&](const std::string& group_name) {
      auto wns = setup_wns_map.find(group_name);
      if (wns == setup_wns_map.end()) {
        (*json_file) << "null";
        return;
      }
      (*json_file) << "{\"wns\":";
      output_double(wns->second, 3);
      (*json_file) << ",\"tns\":";
      output_double(setup_tns_map[group_name], 1);
      (*json_file) << ",\"nvp\":" << setup_nvp_map[group_name] << ",\"frequency_mhz\":";
      output_frequency(setup_frequency_map[group_name]);
      (*json_file) << "}";
    };
    auto output_hold = [&](const std::string& group_name) {
      auto wns = hold_wns_map.find(group_name);
      if (wns == hold_wns_map.end()) {
        (*json_file) << "null";
        return;
      }
      (*json_file) << "{\"wns\":";
      output_double(wns->second, 3);
      (*json_file) << ",\"tns\":";
      output_double(hold_tns_map[group_name], 1);
      (*json_file) << ",\"nvp\":" << hold_nvp_map[group_name] << "}";
    };
    auto output_path_group = [&](const std::string& group_name) {
      (*json_file) << "{\"name\":\"" << escapeJsonString(group_name) << "\",\"setup\":";
      output_setup(group_name);
      (*json_file) << ",\"hold\":";
      output_hold(group_name);
      (*json_file) << "}";
    };

    (*json_file) << "{\n  \"path_groups\": [";
    bool first_group = true;
    for (const std::string& group_name : setup_group_list) {
      if (!first_group) {
        (*json_file) << ",";
      }
      output_path_group(group_name);
      first_group = false;
    }
    for (const std::string& group_name : hold_group_list) {
      if (setup_wns_map.count(group_name) > 0) {
        continue;
      }
      if (!first_group) {
        (*json_file) << ",";
      }
      output_path_group(group_name);
      first_group = false;
    }
    (*json_file) << "],\n  \"summary\": {\"setup\":";
    if (setup_group_list.empty()) {
      (*json_file) << "null";
    } else {
      (*json_file) << "{\"wns\":";
      output_double(setup_wns_map[setup_group_list.front()], 3);
      (*json_file) << ",\"tns\":";
      output_double(total_setup_tns, 1);
      (*json_file) << ",\"nvp\":" << total_setup_nvp << ",\"frequency_mhz\":";
      output_frequency(setup_frequency_map[setup_group_list.front()]);
      (*json_file) << "}";
    }
    (*json_file) << ",\"hold\":";
    if (hold_group_list.empty()) {
      (*json_file) << "null";
    } else {
      (*json_file) << "{\"wns\":";
      output_double(hold_wns_map[hold_group_list.front()], 3);
      (*json_file) << ",\"tns\":";
      output_double(total_hold_tns, 1);
      (*json_file) << ",\"nvp\":" << total_hold_nvp << "}";
    }
    (*json_file) << "},\n  \"design_statistics\": {\"cap\":0,\"fanout\":0,\"tran\":0,\"tdrc\":0,\"cella\":" << cell_area
                 << ",\"bufs\":null,\"leafs_k\":" << leaf_cell_k
                 << ",\"tnets_k\":null,\"ctbuf\":null,\"regs\":null}\n}\n";
    STAUTIL.closeFileStream(json_file);
  };

  if (STADM.getConfig().output_timing_reports == 0) {
    if (STADM.getConfig().output_timing_features != 0) {
      output_qor_summary_json();
    }
    return;
  }

  std::string report_file_path = getQorSummaryReportFilePath();
  std::ofstream* report_file = STAUTIL.getOutputFileStream(report_file_path);
  if (group_set.empty()) {
    STAUTIL.closeFileStream(report_file);
    if (STADM.getConfig().output_timing_features != 0) {
      output_qor_summary_json();
    }
    return;
  }
  (*report_file) << std::left << std::setw(max_group_length) << "Path Group" << std::right << std::setw(11) << "WNS" << std::setw(11) << "TNS"
                 << std::setw(8) << "NVP" << std::setw(10) << "FREQ" << "    " << std::setw(8) << "WNS(H)" << std::setw(11) << "TNS(H)"
                 << std::setw(8) << "NVP(H)" << "\n";
  (*report_file) << bar << "\n";

  std::set<std::string> printed_group_set;
  for (std::string& group_name : setup_group_list) {
    (*report_file) << std::left << std::setw(max_group_length) << group_name << std::right << " "
                   << getQorDoubleString(setup_wns_map[group_name], 10, 3) << " " << getQorDoubleString(setup_tns_map[group_name], 10, 1) << " "
                   << getQorIntString(setup_nvp_map[group_name], 7) << " " << getQorFrequencyString(setup_frequency_map[group_name]) << " ";
    if (hold_wns_map.count(group_name) > 0) {
      (*report_file) << getQorDoubleString(hold_wns_map[group_name], 10, 3) << " " << getQorDoubleString(hold_tns_map[group_name], 10, 1) << " "
                     << getQorIntString(hold_nvp_map[group_name], 7);
    } else {
      (*report_file) << getQorNilString(10) << " " << getQorNilString(10) << " " << getQorNilString(7);
    }
    (*report_file) << "\n";
    printed_group_set.insert(group_name);
  }

  for (std::string& group_name : hold_group_list) {
    if (printed_group_set.count(group_name) > 0) {
      continue;
    }
    (*report_file) << std::left << std::setw(max_group_length) << group_name << std::right << " " << getQorNilString(10) << " "
                   << getQorNilString(10) << " " << getQorNilString(7) << " " << getQorNilString(10) << " "
                   << getQorDoubleString(hold_wns_map[group_name], 10, 3) << " " << getQorDoubleString(hold_tns_map[group_name], 10, 1) << " "
                   << getQorIntString(hold_nvp_map[group_name], 7) << "\n";
  }

  (*report_file) << bar << "\n";
  (*report_file) << std::left << std::setw(max_group_length) << "Summary" << std::right << " " << worst_setup_wns << " "
                 << getQorDoubleString(total_setup_tns, 10, 1) << " " << getQorIntString(total_setup_nvp, 7) << " " << worst_frequency << " "
                 << worst_hold_wns << " " << getQorDoubleString(total_hold_tns, 10, 1) << " " << getQorIntString(total_hold_nvp, 7) << "\n";
  (*report_file) << bar << "\n";
  int32_t drc_column_width = std::max(7, max_group_length - 13);
  (*report_file) << std::setw(7) << "CAP" << std::setw(8) << "FANOUT" << std::setw(8) << "TRAN" << std::setw(drc_column_width + 1) << "TDRC"
                 << std::setw(11) << "CELLA" << std::setw(8) << "BUFS" << std::setw(10) << "LEAFS" << std::setw(12) << "TNETS"
                 << std::setw(11) << "CTBUF" << std::setw(8) << "REGS" << "\n";
  (*report_file) << bar << "\n";
  (*report_file) << std::setw(7) << 0 << std::setw(8) << 0 << std::setw(8) << 0 << std::setw(drc_column_width + 1) << 0 << std::setw(11)
                 << cell_area << getQorNilString(7) << "K" << getQorKString(leaf_cell_k, 10)
                 << getQorNilString(11) << "K" << getQorNilString(11) << getQorNilString(8) << "\n";
  (*report_file) << bar << "\n";
  (*report_file) << "\n";
  (*report_file) << "NVP    - No. of Violating Paths\n";
  (*report_file) << "FREQ   - Estimated Frequency, not accurate in some cases, multi/half-cycle, etc\n";
  (*report_file) << "WNS(H) - Hold WNS\n";
  (*report_file) << "TNS(H) - Hold TNS\n";
  (*report_file) << "NVP(H) - Hold NVP\n";
  STAUTIL.closeFileStream(report_file);
  if (STADM.getConfig().output_timing_features != 0) {
    output_qor_summary_json();
  }
}

std::string TimingReporter::getQorSummaryReportFilePath()
{
  return STAUTIL.getString(STADM.getConfig().tr_temp_directory_path, "qor_summary.rpt");
}

std::string TimingReporter::getQorSummaryJsonFilePath()
{
  return STAUTIL.getString(STADM.getConfig().tr_temp_directory_path, "qor_summary.json");
}

void TimingReporter::outputTimingPathsJson()
{
  Database& database = STADM.getDatabase();
  std::ofstream* json_file = STAUTIL.getOutputFileStream(getTimingPathsJsonFilePath());
  const int32_t path_limit = std::max(STADM.getConfig().timing_path_limit, 0);
  const std::string corner = STADM.getConfig().timing_corner.empty() ? "unknown" : STADM.getConfig().timing_corner;

  (*json_file) << "{\n  \"schema_version\": 1,\n  \"corner\": \"" << escapeJsonString(corner)
               << "\",\n  \"path_limit\": " << path_limit << ",\n  \"paths\": [";
  bool first_path = true;
  for (DelayType delay_type : {DelayType::kMax, DelayType::kMin}) {
    std::vector<std::pair<std::string, TimingPath*>> timing_paths;
    for (TimingPathGroup& timing_path_group : database.get_timing_path_group_list()) {
      std::vector<TimingPath*> group_paths = getQorTimingPathList(timing_path_group, delay_type);
      for (TimingPath* timing_path : group_paths) {
        timing_paths.emplace_back(timing_path_group.get_group_name(), timing_path);
      }
    }
    std::sort(timing_paths.begin(), timing_paths.end(), [this](const auto& left, const auto& right) {
      if (std::fabs(left.second->get_slack() - right.second->get_slack()) > STA_ERROR) {
        return left.second->get_slack() < right.second->get_slack();
      }
      if (left.first != right.first) {
        return left.first < right.first;
      }
      if (left.second->get_start_point() != right.second->get_start_point()) {
        return left.second->get_start_point() < right.second->get_start_point();
      }
      if (left.second->get_end_point() != right.second->get_end_point()) {
        return left.second->get_end_point() < right.second->get_end_point();
      }
      return getClockName(*left.second) < getClockName(*right.second);
    });

    int32_t output_count = 0;
    for (const auto& [path_group_name, timing_path] : timing_paths) {
      if (output_count >= path_limit) {
        break;
      }
      if (!first_path) {
        (*json_file) << ",";
      }
      std::string group_name = path_group_name;
      outputTimingPathJson(json_file, *timing_path, group_name, delay_type);
      first_path = false;
      output_count++;
    }
  }
  (*json_file) << "]\n}\n";
  STAUTIL.closeFileStream(json_file);
}

std::string TimingReporter::getTimingPathsJsonFilePath()
{
  return STAUTIL.getString(STADM.getConfig().tr_temp_directory_path, "timing_paths.json");
}

void TimingReporter::outputTimingPathJson(std::ofstream* json_file, TimingPath& timing_path, std::string& path_group_name,
                                          DelayType delay_type)
{
  const std::string analysis_type = delay_type == DelayType::kMax ? "setup" : "hold";
  const std::string clock_name = getClockName(timing_path);
  const std::string check_type = GetTimingCheckTypeName()(timing_path.get_check_type());
  (*json_file) << "{\"path_id\":\"" << escapeJsonString(getTimingPathId(timing_path, path_group_name, delay_type))
               << "\",\"analysis_type\":\"" << analysis_type << "\",\"path_group\":\""
               << escapeJsonString(path_group_name) << "\",\"start_point\":\""
               << escapeJsonString(timing_path.get_start_point()) << "\",\"end_point\":\""
               << escapeJsonString(timing_path.get_end_point()) << "\",\"launch_clock\":\""
               << escapeJsonString(clock_name) << "\",\"capture_clock\":\"" << escapeJsonString(clock_name)
               << "\",\"check_type\":\"" << escapeJsonString(check_type) << "\",\"slack_ns\":";
  outputJsonNumber(json_file, timing_path.get_slack());
  (*json_file) << ",\"arrival_ns\":";
  outputJsonNumber(json_file, timing_path.get_path_delay());
  (*json_file) << ",\"required_ns\":";
  outputJsonNumber(json_file, timing_path.get_required_time());
  (*json_file) << ",\"cppr_ns\":";
  outputJsonNumber(json_file, timing_path.get_clock_reconvergence_pessimism());
  (*json_file) << ",\"launch_clock_network_delay_ns\":";
  outputJsonNumber(json_file, timing_path.get_launch_clock_network_delay());
  (*json_file) << ",\"capture_clock_network_delay_ns\":";
  outputJsonNumber(json_file, timing_path.get_capture_clock_network_delay());
  (*json_file) << ",\"stages\":[";
  bool first_stage = true;
  for (TimingPathPoint& path_point : timing_path.get_point_list()) {
    if (!first_stage) {
      (*json_file) << ",";
    }
    std::string kind = GetArcTypeName()(path_point.get_arc_type());
    if (kind == "none") {
      kind = "point";
    } else {
      kind += "_arc";
    }
    (*json_file) << "{\"kind\":\"" << kind << "\",\"pin\":\""
                 << escapeJsonString(path_point.get_pin_name()) << "\",\"instance\":\""
                 << escapeJsonString(path_point.get_instance_name()) << "\",\"cell\":\""
                 << escapeJsonString(path_point.get_cell_name()) << "\",\"incremental_delay_ns\":";
    outputJsonNumber(json_file, path_point.get_arc_delay());
    (*json_file) << ",\"arrival_ns\":";
    outputJsonNumber(json_file, path_point.get_arrival());
    (*json_file) << ",\"transition\":\""
                 << GetTransTypeName()(path_point.get_trans_type()) << "\"}";
    first_stage = false;
  }
  (*json_file) << "]}";
}

std::string TimingReporter::getTimingPathId(TimingPath& timing_path, std::string& path_group_name, DelayType delay_type)
{
  const std::string analysis_type = delay_type == DelayType::kMax ? "setup" : "hold";
  const std::string corner = STADM.getConfig().timing_corner.empty() ? "unknown" : STADM.getConfig().timing_corner;
  const std::string clock_name = getClockName(timing_path);
  return stableTimingPathId(
      STAUTIL.getString(corner, "|", analysis_type, "|", path_group_name, "|", timing_path.get_start_point(), "|",
                         timing_path.get_end_point(), "|", clock_name, "|", clock_name));
}

std::vector<TimingPath*> TimingReporter::getQorTimingPathList(TimingPathGroup& timing_path_group, DelayType delay_type)
{
  std::vector<TimingPath*> timing_path_list;
  for (std::pair<const std::string, TimingPathEnd>& timing_path_end_pair : timing_path_group.get_timing_path_end_map()) {
    for (TimingPath& timing_path : timing_path_end_pair.second.get_timing_path_list()) {
      if (isMatchAnalysisType(timing_path, delay_type)) {
        timing_path_list.push_back(&timing_path);
      }
    }
  }
  timing_path_list = getEndpointWorstTimingPathList(timing_path_list);
  std::sort(timing_path_list.begin(), timing_path_list.end(), [](TimingPath* left, TimingPath* right) { return left->get_slack() < right->get_slack(); });
  return timing_path_list;
}

std::vector<std::string> TimingReporter::getQorSortedGroupList(std::map<std::string, double>& value_map)
{
  std::vector<std::string> group_list;
  for (std::pair<const std::string, double>& value_pair : value_map) {
    group_list.push_back(value_pair.first);
  }
  std::sort(group_list.begin(), group_list.end(), [&value_map](std::string& left, std::string& right) {
    if (std::fabs(value_map[left] - value_map[right]) > STA_ERROR) {
      return value_map[left] < value_map[right];
    }
    return left < right;
  });
  return group_list;
}

double TimingReporter::getQorFrequency(TimingPath& timing_path)
{
  std::string clock_name = getClockName(timing_path);
  double clock_period = getClockPeriod(clock_name);
  double effective_period = clock_period - timing_path.get_slack();
  if (effective_period <= STA_ERROR) {
    return 0.0;
  }
  return 1000.0 / effective_period;
}

std::string TimingReporter::getQorDoubleString(double value, int32_t width, int32_t precision)
{
  if (std::fabs(value) < STA_ERROR) {
    value = 0.0;
  }
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(precision) << std::setw(width) << value;
  return oss.str();
}

std::string TimingReporter::getQorIntString(int32_t value, int32_t width)
{
  std::ostringstream oss;
  oss << std::setw(width) << value;
  return oss.str();
}

std::string TimingReporter::getQorNilString(int32_t width)
{
  std::ostringstream oss;
  oss << std::setw(width) << "~";
  return oss.str();
}

std::string TimingReporter::getQorFrequencyString(double frequency)
{
  if (frequency <= STA_ERROR) {
    return getQorNilString(10);
  }
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(0) << std::setw(7) << frequency << "MHz";
  return oss.str();
}

std::string TimingReporter::getQorKString(int32_t value, int32_t width)
{
  std::ostringstream oss;
  oss << std::setw(width - 1) << value << "K";
  return oss.str();
}

int32_t TimingReporter::getQorCellArea()
{
  Database& database = STADM.getDatabase();
  double cell_area = 0.0;
  for (std::pair<const std::string, Instance>& instance_pair : database.get_instance_map()) {
    Instance& instance = database.get_instance_map()[instance_pair.first];
    std::map<std::string, TimingCell>& timing_cell_map = database.get_timing_library().get_cell_map();
    if (timing_cell_map.count(instance.get_cell_name()) > 0) {
      cell_area += timing_cell_map[instance.get_cell_name()].get_area();
    }
  }
  return static_cast<int32_t>(cell_area);
}

int32_t TimingReporter::getQorLeafCellK()
{
  return static_cast<int32_t>(STADM.getDatabase().get_instance_map().size() / 1000);
}

bool TimingReporter::isMatchAnalysisType(TimingPath& timing_path, DelayType delay_type)
{
  if (delay_type == DelayType::kNone) {
    return false;
  }
  if (delay_type == DelayType::kMin) {
    return timing_path.get_analysis_type() == AnalysisType::kMin;
  }
  return timing_path.get_analysis_type() == AnalysisType::kMax;
}

bool TimingReporter::isMatchStartEndType(TimingPath& timing_path, StartEndType start_end_type)
{
  if (start_end_type == StartEndType::kNone) {
    return true;
  }
  bool start_is_port = isPort(timing_path.get_start_point());
  bool end_is_port = isPort(timing_path.get_end_point());
  if (start_end_type == StartEndType::kInToOut) {
    return start_is_port && end_is_port;
  }
  if (start_end_type == StartEndType::kInToReg) {
    return start_is_port && isRegisterEndPoint(timing_path.get_end_point());
  }
  if (start_end_type == StartEndType::kRegToOut) {
    return isRegisterStartPoint(timing_path.get_start_point()) && end_is_port;
  }
  if (start_end_type == StartEndType::kRegToReg) {
    return isRegisterStartPoint(timing_path.get_start_point()) && isRegisterEndPoint(timing_path.get_end_point());
  }
  return false;
}

bool TimingReporter::isPort(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  return database.get_pin_map()[pin_name].get_is_port();
}

bool TimingReporter::isRegisterStartPoint(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  Pin& pin = database.get_pin_map()[pin_name];
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  return instance.get_is_sequential() && pin_name == instance.get_output_pin_name() && hasClockPoint(instance.get_clock_pin_name());
}

bool TimingReporter::isRegisterEndPoint(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  Pin& pin = database.get_pin_map()[pin_name];
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  if (!instance.get_is_sequential() || !hasClockPoint(instance.get_clock_pin_name())) {
    return false;
  }
  for (TimingCheckArc& timing_check_arc : instance.get_check_arc_list()) {
    if (timing_check_arc.get_data_port() == pin_name) {
      return true;
    }
  }
  return false;
}

bool TimingReporter::hasClockPoint(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  return database.get_timing_point_map().count(pin_name) > 0 && database.get_timing_point_map()[pin_name].get_is_clock_point();
}

bool TimingReporter::isClockSourceStartPoint(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  Pin& pin = database.get_pin_map()[pin_name];
  if (!pin.get_is_port()) {
    return false;
  }
  for (std::pair<const std::string, TimingClock>& clock_pair : database.get_timing_constraint().get_clock_map()) {
    if (STAUTIL.exist(clock_pair.second.get_source_list(), pin_name)) {
      return true;
    }
  }
  return false;
}

void TimingReporter::outputTimingPath(std::ofstream* report_file, TimingPath& timing_path, std::string& path_group_name,
                                      DelayType delay_type)
{
  outputTimingPathHeader(report_file, timing_path, path_group_name, delay_type);
  std::size_t label_width = outputTimingPointList(report_file, timing_path, delay_type);
  outputTimingPathSummary(report_file, timing_path, label_width);
}

void TimingReporter::outputTimingPathHeader(std::ofstream* report_file, TimingPath& timing_path, std::string& path_group_name,
                                            DelayType delay_type)
{
  outputStartEndPoint(report_file, "Startpoint", getStartPointText(timing_path));
  outputStartEndPoint(report_file, "Endpoint", getEndPointText(timing_path));
  if (isRegisterStartPoint(timing_path.get_start_point()) && isRegisterEndPoint(timing_path.get_end_point())) {
    (*report_file) << "  Last common pin: " << getPTPinName(timing_path.get_last_common_pin()) << "\n";
  }
  (*report_file) << "  Path Group: " << path_group_name << "\n";
  (*report_file) << "  Path Type: " << GetDelayTypeName()(delay_type) << "\n\n";
}

void TimingReporter::outputStartEndPoint(std::ofstream* report_file, std::string label, std::string text)
{
  std::string name = getStartEndPointName(text);
  std::string description = getStartEndPointDescription(text);
  (*report_file) << "  " << label << ": " << name;
  if (!description.empty()) {
    if (name.length() <= 10) {
      (*report_file) << " " << description;
    } else {
      (*report_file) << "\n               " << description;
    }
  }
  (*report_file) << "\n";
}

std::string TimingReporter::getStartEndPointName(std::string& text)
{
  std::size_t description_pos = text.find(" (");
  if (description_pos == std::string::npos) {
    return text;
  }
  return text.substr(0, description_pos);
}

std::string TimingReporter::getStartEndPointDescription(std::string& text)
{
  std::size_t description_pos = text.find(" (");
  if (description_pos == std::string::npos) {
    return "";
  }
  return text.substr(description_pos + 1);
}

std::string TimingReporter::getStartPointText(TimingPath& timing_path)
{
  Database& database = STADM.getDatabase();
  Pin& start_pin = database.get_pin_map()[timing_path.get_start_point()];
  std::string clock_name = getClockName(timing_path);
  std::string start_point = getPTPinName(timing_path.get_start_point());
  if (!start_pin.get_is_port()) {
    Instance& start_instance = database.get_instance_map()[start_pin.get_instance_name()];
    if (start_instance.get_is_sequential() && isInternalStartPoint(timing_path)) {
      start_point = getPTPinName(start_instance.get_clock_pin_name());
      return STAUTIL.getString(start_point, " (internal path startpoint clocked by ", clock_name, ")");
    }
    start_point = start_pin.get_instance_name();
  }
  if (isClockSourceStartPoint(timing_path.get_start_point())) {
    return STAUTIL.getString(start_point, " (clock source '", clock_name, "')");
  }
  if (isPort(timing_path.get_start_point())) {
    return STAUTIL.getString(start_point, " (input port clocked by ", clock_name, ")");
  }
  return STAUTIL.getString(start_point, " (rising edge-triggered flip-flop clocked by ", clock_name, ")");
}

bool TimingReporter::isInternalStartPoint(TimingPath& timing_path)
{
  Database& database = STADM.getDatabase();
  Pin& start_pin = database.get_pin_map()[timing_path.get_start_point()];
  Instance& start_instance = database.get_instance_map()[start_pin.get_instance_name()];
  return timing_path.get_start_point() == start_instance.get_clock_pin_name()
         || (timing_path.get_start_point() == start_instance.get_output_pin_name() && isTieDrivenConstantOutput(start_instance));
}

bool TimingReporter::isTieDrivenConstantOutput(Instance& instance)
{
  std::optional<bool> data_value = getTieDriverValue(instance.get_data_pin_name());
  if (!data_value.has_value()) {
    return false;
  }
  if (instance.get_has_clear_arc() && *data_value) {
    return false;
  }
  if (instance.get_has_preset_arc() && !*data_value) {
    return false;
  }
  return true;
}

std::optional<bool> TimingReporter::getTieDriverValue(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  Pin& pin = database.get_pin_map()[pin_name];
  Net& net = database.get_net_map()[pin.get_net_name()];
  for (std::string& driver_pin_name : net.get_driver_pin_list()) {
    Pin& driver_pin = database.get_pin_map()[driver_pin_name];
    if (driver_pin.get_is_port()) {
      continue;
    }
    Instance& driver_instance = database.get_instance_map()[driver_pin.get_instance_name()];
    if (isTieHighCell(driver_instance)) {
      return true;
    }
    if (isTieLowCell(driver_instance)) {
      return false;
    }
  }
  return std::nullopt;
}

bool TimingReporter::isTieHighCell(Instance& instance)
{
  std::string& cell_name = instance.get_cell_name();
  return cell_name.rfind("TIEHI", 0) == 0;
}

bool TimingReporter::isTieLowCell(Instance& instance)
{
  std::string& cell_name = instance.get_cell_name();
  return cell_name.rfind("TIELO", 0) == 0;
}

std::string TimingReporter::getEndPointText(TimingPath& timing_path)
{
  Database& database = STADM.getDatabase();
  Pin& end_pin = database.get_pin_map()[timing_path.get_end_point()];
  std::string clock_name = getClockName(timing_path);
  std::string end_point = getPTPinName(timing_path.get_end_point());
  if (!end_pin.get_is_port()) {
    end_point = end_pin.get_instance_name();
  }
  if (isPort(timing_path.get_end_point())) {
    return STAUTIL.getString(end_point, " (output port clocked by ", clock_name, ")");
  }
  return getEndPointCheckText(end_point, clock_name, timing_path);
}

std::string TimingReporter::getEndPointCheckText(std::string& end_point, std::string& clock_name, TimingPath& timing_path)
{
  if (timing_path.get_check_type() == TimingCheckType::kRecovery) {
    return STAUTIL.getString(end_point, " (recovery check against rising-edge clock ", clock_name, ")");
  }
  if (timing_path.get_check_type() == TimingCheckType::kRemoval) {
    return STAUTIL.getString(end_point, " (removal check against rising-edge clock ", clock_name, ")");
  }
  return STAUTIL.getString(end_point, " (rising edge-triggered flip-flop clocked by ", clock_name, ")");
}

std::size_t TimingReporter::outputTimingPointList(std::ofstream* report_file, TimingPath& timing_path, DelayType delay_type)
{
  std::size_t label_width = getTimingLineLabelWidth(timing_path, delay_type);
  outputTimingPointHeader(report_file, label_width);
  (*report_file) << "  " << std::string(label_width + 28, '-') << "\n";
  outputLaunchClockInfo(report_file, timing_path, delay_type, label_width);
  bool is_first_point = true;
  for (TimingPathPoint& path_point : timing_path.get_point_list()) {
    if (shouldOutputTimingPoint(timing_path, path_point)) {
      outputTimingPoint(report_file, timing_path, path_point, is_first_point, label_width);
      is_first_point = false;
    }
  }
  outputTimingSummaryLine(report_file, "data arrival time", timing_path.get_path_delay(), label_width);
  (*report_file) << "\n";
  outputRequiredClockInfo(report_file, timing_path, delay_type, label_width);
  return label_width;
}

std::size_t TimingReporter::getTimingLineLabelWidth(TimingPath& timing_path, DelayType delay_type)
{
  Database& database = STADM.getDatabase();
  std::size_t label_width = 35;
  std::string clock_name = getClockName(timing_path);
  updateTimingLineLabelWidth(label_width, STAUTIL.getString("clock ", clock_name, " (rise edge)"));
  updateTimingLineLabelWidth(label_width, "clock network delay (propagated)");

  std::string start_clock_pin = getStartClockPin(timing_path);
  if (!start_clock_pin.empty() && start_clock_pin != timing_path.get_start_point()) {
    updateTimingLineLabelWidth(label_width, getPinLabel(start_clock_pin));
  }

  std::map<std::string, TimingPortConstraint>& port_constraint_map = database.get_timing_constraint().get_port_constraint_map();
  bool has_input_delay = port_constraint_map.count(timing_path.get_start_point()) > 0
                         && (port_constraint_map[timing_path.get_start_point()].get_has_input_delay_max()
                             || port_constraint_map[timing_path.get_start_point()].get_has_input_delay_min());
  if (isPort(timing_path.get_start_point()) && has_input_delay) {
    updateTimingLineLabelWidth(label_width, "input external delay");
  }

  for (TimingPathPoint& path_point : timing_path.get_point_list()) {
    if (shouldOutputTimingPoint(timing_path, path_point)) {
      updateTimingLineLabelWidth(label_width, getPointLabel(path_point));
    }
  }

  updateTimingLineLabelWidth(label_width, "clock reconvergence pessimism");
  if (!timing_path.get_capture_clock_pin().empty()) {
    updateTimingLineLabelWidth(label_width, getPinLabel(timing_path.get_capture_clock_pin()));
  }
  if (std::fabs(timing_path.get_check_time()) > STA_ERROR) {
    updateTimingLineLabelWidth(label_width, getLibraryCheckText(timing_path, delay_type));
  } else if (isPort(timing_path.get_end_point())) {
    bool has_output_delay = port_constraint_map.count(timing_path.get_end_point()) > 0
                            && (port_constraint_map[timing_path.get_end_point()].get_has_output_delay_max()
                                || port_constraint_map[timing_path.get_end_point()].get_has_output_delay_min());
    if (has_output_delay) {
      updateTimingLineLabelWidth(label_width, "output external delay");
    }
  }
  return label_width;
}

bool TimingReporter::shouldOutputTimingPoint(TimingPath& timing_path, TimingPathPoint& path_point)
{
  Database& database = STADM.getDatabase();
  Pin& pin = database.get_pin_map()[path_point.get_pin_name()];
  if (pin.get_is_port()) {
    return true;
  }
  if (path_point.get_pin_name() == timing_path.get_start_point() || path_point.get_pin_name() == timing_path.get_end_point()) {
    return true;
  }
  return pin.get_direction() == PinDirection::kOutput || pin.get_direction() == PinDirection::kInout;
}

void TimingReporter::updateTimingLineLabelWidth(std::size_t& label_width, std::string label)
{
  label_width = std::max(label_width, label.length());
}

void TimingReporter::outputTimingPointHeader(std::ofstream* report_file, std::size_t label_width)
{
  (*report_file) << "  " << std::left << std::setw(label_width) << "Point" << std::right << std::setw(10) << "Incr" << std::setw(11) << "Path"
                 << "\n";
}

void TimingReporter::outputLaunchClockInfo(std::ofstream* report_file, TimingPath& timing_path, DelayType delay_type,
                                           std::size_t label_width)
{
  Database& database = STADM.getDatabase();
  std::string clock_name = getClockName(timing_path);
  double launch_time = timing_path.get_launch_time();
  double launch_clock_network_delay = timing_path.get_launch_clock_network_delay();
  double launch_clock_edge = isClockSourceStartPoint(timing_path.get_start_point()) ? launch_time : 0.0;
  outputTimingLine(report_file, STAUTIL.getString("clock ", clock_name, " (", getLaunchClockEdgeText(timing_path, delay_type), " edge)"),
                   launch_clock_edge, launch_clock_edge, true, "", label_width);
  if (isClockSourceStartPoint(timing_path.get_start_point())) {
    outputTimingLine(report_file, "clock source latency", 0.0, launch_time, true, "", label_width);
  } else {
    outputTimingLine(report_file, "clock network delay (propagated)", launch_clock_network_delay, launch_time, true, "", label_width);
  }

  std::string start_clock_pin = getStartClockPin(timing_path);
  if (!start_clock_pin.empty() && start_clock_pin != timing_path.get_start_point()) {
    outputTimingLine(report_file, getPinLabel(start_clock_pin), 0.0, launch_time, true, "r", label_width);
  }

  double input_delay = getInputDelay(timing_path, delay_type);
  std::map<std::string, TimingPortConstraint>& port_constraint_map = database.get_timing_constraint().get_port_constraint_map();
  bool has_input_delay = port_constraint_map.count(timing_path.get_start_point()) > 0
                         && (port_constraint_map[timing_path.get_start_point()].get_has_input_delay_max()
                             || port_constraint_map[timing_path.get_start_point()].get_has_input_delay_min());
  if (isPort(timing_path.get_start_point()) && has_input_delay) {
    outputTimingLine(report_file, "input external delay", input_delay, launch_time + input_delay, true, "r", label_width);
  }
}

std::string TimingReporter::getLaunchClockEdgeText(TimingPath& timing_path, DelayType delay_type)
{
  if (isClockSourceStartPoint(timing_path.get_start_point()) && delay_type == DelayType::kMax && timing_path.get_trans_type() == TransType::kFall) {
    return GetTransTypeName()(TransType::kFall);
  }
  return GetTransTypeName()(TransType::kRise);
}

void TimingReporter::outputTimingLine(std::ofstream* report_file, std::string label, double incr, double path, bool has_incr, std::string transition,
                                      std::size_t label_width)
{
  if (has_incr) {
    (*report_file) << "  " << std::left << std::setw(label_width + 2) << label << getNumberString(incr) << "\n";
    (*report_file) << "  " << std::setw(label_width + 13) << "" << getNumberString(path);
  } else {
    (*report_file) << "  " << std::left << std::setw(label_width + 13) << label << getNumberString(path);
  }
  if (!transition.empty()) {
    (*report_file) << " " << transition;
  }
  (*report_file) << "\n";
}

void TimingReporter::outputTimingSummaryLine(std::ofstream* report_file, std::string label, double value, std::size_t label_width)
{
  (*report_file) << "  " << std::left << std::setw(label_width + 13) << label << getNumberString(value) << "\n";
}

std::string TimingReporter::getClockName(TimingPath& timing_path)
{
  Database& database = STADM.getDatabase();
  if (!timing_path.get_clock_name().empty()) {
    return timing_path.get_clock_name();
  }
  std::map<std::string, TimingClock>& clock_map = database.get_timing_constraint().get_clock_map();
  if (!clock_map.empty()) {
    return clock_map.begin()->first;
  }
  return "clk";
}

double TimingReporter::getClockPeriod(std::string& clock_name)
{
  Database& database = STADM.getDatabase();
  std::map<std::string, TimingClock>& clock_map = database.get_timing_constraint().get_clock_map();
  if (clock_map.count(clock_name) > 0) {
    return clock_map[clock_name].get_period();
  }
  if (!clock_map.empty()) {
    return clock_map.begin()->second.get_period();
  }
  return 0.0;
}

double TimingReporter::getClockUncertainty(std::string& clock_name, DelayType delay_type)
{
  Database& database = STADM.getDatabase();
  auto& clock_map = database.get_timing_constraint().get_clock_map();
  auto clock_it = clock_map.find(clock_name);
  if (clock_it == clock_map.end()) {
    return 0.0;
  }
  return delay_type == DelayType::kMin ? clock_it->second.get_hold_uncertainty() : clock_it->second.get_setup_uncertainty();
}

double TimingReporter::getInputDelay(TimingPath& timing_path, DelayType delay_type)
{
  Database& database = STADM.getDatabase();
  std::map<std::string, TimingPortConstraint>& port_constraint_map = database.get_timing_constraint().get_port_constraint_map();
  if (port_constraint_map.count(timing_path.get_start_point()) == 0) {
    return 0.0;
  }
  TimingPortConstraint& port_constraint = port_constraint_map[timing_path.get_start_point()];
  if (delay_type == DelayType::kMin && port_constraint.get_has_input_delay_min()) {
    return port_constraint.get_input_delay_min();
  }
  if (port_constraint.get_has_input_delay_max()) {
    return port_constraint.get_input_delay_max();
  }
  return 0.0;
}

std::string TimingReporter::getStartClockPin(TimingPath& timing_path)
{
  Database& database = STADM.getDatabase();
  Pin& start_pin = database.get_pin_map()[timing_path.get_start_point()];
  if (start_pin.get_is_port() || database.get_instance_map().count(start_pin.get_instance_name()) == 0) {
    return "";
  }
  return database.get_instance_map()[start_pin.get_instance_name()].get_clock_pin_name();
}

void TimingReporter::outputTimingPoint(std::ofstream* report_file, TimingPath& timing_path, TimingPathPoint& path_point,
                                       bool is_first_point, std::size_t label_width)
{
  Database& database = STADM.getDatabase();
  double arc_delay = path_point.get_arc_delay();
  if (is_first_point && !database.get_pin_map()[path_point.get_pin_name()].get_is_port()) {
    arc_delay = path_point.get_arrival() - timing_path.get_launch_time();
  }
  outputTimingLine(report_file, getPointLabel(path_point), arc_delay, path_point.get_arrival(), true, GetTransTypeInitial()(path_point.get_trans_type()),
                   label_width);
}

std::string TimingReporter::getNumberString(double value)
{
  std::ostringstream oss;
  if (std::fabs(value) < STA_ERROR) {
    value = 0.0;
  }
  oss << std::fixed << std::setprecision(10) << value;
  return oss.str();
}

std::string TimingReporter::getPointLabel(TimingPathPoint& path_point)
{
  Database& database = STADM.getDatabase();
  Pin& pin = database.get_pin_map()[path_point.get_pin_name()];
  if (pin.get_is_port()) {
    if (pin.get_direction() == PinDirection::kInput) {
      return STAUTIL.getString(getPTPinName(path_point.get_pin_name()), " (in)");
    }
    if (pin.get_direction() == PinDirection::kOutput) {
      return STAUTIL.getString(getPTPinName(path_point.get_pin_name()), " (out)");
    }
  }
  std::string point_label = getPTPinName(path_point.get_pin_name());
  std::string cell_name = getPTCellName(path_point);
  if (!cell_name.empty()) {
    point_label = STAUTIL.getString(point_label, " (", cell_name, ")");
  }
  return point_label;
}

std::string TimingReporter::getPTPinName(std::string& pin_name)
{
  std::string pt_pin_name = pin_name;
  std::replace(pt_pin_name.begin(), pt_pin_name.end(), ':', '/');
  return pt_pin_name;
}

std::string TimingReporter::getPTCellName(TimingPathPoint& path_point)
{
  return path_point.get_cell_name();
}

void TimingReporter::outputRequiredClockInfo(std::ofstream* report_file, TimingPath& timing_path, DelayType delay_type,
                                             std::size_t label_width)
{
  Database& database = STADM.getDatabase();
  std::string clock_name = getClockName(timing_path);
  double capture_time = timing_path.get_capture_time();
  double clock_edge = delay_type == DelayType::kMin ? 0.0 : getClockPeriod(clock_name);
  double capture_clock_network_delay = timing_path.get_capture_clock_network_delay();
  outputTimingLine(report_file, STAUTIL.getString("clock ", clock_name, " (rise edge)"), clock_edge, clock_edge, true, "", label_width);
  outputTimingLine(report_file, "clock network delay (propagated)", capture_clock_network_delay, clock_edge + capture_clock_network_delay, true, "",
                   label_width);
  outputTimingLine(report_file, "clock reconvergence pessimism", timing_path.get_clock_reconvergence_pessimism(), capture_time, true, "", label_width);
  if (!timing_path.get_capture_clock_pin().empty()) {
    outputTimingLine(report_file, getPinLabel(timing_path.get_capture_clock_pin()), 0.0, capture_time, false, "r", label_width);
  }
  double uncertainty = getClockUncertainty(clock_name, delay_type);
  if (uncertainty > STA_ERROR) {
    double signed_uncertainty = delay_type == DelayType::kMax ? -uncertainty : uncertainty;
    double required_before_check = timing_path.get_required_time();
    if (std::fabs(timing_path.get_check_time()) > STA_ERROR) {
      required_before_check -= delay_type == DelayType::kMax ? -timing_path.get_check_time() : timing_path.get_check_time();
    }
    outputTimingLine(report_file, "clock uncertainty", signed_uncertainty, required_before_check, true, "", label_width);
  }
  if (std::fabs(timing_path.get_check_time()) > STA_ERROR) {
    double check_time = timing_path.get_check_time();
    if (delay_type == DelayType::kMax) {
      check_time = -check_time;
    }
    outputTimingLine(report_file, getLibraryCheckText(timing_path, delay_type), check_time, timing_path.get_required_time(), true, "", label_width);
  } else if (isPort(timing_path.get_end_point())) {
    double output_delay = getOutputDelay(timing_path, delay_type);
    std::map<std::string, TimingPortConstraint>& port_constraint_map = database.get_timing_constraint().get_port_constraint_map();
    bool has_output_delay = port_constraint_map.count(timing_path.get_end_point()) > 0
                            && (port_constraint_map[timing_path.get_end_point()].get_has_output_delay_max()
                                || port_constraint_map[timing_path.get_end_point()].get_has_output_delay_min());
    if (has_output_delay) {
      outputTimingLine(report_file, "output external delay", output_delay, timing_path.get_required_time(), true, "", label_width);
    }
  }
  outputTimingSummaryLine(report_file, "data required time", timing_path.get_required_time(), label_width);
}

std::string TimingReporter::getLibraryCheckText(TimingPath& timing_path, DelayType delay_type)
{
  if (timing_path.get_check_type() == TimingCheckType::kRecovery) {
    return "library recovery time";
  }
  if (timing_path.get_check_type() == TimingCheckType::kRemoval) {
    return "library removal time";
  }
  if (delay_type == DelayType::kMin) {
    return "library hold time";
  }
  return "library setup time";
}

double TimingReporter::getOutputDelay(TimingPath& timing_path, DelayType delay_type)
{
  Database& database = STADM.getDatabase();
  std::map<std::string, TimingPortConstraint>& port_constraint_map = database.get_timing_constraint().get_port_constraint_map();
  if (port_constraint_map.count(timing_path.get_end_point()) == 0) {
    return 0.0;
  }
  TimingPortConstraint& port_constraint = port_constraint_map[timing_path.get_end_point()];
  if (delay_type == DelayType::kMin && port_constraint.get_has_output_delay_min()) {
    return port_constraint.get_output_delay_min();
  }
  if (port_constraint.get_has_output_delay_max()) {
    return port_constraint.get_output_delay_max();
  }
  return 0.0;
}

std::string TimingReporter::getPinLabel(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  std::string point_label = getPTPinName(pin_name);
  Pin& pin = database.get_pin_map()[pin_name];
  if (!pin.get_instance_name().empty() && database.get_instance_map().count(pin.get_instance_name()) > 0) {
    point_label = STAUTIL.getString(point_label, " (", database.get_instance_map()[pin.get_instance_name()].get_cell_name(), ")");
  }
  return point_label;
}

void TimingReporter::outputTimingPathSummary(std::ofstream* report_file, TimingPath& timing_path, std::size_t label_width)
{
  (*report_file) << "  " << std::string(label_width + 28, '-') << "\n";
  outputTimingSummaryLine(report_file, "data required time", timing_path.get_required_time(), label_width);
  outputTimingSummaryLine(report_file, "data arrival time", -timing_path.get_path_delay(), label_width);
  (*report_file) << "  " << std::string(label_width + 28, '-') << "\n";
  outputTimingSummaryLine(report_file, STAUTIL.getString("slack (", getSlackStatus(timing_path), ")"), timing_path.get_slack(), label_width);
  (*report_file) << "\n\n";
}

std::string TimingReporter::getSlackStatus(TimingPath& timing_path)
{
  return timing_path.get_slack() < 0.0 ? "VIOLATED" : "MET";
}

}  // namespace ista
