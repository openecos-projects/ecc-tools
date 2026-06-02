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

  auto rect_to_json_values = [](int32_t llx, int32_t lly, int32_t urx, int32_t ury) {
    json rect_json = json::object();
    rect_json["llx"] = llx;
    rect_json["lly"] = lly;
    rect_json["urx"] = urx;
    rect_json["ury"] = ury;
    rect_json["width"] = std::abs(urx - llx);
    rect_json["height"] = std::abs(ury - lly);
    rect_json["area"] = static_cast<uint64_t>(std::abs(urx - llx)) * static_cast<uint64_t>(std::abs(ury - lly));
    return rect_json;
  };

  auto rect_to_json = [&](IdbRect* rect) {
    if (rect == nullptr) {
      return json::object();
    }

    return rect_to_json_values(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
  };

  auto has_real_rect = [](IdbRect* rect) {
    return rect != nullptr && rect->is_init() && (rect->get_width() > 0 || rect->get_height() > 0);
  };

  auto clamp_span_low = [](int32_t center, int32_t span, int32_t low, int32_t high) {
    if (high <= low) {
      return low;
    }

    span = std::max(1, std::min(span, high - low));
    int64_t span_low = static_cast<int64_t>(center) - span / 2;
    if (span_low < low) {
      span_low = low;
    }
    if (span_low + span > high) {
      span_low = high - span;
    }

    return static_cast<int32_t>(span_low);
  };

  auto abs_distance = [](int32_t lhs, int32_t rhs) {
    int64_t distance = static_cast<int64_t>(lhs) - static_cast<int64_t>(rhs);
    return distance < 0 ? -distance : distance;
  };

  auto get_pin_shape_seed = [&](IdbPin* pin, int32_t& center_x, int32_t& center_y, int32_t& width, int32_t& height) {
    if (pin == nullptr) {
      return false;
    }

    auto* rect = pin->get_bounding_box();
    if (has_real_rect(rect)) {
      center_x = rect->get_middle_point_x();
      center_y = rect->get_middle_point_y();
      width = std::max(1, rect->get_width());
      height = std::max(1, rect->get_height());
      return true;
    }

    auto* coordinate = pin->get_average_coordinate();
    if (coordinate != nullptr && !coordinate->is_negative()) {
      center_x = coordinate->get_x();
      center_y = coordinate->get_y();
      width = 1;
      height = 1;
      return true;
    }

    return false;
  };

  auto nearest_die_edge_distance = [&](int32_t x, int32_t y) {
    auto* die = idb_layout->get_die();
    auto* die_box = die == nullptr ? nullptr : die->get_bounding_box();
    if (!has_real_rect(die_box)) {
      return static_cast<int64_t>(0);
    }

    int64_t x_distance = std::min(abs_distance(x, die_box->get_low_x()), abs_distance(x, die_box->get_high_x()));
    int64_t y_distance = std::min(abs_distance(y, die_box->get_low_y()), abs_distance(y, die_box->get_high_y()));
    return std::min(x_distance, y_distance);
  };

  auto project_pin_to_die_edge = [&](IdbPin* pad_pin) {
    json empty_json = json::object();
    auto* die = idb_layout->get_die();
    auto* die_box = die == nullptr ? nullptr : die->get_bounding_box();
    if (!has_real_rect(die_box)) {
      return empty_json;
    }

    int32_t center_x = 0;
    int32_t center_y = 0;
    int32_t width = 1;
    int32_t height = 1;
    if (!get_pin_shape_seed(pad_pin, center_x, center_y, width, height)) {
      return empty_json;
    }

    width = std::min(width, std::max(1, die_box->get_width()));
    height = std::min(height, std::max(1, die_box->get_height()));

    int64_t left_distance = abs_distance(center_x, die_box->get_low_x());
    int64_t right_distance = abs_distance(center_x, die_box->get_high_x());
    int64_t bottom_distance = abs_distance(center_y, die_box->get_low_y());
    int64_t top_distance = abs_distance(center_y, die_box->get_high_y());
    int64_t edge_distance = std::min(std::min(left_distance, right_distance), std::min(bottom_distance, top_distance));

    if (edge_distance == left_distance) {
      int32_t llx = die_box->get_low_x();
      int32_t urx = std::min(die_box->get_high_x(), llx + width);
      int32_t lly = clamp_span_low(center_y, height, die_box->get_low_y(), die_box->get_high_y());
      return rect_to_json_values(llx, lly, urx, lly + height);
    }

    if (edge_distance == right_distance) {
      int32_t urx = die_box->get_high_x();
      int32_t llx = std::max(die_box->get_low_x(), urx - width);
      int32_t lly = clamp_span_low(center_y, height, die_box->get_low_y(), die_box->get_high_y());
      return rect_to_json_values(llx, lly, urx, lly + height);
    }

    if (edge_distance == bottom_distance) {
      int32_t lly = die_box->get_low_y();
      int32_t ury = std::min(die_box->get_high_y(), lly + height);
      int32_t llx = clamp_span_low(center_x, width, die_box->get_low_x(), die_box->get_high_x());
      return rect_to_json_values(llx, lly, llx + width, ury);
    }

    int32_t ury = die_box->get_high_y();
    int32_t lly = std::max(die_box->get_low_y(), ury - height);
    int32_t llx = clamp_span_low(center_x, width, die_box->get_low_x(), die_box->get_high_x());
    return rect_to_json_values(llx, lly, llx + width, ury);
  };

  auto infer_io_pin_bounding_box = [&](IdbPin* io_pin) {
    json empty_json = json::object();
    if (io_pin == nullptr || io_pin->get_net() == nullptr) {
      return empty_json;
    }

    IdbPin* nearest_pad_pin = nullptr;
    int64_t nearest_distance = 0;
    for (auto* inst_pin : io_pin->get_net()->get_instance_pin_list()->get_pin_list()) {
      if (inst_pin == nullptr || inst_pin->get_instance() == nullptr || inst_pin->get_instance()->get_cell_master() == nullptr) {
        continue;
      }

      auto* cell_master = inst_pin->get_instance()->get_cell_master();
      if (!cell_master->is_io_cell()) {
        continue;
      }

      int32_t center_x = 0;
      int32_t center_y = 0;
      int32_t width = 1;
      int32_t height = 1;
      if (!get_pin_shape_seed(inst_pin, center_x, center_y, width, height)) {
        continue;
      }

      int64_t distance = nearest_die_edge_distance(center_x, center_y);
      if (nearest_pad_pin == nullptr || distance < nearest_distance) {
        nearest_pad_pin = inst_pin;
        nearest_distance = distance;
      }
    }

    return nearest_pad_pin == nullptr ? empty_json : project_pin_to_die_edge(nearest_pad_pin);
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
    auto* bounding_box = pin->get_bounding_box();
    auto inferred_box = has_real_rect(bounding_box) ? json::object() : infer_io_pin_bounding_box(pin);
    pin_json["bounding_box"] = inferred_box.empty() ? rect_to_json(bounding_box) : inferred_box;

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
