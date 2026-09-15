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
#include "FillerInserter.hpp"

#include "IdbBlockages.h"
#include "IdbTerm.h"
#include "Utility.hpp"
#include "idm.h"

namespace izh {

// public

void FillerInserter::initInst()
{
  if (_fi_instance == nullptr) {
    _fi_instance = new FillerInserter();
  }
}

FillerInserter& FillerInserter::getInst()
{
  if (_fi_instance == nullptr) {
    ZHLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_fi_instance;
}

void FillerInserter::destroyInst()
{
  if (_fi_instance != nullptr) {
    delete _fi_instance;
    _fi_instance = nullptr;
  }
}

// function

void FillerInserter::insert(std::map<std::string, std::any> config_map)
{
  Monitor monitor;
  ZHLOG.info(Loc::current(), "Starting...");

  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(0), "ZH insertFiller");

  FIModel fi_model = initFIModel(config_map);
  idb::IdbDesign* idb_design = dmInst->get_idb_design();

  buildBlockage(fi_model, idb_design);
  buildAvailableSegmentList(fi_model);
  addFillerCell(fi_model, idb_design);

  ZHLOG.info(Loc::current(), "Inserted ", fi_model.get_inserted_filler_num(), " filler cells");

  ZHLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

FIModel FillerInserter::initFIModel(std::map<std::string, std::any>& config_map)
{
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  if (idb_layout == nullptr || idb_design == nullptr) {
    ZHLOG.error(Loc::current(), "The idb layout or design is null!");
  }

  FIModel fi_model;
  fi_model.set_min_filler_site_count(1);
  if (Utility::exist(config_map, std::string("-min_filler_width"))) {
    fi_model.set_min_filler_site_count(ZHUTIL.getConfigValue<int32_t>(config_map, "-min_filler_width", 1));
  }
  if (fi_model.get_min_filler_site_count() <= 0) {
    ZHLOG.error(Loc::current(), "The min_filler_width must be greater than 0!");
  }

  std::vector<std::string> filler_name_list = getFillerNameList(config_map);
  initRowList(fi_model, idb_layout);
  initFillerMasterList(fi_model, idb_layout, filler_name_list);

  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(1), "filler_master_num: ", fi_model.get_filler_master_list().size());
  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(1), "row_num: ", fi_model.get_row_list().size());
  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(1), "min_filler_width: ", fi_model.get_min_filler_site_count());

  return fi_model;
}

std::vector<std::string> FillerInserter::getFillerNameList(std::map<std::string, std::any>& config_map)
{
  std::vector<std::string> filler_name_list;
  if (Utility::exist(config_map, std::string("-filler"))) {
    filler_name_list = splitFillerNameList(ZHUTIL.getConfigValue<std::string>(config_map, "-filler", ""));
  }
  return filler_name_list;
}

std::vector<std::string> FillerInserter::splitFillerNameList(const std::string& filler_name_list_str)
{
  std::string normalized_str = filler_name_list_str;
  for (char& str_char : normalized_str) {
    if (std::string(" \t\r\n,;{}[]").find(str_char) != std::string::npos) {
      str_char = ' ';
    }
  }

  std::vector<std::string> filler_name_list;
  std::stringstream str_stream(normalized_str);
  std::string filler_name;
  while (str_stream >> filler_name) {
    filler_name_list.push_back(filler_name);
  }
  return filler_name_list;
}

