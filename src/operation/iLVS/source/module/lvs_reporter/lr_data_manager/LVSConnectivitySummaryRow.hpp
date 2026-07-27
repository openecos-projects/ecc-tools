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
  std::string& get_type() { return _type; }
  int64_t get_count() const { return _count; }
  // const getter
  const std::string& get_connectivity() const { return _connectivity; }
  const std::string& get_type() const { return _type; }
  // setter
  void set_connectivity(const std::string& connectivity) { _connectivity = connectivity; }
  void set_type(const std::string& type) { _type = type; }
  void set_count(const int64_t count) { _count = count; }

 private:
  std::string _connectivity;
  std::string _type;
  int64_t _count = 0;
};

}  // namespace ilvs
