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

#include <cstdint>
#include <string>
#include <vector>

namespace idb {
class LibArc;
class LibArcSet;
class LibCell;
class LibLibrary;
class LibPort;
class LibTable;
}  // namespace idb

struct LibertyExpr;

namespace ista {
class LogicExpression;
class TimingArc;
class LibertyCell;
class TimingCellArc;
class TimingCheckArc;
class TimingTable;
enum class LogicOperationType;
enum class TimingArcSense;
enum class TimingCapacitiveUnit;
enum class TimingCheckType;
enum class TimingResistanceUnit;
enum class TimingTableVariableType;
enum class TimingTimeUnit;
enum class TransType;

class LibertyLibrary;
class Network;
class ReadLiberty
{
 public:
  ReadLiberty(LibertyLibrary& liberty, const Network& network) : _liberty(liberty), _network(network) {}
  void read();
  void readPort(LibertyCell& timing_cell, idb::LibPort* lib_port);
  LogicExpression readLogicExpression(const std::string& expression_string);

 private:
  LibertyLibrary& _liberty;
  const Network& _network;
  void readCells(const std::vector<idb::LibLibrary*>& lib_list);
  void readLibraryInfo(const std::vector<idb::LibLibrary*>& lib_list);
  idb::LibLibrary* findReferenceLibrary(const std::vector<idb::LibLibrary*>& lib_list);
  TimingCapacitiveUnit capacitanceUnit(idb::LibLibrary* lib_library);
  TimingResistanceUnit resistanceUnit(idb::LibLibrary* lib_library);
  TimingTimeUnit timeUnit(idb::LibLibrary* lib_library);
  void readCell(idb::LibCell* lib_cell);
  void readClockChecks(LibertyCell& cell, idb::LibPort* port);
  void readLogicTerms(LogicExpression& logic_expression, LibertyExpr* liberty_expr);
  LogicOperationType logicOperation(const int32_t liberty_expr_op);
  void readCellArcs(LibertyCell& timing_cell, idb::LibArcSet* lib_arc_set);
  bool isSdfDelayArc(idb::LibArc* lib_arc);
  bool isSdfCheckArc(idb::LibArc* lib_arc);
  TimingCellArc readDelayArc(idb::LibArcSet* lib_arc_set);
  void readClearPresetArc(LibertyCell& timing_cell, idb::LibArc* lib_arc);
  TimingCheckArc readCheckArc(idb::LibArcSet* lib_arc_set);
  std::vector<TimingArc> readArcs(idb::LibArcSet* lib_arc_set);
  TimingArc readArc(idb::LibArc* lib_arc);
  void readArcTables(TimingArc& timing_arc, idb::LibArc* lib_arc);
  TimingTable readTable(idb::LibTable* lib_table);
  TimingTableVariableType tableVariableType(idb::LibTable* lib_table, bool is_first_variable);
  double timeUnitScale(idb::LibLibrary* lib_library);
  double capacitanceUnitScale(idb::LibLibrary* lib_library);
  TimingArcSense arcSense(idb::LibArc* lib_arc);
  TransType triggerTransition(idb::LibArc* lib_arc);
  TransType checkTransition(idb::LibArc* lib_arc);
  TimingCheckType checkType(idb::LibArc* lib_arc);
  void readCellInfo(LibertyCell& timing_cell);
};
}  // namespace ista
