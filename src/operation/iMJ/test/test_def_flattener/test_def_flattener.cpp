// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "DefFlattener.hpp"
#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbSpecialNet.h"
#include "idm.h"

#include <array>

int main()
{
  std::filesystem::path data_directory_path = std::filesystem::path(__FILE__).parent_path() / "data";
  std::vector<std::string> tech_lef_path_list = {(data_directory_path / "tech.lef").string()};
  std::vector<std::string> cell_lef_path_list = {(data_directory_path / "cell.lef").string()};
  std::filesystem::path top_def_path = data_directory_path / "top.def.in";
  std::filesystem::path top_orient_def_path = data_directory_path / "top_orient.def.in";
  std::filesystem::path top_graph_def_path = data_directory_path / "top_graph.def.in";
  std::string hierarchy_def_path_list = (data_directory_path / "pass.def.in").string() + " "
                                      + (data_directory_path / "mid.def.in").string() + " "
                                      + (data_directory_path / "child.def.in").string();
  std::string orient_hierarchy_def_path_list = (data_directory_path / "mid.def.in").string() + " "
                                             + (data_directory_path / "child.def.in").string();
  std::string graph_hierarchy_def_path_list = (data_directory_path / "alt.def.in").string() + " "
                                            + (data_directory_path / "child.def.in").string() + " "
                                            + (data_directory_path / "mid.def.in").string();

  std::array<idb::IdbOrient, 8> orient_list = {idb::IdbOrient::kN_R0,  idb::IdbOrient::kW_R90,
                                                 idb::IdbOrient::kS_R180, idb::IdbOrient::kE_R270,
                                                 idb::IdbOrient::kFN_MY,  idb::IdbOrient::kFS_MX,
                                                 idb::IdbOrient::kFW_MX90, idb::IdbOrient::kFE_MY90};
  std::array<std::pair<int32_t, int32_t>, 8> expected_coordinate_list
      = {std::make_pair(11000, 22000), std::make_pair(14000, 21000), std::make_pair(19000, 24000),
         std::make_pair(12000, 29000), std::make_pair(19000, 22000), std::make_pair(11000, 24000),
         std::make_pair(12000, 21000), std::make_pair(14000, 29000)};
  for (int32_t orient_idx = 0; orient_idx < static_cast<int32_t>(orient_list.size()); orient_idx++) {
    imj::DFTransform transform;
    transform.set_instance_transform(orient_list[orient_idx], 10000, 20000, 10000, 6000, 0, 0);
    idb::IdbCoordinate<int32_t> coordinate = transform.get_transformed_coordinate(1000, 2000);
    if (coordinate.get_x() != expected_coordinate_list[orient_idx].first || coordinate.get_y() != expected_coordinate_list[orient_idx].second
        || transform.get_composed_orient(idb::IdbOrient::kN_R0) != orient_list[orient_idx]) {
      return 1;
    }
  }

  dmInst->reset();
  if (!dmInst->readLef(tech_lef_path_list, true) || !dmInst->readLef(cell_lef_path_list) || !dmInst->readDef(top_def_path.string())) {
    dmInst->reset();
    return 1;
  }

  idb::IdbDesign* design = dmInst->get_idb_design();
  if (design == nullptr || design->get_instance_list()->find_instance("u_child") == nullptr) {
    dmInst->reset();
    return 1;
  }

  std::map<std::string, std::any> invalid_config_map;
  invalid_config_map["-hierarchy"] = (data_directory_path / "missing.def.in").string();
  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
  bool is_invalid_error_caught = false;
  imj::DefFlattener::initInst();
  try {
    MJDF.flatten(invalid_config_map);
  } catch (const std::runtime_error&) {
    is_invalid_error_caught = true;
  }
  imj::DefFlattener::destroyInst();
  unsetenv("ECC_LOGGER_THROW_ON_ERROR");
  if (!is_invalid_error_caught || design->get_instance_list()->find_instance("u_child") == nullptr) {
    dmInst->reset();
    return 1;
  }

  std::map<std::string, std::any> config_map;
  config_map["-hierarchy"] = hierarchy_def_path_list;
  imj::DefFlattener::initInst();
  MJDF.flatten(config_map);
  imj::DefFlattener::destroyInst();

  idb::IdbInstance* child_instance = design->get_instance_list()->find_instance("u_child");
  idb::IdbInstance* pass_instance = design->get_instance_list()->find_instance("u_pass");
  idb::IdbInstance* leaf_instance = design->get_instance_list()->find_instance("u_child__u_mid__u_leaf");
  if (child_instance != nullptr || pass_instance != nullptr || leaf_instance == nullptr || leaf_instance->get_coordinate()->get_x() != 33000
      || leaf_instance->get_coordinate()->get_y() != 42500 || leaf_instance->get_orient() != idb::IdbOrient::kW_R90) {
    dmInst->reset();
    return 1;
  }

  idb::IdbNet* top_in_net = design->get_net_list()->find_net("top_in");
  idb::IdbNet* top_out_net = design->get_net_list()->find_net("top_out");
  idb::IdbPin* leaf_input_pin = leaf_instance->get_pin_by_term("A");
  idb::IdbPin* leaf_output_pin = leaf_instance->get_pin_by_term("Y");
  if (top_in_net == nullptr || top_out_net == nullptr || leaf_input_pin == nullptr || leaf_output_pin == nullptr
      || leaf_input_pin->get_net() != top_in_net || leaf_output_pin->get_net() != top_out_net
      || top_in_net->get_wire_list()->get_wire_list().empty() || top_out_net->get_wire_list()->get_wire_list().empty()) {
    dmInst->reset();
    return 1;
  }

  idb::IdbNet* top_pass_in_net = design->get_net_list()->find_net("top_pass_in");
  idb::IdbNet* top_pass_out_net = design->get_net_list()->find_net("top_pass_out");
  if (top_pass_in_net == nullptr || top_pass_out_net != nullptr || top_pass_in_net->get_wire_list()->get_wire_list().empty()) {
    dmInst->reset();
    return 1;
  }

  idb::IdbSpecialNet* vdd_pass_a_net = design->get_special_net_list()->find_net("VDD_PASS_A");
  idb::IdbSpecialNet* vdd_pass_b_net = design->get_special_net_list()->find_net("VDD_PASS_B");
  if (vdd_pass_a_net == nullptr || vdd_pass_b_net != nullptr || vdd_pass_a_net->get_wire_list()->get_wire_list().empty()) {
    dmInst->reset();
    return 1;
  }

  idb::IdbSpecialNet* vdd_net = design->get_special_net_list()->find_net("VDD");
  idb::IdbSpecialNet* vss_net = design->get_special_net_list()->find_net("VSS");
  idb::IdbPin* leaf_vdd_pin = leaf_instance->get_pin_by_term("VPWR");
  idb::IdbPin* leaf_vss_pin = leaf_instance->get_pin_by_term("VGND");
  if (vdd_net == nullptr || vss_net == nullptr || leaf_vdd_pin == nullptr || leaf_vss_pin == nullptr
      || leaf_vdd_pin->get_special_net() != vdd_net || leaf_vss_pin->get_special_net() != vss_net
      || vdd_net->get_wire_list()->get_wire_list().size() < 2 || vss_net->get_wire_list()->get_wire_list().size() < 2) {
    dmInst->reset();
    return 1;
  }

  std::filesystem::path output_def_path = std::filesystem::temp_directory_path() / "test_imj_def_flattener.def";
  std::filesystem::remove(output_def_path);
  bool is_written = dmInst->saveDef(output_def_path.string());
  bool is_output_valid = std::filesystem::exists(output_def_path) && std::filesystem::file_size(output_def_path) > 0;
  dmInst->reset();
  if (!is_written || !is_output_valid || !dmInst->readLef(tech_lef_path_list, true) || !dmInst->readLef(cell_lef_path_list)
      || !dmInst->readDef(output_def_path.string())) {
    std::filesystem::remove(output_def_path);
    dmInst->reset();
    return 1;
  }
  design = dmInst->get_idb_design();
  if (design == nullptr || design->get_instance_list()->find_instance("u_child") != nullptr
      || design->get_instance_list()->find_instance("u_pass") != nullptr
      || design->get_instance_list()->find_instance("u_child__u_mid__u_leaf") == nullptr
      || design->get_net_list()->find_net("top_pass_in") == nullptr || design->get_net_list()->find_net("top_pass_out") != nullptr
      || design->get_special_net_list()->find_net("VDD_PASS_A") == nullptr
      || design->get_special_net_list()->find_net("VDD_PASS_B") != nullptr) {
    std::filesystem::remove(output_def_path);
    dmInst->reset();
    return 1;
  }
  std::filesystem::remove(output_def_path);
  dmInst->reset();

  if (!dmInst->readLef(tech_lef_path_list, true) || !dmInst->readLef(cell_lef_path_list) || !dmInst->readDef(top_graph_def_path.string())) {
    dmInst->reset();
    return 1;
  }
  std::map<std::string, std::any> graph_config_map;
  graph_config_map["-hierarchy"] = graph_hierarchy_def_path_list;
  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
  bool is_graph_error_caught = false;
  imj::DefFlattener::initInst();
  try {
    MJDF.flatten(graph_config_map);
  } catch (const std::runtime_error&) {
    is_graph_error_caught = true;
  }
  imj::DefFlattener::destroyInst();
  unsetenv("ECC_LOGGER_THROW_ON_ERROR");
  design = dmInst->get_idb_design();
  if (!is_graph_error_caught || design == nullptr || design->get_instance_list()->find_instance("u_child") == nullptr
      || design->get_instance_list()->find_instance("u_alt") == nullptr) {
    dmInst->reset();
    return 1;
  }
  dmInst->reset();

  if (!dmInst->readLef(tech_lef_path_list, true) || !dmInst->readLef(cell_lef_path_list) || !dmInst->readDef(top_orient_def_path.string())) {
    dmInst->reset();
    return 1;
  }
  std::map<std::string, std::any> orient_config_map;
  orient_config_map["-hierarchy"] = orient_hierarchy_def_path_list;
  imj::DefFlattener::initInst();
  MJDF.flatten(orient_config_map);
  imj::DefFlattener::destroyInst();

  std::array<std::string, 8> orient_instance_name_list = {"u_child_n",  "u_child_w",  "u_child_s",  "u_child_e",
                                                          "u_child_fn", "u_child_fs", "u_child_fw", "u_child_fe"};
  std::array<std::pair<int32_t, int32_t>, 8> expected_leaf_coordinate_list
      = {std::make_pair(13000, 12500), std::make_pair(33500, 13000), std::make_pair(57000, 13500),
         std::make_pair(72500, 17000), std::make_pair(17000, 42500), std::make_pair(33000, 43500),
         std::make_pair(52500, 43000), std::make_pair(73500, 47000)};
  std::array<idb::IdbOrient, 8> expected_leaf_orient_list = {idb::IdbOrient::kW_R90, idb::IdbOrient::kS_R180,
                                                               idb::IdbOrient::kE_R270, idb::IdbOrient::kN_R0,
                                                               idb::IdbOrient::kFW_MX90, idb::IdbOrient::kFE_MY90,
                                                               idb::IdbOrient::kFS_MX, idb::IdbOrient::kFN_MY};
  design = dmInst->get_idb_design();
  for (int32_t orient_idx = 0; orient_idx < static_cast<int32_t>(orient_list.size()); orient_idx++) {
    if (design->get_instance_list()->find_instance(orient_instance_name_list[orient_idx]) != nullptr) {
      dmInst->reset();
      return 1;
    }
    idb::IdbInstance* orient_leaf_instance
        = design->get_instance_list()->find_instance(orient_instance_name_list[orient_idx] + "__u_mid__u_leaf");
    if (orient_leaf_instance == nullptr || orient_leaf_instance->get_coordinate()->get_x() != expected_leaf_coordinate_list[orient_idx].first
        || orient_leaf_instance->get_coordinate()->get_y() != expected_leaf_coordinate_list[orient_idx].second
        || orient_leaf_instance->get_orient() != expected_leaf_orient_list[orient_idx]) {
      dmInst->reset();
      return 1;
    }
  }
  dmInst->reset();
  return 0;
}
