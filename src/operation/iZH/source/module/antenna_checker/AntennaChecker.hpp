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

#include <any>
#include <map>
#include <string>
#include <vector>

#include "ACModel.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"

namespace izh {

enum class ViolationType
{
  kAntennaPar,
  kAntennaDiffPar,
  kAntennaCar,
  kAntennaDiffCar,
  kAntennaPsr,
  kAntennaDiffPsr,
  kAntennaCsr,
  kAntennaDiffCsr,
  kAntennaCutPar,
  kAntennaCutCar,
  kAntennaDiffCutPar,
  kAntennaDiffCutCar
};

struct Violation
{
  std::string net_name;
  std::string layer_name;
  ViolationType type = ViolationType::kAntennaPar;
  double ratio = 0.0;
  double threshold = 0.0;
  double lx = 0.0;
  double ly = 0.0;
  double hx = 0.0;
  double hy = 0.0;
};

#define ZHAC (izh::AntennaChecker::getInst())

class AntennaChecker
{
 public:
  static void initInst();
  static AntennaChecker& getInst();
  static void destroyInst();

  // function
  void check(std::map<std::string, std::any> config_map);

  int get_violation_num() const { return _violation_num; }
  const std::vector<Violation>& get_violations() const { return _violations; }
  void set_violations(const std::vector<Violation>& violations)
  {
    _violations = violations;
    _violation_num = static_cast<int>(_violations.size());
  }

 private:
  // self
  static AntennaChecker* _ac_instance;

  int _violation_num = 0;
  std::vector<Violation> _violations;

  AntennaChecker() = default;
  AntennaChecker(const AntennaChecker& other) = delete;
  AntennaChecker(AntennaChecker&& other) = delete;
  ~AntennaChecker() = default;

  AntennaChecker& operator=(const AntennaChecker& other) = delete;
  AntennaChecker& operator=(AntennaChecker&& other) = delete;

  // function
  ACModel initACModel(std::map<std::string, std::any>& config_map);
};

}  // namespace izh
