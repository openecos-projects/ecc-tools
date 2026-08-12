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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "Database.hpp"
#include "TCModel.hpp"

namespace ista {

#define STATC (ista::TimingCharacterizer::getInst())

class TimingCharacterizer
{
 public:
  static void initInst();
  static TimingCharacterizer& getInst();
  static void destroyInst();
  // function
  void characterize();

 private:
  // self
  static TimingCharacterizer* _tc_instance;

  TimingCharacterizer() = default;
  TimingCharacterizer(const TimingCharacterizer& other) = delete;
  TimingCharacterizer(TimingCharacterizer&& other) = delete;
  ~TimingCharacterizer() = default;
  TimingCharacterizer& operator=(const TimingCharacterizer& other) = delete;
  TimingCharacterizer& operator=(TimingCharacterizer&& other) = delete;
  // function
  TCModel initTCModel();
  void buildTCLibList(TCModel& tc_model);
  void writeLib(TCModel& tc_model);
  void buildTCLib(TCModel& tc_model, AnalysisType analysis_type);
  double getDesignArea();
  void buildTCPortList(TCLib& tc_lib);
  void buildTCPort(TCLib& tc_lib, Pin& pin);
  double getPortCapacitance(Pin& pin);
  void buildTCClockPathArcList(TCLib& tc_lib);
  void buildTCClockPathArc(TCLib& tc_lib, std::string& clock_port_name, std::string timing_type);
  void addTCScalarTable(TCTimingArc& timing_arc, std::string table_name, double value);
  void buildTCCheckArcList(TCLib& tc_lib, AnalysisType analysis_type);
  void buildTCCheckArc(TCLib& tc_lib, std::string& source_port, std::string& sink_port, std::string& timing_type, double rise_constraint,
                       double fall_constraint, bool has_rise_constraint, bool has_fall_constraint);
  std::string getTimingCheckType(TimingPath& timing_path, AnalysisType analysis_type);
  TimingCheckArc* getTimingCheckArc(TimingPath& timing_path, AnalysisType analysis_type);
  bool isMatchCheckType(TimingCheckArc& timing_check_arc, AnalysisType analysis_type);
  std::string getTimingCheckTypeSuffix(TransType clock_trans_type);
  std::string getRelatedClockPort(TimingPath& timing_path);
  double getCheckConstraint(TimingPath& timing_path, AnalysisType analysis_type);
  void buildTCDelayArcList(TCLib& tc_lib, AnalysisType analysis_type);
  double getWorseDelay(double current_delay, double delay, bool has_delay, AnalysisType analysis_type);
  void buildTCDelayArc(TCLib& tc_lib, std::string& source_port, std::string& sink_port, std::string& timing_type, std::string& timing_sense,
                       double rise_delay, double fall_delay, bool has_rise_delay, bool has_fall_delay);
  std::string getDelayArcRelatedPin(TimingPath& timing_path);
  std::string getDelayArcTimingType(TimingPath& timing_path);
  TransType getRegisterClockTransType(TimingPath& timing_path);
  std::string getDelayArcTimingSense(TimingPath& timing_path);
  double getDelayArcValue(TimingPath& timing_path);
  std::vector<TimingPath*> getTimingPathList(AnalysisType analysis_type);
  bool isMatchAnalysisType(TimingPath& timing_path, AnalysisType analysis_type);
  bool isPort(std::string& pin_name);
  bool isInputPort(std::string& pin_name);
  bool isOutputPort(std::string& pin_name);
  bool isClockPort(std::string& pin_name);
  bool isRegisterStartPoint(std::string& pin_name);
  bool isRegisterEndPoint(std::string& pin_name);
};

}  // namespace ista
