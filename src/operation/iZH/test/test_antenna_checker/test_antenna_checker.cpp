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
#include "AntennaChecker.hpp"
#include "AntennaRuleEvaluator.hpp"
#include "Utility.hpp"

int main()
{
  izh::ACModel ac_model;
  if (ac_model.get_violation_num() != 0) {
    return 1;
  }
  ac_model.set_violation_num(2);
  ac_model.addViolationNum(3);
  if (ac_model.get_violation_num() != 5) {
    return 1;
  }

  const std::vector<std::pair<double, double>> pwl{{0.0, 0.0}, {2.0, 10.0}};
  if (!izh::Utility::equalDoubleByError(izh::Utility::getPWLValue(pwl, 1.0), 5.0, ZH_ERROR)) {
    return 1;
  }
  if (!izh::Utility::equalDoubleByError(izh::Utility::getPWLValue({}, 1.0, 3.0), 3.0, ZH_ERROR)) {
    return 1;
  }

  izh::ACThresholdPick pick = izh::AntennaRuleEvaluator::pickThreshold(-1.0, -1.0, pwl, 1.0, true);
  if (!pick.available || !izh::Utility::equalDoubleByError(pick.threshold, 5.0, ZH_ERROR)) {
    return 1;
  }

  izh::AntennaChecker::initInst();
  izh::AntennaChecker* ac_instance = &ZHAC;
  izh::AntennaChecker::initInst();
  if (ac_instance != &ZHAC) {
    return 1;
  }
  izh::AntennaChecker::destroyInst();
  return 0;
}
