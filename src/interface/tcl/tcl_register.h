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
/**
 * @File Name: tcl_register.h
 * @Brief :
 * @Author : Yell (12112088@qq.com)
 * @Version : 1.0
 * @Creat Date : 2022-04-15
 *
 */
#include "ScriptEngine.hh"
#include "UserShell.hh"

#include "tcl_flow.h"
#include "tcl_register_config.h"
#include "tcl_register_cts.h"
#include "tcl_register_feature.h"
#include "tcl_register_flow.h"
#include "tcl_register_geometry.h"
#include "tcl_register_idb.h"
#include "tcl_register_idrc.h"
#include "tcl_register_irt.h"
#include "tcl_register_ilvs.h"
#include "tcl_register_ista.h"
#include "tcl_register_ifp.h"
#include "tcl_register_iemir.h"
#include "tcl_register_izh.h"
#include "tcl_register_ircx.h"
#include "tcl_register_report.h"

using namespace ecc;
namespace tcl {

int registerCommands()
{
  /// config
  registerConfig();

  /// flow
  registerCmdFlow();

  /// db
  registerCmdDB();

  /// CTS
  registerCmdCTS();

  /// Router
  registerCmdRT();

  /// LVS
  registerCmdLVS();

  /// STA
  registerCmdSTA();

  /// FP
  registerCmdFP();

  /// EMIR
  registerCmdEMIR();

  /// DRC
  registerCmdDRC();

  /// RCX
  registerCmdRCX();

  /// ZH
  registerCmdZH();

  registerCmdReport();

  registerCmdGeometry();
  return EXIT_SUCCESS;
}

}  // namespace tcl
