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

#include "MPComParam.hpp"
#include "Row.hpp"

namespace ifp {

#define FPMP (ifp::MacroPlacer::getInst())

class MacroPlacer
{
 public:
  static void initInst();
  static MacroPlacer& getInst();
  static void destroyInst();
  // function
  void place();

 private:
  // self
  static MacroPlacer* _mp_instance;

  MacroPlacer() = default;
  MacroPlacer(const MacroPlacer& other) = delete;
  MacroPlacer(MacroPlacer&& other) = delete;
  ~MacroPlacer() = default;
  MacroPlacer& operator=(const MacroPlacer& other) = delete;
  MacroPlacer& operator=(MacroPlacer&& other) = delete;
  // function

  void setMPComParam(MPComParam& mp_com_param);
  void checkMacroPlacement();
  void checkMacroInCore();
  void buildMacroPlacementHalo(MPComParam& mp_com_param);
  void buildMacroRoutingHalo(MPComParam& mp_com_param);
  void cutRowList();
  void cutRow(Row& row, std::vector<Row>& cut_row_list);
  std::vector<std::pair<int32_t, int32_t>> getRowBlockageIntervalList(Row& row);
  void addCutRow(Row& row, std::vector<Row>& cut_row_list, int32_t start_x, int32_t end_x, int32_t cut_row_idx);
};

}  // namespace ifp
