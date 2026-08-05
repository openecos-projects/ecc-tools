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
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "LVSHeader.hpp"

namespace ilvs {

class LVSConnectivitySummaryRow
{
 public:
  LVSConnectivitySummaryRow() = default;
  ~LVSConnectivitySummaryRow() = default;
  // getter
  std::string& get_connectivity() { return _connectivity; }
  int64_t get_open_num() const { return _open_num; }
  int64_t get_short_num() const { return _short_num; }
  int64_t get_connected_num() const { return _connected_num; }
  int64_t get_total_num() const { return _total_num; }
  // const getter
  const std::string& get_connectivity() const { return _connectivity; }
  // setter
  void set_connectivity(const std::string& connectivity) { _connectivity = connectivity; }
  void set_open_num(const int64_t open_num) { _open_num = open_num; }
  void set_short_num(const int64_t short_num) { _short_num = short_num; }
  void set_connected_num(const int64_t connected_num) { _connected_num = connected_num; }
  void set_total_num(const int64_t total_num) { _total_num = total_num; }

 private:
  std::string _connectivity;
  int64_t _open_num = 0;
  int64_t _short_num = 0;
  int64_t _connected_num = 0;
  int64_t _total_num = 0;
};

}  // namespace ilvs
