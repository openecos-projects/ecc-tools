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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ista {
// Values accumulated in path-group/endpoint order before text or JSON formatting.
struct QorSummary
{
  std::map<std::string, double> setup_wns_map{};
  std::map<std::string, double> setup_tns_map{};
  std::map<std::string, int32_t> setup_nvp_map{};
  std::map<std::string, double> setup_frequency_map{};
  std::map<std::string, double> hold_wns_map{};
  std::map<std::string, double> hold_tns_map{};
  std::map<std::string, int32_t> hold_nvp_map{};
  std::set<std::string> group_set{};
  double total_setup_tns{};
  int32_t total_setup_nvp{};
  double total_hold_tns{};
  int32_t total_hold_nvp{};
  std::vector<std::string> setup_group_list{};
  std::vector<std::string> hold_group_list{};
  int32_t fanout_violations{};
  int32_t capacitance_violations{};
  int32_t transition_violations{};
  int32_t cell_area{};
  int32_t leaf_cell_k{};
};
}  // namespace ista
