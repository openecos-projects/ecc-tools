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

#include "ZHHeader.hpp"

namespace izh {

class FFModel
{
 public:
  FFModel() = default;
  ~FFModel() = default;
  // getter
  const std::string& get_buffer_name() const { return _buffer_name; }
  int32_t get_max_fanout() const { return _max_fanout; }
  size_t get_fixed_net_num() const { return _fixed_net_num; }
  size_t get_inserted_net_num() const { return _inserted_net_num; }
  size_t get_inserted_buffer_num() const { return _inserted_buffer_num; }
  // setter
  void set_buffer_name(const std::string& buffer_name) { _buffer_name = buffer_name; }
  void set_max_fanout(const int32_t max_fanout) { _max_fanout = max_fanout; }
  void set_fixed_net_num(const size_t fixed_net_num) { _fixed_net_num = fixed_net_num; }
  void set_inserted_net_num(const size_t inserted_net_num) { _inserted_net_num = inserted_net_num; }
  void set_inserted_buffer_num(const size_t inserted_buffer_num) { _inserted_buffer_num = inserted_buffer_num; }
  // function
  void addFixedNetNum(const size_t fixed_net_num) { _fixed_net_num += fixed_net_num; }
  void addInsertedNetNum(const size_t inserted_net_num) { _inserted_net_num += inserted_net_num; }
  void addInsertedBufferNum(const size_t inserted_buffer_num) { _inserted_buffer_num += inserted_buffer_num; }

 private:
  std::string _buffer_name = "zh_buffer";
  int32_t _max_fanout = 32;
  size_t _fixed_net_num = 0;
  size_t _inserted_net_num = 0;
  size_t _inserted_buffer_num = 0;
};

}  // namespace izh
