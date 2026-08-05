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
#include "LVSInterface.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "IdbDesign.h"
#include "IdbDie.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbSpecialNet.h"
#include "IdbVias.h"
#include "DataManager.hpp"
#include "ConnectType.hpp"
#include "DefRoutingData.hpp"
#include "EntityChecker.hpp"
#include "Logger.hpp"
#include "LVSReporter.hpp"
#include "Monitor.hpp"
#include "NetRoutingData.hpp"
#include "PDNChecker.hpp"
#include "RoutingChecker.hpp"
#include "RoutingShape.hpp"
#include "RoutingVia.hpp"
#include "Utility.hpp"
#include "idm.h"

namespace ilvs {

// public

LVSInterface& LVSInterface::getInst()
{
  if (_lvs_interface_instance == nullptr) {
    _lvs_interface_instance = new LVSInterface();
  }
  return *_lvs_interface_instance;
}

void LVSInterface::destroyInst()
{
  if (_lvs_interface_instance != nullptr) {
    delete _lvs_interface_instance;
    _lvs_interface_instance = nullptr;
  }
}

#if 1  // 外部调用LVS的API

#if 1  // iLVS

void LVSInterface::initLVS(std::map<std::string, std::any> config_map)
{
  Logger::initInst();
  // clang-format off
  LVSLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  LVSLOG.info(Loc::current(), "______________    _________    _____________________________________  ");
  LVSLOG.info(Loc::current(), "___(_)__  /__ |  / /_  ___/    __  ___/__  __/__    |__  __ \\__  __/ ");
  LVSLOG.info(Loc::current(), "__  /__  / __ | / /_____ \\     _____ \\__  /  __  /| |_  /_/ /_  /   ");
  LVSLOG.info(Loc::current(), "_  / _  /____ |/ / ____/ /     ____/ /_  /   _  ___ |  _, _/_  /      ");
  LVSLOG.info(Loc::current(), "/_/  /_____/____/  /____/      /____/ /_/    /_/  |_/_/ |_| /_/       ");
  LVSLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  LVSLOG.printLogFilePath();
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  DataManager::initInst();
  LVSDM.input(config_map);

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void LVSInterface::runLVS()
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  EntityChecker::initInst();
  LVSEC.check();
  EntityChecker::destroyInst();

  RoutingChecker::initInst();
  LVSRC.check();
  RoutingChecker::destroyInst();

  PDNChecker::initInst();
  LVSPC.check();
  PDNChecker::destroyInst();

  LVSReporter::initInst();
  LVSLR.report();
  LVSReporter::destroyInst();

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void LVSInterface::destroyLVS()
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  LVSDM.output();
  DataManager::destroyInst();

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
  LVSLOG.printLogFilePath();
  // clang-format off
  LVSLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  LVSLOG.info(Loc::current(), "______________    _________    _____________________   _____________________  __  ");
  LVSLOG.info(Loc::current(), "___(_)__  /__ |  / /_  ___/    ___  ____/___  _/__  | / /___  _/_  ___/__  / / /  ");
  LVSLOG.info(Loc::current(), "__  /__  / __ | / /_____ \\     __  /_    __  / __   |/ / __  / _____ \\__  /_/ / ");
  LVSLOG.info(Loc::current(), "_  / _  /____ |/ / ____/ /     _  __/   __/ /  _  /|  / __/ /  ____/ /_  __  /    ");
  LVSLOG.info(Loc::current(), "/_/  /_____/____/  /____/      /_/      /___/  /_/ |_/  /___/  /____/ /_/ /_/     ");
  LVSLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  Logger::destroyInst();
}

#endif

#endif

#if 1  // LVS调用外部的API

#if 1  // 顶层数据

#if 1  // 输入

void LVSInterface::input(std::map<std::string, std::any>& config_map)
{
  wrapConfig(config_map);
  wrapDatabase();
}

void LVSInterface::wrapConfig(std::map<std::string, std::any>& config_map)
{
  LVSDM.getConfig().temp_directory_path = LVSUTIL.getConfigValue<std::string>(config_map, "-temp_directory_path", "./lvs_temp_directory");
  LVSDM.getConfig().thread_number = LVSUTIL.getConfigValue<int32_t>(config_map, "-thread_number", 128);
  omp_set_num_threads(std::max(LVSDM.getConfig().thread_number, 1));
}

void LVSInterface::wrapDatabase()
{
  if (dmInst->get_config().get_def_path().empty()) {
    LVSLOG.error(Loc::current(), "Direct iLVS database wrapping requires def_init before init_lvs.");
  }

  idb::IdbDesign* netlist_idb_design = dmInst->get_netlist_idb_design();
  idb::IdbDesign* def_idb_design = dmInst->get_def_idb_design();
  if (netlist_idb_design == nullptr || def_idb_design == nullptr) {
    LVSLOG.error(Loc::current(), "Direct iLVS requires both netlist and DEF IDB design views.");
  }

  NetlistData netlist_data = wrapNetlistData(netlist_idb_design);
  DefData def_data = wrapDefData(def_idb_design);
  if (netlist_data.get_design_name().empty() || def_data.get_design_name().empty()) {
    LVSLOG.error(Loc::current(), "Direct iLVS IDB views must both contain a design name.");
  }
  if (netlist_data.get_design_name() != def_data.get_design_name()) {
    LVSLOG.error(Loc::current(), "Direct iLVS IDB design names differ: netlist='", netlist_data.get_design_name(), "' def='",
                 def_data.get_design_name(), "'.");
  }

  Database& database = LVSDM.getDatabase();
  database.set_netlist_data(std::move(netlist_data));
  database.set_def_data(std::move(def_data));
  if (netlist_idb_design == def_idb_design) {
    LVSLOG.info(Loc::current(), "Using the temporary shared DEF IDB design for both netlist and DEF views.");
  }
  LVSLOG.info(Loc::current(), "Wrapped direct iLVS IDB views: netlist_nets=", database.get_netlist_data().get_net_map().size(),
              " def_nets=", database.get_def_data().get_net_map().size(), " def_routing_nets=",
              database.get_def_data().get_def_routing_data().get_net_routing_data_map().size(), ".");
}

NetlistData LVSInterface::wrapNetlistData(idb::IdbDesign* design)
{
  NetlistData netlist_data;
  wrapDesignData(design, netlist_data);
  return netlist_data;
}

DefData LVSInterface::wrapDefData(idb::IdbDesign* design)
{
  DefData def_data;
  wrapDesignData(design, def_data);
  wrapDie(design, def_data);
  wrapDefRoutingData(design, def_data);
  return def_data;
}

void LVSInterface::wrapDie(idb::IdbDesign* design, DefData& def_data)
{
  if (design == nullptr || design->get_layout() == nullptr || design->get_layout()->get_die() == nullptr) {
    return;
  }

  idb::IdbDie* idb_die = design->get_layout()->get_die();
  Die& die = def_data.get_die();
  die.set_real_ll(idb_die->get_llx(), idb_die->get_lly());
  die.set_real_ur(idb_die->get_urx(), idb_die->get_ury());
}

void LVSInterface::wrapDesignData(idb::IdbDesign* design, DesignData& design_data)
{
  if (design == nullptr) {
    return;
  }

  design_data.set_design_name(design->get_design_name());
  wrapInstanceList(design, design_data);
  wrapIOPinList(design, design_data);
  wrapNetList(design, design_data);
  wrapPowerGroundTerminal(design, design_data);
}

void LVSInterface::wrapInstanceList(idb::IdbDesign* design, DesignData& design_data)
{
  if (design == nullptr) {
    return;
  }

  if (idb::IdbInstanceList* instance_list = design->get_instance_list(); instance_list != nullptr) {
    for (idb::IdbInstance* instance : instance_list->get_instance_list()) {
      if (instance != nullptr) {
        wrapInstance(instance, design_data);
      }
    }
  }
}

void LVSInterface::wrapInstance(idb::IdbInstance* instance, DesignData& design_data)
{
  if (instance != nullptr) {
    design_data.get_instance_name_set().insert(instance->get_name());
  }
}

void LVSInterface::wrapIOPinList(idb::IdbDesign* design, DesignData& design_data)
{
  if (design == nullptr) {
    return;
  }

  if (idb::IdbPins* io_pin_list = design->get_io_pin_list(); io_pin_list != nullptr) {
    for (idb::IdbPin* pin : io_pin_list->get_pin_list()) {
      std::string terminal_name = wrapDesignTerminal(pin, design_data);
      if (!terminal_name.empty()) {
        design_data.get_io_terminal_name_list().push_back(terminal_name);
      }
    }
  }
}

std::string LVSInterface::wrapDesignTerminal(idb::IdbPin* pin, DesignData& design_data)
{
  if (pin == nullptr) {
    return "";
  }
  std::string terminal_name = getTerminalName(pin);
  if (terminal_name.empty()) {
    return "";
  }
  design_data.get_terminal_connect_type_map()[terminal_name] = ConnectType::kNone;
  return terminal_name;
}

void LVSInterface::wrapNetList(idb::IdbDesign* design, DesignData& design_data)
{
  if (design == nullptr) {
    return;
  }

  if (idb::IdbNetList* net_list = design->get_net_list(); net_list != nullptr) {
    for (idb::IdbNet* idb_net : net_list->get_net_list()) {
      if (idb_net == nullptr) {
        continue;
      }
      std::string net_name = idb_net->get_net_name();
      Net net;
      wrapNetPinList(idb_net->get_io_pins(), net, design_data);
      wrapNetPinList(idb_net->get_instance_pin_list(), net, design_data);
      design_data.get_net_map()[net_name] = std::move(net);
    }
  }
}

void LVSInterface::wrapNetPinList(idb::IdbPins* pin_list, Net& net, DesignData& design_data)
{
  if (pin_list == nullptr) {
    return;
  }

  for (idb::IdbPin* pin : pin_list->get_pin_list()) {
    std::string terminal_name = wrapDesignTerminal(pin, design_data);
    if (terminal_name.empty()) {
      continue;
    }
    net.get_terminal_name_list().push_back(terminal_name);
    if (idb::IdbInstance* instance = pin->get_instance(); instance != nullptr) {
      wrapInstance(instance, design_data);
    }
  }
}

void LVSInterface::wrapPowerGroundTerminal(idb::IdbDesign* design, DesignData& design_data)
{
  if (design == nullptr || design->get_special_net_list() == nullptr) {
    return;
  }

  for (idb::IdbSpecialNet* special_net : design->get_special_net_list()->get_net_list()) {
    if (special_net == nullptr || (!special_net->is_vdd() && !special_net->is_vss())) {
      continue;
    }
    ConnectType connect_type = special_net->is_vdd() ? ConnectType::kPower : ConnectType::kGround;
    std::unordered_set<idb::IdbPin*> special_pin_set;
    if (idb::IdbPins* io_pin_list = special_net->get_io_pins(); io_pin_list != nullptr) {
      for (idb::IdbPin* pin : io_pin_list->get_pin_list()) {
        wrapPowerGroundPin(pin, design_data, connect_type, special_pin_set);
      }
    }
    if (idb::IdbPins* instance_pin_list = special_net->get_instance_pin_list(); instance_pin_list != nullptr) {
      for (idb::IdbPin* pin : instance_pin_list->get_pin_list()) {
        wrapPowerGroundPin(pin, design_data, connect_type, special_pin_set);
      }
    }
    if (special_net->has_wildcard_instance_pins() && design->get_instance_list() != nullptr) {
      for (idb::IdbInstance* instance : design->get_instance_list()->get_instance_list()) {
        if (instance == nullptr || instance->get_pin_list() == nullptr) {
          continue;
        }
        for (idb::IdbPin* pin : instance->get_pin_list()->get_pin_list()) {
          if (design->findSpecialNetForInstancePin(pin) == special_net) {
            wrapPowerGroundPin(pin, design_data, connect_type, special_pin_set);
          }
        }
      }
    }
  }
}

void LVSInterface::wrapPowerGroundPin(idb::IdbPin* pin, DesignData& design_data, const ConnectType connect_type,
                                       std::unordered_set<idb::IdbPin*>& pin_set)
{
  if (pin == nullptr || !pin_set.insert(pin).second) {
    return;
  }
  std::string terminal_name = getTerminalName(pin);
  if (terminal_name.empty()) {
    return;
  }
  design_data.get_terminal_connect_type_map()[terminal_name] = connect_type;
}

void LVSInterface::wrapDefRoutingData(idb::IdbDesign* design, DefData& def_data)
{
  if (design == nullptr) {
    return;
  }

  wrapNetRoutingData(design, def_data);
  wrapSpecialNetRoutingData(design, def_data);
}

void LVSInterface::wrapNetRoutingData(idb::IdbDesign* design, DefData& def_data)
{
  if (design == nullptr) {
    return;
  }

  DefRoutingData& def_routing_data = def_data.get_def_routing_data();
  if (idb::IdbNetList* net_list = design->get_net_list(); net_list != nullptr) {
    for (idb::IdbNet* idb_net : net_list->get_net_list()) {
      if (idb_net == nullptr) {
        continue;
      }
      std::string net_name = idb_net->get_net_name();
      NetRoutingData& net_routing_data = def_routing_data.get_net_routing_data_map()[net_name];
      if (idb_net->get_pin_number() > 0) {
        if (idb::IdbPin* driving_pin = idb_net->get_driving_pin(); driving_pin != nullptr) {
          net_routing_data.set_driver_terminal_name(getTerminalName(driving_pin));
        }
      }
      if (idb::IdbPins* io_pin_list = idb_net->get_io_pins(); io_pin_list != nullptr) {
        for (idb::IdbPin* pin : io_pin_list->get_pin_list()) {
          wrapRoutingDataPin(net_name, pin, false, false, def_data);
        }
      }
      if (idb::IdbPins* instance_pin_list = idb_net->get_instance_pin_list(); instance_pin_list != nullptr) {
        for (idb::IdbPin* pin : instance_pin_list->get_pin_list()) {
          wrapRoutingDataPin(net_name, pin, false, false, def_data);
        }
      }
      if (idb::IdbRegularWireList* wire_list = idb_net->get_wire_list(); wire_list != nullptr) {
        for (idb::IdbRegularWire* wire : wire_list->get_wire_list()) {
          if (wire == nullptr) {
            continue;
          }
          for (idb::IdbRegularWireSegment* segment : wire->get_segment_list()) {
            if (segment == nullptr) {
              continue;
            }
            if (segment->is_via()) {
              for (idb::IdbVia* via : segment->get_via_list()) {
                wrapRoutingDataVia(via, net_routing_data);
              }
            } else if (segment->get_layer() != nullptr && segment->get_layer()->is_routing() && (segment->is_wire() || segment->is_rect())) {
              net_routing_data.get_wire_routing_shape_list().push_back(wrapRoutingDataShape(segment->get_layer(), getPhysicalSegmentRect(segment)));
            }
          }
        }
      }
    }
  }
}

void LVSInterface::wrapRoutingDataPin(const std::string& net_name, idb::IdbPin* pin, const bool is_power_net,
                                      const bool is_ground_net, DefData& def_data)
{
  if (pin == nullptr) {
    return;
  }

  DefRoutingData& def_routing_data = def_data.get_def_routing_data();
  std::string terminal_name = getTerminalName(pin);
  if (terminal_name.empty()) {
    return;
  }
  ConnectType connect_type = ConnectType::kNone;
  if (is_power_net) {
    connect_type = ConnectType::kPower;
  } else if (is_ground_net) {
    connect_type = ConnectType::kGround;
  }
  def_data.get_terminal_connect_type_map()[terminal_name] = connect_type;
  if (!pin->is_io_pin()) {
    if (is_power_net) {
      def_routing_data.get_power_instance_pin_net_map()[terminal_name] = net_name;
    } else if (is_ground_net) {
      def_routing_data.get_ground_instance_pin_net_map()[terminal_name] = net_name;
    }
  }

  std::vector<RoutingShape> terminal_routing_shape_list;
  for (idb::IdbLayerShape* layer_shape : pin->get_port_box_list()) {
    if (layer_shape == nullptr || layer_shape->get_layer() == nullptr || !layer_shape->get_layer()->is_routing()) {
      continue;
    }
    for (idb::IdbRect* rect : layer_shape->get_rect_list()) {
      if (rect == nullptr) {
        continue;
      }
      terminal_routing_shape_list.push_back(wrapRoutingDataShape(layer_shape->get_layer(), *rect));
    }
  }
  if (!terminal_routing_shape_list.empty()) {
    def_routing_data.get_net_routing_data_map()[net_name].get_terminal_routing_shape_map()[terminal_name] =
        std::move(terminal_routing_shape_list);
  }
}

RoutingShape LVSInterface::wrapRoutingDataShape(idb::IdbLayer* layer, const idb::IdbRect& rect)
{
  RoutingShape routing_shape;
  routing_shape.set_shape(wrapShape(layer->get_id(), rect));
  routing_shape.set_layer_order(layer->get_order());
  return routing_shape;
}

void LVSInterface::wrapRoutingDataVia(idb::IdbVia* via, NetRoutingData& net_routing_data)
{
  if (via == nullptr) {
    return;
  }

  idb::IdbLayerShape bottom_shape = via->get_bottom_layer_shape();
  idb::IdbLayerShape top_shape = via->get_top_layer_shape();
  if (bottom_shape.get_layer() == nullptr || top_shape.get_layer() == nullptr || !bottom_shape.get_layer()->is_routing()
      || !top_shape.get_layer()->is_routing()) {
    return;
  }

  RoutingVia routing_via;
  routing_via.set_bottom_routing_shape(wrapRoutingDataShape(bottom_shape.get_layer(), bottom_shape.get_bounding_box()));
  routing_via.set_top_routing_shape(wrapRoutingDataShape(top_shape.get_layer(), top_shape.get_bounding_box()));
  net_routing_data.get_routing_via_list().push_back(std::move(routing_via));
}

void LVSInterface::wrapSpecialNetRoutingData(idb::IdbDesign* design, DefData& def_data)
{
  if (design == nullptr) {
    return;
  }

  DefRoutingData& def_routing_data = def_data.get_def_routing_data();
  if (idb::IdbSpecialNetList* special_net_list = design->get_special_net_list(); special_net_list != nullptr) {
    for (idb::IdbSpecialNet* special_net : special_net_list->get_net_list()) {
      if (special_net == nullptr) {
        continue;
      }
      std::string net_name = special_net->get_net_name();
      NetRoutingData& net_routing_data = def_routing_data.get_net_routing_data_map()[net_name];
      bool is_power_net = special_net->is_vdd();
      bool is_ground_net = special_net->is_vss();
      if (is_power_net) {
        def_routing_data.get_power_net_name_set().insert(net_name);
      } else if (is_ground_net) {
        def_routing_data.get_ground_net_name_set().insert(net_name);
      }

      std::unordered_set<idb::IdbPin*> special_pin_set;
      if (idb::IdbPins* io_pin_list = special_net->get_io_pins(); io_pin_list != nullptr) {
        for (idb::IdbPin* pin : io_pin_list->get_pin_list()) {
          if (pin != nullptr && special_pin_set.insert(pin).second) {
            wrapRoutingDataPin(net_name, pin, is_power_net, is_ground_net, def_data);
          }
        }
      }
      if (idb::IdbPins* instance_pin_list = special_net->get_instance_pin_list(); instance_pin_list != nullptr) {
        for (idb::IdbPin* pin : instance_pin_list->get_pin_list()) {
          if (pin != nullptr && special_pin_set.insert(pin).second) {
            wrapRoutingDataPin(net_name, pin, is_power_net, is_ground_net, def_data);
          }
        }
      }
      if (special_net->has_wildcard_instance_pins() && design->get_instance_list() != nullptr) {
        for (idb::IdbInstance* instance : design->get_instance_list()->get_instance_list()) {
          if (instance == nullptr || instance->get_pin_list() == nullptr) {
            continue;
          }
          for (idb::IdbPin* pin : instance->get_pin_list()->get_pin_list()) {
            if (pin == nullptr || design->findSpecialNetForInstancePin(pin) != special_net || !special_pin_set.insert(pin).second) {
              continue;
            }
            wrapRoutingDataPin(net_name, pin, is_power_net, is_ground_net, def_data);
          }
        }
      }
      if (idb::IdbSpecialWireList* wire_list = special_net->get_wire_list(); wire_list != nullptr) {
        for (idb::IdbSpecialWire* wire : wire_list->get_wire_list()) {
          if (wire == nullptr) {
            continue;
          }
          for (idb::IdbSpecialWireSegment* segment : wire->get_segment_list()) {
            if (segment == nullptr) {
              continue;
            }
            if (segment->is_via()) {
              wrapRoutingDataVia(segment->get_via(), net_routing_data);
            } else if (segment->get_layer() != nullptr && segment->get_layer()->is_routing() && segment->is_line()) {
              net_routing_data.get_wire_routing_shape_list().push_back(
                  wrapRoutingDataShape(segment->get_layer(),
                                       idb::IdbRect(segment->get_point_start(), segment->get_point_second(), segment->get_route_width())));
            } else if (segment->get_layer() != nullptr && segment->get_layer()->is_routing() && segment->is_rect()
                       && segment->get_delta_rect() != nullptr) {
              net_routing_data.get_wire_routing_shape_list().push_back(
                  wrapRoutingDataShape(segment->get_layer(), *segment->get_delta_rect()));
            }
          }
        }
      }
    }
  }
}

Shape LVSInterface::wrapShape(const int32_t layer_idx, idb::IdbRect idb_rect)
{
  Shape shape;
  shape.set_layer_idx(layer_idx);
  shape.set_ll_x(idb_rect.get_low_x());
  shape.set_ll_y(idb_rect.get_low_y());
  shape.set_ur_x(idb_rect.get_high_x());
  shape.set_ur_y(idb_rect.get_high_y());
  return shape;
}

idb::IdbRect LVSInterface::getPhysicalSegmentRect(idb::IdbRegularWireSegment* idb_segment)
{
  idb::IdbRect rect = idb_segment->get_segment_rect();
  if (!idb_segment->is_rect()) {
    return rect;
  }
  // DEF path RECT coordinates are offsets from the preceding path point.
  if (idb::IdbCoordinate<int32_t>* point = idb_segment->get_point_start(); point != nullptr) {
    rect.moveByStep(point->get_x(), point->get_y());
  }
  return rect;
}

std::string LVSInterface::getTerminalName(idb::IdbPin* pin)
{
  if (pin == nullptr) {
    return "";
  }
  if (pin->is_io_pin()) {
    return LVSUTIL.getIOName(pin->get_pin_name());
  }
  if (idb::IdbInstance* instance = pin->get_instance(); instance != nullptr) {
    return LVSUTIL.getInstancePinName(instance->get_name(), pin->get_pin_name());
  }
  return LVSUTIL.getIOName(pin->get_pin_name());
}

#endif

#if 1  // 输出

void LVSInterface::output()
{
}

#endif

#endif

#endif

// private

LVSInterface* LVSInterface::_lvs_interface_instance = nullptr;

}  // namespace ilvs
