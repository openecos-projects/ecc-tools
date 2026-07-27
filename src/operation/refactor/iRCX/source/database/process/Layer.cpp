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
 * @file Layer.cpp
 * @brief Process layer model implementation.
 */
#include "Layer.hpp"

#include <algorithm>
#include <type_traits>

namespace ircx
{

Layer::Layer(LayerType t)
: _type(t)
{ }

LayerType
Layer::get_type() const
{
  return _type;
}

I16
Layer::get_id() const
{
  return _id;
}

std::optional<U8>
Layer::get_order() const
{
  return _order;
}

F64
Layer::get_height() const
{
  return _height;
}

void
Layer::set_id(I16 id)
{
  _id = id;
}

void
Layer::set_order(U8 order)
{
  _order = order;
}

void
Layer::set_height(F64 height)
{
  _height = height;
}

LayerConductor::LayerConductor()
: Layer(LayerType::kConductor)
{

}

LayerConductor::LayerConductor(const itf::itfiConductor& cdt)
: Layer(LayerType::kConductor)
{
  itf::itfiConductor::operator=(cdt);
}

std::string
LayerConductor::get_name() const
{
  return get_conductor_name();
}

// Units: microns
F64
LayerConductor::get_layer_thickness() const
{
  return get_thickness();
}

I32
LayerConductor::get_width() const
{
  return _width;
}

void
LayerConductor::set_width(I32 w)
{
  _width = w;
}

LayerDielectric::LayerDielectric()
: Layer(LayerType::kDielectric)
{ }

LayerDielectric::LayerDielectric(const itf::itfiDielectric& d)
: Layer(LayerType::kDielectric)
{
  itf::itfiDielectric::operator=(d);
}

std::string
LayerDielectric::get_name() const
{
  return get_dielectric_name();
}

// Units: microns
F64
LayerDielectric::get_layer_thickness() const
{
  return get_thickness();
}

LayerVia::LayerVia()
: Layer(LayerType::kVia)
{ }

LayerVia::LayerVia(const itf::itfiVia& v)
: Layer(LayerType::kVia)
{
  itf::itfiVia::operator=(v);
}

std::string
LayerVia::get_name() const
{
  return get_via_name();
}

// Units: microns
F64
LayerVia::get_layer_thickness() const
{
  return get_top_height() - get_bottom_height();
}

F64
LayerVia::get_bottom_height() const
{
  return get_height();
}

F64
LayerVia::get_top_height() const
{
  return _top_height;
}

void
LayerVia::set_top_height(F64 top)
{
  _top_height = top;
}

// @param order of layer
Layer*
ProcessLayerStack::findLayer(U8 order) const
{
  std::optional<U8> lo;
  for (auto layer : _layers) {
    lo = layer->get_order();
    if (lo.has_value() && lo.value() == order) {
      return layer;
    }
  }

  return nullptr;
}

Layer*
ProcessLayerStack::findLayer(std::string_view name) const
{
  const auto iter = _layer_by_name.find(std::string{name});
  return iter == _layer_by_name.end() ? nullptr : iter->second;
}

Layer*
ProcessLayerStack::findLayer(const char* name) const
{
  return name == nullptr ? nullptr : findLayer(std::string_view(name));
}

// @param order layer order
LayerConductor*
ProcessLayerStack::findConductorLayer(U8 order) const
{
  for (auto layer : _conductor_layers) {
    if (layer->get_order() == order) {
      return layer;
    }
  }

  return nullptr;
}

LayerConductor*
ProcessLayerStack::findConductorLayer(std::string_view name) const
{
  const auto iter = _conductor_by_name.find(std::string{name});
  return iter == _conductor_by_name.end() ? nullptr : iter->second;
}

LayerConductor*
ProcessLayerStack::findConductorLayer(const char* name) const
{
  return name == nullptr ? nullptr : findConductorLayer(std::string_view(name));
}

LayerVia*
ProcessLayerStack::findViaLayer(U8 order) const
{
  for (auto layer : _via_layers) {
    if (layer->get_order() == order) {
      return layer;
    }
  }

  return nullptr;
}

void
ProcessLayerStack::clear()
{
  _owned_layers.clear();
  _layers.clear();
  _conductor_layers.clear();
  _dielectric_layers.clear();
  _via_layers.clear();
  _layer_by_name.clear();
  _conductor_by_name.clear();
  _dielectric_by_name.clear();
}

const std::vector<Layer*>&
ProcessLayerStack::get_layers() const
{
  return _layers;
}

const std::vector<LayerConductor*>&
ProcessLayerStack::get_conductor_layers() const
{
  return _conductor_layers;
}

const std::vector<LayerDielectric*>&
ProcessLayerStack::get_dielectric_layers() const
{
  return _dielectric_layers;
}

const std::vector<LayerVia*>&
ProcessLayerStack::get_via_layers() const
{
  return _via_layers;
}

Layer*
ProcessLayerStack::get_uppermost_layer_by_order() const
{
  Layer* ret = nullptr;
  for (auto l : _layers) {
    if (!l->get_order().has_value())  continue;

    if ((ret == nullptr)
     || (ret->get_order().value() < l->get_order().value()) ) {
      ret = l;
    }
  }
  return ret;
}

Layer*
ProcessLayerStack::get_lowermost_layer_by_order() const
{
  Layer* ret = nullptr;
  for (auto l : _layers) {
    if (!l->get_order().has_value())  continue;

    if ((ret == nullptr)
     || (ret->get_order().value() > l->get_order().value()) ) {
      ret = l;
    }
  }
  return ret;
}

template <typename LayerT>
auto
ProcessLayerStack::addLayer(LayerT layer) -> LayerT*
{
  auto owned_layer = std::make_unique<LayerT>(std::move(layer));
  auto* layer_ptr = owned_layer.get();
  layer_ptr->set_id(static_cast<I16>(_layers.size()));
  if constexpr (!std::is_same_v<LayerT, LayerDielectric>) {
    layer_ptr->set_order(static_cast<U8>(_layers.size()));
  }
  _owned_layers.push_back(std::move(owned_layer));
  _layers.push_back(layer_ptr);
  indexLayerName(layer_ptr);
  return layer_ptr;
}

void
ProcessLayerStack::indexLayerName(Layer* layer)
{
  if (layer == nullptr) {
    return;
  }

  const std::string name = layer->get_name();
  if (!name.empty()) {
    _layer_by_name.try_emplace(name, layer);
  }
}

void
ProcessLayerStack::indexConductorName(LayerConductor* layer)
{
  if (layer == nullptr) {
    return;
  }

  const std::string name = layer->get_name();
  if (!name.empty()) {
    _conductor_by_name.try_emplace(name, layer);
  }
}

void
ProcessLayerStack::indexDielectricName(LayerDielectric* layer)
{
  if (layer == nullptr) {
    return;
  }

  const std::string name = layer->get_name();
  if (!name.empty()) {
    _dielectric_by_name.try_emplace(name, layer);
  }
}

void
ProcessLayerStack::addConductor(const itf::itfiConductor& layer)
{
  auto* layer_ptr = addLayer(LayerConductor(layer));
  _conductor_layers.push_back(layer_ptr);
  indexConductorName(layer_ptr);
}

void
ProcessLayerStack::addDielectric(const itf::itfiDielectric& layer)
{
  auto* layer_ptr = addLayer(LayerDielectric(layer));
  _dielectric_layers.push_back(layer_ptr);
  indexDielectricName(layer_ptr);
}

void
ProcessLayerStack::addVia(const itf::itfiVia& layer)
{
  _via_layers.push_back(addLayer(LayerVia(layer)));
}

// @brief find the first dielectric layer below the conductor layer
LayerDielectric*
ProcessLayerStack::findDielBelow(LayerConductor* cdt) const
{
  if (!cdt) return nullptr;

  auto it = std::upper_bound(_dielectric_layers.begin(), _dielectric_layers.end(), cdt->get_id(),
    [](I16 id_high, LayerDielectric* d){
      return d->get_id() > id_high;
  });

  return it == _dielectric_layers.end() ? nullptr :
    _dielectric_layers.at(std::distance(_dielectric_layers.begin(), it));
}

LayerDielectric*
ProcessLayerStack::findDielBelow(F64 height) const
{
  auto it = std::upper_bound(_dielectric_layers.begin(), _dielectric_layers.end(), height,
    [](F64 h, LayerDielectric* d){
      return d->get_height() < h;
  });

  return it == _dielectric_layers.end() ? nullptr : *it;
}

// @brief find the first dielectric layer above the height
LayerDielectric*
ProcessLayerStack::findDielAbove(F64 height) const
{
  auto it = std::upper_bound(_dielectric_layers.rbegin(), _dielectric_layers.rend(), height,
    [](F64 h, LayerDielectric* d){
      return d->get_height() + d->get_layer_thickness() > h;
  });

  return it == _dielectric_layers.rend() ? nullptr : *it;
}

LayerDielectric*
ProcessLayerStack::findDiel(I16 id) const
{
  for (auto diel : _dielectric_layers) {
    if (diel->get_id() == id) {
      return diel;
    }
  }

  return nullptr;
}

LayerDielectric*
ProcessLayerStack::findDiel(std::string_view name) const
{
  const auto iter = _dielectric_by_name.find(std::string{name});
  return iter == _dielectric_by_name.end() ? nullptr : iter->second;
}

LayerDielectric*
ProcessLayerStack::findDiel(const char* name) const
{
  return name == nullptr ? nullptr : findDiel(std::string_view(name));
}

bool
ProcessLayerStack::isLowermostDiel(I16 id) const
{
  if (_dielectric_layers.size()) {
    auto diel = _dielectric_layers.at(_dielectric_layers.size() - 1);
    if (diel->get_id() == id) {
      return true;
    }
  }

  return false;
}

} // namespace ircx
