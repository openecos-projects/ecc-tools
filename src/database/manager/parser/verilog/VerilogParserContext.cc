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

#include "VerilogParserContext.hh"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>

#include "VerilogParser.hh"
#include "VerilogScanner.hh"

#include "utility/logger/Logger.hpp"
namespace idb::verilog {

enum class IdKind
{
  kId,
  kIndex,
  kSlice,
  kConstant
};

enum class ExprKind
{
  kId,
  kConcat,
  kConstant
};

enum class StmtKind
{
  kDcls,
  kInst,
  kAssign
};

namespace {

char* mutableCString(std::string& value)
{
  return value.empty() ? nullptr : value.data();
}

VerilogVec makeVerilogVec(std::vector<void*>& vec)
{
  return VerilogVec{vec.empty() ? nullptr : vec.data(), vec.size(), vec.capacity(), sizeof(void*)};
}

std::string trim(const std::string& value)
{
  size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }

  size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }

  return value.substr(begin, end - begin);
}

std::string stripOuterSpace(const std::string& value)
{
  return trim(value);
}

bool parseInteger(const std::string& value, int& parsed)
{
  try {
    size_t used = 0;
    parsed = std::stoi(value, &used, 10);
    return used == value.size();
  } catch (...) {
    return false;
  }
}

bool splitIndex(const std::string& name, std::string& base, int& index)
{
  const auto open = name.rfind('[');
  const auto close = name.rfind(']');
  if (open == std::string::npos || close == std::string::npos || close != name.size() - 1 || open > close) {
    return false;
  }
  if (name.find('[', close + 1) != std::string::npos || name.find(':', open) != std::string::npos) {
    return false;
  }
  base = stripOuterSpace(name.substr(0, open));
  return parseInteger(name.substr(open + 1, close - open - 1), index);
}

bool splitSlice(const std::string& name, std::string& base, int& from, int& to)
{
  const auto open = name.rfind('[');
  const auto colon = name.rfind(':');
  const auto close = name.rfind(']');
  if (open == std::string::npos || colon == std::string::npos || close == std::string::npos || close != name.size() - 1
      || !(open < colon && colon < close)) {
    return false;
  }
  base = stripOuterSpace(name.substr(0, open));
  return parseInteger(name.substr(open + 1, colon - open - 1), from)
         && parseInteger(name.substr(colon + 1, close - colon - 1), to);
}

CRange makeCRange(ParserRange range)
{
  if (!range.has_value) {
    return CRange{false, 0, 0};
  }
  return CRange{true, range.start, range.end};
}

}  // namespace

class CppVerilogID
{
 public:
  CppVerilogID(IdKind kind, std::string name, std::string base_name, int index, int range_from, int range_to)
      : _kind(kind), _name(std::move(name)), _base_name(std::move(base_name)), _index(index), _range_from(range_from), _range_to(range_to)
  {
    refreshCStructs();
  }

  CppVerilogID(const CppVerilogID& other)
      : _kind(other._kind),
        _name(other._name),
        _base_name(other._base_name),
        _index(other._index),
        _range_from(other._range_from),
        _range_to(other._range_to)
  {
    refreshCStructs();
  }

  IdKind kind() const { return _kind; }
  const std::string& name() const { return _name; }
  const std::string& baseName() const { return _base_name; }
  int index() const { return _index; }
  int rangeFrom() const { return _range_from; }
  int rangeTo() const { return _range_to; }
  int rangeBase() const { return std::min(_range_from, _range_to); }
  int rangeMax() const { return std::max(_range_from, _range_to); }

  std::unique_ptr<CppVerilogID> clone() const { return std::make_unique<CppVerilogID>(*this); }

  CppVerilogID* cloneWithBase(const std::string& new_base, std::vector<std::unique_ptr<CppVerilogID>>& pool) const
  {
    std::unique_ptr<CppVerilogID> cloned;
    if (_kind == IdKind::kIndex) {
      cloned = std::make_unique<CppVerilogID>(IdKind::kIndex, new_base + "[" + std::to_string(_index) + "]", new_base, _index, 0, 0);
    } else if (_kind == IdKind::kSlice) {
      cloned = std::make_unique<CppVerilogID>(IdKind::kSlice,
                                              new_base + "[" + std::to_string(_range_from) + ":" + std::to_string(_range_to) + "]",
                                              new_base, 0, _range_from, _range_to);
    } else {
      cloned = std::make_unique<CppVerilogID>(_kind, new_base, new_base, 0, 0, 0);
    }
    auto* ptr = cloned.get();
    pool.push_back(std::move(cloned));
    return ptr;
  }

  ParsedVerilogID* asId() { return &_c_id; }
  ParsedVerilogIndexID* asIndex() { return &_c_index; }
  ParsedVerilogSliceID* asSlice() { return &_c_slice; }

 private:
  void refreshCStructs()
  {
    _c_id = ParsedVerilogID{mutableCString(_name)};
    _c_index = ParsedVerilogIndexID{mutableCString(_name), mutableCString(_base_name), _index};
    _c_slice = ParsedVerilogSliceID{mutableCString(_name), mutableCString(_base_name), rangeBase(), rangeMax()};
  }

  IdKind _kind = IdKind::kId;
  std::string _name;
  std::string _base_name;
  int _index = 0;
  int _range_from = 0;
  int _range_to = 0;
  ParsedVerilogID _c_id{};
  ParsedVerilogIndexID _c_index{};
  ParsedVerilogSliceID _c_slice{};
};

class CppVerilogNetExpr
{
 public:
  explicit CppVerilogNetExpr(ExprKind kind, int line_no) : _kind(kind), _line_no(line_no) {}

  ExprKind kind() const { return _kind; }
  int lineNo() const { return _line_no; }
  CppVerilogID* id() const { return _id; }
  std::vector<void*>& concatHandles() { return _concat_handles; }
  const std::vector<void*>& concatHandles() const { return _concat_handles; }

  static std::unique_ptr<CppVerilogNetExpr> makeId(int line_no, CppVerilogID* id)
  {
    auto expr = std::make_unique<CppVerilogNetExpr>(ExprKind::kId, line_no);
    expr->_id = id;
    expr->_id_handle = id;
    expr->refreshCStructs();
    return expr;
  }

  static std::unique_ptr<CppVerilogNetExpr> makeConstant(int line_no, CppVerilogID* id)
  {
    auto expr = std::make_unique<CppVerilogNetExpr>(ExprKind::kConstant, line_no);
    expr->_id = id;
    expr->_id_handle = id;
    expr->refreshCStructs();
    return expr;
  }

  static std::unique_ptr<CppVerilogNetExpr> makeConcat(int line_no, std::vector<CppVerilogNetExpr*> items)
  {
    auto expr = std::make_unique<CppVerilogNetExpr>(ExprKind::kConcat, line_no);
    for (auto* item : items) {
      expr->_concat_handles.push_back(item);
    }
    expr->refreshCStructs();
    return expr;
  }

