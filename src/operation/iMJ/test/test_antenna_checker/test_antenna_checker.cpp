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

  imj::AntennaChecker::initInst();
  imj::AntennaChecker* ac_instance = &MJAC;
  imj::AntennaChecker::initInst();
  if (ac_instance != &MJAC) {
    return 1;
  }
  imj::AntennaChecker::destroyInst();
  return 0;
}
