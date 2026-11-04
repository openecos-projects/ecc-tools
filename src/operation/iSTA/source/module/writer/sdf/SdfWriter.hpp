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

#include <map>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "Config.hpp"
#include "Database.hpp"
#include "SdfDelay.hpp"
#include "SdfFormatter.hpp"
#include "SdfValueExtractor.hpp"

namespace ista {

class SdfWriter
{
 public:
  SdfWriter(Database& database, const Config& config) : _database(database), _config(config), _values(database), _format(database.network()) {}
  SdfWriter(const SdfWriter&) = delete;
  SdfWriter& operator=(const SdfWriter&) = delete;
  void write(std::ostream& sdf_file);
  void write();
  void write(std::string_view file_path);

 private:
  Database& _database;
  const Config& _config;
  SdfValueExtractor _values;
  SdfFormatter _format;
  std::map<std::string, std::vector<Arc*>> _instance_cell_arcs;

  // SdfWriter.cpp
  std::string getSdfFilePath();
  void outputSdfHeader(std::ostream& sdf_file);
  void outputSdfInterconnect(std::ostream& sdf_file);
  void outputSdfInterconnectArc(std::ostream& sdf_file, Arc& arc);
  bool isSdfOutputOnlyCellArc(Arc& arc);
  void outputSdfCellList(std::ostream& sdf_file);
  void buildInstanceCellArcMap();
  void outputSdfCell(std::ostream& sdf_file, Instance& instance);
  bool hasSdfCellContent(Instance& instance);
  bool hasSdfCellDelay(Instance& instance);
  bool hasSdfTimingCheck(Instance& instance);
  void outputSdfCellDelay(std::ostream& sdf_file, Instance& instance);
  void outputSdfGraphCellArc(std::ostream& sdf_file, Arc& arc);
  void outputSdfCellArcDelayMap(std::ostream& sdf_file, std::string& source_port_name, std::string& sink_port_name, SdfCellArcDelayMap& cell_arc_delay_map);
  void outputSdfOnlyCellArcList(std::ostream& sdf_file, Instance& instance);
  void outputSdfOnlyCellArc(std::ostream& sdf_file, TimingCellArc& timing_cell_arc);
  void outputSdfCellArc(std::ostream& sdf_file, std::string& source_port_name, std::string& sink_port_name, std::string& condition, TransType input_trans_type,
                        SdfDelay& sdf_delay, SdfDelay* retaining = nullptr);
  void outputSdfTimingCheckList(std::ostream& sdf_file, Instance& instance);
  void outputSdfTimingCheck(std::ostream& sdf_file, Instance& instance, TimingCheckArc& timing_check_arc);
  void outputSdfEdgeTimingCheck(std::ostream& sdf_file, Instance& instance, TimingCheckArc& timing_check_arc, TimingArc& timing_arc, TransType data_trans_type);
  void outputSdfWidthTimingCheck(std::ostream& sdf_file, Instance& instance, TimingCheckArc& timing_check_arc, TimingArc& timing_arc, TransType trans_type);
  void outputSdfPeriodTimingCheck(std::ostream& sdf_file, Instance& instance, TimingCheckArc& timing_check_arc, TimingArc& timing_arc);
};

}  // namespace ista