  CppVerilogNetExpr* clone(std::vector<std::unique_ptr<CppVerilogID>>& id_pool, std::vector<std::unique_ptr<CppVerilogNetExpr>>& expr_pool) const
  {
    if (_kind == ExprKind::kConcat) {
      std::vector<CppVerilogNetExpr*> items;
      for (auto* handle : _concat_handles) {
        auto* item = static_cast<CppVerilogNetExpr*>(handle);
        items.push_back(item->clone(id_pool, expr_pool));
      }
      auto cloned = makeConcat(_line_no, items);
      auto* ptr = cloned.get();
      expr_pool.push_back(std::move(cloned));
      return ptr;
    }

    auto id_clone = _id->clone();
    auto* id_ptr = id_clone.get();
    id_pool.push_back(std::move(id_clone));
    std::unique_ptr<CppVerilogNetExpr> cloned
        = (_kind == ExprKind::kConstant) ? makeConstant(_line_no, id_ptr) : makeId(_line_no, id_ptr);
    auto* ptr = cloned.get();
    expr_pool.push_back(std::move(cloned));
    return ptr;
  }

  CppVerilogNetExpr* cloneWithPrefixedBase(const std::string& prefix,
                                           std::vector<std::unique_ptr<CppVerilogID>>& id_pool,
                                           std::vector<std::unique_ptr<CppVerilogNetExpr>>& expr_pool) const
  {
    if (_kind == ExprKind::kConcat) {
      std::vector<CppVerilogNetExpr*> items;
      for (auto* handle : _concat_handles) {
        auto* item = static_cast<CppVerilogNetExpr*>(handle);
        items.push_back(item->cloneWithPrefixedBase(prefix, id_pool, expr_pool));
      }
      auto cloned = makeConcat(_line_no, items);
      auto* ptr = cloned.get();
      expr_pool.push_back(std::move(cloned));
      return ptr;
    }

    if (_kind == ExprKind::kConstant) {
      return clone(id_pool, expr_pool);
    }

    auto* id_ptr = _id->cloneWithBase(prefix + "/" + _id->baseName(), id_pool);
    auto cloned = makeId(_line_no, id_ptr);
    auto* ptr = cloned.get();
    expr_pool.push_back(std::move(cloned));
    return ptr;
  }

  ParsedVerilogNetIDExpr* asIdExpr() { return &_c_id_expr; }
  ParsedVerilogNetConcatExpr* asConcatExpr() { return &_c_concat_expr; }
  ParsedVerilogConstantExpr* asConstantExpr() { return &_c_constant_expr; }

  void refreshCStructs()
  {
    _id_handle = _id;
    _c_id_expr = ParsedVerilogNetIDExpr{static_cast<uintptr_t>(_line_no), &_id_handle};
    _c_constant_expr = ParsedVerilogConstantExpr{static_cast<uintptr_t>(_line_no), &_id_handle};
    _c_concat_expr = ParsedVerilogNetConcatExpr{static_cast<uintptr_t>(_line_no), makeVerilogVec(_concat_handles)};
  }

 private:
  ExprKind _kind;
  int _line_no = 0;
  CppVerilogID* _id = nullptr;
  void* _id_handle = nullptr;
  std::vector<void*> _concat_handles;
  ParsedVerilogNetIDExpr _c_id_expr{};
  ParsedVerilogNetConcatExpr _c_concat_expr{};
  ParsedVerilogConstantExpr _c_constant_expr{};
};

class CppVerilogPortConnection
{
 public:
  CppVerilogPortConnection(CppVerilogID* port_id, CppVerilogNetExpr* net_expr) : _port_id(port_id), _net_expr(net_expr) { refreshCStructs(); }

  CppVerilogID* portId() const { return _port_id; }
  CppVerilogNetExpr* netExpr() const { return _net_expr; }

  std::unique_ptr<CppVerilogPortConnection> clone(std::vector<std::unique_ptr<CppVerilogID>>& id_pool,
                                                  std::vector<std::unique_ptr<CppVerilogNetExpr>>& expr_pool) const
  {
    auto id_clone = _port_id->clone();
    auto* id_ptr = id_clone.get();
    id_pool.push_back(std::move(id_clone));
    auto* expr_ptr = _net_expr ? _net_expr->clone(id_pool, expr_pool) : nullptr;
    return std::make_unique<CppVerilogPortConnection>(id_ptr, expr_ptr);
  }

  void setNetExpr(CppVerilogNetExpr* net_expr)
  {
    _net_expr = net_expr;
    refreshCStructs();
  }

  ParsedVerilogPortRefPortConnect* asCStruct() { return &_c; }

 private:
  void refreshCStructs()
  {
    _port_id_handle = _port_id;
    _net_expr_handle = _net_expr;
    _c = ParsedVerilogPortRefPortConnect{&_port_id_handle, _net_expr ? &_net_expr_handle : nullptr};
  }

  CppVerilogID* _port_id = nullptr;
  CppVerilogNetExpr* _net_expr = nullptr;
  void* _port_id_handle = nullptr;
  void* _net_expr_handle = nullptr;
  ParsedVerilogPortRefPortConnect _c{};
};

class CppVerilogStmt
{
 public:
  explicit CppVerilogStmt(StmtKind kind, int line_no) : _kind(kind), _line_no(line_no) {}
  virtual ~CppVerilogStmt() = default;

  StmtKind kind() const { return _kind; }
  int lineNo() const { return _line_no; }

 private:
  StmtKind _kind;
  int _line_no = 0;
};

class CppVerilogDcl
{
 public:
  CppVerilogDcl(int line_no, DclType dcl_type, std::string name, ParserRange range)
      : _line_no(line_no), _dcl_type(dcl_type), _name(std::move(name)), _range(range)
  {
    refreshCStruct();
  }

  int lineNo() const { return _line_no; }
  DclType dclType() const { return _dcl_type; }
  const std::string& name() const { return _name; }
  ParserRange range() const { return _range; }
  void setName(std::string name)
  {
    _name = std::move(name);
    refreshCStruct();
  }
  ParsedVerilogDcl* asCStruct() { return &_c; }

 private:
  void refreshCStruct() { _c = ParsedVerilogDcl{static_cast<uintptr_t>(_line_no), _dcl_type, mutableCString(_name), makeCRange(_range)}; }

  int _line_no = 0;
  DclType _dcl_type = DclType::KWire;
  std::string _name;
  ParserRange _range;
  ParsedVerilogDcl _c{};
};

class CppVerilogDcls : public CppVerilogStmt
{
 public:
  explicit CppVerilogDcls(int line_no) : CppVerilogStmt(StmtKind::kDcls, line_no) {}

