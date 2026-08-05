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
 * @file main.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-30
 * @brief Shared GoogleTest entry point with isolated CTS global state.
 */

#include <gtest/gtest.h>

#include <memory>

#include "Logger.hh"
#include "data_manager/DataManager.hh"

namespace icts_test {
namespace {

class DataManagerTestListener final : public ::testing::EmptyTestEventListener
{
 public:
  void OnTestStart([[maybe_unused]] const ::testing::TestInfo& test_info) override
  {
    CTSLOG.closeLogFileStream();
    CTSDM.reset();
  }
  void OnTestEnd([[maybe_unused]] const ::testing::TestInfo& test_info) override
  {
    CTSLOG.closeLogFileStream();
    CTSDM.reset();
  }
};

}  // namespace
}  // namespace icts_test

auto main(int argc, char** argv) -> int
{
  ::testing::InitGoogleTest(&argc, argv);
  icts::Logger::initInst();
  icts::DataManager::initInst();

  auto& listeners = ::testing::UnitTest::GetInstance()->listeners();
  auto data_manager_listener = std::make_unique<icts_test::DataManagerTestListener>();
  listeners.Append(data_manager_listener.get());
  const int result = RUN_ALL_TESTS();
  (void) listeners.Release(data_manager_listener.get());

  icts::DataManager::destroyInst();
  icts::Logger::destroyInst();
  return result;
}
