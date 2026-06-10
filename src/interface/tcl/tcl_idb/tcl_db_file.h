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
 * @File Name: tcl_db.h
 * @Brief :
 * @Author : Yell (12112088@qq.com)
 * @Version : 1.0
 * @Creat Date : 2022-04-15
 *
 */
#include <iostream>
#include <string>

#include "ScriptEngine.hh"
#include "tcl_definition.h"

using ieda::TclCmd;
using ieda::TclIntOption;
using ieda::TclOption;
using ieda::TclStringListOption;
using ieda::TclStringOption;
using ieda::TclSwitchOption;

namespace tcl {

class CmdInitIdb : public TclCmd
{
 public:
  explicit CmdInitIdb(const char* cmd_name);
  ~CmdInitIdb() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdInitTechLef : public TclCmd
{
 public:
  explicit CmdInitTechLef(const char* cmd_name);
  ~CmdInitTechLef() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdInitLef : public TclCmd
{
 public:
  explicit CmdInitLef(const char* cmd_name);
  ~CmdInitLef() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdInitDef : public TclCmd
{
 public:
  explicit CmdInitDef(const char* cmd_name);
  ~CmdInitDef() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdInitVerilog : public TclCmd
{
 public:
  explicit CmdInitVerilog(const char* cmd_name);
  ~CmdInitVerilog() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdSaveDef : public TclCmd
{
 public:
  explicit CmdSaveDef(const char* cmd_name);
  ~CmdSaveDef() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdSaveLef : public TclCmd
{
 public:
  explicit CmdSaveLef(const char* cmd_name);
  ~CmdSaveLef() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdSaveNetlist : public TclCmd
{
 public:
  explicit CmdSaveNetlist(const char* cmd_name);
  ~CmdSaveNetlist() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdSaveGDS : public TclCmd
{
 public:
  explicit CmdSaveGDS(const char* cmd_name);
  ~CmdSaveGDS() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdGenerateMPScript : public TclCmd
{
 public:
  explicit CmdGenerateMPScript(const char* cmd_name);
  ~CmdGenerateMPScript() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdSaveJSON : public TclCmd
{
 public:
  explicit CmdSaveJSON(const char* cmd_name);
  ~CmdSaveJSON() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdSaveViewJson : public TclCmd
{
 public:
  explicit CmdSaveViewJson(const char* cmd_name);
  ~CmdSaveViewJson() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdApplyViewJsonEdits : public TclCmd
{
 public:
  explicit CmdApplyViewJsonEdits(const char* cmd_name);
  ~CmdApplyViewJsonEdits() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdSaveData : public TclCmd
{
 public:
  explicit CmdSaveData(const char* cmd_name);
  ~CmdSaveData() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdLoadData : public TclCmd
{
 public:
  explicit CmdLoadData(const char* cmd_name);
  ~CmdLoadData() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdResetData : public TclCmd
{
 public:
  explicit CmdResetData(const char* cmd_name);
  ~CmdResetData() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdValidateIdb : public TclCmd
{
 public:
  explicit CmdValidateIdb(const char* cmd_name);
  ~CmdValidateIdb() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdWriteSocJson : public TclCmd
{
 public:
  explicit CmdWriteSocJson(const char* cmd_name);
  ~CmdWriteSocJson() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

class CmdWriteAbstractLef : public TclCmd
{
 public:
  explicit CmdWriteAbstractLef(const char* cmd_name);
  ~CmdWriteAbstractLef() override = default;

  unsigned check() override;
  unsigned exec() override;

 private:
  // private function
  // private data
};

}  // namespace tcl