  void addDcl(CppVerilogDcl* dcl)
  {
    _dcls.push_back(dcl);
    _dcl_handles.push_back(dcl);
    refreshCStruct();
  }

  const std::vector<CppVerilogDcl*>& dcls() const { return _dcls; }
  ParsedVerilogDcls* asCStruct()
  {
    refreshCStruct();
    return &_c;
  }

 private:
  void refreshCStruct() { _c = ParsedVerilogDcls{static_cast<uintptr_t>(lineNo()), makeVerilogVec(_dcl_handles)}; }

  std::vector<CppVerilogDcl*> _dcls;
  std::vector<void*> _dcl_handles;
  ParsedVerilogDcls _c{};
};

class CppVerilogInst : public CppVerilogStmt
{
 public:
  CppVerilogInst(int line_no, std::string inst_name, std::string cell_name, std::vector<CppVerilogPortConnection*> connections)
      : CppVerilogStmt(StmtKind::kInst, line_no), _inst_name(std::move(inst_name)), _cell_name(std::move(cell_name)), _connections(std::move(connections))
  {
    refreshCStruct();
  }

  const std::string& instName() const { return _inst_name; }
  const std::string& cellName() const { return _cell_name; }
  const std::vector<CppVerilogPortConnection*>& connections() const { return _connections; }
  void setInstName(std::string name)
  {
    _inst_name = std::move(name);
    refreshCStruct();
  }
  ParsedVerilogInst* asCStruct()
  {
    refreshCStruct();
    return &_c;
  }

 private:
  void refreshCStruct()
  {
    _connection_handles.clear();
    for (auto* connection : _connections) {
      _connection_handles.push_back(connection);
    }
    _c = ParsedVerilogInst{static_cast<uintptr_t>(lineNo()), mutableCString(_inst_name), mutableCString(_cell_name), makeVerilogVec(_connection_handles)};
  }

  std::string _inst_name;
  std::string _cell_name;
  std::vector<CppVerilogPortConnection*> _connections;
  std::vector<void*> _connection_handles;
  ParsedVerilogInst _c{};
};

class CppVerilogAssign : public CppVerilogStmt
{
 public:
  CppVerilogAssign(int line_no, CppVerilogNetExpr* left, CppVerilogNetExpr* right)
      : CppVerilogStmt(StmtKind::kAssign, line_no), _left(left), _right(right)
  {
    refreshCStruct();
  }

  CppVerilogNetExpr* left() const { return _left; }
  CppVerilogNetExpr* right() const { return _right; }
  ParsedVerilogAssign* asCStruct()
  {
    refreshCStruct();
    return &_c;
  }

 private:
  void refreshCStruct()
  {
    _left_handle = _left;
    _right_handle = _right;
    _c = ParsedVerilogAssign{static_cast<uintptr_t>(lineNo()), &_left_handle, &_right_handle};
  }

  CppVerilogNetExpr* _left = nullptr;
  CppVerilogNetExpr* _right = nullptr;
  void* _left_handle = nullptr;
  void* _right_handle = nullptr;
  ParsedVerilogAssign _c{};
};

class CppVerilogModule
{
 public:
  CppVerilogModule(int line_no, std::string name, std::vector<CppVerilogID*> ports)
      : _line_no(line_no), _name(std::move(name)), _ports(std::move(ports))
  {
    refreshCStruct();
  }

  const std::string& name() const { return _name; }
  std::vector<CppVerilogStmt*>& stmts() { return _stmts; }
  const std::vector<CppVerilogStmt*>& stmts() const { return _stmts; }

  void addStmt(CppVerilogStmt* stmt)
  {
    _stmts.push_back(stmt);
    refreshCStruct();
  }

  void eraseStmt(CppVerilogStmt* stmt)
  {
    _stmts.erase(std::remove(_stmts.begin(), _stmts.end(), stmt), _stmts.end());
    refreshCStruct();
  }

  bool isPort(const std::string& name) const
  {
    return std::any_of(_ports.begin(), _ports.end(), [&](auto* port) { return port->name() == name || port->baseName() == name; });
  }

  std::optional<ParserRange> findDclRange(const std::string& name) const
  {
    for (auto* stmt : _stmts) {
      if (stmt->kind() != StmtKind::kDcls) {
        continue;
      }
      auto* dcls = static_cast<CppVerilogDcls*>(stmt);
      for (auto* dcl : dcls->dcls()) {
        if (dcl->name() == name) {
          return dcl->range();
        }
      }
    }
    return std::nullopt;
  }

  bool isDeclaredPort(const std::string& name) const
  {
    for (auto* stmt : _stmts) {
      if (stmt->kind() != StmtKind::kDcls) {
        continue;
      }
      auto* dcls = static_cast<CppVerilogDcls*>(stmt);
      for (auto* dcl : dcls->dcls()) {
        if (dcl->name() == name
            && (dcl->dclType() == DclType::KInput || dcl->dclType() == DclType::KOutput || dcl->dclType() == DclType::KInout)) {
          return true;
        }
      }
    }
    return isPort(name);
  }

  ParsedVerilogModule* asCStruct()
  {
    refreshCStruct();
    return &_c;
  }

 private:
  void refreshCStruct()
  {
    _port_handles.clear();
    for (auto* port : _ports) {
      _port_handles.push_back(port);
    }
    _stmt_handles.clear();
    for (auto* stmt : _stmts) {
      _stmt_handles.push_back(stmt);
    }
    _c = ParsedVerilogModule{static_cast<uintptr_t>(_line_no), mutableCString(_name), makeVerilogVec(_port_handles), makeVerilogVec(_stmt_handles)};
  }

  int _line_no = 0;
  std::string _name;
  std::vector<CppVerilogID*> _ports;
  std::vector<CppVerilogStmt*> _stmts;
  std::vector<void*> _port_handles;
  std::vector<void*> _stmt_handles;
  ParsedVerilogModule _c{};
};

class CppVerilogFile
{
 public:
  void addModule(std::unique_ptr<CppVerilogModule> module)
  {
    auto* ptr = module.get();
    _module_map[ptr->name()] = ptr;
    _module_handles.push_back(ptr);
    _modules.push_back(std::move(module));
    refreshCStruct();
  }

  CppVerilogModule* findModule(const std::string& name) const
  {
    auto iter = _module_map.find(name);
    return iter == _module_map.end() ? nullptr : iter->second;
  }

  const std::vector<std::unique_ptr<CppVerilogModule>>& modules() const { return _modules; }
  ParsedVerilogFile* asCStruct()
  {
    refreshCStruct();
    return &_c;
  }

  std::vector<std::unique_ptr<CppVerilogID>> ids;
  std::vector<std::unique_ptr<CppVerilogNetExpr>> exprs;
  std::vector<std::unique_ptr<CppVerilogPortConnection>> connections;
  std::vector<std::unique_ptr<CppVerilogStmt>> stmts;
  std::vector<std::unique_ptr<CppVerilogDcl>> dcls;

