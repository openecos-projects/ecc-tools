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
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#if 1  // 前向声明

namespace idb {
class IdbInstance;
class IdbNet;
class IdbPin;
class IdbPins;
class LibArc;
class LibArcSet;
class LibAxis;
class LibAttrValue;
class LibCell;
class LibInternalPowerInfo;
class LibLeakagePower;
class LibLibrary;
class LibLutTableTemplate;
class LibPort;
class LibPowerArc;
class LibPowerArcSet;
class LibTable;
class LibTableModel;
enum class IdbConnectType : uint8_t;
enum class IdbConnectDirection : uint8_t;
}  // namespace idb

namespace spef {
struct ConnEntry;
struct Net;
struct ResCap;
}  // namespace spef

struct LibertyExpr;

namespace ista {
class LogicExpression;
class ParasiticNet;
class ParasiticNode;
enum class PinDirection;
class Net;
class Pin;
class TimingArc;
class TimingCell;
class TimingCellArc;
class TimingCheckArc;
class TimingLeakagePower;
class TimingPowerArc;
class TimingTable;
class TCModel;
class TCLib;
class TCPort;
class TCScalarTable;
class TCTimingArc;
enum class LogicOperationType;
enum class TimingArcSense;
enum class TimingCapacitiveUnit;
enum class TimingCheckType;
enum class TimingResistanceUnit;
enum class TimingTableVariableType;
enum class TimingTimeUnit;
enum class TransType;
}  // namespace ista

#endif

