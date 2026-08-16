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
/**
 * @file LibParserCppC.cc
 * @brief C++ Liberty parser API implementation.
 */

#include "LibParserCpp.hh"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "CppLibertyAST.hh"
#include "CppLibertyDriver.hh"

namespace {

enum class ExprStorage
{
  kOwner,
  kView
};

constexpr std::uint64_t kCppExprHandleMagic = 0x45434f534c494258ULL;

struct CppExprNode
{
  explicit CppExprNode(LibertyExprOp node_op) : op(node_op) {}

  LibertyExprOp op;
  std::string port_name;
  std::unique_ptr<CppExprNode> left;
  std::unique_ptr<CppExprNode> right;
};

struct CppRawExpr
{
  explicit CppRawExpr(std::unique_ptr<CppExprNode> expr) : ast_node(expr.get()), ast(std::move(expr)) {}

  std::uint64_t magic = kCppExprHandleMagic;
  CppExprNode* ast_node = nullptr;
  std::unique_ptr<CppExprNode> ast;
  bool owner = true;
};

struct CppLibertyExpr : LibertyExpr
{
  CppLibertyExpr(CppExprNode* node, ExprStorage storage_type) : ast_node(node), storage(storage_type)
  {
    op = node ? node->op : LibertyExprOp::kBuffer;
    if (node && node->left) {
      left_handle = std::make_unique<CppRawExpr>(nullptr);
      left_handle->owner = false;
      left_handle->ast_node = node->left.get();
      left = reinterpret_cast<LibertyExpr*>(left_handle.get());
    } else {
      left = nullptr;
    }

    if (node && node->right) {
      right_handle = std::make_unique<CppRawExpr>(nullptr);
      right_handle->owner = false;
      right_handle->ast_node = node->right.get();
      right = reinterpret_cast<LibertyExpr*>(right_handle.get());
    } else {
      right = nullptr;
    }
    port_name = node && !node->port_name.empty() ? const_cast<char*>(node->port_name.c_str()) : nullptr;
  }

  std::unique_ptr<CppExprNode> owned_ast;
  std::unique_ptr<CppRawExpr> left_handle;
  std::unique_ptr<CppRawExpr> right_handle;
  CppExprNode* ast_node = nullptr;
  ExprStorage storage = ExprStorage::kView;
};

struct CppLibertyStringValue : LibertyStringValue
{
  explicit CppLibertyStringValue(std::string value_str) : storage(std::move(value_str)) { value = storage.data(); }
  std::string storage;
};

struct CppLibertyFloatValue : LibertyFloatValue
{
  explicit CppLibertyFloatValue(double float_value) { value = float_value; }
};

struct CppLibertyVecStorage
{
  std::vector<void*> ptrs;

  LibertyVec view()
  {
    return LibertyVec{ptrs.data(), ptrs.size(), ptrs.capacity(), sizeof(void*)};
  }
};

struct CppLibertyGroupStmt : LibertyGroupStmt
{
  CppLibertyGroupStmt(liberty::LibGroup* group, bool raw_group) : source(group), raw(raw_group)
  {
    file_name = group ? const_cast<char*>(group->getSourceFile()) : nullptr;
    line_no = group ? static_cast<uintptr_t>(group->getSourceLine()) : 0;
    group_name = group ? const_cast<char*>(group->getGroupType()) : nullptr;

    if (group) {
      if (auto* params = group->getParams()) {
        attr_values_storage.ptrs.reserve(params->size());
        for (auto& value : *params) {
          attr_values_storage.ptrs.push_back(value.get());
        }
      }

      auto& statements = group->getStatements();
      stmts_storage.ptrs.reserve(statements.size());
      for (auto* stmt : statements) {
        stmts_storage.ptrs.push_back(stmt);
      }
    }

    attri_values = attr_values_storage.view();
    stmts = stmts_storage.view();
  }

