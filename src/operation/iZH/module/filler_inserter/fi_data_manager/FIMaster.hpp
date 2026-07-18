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

#include "IdbCellMaster.h"
#include "ZHHeader.hpp"

namespace izh {

class FIMaster
{
 public:
  FIMaster() = default;
  ~FIMaster() = default;
  // getter
  idb::IdbCellMaster* get_master() { return _master; }
  const std::string& get_name() const { return _name; }
  int32_t get_width() const { return _width; }
  int32_t get_site_count() const { return _site_count; }
  int32_t get_inserted_num() const { return _inserted_num; }
  // setter
  void set_master(idb::IdbCellMaster* master) { _master = master; }
  void set_name(const std::string& name) { _name = name; }
  void set_width(const int32_t width) { _width = width; }
  void set_site_count(const int32_t site_count) { _site_count = site_count; }
  void set_inserted_num(const int32_t inserted_num) { _inserted_num = inserted_num; }
  // function
  void addInsertedNum() { ++_inserted_num; }

 private:
  idb::IdbCellMaster* _master = nullptr;
  std::string _name;
  int32_t _width = -1;
  int32_t _site_count = -1;
  int32_t _inserted_num = 0;
};

}  // namespace izh
