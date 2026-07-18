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
#include "STAInterface.hpp"

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "ClockPropagator.hpp"
#include "GraphBuilder.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "SDFWriter.hpp"
#include "STAHeader.hpp"
#include "TimingCharacterizer.hpp"
#include "TimingAnalyzer.hpp"
#include "TimingPropagator.hpp"
#include "TimingReporter.hpp"
#include "Utility.hpp"
#include "idm.h"

namespace ista {

// public

STAInterface& STAInterface::getInst()
{
  if (_sta_interface_instance == nullptr) {
    _sta_interface_instance = new STAInterface();
  }
  return *_sta_interface_instance;
}

void STAInterface::destroyInst()
{
  if (_sta_interface_instance != nullptr) {
    delete _sta_interface_instance;
    _sta_interface_instance = nullptr;
  }
}

#if 1  // 外部调用STA的API

#if 1  // iSTA

void STAInterface::initSTA(std::map<std::string, std::any> config_map)
{
  Logger::initInst();
  // clang-format off
  STALOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  STALOG.info(Loc::current(), "_____ _______________________       _______________________ ________ ________ ");
  STALOG.info(Loc::current(), "___(_)__  ___/___  __/___    |      __  ___/___  __/___    |___  __ \\___  __/");
  STALOG.info(Loc::current(), "__  / _____ \\ __  /   __  /| |      _____ \\ __  /   __  /| |__  /_/ /__  /  ");
  STALOG.info(Loc::current(), "_  /  ____/ / _  /    _  ___ |      ____/ / _  /    _  ___ |_  _, _/ _  /     ");
  STALOG.info(Loc::current(), "/_/   /____/  /_/     /_/  |_|      /____/  /_/     /_/  |_|/_/ |_|  /_/      ");
  STALOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  STALOG.printLogFilePath();
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  DataManager::initInst();
  STADM.input(config_map);
  DelayCalculator::initInst();

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void STAInterface::runSTA()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  STADC.init();

  GraphBuilder::initInst();
  STAGB.build();
  GraphBuilder::destroyInst();

  ClockPropagator::initInst();
  STACP.propagate();
  ClockPropagator::destroyInst();

  TimingPropagator::initInst();
  STATP.propagate();
  TimingPropagator::destroyInst();

  TimingAnalyzer::initInst();
  STATA.analyze();
  TimingAnalyzer::destroyInst();

  TimingReporter::initInst();
  STATR.report();
  TimingReporter::destroyInst();

  SDFWriter::initInst();
  STASW.write();
  SDFWriter::destroyInst();

  STADC.destroy();

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void STAInterface::extractLib()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  STADC.init();

  GraphBuilder::initInst();
  STAGB.build();
  GraphBuilder::destroyInst();

  ClockPropagator::initInst();
  STACP.propagate();
  ClockPropagator::destroyInst();

  TimingPropagator::initInst();
  STATP.propagate();
  TimingPropagator::destroyInst();

  TimingAnalyzer::initInst();
  STATA.analyze();
  TimingAnalyzer::destroyInst();

  TimingCharacterizer::initInst();
  STATC.characterize();
  TimingCharacterizer::destroyInst();

  STADC.destroy();

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void STAInterface::destroySTA()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  DelayCalculator::destroyInst();
  STADM.output();
  DataManager::destroyInst();

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());

  STALOG.printLogFilePath();
  // clang-format off
  STALOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  STALOG.info(Loc::current(), "_____ _______________________       _______________________   ________________________  __  ");
  STALOG.info(Loc::current(), "___(_)__  ___/___  __/___    |      ___  ____/____  _/___  | / /____  _/__  ___/___  / / /  ");
  STALOG.info(Loc::current(), "__  / _____ \\ __  /   __  /| |      __  /_     __  /  __   |/ /  __  /  _____ \\ __  /_/ / ");
  STALOG.info(Loc::current(), "_  /  ____/ / _  /    _  ___ |      _  __/    __/ /   _  /|  /  __/ /   ____/ / _  __  /    ");
  STALOG.info(Loc::current(), "/_/   /____/  /_/     /_/  |_|      /_/       /___/   /_/ |_/   /___/   /____/  /_/ /_/     ");
  STALOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  Logger::destroyInst();
}