 private:
  void refreshCStruct() { _c = ParsedVerilogFile{makeVerilogVec(_module_handles)}; }

  std::vector<std::unique_ptr<CppVerilogModule>> _modules;
  std::unordered_map<std::string, CppVerilogModule*> _module_map;
  std::vector<void*> _module_handles;
  ParsedVerilogFile _c{};
};

class ParserContext::Impl
{
 public:
  std::unique_ptr<CppVerilogFile> file = std::make_unique<CppVerilogFile>();
  CppVerilogModule* current_module = nullptr;
};

ParserContext::~ParserContext() = default;

void ParserContext::reset()
{
  _impl = std::make_unique<Impl>();
  _has_error = false;
  _error_line = 0;
  _error_message.clear();
}

bool ParserContext::parseFile(const char* verilog_file_path)
{
  reset();
  std::ifstream stream(verilog_file_path);
  if (!stream) {
    setError(0, std::string("cannot open verilog file: ") + verilog_file_path);
    return false;
  }

  Scanner scanner(&stream);
  Parser parser(&scanner, this);
  const int status = parser.parse();
  return status == 0 && !hasError();
}

void ParserContext::beginModule(int line_no, std::string module_name, std::vector<CppVerilogID*> ports)
{
  auto module = std::make_unique<CppVerilogModule>(line_no, std::move(module_name), std::move(ports));
  _impl->current_module = module.get();
  _impl->file->addModule(std::move(module));
}

void ParserContext::endModule()
{
  _impl->current_module = nullptr;
}

void ParserContext::addDeclaration(int line_no, DclType dcl_type, ParserRange range, std::vector<std::string> names)
{
  if (!_impl->current_module) {
    return;
  }

  auto dcls = std::make_unique<CppVerilogDcls>(line_no);
  auto* dcls_ptr = dcls.get();
  _impl->file->stmts.push_back(std::move(dcls));

  for (auto& name : names) {
    auto dcl = std::make_unique<CppVerilogDcl>(line_no, dcl_type, stripOuterSpace(name), range);
    auto* dcl_ptr = dcl.get();
    _impl->file->dcls.push_back(std::move(dcl));
    dcls_ptr->addDcl(dcl_ptr);
  }

  _impl->current_module->addStmt(dcls_ptr);
}

void ParserContext::addInstance(int line_no, std::string cell_name, std::string inst_name, std::vector<PortConnectionSpec> connections)
{
  if (!_impl->current_module) {
    return;
  }

  std::vector<CppVerilogPortConnection*> connection_ptrs;
  for (auto& connection : connections) {
    auto conn = std::make_unique<CppVerilogPortConnection>(connection.port_id, connection.net_expr);
    connection_ptrs.push_back(conn.get());
    _impl->file->connections.push_back(std::move(conn));
  }

  auto inst = std::make_unique<CppVerilogInst>(line_no, stripOuterSpace(inst_name), stripOuterSpace(cell_name), std::move(connection_ptrs));
  auto* inst_ptr = inst.get();
  _impl->file->stmts.push_back(std::move(inst));
  _impl->current_module->addStmt(inst_ptr);
}

void ParserContext::addAssign(int line_no, CppVerilogNetExpr* left, CppVerilogNetExpr* right)
{
  if (!_impl->current_module) {
    return;
  }

  auto assign = std::make_unique<CppVerilogAssign>(line_no, left, right);
  auto* assign_ptr = assign.get();
  _impl->file->stmts.push_back(std::move(assign));
  _impl->current_module->addStmt(assign_ptr);
}

CppVerilogID* ParserContext::makeId(std::string name)
{
  name = stripOuterSpace(name);
  auto id = std::make_unique<CppVerilogID>(IdKind::kId, name, name, 0, 0, 0);
  auto* ptr = id.get();
  _impl->file->ids.push_back(std::move(id));
  return ptr;
}

CppVerilogID* ParserContext::makeIndexId(std::string base_name, int index)
{
  base_name = stripOuterSpace(base_name);
  auto id = std::make_unique<CppVerilogID>(IdKind::kIndex, base_name + "[" + std::to_string(index) + "]", base_name, index, 0, 0);
  auto* ptr = id.get();
  _impl->file->ids.push_back(std::move(id));
  return ptr;
}

CppVerilogID* ParserContext::makeSliceId(std::string base_name, int range_from, int range_to)
{
  base_name = stripOuterSpace(base_name);
  auto id = std::make_unique<CppVerilogID>(IdKind::kSlice,
                                           base_name + "[" + std::to_string(range_from) + ":" + std::to_string(range_to) + "]",
                                           base_name, 0, range_from, range_to);
  auto* ptr = id.get();
  _impl->file->ids.push_back(std::move(id));
  return ptr;
}

CppVerilogID* ParserContext::makeIdFromName(std::string name)
{
  name = stripOuterSpace(name);
  std::string base;
  int from = 0;
  int to = 0;
  int index = 0;
  if (splitSlice(name, base, from, to)) {
    return makeSliceId(base, from, to);
  }
  if (splitIndex(name, base, index)) {
    return makeIndexId(base, index);
  }
  return makeId(name);
}

CppVerilogNetExpr* ParserContext::makeIdExpr(int line_no, CppVerilogID* id)
{
  auto expr = CppVerilogNetExpr::makeId(line_no, id);
  auto* ptr = expr.get();
  _impl->file->exprs.push_back(std::move(expr));
  return ptr;
}

CppVerilogNetExpr* ParserContext::makeConstantExpr(int line_no, std::string text)
{
  text = stripOuterSpace(text);
  auto id = std::make_unique<CppVerilogID>(IdKind::kConstant, text, text, 0, 0, 0);
  auto* id_ptr = id.get();
  _impl->file->ids.push_back(std::move(id));
  auto expr = CppVerilogNetExpr::makeConstant(line_no, id_ptr);
  auto* ptr = expr.get();
  _impl->file->exprs.push_back(std::move(expr));
  return ptr;
}

CppVerilogNetExpr* ParserContext::makeConcatExpr(int line_no, std::vector<CppVerilogNetExpr*> items)
{
  auto expr = CppVerilogNetExpr::makeConcat(line_no, std::move(items));
  auto* ptr = expr.get();
  _impl->file->exprs.push_back(std::move(expr));
  return ptr;
}

CppVerilogNetExpr* ParserContext::makeAssignCompatibleExpr(int line_no, CppVerilogNetExpr* expr)
{
  if (expr && expr->kind() == ExprKind::kConstant) {
    return makeIdExpr(line_no, makeId(expr->id()->name()));
  }
  return expr;
}

std::string ParserContext::idName(CppVerilogID* id) const
{
  return id ? id->name() : std::string();
}

