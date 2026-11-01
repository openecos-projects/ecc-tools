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
#include <functional>
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
class SpefReader;
}  // namespace spef

namespace vcd {
class VcdReader;
}  // namespace vcd

struct LibertyExpr;

namespace ipw {
class LogicExpression;
class ParasiticNet;
enum class PinDirection;
enum class TimingCaseValue;
class Net;
class Pin;
class TimingArc;
class TimingCell;
class TimingCellArc;
class TimingLeakagePower;
class TimingPowerArc;
class TimingTable;
enum class LogicOperationType;
enum class TimingArcSense;
enum class TimingTableVariableType;
enum class TransType;
}  // namespace ipw

#endif

namespace ipw {

#define PWI (ipw::PWInterface::getInst())

// An external design source borrows analysis inputs from the surrounding flow.
// The caller owns every pointer and must keep it valid for the PW session.
struct NativePowerInputs
{
  std::vector<idb::LibLibrary*> liberty_libraries;
  spef::SpefReader* spef_reader = nullptr;
  vcd::VcdReader* vcd_reader = nullptr;
  std::string sdc_path;
  std::string spef_path;
};

class PWInterface
{
 public:
  static PWInterface& getInst();
  static void destroyInst();

#if 1  // 外部调用PW的API

#if 1  // iPW
  void initPW(std::map<std::string, std::any> config_map);
  void runPW(bool is_peak = false, bool is_avg = false);
  void destroyPW();
  // Releases an incomplete PW session without producing reports.
  void abortPW() noexcept;
  void reportCellPower(const std::string& directory_path, const int32_t top_num, const bool is_all);
  void reportNetPower(const std::string& directory_path, const int32_t top_num, const bool is_all);
  void reportPeakPowerWaveform(const std::string& file_path);
#endif

#endif

#if 1  // PW调用外部的API

#if 1  // TopData

#if 1  // input
  void input(std::map<std::string, std::any>& config_map);
  void wrapConfig(std::map<std::string, std::any>& config_map);
  void wrapDatabase();
  // An empty wrapper restores the legacy iDB projection. The active analysis
  // inputs are supplied at execution time so callers may update them between
  // sessions without rebinding the source.
  void setDatabaseWrapper(std::function<void(const NativePowerInputs&)> database_wrapper);
  [[nodiscard]] bool hasDatabaseWrapper() const noexcept { return static_cast<bool>(_database_wrapper); }
  void setPowerInputs(NativePowerInputs inputs);
  void wrapVcdActivity();
  void wrapDBInfo();
  void wrapInstanceList();
  void wrapInstance(idb::IdbInstance* idb_instance);
  void wrapInstancePinList(idb::IdbInstance* idb_instance);
  void wrapInstancePin(idb::IdbInstance* idb_instance, idb::IdbPin* idb_pin);
  bool wrapSignalConnectType(idb::IdbConnectType connect_type);
  std::string wrapInstancePinName(idb::IdbInstance* idb_instance, idb::IdbPin* idb_pin);
  PinDirection wrapPinDirection(idb::IdbConnectDirection idb_direction);
  TimingCaseValue wrapConstantValue(idb::IdbPin* idb_pin);
  void wrapPortList();
  void wrapPortPin(idb::IdbPin* idb_pin);
  std::string wrapPinName(idb::IdbPin* idb_pin);
  void wrapNetList();
  void wrapPowerIndex();
  void wrapNet(idb::IdbNet* idb_net);
  void wrapNetPinList(idb::IdbNet* idb_net, Net& net);
  void wrapNetPinList(idb::IdbPins* io_pin_list, idb::IdbPins* instance_pin_list, Net& net);
  void wrapNetPin(idb::IdbPin* idb_pin, Net& net);
  std::string wrapNetIOPinName(idb::IdbPin* idb_pin);
  std::string wrapNetInstancePinName(idb::IdbPin* idb_pin);
  void wrapNetPinNameList(Net& net, std::string& pin_name);
  void wrapNetToDatabase(const std::string& net_name, Net& net);
  void wrapTimingLibrary();
  void wrapTimingCellMap(const std::vector<idb::LibLibrary*>& lib_list);
  // 按需 wrap 一个库 cell（惰性 wrap 的兜底路径，供 SDC 命令使用）。
  bool wrapTimingCellByName(const std::string& cell_name);
  // 最近一次 wrapTimingLibrary 用到的库列表，供按需 wrap 反查。
  std::vector<idb::LibLibrary*> _lib_list;
  void wrapTimingLibraryInfo(const std::vector<idb::LibLibrary*>& lib_list);
  idb::LibLibrary* wrapReferenceLib(const std::vector<idb::LibLibrary*>& lib_list);
  void wrapTimingCell(idb::LibCell* lib_cell);
  void wrapTimingCellPort(TimingCell& timing_cell, idb::LibPort* lib_port);
  void wrapTimingCellSequential(TimingCell& timing_cell, const idb::LibCell* lib_cell);
  void wrapTimingCellPower(TimingCell& timing_cell, idb::LibCell* lib_cell);
  void wrapTimingCellLeakagePower(TimingCell& timing_cell, idb::LibCell* lib_cell);
  void wrapTimingCellPowerConditions(TimingCell& timing_cell);
  TimingPowerArc wrapTimingPowerArc(idb::LibPowerArc* lib_power_arc);
  TimingPowerArc wrapTimingPortPowerArc(
      idb::LibInternalPowerInfo* internal_power_info,
      std::string& port_name,
      idb::LibLibrary* lib_library
  );
  void wrapTimingPowerArcTable(TimingPowerArc& timing_power_arc, idb::LibTableModel* power_table_model);
  TimingLeakagePower wrapTimingLeakagePower(idb::LibLeakagePower* lib_leakage_power);
  LogicExpression wrapLogicExpression(const std::string& expression_string);
  void wrapLogicExpressionTermList(LogicExpression& logic_expression, LibertyExpr* liberty_expr);
  LogicOperationType wrapLogicOperationType(int32_t liberty_expr_op);
  void wrapTimingCellArc(TimingCell& timing_cell, idb::LibArcSet* lib_arc_set);
  bool isTimingDelayArc(idb::LibArc* lib_arc);
  TimingCellArc wrapDelayArc(idb::LibArcSet* lib_arc_set);
  std::vector<TimingArc> wrapTimingArcList(idb::LibArcSet* lib_arc_set);
  TimingArc wrapTimingArc(idb::LibArc* lib_arc);
  void wrapTimingArcTable(TimingArc& timing_arc, idb::LibArc* lib_arc);
  TimingTable wrapTimingTable(idb::LibTable* lib_table);
  TimingTableVariableType wrapTimingTableVariableType(idb::LibTable* lib_table);
  double wrapLibTimeUnitScale(idb::LibLibrary* lib_library);
  double wrapLibCapUnitScale(idb::LibLibrary* lib_library);
  TimingArcSense wrapTimingArcSense(idb::LibArc* lib_arc);
  TransType wrapTriggerTransType(idb::LibArc* lib_arc);
  void wrapParasiticLibrary();
  double wrapSpefUnitScale(std::string& spef_unit, std::string& target_unit);
  void wrapParasiticNet(spef::Net& spef_net, double capacitance_scale, double resistance_scale);
  void wrapParasiticConnection(ParasiticNet& parasitic_net, spef::ConnEntry& spef_conn);
  void wrapParasiticResistance(ParasiticNet& parasitic_net, spef::ResCap& spef_res, double resistance_scale);
  void wrapParasiticCapacitance(ParasiticNet& parasitic_net, spef::ResCap& spef_cap, double capacitance_scale);
  void wrapParasiticNode(ParasiticNet& parasitic_net, const std::string& node_name);
#endif

#if 1  // output
  void output();
#endif

#endif

#endif

 private:
  bool resolvePeakFlow(const bool is_peak, const bool is_avg);
  void runAveragedPower();
  void runPeakPower();

  static PWInterface* _pw_interface_instance;
  std::function<void(const NativePowerInputs&)> _database_wrapper;
  NativePowerInputs _native_power_inputs;

  PWInterface() = default;
  PWInterface(const PWInterface& other) = delete;
  PWInterface(PWInterface&& other) = delete;
  ~PWInterface() = default;
  PWInterface& operator=(const PWInterface& other) = delete;
  PWInterface& operator=(PWInterface&& other) = delete;
};

}  // namespace ipw
