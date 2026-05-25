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
#include "py_ircx.h"

#include "RCXAPI.hh"

namespace python_interface {

bool init_rcx(unsigned thread_number, double temperature)
{
  ircx::RCXInitOptions options;
  options.thread_number = thread_number == 0 ? 1U : thread_number;
  options.operating_temperature = temperature;
  RCX_API_INST.init(options);
  return true;
}

bool read_rcx_corner(const std::string& corner_name,
                     const std::string& itf_file,
                     const std::string& captab_file)
{
  return RCX_API_INST.readCorner(corner_name, itf_file.c_str(), captab_file.c_str());
}

bool read_rcx_mapping(const std::string& mapping_file)
{
  return RCX_API_INST.readMapping(mapping_file.c_str());
}

bool adapt_rcx_db()
{
  return RCX_API_INST.adaptDB();
}

bool build_rcx_topology()
{
  return RCX_API_INST.buildTopology();
}

bool build_rcx_environment()
{
  return RCX_API_INST.buildEnvironment();
}

bool build_rcx_process_variation()
{
  return RCX_API_INST.buildProcessVariation();
}

bool extract_rcx_parasitics()
{
  return RCX_API_INST.extractParasitics();
}

bool run_rcx(const std::string& config)
{
  RCX_API_INST.init(config);
  RCX_API_INST.runRCX();
  return RCX_API_INST.runSuccess();
}

void report_rcx(const std::string& output_dir)
{
  RCX_API_INST.report(output_dir);
}

}  // namespace python_interface
