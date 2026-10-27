// SPDX-License-Identifier: MulanPSL-2.0
#include "ReadNetwork.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "Logger.hpp"
#include "Utility.hpp"
#include "idm.h"
#include "network/Network.hpp"

namespace ista {
namespace read_network_detail {

void readDesignName(Network& network)
{
  network.setName(dmInst->get_idb_design()->get_design_name());
}

void readInstances(Network& network)
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (idb::IdbInstance* idb_instance : idb_design->get_instance_list()->get_instance_list()) {
    readInstance(network, idb_instance);
    readInstancePins(network, idb_instance);
  }
}

void readInstance(Network& network, idb::IdbInstance* idb_instance)
{
  Instance instance;
  instance.set_instance_id(idb_instance->get_id());
  instance.set_instance_name(idb_instance->get_name());
  instance.set_cell_name(idb_instance->get_cell_master()->get_name());
  instance.set_is_io_cell(idb_instance->get_cell_master()->is_io_cell());
  network.instances()[instance.get_instance_name()] = instance;
}

void readInstancePins(Network& network, idb::IdbInstance* idb_instance)
{
  for (idb::IdbPin* idb_pin : idb_instance->get_pin_list()->get_pin_list()) {
    readInstancePin(network, idb_instance, idb_pin);
  }
}

void readInstancePin(Network& network, idb::IdbInstance* idb_instance, idb::IdbPin* idb_pin)
{
  if (!isSignalPin(idb_pin)) {
    return;
  }

  std::string full_name = instancePinName(idb_instance, idb_pin);
  Pin pin;
  pin.set_pin_name(idb_pin->get_pin_name());
  pin.set_full_name(full_name);
  pin.set_instance_name(idb_instance->get_name());
  pin.set_direction(pinDirection(idb_pin->get_term()->get_direction()));
  pin.set_constant_value(constantValue(idb_pin));
  readPinLocation(pin, idb_pin);
  network.pins()[full_name] = pin;
}

bool isSignalPin(idb::IdbPin* idb_pin)
{
  if (!isSignalConnectType(idb_pin->get_term()->get_type())) {
    return false;
  }
  // Keep logic inputs tied to supplies; only the supply net/PG terminals are excluded.
  if (!idb_pin->is_io_pin()) {
    return true;
  }
  idb::IdbNet* idb_net = idb_pin->get_net();
  if (idb_net != nullptr && !isSignalConnectType(idb_net->get_connect_type())) {
    return false;
  }
  idb::IdbSpecialNet* idb_special_net = idb_pin->get_special_net();
  if (idb_special_net != nullptr && !isSignalConnectType(idb_special_net->get_connect_type())) {
    return false;
  }
  // 电源网及其端口一起排除；只在 IO 端扫描 LEF PG 类型，避免逐个电源 pin 重复遍历大网。
  if (idb_pin->is_io_pin() && isPGNet(idb_net)) {
    return false;
  }
  return true;
}

bool isSignalConnectType(idb::IdbConnectType connect_type)
{
  return connect_type == idb::IdbConnectType::kNone || connect_type == idb::IdbConnectType::kSignal || connect_type == idb::IdbConnectType::kClock
         || connect_type == idb::IdbConnectType::kReset || connect_type == idb::IdbConnectType::kScan || connect_type == idb::IdbConnectType::kTieOff;
}

bool isPGNet(idb::IdbNet* idb_net)
{
  if (idb_net == nullptr) {
    return false;
  }
  if (idb_net->is_pdn()) {
    return true;
  }
  if (idb_net->get_connect_type() != idb::IdbConnectType::kNone && idb_net->get_connect_type() != idb::IdbConnectType::kSignal) {
    return false;
  }
  // Verilog wire 没有 POWER/GROUND 类型时，仅从一致的 LEF PG 引脚识别供电网，不按网名判断。
  // 混有逻辑 pin 的连接保留，不把接常量的逻辑输入误判为电源引脚。
  idb::IdbConnectType pg_type = idb::IdbConnectType::kNone;
  for (idb::IdbPin* idb_pin : idb_net->get_instance_pin_list()->get_pin_list()) {
    idb::IdbConnectType pin_type = idb_pin->get_term()->get_type();
    if (pin_type != idb::IdbConnectType::kPower && pin_type != idb::IdbConnectType::kGround) {
      return false;
    }
    if (pg_type != idb::IdbConnectType::kNone && pg_type != pin_type) {
      return false;
    }
    pg_type = pin_type;
  }
  if (pg_type == idb::IdbConnectType::kNone) {
    return false;
  }
  for (idb::IdbPin* idb_pin : idb_net->get_io_pins()->get_pin_list()) {
    idb::IdbConnectType pin_type = idb_pin->get_term()->get_type();
    if ((pin_type == idb::IdbConnectType::kPower || pin_type == idb::IdbConnectType::kGround) && pin_type != pg_type) {
      return false;
    }
  }
  return true;
}

std::string instancePinName(idb::IdbInstance* idb_instance, idb::IdbPin* idb_pin)
{
  return idb_instance->get_name() + ":" + idb_pin->get_pin_name();
}

PinDirection pinDirection(idb::IdbConnectDirection idb_direction)
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

TimingCaseValue constantValue(idb::IdbPin* idb_pin)
{
  idb::IdbConnectType type = idb::IdbConnectType::kNone;
  if (idb_pin->get_net() != nullptr) {
    type = idb_pin->get_net()->get_connect_type();
  } else if (idb_pin->get_special_net() != nullptr) {
    type = idb_pin->get_special_net()->get_connect_type();
  }
  if (type == idb::IdbConnectType::kPower) {
    return TimingCaseValue::kOne;
  }
  if (type == idb::IdbConnectType::kGround) {
    return TimingCaseValue::kZero;
  }
  return TimingCaseValue::kNone;
}

void readPinLocation(Pin& pin, idb::IdbPin* idb_pin)
{
  idb::IdbCoordinate<int32_t>* coordinate = idb_pin->get_average_coordinate();
  pin.set_x(coordinate->get_x());
  pin.set_y(coordinate->get_y());
}

void readPorts(Network& network)
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (idb::IdbPin* idb_pin : idb_design->get_io_pin_list()->get_pin_list()) {
    readPort(network, idb_pin);
  }
}

