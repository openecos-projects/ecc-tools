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
#include "EMIRInterface.hpp"

#include "DataManager.hpp"
#include "EMAnalyzer.hpp"
#include "EMIRReporter.hpp"
#include "GraphBuilder.hpp"
#include "IRAnalyzer.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "PowerNet.hpp"
#include "PowerNetType.hpp"
#include "PowerPin.hpp"
#include "PowerVia.hpp"
#include "PowerWireSegment.hpp"
#include "Utility.hpp"
#include "idm.h"

namespace iemir {

EMIRInterface* EMIRInterface::_emir_interface_instance = nullptr;

// public

EMIRInterface& EMIRInterface::getInst()
{
  if (_emir_interface_instance == nullptr) {
    _emir_interface_instance = new EMIRInterface();
  }
  return *_emir_interface_instance;
}

void EMIRInterface::destroyInst()
{
  if (_emir_interface_instance != nullptr) {
    delete _emir_interface_instance;
    _emir_interface_instance = nullptr;
  }
}

#if 1  // 外部调用EMIR的API

#if 1  // iEMIR

void EMIRInterface::initEMIR(std::map<std::string, std::any> config_map)
{
  Logger::initInst();
  // clang-format off
  EMIRLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  EMIRLOG.info(Loc::current(), "____________________  _________________     _____________________________________  ");
  EMIRLOG.info(Loc::current(), "___(_)__  ____/__   |/  /___  _/__  __ \\    __  ___/__  __/__    |__  __ \\__  __/");
  EMIRLOG.info(Loc::current(), "__  /__  __/  __  /|_/ / __  / __  /_/ /    _____ \\__  /  __  /| |_  /_/ /_  /    ");
  EMIRLOG.info(Loc::current(), "_  / _  /___  _  /  / / __/ /  _  _, _/     ____/ /_  /   _  ___ |  _, _/_  /      ");
  EMIRLOG.info(Loc::current(), "/_/  /_____/  /_/  /_/  /___/  /_/ |_|      /____/ /_/    /_/  |_/_/ |_| /_/       ");
  EMIRLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  EMIRLOG.printLogFilePath();
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  Monitor monitor;
  EMIRLOG.info(Loc::current(), "Starting...");

  DataManager::initInst();
  EMIRDM.input(config_map);

  EMIRLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EMIRInterface::runEMIR()
{
  Monitor monitor;
  EMIRLOG.info(Loc::current(), "Starting...");

  GraphBuilder::initInst();
  EMIRGB.build();
  GraphBuilder::destroyInst();

  IRAnalyzer::initInst();
  EMIRIA.analyze();
  IRAnalyzer::destroyInst();

  EMAnalyzer::initInst();
  EMIREA.analyze();
  EMAnalyzer::destroyInst();

  EMIRReporter::initInst();
  EMIRER.report();
  EMIRReporter::destroyInst();

  EMIRLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EMIRInterface::destroyEMIR()
{
  Monitor monitor;
  EMIRLOG.info(Loc::current(), "Starting...");

  EMIRDM.output();
  DataManager::destroyInst();

  EMIRLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());

  EMIRLOG.printLogFilePath();
  // clang-format off
  EMIRLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  EMIRLOG.info(Loc::current(), "____________________  _________________     _____________________   _____________________  __ ");
  EMIRLOG.info(Loc::current(), "___(_)__  ____/__   |/  /___  _/__  __ \\    ___  ____/___  _/__  | / /___  _/_  ___/__  / / /");
  EMIRLOG.info(Loc::current(), "__  /__  __/  __  /|_/ / __  / __  /_/ /    __  /_    __  / __   |/ / __  / _____ \\__  /_/ / ");
  EMIRLOG.info(Loc::current(), "_  / _  /___  _  /  / / __/ /  _  _, _/     _  __/   __/ /  _  /|  / __/ /  ____/ /_  __  /   ");
  EMIRLOG.info(Loc::current(), "/_/  /_____/  /_/  /_/  /___/  /_/ |_|      /_/      /___/  /_/ |_/  /___/  /____/ /_/ /_/    ");
  EMIRLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  Logger::destroyInst();
}

#endif

#endif

#if 1  // EMIR调用外部的API

#if 1  // TopData

#if 1  // input

void EMIRInterface::input(std::map<std::string, std::any>& config_map)
{
  wrapConfig(config_map);
  wrapDatabase();
}

void EMIRInterface::wrapConfig(std::map<std::string, std::any>& config_map)
{
  /////////////////////////////////////////////
  EMIRDM.getConfig().temp_directory_path = EMIRUTIL.getConfigValue<std::string>(config_map, "-temp_directory_path", "./emir_temp_directory");
  EMIRDM.getConfig().instance_power_file_path = EMIRUTIL.getConfigValue<std::string>(config_map, "-instance_power_file_path", "");
  EMIRDM.getConfig().thread_number = EMIRUTIL.getConfigValue<int32_t>(config_map, "-thread_number", 128);
  omp_set_num_threads(std::max(EMIRDM.getConfig().thread_number, 1));
  /////////////////////////////////////////////
}

void EMIRInterface::wrapDatabase()
{
  wrapDBInfo();
  wrapInstanceIdSet();
  wrapPowerNetList();
}

void EMIRInterface::wrapDBInfo()
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  EMIRDM.getDatabase().set_design_name(idb_design->get_design_name());
  EMIRDM.getDatabase().set_micron_dbu(idb_design->get_units()->get_micron_dbu());
}

void EMIRInterface::wrapInstanceIdSet()
{
  std::set<uint64_t>& instance_id_set = EMIRDM.getDatabase().get_instance_id_set();
  instance_id_set.clear();
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (idb::IdbInstance* idb_instance : idb_design->get_instance_list()->get_instance_list()) {
    instance_id_set.insert(idb_instance->get_id());
  }
}

void EMIRInterface::wrapPowerNetList()
{
  std::map<std::string, PowerNet>& power_net_map = EMIRDM.getDatabase().get_power_net_map();
  power_net_map.clear();
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (idb::IdbSpecialNet* idb_power_net : idb_design->get_special_net_list()->get_net_list()) {
    PowerNetType power_net_type = wrapPowerNetType(idb_power_net->get_connect_type());
    if (power_net_type == PowerNetType::kNone) {
      continue;
    }
    wrapPowerNet(idb_power_net);
  }
}

void EMIRInterface::wrapPowerNet(idb::IdbSpecialNet* idb_power_net)
{
  PowerNet power_net;
  power_net.set_net_name(idb_power_net->get_net_name());
  power_net.set_type(wrapPowerNetType(idb_power_net->get_connect_type()));
  wrapPowerWireSegmentList(power_net, idb_power_net);
  wrapPowerPinList(power_net, idb_power_net);
  if (power_net.get_wire_segment_list().empty() && power_net.get_via_list().empty() && power_net.get_pin_list().empty()) {
    return;
  }
  EMIRDM.getDatabase().get_power_net_map()[power_net.get_net_name()] = power_net;
}

PowerNetType EMIRInterface::wrapPowerNetType(idb::IdbConnectType connect_type)
{
  if (connect_type == idb::IdbConnectType::kPower) {
    return PowerNetType::kPower;
  }
  if (connect_type == idb::IdbConnectType::kGround) {
    return PowerNetType::kGround;
  }
  return PowerNetType::kNone;
}

void EMIRInterface::wrapPowerWireSegmentList(PowerNet& power_net, idb::IdbSpecialNet* idb_power_net)
{
  for (idb::IdbSpecialWire* idb_wire : idb_power_net->get_wire_list()->get_wire_list()) {
    for (idb::IdbSpecialWireSegment* idb_segment : idb_wire->get_segment_list()) {
      if (idb_segment->get_point_num() >= 2) {
        wrapPowerWireSegment(power_net, idb_segment);
      }
      if (idb_segment->is_via()) {
        wrapPowerVia(power_net, idb_segment->get_via());
      }
    }
  }
}

void EMIRInterface::wrapPowerWireSegment(PowerNet& power_net, idb::IdbSpecialWireSegment* idb_segment)
{
  idb::IdbLayerRouting* idb_layer = dynamic_cast<idb::IdbLayerRouting*>(idb_segment->get_layer());
  if (idb_layer == nullptr) {
    EMIRLOG.error(Loc::current(), "The power wire segment layer is invalid!");
  }
  idb::IdbCoordinate<int32_t>* first_coordinate = idb_segment->get_point_start();
  idb::IdbCoordinate<int32_t>* second_coordinate = idb_segment->get_point_second();
  if (first_coordinate == nullptr || second_coordinate == nullptr) {
    EMIRLOG.error(Loc::current(), "The power wire segment coordinate is invalid!");
  }
  PowerWireSegment power_wire_segment;
  power_wire_segment.set_layer_idx(idb_layer->get_id());
  power_wire_segment.set_layer_name(idb_layer->get_name());
  power_wire_segment.set_first_x(first_coordinate->get_x());
  power_wire_segment.set_first_y(first_coordinate->get_y());
  power_wire_segment.set_second_x(second_coordinate->get_x());
  power_wire_segment.set_second_y(second_coordinate->get_y());
  power_wire_segment.set_width(idb_segment->get_route_width());
  power_wire_segment.set_resistance_per_square(idb_layer->get_resistance());
  power_net.get_wire_segment_list().push_back(power_wire_segment);
}

void EMIRInterface::wrapPowerVia(PowerNet& power_net, idb::IdbVia* idb_via)
{
  idb::IdbLayerShape bottom_layer_shape = idb_via->get_bottom_layer_shape();
  idb::IdbLayerShape top_layer_shape = idb_via->get_top_layer_shape();
  idb::IdbLayerShape cut_layer_shape = idb_via->get_cut_layer_shape();
  idb::IdbCoordinate<int32_t>* coordinate = idb_via->get_coordinate();
  idb::IdbViaMaster* idb_via_master = idb_via->get_instance();
  PowerVia power_via;
  power_via.set_bottom_layer_idx(bottom_layer_shape.get_layer()->get_id());
  power_via.set_top_layer_idx(top_layer_shape.get_layer()->get_id());
  power_via.set_x(coordinate->get_x());
  power_via.set_y(coordinate->get_y());
  power_via.set_cut_num(static_cast<int32_t>(cut_layer_shape.get_rect_list().size()));
  if (idb_via_master->get_resistance() > 0.0) {
    power_via.set_resistance(idb_via_master->get_resistance());
  } else if (idb_via_master->get_master_generate()->get_rule_generate() != nullptr
             && idb_via_master->get_master_generate()->get_rule_generate()->get_resistance_per_cut() > 0.0 && power_via.get_cut_num() > 0) {
    power_via.set_resistance(idb_via_master->get_master_generate()->get_rule_generate()->get_resistance_per_cut() / power_via.get_cut_num());
  } else {
    power_via.set_resistance(getGeneratedViaResistance(idb_via_master, power_via.get_cut_num()));
  }
  power_net.get_via_list().push_back(power_via);
}

double EMIRInterface::getGeneratedViaResistance(idb::IdbViaMaster* idb_via_master, int32_t cut_num)
{
  if (idb_via_master->get_master_generate() == nullptr || cut_num <= 0) {
    return 0.0;
  }
  idb::IdbViaMasterGenerate* generated_master = idb_via_master->get_master_generate();
  idb::IdbLayerRouting* bottom_layer = generated_master->get_layer_bottom();
  idb::IdbLayerCut* cut_layer = generated_master->get_layer_cut();
  idb::IdbLayerRouting* top_layer = generated_master->get_layer_top();
  double resistance_per_cut = 0.0;
  for (idb::IdbVia* technology_via : dmInst->get_idb_layout()->get_via_list()->get_via_list()) {
    idb::IdbViaMaster* technology_via_master = technology_via->get_instance();
    if (!technology_via_master->is_fix() || !technology_via_master->is_default() || !technology_via_master->isOneCut()
        || technology_via_master->get_resistance() <= 0.0) {
      continue;
    }
    idb::IdbLayerShape* bottom_layer_shape = technology_via_master->get_bottom_layer_shape();
    idb::IdbLayerShape* cut_layer_shape = technology_via_master->get_cut_layer_shape();
    idb::IdbLayerShape* top_layer_shape = technology_via_master->get_top_layer_shape();
    if (bottom_layer_shape->get_layer() != bottom_layer || cut_layer_shape->get_layer() != cut_layer || top_layer_shape->get_layer() != top_layer) {
      continue;
    }
    if (resistance_per_cut == 0.0) {
      resistance_per_cut = technology_via_master->get_resistance();
    } else if (std::abs(resistance_per_cut - technology_via_master->get_resistance()) > EMIR_ERROR) {
      return 0.0;
    }
  }
  return resistance_per_cut / cut_num;
}

void EMIRInterface::wrapPowerPinList(PowerNet& power_net, idb::IdbSpecialNet* idb_power_net)
{
  for (idb::IdbPin* idb_pin : idb_power_net->get_instance_pin_list()->get_pin_list()) {
    wrapPowerPin(power_net, idb_pin, false);
  }
  for (idb::IdbPin* idb_pin : idb_power_net->get_io_pin_list()->get_pin_list()) {
    wrapPowerPin(power_net, idb_pin, true);
  }
}

void EMIRInterface::wrapPowerPin(PowerNet& power_net, idb::IdbPin* idb_pin, bool is_source)
{
  for (idb::IdbLayerShape* idb_layer_shape : idb_pin->get_port_box_list()) {
    wrapPowerPinShape(power_net, idb_pin, idb_layer_shape, is_source);
  }
}

void EMIRInterface::wrapPowerPinShape(PowerNet& power_net, idb::IdbPin* idb_pin, idb::IdbLayerShape* idb_layer_shape, bool is_source)
{
  idb::IdbRect idb_bounding_box = idb_layer_shape->get_bounding_box();
  PowerPin power_pin;
  if (!idb_pin->is_io_pin()) {
    power_pin.set_instance_id(idb_pin->get_instance()->get_id());
    power_pin.set_pin_name(idb_pin->get_instance()->get_name() + ":" + idb_pin->get_pin_name());
  } else {
    power_pin.set_pin_name(idb_pin->get_pin_name());
  }
  power_pin.set_layer_idx(idb_layer_shape->get_layer()->get_id());
  power_pin.set_x((idb_bounding_box.get_low_x() + idb_bounding_box.get_high_x()) / 2);
  power_pin.set_y((idb_bounding_box.get_low_y() + idb_bounding_box.get_high_y()) / 2);
  power_pin.set_is_source(is_source);
  power_net.get_pin_list().push_back(power_pin);
}

#endif

#if 1  // output

void EMIRInterface::output()
{
}

#endif

#endif

#endif

}  // namespace iemir
