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
 * @file VerilogReader.cc
 * @author longshy (longshy@pcl.ac.cn)
 * @brief The VerilogParser C++ API.
 * @version 0.1
 * @date 2023-10-30
 *
 */
#include "VerilogReader.hh"

#include <cstring>

#include "utility/logger/Logger.hpp"

namespace idb {

/**
 * @brief Read the verilog file use Verilog parser.
 *
 * @return unsigned
 */

unsigned VerilogReader::readVerilog(const char* verilog_file_path)
{
  unsigned is_ok = 1;
  ECCLOG.info(ecc::Loc::current(), "load verilog file ", verilog_file_path);
  _verilog_file_ptr = verilog_parse_file(verilog_file_path);

  if (_verilog_file_ptr) {
    ParsedVerilogFile* verilog_file = verilog_convert_file(_verilog_file_ptr);
    auto verilog_modules = verilog_file->verilog_modules;
    void* module_ref;
    FOREACH_VERILOG_VEC_ELEM(&verilog_modules, void, module_ref)
    {
      void* verilog_module_ptr = verilog_convert_module_ref(module_ref);
      ParsedVerilogModule* verilog_module = verilog_convert_module(verilog_module_ptr);
      _verilog_modules.emplace_back(verilog_module);
    }
  } else {
    is_ok = 0;
  }

  return is_ok;
}

/**
 * @brief auto set the top module without the specific module name
 * @note only support the flatten module
 */
bool VerilogReader::autoTopModule()
{
  ECCLOG.info(ecc::Loc::current(), "auto set top module ");
  if (_verilog_file_ptr == nullptr)
    return false;
  ParsedVerilogFile* verilog_file = verilog_convert_file(_verilog_file_ptr);
  auto verilog_modules = verilog_file->verilog_modules;
  if (verilog_modules.len != 1u) {
    return false;
  }

  void* module_ref;
  int count = 1;
  FOREACH_VERILOG_VEC_ELEM(&verilog_modules, void, module_ref)
  {
    if (count-- > 0) {
      void* verilog_module_ptr = verilog_convert_module_ref(module_ref);
      ParsedVerilogModule* verilog_module = verilog_convert_module(verilog_module_ptr);
      _top_module = verilog_module;
      _top_module_name = verilog_module->module_name;  // auto set the module name
    } else {
      break;
    }
  }
  return true;
}

/**
 * @brief Flatten module use Verilog parser.
 *
 * @param top_module_name
 * @return unsigned
 */
unsigned VerilogReader::flattenModule(const char* top_module_name)
{
  _top_module_name = top_module_name;
  verilog_flatten_module(_verilog_file_ptr, top_module_name);
  ParsedVerilogFile* verilog_file = verilog_convert_file(_verilog_file_ptr);

  auto verilog_modules = verilog_file->verilog_modules;
  void* module_ref;
  FOREACH_VERILOG_VEC_ELEM(&verilog_modules, void, module_ref)
  {
    void* verilog_module_ptr = verilog_convert_module_ref(module_ref);
    ParsedVerilogModule* verilog_module = verilog_convert_module(verilog_module_ptr);
    if (std::strcmp(verilog_module->module_name, top_module_name) == 0) {
      _top_module = verilog_module;
      break;
    }
  }

  return 1;
}

}  // namespace idb
