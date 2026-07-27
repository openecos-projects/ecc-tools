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

#include "tcl_util.h"

namespace tcl {

#if 1  // rcx

class TclInitRCX : public TclCmd
{
 public:
  explicit TclInitRCX(const char* cmd_name);
  ~TclInitRCX() override = default;

  unsigned check() override;

  unsigned exec() override;

 private:
  std::vector<std::pair<std::string, ValueType>> _config_list;
};

class TclRunRCX : public TclCmd
{
 public:
  explicit TclRunRCX(const char* cmd_name);
  ~TclRunRCX() override = default;

  unsigned check() override { return 1; };

  unsigned exec() override;

 private:
  std::vector<std::pair<std::string, ValueType>> _config_list;
};

class TclDestroyRCX : public TclCmd
{
 public:
  explicit TclDestroyRCX(const char* cmd_name);
  ~TclDestroyRCX() override = default;

  unsigned check() override { return 1; };

  unsigned exec() override;

 private:
  std::vector<std::pair<std::string, ValueType>> _config_list;
};

#endif

#if 1  // aux

class TclCompareSpef : public TclCmd
{
 public:
  explicit TclCompareSpef(const char* cmd_name);
  ~TclCompareSpef() override = default;

  unsigned check() override { return 1; };

  unsigned exec() override;

 private:
  std::vector<std::pair<std::string, ValueType>> _config_list;
};

class TclDumpNetShape : public TclCmd
{
 public:
  explicit TclDumpNetShape(const char* cmd_name);
  ~TclDumpNetShape() override = default;

  unsigned check() override { return 1; };

  unsigned exec() override;

 private:
  std::vector<std::pair<std::string, ValueType>> _config_list;
};

class TclRunRCXFromTopology : public TclCmd
{
 public:
  explicit TclRunRCXFromTopology(const char* cmd_name);
  ~TclRunRCXFromTopology() override = default;

  unsigned check() override { return 1; };

  unsigned exec() override;

 private:
  std::vector<std::pair<std::string, ValueType>> _config_list;
};

class TclPlotSpef : public TclCmd
{
 public:
  explicit TclPlotSpef(const char* cmd_name);
  ~TclPlotSpef() override = default;

  unsigned check() override { return 1; };

  unsigned exec() override;

 private:
  std::vector<std::pair<std::string, ValueType>> _config_list;
};

#endif

}  // namespace tcl
