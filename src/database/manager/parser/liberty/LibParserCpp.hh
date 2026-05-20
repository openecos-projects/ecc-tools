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
 * @file LibParserCpp.hh
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief The liberty parser C++ API.
 * @version 0.1
 * @date 2023-10-12
 *
 */
#pragma once

#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <type_traits>

extern "C" {

/**
 * @brief liberty expression operation.
 *
 */
enum LibertyExprOp
{
  kBuffer,
  kNot,
  kOr,
  kAnd,
  kXor,
  kOne,
  kZero,
  kPlus,
  kMult,
};

/**
 * @brief liberty expr.
 *
 */
typedef struct LibertyExpr
{
  enum LibertyExprOp op;
  struct LibertyExpr* left;
  struct LibertyExpr* right;
  char* port_name;
} LibertyExpr;

/**
 * @brief parse expression in rust.
 *
 * @param expr_str
 * @return void*
 */
void* liberty_parse_expr(const char* expr_str);

/**
 * @brief convert expr to c expr.
 *
 * @param c_expr
 * @return struct LibertyExpr*
 */
LibertyExpr* liberty_convert_expr(void* c_expr);

/**
 * free c expr after use.
 */
void liberty_free_expr(struct LibertyExpr* c_expr);

/**
 * @brief Get the expr lef object
 *
 * @param c_expr
 * @return LibertyExpr*
 */
inline LibertyExpr* liberty_get_expr_left(LibertyExpr* c_expr)
{
  return c_expr->left ? liberty_convert_expr(c_expr->left) : nullptr;
}

/**
 * @brief Get the expr right object
 *
 * @param c_expr
 * @return LibertyExpr*
 */
inline LibertyExpr* liberty_get_expr_right(LibertyExpr* c_expr)
{
  return c_expr->right ? liberty_convert_expr(c_expr->right) : nullptr;
}

/**
 * @brief judge expr func is one.
 *
 * @param c_expr
 * @return true
 * @return false
 */
inline bool liberty_expr_func_is_one(LibertyExpr* c_expr)
{
  return c_expr->op == LibertyExprOp::kOne;
}

/**
 * @brief judge expr func is zero.
 *
 * @param c_expr
 * @return true
 * @return false
 */
inline bool liberty_expr_func_is_zero(LibertyExpr* c_expr)
{
  return c_expr->op == LibertyExprOp::kZero;
}

typedef struct LibertyVec
{
  void* data;
  uintptr_t len;
  uintptr_t cap;
  uintptr_t type_size;
} LibertyVec;

/**
 * @brief Liberty group stmt for parser data.
 *
 */
typedef struct LibertyGroupStmt
{
  char* file_name;
  uintptr_t line_no;
  char* group_name;
  struct LibertyVec attri_values;
  struct LibertyVec stmts;
} LibertyGroupStmt;

/**
 * @brief Liberty simple attribute stmt for parser data.
 *
 */
typedef struct LibertySimpleAttrStmt
{
  char* file_name;
  uintptr_t line_no;
  char* attri_name;
  const void* attri_value;
} LibertySimpleAttrStmt;

/**
 * @brief Liberty complex attribute stmt for parser data.
 *
 */
typedef struct LibertyComplexAttrStmt
{
  char* file_name;
  uintptr_t line_no;
  char* attri_name;
  struct LibertyVec attri_values;
} LibertyComplexAttrStmt;

/**
 * @brief Liberty string value for parser data.
 *
 */
typedef struct LibertyStringValue
{
  char* value;
} LibertyStringValue;

/**
 * @brief Liberty float value for parser data.
 *
 */
typedef struct LibertyFloatValue
{
  double value;
} LibertyFloatValue;

/**
 * @brief Liberty parser lib interface.
 *
 * @param lib_path
 * @return void*
 */
void* liberty_parse_lib(const char* lib_path);

/**
 * @brief Free lib group memory after building lib data.
 *
 * @param c_lib_group
 */
void liberty_free_lib_group(void* c_lib_group);

/**
 * @brief Free parser string converted to C.
 *
 * @param s
 */
void lib_free_c_char(char* s);

/**
 * @brief judge whether stmt is simple attribute stmt.
 *
 * @param lib_stmt
 * @return true
 * @return false
 */
bool liberty_is_simple_attri_stmt(void* lib_stmt);

/**
 * @brief judge whether stmt is complex attribute stmt.
 *
 * @param lib_stmt
 * @return true
 * @return false
 */
bool liberty_is_complex_attri_stmt(void* lib_stmt);

/**
 * @brief judge whether stmt is attribute stmt.
 *
 * @param lib_stmt
 * @return true
 * @return false
 */
bool liberty_is_attri_stmt(void* lib_stmt);

/**
 * @brief judge whether stmt is group stmt.
 *
 * @param lib_stmt
 * @return true
 * @return false
 */
bool liberty_is_group_stmt(void* lib_stmt);

/**
 * @brief Convert raw point group stmt to C struct, while below
 * liberty_convert_group_stmt convert dyn LibertyStmt.
 *
 * @param group_stmt
 * @return struct LibertyGroupStmt*
 */
struct LibertyGroupStmt* liberty_convert_raw_group_stmt(void* group_stmt);

/**
 * @brief Convert group stmt to C struct.
 *
 * @param group_stmt
 * @return struct LibertyGroupStmt*
 */
struct LibertyGroupStmt* liberty_convert_group_stmt(void* group_stmt);

void liberty_free_group_stmt(struct LibertyGroupStmt* c_group_stmt);

/**
 * @brief Convert simple attribute stmt to C struct.
 *
 * @param simple_attri_stmt
 * @return struct LibertySimpleAttrStmt*
 */
struct LibertySimpleAttrStmt* liberty_convert_simple_attribute_stmt(void* simple_attri_stmt);

void liberty_free_simple_attribute_stmt(struct LibertySimpleAttrStmt* c_simple_attri_stmt);

/**
 * @brief Convert complex attribute stmt to C struct.
 *
 * @param complex_attri_stmt
 * @return struct LibertyComplexAttrStmt*
 */
struct LibertyComplexAttrStmt* liberty_convert_complex_attribute_stmt(void* complex_attri_stmt);

void liberty_free_complex_attribute_stmt(struct LibertyComplexAttrStmt* c_complex_attri_stmt);

/**
 * @brief Judge whether attribute is float value.
 *
 * @param c_attribute_value
 * @return true
 * @return false
 */
bool liberty_is_float_value(void* c_attribute_value);

/**
 * @brief Judge whether attribute is string value.
 *
 * @param c_attribute_value
 * @return true
 * @return false
 */
bool liberty_is_string_value(void* c_attribute_value);

/**
 * @brief Convert string attribute to C struct.
 *
 * @param string_value
 * @return struct LibertyStringValue*
 */
struct LibertyStringValue* liberty_convert_string_value(void* string_value);

/**
 * strint value converted value should be release by the API.
 */
void liberty_free_string_value(struct LibertyStringValue* c_string_value);

/**
 * @brief Convert float attribute to C struct.
 *
 * @param float_value
 * @return struct LibertyFloatValue*
 */
struct LibertyFloatValue* liberty_convert_float_value(void* float_value);

void liberty_free_float_value(struct LibertyFloatValue* c_float_value);
}

