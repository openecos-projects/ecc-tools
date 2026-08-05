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
#include <cassert>

#include "MIFillShape.hpp"
#include "MILayerRule.hpp"
#include "MIModel.hpp"
#include "MIRect.hpp"
#include "MetalInserter.hpp"

int main()
{
  izh::MIRect rect(0, 0, 4, 2);
  assert(rect.isValid());
  assert(rect.isIntersect(izh::MIRect(3, 0, 5, 2)));
  assert(!rect.isIntersect(izh::MIRect(4, 0, 6, 2)));

  izh::MIFillShape fill_shape(2, 1);
  assert(fill_shape.isValid());
  izh::MILayerRule layer_rule;
  layer_rule.set_layer_name("M1");
  layer_rule.set_fill_shape_list({fill_shape});
  layer_rule.set_space_to_fill(1);
  layer_rule.set_space_to_non_fill(2);

  izh::MIModel mi_model;
  mi_model.set_rule_file_path("metal_fill.json");
  mi_model.set_fill_area(rect);
  mi_model.set_layer_rule_list({layer_rule});
  mi_model.set_reset_fill(true);
  assert(mi_model.get_rule_file_path() == "metal_fill.json");
  assert(mi_model.get_fill_area().isValid());
  assert(mi_model.get_layer_rule_list().size() == 1);
  assert(mi_model.get_reset_fill());
  if (mi_model.get_inserted_metal_num() != 0) {
    return 1;
  }
  mi_model.set_inserted_metal_num(2);
  mi_model.addInsertedMetalNum();
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