  liberty::LibGroup* source = nullptr;
  bool raw = false;
  CppLibertyVecStorage attr_values_storage;
  CppLibertyVecStorage stmts_storage;
};

struct CppLibertySimpleAttrStmt : LibertySimpleAttrStmt
{
  explicit CppLibertySimpleAttrStmt(liberty::LibSimpleAttribute* attr) : source(attr)
  {
    file_name = attr ? const_cast<char*>(attr->getSourceFile()) : nullptr;
    line_no = attr ? static_cast<uintptr_t>(attr->getSourceLine()) : 0;
    attri_name = attr ? const_cast<char*>(attr->getName()) : nullptr;
    value_slot = attr ? attr->getFirstValue() : nullptr;
    attri_value = &value_slot;
  }

  explicit CppLibertySimpleAttrStmt(liberty::LibVarDecl* var) : source_var(var), var_value(var ? var->getValue() : 0.0)
  {
    file_name = var ? const_cast<char*>(var->getSourceFile()) : nullptr;
    line_no = var ? static_cast<uintptr_t>(var->getSourceLine()) : 0;
    attri_name = var ? const_cast<char*>(var->getVarName()) : nullptr;
    value_slot = &var_value;
    attri_value = &value_slot;
  }

  liberty::LibSimpleAttribute* source = nullptr;
  liberty::LibVarDecl* source_var = nullptr;
  void* value_slot = nullptr;
  liberty::LibFloatValue var_value{0.0};
};

struct CppLibertyComplexAttrStmt : LibertyComplexAttrStmt
{
  explicit CppLibertyComplexAttrStmt(liberty::LibComplexAttribute* attr) : source(attr)
  {
    file_name = attr ? const_cast<char*>(attr->getSourceFile()) : nullptr;
    line_no = attr ? static_cast<uintptr_t>(attr->getSourceLine()) : 0;
    attri_name = attr ? const_cast<char*>(attr->getName()) : nullptr;

    if (attr) {
      auto* values = attr->getAllValues();
      if (values) {
        attr_values_storage.ptrs.reserve(values->size());
        for (auto& value : *values) {
          attr_values_storage.ptrs.push_back(value.get());
        }
      }
    }
    attri_values = attr_values_storage.view();
  }

  liberty::LibComplexAttribute* source = nullptr;
  CppLibertyVecStorage attr_values_storage;
};

class ExprParser
{
 public:
  explicit ExprParser(std::string_view expr) : _expr(expr) {}

  std::unique_ptr<CppExprNode> parse()
  {
    auto expr = parseExpr();
    skipSpaces();
    if (peek() == ';') {
      consume();
    }
    return expr;
  }

 private:
  std::unique_ptr<CppExprNode> parseExpr()
  {
    auto left = parseDefaultAndExpr();
    while (true) {
      skipSpaces();
      LibertyExprOp op;
      if (peek() == '+') {
        op = LibertyExprOp::kPlus;
      } else if (peek() == '|') {
        op = LibertyExprOp::kOr;
      } else if (peek() == '*') {
        op = LibertyExprOp::kMult;
      } else if (peek() == '&') {
        op = LibertyExprOp::kAnd;
      } else if (peek() == '^') {
        op = LibertyExprOp::kXor;
      } else {
        break;
      }
      consume();
      auto node = std::make_unique<CppExprNode>(op);
      node->left = std::move(left);
      node->right = parseDefaultAndExpr();
      left = std::move(node);
    }
    return left;
  }

  std::unique_ptr<CppExprNode> parseDefaultAndExpr()
  {
    auto left = parseUnary();
    while (true) {
      skipSpaces();
      const char current = peek();
      if (startsUnary(current)) {
        auto node = std::make_unique<CppExprNode>(LibertyExprOp::kAnd);
        node->left = std::move(left);
        node->right = parseUnary();
        left = std::move(node);
        continue;
      }
      break;
    }
    return left;
  }

