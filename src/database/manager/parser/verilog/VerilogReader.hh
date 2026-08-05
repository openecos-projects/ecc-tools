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
 * @file VerilogReader.hh
 * @author longshy (longshy@pcl.ac.cn)
 * @brief The Verilog parser C++ reader API.
 * @version 0.1
 * @date 2023-10-30
 *
 */

#pragma once

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

extern "C" {

/**
 * @brief A stable view of parser-owned vector storage.
 */
typedef struct VerilogVec
{
  void* data;
  uintptr_t len;
  uintptr_t cap;
  uintptr_t type_size;
} VerilogVec;

}

template <typename T>
class VerilogVecIterator
{
 public:
  explicit VerilogVecIterator(VerilogVec* verilog_vec) : _verilog_vec(verilog_vec) {}
  ~VerilogVecIterator() = default;

  bool hasNext() { return _index < _verilog_vec->len; }
  T* next();

 private:
  VerilogVec* _verilog_vec;
  uintptr_t _index = 0;
};

template <typename T>
inline T* VerilogVecIterator<T>::next()
{
  uintptr_t ptr_move = _index;
  auto* ret_value = static_cast<T*>(_verilog_vec->data) + ptr_move;

  ++_index;
  return ret_value;
}

template <>
inline void* VerilogVecIterator<void>::next()
{
  char* base_ptr = static_cast<char*>(_verilog_vec->data);
  void* ret_value = static_cast<void*>(base_ptr + _index * _verilog_vec->type_size);

  ++_index;
  return ret_value;
}

#define FOREACH_VERILOG_VEC_ELEM(vec, T, elem) \
  for (VerilogVecIterator<T> iter(vec); iter.hasNext() ? elem = iter.next(), true : false;)

template <typename T>
inline T* GetVerilogVecElem(VerilogVec* verilog_vec, uintptr_t index)
{
  uintptr_t ptr_move = index;
  auto* ret_value = static_cast<T*>(verilog_vec->data) + ptr_move;
  return ret_value;
}

template <>
inline void* GetVerilogVecElem<void>(VerilogVec* verilog_vec, uintptr_t index)
{
  char* base_ptr = static_cast<char*>(verilog_vec->data);
  void* ret_value = static_cast<void*>(base_ptr + index * verilog_vec->type_size);
  return ret_value;
}

