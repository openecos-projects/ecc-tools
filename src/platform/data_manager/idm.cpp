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
 * @File Name: data_manager.cpp
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

#include <cassert>
namespace idm {

DataManager* DataManager::_instance = nullptr;

bool DataManager::initConfig(string config_path)
{
  return _config.initConfig(config_path);
}

bool DataManager::init(string config_path)
{
  if (_idb_builder == nullptr) {
    _idb_builder = new IdbBuilder();
  }

  if (!initConfig(config_path)) {
    return false;
  }

  if (!initLef(_config.get_lef_paths())) {
    return false;
  }

  if (!initDef(_config.get_def_path())) {
    return false;
  }

  return true;
}

void DataManager::reset()
{
  resetData();
  _config = DataConfig();
}

void DataManager::resetData()
{
  delete _idb_builder;
  _idb_builder = nullptr;

  _idb_def_service = nullptr;
  _idb_lef_service = nullptr;
  _design = nullptr;
  _layout = nullptr;
}

bool DataManager::readLef(string config_path)
{
  if (_idb_builder == nullptr) {
    _idb_builder = new IdbBuilder();
  }

  if (!initConfig(config_path)) {
    return false;
  }

  // tech lef
  if (!initLef(std::vector<std::string>{_config.get_tech_lef_path()}, true)) {
    return false;
  }
  // lef
  if (!initLef(_config.get_lef_paths())) {
    return false;
  }

  return true;
}

bool DataManager::readLef(vector<string> lef_paths, bool b_techlef)
{
  if (_idb_builder == nullptr) {
    _idb_builder = new IdbBuilder();
  }

  if (!initLef(lef_paths, b_techlef)) {
    return false;
  }

  return true;
}

void DataManager::write_placement_back(const float* x, const float* y, int len)
{
  // std::vector<ContestParser::Instance*> inst_list;
  int i = 0;
  printf("write_placement_back start!!! Db address is %p\n", this);
  printf("write_placement_back start!!! idb_design address is %p\n", this->get_idb_design());
  if (x == nullptr || y == nullptr || len <= 0) {
    std::cout << "WriteBack placement finished!!" << std::endl;
    return;
  }
  auto const& row_list = this->get_idb_layout()->get_rows()->get_row_list();
  for (auto name : this->get_idb_design()->m_instID2Name) {
    // std::string name;
    if (i >= len) {
      break;
    }

    auto inst = this->get_idb_design()->get_instance_list()->find_instance(name);
    if (inst == nullptr) {
      i++;
      continue;
    }
    // int node_id = m_mNodeName2Index.find(name)->second;
    float xx = x[i];
    float yy = y[i];
    auto orient = IdbOrient::kN_R0;
    auto* cell_master = inst->get_cell_master();
    if (cell_master == nullptr) {
      i++;
      continue;
    }
    if (!cell_master->is_block()) {
      if (row_list.empty()) {
        i++;
        continue;
      }
      auto target_y = static_cast<int32_t>(yy);
      IdbRow* matched_row = nullptr;
      int32_t matched_y = 0;
      IdbRow* last_row = nullptr;
      int32_t last_y = 0;
      for (auto* row : row_list) {
        int32_t row_y = row->get_original_coordinate()->get_y();
        if (last_row == nullptr || row_y > last_y) {
          last_row = row;
          last_y = row_y;
        }
        if (row_y >= target_y && (matched_row == nullptr || row_y < matched_y)) {
          matched_row = row;
          matched_y = row_y;
        }
      }
      if (matched_row == nullptr) {
        matched_row = last_row;
      }
      if (matched_row == nullptr) {
        i++;
        continue;
      }
      orient = matched_row->get_orient();
    } else {
      orient = inst->get_orient();
    }
    inst->set_orient(orient, false);
    inst->set_coodinate(static_cast<int32_t>(xx), static_cast<int32_t>(yy), false);
    inst->set_status(IdbPlacementStatus::kPlaced);
    inst->set_bounding_box();
    for (auto* pin : inst->get_pin_list()->get_pin_list()) {
      auto* term = pin->get_term();
      if (term == nullptr || term->get_port_number() <= 0) {
        continue;
      }
      pin->set_average_coordinate(static_cast<int32_t>(xx) + term->get_average_position().get_x(),
                                  static_cast<int32_t>(yy) + term->get_average_position().get_y());
      pin->set_bounding_box();
      pin->set_grid_coordinate();
    }
    inst->set_halo_coodinate();
    inst->set_obs_box_list();
    i++;
    // flag = true;
  }
  // output hpwl
  std::cout << "WriteBack placement finished!!" << std::endl;
  // std::cout << "WriteBack double finished, Current Contest DB Total HPWL : " << contest_db->obtainTotalHPWL() <<
  // std::endl;

  return;
}
bool DataManager::readDef(string path)
{
  if (_idb_builder == nullptr || _idb_lef_service == nullptr || _layout == nullptr) {
    return false;
  }

  if (!initDef(path)) {
    return false;
  }

  return true;
}

bool DataManager::readVerilog(string path, string top_module)
{
  if (_idb_builder == nullptr || _idb_lef_service == nullptr || _layout == nullptr) {
    return false;
  }

  if (!initVerilog(path, top_module)) {
    return false;
  }

  return true;
}

int DataManager::get_routing_layer_1st()
{
  string routing_layer_1st = _config.get_routing_layer_1st();
  auto layout = get_idb_layout();
  auto layer_list = layout->get_layers();

  auto layer_find = layer_list->find_layer(routing_layer_1st);

  return layer_find != nullptr ? layer_find->get_id() : 0;
}

}  // namespace idm
