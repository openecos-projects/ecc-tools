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
 * @file Layer.hpp
 * @brief Process layer model built from ITF records.
 */
#pragma once

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Types.hh"
#include "itfiConductor.hpp"
#include "itfiDielectric.hpp"
#include "itfiVia.hpp"

namespace ircx
{

enum class LayerType { kDielectric, kConductor, kVia };

class Layer {
 public:
  explicit Layer(LayerType);
  virtual ~Layer() = default;

  LayerType get_type() const;
  I16 get_id() const;
  std::optional<U8> get_order() const;
  F64 get_height() const;
  virtual std::string get_name() const = 0;
  virtual F64 get_layer_thickness() const = 0;

  // setter
  void set_id(I16);
  void set_order(U8);
  void set_height(F64);

 private:
  LayerType _type;
  I16 _id{0};                // order in the ITF stack
  std::optional<U8> _order;  // order in the technology LEF stack
  F64 _height{0};            // micron
};

class LayerConductor : public Layer, public itf::itfiConductor {
 public:
  LayerConductor();
  explicit LayerConductor(const itf::itfiConductor&);
  ~LayerConductor() override = default;

  std::string get_name() const override;
  F64 get_layer_thickness() const override;
  I32 get_width() const;

  void set_width(I32);

 private:
  I32 _width{0};
};

class LayerDielectric : public Layer, public itf::itfiDielectric {
 public:
  LayerDielectric();
  explicit LayerDielectric(const itf::itfiDielectric&);
  ~LayerDielectric() override = default;

  std::string get_name() const override;
  F64 get_layer_thickness() const override;
};

class LayerVia : public Layer, public itf::itfiVia {
 public:
  LayerVia();
  explicit LayerVia(const itf::itfiVia&);
  ~LayerVia() override = default;

  std::string get_name() const override;
  F64 get_layer_thickness() const override;
  F64 get_bottom_height() const;
  F64 get_top_height() const;

  void set_top_height(F64);

 private:
  F64 _top_height{0};
};

class ProcessLayerStack {
 public:
  ProcessLayerStack() = default;
  ~ProcessLayerStack() = default;
  ProcessLayerStack(const ProcessLayerStack&) = delete;
  auto operator=(const ProcessLayerStack&) -> ProcessLayerStack& = delete;
  ProcessLayerStack(ProcessLayerStack&&) = default;
  auto operator=(ProcessLayerStack&&) -> ProcessLayerStack& = default;

  const std::vector<Layer*>& get_layers() const;
  const std::vector<LayerConductor*>& get_conductor_layers() const;
  const std::vector<LayerDielectric*>& get_dielectric_layers() const;
  const std::vector<LayerVia*>& get_via_layers() const;
  Layer* get_uppermost_layer_by_order() const;
  Layer* get_lowermost_layer_by_order() const;

  void addConductor(const itf::itfiConductor&);
  void addDielectric(const itf::itfiDielectric&);
  void addVia(const itf::itfiVia&);
  void clear();

  Layer* findLayer(U8) const;
  Layer* findLayer(std::string_view) const;
  Layer* findLayer(const char*) const;
  LayerConductor* findConductorLayer(U8) const;
  LayerConductor* findConductorLayer(std::string_view) const;
  LayerConductor* findConductorLayer(const char*) const;
  LayerVia* findViaLayer(U8) const;
  LayerDielectric* findDielBelow(LayerConductor*) const;
  LayerDielectric* findDielBelow(F64) const;
  LayerDielectric* findDielAbove(F64) const;
  LayerDielectric* findDiel(I16) const;
  LayerDielectric* findDiel(std::string_view) const;
  LayerDielectric* findDiel(const char*) const;
  bool isLowermostDiel(I16) const;

 private:
  template <typename LayerT>
  auto addLayer(LayerT layer) -> LayerT*;
  void indexLayerName(Layer*);
  void indexConductorName(LayerConductor*);
  void indexDielectricName(LayerDielectric*);

  std::vector<std::unique_ptr<Layer>> _owned_layers;
  std::vector<Layer*> _layers;
  std::vector<LayerConductor*> _conductor_layers;
  std::vector<LayerDielectric*> _dielectric_layers;
  std::vector<LayerVia*> _via_layers;
  std::unordered_map<std::string, Layer*> _layer_by_name;
  std::unordered_map<std::string, LayerConductor*> _conductor_by_name;
  std::unordered_map<std::string, LayerDielectric*> _dielectric_by_name;
};

} // namespace ircx
