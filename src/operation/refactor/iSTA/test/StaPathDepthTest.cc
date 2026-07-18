// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gtest/gtest.h"
#include "sta/StaArc.hh"
#include "sta/StaVertex.hh"

using namespace ista;

namespace {

class DelayArcForTest : public StaArc {
 public:
  DelayArcForTest(StaVertex* src, StaVertex* snk) : StaArc(src, snk) {}

  unsigned isDelayArc() const override { return 1; }
};

void Connect(StaVertex& src, StaVertex& snk,
             std::vector<std::unique_ptr<StaArc>>& arcs) {
  auto arc = std::make_unique<DelayArcForTest>(&src, &snk);
  src.addSrcArc(arc.get());
  snk.addSnkArc(arc.get());
  arcs.push_back(std::move(arc));
}

TEST(StaPathDepthTest, GetsShortestDepthAcrossReconvergentPaths) {
  StaVertex start(nullptr);
  StaVertex short_mid(nullptr);
  StaVertex long_mid1(nullptr);
  StaVertex long_mid2(nullptr);
  StaVertex sink(nullptr);
  start.set_is_start();

  std::vector<std::unique_ptr<StaArc>> arcs;
  Connect(start, short_mid, arcs);
  Connect(short_mid, sink, arcs);
  Connect(start, long_mid1, arcs);
  Connect(long_mid1, long_mid2, arcs);
  Connect(long_mid2, sink, arcs);

  std::unordered_map<StaVertex*, int> depth_cache;
  std::unordered_set<StaVertex*> visiting;
  EXPECT_EQ(3, sink.getPathDepth(depth_cache, visiting));
}

TEST(StaPathDepthTest, DoesNotCacheCycleOnlyDepthAsPathRoot) {
  StaVertex cycle_a(nullptr);
  StaVertex cycle_b(nullptr);
  StaVertex start(nullptr);
  StaVertex bridge(nullptr);
  start.set_is_start();

  std::vector<std::unique_ptr<StaArc>> arcs;
  Connect(cycle_a, cycle_b, arcs);
  Connect(cycle_b, cycle_a, arcs);

  std::unordered_map<StaVertex*, int> depth_cache;
  std::unordered_set<StaVertex*> visiting;
  EXPECT_EQ(std::numeric_limits<int>::max(),
            cycle_a.getPathDepth(depth_cache, visiting));
  EXPECT_FALSE(depth_cache.contains(&cycle_a));
  EXPECT_FALSE(depth_cache.contains(&cycle_b));

  Connect(start, bridge, arcs);
  Connect(bridge, cycle_a, arcs);
  visiting.clear();
  EXPECT_EQ(3, cycle_a.getPathDepth(depth_cache, visiting));
}

}  // namespace
