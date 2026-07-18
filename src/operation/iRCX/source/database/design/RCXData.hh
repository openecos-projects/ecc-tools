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
 * @file RCXData.hh
 * @brief Shared runtime data container for iRCX extraction.
 */
#pragma once

#include <optional>
#include <vector>

#include "CapTable.hpp"
#include "CornerNetPool.hh"
#include "RCXConfig.hh"
#include "NetEtchProfile.hh"
#include "NetEnvironment.hh"
#include "LayerTable.hh"
#include "LayoutData.hh"
#include "MappingBuilder.hpp"
#include "ProcessCorner.hpp"
#include "RCTable.hh"
#include "SpefContext.hh"
#include "TopoPool.hh"
#include "Types.hh"

namespace ircx {

namespace parser {
class CapTable;
class MappingBuilder;
}  // namespace parser

#define RCX_DATA_INST (ircx::RCXData::getInst())

class RCXData final {
 public:
  struct CornerData {
    CornerData();
    ~CornerData();
    CornerData(CornerData&&);
    CornerData& operator=(CornerData&&);
    CornerData(const CornerData&) = delete;
    CornerData& operator=(const CornerData&) = delete;

    std::string name;
    F64 temperature{kDefaultOperatingTemperature};
    std::string itf_file;
    std::string captab_file;
    std::optional<ProcessCorner> process_corner;
    parser::CapTable cap_table;

    F64 get_half_node_scale_factor() const;
  };

  static RCXData& getInst() {
    static RCXData inst;
    return inst;
  }

  void reset();
  void set_db_data(LayoutData layout_data,
                   const LayerTable& design_layer_table,
                   SpefContext spef_context);

  LayoutData& get_layout() { return layout_; }
  const LayoutData& get_layout() const { return layout_; }
  SpefContext& get_spef_context() { return spef_context_; }
  const SpefContext& get_spef_context() const { return spef_context_; }
  LayerTable& get_layer_table() { return layer_table_; }
  const LayerTable& get_layer_table() const { return layer_table_; }
  parser::MappingBuilder& get_mapping_builder() { return mapping_builder_; }
  const parser::MappingBuilder& get_mapping_builder() const { return mapping_builder_; }
  TopoPool& get_topo_pool() { return topo_pool_; }
  const TopoPool& get_topo_pool() const { return topo_pool_; }
  RCTable& get_rc_table() { return rc_table_; }
  const RCTable& get_rc_table() const { return rc_table_; }
  std::vector<NetEnvironment>& get_net_env_pools() { return net_env_pools_; }
  const std::vector<NetEnvironment>& get_net_env_pools() const { return net_env_pools_; }
  CornerNetPool<NetEtchProfile>& get_corner_net_etch_pools() { return corner_net_etch_pools_; }
  const CornerNetPool<NetEtchProfile>& get_corner_net_etch_pools() const
  {
    return corner_net_etch_pools_;
  }
  std::vector<CornerData>& get_corner_data() { return corners_; }
  const std::vector<CornerData>& get_corner_data() const { return corners_; }
  F64 get_half_node_scale_factor(Size corner_idx) const;

  bool has_corner(const std::string& corner_name) const;
  void set_process_layers_registered(bool value) { process_layers_registered_ = value; }
  bool is_process_layers_registered() const { return process_layers_registered_; }

  RCXData(const RCXData&) = delete;
  RCXData(RCXData&&) = delete;
  auto operator=(const RCXData&) -> RCXData& = delete;
  auto operator=(RCXData&&) -> RCXData& = delete;

 private:
  RCXData() = default;
  ~RCXData();

  // Design data adapted from iDB.
  LayoutData layout_;
  SpefContext spef_context_;
  LayerTable layer_table_;

  // Technology and mapping data loaded during setup.
  std::vector<CornerData> corners_;
  parser::MappingBuilder mapping_builder_;

  // Extraction intermediate and result data.
  TopoPool topo_pool_;
  RCTable rc_table_;
  std::vector<NetEnvironment> net_env_pools_;
  CornerNetPool<NetEtchProfile> corner_net_etch_pools_;

  bool process_layers_registered_{false};
};

}  // namespace ircx