#endif

#endif

#if 1  // STA调用外部的API

#if 1  // TopData

#if 1  // input

void STAInterface::input(std::map<std::string, std::any>& config_map)
{
  wrapConfig(config_map);
  wrapDatabase();
}

void STAInterface::wrapConfig(std::map<std::string, std::any>& config_map)
{
  /////////////////////////////////////////////
  STADM.getConfig().temp_directory_path = STAUTIL.getConfigValue<std::string>(config_map, "-temp_directory_path", "./sta_temp_directory");
  STADM.getConfig().thread_number = STAUTIL.getConfigValue<int32_t>(config_map, "-thread_number", 128);
  omp_set_num_threads(std::max(STADM.getConfig().thread_number, 1));
  /////////////////////////////////////////////
}

void STAInterface::wrapDatabase()
{
  wrapDBInfo();
  wrapInstanceList();
  wrapPortList();
  wrapNetList();
}

void STAInterface::wrapDBInfo()
{
  STADM.getDatabase().set_design_name(dmInst->get_idb_design()->get_design_name());
}

void STAInterface::wrapInstanceList()
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (idb::IdbInstance* idb_instance : idb_design->get_instance_list()->get_instance_list()) {
    wrapInstance(idb_instance);
    wrapInstancePinList(idb_instance);
  }
}

void STAInterface::wrapInstance(idb::IdbInstance* idb_instance)
{
  Instance instance;
  instance.set_instance_name(idb_instance->get_name());
  instance.set_cell_name(idb_instance->get_cell_master()->get_name());
  STADM.getDatabase().get_instance_map()[instance.get_instance_name()] = instance;
}

void STAInterface::wrapInstancePinList(idb::IdbInstance* idb_instance)
{
  for (idb::IdbPin* idb_pin : idb_instance->get_pin_list()->get_pin_list()) {
    wrapInstancePin(idb_instance, idb_pin);
  }
}

void STAInterface::wrapInstancePin(idb::IdbInstance* idb_instance, idb::IdbPin* idb_pin)
{
  if (!wrapSignalConnectType(idb_pin->get_term()->get_type())) {
    return;
  }

  std::string full_name = wrapInstancePinName(idb_instance, idb_pin);
  Pin pin;
  pin.set_pin_name(idb_pin->get_pin_name());
  pin.set_full_name(full_name);
  pin.set_instance_name(idb_instance->get_name());
  pin.set_direction(wrapPinDirection(idb_pin->get_term()->get_direction()));
  wrapPinCoordinate(pin, idb_pin);
  STADM.getDatabase().get_pin_map()[full_name] = pin;
}

bool STAInterface::wrapSignalConnectType(idb::IdbConnectType connect_type)
{
  return connect_type == idb::IdbConnectType::kNone || connect_type == idb::IdbConnectType::kSignal || connect_type == idb::IdbConnectType::kClock
         || connect_type == idb::IdbConnectType::kReset || connect_type == idb::IdbConnectType::kScan || connect_type == idb::IdbConnectType::kTieOff;
}

std::string STAInterface::wrapInstancePinName(idb::IdbInstance* idb_instance, idb::IdbPin* idb_pin)
{
  return idb_instance->get_name() + ":" + idb_pin->get_pin_name();
}

PinDirection STAInterface::wrapPinDirection(idb::IdbConnectDirection idb_direction)
{
  switch (idb_direction) {
    case idb::IdbConnectDirection::kInput:
      return PinDirection::kInput;
    case idb::IdbConnectDirection::kOutput:
    case idb::IdbConnectDirection::kOutputTriState:
      return PinDirection::kOutput;
    case idb::IdbConnectDirection::kInOut:
      return PinDirection::kInout;
    default:
      STALOG.error(Loc::current(), "Unrecognized type!");
      break;
  }
  return PinDirection::kNone;
}

