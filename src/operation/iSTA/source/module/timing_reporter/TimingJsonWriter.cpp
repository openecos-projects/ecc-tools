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
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ClockReconvergenceCalculator.hpp"
#include "GeometryDisplayWriter.h"
#include "ReportFormatter.hpp"
#include "ReportPathFilter.hpp"
#include "ReportPathSelector.hpp"
#include "TimingTolerance.hpp"
#include "Utility.hpp"
#include "network/Network.hpp"

namespace ista {
namespace {
AnalysisType captureAnalysisType(AnalysisType analysis_type)
{
  return analysis_type == AnalysisType::kMax ? AnalysisType::kMin : AnalysisType::kMax;
}

std::string clockSourcePin(const std::vector<ClockPathPointInfo>& clock_path)
{
  return clock_path.empty() ? std::string{} : clock_path.front().pin_name;
}

std::string clockEndpointPin(const std::vector<ClockPathPointInfo>& clock_path)
{
  return clock_path.empty() ? std::string{} : clock_path.back().pin_name;
}

bool isVirtualClock(Database& database, const std::string& clock_name)
{
  const auto clock = database.sdc().get_clock_map().find(clock_name);
  return clock != database.sdc().get_clock_map().end() && clock->second.get_source_list().empty();
}

TransType localClockTransition(TransType transition)
{
  return transition == TransType::kNone ? TransType::kRise : transition;
}

std::optional<TransType> clockSourceTransition(TransType transition)
{
  return transition == TransType::kNone ? std::nullopt : std::optional<TransType>{transition};
}

nlohmann::json clockPathValue(Database& database, const std::vector<ClockPathPointInfo>& clock_path)
{
  auto result = nlohmann::json::array();
  for (const auto& point : clock_path) {
    const auto pin = database.network().pins().find(point.pin_name);
    const bool is_port = pin != database.network().pins().end() && pin->second.get_is_port();
    const std::string instance_name = pin == database.network().pins().end() ? std::string{} : pin->second.get_instance_name();
    const auto instance = database.network().instances().find(instance_name);
    result.push_back({{"kind", is_port ? "point" : "clock_arc"},
                      {"pin", point.pin_name},
                      {"instance", instance_name},
                      {"cell", instance == database.network().instances().end() ? std::string{} : instance->second.get_cell_name()},
                      {"transition", GetTransTypeName()(point.transition)}});
  }
  return result;
}
}  // namespace

void TimingJsonWriter::write()
{
  Database& database = _database;
  auto json_file = STAUTIL.openOutputFile(getTimingPathsJsonFilePath());
  const int32_t path_limit = std::max(_config.timing_path_limit, 0);
  const std::string corner = _config.timing_corner.empty() ? "unknown" : _config.timing_corner;

  json_file << "{\n  \"schema_version\": 1,\n  \"corner\": \"" << escapeJsonString(corner) << "\",\n  \"path_limit\": " << path_limit << ",\n  \"paths\": [";
  auto binary_path = std::filesystem::path(getTimingPathsJsonFilePath());
  binary_path.replace_extension(".gdisplay");
  ecc::geometry::GeometryDisplayWriter binary(
      binary_path, {{"kind", "timing"}, {"key", "timing:" + corner}, {"label", corner + " timing paths"}, {"corner", corner}, {"unit", "ns"}},
      ecc::geometry::GeometryDisplayWriter::Encoding::kRecords);
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
      auto value = timingPathValue(*timing_path, group_name, delay_type);
      binary.record(value);
      json_file << value.dump();
      first_path = false;
      output_count++;
    }
  }
  json_file << "]\n}\n";
  STAUTIL.finishOutputFile(json_file);
  binary.finish();
}

std::string TimingJsonWriter::getTimingPathsJsonFilePath()
{
  return STAUTIL.getString(_config.tr_temp_directory_path, "timing_paths.json");
}

