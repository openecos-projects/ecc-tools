// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <cstdint>
#include <string>

namespace idb {
class IdbInstance;
class IdbNet;
class IdbPin;
class IdbPins;
enum class IdbConnectType : uint8_t;
enum class IdbConnectDirection : uint8_t;
}  // namespace idb

namespace ista {
class Network;

// Import the current iDB design without clearing or building derived STA state.
void readNetwork(Network& network);
class Net;
class Pin;
enum class PinDirection;
enum class TimingCaseValue;

// Netlist conversion helpers; tests exercise partial iDB fixtures directly.
namespace read_network_detail {
void readDesignName(Network& network);
void readInstances(Network& network);
void readInstance(Network& network, idb::IdbInstance* idb_instance);
void readInstancePins(Network& network, idb::IdbInstance* idb_instance);
void readInstancePin(Network& network, idb::IdbInstance* idb_instance, idb::IdbPin* idb_pin);
bool isSignalPin(idb::IdbPin* idb_pin);
bool isSignalConnectType(idb::IdbConnectType connect_type);
bool isPGNet(idb::IdbNet* idb_net);
std::string instancePinName(idb::IdbInstance* idb_instance, idb::IdbPin* idb_pin);
PinDirection pinDirection(idb::IdbConnectDirection idb_direction);
TimingCaseValue constantValue(idb::IdbPin* idb_pin);
void readPinLocation(Pin& pin, idb::IdbPin* idb_pin);
void readPorts(Network& network);
void readPort(Network& network, idb::IdbPin* idb_pin);
std::string pinName(idb::IdbPin* idb_pin);
void readNets(Network& network);
void readNet(Network& network, idb::IdbNet* idb_net);
void readNetPins(Network& network, idb::IdbNet* idb_net, Net& net);
void readNetPins(Network& network, idb::IdbPins* io_pin_list, idb::IdbPins* instance_pin_list, Net& net);
void readNetPin(Network& network, idb::IdbPin* idb_pin, Net& net);
std::string netPortName(idb::IdbPin* idb_pin);
std::string netInstancePinName(idb::IdbPin* idb_pin);
void appendNetPin(Network& network, Net& net, std::string& pin_name);
void addNet(Network& network, Net& net);
}  // namespace read_network_detail
}  // namespace ista
