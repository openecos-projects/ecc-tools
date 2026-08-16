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
 * @File Name: dm_layout.cpp
 * @Brief :
 * @Author : Yell (12112088@qq.com)
 * @Version : 1.0
 * @Creat Date : 2022-04-15
 *
 */

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "idm.h"

namespace idm {

double DataManager::dieAreaUm()
{
  int dbu = _design->get_units()->get_micron_dbu() < 0 ? _layout->get_units()->get_micron_dbu() : _design->get_units()->get_micron_dbu();
  auto* idb_die = _layout->get_die();
  auto die_width = ((double) idb_die->get_width()) / dbu;
  auto die_height = ((double) idb_die->get_height()) / dbu;

  return die_width * die_height;
}

float DataManager::dieUtilization()
{
  uint64_t inst_area = netlistInstArea() + timingInstArea();

  float utilization = ((double) inst_area) / dieAreaUm();

  return utilization;
}

double DataManager::coreAreaUm()
{
  int dbu = _design->get_units()->get_micron_dbu() < 0 ? _layout->get_units()->get_micron_dbu() : _design->get_units()->get_micron_dbu();
  auto idb_core_box = _layout->get_core()->get_bounding_box();
  auto core_width = ((double) idb_core_box->get_width()) / dbu;
  auto core_height = ((double) idb_core_box->get_height()) / dbu;

  return core_width * core_height;
}

float DataManager::coreUtilization()
{
  uint64_t inst_area = netlistInstArea() + timingInstArea();

  float utilization = ((double) inst_area) / coreAreaUm();

  return utilization;
}

IdbRow* DataManager::createRow(string row_name, string site_name, int32_t orig_x, int32_t orig_y, IdbOrient site_orient, int32_t num_x,
                               int32_t num_y, int32_t step_x, int32_t step_y)
{
  return _layout->createRow(row_name, site_name, orig_x, orig_y, site_orient, num_x, num_y, step_x, step_y);
}

}  // namespace idm