nlohmann::json TimingJsonWriter::timingPathValue(TimingPath& timing_path, std::string& path_group_name, DelayType delay_type)
{
  const auto clock_name = _report_path_context.getClockName(timing_path);
  nlohmann::json value = {{"path_id", getTimingPathId(timing_path, path_group_name, delay_type)},
                          {"analysis_type", delay_type == DelayType::kMax ? "setup" : "hold"},
                          {"path_group", path_group_name},
                          {"start_point", timing_path.get_start_point()},
                          {"end_point", timing_path.get_end_point()},
                          {"launch_clock", clock_name},
                          {"capture_clock", timing_path.get_capture_clock_name()},
                          {"check_type", GetTimingCheckTypeName()(timing_path.get_check_type())}};
  auto number = [&](const char* key, double x) { value[key] = std::isfinite(x) ? nlohmann::json(x) : nlohmann::json(nullptr); };
  number("slack_ns", timing_path.get_slack());
  number("arrival_ns", timing_path.get_path_delay());
  number("required_ns", timing_path.get_required_time());
  number("cppr_ns", timing_path.get_clock_reconvergence_pessimism());
  number("launch_clock_source_latency_ns", timing_path.get_launch_clock_source_latency());
  number("launch_clock_network_delay_ns", timing_path.get_launch_clock_network_delay());
  number("capture_clock_source_latency_ns", timing_path.get_capture_clock_source_latency());
  number("capture_clock_network_delay_ns", timing_path.get_capture_clock_network_delay());
  ClockReconvergenceCalculator reconvergence(_database);
  const auto launch_clock_path = timing_path.get_launch_clock_pin().empty()
                                     ? std::vector<ClockPathPointInfo>{}
                                     : reconvergence.getClockPathPins(timing_path.get_launch_clock_pin(), clock_name, timing_path.get_analysis_type(),
                                                                      localClockTransition(timing_path.get_launch_clock_pin_transition()),
                                                                      clockSourceTransition(timing_path.get_launch_clock_transition()));
  const auto capture_clock_path = timing_path.get_capture_clock_pin().empty()
                                      ? std::vector<ClockPathPointInfo>{}
                                      : reconvergence.getClockPathPins(timing_path.get_capture_clock_pin(), timing_path.get_capture_clock_name(),
                                                                       captureAnalysisType(timing_path.get_analysis_type()),
                                                                       localClockTransition(timing_path.get_capture_clock_pin_transition()),
                                                                       clockSourceTransition(timing_path.get_capture_clock_transition()));
  value["launch_clock_virtual"] = isVirtualClock(_database, clock_name);
  value["capture_clock_virtual"] = isVirtualClock(_database, timing_path.get_capture_clock_name());
  value["launch_clock_source_pin"] = clockSourcePin(launch_clock_path);
  value["launch_clock_endpoint_pin"] = clockEndpointPin(launch_clock_path);
  value["capture_clock_source_pin"] = clockSourcePin(capture_clock_path);
  value["capture_clock_endpoint_pin"] = clockEndpointPin(capture_clock_path);
  value["launch_clock_path"] = clockPathValue(_database, launch_clock_path);
  value["capture_clock_path"] = clockPathValue(_database, capture_clock_path);
  value["stages"] = nlohmann::json::array();
  for (auto& point : timing_path.get_point_list()) {
    auto kind = GetArcTypeName()(point.get_arc_type());
    kind = kind == "none" ? "point" : kind + "_arc";
    nlohmann::json stage = {{"kind", kind},
                            {"pin", point.get_pin_name()},
                            {"instance", point.get_instance_name()},
                            {"cell", point.get_cell_name()},
                            {"transition", GetTransTypeName()(point.get_trans_type())}};
    stage["incremental_delay_ns"] = std::isfinite(point.get_arc_delay()) ? nlohmann::json(point.get_arc_delay()) : nlohmann::json(nullptr);
    stage["arrival_ns"] = std::isfinite(point.get_arrival()) ? nlohmann::json(point.get_arrival()) : nlohmann::json(nullptr);
    if (point.rcFailed())
      stage["rc_failed"] = true;
    if (point.rcNonbounding()) {
      stage["rc_nonbounding"] = true;
      value["rc_nonbounding"] = true;
    }
    value["stages"].push_back(std::move(stage));
  }
  return value;
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