void FillerInserter::initRowList(FIModel& fi_model, idb::IdbLayout* idb_layout)
{
  if (idb_layout == nullptr || idb_layout->get_rows() == nullptr) {
    ZHLOG.error(Loc::current(), "The idb row list is null!");
  }

  std::vector<FIRow> fi_row_list;
  int32_t row_idx = 0;
  for (idb::IdbRow* idb_row : idb_layout->get_rows()->get_row_list()) {
    if (idb_row == nullptr || idb_row->get_site() == nullptr || idb_row->get_original_coordinate() == nullptr) {
      continue;
    }
    if (!idb_row->is_horizontal()) {
      ZHLOG.warn(Loc::current(), "Skip non-horizontal row: ", idb_row->get_name());
      continue;
    }
    int32_t site_width = idb_row->get_step_x() > 0 ? idb_row->get_step_x() : idb_row->get_site()->get_width();
    int32_t row_height = idb_row->get_site()->get_height();
    int32_t site_count = idb_row->get_row_num_x();
    if (site_width <= 0 || row_height <= 0 || site_count <= 0) {
      ZHLOG.warn(Loc::current(), "Skip invalid row: ", idb_row->get_name());
      continue;
    }

    FIRow fi_row;
    fi_row.set_row_idx(row_idx++);
    fi_row.set_origin_x(idb_row->get_original_coordinate()->get_x());
    fi_row.set_origin_y(idb_row->get_original_coordinate()->get_y());
    fi_row.set_site_width(site_width);
    fi_row.set_row_height(row_height);
    fi_row.set_site_count(site_count);
    fi_row.set_orient(idb_row->get_orient());
    fi_row.initSiteAvailableList();
    fi_row_list.push_back(fi_row);
  }
  std::sort(fi_row_list.begin(), fi_row_list.end(), [](const FIRow& a, const FIRow& b) {
    if (a.get_origin_y() == b.get_origin_y()) {
      return a.get_origin_x() < b.get_origin_x();
    }
    return a.get_origin_y() < b.get_origin_y();
  });
  fi_model.set_row_list(fi_row_list);
  if (fi_model.get_row_list().empty()) {
    ZHLOG.error(Loc::current(), "The filler row list is empty!");
  }
}

void FillerInserter::initFillerMasterList(FIModel& fi_model, idb::IdbLayout* idb_layout, const std::vector<std::string>& filler_name_list)
{
  if (idb_layout == nullptr || idb_layout->get_cell_master_list() == nullptr) {
    ZHLOG.error(Loc::current(), "The idb cell master list is null!");
  }

  int32_t site_width = fi_model.get_row_list().front().get_site_width();
  std::vector<idb::IdbCellMaster*> idb_filler_master_list;
  if (filler_name_list.empty()) {
    for (idb::IdbCellMaster* idb_cell_master : idb_layout->get_cell_master_list()->get_cell_master()) {
      if (isFillerMaster(idb_cell_master)) {
        idb_filler_master_list.push_back(idb_cell_master);
      }
    }
  } else {
    for (const std::string& filler_name : filler_name_list) {
      idb::IdbCellMaster* idb_cell_master = idb_layout->get_cell_master_list()->find_cell_master(filler_name);
      if (idb_cell_master == nullptr) {
        ZHLOG.error(Loc::current(), "Cannot find filler master: ", filler_name);
      }
      if (!isFillerMaster(idb_cell_master)) {
        ZHLOG.error(Loc::current(), "The master is not a core filler: ", filler_name);
      }
      idb_filler_master_list.push_back(idb_cell_master);
    }
  }

  std::vector<FIMaster> fi_master_list;
  for (idb::IdbCellMaster* idb_filler_master : idb_filler_master_list) {
    int32_t master_width = static_cast<int32_t>(idb_filler_master->get_width());
    if (master_width <= 0 || master_width % site_width != 0) {
      ZHLOG.error(Loc::current(), "The filler width is not aligned to row site width: ", idb_filler_master->get_name());
    }
    FIMaster fi_master;
    fi_master.set_master(idb_filler_master);
    fi_master.set_name(idb_filler_master->get_name());
    fi_master.set_width(master_width);
    fi_master.set_site_count(master_width / site_width);
    fi_master_list.push_back(fi_master);
  }
  std::sort(fi_master_list.begin(), fi_master_list.end(), [](const FIMaster& a, const FIMaster& b) { return a.get_width() > b.get_width(); });
  fi_model.set_filler_master_list(fi_master_list);
  if (fi_model.get_filler_master_list().empty()) {
    ZHLOG.error(Loc::current(), "The filler master list is empty!");
  }
}