void STAInterface::wrapPinCoordinate(Pin& pin, idb::IdbPin* idb_pin)
{
  idb::IdbCoordinate<int32_t>* coordinate = idb_pin->get_average_coordinate();
  pin.set_x(coordinate->get_x());
  pin.set_y(coordinate->get_y());
}

void STAInterface::wrapPortList()
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (idb::IdbPin* idb_pin : idb_design->get_io_pin_list()->get_pin_list()) {
    wrapPortPin(idb_pin);
  }
}

void STAInterface::wrapPortPin(idb::IdbPin* idb_pin)
{
  if (!wrapSignalConnectType(idb_pin->get_term()->get_type())) {
    return;
  }

  std::string full_name = wrapPinName(idb_pin);
  Pin pin;
  pin.set_pin_name(idb_pin->get_pin_name());
  pin.set_full_name(full_name);
  pin.set_direction(wrapPinDirection(idb_pin->get_term()->get_direction()));
  pin.set_is_port(true);
  wrapPinCoordinate(pin, idb_pin);
  STADM.getDatabase().get_pin_map()[full_name] = pin;
}

std::string STAInterface::wrapPinName(idb::IdbPin* idb_pin)
{
  return idb_pin->get_pin_name();
}

void STAInterface::wrapNetList()
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (idb::IdbNet* idb_net : idb_design->get_net_list()->get_net_list()) {
    wrapNet(idb_net);
  }
}

void STAInterface::wrapNet(idb::IdbNet* idb_net)
{
  if (!wrapSignalConnectType(idb_net->get_connect_type())) {
    return;
  }

  Net net;
  net.set_net_name(idb_net->get_net_name());
  wrapNetPinList(idb_net, net);
  wrapNetToDatabase(net);
}

void STAInterface::wrapNetPinList(idb::IdbNet* idb_net, Net& net)
{
  wrapNetPinList(idb_net->get_io_pins(), idb_net->get_instance_pin_list(), net);
}

void STAInterface::wrapNetPinList(idb::IdbPins* io_pin_list, idb::IdbPins* instance_pin_list, Net& net)
{
  for (idb::IdbPin* idb_pin : io_pin_list->get_pin_list()) {
    wrapNetPin(idb_pin, net);
  }
  for (idb::IdbPin* idb_pin : instance_pin_list->get_pin_list()) {
    wrapNetPin(idb_pin, net);
  }
}

void STAInterface::wrapNetPin(idb::IdbPin* idb_pin, Net& net)
{
  std::string pin_name;
  if (idb_pin->is_io_pin()) {
    pin_name = wrapNetIOPinName(idb_pin);
  } else {
    pin_name = wrapNetInstancePinName(idb_pin);
  }
  wrapNetPinNameList(net, pin_name);
}

std::string STAInterface::wrapNetIOPinName(idb::IdbPin* idb_pin)
{
  return idb_pin->get_pin_name();
}

std::string STAInterface::wrapNetInstancePinName(idb::IdbPin* idb_pin)
{
  return idb_pin->get_instance()->get_name() + ":" + idb_pin->get_pin_name();
}

void STAInterface::wrapNetPinNameList(Net& net, std::string& pin_name)
{
  std::vector<std::string>& pin_name_list = net.get_pin_name_list();
  if (!STAUTIL.exist(pin_name_list, pin_name)) {
    pin_name_list.push_back(pin_name);
  }
}

void STAInterface::wrapNetToDatabase(Net& net)
{
  STADM.getDatabase().get_net_map()[net.get_net_name()] = net;
}

#endif

#if 1  // output

void STAInterface::output()
{
}

#endif

#endif

#endif

// private

STAInterface* STAInterface::_sta_interface_instance = nullptr;

}  // namespace ista
