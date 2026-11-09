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
 * @file IstaConstraintReader.hh
 * @brief Native STA initialization and owned constraint snapshot boundary.
 */
#pragma once

#include <string>

#include "timing/TimingConstraints.hh"

namespace ista {
class Database;
}

namespace icts {

class IstaConstraintReader
{
 public:
  // The current iDB design and shared raw Liberty generation must already be loaded.
  // The temporary native session is destroyed before this call returns.
  static auto read(const std::string& work_dir) -> SdcClockData;
  // Value-only extraction; no Tcl interpretation or object-query resolution.
  static auto snapshot(ista::Database& database) -> SdcClockData;
};

}  // namespace icts
