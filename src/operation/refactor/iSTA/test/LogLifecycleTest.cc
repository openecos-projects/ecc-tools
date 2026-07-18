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

#include "gtest/gtest.h"

#include "log/Log.hh"

namespace {

TEST(LogLifecycleTest, end_clears_init_flag) {
  char config[] = "test";
  char* argv[] = {config};

  ieda::Log::init(argv);
  ASSERT_TRUE(ieda::Log::isInit());

  ieda::Log::end();
  EXPECT_FALSE(ieda::Log::isInit());
}

TEST(LogLifecycleTest, can_reinitialize_after_end) {
  char config[] = "test";
  char* argv[] = {config};

  ieda::Log::init(argv);
  ASSERT_TRUE(ieda::Log::isInit());
  ieda::Log::end();
  ASSERT_FALSE(ieda::Log::isInit());

  ieda::Log::init(argv);
  EXPECT_TRUE(ieda::Log::isInit());
  ieda::Log::end();
  EXPECT_FALSE(ieda::Log::isInit());
}

}  // namespace