bool FillerInserter::isFillerMaster(idb::IdbCellMaster* idb_cell_master)
{
  if (idb_cell_master == nullptr || !idb_cell_master->is_core() || idb_cell_master->get_width() == 0 || idb_cell_master->get_height() == 0) {
    return false;
  }
  std::vector<idb::IdbTerm*> term_list = idb_cell_master->get_term_list();
  if (term_list.empty()) {
    return idb_cell_master->is_core_filler();
  }
  for (idb::IdbTerm* idb_term : term_list) {
    if (idb_term == nullptr) {
      continue;
    }
    if (idb_term->get_type() != idb::IdbConnectType::kPower && idb_term->get_type() != idb::IdbConnectType::kGround) {
      return false;
    }
  }
  return true;
}

void FillerInserter::buildBlockage(FIModel& fi_model, idb::IdbDesign* idb_design)
{
  if (idb_design == nullptr || idb_design->get_instance_list() == nullptr) {
    ZHLOG.error(Loc::current(), "The idb instance list is null!");
  }

  for (FIRow& fi_row : fi_model.get_row_list()) {
    fi_row.initSiteAvailableList();
  }
  for (idb::IdbInstance* idb_instance : idb_design->get_instance_list()->get_instance_list()) {
    addInstanceBlockage(fi_model, idb_instance);
  }
  addPlacementBlockage(fi_model, idb_design);
}

void FillerInserter::addInstanceBlockage(FIModel& fi_model, idb::IdbInstance* idb_instance)
{
  if (idb_instance == nullptr || idb_instance->get_cell_master() == nullptr || idb_instance->is_unplaced()
      || idb_instance->get_status() == idb::IdbPlacementStatus::kNone) {
    return;
  }
  idb::IdbRect* idb_rect = idb_instance->get_bounding_box();
  if (idb_rect == nullptr || idb_rect->get_width() <= 0 || idb_rect->get_height() <= 0) {
    idb_instance->set_bounding_box();
    idb_rect = idb_instance->get_bounding_box();
  }
  addRectBlockage(fi_model, idb_rect);
}

void FillerInserter::addPlacementBlockage(FIModel& fi_model, idb::IdbDesign* idb_design)
{
  if (idb_design == nullptr || idb_design->get_blockage_list() == nullptr) {
    return;
  }
  for (idb::IdbBlockage* idb_blockage : idb_design->get_blockage_list()->get_blockage_list()) {
    if (idb_blockage == nullptr || !idb_blockage->is_palcement_blockage()) {
      continue;
    }
    for (idb::IdbRect* idb_rect : idb_blockage->get_rect_list()) {
      addRectBlockage(fi_model, idb_rect);
    }
  }
}

void FillerInserter::addRectBlockage(FIModel& fi_model, idb::IdbRect* idb_rect)
{
  if (idb_rect == nullptr) {
    return;
  }
  addRectBlockage(fi_model, idb_rect->get_low_x(), idb_rect->get_low_y(), idb_rect->get_high_x(), idb_rect->get_high_y());
}

void FillerInserter::addRectBlockage(FIModel& fi_model, int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y)
{
  if (ur_x <= ll_x || ur_y <= ll_y) {
    return;
  }
  for (FIRow& fi_row : fi_model.get_row_list()) {
    if (ur_y <= fi_row.get_origin_y() || ll_y >= fi_row.get_end_y()) {
      continue;
    }
    int32_t clipped_ll_x = std::max(ll_x, fi_row.get_origin_x());
    int32_t clipped_ur_x = std::min(ur_x, fi_row.get_end_x());
    if (clipped_ur_x <= clipped_ll_x) {
      continue;
    }
    int32_t begin_site_idx = (clipped_ll_x - fi_row.get_origin_x()) / fi_row.get_site_width();
    int32_t end_site_idx = getCeilDiv(clipped_ur_x - fi_row.get_origin_x(), fi_row.get_site_width()) - 1;
    fi_row.blockSiteRange(begin_site_idx, end_site_idx);
  }
}

