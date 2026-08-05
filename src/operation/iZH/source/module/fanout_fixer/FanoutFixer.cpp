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
#include "FanoutFixer.hpp"

#include "Utility.hpp"
#include "idm.h"

namespace izh {

// public

void FanoutFixer::initInst()
{
  if (_ff_instance == nullptr) {
    _ff_instance = new FanoutFixer();
  }
}

FanoutFixer& FanoutFixer::getInst()
{
  if (_ff_instance == nullptr) {
    ZHLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_ff_instance;
}

void FanoutFixer::destroyInst()
{
  if (_ff_instance != nullptr) {
    delete _ff_instance;
    _ff_instance = nullptr;
  }
}

// function

void FanoutFixer::fix(std::map<std::string, std::any> config_map)
{
  Monitor monitor;
  ZHLOG.info(Loc::current(), "Starting...");

  FFModel ff_model = initFFModel(config_map);
  std::string buffer_name = ff_model.get_buffer_name();
  int32_t max_fanout = ff_model.get_max_fanout();

  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(0), "ZH fixFanout");
  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(1), "buffer_name: ", buffer_name);
  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(1), "max_fanout: ", max_fanout);

  auto* idb_design = dmInst->get_idb_def_service()->get_design();
  idb::IdbNetList* idb_net_list = idb_design->get_net_list();

  while (true) {
    std::set<idb::IdbNet*> origin_net_set;
    for (idb::IdbNet* idb_net : idb_net_list->get_net_list()) {
      if (idb_net->get_connect_type() == idb::IdbConnectType::kClock) {
        continue;
      }
      if (static_cast<int32_t>(idb_net->get_load_pins().size()) <= max_fanout) {
        continue;
      }
      origin_net_set.insert(idb_net);
    }
    if (origin_net_set.empty()) {
      break;
    }
    size_t begin_net_num = idb_net_list->get_num();
    size_t inserted_buffer_num = 0;
    for (idb::IdbNet* origin_net : origin_net_set) {
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
        idb::IdbNet* new_net = idb_design->createOrFindNet(idb_design->makeUniqueNetName(ZHUTIL.getString("zh_fanout_net_", new_idx++)),
                                                           idb::IdbConnectType::kSignal, idb::IdbCreatePolicy::kErrorIfExists);
        idb::IdbInstance* new_buf = idb_design->createInstance(idb_design->makeUniqueInstanceName(ZHUTIL.getString("zh_fanout_buf_", new_idx++)),
                                                               buffer_name, idb::IdbInstanceType::kTiming, idb::IdbPlacementStatus::kNone,
                                                               idb::IdbOrient::kNone, 0, 0, idb::IdbCreatePolicy::kErrorIfExists);
        if (new_net == nullptr || new_buf == nullptr) {
          ZHLOG.error(Loc::current(), "new_net == nullptr || new_buf == nullptr!");
        }
        inserted_buffer_num++;
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
        for (idb::IdbPin* load_pin : load_pin_list) {
          idb_design->connectPinToNet(load_pin, new_net);
        }
      }
    }
    size_t inserted_net_num = idb_net_list->get_num() - begin_net_num;
    ff_model.addFixedNetNum(origin_net_set.size());
    ff_model.addInsertedNetNum(inserted_net_num);
    ff_model.addInsertedBufferNum(inserted_buffer_num);
    ZHLOG.info(Loc::current(), "Fixed ", origin_net_set.size(), " nets!( +", inserted_net_num, " nets, +", inserted_buffer_num, " buffers )");
  }

  ZHLOG.info(Loc::current(), "Total fixed ", ff_model.get_fixed_net_num(), " nets, inserted ", ff_model.get_inserted_net_num(), " nets and ",
             ff_model.get_inserted_buffer_num(), " buffers");
  ZHLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

FFModel FanoutFixer::initFFModel(std::map<std::string, std::any>& config_map)
{
  FFModel ff_model;
  ff_model.set_buffer_name(ZHUTIL.getConfigValue<std::string>(config_map, "-buffer_name", "zh_buffer"));
  ff_model.set_max_fanout(ZHUTIL.getConfigValue<int32_t>(config_map, "-max_fanout", 32));
  if (ff_model.get_max_fanout() <= 0) {
    ZHLOG.error(Loc::current(), "The max_fanout must be greater than 0!");
  }
  return ff_model;
}

FanoutFixer* FanoutFixer::_ff_instance = nullptr;

}  // namespace izh
