// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the License at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <memory>
#include <stdexcept>

#include "CapTableConfig.hpp"
#include "Logger.hpp"
#include "ProcessTechnology.hpp"

namespace ircx {

class CornerData
{
 public:
  CornerData() = default;
  ~CornerData() = default;
  // getter
  std::string& get_corner_name() { return _corner_name; }
  double get_tmpr() const { return _tmpr; }
  double get_global_tmpr() const { return _process_technology->nominal_temperature; }
  double get_half_node_scale_factor() const { return _process_technology->geometry_scale; }
  std::vector<CapTableConfig>& get_cap_table_config_list() { return _cap_table_config_list; }
  const std::shared_ptr<const ProcessTechnology>& get_process_technology() const { return _process_technology; }
  // const getter
  const std::vector<ProcessConductor>& get_process_conductor_list() const { return _process_technology->conductors; }
  const std::vector<ProcessVia>& get_process_via_list() const { return _process_technology->vias; }
  const std::vector<CapTableConfig>& get_cap_table_config_list() const { return _cap_table_config_list; }
  // setter
  void set_corner_name(const std::string& corner_name) { _corner_name = corner_name; }
  void set_tmpr(double tmpr) { _tmpr = tmpr; }
  void set_cap_table_config_list(const std::vector<CapTableConfig>& cap_table_config_list) { _cap_table_config_list = cap_table_config_list; }
  void set_process_technology(std::shared_ptr<const ProcessTechnology> model)
  {
    if (!model)
      RCXLOG.error(Loc::current(), "process technology must not be null");
    _process_technology = std::move(model);
  }
  // function

 private:
  std::shared_ptr<const ProcessTechnology> _process_technology = std::make_shared<const ProcessTechnology>();
  std::string _corner_name;
  double _tmpr = -1.0;
  std::vector<CapTableConfig> _cap_table_config_list;
};

}  // namespace ircx
