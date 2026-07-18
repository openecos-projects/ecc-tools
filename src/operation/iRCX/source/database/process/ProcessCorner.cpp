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
 * @file ProcessCorner.cpp
 * @brief ITF process-corner data model implementation.
 */
#include "ProcessCorner.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <utility>
#include <vector>

#include "StringUtils.hh"
#include "log/Log.hh"
#include "magic_enum/magic_enum.hpp"

namespace ircx
{

ProcessCorner::ProcessCorner()
: _info()
{ }

const ProcessCornerInfo&
ProcessCorner::get_info() const
{
  return _info;
}

const std::string&
ProcessCorner::get_technology() const
{
  return _info.technology;
}

const std::string&
ProcessCorner::get_process_foundry() const
{
  return _info.process_foundry;
}

F64
ProcessCorner::get_process_node() const
{
  return _info.process_node;
}

const std::string&
ProcessCorner::get_process_type() const
{
  return _info.process_type;
}

F64
ProcessCorner::get_process_version() const
{
  return _info.process_version;
}

const std::string&
ProcessCorner::get_process_corner() const
{
  return _info.process_corner;
}

const std::string&
ProcessCorner::get_reference_direction() const
{
  return _info.reference_direction;
}

F64
ProcessCorner::get_global_temperature() const
{
  return _info.global_temperature;
}

F64
ProcessCorner::get_background_er() const
{
  return _info.background_er;
}

F64
ProcessCorner::get_half_node_scale_factor() const
{
  return _info.half_node_scale_factor;
}

bool
ProcessCorner::use_si_density() const
{
  return _info.use_si_density;
}

F64
ProcessCorner::get_drop_factor_lateral_spacing() const
{
  return _info.drop_factor_lateral_spacing;
}

ProcessLayerStack&
ProcessCorner::get_layers()
{
  return _layers;
}

const ProcessLayerStack&
ProcessCorner::get_layers() const
{
  return _layers;
}

void
ProcessCorner::set_technology(std::string v)
{
  _info.technology = std::move(v);
}
void
ProcessCorner::set_process_foundry(std::string v)
{
  _info.process_foundry = std::move(v);
}
void
ProcessCorner::set_process_node(F64 v)
{
  _info.process_node = v;
}
void
ProcessCorner::set_process_type(std::string v)
{
  _info.process_type = std::move(v);
}
void
ProcessCorner::set_process_version(F64 v)
{
  _info.process_version = v;
}
void
ProcessCorner::set_process_corner(std::string v)
{
  _info.process_corner = std::move(v);
}
void
ProcessCorner::set_reference_direction(std::string v)
{
  _info.reference_direction = std::move(v);
}
void
ProcessCorner::set_global_temperature(F64 v)
{
  _info.global_temperature = v;
}
void
ProcessCorner::set_background_er(F64 v)
{
  _info.background_er = v;
}
void
ProcessCorner::set_half_node_scale_factor(F64 v)
{
  _info.half_node_scale_factor = v;
}
void
ProcessCorner::set_use_si_density(bool v)
{
  _info.use_si_density = v;
}
void
ProcessCorner::set_drop_factor_lateral_spacing(F64 v)
{
  _info.drop_factor_lateral_spacing = v;
}

// @param name_order_map itf layer name -> layer order
void
ProcessCorner::set_layers_map(const std::map<std::string, U8>& name_order_map)
{
  for (const auto& [name, layer_order] : name_order_map) {
    auto layer = _layers.findLayer(name);
    if (layer) {
      layer->set_order(layer_order);
    }
  }
}

void
ProcessCorner::updateLayersHeight()
{
  std::vector<Layer*> stack_layers;
  for (auto layer : _layers.get_layers()) {
    if (layer->get_type() == LayerType::kVia) continue;
    stack_layers.push_back(layer);
  }
  std::sort(stack_layers.begin(), stack_layers.end(), [](Layer* high, Layer* low){
    return high->get_id() > low->get_id();
  });

  LayerDielectric* last_diel = nullptr;
  for (auto layer : stack_layers) {
    switch (layer->get_type()) {
      case LayerType::kConductor:
        updateConductorLayerHeight(static_cast<LayerConductor*>(layer), last_diel);
        break;
      case LayerType::kDielectric:
        updateDielectricLayerHeight(last_diel, static_cast<LayerDielectric*>(layer));
        last_diel = static_cast<LayerDielectric*>(layer);
        break;
      default:
        LOG_ERROR << "Unhandled layer type: " << magic_enum::enum_name(layer->get_type());
        break;
    }
  }

  // via layer
  for (auto via : _layers.get_via_layers()) {
    const char* from_layer_name = via->get_from();
    auto bot = _layers.findLayer(from_layer_name);
    if (bot) {
      via->set_height(bot->get_height() + bot->get_layer_thickness());
    } else if (from_layer_name != nullptr
               && !string::equalsIgnoreCase(from_layer_name, "SUBSTRATE")) {
      LOG_ERROR << "fail to find layer: " << from_layer_name;
    }

    const char* to_layer_name = via->get_to();
    auto top = _layers.findLayer(to_layer_name);
    if (top) {
      via->set_top_height(top->get_height());
    } else {
      LOG_ERROR << "fail to find layer: " << (to_layer_name == nullptr ? "<null>" : to_layer_name);
    }
  }

  // debug
  // show_layers();
}

void
ProcessCorner::updateDielectricLayerHeight(LayerDielectric* last_diel,
                                              LayerDielectric* cur_diel)
{
  if (!cur_diel) return;

  if (cur_diel->has_associated_conductor()) {
    auto cdt = _layers.findConductorLayer(cur_diel->get_associated_conductor());
    if (cdt) {
      if (cur_diel->has_measured_from_conductor()) {
        cur_diel->set_height(cdt->get_height() + cdt->get_layer_thickness());
      } else {
        cur_diel->set_height(cdt->get_height());
      }
    }
  } else if (cur_diel->has_measured_from()) {
    auto layer = _layers.findLayer(cur_diel->get_measured_from());
    if (layer) {
      cur_diel->set_height(layer->get_height() + layer->get_layer_thickness());
    } else if (string::equalsIgnoreCase(cur_diel->get_measured_from(), "TOP_OF_CHIP")
               && last_diel) {
      cur_diel->set_height(last_diel->get_height() + last_diel->get_layer_thickness());
    }
  } else if (last_diel) {
    cur_diel->set_height(last_diel->get_height() + last_diel->get_layer_thickness());
  } else if (_layers.isLowermostDiel(cur_diel->get_id())) {
    return;
  } else {
    LOG_ERROR << "fail to update layer height at: " << cur_diel->get_name();
  }
}

void
ProcessCorner::updateConductorLayerHeight(LayerConductor* cdt,
                                             LayerDielectric* last_diel)
{
  if (!cdt) return;

  const char* measured = cdt->get_measured_from();
  LayerDielectric* diel = nullptr;
  if (measured) {
    diel = _layers.findDiel(measured);
  } else {
    diel = last_diel;
  }

  if (diel) {
    cdt->set_height(diel->get_height() + diel->get_thickness());
  } else {
    LOG_ERROR << "fail to find diel, cdt_id = " << cdt->get_id();
  }
}

void
ProcessCorner::showLayers() const
{
  auto show_er = [](Layer* layer) -> std::string {
    if (layer->get_type() != LayerType::kDielectric) return "";
    else return "| " + std::to_string(static_cast<LayerDielectric*>(layer)->get_er());
  };
  auto show_layer_type = [](Layer* layer) -> std::string_view {
    return magic_enum::enum_name(layer->get_type());
  };
  std::cout << "  layer type   | layer name               | height | thickness | ER" << std::endl;
  for (auto layer : _layers.get_layers()) {
    std::cout
      << std::left << std::setw(15) << show_layer_type(layer) << "| "
      << std::left << std::setw(25) << layer->get_name()  << "| "
      << std::setw(7) << layer->get_height() << "| "
      << std::setw(10) << layer->get_layer_thickness()
      << show_er(layer) << ""
      << std::endl;
  }
}

} // namespace ircx
