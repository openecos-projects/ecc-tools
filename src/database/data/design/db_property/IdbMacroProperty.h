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
#pragma once
/**
 * @project		iDB
 * @file		IdbMacroProperty.h
 * @date		2026/10/14
 * @version		0.1
 * @description

        Describe MACRO level design rules that lef states on the macro itself,
        not on the layer. Currently MACRO OBS LAYER SPACING.
 *
 */

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <cstdint>
#include <vector>

namespace idb {

class IdbLayer;

namespace macro {

class ObsSpacing
{
 public:
  ObsSpacing() = default;
  ObsSpacing(IdbLayer* layer, int32_t min_spacing) : _layer(layer), _min_spacing(min_spacing) {}
  ~ObsSpacing() = default;

  // getter
  IdbLayer* get_layer() { return _layer; }
  int32_t get_min_spacing() { return _min_spacing; }

  // setter
  void set_layer(IdbLayer* layer) { _layer = layer; }
  void set_min_spacing(int32_t min_spacing) { _min_spacing = min_spacing; }

  // operator

 private:
  IdbLayer* _layer = nullptr;
  int32_t _min_spacing = -1;
};

class MacroProperty
{
 public:
  MacroProperty() = default;
  ~MacroProperty() = default;

  // getter
  bool has_obs_spacing() { return !_obs_spacing_list.empty(); }
  std::vector<ObsSpacing>& get_obs_spacing_list() { return _obs_spacing_list; }
  ObsSpacing* find_obs_spacing(IdbLayer* layer)
  {
    for (ObsSpacing& obs_spacing : _obs_spacing_list) {
      if (obs_spacing.get_layer() == layer) {
        return &obs_spacing;
      }
    }

    return nullptr;
  }

  // setter
  void add_obs_spacing(const ObsSpacing& obs_spacing) { _obs_spacing_list.emplace_back(obs_spacing); }

  // operator

 private:
  std::vector<ObsSpacing> _obs_spacing_list;
};

}  // namespace macro

}  // namespace idb