void ParserContext::setError(int line_no, const std::string& message)
{
  if (_has_error) {
    return;
  }
  _has_error = true;
  _error_line = line_no;
  _error_message = message;
}

void* ParserContext::releaseFile()
{
  return _impl->file.release();
}

template <typename T>
T* resolveHandle(void* handle)
{
  if (!handle) {
    return nullptr;
  }
  return *static_cast<T**>(handle);
}

CppVerilogID* resolveId(void* handle)
{
  return resolveHandle<CppVerilogID>(handle);
}

CppVerilogNetExpr* resolveExpr(void* handle)
{
  return resolveHandle<CppVerilogNetExpr>(handle);
}

CppVerilogStmt* resolveStmt(void* handle)
{
  return resolveHandle<CppVerilogStmt>(handle);
}

CppVerilogDcl* resolveDcl(void* handle)
{
  return resolveHandle<CppVerilogDcl>(handle);
}

CppVerilogPortConnection* resolvePortConnection(void* handle)
{
  return resolveHandle<CppVerilogPortConnection>(handle);
}

CppVerilogNetExpr* mapExprForFlatten(CppVerilogNetExpr* expr,
                                     const std::string& inst_prefix,
                                     const CppVerilogModule* child_module,
                                     const CppVerilogModule* parent_module,
                                     const CppVerilogInst* parent_inst,
                                     std::vector<std::unique_ptr<CppVerilogID>>& id_pool,
                                     std::vector<std::unique_ptr<CppVerilogNetExpr>>& expr_pool);

std::vector<int> indexSequence(int from, int to)
{
  std::vector<int> indexes;
  const int step = from > to ? -1 : 1;
  for (int index = from;; index += step) {
    indexes.push_back(index);
    if (index == to) {
      break;
    }
  }
  return indexes;
}

std::optional<ParserRange> declarationRangeFor(const CppVerilogModule* module, const CppVerilogID* id)
{
  if (!module || !id) {
    return std::nullopt;
  }
  if (auto range = module->findDclRange(id->baseName())) {
    return range;
  }
  if (id->name() != id->baseName()) {
    return module->findDclRange(id->name());
  }
  return std::nullopt;
}

CppVerilogID* makePooledIndexId(const std::string& base_name, int index, std::vector<std::unique_ptr<CppVerilogID>>& id_pool)
{
  auto id = std::make_unique<CppVerilogID>(IdKind::kIndex, base_name + "[" + std::to_string(index) + "]", base_name, index, 0, 0);
  auto* ptr = id.get();
  id_pool.push_back(std::move(id));
  return ptr;
}

CppVerilogNetExpr* makePooledIdExpr(int line_no, CppVerilogID* id, std::vector<std::unique_ptr<CppVerilogNetExpr>>& expr_pool)
{
  auto expr = CppVerilogNetExpr::makeId(line_no, id);
  auto* ptr = expr.get();
  expr_pool.push_back(std::move(expr));
  return ptr;
}

CppVerilogNetExpr* makePooledConstantExpr(int line_no,
                                          const std::string& text,
                                          std::vector<std::unique_ptr<CppVerilogID>>& id_pool,
                                          std::vector<std::unique_ptr<CppVerilogNetExpr>>& expr_pool)
{
  auto id = std::make_unique<CppVerilogID>(IdKind::kConstant, text, text, 0, 0, 0);
  auto* id_ptr = id.get();
  id_pool.push_back(std::move(id));
  auto expr = CppVerilogNetExpr::makeConstant(line_no, id_ptr);
  auto* ptr = expr.get();
  expr_pool.push_back(std::move(expr));
  return ptr;
}

CppVerilogNetExpr* makePooledConcatExpr(int line_no,
                                        std::vector<CppVerilogNetExpr*> items,
                                        std::vector<std::unique_ptr<CppVerilogNetExpr>>& expr_pool)
{
  auto expr = CppVerilogNetExpr::makeConcat(line_no, std::move(items));
  auto* ptr = expr.get();
  expr_pool.push_back(std::move(expr));
  return ptr;
}

std::vector<char> constantBits(const std::string& text)
{
  const auto quote = text.find('\'');
  if (quote == std::string::npos) {
    return text == "0" ? std::vector<char>{'0'} : std::vector<char>{'1'};
  }

  int width = 0;
  std::string width_text = text.substr(0, quote);
  if (!width_text.empty() && (width_text.front() == '+' || width_text.front() == '-')) {
    width_text.erase(width_text.begin());
  }
  if (!width_text.empty()) {
    parseInteger(width_text, width);
  }

  size_t base_index = quote + 1;
  if (base_index < text.size() && (text[base_index] == 's' || text[base_index] == 'S')) {
    ++base_index;
  }
  if (base_index >= text.size()) {
    return {'0'};
  }

  const char base = static_cast<char>(std::tolower(static_cast<unsigned char>(text[base_index])));
  std::string digits;
  for (size_t index = base_index + 1; index < text.size(); ++index) {
    const char ch = text[index];
    if (ch != '_') {
      digits.push_back(ch);
    }
  }

  if (digits.empty()) {
    return {'0'};
  }

  std::vector<char> bits;
  auto append_value_bits = [&bits](unsigned value, int bit_count) {
    for (int shift = bit_count - 1; shift >= 0; --shift) {
      bits.push_back(((value >> shift) & 1U) ? '1' : '0');
    }
  };

  if (base == 'b') {
    for (char ch : digits) {
      bits.push_back(ch);
    }
  } else if (base == 'o') {
    for (char ch : digits) {
      if (ch == 'x' || ch == 'X' || ch == 'z' || ch == 'Z' || ch == '?') {
        bits.insert(bits.end(), 3, ch);
      } else {
        append_value_bits(static_cast<unsigned>(ch - '0'), 3);
      }
    }
  } else if (base == 'h') {
    for (char ch : digits) {
      if (ch == 'x' || ch == 'X' || ch == 'z' || ch == 'Z' || ch == '?') {
        bits.insert(bits.end(), 4, ch);
      } else {
        const auto lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        const unsigned value = lower >= 'a' ? static_cast<unsigned>(lower - 'a' + 10) : static_cast<unsigned>(lower - '0');
        append_value_bits(value, 4);
      }
    }
  } else if (base == 'd') {
    unsigned long long value = 0;
    try {
      value = std::stoull(digits, nullptr, 10);
    } catch (...) {
      return {'0'};
    }
    const int bit_count = width > 0 ? width : 1;
    for (int shift = bit_count - 1; shift >= 0; --shift) {
      bits.push_back(((value >> shift) & 1ULL) ? '1' : '0');
    }
  }

  if (bits.empty()) {
    return {'0'};
  }
  if (width > 0 && static_cast<size_t>(width) > bits.size()) {
    bits.insert(bits.begin(), static_cast<size_t>(width) - bits.size(), '0');
  } else if (width > 0 && static_cast<size_t>(width) < bits.size()) {
    bits.erase(bits.begin(), bits.end() - width);
  }
  return bits;
}

