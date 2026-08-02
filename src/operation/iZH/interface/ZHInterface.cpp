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
  AntennaChecker::initInst();
  ZHAC.check(config_map);
  AntennaChecker::destroyInst();
}

#endif

#endif

// private

ZHInterface* ZHInterface::_zh_interface_instance = nullptr;

}  // namespace izh
