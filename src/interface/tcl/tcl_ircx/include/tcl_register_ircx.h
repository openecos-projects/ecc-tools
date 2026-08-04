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

#include "tcl_rcx.h"

using namespace ecc;

namespace tcl {

int registerCmdRCX()
{
  // rcx
  registerTclCmd(TclInitRCX, "init_rcx");
  registerTclCmd(TclRunRCX, "run_rcx");
  registerTclCmd(TclDestroyRCX, "destroy_rcx");
  // aux
  registerTclCmd(TclCompareSpef, "compare_spef");
  registerTclCmd(TclDumpNetShape, "dump_net_shape");
  registerTclCmd(TclRunRCXFromTopology, "run_rcx_from_topology");
  registerTclCmd(TclPlotSpef, "plot_spef");
  return EXIT_SUCCESS;
}

}  // namespace tcl