namespace ista {

#define STAI (ista::STAInterface::getInst())

class STAInterface
{
 public:
  static STAInterface& getInst();
  static void destroyInst();

#if 1  // 外部调用STA的API

#if 1  // iSTA
  void initSTA(std::map<std::string, std::any> config_map);
  void runSTA();
  void extractLib();
  void destroySTA();
  bool updateTiming(std::string& error_message);
  bool writeSDF(const std::string& file_path, std::string& error_message);
  bool reportTiming(std::string& error_message);
  bool createClock(const std::string& clock_name, double period, double rise_edge, double fall_edge,
                   const std::vector<std::string>& source_list, std::string& error_message);
  bool setPropagatedClock(const std::vector<std::string>& clock_name_list, std::string& error_message);
  bool getPorts(const std::vector<std::string>& port_name_list, std::vector<std::string>& resolved_port_list,
                std::string& error_message);
  bool getClocks(const std::vector<std::string>& clock_name_list, std::vector<std::string>& resolved_clock_list,
                 std::string& error_message);
#endif

#endif

#if 1  // STA调用外部的API

#if 1  // TopData

#if 1  // input
  void input(std::map<std::string, std::any>& config_map);
  void wrapConfig(std::map<std::string, std::any>& config_map);
  void wrapDatabase();
  void wrapVcdActivity();
  std::string wrapVcdPinName(std::string& vcd_signal_name);
  void wrapDBInfo();
  void wrapConstraintFilePath();
  void wrapInstanceList();
  void wrapInstance(idb::IdbInstance* idb_instance);
  void wrapInstancePinList(idb::IdbInstance* idb_instance);
  void wrapInstancePin(idb::IdbInstance* idb_instance, idb::IdbPin* idb_pin);
  bool wrapSignalConnectType(idb::IdbConnectType connect_type);
  std::string wrapInstancePinName(idb::IdbInstance* idb_instance, idb::IdbPin* idb_pin);
  PinDirection wrapPinDirection(idb::IdbConnectDirection idb_direction);
  void wrapPinCoordinate(Pin& pin, idb::IdbPin* idb_pin);
  void wrapPortList();
  void wrapPortPin(idb::IdbPin* idb_pin);
  std::string wrapPinName(idb::IdbPin* idb_pin);
  void wrapNetList();
  void wrapNet(idb::IdbNet* idb_net);
  void wrapNetPinList(idb::IdbNet* idb_net, Net& net);
  void wrapNetPinList(idb::IdbPins* io_pin_list, idb::IdbPins* instance_pin_list, Net& net);
  void wrapNetPin(idb::IdbPin* idb_pin, Net& net);
  std::string wrapNetIOPinName(idb::IdbPin* idb_pin);
  std::string wrapNetInstancePinName(idb::IdbPin* idb_pin);
  void wrapNetPinNameList(Net& net, std::string& pin_name);
  void wrapNetToDatabase(Net& net);
  void wrapTimingLibrary();
  void wrapTimingCellMap(std::vector<std::unique_ptr<idb::LibLibrary>>& lib_list);
  void wrapTimingLibraryInfo(std::vector<std::unique_ptr<idb::LibLibrary>>& lib_list);
  idb::LibLibrary* wrapReferenceLib(std::vector<std::unique_ptr<idb::LibLibrary>>& lib_list);
  TimingCapacitiveUnit wrapTimingCapacitiveUnit(idb::LibLibrary* lib_library);
  TimingResistanceUnit wrapTimingResistanceUnit(idb::LibLibrary* lib_library);
  TimingTimeUnit wrapTimingTimeUnit(idb::LibLibrary* lib_library);
  void wrapTimingCell(idb::LibCell* lib_cell);
  void wrapTimingCellPort(TimingCell& timing_cell, idb::LibPort* lib_port);
  void wrapTimingCellPower(TimingCell& timing_cell, idb::LibCell* lib_cell);
  void wrapTimingCellLeakagePower(TimingCell& timing_cell, idb::LibCell* lib_cell);
  TimingPowerArc wrapTimingPowerArc(idb::LibPowerArc* lib_power_arc);
  TimingPowerArc wrapTimingPortPowerArc(idb::LibInternalPowerInfo* internal_power_info, std::string& port_name,
                                        idb::LibLibrary* lib_library);
  void wrapTimingPowerArcTable(TimingPowerArc& timing_power_arc, idb::LibTableModel* power_table_model);
  TimingLeakagePower wrapTimingLeakagePower(idb::LibLeakagePower* lib_leakage_power);
  LogicExpression wrapLogicExpression(std::string& expression_string);
  void wrapLogicExpressionTermList(LogicExpression& logic_expression, LibertyExpr* liberty_expr);
  LogicOperationType wrapLogicOperationType(int32_t liberty_expr_op);
  void wrapTimingCellArc(TimingCell& timing_cell, idb::LibArcSet* lib_arc_set);
  bool isSDFDelayArc(idb::LibArc* lib_arc);
  bool isSDFCheckArc(idb::LibArc* lib_arc);
  TimingCellArc wrapDelayArc(idb::LibArcSet* lib_arc_set);
  void wrapClearPresetArc(TimingCell& timing_cell, idb::LibArc* lib_arc);
  TimingCheckArc wrapCheckArc(idb::LibArcSet* lib_arc_set);
  std::vector<TimingArc> wrapTimingArcList(idb::LibArcSet* lib_arc_set);
  TimingArc wrapTimingArc(idb::LibArc* lib_arc);
  void wrapTimingArcTable(TimingArc& timing_arc, idb::LibArc* lib_arc);
  TimingTable wrapTimingTable(idb::LibTable* lib_table);
  TimingTableVariableType wrapTimingTableVariableType(idb::LibTable* lib_table, bool is_first_variable);
  double wrapLibTimeUnitScale(idb::LibLibrary* lib_library);
  double wrapLibCapUnitScale(idb::LibLibrary* lib_library);
  TimingArcSense wrapTimingArcSense(idb::LibArc* lib_arc);
  TransType wrapTriggerTransType(idb::LibArc* lib_arc);
  TransType wrapCheckTransType(idb::LibArc* lib_arc);
  TimingCheckType wrapTimingCheckType(idb::LibArc* lib_arc);
  void wrapTimingCellInfo(TimingCell& timing_cell);
  void wrapParasiticLibrary();
  void wrapParasiticNet(spef::Net& spef_net);
  void wrapParasiticConnection(ParasiticNet& parasitic_net, spef::ConnEntry& spef_conn);
  void wrapParasiticCapacitance(ParasiticNet& parasitic_net, spef::ResCap& spef_cap);
  void wrapParasiticResistance(ParasiticNet& parasitic_net, spef::ResCap& spef_res);
  double wrapParasiticCapacitance(double spef_capacitance);
  double wrapParasiticResistance(double spef_resistance);
  double wrapSpefUnitScale(std::string& spef_unit, std::string& target_unit);
  ParasiticNode& wrapParasiticNode(ParasiticNet& parasitic_net, const std::string& node_name);
#endif

#if 1  // output
  void output();
#endif

#endif

#if 1  // Lib
  void writeLib(TCModel& tc_model, std::string& output_path);
  std::string getLibFilePath(TCLib& tc_lib, std::string& output_path);
  std::unique_ptr<idb::LibLibrary> buildLib(TCLib& tc_lib);
  void buildLibHeader(idb::LibLibrary& lib_library);
  std::unique_ptr<idb::LibCell> buildLibCell(idb::LibLibrary& lib_library, TCLib& tc_lib);
  void buildLibPortList(idb::LibCell& lib_cell, TCLib& tc_lib);
  void buildLibPort(idb::LibCell& lib_cell, TCPort& tc_port);
  void buildLibTimingArcList(idb::LibCell& lib_cell, TCLib& tc_lib);
  void buildLibTimingArc(idb::LibCell& lib_cell, TCTimingArc& tc_timing_arc);
  void buildLibDelayTableModel(idb::LibArc& lib_arc, TCTimingArc& tc_timing_arc);
  void buildLibCheckTableModel(idb::LibArc& lib_arc, TCTimingArc& tc_timing_arc);
  std::unique_ptr<idb::LibArc> makeLibArc(std::string& source_port, std::string& sink_port, std::string& timing_type);
  std::unique_ptr<idb::LibTable> makeLibScalarTable(int32_t table_type, TCScalarTable& tc_scalar_table);
#endif

#endif

 private:
  static STAInterface* _sta_interface_instance;
  bool _is_initialized = false;
  bool _is_timing_updated = false;

  STAInterface() = default;
  STAInterface(const STAInterface& other) = delete;
  STAInterface(STAInterface&& other) = delete;
  ~STAInterface() = default;
  STAInterface& operator=(const STAInterface& other) = delete;
  STAInterface& operator=(STAInterface&& other) = delete;
  // function
  bool isSTAInitialized(std::string& error_message) const;
};

}  // namespace ista
