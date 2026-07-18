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
 * @file ItfBuilder.hpp
 * @brief ITF builder facade.
 */
#pragma once

#include <optional>
#include <vector>

#include "ProcessCorner.hpp"
#include "Types.hh"

namespace ircx
{

class ItfBuilder {
 public:
  ItfBuilder() = default;
  ~ItfBuilder() = default;

  ProcessCorner* get_last_process_corner();
  const ProcessCorner* get_last_process_corner() const;

  std::vector<ProcessCorner> takeProcessCorners();
  std::optional<ProcessCorner> takeLastProcessCorner();

  bool build(const std::string&);

  const ProcessCorner* findProcessCorner(const std::string&) const;

 private:
  void addProcessCorner(ProcessCorner);

  std::vector<ProcessCorner> _process_corners;
};

} // namespace ircx