std::vector<CppVerilogNetExpr*> expandExprToBits(CppVerilogNetExpr* expr,
                                                 const CppVerilogModule* module,
                                                 std::vector<std::unique_ptr<CppVerilogID>>& id_pool,
                                                 std::vector<std::unique_ptr<CppVerilogNetExpr>>& expr_pool)
{
  if (!expr) {
    return {};
  }
  if (expr->kind() == ExprKind::kConcat) {
    std::vector<CppVerilogNetExpr*> expanded;
    for (auto* handle : expr->concatHandles()) {
      auto* item = static_cast<CppVerilogNetExpr*>(handle);
      auto item_bits = expandExprToBits(item, module, id_pool, expr_pool);
      expanded.insert(expanded.end(), item_bits.begin(), item_bits.end());
    }
    return expanded;
  }
  if (expr->kind() == ExprKind::kConstant) {
    std::vector<CppVerilogNetExpr*> bits;
    for (char bit : constantBits(expr->id()->name())) {
      bits.push_back(makePooledConstantExpr(expr->lineNo(), std::string("1'b") + bit, id_pool, expr_pool));
    }
    return bits;
  }

  const auto* id = expr->id();
  if (id->kind() == IdKind::kIndex) {
    return {expr->clone(id_pool, expr_pool)};
  }
  if (id->kind() == IdKind::kSlice) {
    std::vector<CppVerilogNetExpr*> bits;
    for (int index : indexSequence(id->rangeFrom(), id->rangeTo())) {
      bits.push_back(makePooledIdExpr(expr->lineNo(), makePooledIndexId(id->baseName(), index, id_pool), expr_pool));
    }
    return bits;
  }
  if (auto range = declarationRangeFor(module, id); range && range->has_value) {
    std::vector<CppVerilogNetExpr*> bits;
    for (int index : indexSequence(range->start, range->end)) {
      bits.push_back(makePooledIdExpr(expr->lineNo(), makePooledIndexId(id->baseName(), index, id_pool), expr_pool));
    }
    return bits;
  }
  return {expr->clone(id_pool, expr_pool)};
}

std::optional<size_t> offsetInRange(ParserRange range, int index)
{
  if (!range.has_value) {
    return std::nullopt;
  }
  size_t offset = 0;
  for (int candidate : indexSequence(range.start, range.end)) {
    if (candidate == index) {
      return offset;
    }
    ++offset;
  }
  return std::nullopt;
}

int applyOffset(ParserRange range, size_t offset)
{
  return range.start > range.end ? range.start - static_cast<int>(offset) : range.start + static_cast<int>(offset);
}

CppVerilogNetExpr* selectBitFromExpr(CppVerilogNetExpr* expr,
                                     int source_index,
                                     std::optional<ParserRange> source_range,
                                     const CppVerilogModule* target_module,
                                     std::vector<std::unique_ptr<CppVerilogID>>& id_pool,
                                     std::vector<std::unique_ptr<CppVerilogNetExpr>>& expr_pool)
{
  if (!expr) {
    return nullptr;
  }

  const size_t offset = source_range ? offsetInRange(*source_range, source_index).value_or(0) : 0;
  if (expr->kind() == ExprKind::kConcat || expr->kind() == ExprKind::kConstant) {
    auto bits = expandExprToBits(expr, target_module, id_pool, expr_pool);
    return offset < bits.size() ? bits[offset] : expr->clone(id_pool, expr_pool);
  }

  const auto* id = expr->id();
  if (id->kind() == IdKind::kIndex) {
    return expr->clone(id_pool, expr_pool);
  }
  if (id->kind() == IdKind::kSlice) {
    auto indexes = indexSequence(id->rangeFrom(), id->rangeTo());
    if (offset < indexes.size()) {
      return makePooledIdExpr(expr->lineNo(), makePooledIndexId(id->baseName(), indexes[offset], id_pool), expr_pool);
    }
    return expr->clone(id_pool, expr_pool);
  }
  if (auto target_range = declarationRangeFor(target_module, id); target_range && target_range->has_value) {
    return makePooledIdExpr(expr->lineNo(), makePooledIndexId(id->baseName(), applyOffset(*target_range, offset), id_pool), expr_pool);
  }
  return expr->clone(id_pool, expr_pool);
}

CppVerilogNetExpr* selectSliceFromExpr(CppVerilogNetExpr* expr,
                                       const CppVerilogID* source_id,
                                       std::optional<ParserRange> source_range,
                                       const CppVerilogModule* target_module,
                                       std::vector<std::unique_ptr<CppVerilogID>>& id_pool,
                                       std::vector<std::unique_ptr<CppVerilogNetExpr>>& expr_pool)
{
  std::vector<CppVerilogNetExpr*> items;
  for (int index : indexSequence(source_id->rangeFrom(), source_id->rangeTo())) {
    if (auto* selected = selectBitFromExpr(expr, index, source_range, target_module, id_pool, expr_pool)) {
      items.push_back(selected);
    }
  }
  if (items.empty()) {
    return nullptr;
  }
  if (items.size() == 1) {
    return items.front();
  }
  return makePooledConcatExpr(expr ? expr->lineNo() : 0, std::move(items), expr_pool);
}

CppVerilogNetExpr* parentConnectionForPort(const CppVerilogID* port_id,
                                           const CppVerilogModule* child_module,
                                           const CppVerilogModule* parent_module,
                                           const CppVerilogInst* parent_inst,
                                           std::vector<std::unique_ptr<CppVerilogID>>& id_pool,
                                           std::vector<std::unique_ptr<CppVerilogNetExpr>>& expr_pool)
{
  for (auto* connection : parent_inst->connections()) {
    if (!connection->netExpr()) {
      continue;
    }
    const auto* connected_port = connection->portId();
    if (connected_port->name() == port_id->baseName() || connected_port->name() == port_id->name()
        || connected_port->baseName() == port_id->baseName()) {
      auto source_range = declarationRangeFor(child_module, port_id);
      if (port_id->kind() == IdKind::kIndex) {
        return selectBitFromExpr(connection->netExpr(), port_id->index(), source_range, parent_module, id_pool, expr_pool);
      }
      if (port_id->kind() == IdKind::kSlice) {
        return selectSliceFromExpr(connection->netExpr(), port_id, source_range, parent_module, id_pool, expr_pool);
      }
      return connection->netExpr()->clone(id_pool, expr_pool);
    }
  }
  return nullptr;
}