  std::unique_ptr<CppExprNode> parseUnary()
  {
    skipSpaces();
    if (peek() == '!') {
      consume();
      auto node = std::make_unique<CppExprNode>(LibertyExprOp::kNot);
      node->left = parseUnary();
      return node;
    }

    auto node = parsePrimary();
    skipSpaces();
    while (peek() == '\'') {
      consume();
      auto not_node = std::make_unique<CppExprNode>(LibertyExprOp::kNot);
      not_node->left = std::move(node);
      node = std::move(not_node);
      skipSpaces();
    }
    return node;
  }

  std::unique_ptr<CppExprNode> parsePrimary()
  {
    skipSpaces();
    if (peek() == '(') {
      consume();
      auto node = parseExpr();
      skipSpaces();
      if (peek() == ')') {
        consume();
      }
      return node;
    }

    std::string token = parseToken();
    if (token == "1") {
      return std::make_unique<CppExprNode>(LibertyExprOp::kOne);
    }
    if (token == "0") {
      return std::make_unique<CppExprNode>(LibertyExprOp::kZero);
    }

    auto node = std::make_unique<CppExprNode>(LibertyExprOp::kBuffer);
    node->port_name = std::move(token);
    return node;
  }

  std::string parseToken()
  {
    skipSpaces();
    std::string token;
    while (_pos < _expr.size()) {
      const char c = _expr[_pos];
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '[' || c == ']' || c == ':' || c == '.') {
        token.push_back(c);
        ++_pos;
      } else {
        break;
      }
    }
    return token;
  }

  bool startsUnary(char c) const
  {
    return c == '!' || c == '(' || c == '_' || std::isalnum(static_cast<unsigned char>(c));
  }

  char peek() const
  {
    return _pos < _expr.size() ? _expr[_pos] : '\0';
  }

  void consume() { ++_pos; }

  void skipSpaces()
  {
    while (_pos < _expr.size() && std::isspace(static_cast<unsigned char>(_expr[_pos]))) {
      ++_pos;
    }
  }

  std::string_view _expr;
  std::size_t _pos = 0;
};

template <typename T>
T* parserSlotValue(void* parser_slot)
{
  return parser_slot ? reinterpret_cast<T*>(*reinterpret_cast<void**>(parser_slot)) : nullptr;
}

}  // namespace

