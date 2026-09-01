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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "Database.hpp"
#include "TAModel.hpp"

namespace ista {

#define STATA (ista::TimingAnalyzer::getInst())

class TimingAnalyzer
{
 public:
  static void initInst();
  static TimingAnalyzer& getInst();
  static void destroyInst();
  // function
  void analyze();

 private:
  // self
  static TimingAnalyzer* _ta_instance;

  TimingAnalyzer() = default;
  TimingAnalyzer(const TimingAnalyzer& other) = delete;
  TimingAnalyzer(TimingAnalyzer&& other) = delete;
  ~TimingAnalyzer() = default;
  TimingAnalyzer& operator=(const TimingAnalyzer& other) = delete;
  TimingAnalyzer& operator=(TimingAnalyzer&& other) = delete;
  // function
  TAModel initTAModel();
  bool isDisableArc(Arc& arc);
  bool shouldStopDataPropagation(Arc& arc);
  bool isSequentialClockPin(std::string& pin_name);
  double getClockArrival(std::string& pin_name, AnalysisType analysis_type);
  double getClockArrival(std::string& pin_name, AnalysisType analysis_type, TransType trans_type);
  double getClockArrival(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type);
  double getClockSlew(std::string& pin_name, AnalysisType analysis_type, TransType trans_type);
  double roundTime(double time);
  std::vector<TransType> getOutputTransTypeList(Arc& arc, AnalysisType analysis_type, TransType input_trans_type);
  double getArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type);
  double getArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type);
  bool hasPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type);
  bool hasPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type);
  std::map<std::string, TimingPathState>& getPathStateMap(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type,
                                                          TransType trans_type);
  TimingPathState& getPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type,
                                std::string& start_point);
  TimingPathState* getWorstPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type);
  TimingPathState* getWorstPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type);
  TransType getEndPointTransType(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type);
  bool isBetterArrival(double candidate_arrival, double current_arrival, AnalysisType analysis_type);
  bool isFinite(double value);
  void propagateRequired();
  double resolveRequiredTime();
  void seedEndPointRequired(double required_time);
  double getEndPointRequired(std::string& end_point, double default_required_time, AnalysisType analysis_type);
  double getEndPointRequired(std::string& end_point, double default_required_time, AnalysisType analysis_type, TransType data_trans_type,
                             double data_slew);
  double getEndPointRequired(std::string& start_point, std::string& end_point, double default_required_time, AnalysisType analysis_type,
                             TransType data_trans_type, double data_slew);
  std::string_view getClockName(std::string& pin_name);
  double getClockUncertainty(std::string& pin_name, AnalysisType analysis_type);
  TimingClock* getStartPointClock(std::string& start_point);
  double getEndPointRequired(TimingPathState& end_path_state, std::string& end_point, double default_required_time,
                             AnalysisType analysis_type);
  bool isMatchCheckTransType(TimingCheckArc& timing_check_arc, TransType data_trans_type);
  double getEndPointCheckTime(std::string& end_point, TimingCheckArc& timing_check_arc, AnalysisType analysis_type,
                              TransType data_trans_type, double data_slew);
  double calcTimingCheckArcTime(TimingCheckArc& timing_check_arc, AnalysisType analysis_type, TransType clock_trans_type,
                                TransType data_trans_type, double clock_slew, double data_slew);
  AnalysisType getCaptureAnalysisType(AnalysisType analysis_type);
  TransType getClockTransType(TimingCheckArc& timing_check_arc);
  double getEndPointCaptureTime(std::string& end_point, AnalysisType analysis_type);
  double getEndPointClockArrival(std::string& end_point, AnalysisType analysis_type);
  double getEndPointClockArrival(std::string& end_point, AnalysisType analysis_type, TransType trans_type);
  double getClockReconvergencePessimism(TimingPathState& end_path_state, std::string& end_point, AnalysisType analysis_type,
                                        std::string& common_pin_name);
  double getClockReconvergencePessimism(std::string& start_point, std::string& end_point, AnalysisType analysis_type,
                                        std::string& common_pin_name);
  TransType getClockTransType(TimingCellArc& timing_cell_arc);
  bool isRegisterStartPoint(std::string& start_point);
  bool hasClockPoint(std::string& pin_name);
  double getClockReconvergencePessimism(std::pair<std::string, TransType>& launch_crpr_pin, std::string& end_point,
                                        AnalysisType analysis_type, std::string& common_pin_name);
  double getClockCommonPathDelayDelta(std::pair<std::string, TransType>& common_pin, AnalysisType analysis_type);
  void shrinkClockPathToCrprPath(std::vector<std::pair<std::string, TransType>>& clock_path);
  bool isLeafClockDriverPin(std::string& pin_name);
  bool isLeafClockBufferDriverPin(std::vector<std::pair<std::string, TransType>>& clock_path);
  bool hasSingleLeafClockBufferLoad(std::string& pin_name);
  bool isClockRootBufferDriverPin(std::string& pin_name);
  bool isLeafClockBufferDriverPin(std::string& pin_name);
  bool isLeafClockBufferLoadPin(std::string& pin_name);
  bool shouldShrinkLeafClockBufferLoad(std::string& pin_name);
  double getBufferDriveResistance(std::string& pin_name);
  std::vector<std::pair<std::string, TransType>> getClockPathPinList(std::string& clock_pin_name, AnalysisType analysis_type,
                                                                       TransType trans_type);
  double getClockCommonPathArrival(std::pair<std::string, TransType>& common_pin, AnalysisType analysis_type);
  TimingCheckArc* getEndPointCheckArc(std::string& end_point, AnalysisType analysis_type);
  bool isMatchCheckType(TimingCheckArc& timing_check_arc, AnalysisType analysis_type);
  double getClockPeriod(std::string_view clock_name);
  void propagateRequiredArc(Arc& arc);
  void updateSlack();
  void analyzeEndPointList(TAModel& ta_model);
  void uploadTimingPathGroupList(TAModel& ta_model);
  TimingPathGroup& getTimingPathGroup(TAModel& ta_model, TimingPath& timing_path);
  std::string getTimingPathGroupName(TimingPath& timing_path);
  TimingPathGroup initTimingPathGroup(std::string& group_name);
  std::vector<TimingPath> buildTimingPathList(std::string& end_point);
  void buildPathDiversionList(std::string& end_point);
  void buildPathDiversionList(std::string& end_point, AnalysisType analysis_type, PathSourceType source_type);
  void buildPathDiversionList(std::string& end_point, AnalysisType analysis_type, PathSourceType source_type,
                              std::vector<std::string>& path_pin_name_list, std::vector<TransType>& path_trans_type_list,
                              std::vector<std::size_t>& path_arc_idx_list);
  void buildPathDiversionState(AnalysisType analysis_type, PathSourceType source_type,
                               std::vector<std::string>& path_pin_name_list, std::vector<TransType>& path_trans_type_list,
                               std::vector<std::size_t>& path_arc_idx_list, std::size_t sink_idx, std::size_t diversion_arc_idx,
                               TransType input_trans_type, TimingPathState& source_path_state);
  bool isOutputTransType(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type);
  bool updateDiversionPathState(std::string& pin_name, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type,
                                TimingPathState& source_path_state, std::string& predecessor, std::size_t predecessor_arc_idx,
                                double predecessor_arc_delay, TransType predecessor_trans_type, double arrival);
  double getDataSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type);
  TimingPathState* getWorstSlackPathState(std::string& end_point, AnalysisType analysis_type, PathSourceType source_type);
  double calcPathRequiredTime(std::string& end_point, TimingPathState& end_path_state, AnalysisType analysis_type);
  double calcPathSlack(TimingPathState& end_path_state, double required_time, AnalysisType analysis_type);
  bool isConstrainedEndPoint(std::string& end_point);
  bool isOutputEndPoint(std::string& end_point);
  bool hasOutputDelay(std::string& end_point);
  bool isRegisterEndPoint(std::string& end_point);
  bool isTimingCheckEndPoint(std::string& end_point);
  TimingPath buildTimingPath(std::string& end_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type,
                             std::string& start_point);
  void buildPathTrace(std::string& end_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type,
                      std::string& start_point, std::vector<std::string>& path_pin_name_list, std::vector<TransType>& path_trans_type_list);
  std::vector<std::size_t> getPathArcIdxList(std::vector<std::string>& path_pin_name_list,
                                             std::vector<TransType>& path_trans_type_list, AnalysisType analysis_type,
                                             PathSourceType source_type, std::string& start_point);
  void updatePathDelay(TimingPath& timing_path, Arc* arc, double arc_delay);
  void updateClockInfo(TimingPath& timing_path, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type,
                       std::string& start_point);
  TimingPathPoint makeTimingPathPoint(std::string& pin_name, Arc* arc, AnalysisType analysis_type, PathSourceType source_type,
                                      TransType input_trans_type, TransType trans_type, std::string& start_point);
  void insertTimingPath(TimingPathGroup& timing_path_group, TimingPath& timing_path);
  TimingPathEnd initTimingPathEnd(std::string& end_point);
  void updateWorstSlack(TAModel& ta_model, TimingPath& timing_path);
  void updateViolation(TAModel& ta_model, TimingPath& timing_path);
  std::size_t getTimingPathNum(std::map<std::string, TimingPathGroup>& timing_path_group_map);
  std::size_t getTimingPathNum(TimingPathGroup& timing_path_group);
  void updateSummary(TAModel& ta_model);
};

}  // namespace ista
