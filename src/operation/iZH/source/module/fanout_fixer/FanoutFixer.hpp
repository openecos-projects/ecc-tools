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

#include "FFModel.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"

namespace izh {

#define ZHFF (izh::FanoutFixer::getInst())

class FanoutFixer
{
 public:
  static void initInst();
  static FanoutFixer& getInst();
  static void destroyInst();
  // function
  void fix(std::map<std::string, std::any> config_map);

 private:
  // self
  static FanoutFixer* _ff_instance;

  FanoutFixer() = default;
  FanoutFixer(const FanoutFixer& other) = delete;
  FanoutFixer(FanoutFixer&& other) = delete;
  ~FanoutFixer() = default;
  FanoutFixer& operator=(const FanoutFixer& other) = delete;
  FanoutFixer& operator=(FanoutFixer&& other) = delete;
  // function
  FFModel initFFModel(std::map<std::string, std::any>& config_map);
};

}  // namespace izh
