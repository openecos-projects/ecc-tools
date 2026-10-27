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
#include "PathReportWriter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <ios>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ReportFormatter.hpp"
#include "ReportPathFilter.hpp"
#include "ReportPathSelector.hpp"
#include "TimingTolerance.hpp"
#include "Utility.hpp"
#include "network/Network.hpp"

namespace ista {
void PathReportWriter::write(DelayType delay_type, StartEndType start_end_type)
{
  std::string report_file_path = getReportFilePath(delay_type, start_end_type);
  auto report_file = STAUTIL.openOutputFile(report_file_path);
  outputReportHeader(report_file, delay_type, start_end_type);
  outputPathGroupList(report_file, delay_type, start_end_type);
  outputReportFooter(report_file);
  STAUTIL.finishOutputFile(report_file);
}

std::string PathReportWriter::getReportFilePath(DelayType delay_type, StartEndType start_end_type)
{
  if (start_end_type == StartEndType::kNone) {
    return STAUTIL.getString(_config.tr_temp_directory_path, "timing_", GetDelayTypeName()(delay_type), ".rpt");
  }
  return STAUTIL.getString(_config.tr_temp_directory_path, "timing_", GetDelayTypeName()(delay_type), "_", GetStartEndTypeReportName()(start_end_type), ".rpt");
}

void PathReportWriter::outputReportHeader(std::ostream& report_file, DelayType delay_type, StartEndType start_end_type)
{
  Database& database = _database;
  report_file << "****************************************\n";
  report_file << "Design : " << database.network().name() << "\n";
  report_file << "DelayType : " << GetDelayTypeName()(delay_type) << "\n";
  report_file << "StartEndType : " << (start_end_type == StartEndType::kNone ? "all" : GetStartEndTypeName()(start_end_type)) << "\n";
  report_file << "SlackLesserThan : ";
  if (_config.has_timing_report_slack_lesser_than) {
    report_file << _report_path_context.getNumberString(_config.timing_report_slack_lesser_than);
  } else if (getReportSlackFilter(_config).implicit_negative_only) {
    report_file << _report_path_context.getNumberString(0.0);
  } else {
    report_file << "infinity";
  }
  report_file << "\n";
  report_file << "SlackGreaterThan : ";
  if (_config.has_timing_report_slack_greater_than) {
    report_file << _report_path_context.getNumberString(_config.timing_report_slack_greater_than);
  } else {
    report_file << "-infinity";
  }
  report_file << "\n";
  report_file << "Nworst : " << _config.endpoint_path_report_number << "\n";
  report_file << "MaxPaths : " << _config.path_report_number << "\n";
  report_file << "SortBy : slack\n";
  report_file << "Fanout : number of unique load pins driven by the point (blank for non-drivers)\n";
  report_file << "****************************************\n\n";
}

void PathReportWriter::outputPathGroupList(std::ostream& report_file, DelayType delay_type, StartEndType start_end_type)
{
  std::vector<std::pair<std::string, TimingPath*>> timing_path_list = _report_path_selector.reportPaths(delay_type, start_end_type);
  if (timing_path_list.empty()) {
    report_file << "No constrained paths.\n\n";
    return;
  }
  for (std::pair<std::string, TimingPath*>& timing_path_pair : timing_path_list) {
    outputTimingPath(report_file, *timing_path_pair.second, timing_path_pair.first, delay_type);
  }
}

void PathReportWriter::outputReportFooter(std::ostream& report_file)
{
  report_file << "1\n";
}

void PathReportWriter::outputTimingPath(std::ostream& report_file, TimingPath& timing_path, std::string& path_group_name, DelayType delay_type)
{
  outputTimingPathHeader(report_file, timing_path, path_group_name, delay_type);
  std::size_t label_width = outputTimingPointList(report_file, timing_path, delay_type);
  outputTimingPathSummary(report_file, timing_path, label_width);
}

void PathReportWriter::outputTimingPathHeader(std::ostream& report_file, TimingPath& timing_path, std::string& path_group_name, DelayType delay_type)
{
  outputStartEndPoint(report_file, "Startpoint", _report_path_context.startPoint(timing_path));
  outputStartEndPoint(report_file, "Endpoint", _report_path_context.endPoint(timing_path));
  if (_report_path_context.isRegisterStartPoint(timing_path.get_start_point()) && _report_path_context.isRegisterEndPoint(timing_path.get_end_point())) {
    report_file << "  Last common pin: " << _report_path_context.getReportPinName(timing_path.get_last_common_pin()) << "\n";
  }
  report_file << "  Path Group: " << path_group_name << "\n";
  report_file << "  Path Type: " << GetDelayTypeName()(delay_type) << "\n\n";
  if (std::any_of(timing_path.get_point_list().begin(), timing_path.get_point_list().end(), [](const TimingPathPoint& point) { return point.rcNonbounding(); }))
    report_file << "  Warning: nonbounding RC result on this path; failed crossings use zero delay and source slew.\n\n";
}

void PathReportWriter::outputStartEndPoint(std::ostream& report_file, std::string label, const ReportEndpoint& endpoint)
{
  const auto& name = endpoint.name;
  const auto& description = endpoint.description;
  report_file << "  " << label << ": " << name;
  if (!description.empty()) {
    if (name.length() <= 10) {
      report_file << " " << description;
    } else {
      report_file << "\n               " << description;
    }
  }
  report_file << "\n";
}

std::size_t PathReportWriter::outputTimingPointList(std::ostream& report_file, TimingPath& timing_path, DelayType delay_type)
{
  std::size_t label_width = getTimingLineLabelWidth(timing_path, delay_type);
  outputTimingPointHeader(report_file, label_width);
  report_file << "  " << std::string(label_width + kTimingTableExtraWidth, '-') << "\n";
  outputLaunchClockInfo(report_file, timing_path, delay_type, label_width);
  bool is_first_point = true;
  for (TimingPathPoint& path_point : timing_path.get_point_list()) {
    if (_report_path_context.shouldOutputTimingPoint(timing_path, path_point)) {
      outputTimingPoint(report_file, timing_path, path_point, is_first_point, label_width);
      is_first_point = false;
    }
  }
  outputTimingSummaryLine(report_file, "data arrival time", timing_path.get_path_delay(), label_width);
  report_file << "\n";
  outputRequiredClockInfo(report_file, timing_path, delay_type, label_width);
  return label_width;
}

std::size_t PathReportWriter::getTimingLineLabelWidth(TimingPath& timing_path, DelayType delay_type)
{
  std::size_t label_width = 35;
  std::string clock_name = _report_path_context.getClockName(timing_path);
  updateTimingLineLabelWidth(label_width, STAUTIL.getString("clock ", clock_name, " (rise edge)"));
  updateTimingLineLabelWidth(label_width, _report_path_context.getClockNetworkDelayLabel(timing_path, false));

  std::string start_clock_pin = _report_path_context.getStartClockPin(timing_path);
  if (!start_clock_pin.empty() && start_clock_pin != timing_path.get_start_point()) {
    updateTimingLineLabelWidth(label_width, _report_path_context.getPinLabel(start_clock_pin));
  }

  if (timing_path.get_input_delay())
    updateTimingLineLabelWidth(label_width, "input external delay");
  updateTimingLineLabelWidth(label_width, _report_path_context.getClockNetworkDelayLabel(timing_path, true));

  for (TimingPathPoint& path_point : timing_path.get_point_list()) {
    if (_report_path_context.shouldOutputTimingPoint(timing_path, path_point)) {
      updateTimingLineLabelWidth(label_width, _report_path_context.getPointLabel(path_point));
    }
  }

  updateTimingLineLabelWidth(label_width, "clock reconvergence pessimism");
  if (!timing_path.get_capture_clock_pin().empty()) {
    updateTimingLineLabelWidth(label_width, _report_path_context.getPinLabel(timing_path.get_capture_clock_pin()));
  }
  if (std::fabs(timing_path.get_check_time()) > ista::kTimingTolerance) {
    updateTimingLineLabelWidth(label_width, _report_path_context.getLibraryCheckText(timing_path, delay_type));
  } else if (_report_path_context.isPort(timing_path.get_end_point())) {
    if (timing_path.get_output_delay())
      updateTimingLineLabelWidth(label_width, "output external delay");
  }
  return label_width;
}

void PathReportWriter::updateTimingLineLabelWidth(std::size_t& label_width, std::string_view label)
{
  label_width = std::max(label_width, label.length());
}

void PathReportWriter::outputTimingPointHeader(std::ostream& report_file, std::size_t label_width)
{
  report_file << "  " << std::left << std::setw(label_width + 2) << "Point" << std::right << std::setw(kTimingFanoutWidth) << "Fanout"
              << std::setw(kTimingValueWidth) << "Incr" << std::setw(kTimingValueWidth) << "Path" << "\n";
}

void PathReportWriter::outputLaunchClockInfo(std::ostream& report_file, TimingPath& timing_path, DelayType delay_type, std::size_t label_width)
{
  std::string clock_name = _report_path_context.getClockName(timing_path);
  double launch_time = timing_path.get_launch_time();
  double launch_clock_source_latency = timing_path.get_launch_clock_source_latency();
  double launch_clock_network_delay = timing_path.get_launch_clock_network_delay();
  double launch_clock_edge = launch_time - launch_clock_source_latency - launch_clock_network_delay;
  outputTimingLine(report_file, STAUTIL.getString("clock ", clock_name, " (", _report_path_context.getLaunchClockEdgeText(timing_path), " edge)"),
                   launch_clock_edge, launch_clock_edge, true, "", label_width);
  double clock_path = launch_clock_edge;
  if (std::fabs(launch_clock_source_latency) > ista::kTimingTolerance || _report_path_context.isClockSourceStartPoint(timing_path.get_start_point())) {
    clock_path += launch_clock_source_latency;
    outputTimingLine(report_file, "clock source latency", launch_clock_source_latency, clock_path, true, "", label_width);
  }
  if (!_report_path_context.isClockSourceStartPoint(timing_path.get_start_point())) {
    clock_path += launch_clock_network_delay;
    outputTimingLine(report_file, _report_path_context.getClockNetworkDelayLabel(timing_path, false), launch_clock_network_delay, clock_path, true, "",
                     label_width);
  }

  std::string start_clock_pin = _report_path_context.getStartClockPin(timing_path);
  if (!start_clock_pin.empty() && start_clock_pin != timing_path.get_start_point()) {
    outputTimingLine(report_file, _report_path_context.getPinLabel(start_clock_pin), 0.0, launch_time, true,
                     GetTransTypeInitial()(timing_path.get_launch_clock_pin_transition()), label_width);
  }

  if (const auto delay = timing_path.get_input_delay()) {
    const auto edge = timing_path.get_point_list().empty() ? timing_path.get_trans_type() : timing_path.get_point_list().front().get_trans_type();
    outputTimingLine(report_file, "input external delay", *delay, launch_time + *delay, true, GetTransTypeInitial()(edge), label_width);
  }
}

void PathReportWriter::outputTimingLine(std::ostream& report_file, std::string_view label, double incr, double path, bool has_incr, std::string transition,
                                        std::size_t label_width, std::optional<std::size_t> fanout)
{
  report_file << "  " << std::left << std::setw(label_width + 2) << label << std::right << std::setw(kTimingFanoutWidth)
              << (fanout.has_value() ? std::to_string(*fanout) : "") << std::setw(kTimingValueWidth)
              << (has_incr ? _report_path_context.getNumberString(incr) : "") << std::setw(kTimingValueWidth) << _report_path_context.getNumberString(path);
  if (!transition.empty()) {
    report_file << " " << transition;
  }
  report_file << "\n";
}

void PathReportWriter::outputTimingSummaryLine(std::ostream& report_file, std::string label, double value, std::size_t label_width)
{
  outputTimingLine(report_file, label, 0.0, value, false, "", label_width);
}

void PathReportWriter::outputTimingPoint(std::ostream& report_file, TimingPath& timing_path, TimingPathPoint& path_point, bool is_first_point,
                                         std::size_t label_width)
{
  double arc_delay = path_point.get_arc_delay();
  if (is_first_point)
    arc_delay = path_point.get_arrival() - timing_path.get_launch_time() - timing_path.get_input_delay().value_or(0.0);
  outputTimingLine(report_file, _report_path_context.getPointLabel(path_point), arc_delay, path_point.get_arrival(), true,
                   GetTransTypeInitial()(path_point.get_trans_type()), label_width, _report_path_context.getPinFanout(path_point.get_pin_name()));
}

void PathReportWriter::outputRequiredClockInfo(std::ostream& report_file, TimingPath& timing_path, DelayType delay_type, std::size_t label_width)
{
  const auto& clock_name = timing_path.get_capture_clock_name();
  const double source = timing_path.get_capture_clock_source_latency();
  const double network = timing_path.get_capture_clock_network_delay();
  const double crpr = timing_path.get_clock_reconvergence_pessimism();
  double path = timing_path.get_capture_time() - source - network - crpr;
  outputTimingLine(report_file, STAUTIL.getString("clock ", clock_name, " (", GetTransTypeName()(timing_path.get_capture_clock_transition()), " edge)"), path,
                   path, true, "", label_width);
  if (std::fabs(source) > ista::kTimingTolerance) {
    path += source;
    outputTimingLine(report_file, "clock source latency", source, path, true, "", label_width);
  } else
    path += source;
  path += network;
  outputTimingLine(report_file, _report_path_context.getClockNetworkDelayLabel(timing_path, true), network, path, true, "", label_width);
  path += crpr;
  outputTimingLine(report_file, "clock reconvergence pessimism", crpr, path, true, "", label_width);
  if (!timing_path.get_capture_clock_pin().empty())
    outputTimingLine(report_file, _report_path_context.getPinLabel(timing_path.get_capture_clock_pin()), 0.0, path, false,
                     GetTransTypeInitial()(timing_path.get_capture_clock_pin_transition()), label_width);
  const double adjustment = timing_path.get_required_time_adjustment();
  path += adjustment;
  if (std::fabs(adjustment) > ista::kTimingTolerance)
    outputTimingLine(report_file, "path exception adjustment", adjustment, path, true, "", label_width);
  const double uncertainty = (delay_type == DelayType::kMax ? -1 : 1) * timing_path.get_clock_uncertainty();
  path += uncertainty;
  if (std::fabs(uncertainty) > ista::kTimingTolerance)
    outputTimingLine(report_file, "clock uncertainty", uncertainty, path, true, "", label_width);
  if (timing_path.get_check_type() != TimingCheckType::kNone) {
    const double check = (delay_type == DelayType::kMin || timing_path.get_check_type() == TimingCheckType::kRecovery ? 1 : -1) * timing_path.get_check_time();
    path += check;
    if (std::fabs(check) > ista::kTimingTolerance)
      outputTimingLine(report_file, _report_path_context.getLibraryCheckText(timing_path, delay_type), check, path, true, "", label_width);
  } else if (const auto delay = timing_path.get_output_delay()) {
    path -= *delay;
    outputTimingLine(report_file, "output external delay", -*delay, path, true, "", label_width);
  }
  outputTimingSummaryLine(report_file, "data required time", timing_path.get_required_time(), label_width);
}

void PathReportWriter::outputTimingPathSummary(std::ostream& report_file, TimingPath& timing_path, std::size_t label_width)
{
  report_file << "  " << std::string(label_width + kTimingTableExtraWidth, '-') << "\n";
  outputTimingSummaryLine(report_file, "data required time", timing_path.get_required_time(), label_width);
  outputTimingSummaryLine(report_file, "data arrival time", -timing_path.get_path_delay(), label_width);
  report_file << "  " << std::string(label_width + kTimingTableExtraWidth, '-') << "\n";
  outputTimingSummaryLine(report_file, STAUTIL.getString("slack (", _report_path_context.getSlackStatus(timing_path), ")"), timing_path.get_slack(),
                          label_width);
  report_file << "\n\n";
}
}  // namespace ista
