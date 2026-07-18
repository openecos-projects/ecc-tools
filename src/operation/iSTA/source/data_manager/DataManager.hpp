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

#include "Config.hpp"
#include "Database.hpp"

namespace spef {
struct ConnEntry;
struct Net;
struct ResCap;
}  // namespace spef

namespace idb {
class LibArc;
class LibArcSet;
class LibCell;
class LibLibrary;
class LibPort;
class LibTable;
}  // namespace idb

namespace ista {

#define STADM (ista::DataManager::getInst())

class DataManager
{
 public:
  static void initInst();
  static DataManager& getInst();
  static void destroyInst();
  // function
  void input(std::map<std::string, std::any>& config_map);
  void output();

  Config& getConfig() { return _config; }
  Database& getDatabase() { return _database; }

 private:
  static DataManager* _dm_instance;

  // config & database
  Config _config;
  Database _database;

  DataManager() = default;
  DataManager(const DataManager& other) = delete;
  DataManager(DataManager&& other) = delete;
  ~DataManager() = default;
  DataManager& operator=(const DataManager& other) = delete;
  DataManager& operator=(DataManager&& other) = delete;

#if 1  // build
  void buildConfig();
  void buildDatabase();
  void buildTimingLibrary();
  void buildTimingCellMap(std::vector<std::unique_ptr<idb::LibLibrary>>& lib_list);
  void buildTimingLibraryInfo(std::vector<std::unique_ptr<idb::LibLibrary>>& lib_list);
  idb::LibLibrary* getReferenceLib(std::vector<std::unique_ptr<idb::LibLibrary>>& lib_list);
  TimingCapacitiveUnit getTimingCapacitiveUnit(idb::LibLibrary* lib_library);
  TimingResistanceUnit getTimingResistanceUnit(idb::LibLibrary* lib_library);
  TimingTimeUnit getTimingTimeUnit(idb::LibLibrary* lib_library);
  void makeTimingCell(idb::LibCell* lib_cell);
  void makeTimingCellPort(TimingCell& timing_cell, idb::LibPort* lib_port);
  void makeTimingCellArc(TimingCell& timing_cell, idb::LibArcSet* lib_arc_set);
  bool isSDFDelayArc(idb::LibArc* lib_arc);
  bool isSDFCheckArc(idb::LibArc* lib_arc);
  TimingCellArc makeDelayArc(idb::LibArcSet* lib_arc_set);
  void updateClearPresetArc(TimingCell& timing_cell, idb::LibArc* lib_arc);
  TimingCheckArc makeCheckArc(idb::LibArcSet* lib_arc_set);
  std::vector<TimingArc> makeTimingArcList(idb::LibArcSet* lib_arc_set);
  TimingArc makeTimingArc(idb::LibArc* lib_arc);
  void makeTimingArcTable(TimingArc& timing_arc, idb::LibArc* lib_arc);
  TimingTable makeTimingTable(idb::LibTable* lib_table);
  TimingTableVariableType getTimingTableVariableType(idb::LibTable* lib_table, bool is_first_variable);
  double getLibTimeUnitScale(idb::LibLibrary* lib_library);
  double getLibCapUnitScale(idb::LibLibrary* lib_library);
  TimingArcSense getTimingArcSense(idb::LibArc* lib_arc);
  TransType getTriggerTransType(idb::LibArc* lib_arc);
  TransType getCheckTransType(idb::LibArc* lib_arc);
  TimingCheckType getTimingCheckType(idb::LibArc* lib_arc);
  void updateTimingCell(TimingCell& timing_cell);
  void buildInstanceList();
  void makeInstanceList();
  void buildInstanceTimingInfo();
  void makeInstanceTimingInfo(Instance& instance);
  TimingCheckArc makeInstanceTimingCheckArc(Instance& instance, TimingCheckArc& timing_check_arc);
  TimingCellArc* findClockToQArc(TimingCell& timing_cell);
  std::string getInstancePinName(Instance& instance, std::string& port_name);
  std::string findOutputPinName(Instance& instance, TimingCell& timing_cell);
  bool isInstancePin(Pin& pin);
  void makeUniqueName(std::vector<std::string>& list, const std::string& value);
  void buildNetList();
  void makeNetList();
  void makeNet(const std::string& net_name, Net& net);
  void buildParasiticLibrary();
  void buildParasiticNetMap(spef::Net& spef_net);
  void makeParasiticConnection(ParasiticNet& parasitic_net, spef::ConnEntry& spef_conn);
  void makeParasiticCapacitance(ParasiticNet& parasitic_net, spef::ResCap& spef_cap);
  void makeParasiticResistance(ParasiticNet& parasitic_net, spef::ResCap& spef_res);
  double getParasiticCapacitance(double spef_capacitance);
  double getParasiticResistance(double spef_resistance);
  double getSpefUnitScale(std::string& spef_unit, std::string& target_unit);
  ParasiticNode& getParasiticNode(ParasiticNet& parasitic_net, const std::string& node_name);
  void readConstraint();
  std::vector<std::vector<std::string>> readCommandList(std::string& sdc_file_path);
  std::vector<std::vector<std::string>> resolveCommandList(std::vector<std::vector<std::string>>& command_list);
  std::vector<std::string> resolveCommandTokenList(std::vector<std::string>& token_list, std::map<std::string, std::string>& variable_map);
  void updateVariableMap(std::vector<std::string>& token_list, std::map<std::string, std::string>& variable_map);
  std::string resolveBracketCommand(std::vector<std::string>& token_list, std::size_t& token_idx, std::map<std::string, std::string>& variable_map);
  std::vector<std::string> getBracketTokenList(std::vector<std::string>& token_list, std::size_t& token_idx,
                                               std::map<std::string, std::string>& variable_map);
  std::string evalExpr(std::vector<std::string>& expr_token_list);
  double calcExprValue(std::vector<std::string>& expr_token_list);
  void calcExprMulDiv(std::vector<double>& value_list, std::vector<std::string>& operator_list);
  std::string getExprValueString(const double value);
  bool isExprOperator(std::string& token);
  std::string resolveVariableToken(std::string token, std::map<std::string, std::string>& variable_map);
  std::string getTokenListString(std::vector<std::string>& token_list, std::size_t begin_idx);
  std::vector<std::string> tokenizeSdc(std::string& content);
  std::string removeComment(std::string& line);
  void parseCommand(std::vector<std::string>& token_list);
  void parseCreateClock(std::vector<std::string>& token_list);
  void parseSetInputDelay(std::vector<std::string>& token_list);
  void parseSetOutputDelay(std::vector<std::string>& token_list);
  void parseSetInputTransition(std::vector<std::string>& token_list);
  void parseSetLoad(std::vector<std::string>& token_list);
  double getCommandDoubleValue(std::vector<std::string>& token_list);
  std::string getOptionValue(std::vector<std::string>& token_list, const std::string& option);
  double getOptionDoubleValue(std::vector<std::string>& token_list, const std::string& option, double default_value);
  bool hasOption(std::vector<std::string>& token_list, const std::string& option);
  std::string getClockName(std::vector<std::string>& token_list);
  std::string getCollectionName(std::vector<std::string>& token_list, std::size_t collection_idx);
  std::vector<std::string> getObjectList(std::vector<std::string>& token_list);
  void pushObjectName(std::vector<std::string>& object_list, std::string object_name);
  std::string getObjectName(std::string& object_name);
  bool isCollectionCommand(std::string& token);
  bool isClockCollectionCommand(std::string& token);
  bool isCommandOptionValue(std::vector<std::string>& token_list, std::size_t token_idx);
  std::vector<std::string> resolveObjectList(std::vector<std::string>& object_list);
  void updateClock(TimingClock& timing_clock);
  TimingPortConstraint& getPortConstraint(const std::string& port_name);
  void printConfig();
  void printDatabase();
#endif
};

}  // namespace ista