template <typename T>
inline T* GetLibertyVecElem(LibertyVec* vec, uintptr_t index)
{
  if (!vec || !vec->data || index >= vec->len) {
    return nullptr;
  }
  auto* base = static_cast<char*>(vec->data);
  return reinterpret_cast<T*>(base + index * vec->type_size);
}

template <typename T>
class LibertyVecIterator
{
 public:
  explicit LibertyVecIterator(LibertyVec* vec) : _vec(vec) {}

  bool hasNext() { return _index < (_vec ? _vec->len : 0); }

  T* next()
  {
    return _index < (_vec ? _vec->len : 0) ? GetLibertyVecElem<T>(_vec, _index++) : nullptr;
  }

 private:
  LibertyVec* _vec = nullptr;
  uintptr_t _index = 0;
};

#define FOREACH_LIBERTY_VEC_ELEM(vec, T, elem) \
  for (LibertyVecIterator<T> elem##_iter(vec); elem##_iter.hasNext() ? (elem = elem##_iter.next(), true) : false;)

namespace ista {

class LibBuilder;

/**
 * @brief The liberty expression builder for parser function string.
 *
 */
class LibertyExprBuilder
{
 public:
  LibertyExprBuilder(const char* expr_str) : _expr_str(expr_str) {}
  ~LibertyExprBuilder() = default;

  void execute();
  LibertyExpr* get_result_expr() { return _result_expr; }

 private:
  std::string _expr_str;          //!< The expression string need to be parsed.
  LibertyExpr* _result_expr;  //!< The parsed expr result.
};

/**
 * @brief The liberty reader is used to read parser data.
 *
 */
class LibertyReader
{
 public:
  explicit LibertyReader(const char* file_name) : _file_name(file_name) {}
  ~LibertyReader() = default;

  LibertyReader(LibertyReader&& other) noexcept = default;
  LibertyReader& operator=(LibertyReader&& rhs) noexcept = default;

  void set_build_cells(std::set<std::string>& build_cells) {
    _build_cells = build_cells;
  }
  auto& get_build_cells() { return _build_cells; }
  bool isNeedBuild(std::string cell_name)
  {
    if (_build_cells.empty()) {
      return true;
    }
    return _build_cells.find(cell_name) != _build_cells.end();
  }

  unsigned visitSimpleAttri(LibertySimpleAttrStmt* attri);

  unsigned visitAxisOrValues(LibertyComplexAttrStmt* attri);
  unsigned visitComplexAttri(LibertyComplexAttrStmt* attri);

  unsigned visitLibrary(LibertyGroupStmt* group);
  unsigned visitLuTableTemplate(LibertyGroupStmt* group);
  unsigned visitWireLoad(LibertyGroupStmt* group);
  unsigned visitType(LibertyGroupStmt* group);
  unsigned visitOutputCurrentTemplate(LibertyGroupStmt* group);
  unsigned visitLeakagePower(LibertyGroupStmt* group);
  unsigned visitCell(LibertyGroupStmt* group);
  unsigned visitPin(LibertyGroupStmt* group);
  unsigned visitBus(LibertyGroupStmt* group);
  unsigned visitTiming(LibertyGroupStmt* group);
  unsigned visitInternalPower(LibertyGroupStmt* group);
  unsigned visitCurrentTable(LibertyGroupStmt* group);
  unsigned visitVector(LibertyGroupStmt* group);
  unsigned visitTable(LibertyGroupStmt* group);
  unsigned visitPowerTable(LibertyGroupStmt* group);

  unsigned visitGroup(LibertyGroupStmt* group);

  unsigned readLib();
  unsigned linkLib();

  void set_library_builder(LibBuilder* library_builder) { _library_builder = library_builder; }
  auto* get_library_builder() { return _library_builder; }

 private:
  const char* getGroupAttriName(LibertyGroupStmt* group);
  unsigned visitStmtInGroup(LibertyGroupStmt* group);

  void* _lib_file = nullptr;           //!< The parsered lib file.
  std::set<std::string> _build_cells;  //!< The needed cells.

  std::string _file_name;        //!< The liberty file name.
  LibBuilder* _library_builder;  //!< The liberty library builder.
};

}  // namespace ista
