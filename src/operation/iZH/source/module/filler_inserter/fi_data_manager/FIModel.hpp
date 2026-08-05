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

#include "FIMaster.hpp"
#include "FIRow.hpp"
#include "ZHHeader.hpp"

namespace izh {

class FIModel
{
 public:
  FIModel() = default;
  ~FIModel() = default;
  // getter
  std::vector<FIMaster>& get_filler_master_list() { return _filler_master_list; }
  std::vector<FIRow>& get_row_list() { return _row_list; }
  int32_t get_min_filler_site_count() const { return _min_filler_site_count; }
  int32_t get_inserted_filler_num() const { return _inserted_filler_num; }
  // setter
  void set_filler_master_list(const std::vector<FIMaster>& filler_master_list) { _filler_master_list = filler_master_list; }
  void set_row_list(const std::vector<FIRow>& row_list) { _row_list = row_list; }
  void set_min_filler_site_count(const int32_t min_filler_site_count) { _min_filler_site_count = min_filler_site_count; }
  void set_inserted_filler_num(const int32_t inserted_filler_num) { _inserted_filler_num = inserted_filler_num; }
  // function
  void addInsertedFillerNum() { ++_inserted_filler_num; }

 private:
  std::vector<FIMaster> _filler_master_list;
  std::vector<FIRow> _row_list;
  int32_t _min_filler_site_count = 1;
  int32_t _inserted_filler_num = 0;
};

}  // namespace izh