void readPort(Network& network, idb::IdbPin* idb_pin)
{
  if (!isSignalPin(idb_pin)) {
    return;
  }

  std::string full_name = pinName(idb_pin);
  Pin pin;
  pin.set_pin_name(idb_pin->get_pin_name());
  pin.set_full_name(full_name);
  pin.set_direction(pinDirection(idb_pin->get_term()->get_direction()));
  pin.set_is_port(true);
  readPinLocation(pin, idb_pin);
  network.pins()[full_name] = pin;
}

std::string pinName(idb::IdbPin* idb_pin)
{
  return idb_pin->get_pin_name();
}

void readNets(Network& network)
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (idb::IdbNet* idb_net : idb_design->get_net_list()->get_net_list()) {
    readNet(network, idb_net);
  }
}

void readNet(Network& network, idb::IdbNet* idb_net)
{
  if (!isSignalConnectType(idb_net->get_connect_type()) || isPGNet(idb_net)) {
    return;
  }

  Net net;
  net.set_net_name(idb_net->get_net_name());
  readNetPins(network, idb_net, net);
  addNet(network, net);
}

void readNetPins(Network& network, idb::IdbNet* idb_net, Net& net)
{
  readNetPins(network, idb_net->get_io_pins(), idb_net->get_instance_pin_list(), net);
}

void readNetPins(Network& network, idb::IdbPins* io_pin_list, idb::IdbPins* instance_pin_list, Net& net)
{
  for (idb::IdbPin* idb_pin : io_pin_list->get_pin_list()) {
    readNetPin(network, idb_pin, net);
  }
  for (idb::IdbPin* idb_pin : instance_pin_list->get_pin_list()) {
    readNetPin(network, idb_pin, net);
  }
}

void readNetPin(Network& network, idb::IdbPin* idb_pin, Net& net)
{
  std::string pin_name;
  if (idb_pin->is_io_pin()) {
    pin_name = netPortName(idb_pin);
  } else {
    pin_name = netInstancePinName(idb_pin);
  }
  appendNetPin(network, net, pin_name);
}

std::string netPortName(idb::IdbPin* idb_pin)
{
  return idb_pin->get_pin_name();
}

std::string netInstancePinName(idb::IdbPin* idb_pin)
{
  return idb_pin->get_instance()->get_name() + ":" + idb_pin->get_pin_name();
}

void appendNetPin(Network& network, Net& net, std::string& pin_name)
{
  std::vector<std::string>& pin_name_list = net.get_pin_name_list();
  if (STAUTIL.exist(network.pins(), pin_name) && !STAUTIL.exist(pin_name_list, pin_name)) {
    pin_name_list.push_back(pin_name);
  }
}

void addNet(Network& network, Net& net)
{
  network.nets()[net.get_net_name()] = net;
}

}  // namespace read_network_detail

void readNetwork(Network& network)
{
  read_network_detail::readDesignName(network);
  read_network_detail::readInstances(network);
  read_network_detail::readPorts(network);
  read_network_detail::readNets(network);
}

}  // namespace ista
