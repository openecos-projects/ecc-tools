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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "RCXHeader.hpp"
#include "SPEFCouplingRef.hpp"
#include "SPEFReportLayer.hpp"

namespace ircx {

class SWModel
{
 public:
  SWModel() = default;
  ~SWModel() = default;
  // getter
  std::vector<std::vector<SPEFCouplingRef>>& get_net_coupling_ref_list() { return _net_coupling_ref_list; }
  std::vector<SPEFReportLayer>& get_report_layer_list() { return _report_layer_list; }
  std::unordered_map<int32_t, int32_t>& get_design_layer_idx_to_report_layer_idx_map() { return _design_layer_idx_to_report_layer_idx_map; }
  // setter
  void set_net_coupling_ref_list(const std::vector<std::vector<SPEFCouplingRef>>& net_coupling_ref_list) { _net_coupling_ref_list = net_coupling_ref_list; }
  void set_report_layer_list(const std::vector<SPEFReportLayer>& report_layer_list) { _report_layer_list = report_layer_list; }
  void set_design_layer_idx_to_report_layer_idx_map(const std::unordered_map<int32_t, int32_t>& design_layer_idx_to_report_layer_idx_map)
  {
    _design_layer_idx_to_report_layer_idx_map = design_layer_idx_to_report_layer_idx_map;
  }
  // function

 private:
  std::vector<std::vector<SPEFCouplingRef>> _net_coupling_ref_list;
  std::vector<SPEFReportLayer> _report_layer_list;
  std::unordered_map<int32_t, int32_t> _design_layer_idx_to_report_layer_idx_map;
};

}  // namespace ircx
