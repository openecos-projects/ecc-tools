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
#pragma once

#include "Database.hpp"
#include "DelayType.hpp"
#include "StartEndType.hpp"

namespace ista {

#define STATR (ista::TimingReporter::getInst())

class TimingReporter
{
 public:
  static void initInst();
  static TimingReporter& getInst();
  static void destroyInst();
  // function
  void report();

 private:
  // self
  static TimingReporter* _tr_instance;

  TimingReporter() = default;
  TimingReporter(const TimingReporter& other) = delete;
  TimingReporter(TimingReporter&& other) = delete;
  ~TimingReporter() = default;
  TimingReporter& operator=(const TimingReporter& other) = delete;
  TimingReporter& operator=(TimingReporter&& other) = delete;
  // function
  void reportTiming();
  void outputTimingReportList();
  void outputTimingReport(DelayType delay_type, StartEndType start_end_type);
  std::string getReportFilePath(DelayType delay_type, StartEndType start_end_type);
  void outputReportHeader(std::ofstream* report_file, DelayType delay_type, StartEndType start_end_type);
  void outputPathGroupList(std::ofstream* report_file, DelayType delay_type, StartEndType start_end_type);
  void outputReportFooter(std::ofstream* report_file);
  void outputTimingPathGroup(std::ofstream* report_file, TimingPathGroup& timing_path_group, DelayType delay_type,
                             StartEndType start_end_type);
  std::vector<TimingPath*> getReportTimingPathList(TimingPathGroup& timing_path_group, DelayType delay_type,
                                                   StartEndType start_end_type);
  std::vector<TimingPath*> getSortedTimingPathList(TimingPathGroup& timing_path_group, DelayType delay_type,
                                                   StartEndType start_end_type);
  std::vector<TimingPath*> getEndpointWorstTimingPathList(std::vector<TimingPath*>& timing_path_list);
  void outputQorSummaryReport();
  std::string getQorSummaryReportFilePath();
  std::string getQorSummaryJsonFilePath();
  void outputTimingPathsJson();
  std::string getTimingPathsJsonFilePath();
  void outputTimingPathJson(std::ofstream* json_file, TimingPath& timing_path, std::string& path_group_name,
                            DelayType delay_type);
  std::string getTimingPathId(TimingPath& timing_path, std::string& path_group_name, DelayType delay_type);
  std::vector<TimingPath*> getQorTimingPathList(TimingPathGroup& timing_path_group, DelayType delay_type);
  std::vector<std::string> getQorSortedGroupList(std::map<std::string, double>& value_map);
  double getQorFrequency(TimingPath& timing_path);
  std::string getQorDoubleString(double value, int32_t width, int32_t precision);
  std::string getQorIntString(int32_t value, int32_t width);
  std::string getQorNilString(int32_t width);
  std::string getQorFrequencyString(double frequency);
  std::string getQorKString(int32_t value, int32_t width);
  int32_t getQorCellArea();
  int32_t getQorLeafCellK();
  bool isMatchAnalysisType(TimingPath& timing_path, DelayType delay_type);
  bool isMatchStartEndType(TimingPath& timing_path, StartEndType start_end_type);
  bool isPort(std::string& pin_name);
  bool isRegisterStartPoint(std::string& pin_name);
  bool isRegisterEndPoint(std::string& pin_name);
  bool hasClockPoint(std::string& pin_name);
  bool isClockSourceStartPoint(std::string& pin_name);
  void outputTimingPath(std::ofstream* report_file, TimingPath& timing_path, std::string& path_group_name, DelayType delay_type);
  void outputTimingPathHeader(std::ofstream* report_file, TimingPath& timing_path, std::string& path_group_name, DelayType delay_type);
  void outputStartEndPoint(std::ofstream* report_file, std::string label, std::string text);
  std::string getStartEndPointName(std::string& text);
  std::string getStartEndPointDescription(std::string& text);
  std::string getStartPointText(TimingPath& timing_path);
  bool isInternalStartPoint(TimingPath& timing_path);
  bool isTieDrivenConstantOutput(Instance& instance);
  std::optional<bool> getTieDriverValue(std::string& pin_name);
  bool isTieHighCell(Instance& instance);
  bool isTieLowCell(Instance& instance);
  std::string getEndPointText(TimingPath& timing_path);
  std::string getEndPointCheckText(std::string& end_point, std::string& clock_name, TimingPath& timing_path);
  std::size_t outputTimingPointList(std::ofstream* report_file, TimingPath& timing_path, DelayType delay_type);
  std::size_t getTimingLineLabelWidth(TimingPath& timing_path, DelayType delay_type);
  bool shouldOutputTimingPoint(TimingPath& timing_path, TimingPathPoint& path_point);
  void updateTimingLineLabelWidth(std::size_t& label_width, std::string label);
  void outputTimingPointHeader(std::ofstream* report_file, std::size_t label_width);
  void outputLaunchClockInfo(std::ofstream* report_file, TimingPath& timing_path, DelayType delay_type, std::size_t label_width);
  std::string getLaunchClockEdgeText(TimingPath& timing_path, DelayType delay_type);
  void outputTimingLine(std::ofstream* report_file, std::string label, double incr, double path, bool has_incr, std::string transition,
                        std::size_t label_width);
  void outputTimingSummaryLine(std::ofstream* report_file, std::string label, double value, std::size_t label_width);
  std::string getClockName(TimingPath& timing_path);
  double getClockPeriod(std::string& clock_name);
  double getInputDelay(TimingPath& timing_path, DelayType delay_type);
  std::string getStartClockPin(TimingPath& timing_path);
  void outputTimingPoint(std::ofstream* report_file, TimingPath& timing_path, TimingPathPoint& path_point, bool is_first_point,
                         std::size_t label_width);
  std::string getNumberString(double value);
  std::string getPointLabel(TimingPathPoint& path_point);
  std::string getPTPinName(std::string& pin_name);
  std::string getPTCellName(TimingPathPoint& path_point);
  void outputRequiredClockInfo(std::ofstream* report_file, TimingPath& timing_path, DelayType delay_type, std::size_t label_width);
  std::string getLibraryCheckText(TimingPath& timing_path, DelayType delay_type);
  double getOutputDelay(TimingPath& timing_path, DelayType delay_type);
  std::string getPinLabel(std::string& pin_name);
  void outputTimingPathSummary(std::ofstream* report_file, TimingPath& timing_path, std::size_t label_width);
  std::string getSlackStatus(TimingPath& timing_path);
};

}  // namespace ista
