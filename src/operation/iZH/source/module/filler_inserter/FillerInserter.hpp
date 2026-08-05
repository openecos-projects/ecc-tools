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

#include "FIModel.hpp"
#include "IdbDesign.h"
#include "IdbLayout.h"
#include "Logger.hpp"
#include "Monitor.hpp"

namespace izh {

#define ZHFI (izh::FillerInserter::getInst())

class FillerInserter
{
 public:
  static void initInst();
  static FillerInserter& getInst();
  static void destroyInst();
  // function
  void insert(std::map<std::string, std::any> config_map);

 private:
  // self
  static FillerInserter* _fi_instance;

  FillerInserter() = default;
  FillerInserter(const FillerInserter& other) = delete;
  FillerInserter(FillerInserter&& other) = delete;
  ~FillerInserter() = default;
  FillerInserter& operator=(const FillerInserter& other) = delete;
  FillerInserter& operator=(FillerInserter&& other) = delete;
  // function
  FIModel initFIModel(std::map<std::string, std::any>& config_map);
  std::vector<std::string> getFillerNameList(std::map<std::string, std::any>& config_map);
  std::vector<std::string> splitFillerNameList(const std::string& filler_name_list_str);
  void initRowList(FIModel& fi_model, idb::IdbLayout* idb_layout);
  void initFillerMasterList(FIModel& fi_model, idb::IdbLayout* idb_layout, const std::vector<std::string>& filler_name_list);
  bool isFillerMaster(idb::IdbCellMaster* idb_cell_master);
  void buildBlockage(FIModel& fi_model, idb::IdbDesign* idb_design);
  void addInstanceBlockage(FIModel& fi_model, idb::IdbInstance* idb_instance);
  void addPlacementBlockage(FIModel& fi_model, idb::IdbDesign* idb_design);
  void addRectBlockage(FIModel& fi_model, idb::IdbRect* idb_rect);
  void addRectBlockage(FIModel& fi_model, int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y);
  void buildAvailableSegmentList(FIModel& fi_model);
  void addFillerCell(FIModel& fi_model, idb::IdbDesign* idb_design);
  void addFillerToSegment(FIModel& fi_model, FIRow& fi_row, FISegment& fi_segment, idb::IdbDesign* idb_design);
  idb::IdbInstance* addFillerInstance(FIModel& fi_model, FIRow& fi_row, FIMaster& fi_master, int32_t begin_site_idx,
                                      idb::IdbDesign* idb_design);
  int32_t getCeilDiv(int32_t dividend, int32_t divisor);
};

}  // namespace izh
