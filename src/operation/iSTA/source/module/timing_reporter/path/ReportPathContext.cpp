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
#include "ReportPathContext.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <ios>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ReportFormatter.hpp"
#include "ReportPathFilter.hpp"
#include "ReportPathSelector.hpp"
#include "TimingTolerance.hpp"
#include "Utility.hpp"
#include "graph/TimingStateQuery.hpp"
#include "network/Network.hpp"

namespace ista {
bool ReportPathContext::isMatchAnalysisType(TimingPath& timing_path, DelayType delay_type)
{
  if (delay_type == DelayType::kNone) {
    return false;
  }
  if (delay_type == DelayType::kMin) {
    return timing_path.get_analysis_type() == AnalysisType::kMin;
  }
  return timing_path.get_analysis_type() == AnalysisType::kMax;
}

bool ReportPathContext::isMatchStartEndType(TimingPath& timing_path, StartEndType start_end_type)
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

bool ReportPathContext::isPort(const std::string& name)
{
  const auto pin = _database.network().pins().find(name);
  return pin != _database.network().pins().end() && pin->second.get_is_port();
}

bool ReportPathContext::isRegisterStartPoint(const std::string& name)
{
  const auto pin = _database.network().pins().find(name);
  if (pin == _database.network().pins().end() || pin->second.get_is_port())
    return false;
  const auto instance = _database.network().instances().find(pin->second.get_instance_name());
  if (instance == _database.network().instances().end() || !instance->second.get_is_sequential() || !hasClockPoint(instance->second.get_clock_pin_name()))
    return false;
  return name == instance->second.get_output_pin_name();
}

bool ReportPathContext::isRegisterEndPoint(const std::string& name)
{
  const auto pin = _database.network().pins().find(name);
  if (pin == _database.network().pins().end() || pin->second.get_is_port())
    return false;
  const auto instance = _database.network().instances().find(pin->second.get_instance_name());
  if (instance == _database.network().instances().end() || !instance->second.get_is_sequential() || !hasClockPoint(instance->second.get_clock_pin_name()))
    return false;
  for (const auto& check : instance->second.get_check_arc_list())
    if (check.get_data_port() == name)
      return true;
  return false;
}

bool ReportPathContext::hasClockPoint(const std::string& name)
{
  const auto point = _database.graph().points().find(name);
  return point != _database.graph().points().end() && point->second.get_is_clock_point();
}

bool ReportPathContext::isClockSourceStartPoint(const std::string& name)
{
  if (!isPort(name))
    return false;
  for (const auto& [clock_name, clock] : _database.sdc().get_clock_map())
    if (std::find(clock.get_source_list().begin(), clock.get_source_list().end(), name) != clock.get_source_list().end())
      return true;
  return false;
}

ReportEndpoint ReportPathContext::startPoint(TimingPath& path)
{
  ReportEndpoint result{getReportPinName(path.get_start_point()), {}};
  const auto pin = _database.network().pins().find(path.get_start_point());
  if (pin == _database.network().pins().end())
    return result;
  const auto clock = getClockName(path);
  const auto instance = _database.network().instances().find(pin->second.get_instance_name());
  if (!pin->second.get_is_port() && instance != _database.network().instances().end()) {
    if (instance->second.get_is_sequential() && isInternalStartPoint(path))
      return {getReportPinName(instance->second.get_clock_pin_name()), "(internal path startpoint clocked by " + clock + ")"};
    result.name = pin->second.get_instance_name();
  }
  if (isClockSourceStartPoint(path.get_start_point()))
    result.description = "(clock source '" + clock + "')";
  else if (pin->second.get_is_port())
    result.description = "(input port clocked by " + clock + ")";
  else
    result.description = (path.get_launch_clock_pin_transition() == TransType::kFall ? "(falling edge-triggered flip-flop clocked by "
                                                                                     : "(rising edge-triggered flip-flop clocked by ")
                         + clock + ")";
  return result;
}

bool ReportPathContext::isInternalStartPoint(TimingPath& path)
{
  const auto pin = _database.network().pins().find(path.get_start_point());
  if (pin == _database.network().pins().end())
    return false;
  const auto instance = _database.network().instances().find(pin->second.get_instance_name());
  if (instance == _database.network().instances().end())
    return false;
  return path.get_start_point() == instance->second.get_clock_pin_name()
         || (path.get_start_point() == instance->second.get_output_pin_name() && isTieDrivenConstantOutput(instance->second));
}

bool ReportPathContext::isTieDrivenConstantOutput(const Instance& instance)
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

std::optional<bool> ReportPathContext::getTieDriverValue(const std::string& name)
{
  const auto pin = _database.network().pins().find(name);
  if (pin == _database.network().pins().end())
    return std::nullopt;
  const auto net = _database.network().nets().find(pin->second.get_net_name());
  if (net == _database.network().nets().end())
    return std::nullopt;
  for (const auto& driver_name : net->second.get_driver_pin_list()) {
    const auto driver = _database.network().pins().find(driver_name);
    if (driver == _database.network().pins().end() || driver->second.get_is_port())
      continue;
    const auto instance = _database.network().instances().find(driver->second.get_instance_name());
    if (instance == _database.network().instances().end())
      continue;
    if (isTieHighCell(instance->second))
      return true;
    if (isTieLowCell(instance->second))
      return false;
  }
  return std::nullopt;
}

bool ReportPathContext::isTieHighCell(const Instance& instance)
{
  const std::string& cell_name = instance.get_cell_name();
  return cell_name.rfind("TIEHI", 0) == 0;
}

bool ReportPathContext::isTieLowCell(const Instance& instance)
{
  const std::string& cell_name = instance.get_cell_name();
  return cell_name.rfind("TIELO", 0) == 0;
}

ReportEndpoint ReportPathContext::endPoint(TimingPath& path)
{
  ReportEndpoint result{getReportPinName(path.get_end_point()), {}};
  const auto pin = _database.network().pins().find(path.get_end_point());
  if (pin == _database.network().pins().end())
    return result;
  if (pin->second.get_is_port())
    result.description = "(output port clocked by " + path.get_capture_clock_name() + ")";
  else {
    result.name = pin->second.get_instance_name();
    result.description = getEndPointCheckText(path);
  }
  return result;
}

std::string ReportPathContext::getEndPointCheckText(TimingPath& path)
{
  const bool falling = path.get_capture_clock_pin_transition() == TransType::kFall;
  const auto& clock = path.get_capture_clock_name();
  if (path.get_check_type() == TimingCheckType::kRecovery || path.get_check_type() == TimingCheckType::kRemoval)
    return std::string(path.get_check_type() == TimingCheckType::kRecovery ? "(recovery check against " : "(removal check against ")
           + (falling ? "falling-edge clock " : "rising-edge clock ") + clock + ")";
  return std::string(falling ? "(falling edge-triggered flip-flop clocked by " : "(rising edge-triggered flip-flop clocked by ") + clock + ")";
}

bool ReportPathContext::shouldOutputTimingPoint(TimingPath& path, TimingPathPoint& point)
{
  const auto pin = _database.network().pins().find(point.get_pin_name());
  if (pin == _database.network().pins().end() || pin->second.get_is_port())
    return true;
  return point.get_pin_name() == path.get_start_point() || point.get_pin_name() == path.get_end_point() || pin->second.get_direction() == PinDirection::kOutput
         || pin->second.get_direction() == PinDirection::kInout;
}

std::string ReportPathContext::getLaunchClockEdgeText(TimingPath& path)
{
  const auto edge = path.get_launch_clock_transition();
  return GetTransTypeName()(edge == TransType::kNone ? _database.network().launchClockTransition(path.get_start_point()) : edge);
}

std::string ReportPathContext::getClockName(TimingPath& timing_path)
{
  const Database& database = _database;
  if (!timing_path.get_clock_name().empty()) {
    return timing_path.get_clock_name();
  }
  const std::map<std::string, TimingClock>& clock_map = database.sdc().get_clock_map();
  if (!clock_map.empty()) {
    return clock_map.begin()->first;
  }
  return "clk";
}

std::string_view ReportPathContext::getClockNetworkDelayLabel(TimingPath& path, bool capture)
{
  const auto analysis = capture ? (path.get_analysis_type() == AnalysisType::kMax ? AnalysisType::kMin : AnalysisType::kMax) : path.get_analysis_type();
  const auto pin = capture ? path.get_capture_clock_pin() : getStartClockPin(path);
  const auto& clock = capture ? path.get_capture_clock_name() : path.get_clock_name();
  const auto edge = capture ? path.get_capture_clock_transition() : path.get_launch_clock_transition();
  const auto local = capture ? path.get_capture_clock_pin_transition() : path.get_launch_clock_pin_transition();
  return clockPinValues(_database, pin, clock, analysis, local, AnalysisType::kNone, edge).propagated ? "clock network delay (propagated)"
                                                                                                      : "clock network delay (ideal)";
}

double ReportPathContext::getClockPeriod(const std::string& clock_name)
{
  const Database& database = _database;
  const std::map<std::string, TimingClock>& clock_map = database.sdc().get_clock_map();
  if (clock_map.count(clock_name) > 0) {
    return clock_map.at(clock_name).get_period();
  }
  if (!clock_map.empty()) {
    return clock_map.begin()->second.get_period();
  }
  return 0.0;
}

std::string ReportPathContext::getStartClockPin(TimingPath& path)
{
  if (!path.get_launch_clock_pin().empty())
    return path.get_launch_clock_pin();
  const auto pin = _database.network().pins().find(path.get_start_point());
  if (pin == _database.network().pins().end() || pin->second.get_is_port())
    return {};
  const auto instance = _database.network().instances().find(pin->second.get_instance_name());
  return instance == _database.network().instances().end() ? std::string{} : instance->second.get_clock_pin_name();
}

std::optional<std::size_t> ReportPathContext::getPinFanout(const std::string& pin_name)
{
  const Database& database = _database;
  auto pin_it = database.network().pins().find(pin_name);
  if (pin_it == database.network().pins().end()) {
    return std::nullopt;
  }
  auto net_it = database.network().nets().find(pin_it->second.get_net_name());
  if (net_it == database.network().nets().end()) {
    return std::nullopt;
  }
  const Net& net = net_it->second;
  const std::vector<std::string>& drivers = net.get_driver_pin_list();
  if (net.get_driver_pin() != pin_name && std::find(drivers.begin(), drivers.end(), pin_name) == drivers.end()) {
    return std::nullopt;
  }
  std::set<std::string> loads(net.get_load_pin_list().begin(), net.get_load_pin_list().end());
  loads.erase(pin_name);
  return loads.size();
}

std::string ReportPathContext::getNumberString(double value)
{
  std::ostringstream oss;
  if (std::fabs(value) < ista::kTimingTolerance) {
    value = 0.0;
  }
  oss << std::fixed << std::setprecision(10) << value;
  return oss.str();
}

std::string ReportPathContext::getPointLabel(TimingPathPoint& point)
{
  std::string label = getReportPinName(point.get_pin_name());
  const auto pin = _database.network().pins().find(point.get_pin_name());
  if (pin != _database.network().pins().end() && pin->second.get_is_port()) {
    if (pin->second.get_direction() == PinDirection::kInput)
      return label + " (in)";
    if (pin->second.get_direction() == PinDirection::kOutput)
      return label + " (out)";
  }
  if (!point.get_cell_name().empty())
    label += " (" + point.get_cell_name() + ")";
  return label;
}

std::string ReportPathContext::getReportPinName(const std::string& pin_name)
{
  std::string report_pin_name = pin_name;
  std::replace(report_pin_name.begin(), report_pin_name.end(), ':', '/');
  return report_pin_name;
}

std::string ReportPathContext::getLibraryCheckText(TimingPath& timing_path, DelayType delay_type)
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

std::string ReportPathContext::getPinLabel(const std::string& name)
{
  std::string label = getReportPinName(name);
  const auto pin = _database.network().pins().find(name);
  if (pin == _database.network().pins().end())
    return label;
  const auto instance = _database.network().instances().find(pin->second.get_instance_name());
  if (instance != _database.network().instances().end())
    label += " (" + instance->second.get_cell_name() + ")";
  return label;
}

std::string ReportPathContext::getSlackStatus(TimingPath& timing_path)
{
  return timing_path.get_slack() < 0.0 ? "VIOLATED" : "MET";
}
}  // namespace ista
