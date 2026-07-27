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
#include "LVSInterface.hpp"
#include "tcl_ilvs.h"
#include "tcl_util.h"

namespace tcl {

namespace {

constexpr const char* kNetlistBinPath = "-netlist_bin_path";
constexpr const char* kDefBinPath = "-def_bin_path";

}  // namespace

TclReadLVS::TclReadLVS(const char* cmd_name) : TclCmd(cmd_name)
{
  addOption(new TclStringOption(kNetlistBinPath, 1, nullptr));
  addOption(new TclStringOption(kDefBinPath, 1, nullptr));
}

unsigned TclReadLVS::check()
{
  TclOption* netlist_path_option = getOptionOrArg(kNetlistBinPath);
  TclOption* def_path_option = getOptionOrArg(kDefBinPath);
  if (netlist_path_option == nullptr || netlist_path_option->getStringVal() == nullptr || def_path_option == nullptr
      || def_path_option->getStringVal() == nullptr) {
    std::cerr << "Please specify both iLVS snapshot paths by: read_lvs -netlist_bin_path <file> -def_bin_path <file>." << std::endl;
    return 0;
  }
  return 1;
}

unsigned TclReadLVS::exec()
{
  if (!check()) {
    return 0;
  }
  LVSI.readSnapshots(getOptionOrArg(kNetlistBinPath)->getStringVal(), getOptionOrArg(kDefBinPath)->getStringVal());
  return 1;
}

}  // namespace tcl
