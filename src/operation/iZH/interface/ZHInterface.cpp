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
#include "ZHInterface.hpp"

#include "AntennaChecker.hpp"
#include "FanoutFixer.hpp"
#include "FillerInserter.hpp"
#include "MetalInserter.hpp"

namespace izh {

// public

ZHInterface& ZHInterface::getInst()
{
  if (_zh_interface_instance == nullptr) {
    _zh_interface_instance = new ZHInterface();
  }
  return *_zh_interface_instance;
}

void ZHInterface::destroyInst()
{
  if (_zh_interface_instance != nullptr) {
    delete _zh_interface_instance;
    _zh_interface_instance = nullptr;
  }
}

#if 1  // 外部调用ZH的API

#if 1  // izh

void ZHInterface::fixFanout(std::map<std::string, std::any> config_map)
{
  FanoutFixer::initInst();
  ZHFF.fix(config_map);
  FanoutFixer::destroyInst();
}

void ZHInterface::insertFiller(std::map<std::string, std::any> config_map)
{
  FillerInserter::initInst();
  ZHFI.insert(config_map);
  FillerInserter::destroyInst();
}

void ZHInterface::insertMetal(std::map<std::string, std::any> config_map)
{
  MetalInserter::initInst();
  ZHMI.insert(config_map);
  MetalInserter::destroyInst();
}

void ZHInterface::checkAntenna(std::map<std::string, std::any> config_map)
{
  static const auto type_to_str = [](const izh::ViolationType t) -> std::string {
    switch (t) {
      case izh::ViolationType::kAntennaPar: return "kAntennaPar";
      case izh::ViolationType::kAntennaDiffPar: return "kAntennaDiffPar";
      case izh::ViolationType::kAntennaCar: return "kAntennaCar";
      case izh::ViolationType::kAntennaDiffCar: return "kAntennaDiffCar";
      case izh::ViolationType::kAntennaPsr: return "kAntennaPsr";
      case izh::ViolationType::kAntennaDiffPsr: return "kAntennaDiffPsr";
      case izh::ViolationType::kAntennaCsr: return "kAntennaCsr";
      case izh::ViolationType::kAntennaDiffCsr: return "kAntennaDiffCsr";
      case izh::ViolationType::kAntennaCutPar: return "kAntennaCutPar";
      case izh::ViolationType::kAntennaCutCar: return "kAntennaCutCar";
      default: return "Unknown";
    }
  };

  AntennaChecker::initInst();
  ZHAC.check(config_map);

  _antenna_violations.clear();
  for (const auto& v : ZHAC.get_violations()) {
    AntennaViolation av;
    av.net_name = v.net_name;
    av.layer_name = v.layer_name;
    av.type = type_to_str(v.type);
    av.ratio = v.ratio;
    av.threshold = v.threshold;
    av.lx = v.lx;
    av.ly = v.ly;
    av.hx = v.hx;
    av.hy = v.hy;
    _antenna_violations.push_back(std::move(av));
  }

  AntennaChecker::destroyInst();
}

#endif

#endif

// private

ZHInterface* ZHInterface::_zh_interface_instance = nullptr;

}  // namespace izh