CppVerilogNetExpr* mapExprForFlatten(CppVerilogNetExpr* expr,
                                     const std::string& inst_prefix,
                                     const CppVerilogModule* child_module,
                                     const CppVerilogModule* parent_module,
                                     const CppVerilogInst* parent_inst,
                                     std::vector<std::unique_ptr<CppVerilogID>>& id_pool,
                                     std::vector<std::unique_ptr<CppVerilogNetExpr>>& expr_pool)
{
  if (!expr) {
    return nullptr;
  }
  if (expr->kind() == ExprKind::kConcat) {
    std::vector<CppVerilogNetExpr*> items;
    for (auto* handle : expr->concatHandles()) {
      auto* item = static_cast<CppVerilogNetExpr*>(handle);
      items.push_back(mapExprForFlatten(item, inst_prefix, child_module, parent_module, parent_inst, id_pool, expr_pool));
    }
    return makePooledConcatExpr(expr->lineNo(), std::move(items), expr_pool);
  }
  if (expr->kind() == ExprKind::kConstant) {
    return expr->clone(id_pool, expr_pool);
  }

  const auto* id = expr->id();
  if (child_module->isDeclaredPort(id->baseName()) || child_module->isDeclaredPort(id->name())) {
    if (auto* parent_expr = parentConnectionForPort(id, child_module, parent_module, parent_inst, id_pool, expr_pool)) {
      return parent_expr;
    }
  }

  if (id->kind() == IdKind::kId) {
    auto range = declarationRangeFor(child_module, id);
    if (range && range->has_value) {
      std::vector<CppVerilogNetExpr*> items;
      for (int index : indexSequence(range->start, range->end)) {
        items.push_back(makePooledIdExpr(expr->lineNo(), makePooledIndexId(inst_prefix + "/" + id->baseName(), index, id_pool), expr_pool));
      }
      return makePooledConcatExpr(expr->lineNo(), std::move(items), expr_pool);
    }
  }

  return expr->cloneWithPrefixedBase(inst_prefix, id_pool, expr_pool);
}

std::unique_ptr<CppVerilogPortConnection> cloneConnectionForFlatten(CppVerilogPortConnection* connection,
                                                                    const std::string& inst_prefix,
                                                                    const CppVerilogModule* child_module,
                                                                    const CppVerilogModule* parent_module,
                                                                    const CppVerilogInst* parent_inst,
                                                                    CppVerilogFile* file)
{
  auto id_clone = connection->portId()->clone();
  auto* id_ptr = id_clone.get();
  file->ids.push_back(std::move(id_clone));
  auto* expr_ptr = mapExprForFlatten(connection->netExpr(), inst_prefix, child_module, parent_module, parent_inst, file->ids, file->exprs);
  return std::make_unique<CppVerilogPortConnection>(id_ptr, expr_ptr);
}

void flattenChildIntoParent(CppVerilogFile* file, CppVerilogModule* child, CppVerilogModule* parent, CppVerilogInst* parent_inst)
{
  const std::string inst_prefix = parent_inst->instName();
  std::vector<CppVerilogStmt*> child_stmts = child->stmts();
  for (auto* stmt : child_stmts) {
    if (stmt->kind() == StmtKind::kDcls) {
      auto* dcls = static_cast<CppVerilogDcls*>(stmt);
      for (auto* dcl : dcls->dcls()) {
        if (dcl->dclType() != DclType::KWire || child->isDeclaredPort(dcl->name())) {
          continue;
        }
        auto new_dcls = std::make_unique<CppVerilogDcls>(dcl->lineNo());
        auto* new_dcls_ptr = new_dcls.get();
        file->stmts.push_back(std::move(new_dcls));

        auto new_dcl = std::make_unique<CppVerilogDcl>(dcl->lineNo(), dcl->dclType(), inst_prefix + "/" + dcl->name(), dcl->range());
        auto* new_dcl_ptr = new_dcl.get();
        file->dcls.push_back(std::move(new_dcl));
        new_dcls_ptr->addDcl(new_dcl_ptr);
        parent->addStmt(new_dcls_ptr);
      }
    } else if (stmt->kind() == StmtKind::kInst) {
      auto* child_inst = static_cast<CppVerilogInst*>(stmt);
      std::vector<CppVerilogPortConnection*> connections;
      for (auto* connection : child_inst->connections()) {
        auto cloned = cloneConnectionForFlatten(connection, inst_prefix, child, parent, parent_inst, file);
        connections.push_back(cloned.get());
        file->connections.push_back(std::move(cloned));
      }
      auto inst = std::make_unique<CppVerilogInst>(child_inst->lineNo(), inst_prefix + "/" + child_inst->instName(), child_inst->cellName(),
                                                   std::move(connections));
      auto* inst_ptr = inst.get();
      file->stmts.push_back(std::move(inst));
      parent->addStmt(inst_ptr);
    } else if (stmt->kind() == StmtKind::kAssign) {
      auto* assign = static_cast<CppVerilogAssign*>(stmt);
      auto* left = mapExprForFlatten(assign->left(), inst_prefix, child, parent, parent_inst, file->ids, file->exprs);
      auto* right = mapExprForFlatten(assign->right(), inst_prefix, child, parent, parent_inst, file->ids, file->exprs);
      auto new_assign = std::make_unique<CppVerilogAssign>(assign->lineNo(), left, right);
      auto* assign_ptr = new_assign.get();
      file->stmts.push_back(std::move(new_assign));
      parent->addStmt(assign_ptr);
    }
  }
}

void flattenModule(CppVerilogFile* file, CppVerilogModule* top)
{
  if (!file || !top) {
    return;
  }

  bool changed = true;
  while (changed) {
    changed = false;
    std::vector<CppVerilogStmt*> stmts = top->stmts();
    for (auto* stmt : stmts) {
      if (stmt->kind() != StmtKind::kInst) {
        continue;
      }
      auto* inst = static_cast<CppVerilogInst*>(stmt);
      auto* child = file->findModule(inst->cellName());
      if (!child) {
        continue;
      }
      flattenModule(file, child);
      flattenChildIntoParent(file, child, top, inst);
      top->eraseStmt(stmt);
      changed = true;
      break;
    }
  }
}

}  // namespace idb::verilog

