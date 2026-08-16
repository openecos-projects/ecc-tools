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
/**
 * @File Name: dm_design_inst.cpp
 * @Brief :
 * @Author : Yell (12112088@qq.com)
 * @Version : 1.0
 * @Creat Date : 2022-04-15
 *
 */

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "utility/logger/Logger.hpp"
#include "idm.h"

namespace idm {

/**
 * @Brief :
 * @return int64_t
 */
double DataManager::instanceArea(IdbInstanceType type)
{
  uint64_t inst_area = 0;
  IdbInstanceList* inst_list = _design->get_instance_list();
  if (inst_list == nullptr) {
    return inst_area;
  }

  for (auto inst : inst_list->get_instance_list()) {
    if (type == IdbInstanceType::kMax || inst->get_type() == type) {
      uint64_t area = inst->get_cell_master()->get_width() * inst->get_cell_master()->get_height();
      inst_area += area;
      continue;
    }

    if (type == IdbInstanceType::kNetlist && IdbInstanceType::kNone == inst->get_type()) {
      uint64_t area = inst->get_cell_master()->get_width() * inst->get_cell_master()->get_height();
      inst_area += area;
      continue;
    }
  }

  int dbu = _design->get_units()->get_micron_dbu() < 0 ? _layout->get_units()->get_micron_dbu() : _design->get_units()->get_micron_dbu();
  return ((double) inst_area) / (dbu * dbu);
}

double DataManager::distInstArea()
{
  return instanceArea(IdbInstanceType::kDist);
}

double DataManager::netlistInstArea()
{
  return instanceArea(IdbInstanceType::kNetlist);
}

double DataManager::timingInstArea()
{
  return instanceArea(IdbInstanceType::kTiming);
}
/**
 * @Brief : create instance
 * @param  inst_name instance name created
 * @param  cell_master_name name of cell master to describe property
 * @param  coord_x coordinate of x
 * @param  coord_y coordinate of y
 * @param  orient Specify orientation
 * @param  type Specifies the source of the instance
 * @param  status Specifies the instance placement status
 * @return IdbInstance*
 */
IdbInstance* DataManager::createInstance(string inst_name, string cell_master_name, int32_t coord_x, int32_t coord_y, IdbOrient orient,
                                         IdbInstanceType type, IdbPlacementStatus status)
{
  return _design->createInstance(inst_name, cell_master_name, type, status, orient, coord_x, coord_y);
}

/**
 * @brief
 *
 * @param inst_name
 * @param x
 * @param y
 * @param orient_name
 * @param cell_master_name
 * @param source
 * @param placement_status one of fixed, placed, unplaced, or preserve
 * @param create_if_missing create a missing instance when true
 * @return true
 * @return false
 */
bool DataManager::placeInst(string inst_name, int32_t x, int32_t y, string orient_name, string cell_master_name, string source,
                            string placement_status_name, bool create_if_missing)
{
  if (_design == nullptr || _layout == nullptr || _design->get_instance_list() == nullptr || _layout->get_cell_master_list() == nullptr
      || inst_name.empty()) {
    return false;
  }

  IdbInstance* instance = _design->get_instance_list()->find_instance(inst_name);
  const bool is_new_instance = instance == nullptr;
  if (is_new_instance && !create_if_missing) {
    return false;
  }

  IdbCellMaster* cellmaster = nullptr;
  if (is_new_instance) {
    if (cell_master_name.empty()) {
      return false;
    }
    cellmaster = _layout->get_cell_master_list()->find_cell_master(cell_master_name);
  } else {
    cellmaster = instance->get_cell_master();
    if (cellmaster == nullptr || (!cell_master_name.empty() && cellmaster->get_name() != cell_master_name)) {
      return false;
    }
  }

  IdbPlacementStatus placement_status = IdbPlacementStatus::kNone;
  if (placement_status_name == "fixed") {
    placement_status = IdbPlacementStatus::kFixed;
  } else if (placement_status_name == "placed") {
    placement_status = IdbPlacementStatus::kPlaced;
  } else if (placement_status_name == "unplaced") {
    placement_status = IdbPlacementStatus::kUnplaced;
  } else if (placement_status_name == "preserve" && !is_new_instance) {
    placement_status = instance->get_status();
    if (placement_status == IdbPlacementStatus::kFixed || placement_status == IdbPlacementStatus::kCover) {
      return false;
    }
  } else {
    return false;
  }

  // GUI move commands preserve an existing instance's orientation. This keeps
  // the direct placement API usable without copying orientation into Geometry.
  IdbOrient orient = orient_name.empty() && placement_status_name == "preserve" && !is_new_instance
                         ? instance->get_orient()
                         : IdbEnum::GetInstance()->get_site_property()->get_orient_value(orient_name);
  if (cellmaster == nullptr || orient == IdbOrient::kNone) {
    ECCLOG.warn(ecc::Loc::current(), "[IDM Error] inst_name = ", inst_name, " cell_master_name = ", cell_master_name, " orient_name = ", orient_name);
    return false;
  }

  if (!source.empty() && source != "NONE" && source != "NETLIST" && source != "DIST" && source != "USER" && source != "TIMING"
      && source != "TEST") {
    return false;
  }

  int32_t width = cellmaster->get_width();
  int32_t height = cellmaster->get_height();
  int32_t urx;
  int32_t ury;
  if (orient == IdbOrient::kN_R0 || orient == IdbOrient::kS_R180 || orient == IdbOrient::kFN_MY || orient == IdbOrient::kFS_MX) {
    urx = x + width;
    ury = y + height;
  } else {
    urx = x + height;
    ury = y + width;
  }

  if (cellmaster->is_endcap()) {
    if (!isOnDieBoundary(x, y, urx, ury, orient)) {
      ECCLOG.warn(ecc::Loc::current(), "Instance ", inst_name, " placement information has a problem.");
      return false;
    }
  } else if (cellmaster->is_pad() || cellmaster->is_pad_filler()) {
    bool can_place = checkInstPlacer(x, y, urx, ury, orient);
    if (!can_place) {
      ECCLOG.warn(ecc::Loc::current(), "Instance ", inst_name, " placement information has a problem.");
      return false;
    }
  }

  if (is_new_instance) {
    instance = _design->createInstance(inst_name, cellmaster->get_name(), IdbInstanceType::kNone, placement_status, orient, x, y);
    if (instance == nullptr) {
      return false;
    }
  } else if (!_design->placeInstance(inst_name, x, y, orient, placement_status)) {
    return false;
  }

  if (!source.empty()) {
    instance->set_type(source);
  }

  return true;
}

}  // namespace idm
