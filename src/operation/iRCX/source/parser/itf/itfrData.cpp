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
 * @file itfrData.cpp
 * @brief Legacy ITF parser data structure implementation detail.
 */
#include <memory>

#include "itfrData.hpp"

namespace itf
{
namespace
{

std::unique_ptr<itfrData> itfDataOwner;

} // namespace

itfrData* itfData = nullptr;

itfrData::itfrData()
: itf_file(),
  log_file(nullptr),

  process_name(),
  process_foundry(),
  process_node(0),
  process_type(),
  process_version(0),
  process_corner(),
  reference_direction(),
  global_temperature(25.f),
  background_er(1.f),
  half_node_scale_factor(1.f),
  drop_factor_lateral_spacing(.5f),
  
  dielectric(),
  conductor(),
  via(),

  use_si_density(0),
  has_open_log_file(0),
  has_global_temperature(0),
  has_background_er(0),
  has_half_node_scale_factor(0),
  has_use_si_density(0),
  has_drop_factor_lateral_spacing(0)
{

}

itfrData::~itfrData()
{
  log_file = nullptr;  // not release here
}

void itfrData::reset() {
  itfDataOwner = std::make_unique<itfrData>();
  itfData = itfDataOwner.get();
}

void itfrData::clear() {
  itfDataOwner.reset();
  itfData = nullptr;
}

void itfrData::initRead() {
  
}

} // namespace itf
