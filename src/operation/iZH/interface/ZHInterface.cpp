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

#include "Monitor.hpp"
#include "Utility.hpp"
#include "idm.h"

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
  ZHLOG.info(Loc::current(), "ZH fixFanout");

  std::string buffer_name = ZHUTIL.getConfigValue<std::string>(config_map, "-buffer_name", "buffer_name");
  auto* idb_design = dmInst->get_idb_def_service()->get_design();
  idb::IdbNetList* idb_net_list = idb_design->get_net_list();

  size_t max_fanout = 32;
  while (true) {
    std::set<idb::IdbNet*> origin_net_set;
    for (idb::IdbNet* idb_net : idb_net_list->get_net_list()) {
      if (idb_net->get_connect_type() == idb::IdbConnectType::kSignal && idb_net->get_load_pins().size() > max_fanout) {
        origin_net_set.insert(idb_net);
      }
    }
    if (origin_net_set.empty()) {
      break;
    }
    size_t begin_net_num = idb_net_list->get_num();
    for (idb::IdbNet* origin_net : origin_net_set) {
      // 解开所有的pin
      std::vector<idb::IdbPin*> load_pin_list = origin_net->get_load_pins();
      for (idb::IdbPin* load_pin : load_pin_list) {
        idb_design->disconnectPinFromNet(load_pin);
      }
      std::vector<std::vector<idb::IdbPin*>> load_pin_list_list;
      for (size_t i = 0; i < load_pin_list.size(); i += max_fanout) {
        size_t end = std::min(i + max_fanout, load_pin_list.size());
        load_pin_list_list.emplace_back(load_pin_list.begin() + i, load_pin_list.begin() + end);
      }
      for (std::vector<idb::IdbPin*>& load_pin_list : load_pin_list_list) {
        static size_t new_idx = 0;
        // 生成net
        idb::IdbNet* new_net = idb_design->createOrFindNet(idb_design->makeUniqueNetName(ZHUTIL.getString("zh_fanout_net_", new_idx++)),
                                                           idb::IdbConnectType::kSignal, idb::IdbCreatePolicy::kErrorIfExists);
        // 生成buf
        idb::IdbInstance* new_buf = idb_design->createInstance(idb_design->makeUniqueInstanceName(ZHUTIL.getString("zh_fanout_buf_", new_idx++)), buffer_name,
                                                               idb::IdbInstanceType::kTiming, idb::IdbPlacementStatus::kNone, idb::IdbOrient::kNone, 0, 0,
                                                               idb::IdbCreatePolicy::kErrorIfExists);
        if (new_net == nullptr || new_buf == nullptr) {
          ZHLOG.error(Loc::current(), "new_net == nullptr || new_buf == nullptr!");
        }
        // 连接buf
        for (idb::IdbPin* buf_pin : new_buf->get_pin_list()->get_pin_list()) {
          if (buf_pin->get_term()->get_type() == idb::IdbConnectType::kPower || buf_pin->get_term()->get_type() == idb::IdbConnectType::kGround) {
            continue;
          }
          if (buf_pin->get_term()->get_direction() == idb::IdbConnectDirection::kInput) {
            idb_design->connectPinToNet(buf_pin, origin_net);
          } else if (buf_pin->get_term()->get_direction() == idb::IdbConnectDirection::kOutput) {
            idb_design->connectPinToNet(buf_pin, new_net);
          }
        }
        // 连接pin
        for (idb::IdbPin* load_pin : load_pin_list) {
          idb_design->connectPinToNet(load_pin, new_net);
        }
      }
    }
    ZHLOG.info(Loc::current(), "Fixed ", origin_net_set.size(), " nets!( +", idb_net_list->get_num() - begin_net_num, " nets )");
  }
}

void ZHInterface::insertFiller(std::map<std::string, std::any> config_map)
{
  ZHLOG.info(Loc::current(), "ZH insertFiller");
}

#endif

#endif

// private

ZHInterface* ZHInterface::_zh_interface_instance = nullptr;

}  // namespace izh
