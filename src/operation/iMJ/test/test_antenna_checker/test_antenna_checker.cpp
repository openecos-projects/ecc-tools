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

  imj::AntennaChecker::initInst();
  imj::AntennaChecker* ac_instance = &MJAC;
  imj::AntennaChecker::initInst();
  if (ac_instance != &MJAC) {
    return 1;
  }
  imj::AntennaChecker::destroyInst();
  return 0;
}
