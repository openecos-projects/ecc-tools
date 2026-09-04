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
#include "MIComParam.hpp"
#include "MIDensityWindow.hpp"
#include "MILayer.hpp"
#include "MIModel.hpp"
#include "MIRect.hpp"
#include "MetalInserter.hpp"

int main()
{
  izh::MIRect rect(0, 0, 10, 4);
  if (!rect.is_valid() || !rect.is_intersect(izh::MIRect(9, 0, 11, 4)) || rect.is_intersect(izh::MIRect(10, 0, 12, 4)) || rect.get_area() != 40.0) {
    return 1;
  }

  izh::MIComParam mi_com_param(100.0, 50.0, 0.10, 0.80);
  mi_com_param.set_min_fill_layer("MET1");
  mi_com_param.set_max_fill_layer("MET5");
  if (mi_com_param.get_density_window_size_micron() != 100.0 || mi_com_param.get_density_window_step_micron() != 50.0 || mi_com_param.get_min_density() != 0.10
      || mi_com_param.get_max_density() != 0.80 || mi_com_param.get_min_fill_layer() != "MET1" || mi_com_param.get_max_fill_layer() != "MET5") {
    return 1;
  }

  izh::MIDensityWindow density_window(izh::MIRect(0, 0, 10, 10), 20.0);
  density_window.add_metal_area(5.0);
  if (density_window.get_density() != 0.25) {
    return 1;
  }

  izh::MILayer mi_layer;
  mi_layer.set_layer_name("M1");
  mi_layer.set_density_window_x_num(2);
  mi_layer.set_density_window_y_num(3);
  mi_layer.set_density_window_list({density_window});
  if (mi_layer.get_layer_name() != "M1" || mi_layer.get_density_window_idx(1, 2) != 5 || mi_layer.get_density_window_list().size() != 1) {
    return 1;
  }

  izh::MIModel mi_model;
  mi_model.set_mi_com_param(mi_com_param);
  mi_model.set_die(rect);
  mi_model.set_mi_layer_list({mi_layer});
  if (mi_model.get_inserted_metal_num() != 0) {
    return 1;
  }
  mi_model.set_inserted_metal_num(2);
  mi_model.add_inserted_metal_num();
  if (mi_model.get_inserted_metal_num() != 3) {
    return 1;
  }

  izh::MetalInserter::initInst();
  izh::MetalInserter* mi_instance = &ZHMI;
  izh::MetalInserter::initInst();
  if (mi_instance != &ZHMI) {
    return 1;
  }
  izh::MetalInserter::destroyInst();
  return 0;
}
