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
 * @File Name: dm_design_blockage.cpp
 * @Brief :
 * @Author : Yell (12112088@qq.com)
 * @Version : 1.0
 * @Creat Date : 2022-07-19
 *
 */

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "idm.h"

namespace idm {
/**
 * @brief add placement blockage
 *
 * @param llx
 * @param lly
 * @param urx
 * @param ury
 * @return IdbPlacementBlockage*
 */
IdbPlacementBlockage* DataManager::addPlacementBlockage(int32_t llx, int32_t lly, int32_t urx, int32_t ury)
{
  return _design->addPlacementBlockage(llx, lly, urx, ury);
}
/**
 * @brief add placement halo
 *
 * @param instance_name
 * @param distance_top
 * @param distance_bottom
 * @param distance_left
 * @param distance_right
 */
void DataManager::addPlacementHalo(const string& instance_name, int32_t distance_top, int32_t distance_bottom, int32_t distance_left,
                                   int32_t distance_right)
{
  _design->addPlacementHalo(instance_name, distance_top, distance_bottom, distance_left, distance_right);
}
/**
 * @brief remove blockage for except pg net
 *
 */
void DataManager::removeBlockageExceptPGNet()
{
  _design->removeBlockageExceptPGNet();
}

void DataManager::clearBlockage(string type)
{
  _design->clearBlockage(type);
}
/**
 * @brief add routing blockage
 *
 * @param llx
 * @param lly
 * @param urx
 * @param ury
 * @param layers
 * @param is_except_pgnet
 */
void DataManager::addRoutingBlockage(int32_t llx, int32_t lly, int32_t urx, int32_t ury, const std::vector<std::string>& layers,
                                     const bool& is_except_pgnet)
{
  _design->addRoutingBlockage(llx, lly, urx, ury, layers, is_except_pgnet);
}
/**
 * @brief add routing halo
 *
 * @param instance_name
 * @param layers
 * @param distance_top
 * @param distance_bottom
 * @param distance_left
 * @param distance_right
 * @param is_except_pgnet
 */
void DataManager::addRoutingHalo(const string& instance_name, const std::vector<std::string>& layers, int32_t distance_top,
                                 int32_t distance_bottom, int32_t distance_left, int32_t distance_right, const bool& is_except_pgnet)
{
  _design->addRoutingHalo(instance_name, layers, distance_top, distance_bottom, distance_left, distance_right, is_except_pgnet);
}

}  // namespace idm
