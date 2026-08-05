// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "VerilogReader.hh"

namespace idb::verilog {

class ParserContext;
class CppVerilogID;
class CppVerilogNetExpr;

struct ParserRange
{
  bool has_value = false;
  int start = 0;
  int end = 0;
};

struct PortConnectionSpec
{
  CppVerilogID* port_id = nullptr;
  CppVerilogNetExpr* net_expr = nullptr;
};

class ParserContext
{
 public:
  ParserContext() = default;
  ~ParserContext();
  ParserContext(const ParserContext&) = delete;
  ParserContext& operator=(const ParserContext&) = delete;

  bool parseFile(const char* verilog_file_path);
  void reset();

  void beginModule(int line_no, std::string module_name, std::vector<CppVerilogID*> ports);
  void endModule();
  void addDeclaration(int line_no, DclType dcl_type, ParserRange range, std::vector<std::string> names);
  void addInstance(int line_no, std::string cell_name, std::string inst_name, std::vector<PortConnectionSpec> connections);
  void addAssign(int line_no, CppVerilogNetExpr* left, CppVerilogNetExpr* right);

  CppVerilogID* makeId(std::string name);
  CppVerilogID* makeIndexId(std::string base_name, int index);
  CppVerilogID* makeSliceId(std::string base_name, int range_from, int range_to);
  CppVerilogID* makeIdFromName(std::string name);
  CppVerilogNetExpr* makeIdExpr(int line_no, CppVerilogID* id);
  CppVerilogNetExpr* makeConstantExpr(int line_no, std::string text);
  CppVerilogNetExpr* makeConcatExpr(int line_no, std::vector<CppVerilogNetExpr*> items);
  CppVerilogNetExpr* makeAssignCompatibleExpr(int line_no, CppVerilogNetExpr* expr);
  std::string idName(CppVerilogID* id) const;

  void setError(int line_no, const std::string& message);
  bool hasError() const { return _has_error; }
  const std::string& errorMessage() const { return _error_message; }
  int errorLine() const { return _error_line; }

  void* releaseFile();

 private:
  class Impl;

  std::unique_ptr<Impl> _impl;
  bool _has_error = false;
  int _error_line = 0;
  std::string _error_message;
};

}  // namespace idb::verilog