extern "C" {

void* liberty_parse_expr(const char* expr_str)
{
  if (!expr_str) {
    return nullptr;
  }

  ExprParser parser(expr_str);
  auto expr = parser.parse();
  if (!expr) {
    return nullptr;
  }

  return new CppRawExpr(std::move(expr));
}

LibertyExpr* liberty_convert_expr(void* c_expr)
{
  if (!c_expr) {
    return nullptr;
  }

  CppExprNode* node = nullptr;
  auto* raw_expr = reinterpret_cast<CppRawExpr*>(c_expr);
  if (raw_expr->magic != kCppExprHandleMagic) {
    return nullptr;
  }

  node = raw_expr->owner ? raw_expr->ast.get() : raw_expr->ast_node;
  const ExprStorage storage = raw_expr->owner ? ExprStorage::kOwner : ExprStorage::kView;
  auto* expr = new CppLibertyExpr(node, storage);
  if (storage == ExprStorage::kOwner) {
    expr->owned_ast = std::move(raw_expr->ast);
    expr->ast_node = expr->owned_ast.get();
    delete raw_expr;
  }

  return expr;
}

void liberty_free_expr(LibertyExpr* c_expr)
{
  delete reinterpret_cast<CppLibertyExpr*>(c_expr);
}

void* liberty_parse_lib(const char* lib_path)
{
  auto driver = std::make_unique<liberty::LibertyDriver>();
  if (!driver->parse(lib_path)) {
    return nullptr;
  }

  auto* result = driver->getParseResult();
  if (!result) {
    return nullptr;
  }

  return driver.release();
}

void liberty_free_lib_group(void* c_lib_group)
{
  delete reinterpret_cast<liberty::LibertyDriver*>(c_lib_group);
}

void lib_free_c_char(char*) {}

bool liberty_is_simple_attri_stmt(void* lib_stmt)
{
  auto* node = parserSlotValue<liberty::LibNode>(lib_stmt);
  return node && (node->isSimpleAttr() || node->isVariable());
}

bool liberty_is_complex_attri_stmt(void* lib_stmt)
{
  auto* node = parserSlotValue<liberty::LibNode>(lib_stmt);
  return node && node->isComplexAttr();
}

bool liberty_is_attri_stmt(void* lib_stmt)
{
  auto* node = parserSlotValue<liberty::LibNode>(lib_stmt);
  return node && (node->isSimpleAttr() || node->isComplexAttr() || node->isVariable());
}

bool liberty_is_group_stmt(void* lib_stmt)
{
  auto* node = parserSlotValue<liberty::LibNode>(lib_stmt);
  return node && node->isGroup();
}

LibertyGroupStmt* liberty_convert_raw_group_stmt(void* group_stmt)
{
  auto* driver = reinterpret_cast<liberty::LibertyDriver*>(group_stmt);
  return driver ? new CppLibertyGroupStmt(driver->getParseResult(), true) : nullptr;
}

LibertyGroupStmt* liberty_convert_group_stmt(void* group_stmt)
{
  auto* group = parserSlotValue<liberty::LibGroup>(group_stmt);
  return group ? new CppLibertyGroupStmt(group, false) : nullptr;
}

void liberty_free_group_stmt(LibertyGroupStmt* c_group_stmt)
{
  delete reinterpret_cast<CppLibertyGroupStmt*>(c_group_stmt);
}

LibertySimpleAttrStmt* liberty_convert_simple_attribute_stmt(void* simple_attri_stmt)
{
  auto* node = parserSlotValue<liberty::LibNode>(simple_attri_stmt);
  if (!node) {
    return nullptr;
  }
  if (node->isVariable()) {
    return new CppLibertySimpleAttrStmt(reinterpret_cast<liberty::LibVarDecl*>(node));
  }
  return new CppLibertySimpleAttrStmt(reinterpret_cast<liberty::LibSimpleAttribute*>(node));
}

void liberty_free_simple_attribute_stmt(LibertySimpleAttrStmt* c_simple_attri_stmt)
{
  delete reinterpret_cast<CppLibertySimpleAttrStmt*>(c_simple_attri_stmt);
}

LibertyComplexAttrStmt* liberty_convert_complex_attribute_stmt(void* complex_attri_stmt)
{
  auto* attr = parserSlotValue<liberty::LibComplexAttribute>(complex_attri_stmt);
  return attr ? new CppLibertyComplexAttrStmt(attr) : nullptr;
}

void liberty_free_complex_attribute_stmt(LibertyComplexAttrStmt* c_complex_attri_stmt)
{
  delete reinterpret_cast<CppLibertyComplexAttrStmt*>(c_complex_attri_stmt);
}

bool liberty_is_float_value(void* c_attribute_value)
{
  auto* value = parserSlotValue<liberty::LibValue>(c_attribute_value);
  return value && value->isFloat();
}

bool liberty_is_string_value(void* c_attribute_value)
{
  auto* value = parserSlotValue<liberty::LibValue>(c_attribute_value);
  return value && value->isString();
}

LibertyStringValue* liberty_convert_string_value(void* string_value)
{
  auto* value = parserSlotValue<liberty::LibValue>(string_value);
  return value ? new CppLibertyStringValue(value->asString()) : nullptr;
}

void liberty_free_string_value(LibertyStringValue* c_string_value)
{
  delete reinterpret_cast<CppLibertyStringValue*>(c_string_value);
}

LibertyFloatValue* liberty_convert_float_value(void* float_value)
{
  auto* value = parserSlotValue<liberty::LibValue>(float_value);
  return value ? new CppLibertyFloatValue(value->asFloat()) : nullptr;
}

void liberty_free_float_value(LibertyFloatValue* c_float_value)
{
  delete reinterpret_cast<CppLibertyFloatValue*>(c_float_value);
}

}  // extern "C"