extern "C" {

/**
 * The wire or port declaration.
 */
typedef enum DclType
{
  KInput = 0,
  KInout = 1,
  KOutput = 2,
  KSupply0 = 3,
  KSupply1 = 4,
  KTri = 5,
  KWand = 6,
  KWire = 7,
  KWor = 8,
} DclType;

/**
 * @brief Verilog id.
 *
 */
typedef struct ParsedVerilogID
{
  char* id;
} ParsedVerilogID;

/**
 * @brief Verilog indexed id.
 *
 */
typedef struct ParsedVerilogIndexID
{
  char* id;
  char* base_id;
  int32_t index;
} ParsedVerilogIndexID;

/**
 * @brief Verilog slice id.
 *
 */
typedef struct ParsedVerilogSliceID
{
  char* id;
  char* base_id;
  int32_t range_base;
  int32_t range_max;
} ParsedVerilogSliceID;

typedef struct ParsedVerilogNetIDExpr
{
  uintptr_t line_no;
  const void* verilog_id;
} ParsedVerilogNetIDExpr;

typedef struct ParsedVerilogNetConcatExpr
{
  uintptr_t line_no;
  struct VerilogVec verilog_id_concat;
} ParsedVerilogNetConcatExpr;

typedef struct ParsedVerilogConstantExpr
{
  uintptr_t line_no;
  const void* verilog_id;
} ParsedVerilogConstantExpr;

/**
 * @brief Parsed Verilog module.
 *
 */
typedef struct ParsedVerilogModule
{
  uintptr_t line_no;
  char* module_name;
  struct VerilogVec port_list;
  struct VerilogVec module_stmts;
} ParsedVerilogModule;

typedef struct CRange
{
  bool has_value;
  int32_t start;
  int32_t end;
} CRange;

typedef struct ParsedVerilogDcl
{
  uintptr_t line_no;
  enum DclType dcl_type;
  char* dcl_name;
  struct CRange range;
} ParsedVerilogDcl;

/**
 * @brief Parsed Verilog declarations statement.
 *
 */
typedef struct ParsedVerilogDcls
{
  uintptr_t line_no;
  struct VerilogVec verilog_dcls;
} ParsedVerilogDcls;

/**
 * @brief Parsed Verilog instance.
 *
 */
typedef struct ParsedVerilogInst
{
  uintptr_t line_no;
  char* inst_name;
  char* cell_name;
  struct VerilogVec port_connections;
} ParsedVerilogInst;

/**
 * @brief Parsed Verilog assign.
 *
 */
typedef struct ParsedVerilogAssign
{
  uintptr_t line_no;
  const void* left_net_expr;
  const void* right_net_expr;
} ParsedVerilogAssign;

/**
 * @brief Parsed Verilog named port connection.
 *
 */
typedef struct ParsedVerilogPortRefPortConnect
{
  const void* port_id;
  void* net_expr;
} ParsedVerilogPortRefPortConnect;

typedef struct ParsedVerilogFile
{
  struct VerilogVec verilog_modules;
} ParsedVerilogFile;

/**
 * @brief Parse a Verilog file.
 *
 * @param verilog_path
 * @return void*
 */
void* verilog_parse_file(const char* verilog_path);

/**
 * @brief Flatten a module in a parsed Verilog file.
 *
 * @param c_verilog_file
 * @param top_module_name
 */
void verilog_flatten_module(void* c_verilog_file, const char* top_module_name);

/**
 * @brief Free a parsed Verilog file.
 *
 * @param c_verilog_file
 */
void verilog_free_file(void* c_verilog_file);

uintptr_t verilog_vec_len(const struct VerilogVec* vec);
/**
 * @brief Free a parser-owned C string.
 *
 * @param s
 */
void verilog_free_c_char(char* s);

struct ParsedVerilogID* verilog_convert_id(void* c_verilog_virtual_base_id);

bool verilog_is_id(void* c_verilog_virtual_base_id);

struct ParsedVerilogIndexID* verilog_convert_index_id(void* c_verilog_virtual_base_id);

bool verilog_is_bus_index_id(void* c_verilog_virtual_base_id);

const char* verilog_get_index_name(struct ParsedVerilogSliceID* verilog_slice_id, uintptr_t index);

struct ParsedVerilogSliceID* verilog_convert_slice_id(void* c_verilog_virtual_base_id);

bool verilog_is_bus_slice_id(void* c_verilog_virtual_base_id);

struct ParsedVerilogNetIDExpr* verilog_convert_net_id_expr(void* c_verilog_net_id_expr);

struct ParsedVerilogNetConcatExpr* verilog_convert_net_concat_expr(void* c_verilog_net_concat_expr);

struct ParsedVerilogConstantExpr* verilog_convert_constant_expr(void* c_verilog_constant_expr);

bool verilog_is_id_expr(void* c_verilog_virtual_base_net_expr);

bool verilog_is_concat_expr(void* c_verilog_virtual_base_net_expr);

bool verilog_is_constant(void* c_verilog_virtual_base_net_expr);

///////////////////////////////////////////////////////////////////////////
/**
 * @brief Convert a raw parsed module pointer to a public module view.
 *
 * @param verilog_module
 * @return struct ParsedVerilogModule*
 */
struct ParsedVerilogModule* verilog_convert_module(void* verilog_module);

struct ParsedVerilogDcl* verilog_convert_dcl(void* c_verilog_dcl_struct);

/**
 * @brief Convert a declaration statement to a public declaration statement view.
 *
 * @param verilog_dcls_struct
 * @return struct ParsedVerilogDcls*
 */
struct ParsedVerilogDcls* verilog_convert_dcls(void* verilog_dcls_struct);

/**
 * @brief Convert an instance statement to a public instance view.
 *
 * @param verilog_inst
 * @return struct ParsedVerilogInst*
 */
struct ParsedVerilogInst* verilog_convert_inst(void* verilog_inst);

/**
 * @brief Convert an assign statement to a public assign view.
 *
 * @param c_verilog_assign
 * @return struct ParsedVerilogAssign*
 */
struct ParsedVerilogAssign* verilog_convert_assign(void* c_verilog_assign);

/**
 * @brief Convert a named port connection to a public connection view.
 *
 * @param c_port_connect
 * @return struct ParsedVerilogPortRefPortConnect*
 */
struct ParsedVerilogPortRefPortConnect* verilog_convert_port_ref_port_connect(void* c_port_connect);

/**
 * @brief judge whether stmt is module inst stmt.
 *
 * @param c_verilog_stmt
 * @return true
 * @return false
 */
bool verilog_is_module_inst_stmt(void* c_verilog_stmt);

/**
 * @brief judge whether stmt is module assign stmt.
 *
 * @param c_verilog_stmt
 * @return true
 * @return false
 */
bool verilog_is_module_assign_stmt(void* c_verilog_stmt);

/**
 * @brief judge whether stmt is verilog_dcl stmt.
 *
 * @param c_verilog_stmt
 * @return true
 * @return false
 */
bool verilog_is_dcl_stmt(void* c_verilog_stmt);

/**
 * @brief judge whether stmt is verilog_dcls stmt.
 *
 * @param c_verilog_stmt
 * @return true
 * @return false
 */
bool verilog_is_dcls_stmt(void* c_verilog_stmt);

/**
 * @brief judge whether stmt is module stmt.
 *
 * @param c_verilog_stmt
 * @return true
 * @return false
 */
bool verilog_is_module_stmt(void* c_verilog_stmt);

/**
 * @brief Convert a parsed file to a public file view.
 *
 * @param c_verilog_file
 * @return struct ParsedVerilogFile*
 */
struct ParsedVerilogFile* verilog_convert_file(void* c_verilog_file);

void* verilog_convert_module_ref(void* c_module_ref);
}

namespace idb {

class VerilogReader
{
 public:
  explicit VerilogReader() = default;
  ~VerilogReader() = default;

  VerilogReader(VerilogReader&& other) noexcept = default;
  VerilogReader& operator=(VerilogReader&& rhs) noexcept = default;

  void* get_verilog_file_ptr() { return _verilog_file_ptr; }
  auto* get_top_module() { return _top_module; }
  auto& get_verilog_modules() { return _verilog_modules; }

  unsigned readVerilog(const char* verilog_file);
  bool autoTopModule();
  unsigned flattenModule(const char* top_module_name);

 private:
  void* _verilog_file_ptr = nullptr;  //!< The parsed Verilog file handle.
  std::string _top_module_name;
  std::vector<ParsedVerilogModule*> _verilog_modules;  //!< The current design parsed from verilog file.
  ParsedVerilogModule* _top_module = nullptr;          //!< The design top module.
};
}  // namespace idb
