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
 * @project		iplf
 * @file		file_cts.h
 * @date		25/05/2021
 * @version		0.1
 * @description


        Process file.
 *
 */

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "file_soc.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>

#include "IdbEnum.h"
#include "idm.h"
#include "json_parser.h"

using namespace std;

namespace idb {

bool JsonSoc::readFile()
{
  return readJson();
}

bool JsonSoc::saveFileData()
{
  return saveJson();
}

bool JsonSoc::saveJson()
{
  auto path = get_data_path();
  if (path.length() < 4) {
    return false;
  }
  std::string tail_str = path.substr(path.length() - 4);
  if (tail_str != "json") {
    return false;
  }
  std::cout << std::endl << "Begin save feature json, path = " << path << std::endl;

  auto* idb_design = dmInst->get_idb_design();
  auto* idb_layout = dmInst->get_idb_layout();
  if (idb_layout == nullptr || idb_design == nullptr) {
    return false;
  }

  auto rect_to_json = [](IdbRect* rect) {
    json rect_json = json::object();
    if (rect == nullptr) {
      return rect_json;
    }

    rect_json["llx"] = rect->get_low_x();
    rect_json["lly"] = rect->get_low_y();
    rect_json["urx"] = rect->get_high_x();
    rect_json["ury"] = rect->get_high_y();
    rect_json["width"] = rect->get_width();
    rect_json["height"] = rect->get_height();
    rect_json["area"] = rect->get_area();
    return rect_json;
  };

  auto coordinate_to_json = [](IdbCoordinate<int32_t>* coordinate) {
    json coordinate_json = json::object();
    if (coordinate == nullptr) {
      return coordinate_json;
    }

    coordinate_json["x"] = coordinate->get_x();
    coordinate_json["y"] = coordinate->get_y();
    return coordinate_json;
  };


  auto pin_to_json = [&](IdbPin* pin) {
    json pin_json = json::object();
    if (pin == nullptr) {
      return pin_json;
    }

    pin_json["name"] = pin->get_pin_name();
    pin_json["info"] = "";
    pin_json["bounding_box"] = rect_to_json(pin->get_bounding_box());

    return pin_json;
  };

  auto cellmaster_to_json = [&](IdbCellMaster* cell_master) {
    json macro_json = json::object();
    if (cell_master == nullptr) {
      return macro_json;
    }

    macro_json["name"] = cell_master->get_name();
    macro_json["width"] = cell_master->get_width();
    macro_json["height"] = cell_master->get_height();
    macro_json["area"] = cell_master->get_width() * cell_master->get_height();

    return macro_json;
  };

  auto instance_to_json = [&](IdbInstance* instance, int core_id) {
    json instance_json = json::object();
    if (instance == nullptr) {
      return instance_json;
    }
    
    instance_json["core_id"] = core_id;
    instance_json["name"] = instance->get_name();
    instance_json["coordinate"] = coordinate_to_json(instance->get_coordinate());
    instance_json["orient"] =  IdbEnum::GetInstance()->get_site_property()->get_orient_name(instance->get_orient());
    instance_json["bounding_box"] = rect_to_json(instance->get_bounding_box());
    instance_json["cell_master"] = cellmaster_to_json(instance->get_cell_master());
    return instance_json;
  };

  json soc_json = json::object();
  soc_json["file_path"] = path;
  soc_json["design_name"] = idb_design == nullptr ? "" : idb_design->get_design_name();
  soc_json["dbu"] = idb_layout != nullptr && idb_layout->get_units() != nullptr ? idb_layout->get_units()->get_micron_dbu() : 0;

  soc_json["die"] =  rect_to_json(idb_layout->get_die()->get_bounding_box());
  soc_json["core"] = rect_to_json(idb_layout->get_core()->get_bounding_box());

  json io_pins_json = json::object();
  json io_pin_list_json = json::array();
  auto* io_pin_list = idb_design == nullptr ? nullptr : idb_design->get_io_pin_list();
  if (io_pin_list != nullptr) {
    for (auto* pin : io_pin_list->get_pin_list()) {
      io_pin_list_json.push_back(pin_to_json(pin));
    }
  }
  io_pins_json["number"] = io_pin_list == nullptr ? 0 : io_pin_list->get_pin_num();
  io_pins_json["list"] = io_pin_list_json;

  soc_json["io_pins"] = io_pins_json;
  
  json cores_json = json::object();
  json core_list_json = json::array();
  auto* instance_list = idb_design == nullptr ? nullptr : idb_design->get_instance_list();
  int num_cores = 0;
  std::vector<IdbInstance*> sorted_instances = instance_list == nullptr ? std::vector<IdbInstance*>() : instance_list->get_instance_list();
  std::stable_sort(sorted_instances.begin(), sorted_instances.end(), [](IdbInstance* left, IdbInstance* right) {
    if (left == nullptr) {
      return false;
    }
    if (right == nullptr) {
      return true;
    }

    auto* left_box = left->get_bounding_box();
    auto* right_box = right->get_bounding_box();
    if (left_box == nullptr) {
      return false;
    }
    if (right_box == nullptr) {
      return true;
    }

    if (left_box->get_high_y() != right_box->get_high_y()) {
      return left_box->get_high_y() > right_box->get_high_y();
    }
    return left_box->get_low_x() < right_box->get_low_x();
  });

  for (auto inst : sorted_instances) {
    if(inst == nullptr || inst->get_cell_master() == nullptr || !is_exist_harden_core(inst->get_name())) {
      continue;
    }

    core_list_json.push_back(instance_to_json(inst, num_cores));
    num_cores++;
  }

  cores_json["number"] = core_list_json.size();
  cores_json["seleted"] = -1; //default id
  cores_json["list"] = core_list_json;
  soc_json["cores"] = cores_json;

  std::ofstream file_stream(path);
  if (!file_stream.is_open()) {
    return false;
  }
  file_stream << std::setw(4) << soc_json;

  file_stream.close();

  std::cout << std::endl << "Save feature json success, path = " << path << std::endl;
  return true;
}

bool JsonSoc::readJson()
{
  auto path = get_data_path();

  parseJson(path);

  return true;
}

void JsonSoc::parseJson(std::string path)
{
  nlohmann::json json;

  ieda::initJson(path, json);

}

}  // namespace idb
