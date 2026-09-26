// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You may use this software according to the terms and conditions of the Mulan PSL v2.
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
 * @file WrapperTimingGraphTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-26
 * @brief Regression test for disconnected DEF nets in the timing graph snapshot.
 */

#include <gtest/gtest.h>

#include "IdbDesign.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "data_manager/io/Wrapper.hh"

namespace icts_test {
namespace {

TEST(WrapperTimingGraphTest, EmptySignalNetDoesNotInvalidateGraph)
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);
  ASSERT_NE(design.get_net_list(), nullptr);
  auto* net = design.get_net_list()->add_net("disconnected_alias", idb::IdbConnectType::kSignal);
  ASSERT_NE(net, nullptr);
  ASSERT_EQ(net->get_pin_number(), 0);

  icts::Wrapper wrapper;
  wrapper.set_idb_layout(&layout);
  wrapper.set_idb_design(&design);
  const auto graph = wrapper.collectTimingGraph();
  EXPECT_TRUE(graph.complete()) << graph.diagnostic;
  EXPECT_TRUE(graph.nets.empty());
}

}  // namespace
}  // namespace icts_test
