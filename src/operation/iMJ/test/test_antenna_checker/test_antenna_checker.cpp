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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include <chrono>

#include "ACModel.hpp"
#include "ACUnionFind.hpp"
#include "AntennaChecker.hpp"
#include "Utility.hpp"

int main()
{
  imj::ACModel ac_model;
  if (ac_model.get_violation_num() != 0) {
    return 1;
  }
  ac_model.set_violation_num(2);
  ac_model.addViolationNum(3);
  if (ac_model.get_violation_num() != 5) {
    return 1;
  }

  const std::vector<std::pair<double, double>> pwl{{0.0, 0.0}, {2.0, 10.0}};
  if (!imj::Utility::equalDoubleByError(imj::Utility::getPWLValue(pwl, 1.0), 5.0, MJ_ERROR)) {
    return 1;
  }
  if (!imj::Utility::equalDoubleByError(imj::Utility::getPWLValue({}, 1.0, 3.0), 3.0, MJ_ERROR)) {
    return 1;
  }

  std::vector<imj::ACGraphNode> nodes(4);
  nodes[0].provides_diff = true;
  nodes[1].gate_area = 1.0;
  nodes[2].gate_area = 2.0;
  nodes[3].is_conductor = true;
  imj::ACUnionFind union_find(4, nodes);
  union_find.merge(0, 3);
  if (union_find.d[union_find.find(3)].gate_node_idx != -1) {
    return 1;
  }
  union_find.merge(2, 3);
  if (union_find.d[union_find.find(3)].gate_node_idx != 2) {
    return 1;
  }
  union_find.merge(1, 3);
  if (union_find.d[union_find.find(3)].gate_node_idx != 1 || union_find.d[union_find.find(3)].gate_area != 3.0) {
    return 1;
  }
  if (imj::GetACViolationTypeName()(imj::ACViolationType::kNone) != "None"
      || imj::GetACViolationTypeName()(imj::ACViolationType::kAntennaDiffCutCar) != "DiffCutCAR") {
    return 1;
  }

  // Scaling regression for high-fanout nets with LAYER-qualified pins and conductor unions.
  {
    constexpr int kFanout = 16000;
    std::vector<imj::ACGraphNode> hf_nodes(static_cast<size_t>(2 * kFanout + 1));
    hf_nodes[0].is_conductor = true;
    for (int i = 0; i < kFanout; ++i) {
      auto& g = hf_nodes[static_cast<size_t>(1 + i)];
      g.pin_id = i;
      g.layer_order = 1;
      g.gate_area = 1.0;

      auto& c = hf_nodes[static_cast<size_t>(1 + kFanout + i)];
      c.is_conductor = true;
    }
    const auto t0 = std::chrono::steady_clock::now();
    imj::ACUnionFind hf_uf(static_cast<int>(hf_nodes.size()), hf_nodes);
    for (int i = 0; i < kFanout; ++i) {
      hf_uf.merge(0, 1 + i);
    }
    for (int i = 0; i < kFanout; ++i) {
      hf_uf.merge(0, 1 + kFanout + i);
    }
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    const int root = hf_uf.find(0);
    if (!imj::Utility::equalDoubleByError(hf_uf.d[root].gate_area, static_cast<double>(kFanout), MJ_ERROR)) {
      return 1;
    }
    if (elapsed_ms > 500) {
      return 1;
    }
  }

  imj::AntennaChecker::initInst();
  imj::AntennaChecker* ac_instance = &MJAC;
  imj::AntennaChecker::initInst();
  if (ac_instance != &MJAC) {
    return 1;
  }
  imj::AntennaChecker::destroyInst();
  return 0;
}