void FillerInserter::buildAvailableSegmentList(FIModel& fi_model)
{
  for (FIRow& fi_row : fi_model.get_row_list()) {
    fi_row.buildAvailableSegmentList();
  }
}

void FillerInserter::addFillerCell(FIModel& fi_model, idb::IdbDesign* idb_design)
{
  for (FIRow& fi_row : fi_model.get_row_list()) {
    for (FISegment& fi_segment : fi_row.get_available_segment_list()) {
      addFillerToSegment(fi_model, fi_row, fi_segment, idb_design);
    }
  }
}

void FillerInserter::addFillerToSegment(FIModel& fi_model, FIRow& fi_row, FISegment& fi_segment, idb::IdbDesign* idb_design)
{
  int32_t left_site_idx = fi_segment.get_begin_site_idx();
  int32_t right_site_idx = fi_segment.get_end_site_idx();
  while (right_site_idx - left_site_idx + 1 >= fi_model.get_min_filler_site_count()) {
    int32_t free_site_count = right_site_idx - left_site_idx + 1;
    bool added = false;
    for (FIMaster& fi_master : fi_model.get_filler_master_list()) {
      int32_t filler_site_count = fi_master.get_site_count();
      int32_t remain_site_count = free_site_count - filler_site_count;
      if (filler_site_count <= 0 || remain_site_count < 0) {
        continue;
      }
      if (remain_site_count < fi_model.get_min_filler_site_count() && remain_site_count != 0) {
        continue;
      }
      addFillerInstance(fi_model, fi_row, fi_master, left_site_idx, idb_design);
      left_site_idx += filler_site_count;
      added = true;
      break;
    }
    if (!added) {
      break;
    }
  }
}

idb::IdbInstance* FillerInserter::addFillerInstance(FIModel& fi_model, FIRow& fi_row, FIMaster& fi_master, int32_t begin_site_idx, idb::IdbDesign* idb_design)
{
  if (idb_design == nullptr || fi_master.get_master() == nullptr) {
    ZHLOG.error(Loc::current(), "Cannot create filler instance because design or master is null!");
  }
  int32_t inst_x = fi_row.get_origin_x() + begin_site_idx * fi_row.get_site_width();
  int32_t inst_y = fi_row.get_origin_y();
  idb::IdbOrient inst_orient = fi_row.get_orient() == idb::IdbOrient::kNone ? idb::IdbOrient::kN_R0 : fi_row.get_orient();
  std::string inst_name = idb_design->makeUniqueInstanceName(ZHUTIL.getString(fi_master.get_name(), "_", fi_master.get_inserted_num()));
  idb::IdbInstance* idb_instance = idb_design->createInstance(inst_name, fi_master.get_name(), idb::IdbInstanceType::kDist, idb::IdbPlacementStatus::kPlaced,
                                                              inst_orient, inst_x, inst_y, idb::IdbCreatePolicy::kErrorIfExists);
  if (idb_instance == nullptr) {
    ZHLOG.error(Loc::current(), "Create filler instance failed: ", inst_name);
  }
  fi_master.addInsertedNum();
  fi_model.addInsertedFillerNum();
  return idb_instance;
}

int32_t FillerInserter::getCeilDiv(int32_t dividend, int32_t divisor)
{
  if (divisor <= 0) {
    ZHLOG.error(Loc::current(), "The divisor must be greater than 0!");
  }
  return dividend <= 0 ? 0 : (dividend + divisor - 1) / divisor;
}

FillerInserter* FillerInserter::_fi_instance = nullptr;

}  // namespace izh
