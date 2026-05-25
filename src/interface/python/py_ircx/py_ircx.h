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

#include <string>

#include "RCXAPI.hh"

namespace python_interface {

bool init_rcx(unsigned thread_number, double temperature);
bool read_rcx_corner(const std::string& corner_name,
                     const std::string& itf_file,
                     const std::string& captab_file);
bool read_rcx_mapping(const std::string& mapping_file);

bool adapt_rcx_db();
bool build_rcx_topology();
bool build_rcx_environment();
bool build_rcx_process_variation();
bool extract_rcx_parasitics();

bool run_rcx(const std::string& config);
void report_rcx(const std::string& output_dir);

}  // namespace python_interface
