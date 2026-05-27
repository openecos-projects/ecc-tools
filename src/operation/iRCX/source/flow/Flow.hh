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

#include "Types.hh"

namespace ircx {

#define RCX_FLOW_INST (ircx::Flow::getInst())

class Flow
{
 public:
  static auto getInst() -> Flow&
  {
    static Flow inst;
    return inst;
  }

  auto run() -> bool;

  auto adaptDB() -> bool;
  auto extract() -> bool;
  auto report() -> bool;

  void reset();

  Flow(const Flow& other) = delete;
  Flow(Flow&& other) = delete;
  auto operator=(const Flow& other) -> Flow& = delete;
  auto operator=(Flow&& other) -> Flow& = delete;

 private:
  Flow();
  ~Flow();
};

}  // namespace ircx
