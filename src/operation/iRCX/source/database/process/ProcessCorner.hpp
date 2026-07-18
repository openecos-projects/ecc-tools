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
 * @file ProcessCorner.hpp
 * @brief Process-corner data model built from ITF.
 */
#pragma once

#include <map>

#include "Layer.hpp"

namespace ircx
{

struct ProcessCornerInfo
{
  std::string technology;
  std::string process_foundry;
  F64 process_node{0};
  std::string process_type;
  F64 process_version{0};
  std::string process_corner;
  std::string reference_direction;
  F64 global_temperature{25};
  F64 background_er{1};
  F64 half_node_scale_factor{1};
  bool use_si_density{false};
  F64 drop_factor_lateral_spacing{0.5};
};

class ProcessCorner {
 public:
  ProcessCorner();
  ~ProcessCorner() = default;
  ProcessCorner(const ProcessCorner&) = delete;
  auto operator=(const ProcessCorner&) -> ProcessCorner& = delete;
  ProcessCorner(ProcessCorner&&) = default;
  auto operator=(ProcessCorner&&) -> ProcessCorner& = default;

  const ProcessCornerInfo& get_info() const;
  const std::string& get_technology() const;
  const std::string& get_process_foundry() const;
  F64 get_process_node() const;
  const std::string& get_process_type() const;
  F64 get_process_version() const;
  const std::string& get_process_corner() const;
  const std::string& get_reference_direction() const;
  F64 get_global_temperature() const;
  F64 get_background_er() const;
  F64 get_half_node_scale_factor() const;
  bool use_si_density() const;
  F64 get_drop_factor_lateral_spacing() const;
  ProcessLayerStack& get_layers();
  const ProcessLayerStack& get_layers() const;

  void set_technology(std::string);
  void set_process_foundry(std::string);
  void set_process_node(F64);
  void set_process_type(std::string);
  void set_process_version(F64);
  void set_process_corner(std::string);
  void set_reference_direction(std::string);
  void set_global_temperature(F64);
  void set_background_er(F64);
  void set_half_node_scale_factor(F64);
  void set_use_si_density(bool);
  void set_drop_factor_lateral_spacing(F64);
  void set_layers_map(const std::map<std::string, U8>&);

  void updateLayersHeight();
  void showLayers() const;

 private:
  void updateDielectricLayerHeight(LayerDielectric* last_diel,
                                      LayerDielectric* cur_diel);
  void updateConductorLayerHeight(LayerConductor* conductor,
                                     LayerDielectric* last_diel);

  ProcessCornerInfo _info;
  ProcessLayerStack _layers;
};

} // namespace ircx
