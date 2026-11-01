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
#include "TimingJsonWriter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "ReportFormatter.hpp"
#include "ReportPathFilter.hpp"
#include "ReportPathSelector.hpp"
#include "TimingTolerance.hpp"
#include "Utility.hpp"
#include "network/Network.hpp"

namespace ista {
void TimingJsonWriter::write()
{
  Database& database = _database;
  auto json_file = STAUTIL.openOutputFile(getTimingPathsJsonFilePath());
  const int32_t path_limit = std::max(_config.timing_path_limit, 0);
  const std::string corner = _config.timing_corner.empty() ? "unknown" : _config.timing_corner;

  json_file << "{\n  \"schema_version\": 1,\n  \"corner\": \"" << escapeJsonString(corner) << "\",\n  \"path_limit\": " << path_limit << ",\n  \"paths\": [";
  bool first_path = true;
  for (DelayType delay_type : {DelayType::kMax, DelayType::kMin}) {
    std::vector<std::pair<std::string, TimingPath*>> timing_paths;
    for (PathGroup& timing_path_group : database.pathGroups()) {
      std::vector<TimingPath*> group_paths = _report_path_selector.endpointWorstPaths(timing_path_group, delay_type);
      for (TimingPath* timing_path : group_paths) {
        timing_paths.emplace_back(timing_path_group.name(), timing_path);
      }
    }
    std::sort(timing_paths.begin(), timing_paths.end(), ReportPathSelector::less);

    int32_t output_count = 0;
    for (const auto& [path_group_name, timing_path] : timing_paths) {
      if (output_count >= path_limit) {
        break;
      }
      if (!first_path) {
        json_file << ",";
      }
      std::string group_name = path_group_name;
      outputTimingPathJson(json_file, *timing_path, group_name, delay_type);
      first_path = false;
      output_count++;
    }
  }
  json_file << "]\n}\n";
  STAUTIL.finishOutputFile(json_file);
}

std::string TimingJsonWriter::getTimingPathsJsonFilePath()
{
  return STAUTIL.getString(_config.tr_temp_directory_path, "timing_paths.json");
}

void TimingJsonWriter::outputTimingPathJson(std::ostream& json_file, TimingPath& timing_path, std::string& path_group_name, DelayType delay_type)
{
  const std::string analysis_type = delay_type == DelayType::kMax ? "setup" : "hold";
  const std::string clock_name = _report_path_context.getClockName(timing_path);
  const std::string check_type = GetTimingCheckTypeName()(timing_path.get_check_type());
  json_file << "{\"path_id\":\"" << escapeJsonString(getTimingPathId(timing_path, path_group_name, delay_type)) << "\",\"analysis_type\":\"" << analysis_type
            << "\",\"path_group\":\"" << escapeJsonString(path_group_name) << "\",\"start_point\":\"" << escapeJsonString(timing_path.get_start_point())
            << "\",\"end_point\":\"" << escapeJsonString(timing_path.get_end_point()) << "\",\"launch_clock\":\"" << escapeJsonString(clock_name)
            << "\",\"capture_clock\":\"" << escapeJsonString(timing_path.get_capture_clock_name()) << "\",\"check_type\":\"" << escapeJsonString(check_type)
            << "\",\"slack_ns\":";
  outputJsonNumber(json_file, timing_path.get_slack());
  json_file << ",\"arrival_ns\":";
  outputJsonNumber(json_file, timing_path.get_path_delay());
  json_file << ",\"required_ns\":";
  outputJsonNumber(json_file, timing_path.get_required_time());
  json_file << ",\"cppr_ns\":";
  outputJsonNumber(json_file, timing_path.get_clock_reconvergence_pessimism());
  json_file << ",\"launch_clock_source_latency_ns\":";
  outputJsonNumber(json_file, timing_path.get_launch_clock_source_latency());
  json_file << ",\"launch_clock_network_delay_ns\":";
  outputJsonNumber(json_file, timing_path.get_launch_clock_network_delay());
  json_file << ",\"capture_clock_source_latency_ns\":";
  outputJsonNumber(json_file, timing_path.get_capture_clock_source_latency());
  json_file << ",\"capture_clock_network_delay_ns\":";
  outputJsonNumber(json_file, timing_path.get_capture_clock_network_delay());
  if (std::any_of(timing_path.get_point_list().begin(), timing_path.get_point_list().end(), [](const TimingPathPoint& point) { return point.rcNonbounding(); }))
    json_file << ",\"rc_nonbounding\":true";
  json_file << ",\"stages\":[";
  bool first_stage = true;
  for (TimingPathPoint& path_point : timing_path.get_point_list()) {
    if (!first_stage) {
      json_file << ",";
    }
    std::string kind = GetArcTypeName()(path_point.get_arc_type());
    if (kind == "none") {
      kind = "point";
    } else {
      kind += "_arc";
    }
    json_file << "{\"kind\":\"" << kind << "\",\"pin\":\"" << escapeJsonString(path_point.get_pin_name()) << "\",\"instance\":\""
              << escapeJsonString(path_point.get_instance_name()) << "\",\"cell\":\"" << escapeJsonString(path_point.get_cell_name())
              << "\",\"incremental_delay_ns\":";
    outputJsonNumber(json_file, path_point.get_arc_delay());
    json_file << ",\"arrival_ns\":";
    outputJsonNumber(json_file, path_point.get_arrival());
    json_file << ",\"transition\":\"" << GetTransTypeName()(path_point.get_trans_type()) << "\"";
    if (path_point.rcFailed())
      json_file << ",\"rc_failed\":true";
    if (path_point.rcNonbounding())
      json_file << ",\"rc_nonbounding\":true";
    json_file << "}";
    first_stage = false;
  }
  json_file << "]}";
}

std::string TimingJsonWriter::getTimingPathId(TimingPath& timing_path, std::string& path_group_name, DelayType delay_type)
{
  const std::string analysis_type = delay_type == DelayType::kMax ? "setup" : "hold";
  const std::string corner = _config.timing_corner.empty() ? "unknown" : _config.timing_corner;
  const std::string clock_name = _report_path_context.getClockName(timing_path);
  return stableTimingPathId(STAUTIL.getString(corner, "|", analysis_type, "|", path_group_name, "|", timing_path.get_start_point(), "|",
                                              timing_path.get_end_point(), "|", clock_name, "|", timing_path.get_capture_clock_name()));
}
}  // namespace ista