extern "C" {

void* verilog_parse_file(const char* verilog_path)
{
  auto context = std::make_unique<idb::verilog::ParserContext>();
  if (!context->parseFile(verilog_path)) {
    if (context->errorLine() > 0) {
      ECCLOG.warn(ecc::Loc::current(), "Parse Verilog failed at line ", context->errorLine(), ": ", context->errorMessage());
    } else {
      ECCLOG.warn(ecc::Loc::current(), "Parse Verilog failed: ", context->errorMessage());
    }
    return nullptr;
  }
  return context->releaseFile();
}

void verilog_flatten_module(void* c_verilog_file, const char* top_module_name)
{
  auto* file = static_cast<idb::verilog::CppVerilogFile*>(c_verilog_file);
  if (!file || !top_module_name) {
    return;
  }
  auto* top = file->findModule(top_module_name);
  idb::verilog::flattenModule(file, top);
}

void verilog_free_file(void* c_verilog_file)
{
  delete static_cast<idb::verilog::CppVerilogFile*>(c_verilog_file);
}

uintptr_t verilog_vec_len(const VerilogVec* vec)
{
  return vec ? vec->len : 0;
}

void verilog_free_c_char(char*) {}

ParsedVerilogID* verilog_convert_id(void* c_verilog_virtual_base_id)
{
  auto* id = idb::verilog::resolveId(c_verilog_virtual_base_id);
  return id ? id->asId() : nullptr;
}

bool verilog_is_id(void* c_verilog_virtual_base_id)
{
  auto* id = idb::verilog::resolveId(c_verilog_virtual_base_id);
  return id && id->kind() == idb::verilog::IdKind::kId;
}

ParsedVerilogIndexID* verilog_convert_index_id(void* c_verilog_virtual_base_id)
{
  auto* id = idb::verilog::resolveId(c_verilog_virtual_base_id);
  return id ? id->asIndex() : nullptr;
}

bool verilog_is_bus_index_id(void* c_verilog_virtual_base_id)
{
  auto* id = idb::verilog::resolveId(c_verilog_virtual_base_id);
  return id && id->kind() == idb::verilog::IdKind::kIndex;
}

const char* verilog_get_index_name(ParsedVerilogSliceID* verilog_slice_id, uintptr_t index)
{
  static thread_local std::string index_name;
  if (!verilog_slice_id || !verilog_slice_id->base_id) {
    index_name.clear();
  } else {
    index_name = std::string(verilog_slice_id->base_id) + "[" + std::to_string(index) + "]";
  }
  return index_name.c_str();
}

ParsedVerilogSliceID* verilog_convert_slice_id(void* c_verilog_virtual_base_id)
{
  auto* id = idb::verilog::resolveId(c_verilog_virtual_base_id);
  return id ? id->asSlice() : nullptr;
}

bool verilog_is_bus_slice_id(void* c_verilog_virtual_base_id)
{
  auto* id = idb::verilog::resolveId(c_verilog_virtual_base_id);
  return id && id->kind() == idb::verilog::IdKind::kSlice;
}

ParsedVerilogNetIDExpr* verilog_convert_net_id_expr(void* c_verilog_net_id_expr)
{
  auto* expr = idb::verilog::resolveExpr(c_verilog_net_id_expr);
  return expr ? expr->asIdExpr() : nullptr;
}

ParsedVerilogNetConcatExpr* verilog_convert_net_concat_expr(void* c_verilog_net_concat_expr)
{
  auto* expr = idb::verilog::resolveExpr(c_verilog_net_concat_expr);
  return expr ? expr->asConcatExpr() : nullptr;
}

ParsedVerilogConstantExpr* verilog_convert_constant_expr(void* c_verilog_constant_expr)
{
  auto* expr = idb::verilog::resolveExpr(c_verilog_constant_expr);
  return expr ? expr->asConstantExpr() : nullptr;
}

bool verilog_is_id_expr(void* c_verilog_virtual_base_net_expr)
{
  auto* expr = idb::verilog::resolveExpr(c_verilog_virtual_base_net_expr);
  return expr && expr->kind() == idb::verilog::ExprKind::kId;
}

bool verilog_is_concat_expr(void* c_verilog_virtual_base_net_expr)
{
  auto* expr = idb::verilog::resolveExpr(c_verilog_virtual_base_net_expr);
  return expr && expr->kind() == idb::verilog::ExprKind::kConcat;
}

bool verilog_is_constant(void* c_verilog_virtual_base_net_expr)
{
  auto* expr = idb::verilog::resolveExpr(c_verilog_virtual_base_net_expr);
  return expr && expr->kind() == idb::verilog::ExprKind::kConstant;
}

ParsedVerilogModule* verilog_convert_module(void* verilog_module)
{
  auto* module = static_cast<idb::verilog::CppVerilogModule*>(verilog_module);
  return module ? module->asCStruct() : nullptr;
}

ParsedVerilogDcl* verilog_convert_dcl(void* c_verilog_dcl_struct)
{
  auto* dcl = idb::verilog::resolveDcl(c_verilog_dcl_struct);
  return dcl ? dcl->asCStruct() : nullptr;
}

ParsedVerilogDcls* verilog_convert_dcls(void* verilog_dcls_struct)
{
  auto* stmt = idb::verilog::resolveStmt(verilog_dcls_struct);
  return stmt && stmt->kind() == idb::verilog::StmtKind::kDcls ? static_cast<idb::verilog::CppVerilogDcls*>(stmt)->asCStruct()
                                                               : nullptr;
}

ParsedVerilogInst* verilog_convert_inst(void* verilog_inst)
{
  auto* stmt = idb::verilog::resolveStmt(verilog_inst);
  return stmt && stmt->kind() == idb::verilog::StmtKind::kInst ? static_cast<idb::verilog::CppVerilogInst*>(stmt)->asCStruct() : nullptr;
}

ParsedVerilogAssign* verilog_convert_assign(void* c_verilog_assign)
{
  auto* stmt = idb::verilog::resolveStmt(c_verilog_assign);
  return stmt && stmt->kind() == idb::verilog::StmtKind::kAssign ? static_cast<idb::verilog::CppVerilogAssign*>(stmt)->asCStruct()
                                                                 : nullptr;
}

ParsedVerilogPortRefPortConnect* verilog_convert_port_ref_port_connect(void* c_port_connect)
{
  auto* connection = idb::verilog::resolvePortConnection(c_port_connect);
  return connection ? connection->asCStruct() : nullptr;
}

bool verilog_is_module_inst_stmt(void* c_verilog_stmt)
{
  auto* stmt = idb::verilog::resolveStmt(c_verilog_stmt);
  return stmt && stmt->kind() == idb::verilog::StmtKind::kInst;
}

bool verilog_is_module_assign_stmt(void* c_verilog_stmt)
{
  auto* stmt = idb::verilog::resolveStmt(c_verilog_stmt);
  return stmt && stmt->kind() == idb::verilog::StmtKind::kAssign;
}

bool verilog_is_dcl_stmt(void*) { return false; }

bool verilog_is_dcls_stmt(void* c_verilog_stmt)
{
  auto* stmt = idb::verilog::resolveStmt(c_verilog_stmt);
  return stmt && stmt->kind() == idb::verilog::StmtKind::kDcls;
}

bool verilog_is_module_stmt(void*) { return false; }

ParsedVerilogFile* verilog_convert_file(void* c_verilog_file)
{
  auto* file = static_cast<idb::verilog::CppVerilogFile*>(c_verilog_file);
  return file ? file->asCStruct() : nullptr;
}

void* verilog_convert_module_ref(void* c_module_ref)
{
  return c_module_ref ? *static_cast<void**>(c_module_ref) : nullptr;
}

}  // extern "C"
