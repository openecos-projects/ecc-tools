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

#include <ctime>
#include <string>

#include "header.h"

namespace idb {

class DesignRead
{
 public:
  explicit DesignRead(IdbLayout* layout = nullptr);
  ~DesignRead() = default;

  IdbDesign* readDesign(const char* folder);
  IdbDesign* readDesign(const std::string& folder, bool parallel = true);
  bool readDesign(IdbDesign* design, const std::string& folder, bool parallel = true);

  void set_layout(IdbLayout* layout) { _layout = layout; }
  IdbLayout* get_layout() { return _layout; }

  void set_start_time(clock_t time) { _start_time = time; }
  void set_end_time(clock_t time) { _end_time = time; }
  float time_eclips() { return (float(_end_time - _start_time)) / CLOCKS_PER_MS; }

 private:
  IdbLayout* _layout = nullptr;
  clock_t _start_time = 0;
  clock_t _end_time = 0;
};

}  // namespace idb
