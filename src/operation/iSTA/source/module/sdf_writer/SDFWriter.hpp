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

#include "Config.hpp"
#include "DataManager.hpp"
#include "Database.hpp"
#include "SDFDelay.hpp"
#include "SWModel.hpp"

namespace ista {

#define STASW (ista::SDFWriter::getInst())

class SDFWriter
{
 public:
  static void initInst();
  static SDFWriter& getInst();
  static void destroyInst();
  // function
  void write();
  void write(std::string_view file_path);

 private:
  struct SDFCellArcKey
  {
    std::string condition;
    TransType input_trans_type = TransType::kNone;

    bool operator<(const SDFCellArcKey& other) const { return std::tie(condition, input_trans_type) < std::tie(other.condition, other.input_trans_type); }
  };

  using SDFCellArcDelayMap = std::map<SDFCellArcKey, SDFDelay>;

  // self
  static SDFWriter* _sw_instance;
  SWModel _sw_model;

  SDFWriter() = default;
  SDFWriter(const SDFWriter& other) = delete;
  SDFWriter(SDFWriter&& other) = delete;
  ~SDFWriter() = default;
  SDFWriter& operator=(const SDFWriter& other) = delete;
  SDFWriter& operator=(SDFWriter&& other) = delete;
  // function
  void outputSDF(const std::string_view file_path);
  std::string getSDFFilePath();
  void outputSDFHeader(std::ofstream* sdf_file);
  void outputSDFInterconnect(std::ofstream* sdf_file);
  void outputSDFInterconnectArc(std::ofstream* sdf_file, Arc& arc);
  bool isSDFOutputOnlyCellArc(Arc& arc);
  void outputSDFCellList(std::ofstream* sdf_file);
  void buildInstanceCellArcMap();
  void outputSDFCell(std::ofstream* sdf_file, Instance& instance);
  bool hasSDFCellContent(Instance& instance);
  bool hasSDFCellDelay(Instance& instance);
  bool hasSDFTimingCheck(Instance& instance);
  void outputSDFCellDelay(std::ofstream* sdf_file, Instance& instance);
  void outputSDFGraphCellArc(std::ofstream* sdf_file, Arc& arc);
  void mergeSDFCellArcDelay(SDFCellArcDelayMap& cell_arc_delay_map, TimingArc& timing_arc, TransType input_trans_type, SDFDelay& sdf_delay);
  void mergeSDFDelay(SDFDelay& target_delay, SDFDelay& source_delay);
  void outputSDFCellArcDelayMap(std::ofstream* sdf_file, std::string& source_port_name, std::string& sink_port_name, SDFCellArcDelayMap& cell_arc_delay_map);
  void outputSDFOnlyCellArcList(std::ofstream* sdf_file, Instance& instance);
  void outputSDFOnlyCellArc(std::ofstream* sdf_file, Instance& instance, TimingCellArc& timing_cell_arc);
  void outputSDFCellArc(std::ofstream* sdf_file, std::string& source_port_name, std::string& sink_port_name, std::string& condition, TransType input_trans_type,
                        SDFDelay& sdf_delay);
  void outputSDFTimingCheckList(std::ofstream* sdf_file, Instance& instance);
  void outputSDFTimingCheck(std::ofstream* sdf_file, Instance& instance, TimingCheckArc& timing_check_arc);
  void outputSDFEdgeTimingCheck(std::ofstream* sdf_file, Instance& instance, TimingCheckArc& timing_check_arc, TimingArc& timing_arc,
                                TransType data_trans_type);
  void outputSDFWidthTimingCheck(std::ofstream* sdf_file, Instance& instance, TimingCheckArc& timing_check_arc, TimingArc& timing_arc, TransType trans_type);
  void outputSDFPeriodTimingCheck(std::ofstream* sdf_file, Instance& instance, TimingCheckArc& timing_check_arc, TimingArc& timing_arc);
  void adjustSDFHoldTimingCheckDelay(Instance& instance, TimingCheckArc& hold_timing_check_arc, TimingArc& hold_timing_arc, TransType data_trans_type,
                                     double& minimum_hold_delay, double& maximum_hold_delay);
  TimingCheckArc* findSDFSetupTimingCheck(TimingCell& timing_cell, TimingCheckArc& hold_timing_check_arc);
  TimingArc* findSDFSetupTimingArc(TimingCheckArc& setup_timing_check_arc, TimingArc& hold_timing_arc, TransType data_trans_type);
  TimingCell* getTimingCell(Instance& instance);
  bool isSDFCellArc(Instance& instance, TimingCellArc& timing_cell_arc);
  bool isSDFTimingCheck(Instance& instance, TimingCheckArc& timing_check_arc);
  SDFDelay getSDFArcDelay(Arc& arc, TransType input_trans_type = TransType::kNone);
  SDFDelay getSDFTimingArcDelay(Arc& arc, TimingArc& timing_arc, TransType input_trans_type = TransType::kNone);
  SDFDelay getSDFTimingCellArcDelay(TimingCellArc& timing_cell_arc, TimingArc& timing_arc, TransType input_trans_type = TransType::kNone);
  void updateSDFDelay(SDFDelay& sdf_delay, std::map<AnalysisType, std::map<TransType, std::map<TransType, double>>>& delay_map,
                      TransType input_trans_type = TransType::kNone);
  bool hasSDFDelay(SDFDelay& sdf_delay);
  double getSDFTimingCheckDelay(Instance& instance, TimingCheckArc& timing_check_arc, TimingArc& timing_arc, AnalysisType analysis_type,
                                TransType data_trans_type);
  double getSDFTimingCheckSlew(Instance& instance, TimingCheckArc& timing_check_arc, AnalysisType analysis_type, TransType data_trans_type);
  double getSDFSlew(std::string& pin_name, AnalysisType analysis_type, TransType trans_type);
  AnalysisType getCaptureAnalysisType(AnalysisType analysis_type);
  std::string getSDFTimingCheckName(TimingCheckType timing_check_type);
  std::string getSDFPathName(Pin& pin);
  std::string getSDFPortName(Pin& pin);
  static std::string getSDFInstanceName(Instance& instance);
  static std::string getSDFName(std::string& name);
  std::string getSDFCondition(TimingArc& timing_arc);
  std::vector<TransType> getSDFInputTransTypeList(TimingArc& timing_arc);
  std::string getSDFEdgeName(TransType trans_type);
  void outputSDFDelay(std::ofstream* sdf_file, SDFDelay& sdf_delay);
  void outputSDFTriple(std::ofstream* sdf_file, double min_delay, double max_delay);
  std::string getSDFNumberString(double value);
};

}  // namespace ista
