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
#include "liberty/Lib.hh"

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
  void outputLibFileList();
  void outputLibFile(AnalysisType analysis_type);
  std::unique_ptr<idb::LibLibrary> buildTimingModel(AnalysisType analysis_type);
  void buildTimingModelHeader(idb::LibLibrary& timing_model);
  idb::CapacitiveUnit getLibCapacitiveUnit(TimingLibrary& timing_library);
  idb::ResistanceUnit getLibResistanceUnit(TimingLibrary& timing_library);
  idb::TimeUnit getLibTimeUnit(TimingLibrary& timing_library);
  std::unique_ptr<idb::LibCell> buildDesignCell(idb::LibLibrary& timing_model, AnalysisType analysis_type);
  double getDesignArea();
  void buildPortList(idb::LibCell& design_cell);
  void buildPort(idb::LibCell& design_cell, Pin& pin);
  idb::LibPort::LibertyPortType getLibPortType(Pin& pin);
  double getPortCapacitance(Pin& pin);
  void buildClockPathArcList(idb::LibCell& design_cell);
  void buildClockPathArc(idb::LibCell& design_cell, std::string& clock_port_name, std::string timing_type);
  void buildCheckArcList(idb::LibCell& design_cell, AnalysisType analysis_type);
  void buildCheckArc(idb::LibCell& design_cell, std::string& source_port, std::string& sink_port, std::string& timing_type, double rise_constraint,
                     double fall_constraint, bool has_rise_constraint, bool has_fall_constraint);
  std::string getTimingCheckType(TimingPath& timing_path, AnalysisType analysis_type);
  TimingCheckArc* getTimingCheckArc(TimingPath& timing_path, AnalysisType analysis_type);
  bool isMatchCheckType(TimingCheckArc& timing_check_arc, AnalysisType analysis_type);
  std::string getTimingCheckTypeSuffix(TransType clock_trans_type);
  std::string getRelatedClockPort(TimingPath& timing_path);
  double getCheckConstraint(TimingPath& timing_path, AnalysisType analysis_type);
  void buildDelayArcList(idb::LibCell& design_cell, AnalysisType analysis_type);
  double getWorseDelay(double current_delay, double delay, bool has_delay, AnalysisType analysis_type);
  void buildDelayArc(idb::LibCell& design_cell, std::string& source_port, std::string& sink_port, std::string& timing_type, std::string& timing_sense,
                     double rise_delay, double fall_delay, bool has_rise_delay, bool has_fall_delay);
  std::string getDelayArcRelatedPin(TimingPath& timing_path);
  std::string getDelayArcTimingType(TimingPath& timing_path);
  TransType getRegisterClockTransType(TimingPath& timing_path);
  std::string getDelayArcTimingSense(TimingPath& timing_path);
  double getDelayArcValue(TimingPath& timing_path);
  std::unique_ptr<idb::LibArc> makeLibArc(std::string& source_port, std::string& sink_port, std::string& timing_type);
  std::unique_ptr<idb::LibTable> makeScalarTable(idb::LibTable::TableType table_type, double value);
  std::string getLibFilePath(std::string& design_name, AnalysisType analysis_type);
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
