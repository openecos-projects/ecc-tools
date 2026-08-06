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

#include "FPHeader.hpp"

namespace ifp {

#define FPDB (ifp::DieBuilder::getInst())

class DieBuilder
{
 public:
  static void initInst();
  static DieBuilder& getInst();
  static void destroyInst();
  // function
  void build();

 private:
  // self
  static DieBuilder* _db_instance;

  DieBuilder() = default;
  DieBuilder(const DieBuilder& other) = delete;
  DieBuilder(DieBuilder&& other) = delete;
  ~DieBuilder() = default;
  DieBuilder& operator=(const DieBuilder& other) = delete;
  DieBuilder& operator=(DieBuilder&& other) = delete;
  // function

  void buildFloorplan();
  void buildDie(double die_lx, double die_ly, double die_ux, double die_uy);
  void buildCore(double core_lx, double core_ly, double core_ux, double core_uy, std::string site_name);
  void buildRowList();
  void buildTrackList();
  void buildTrack(std::string layer_name, int32_t x_offset, int32_t x_pitch, int32_t y_offset, int32_t y_pitch);
};

}  // namespace ifp
