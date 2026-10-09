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

#if 1  // iMJ

class TclInitMJ : public TclCmd
{
 public:
  explicit TclInitMJ(const char* cmd_name);
  ~TclInitMJ() override = default;

  unsigned check() override { return 1; };

  unsigned exec() override;

 private:
  std::vector<std::pair<std::string, ValueType>> _config_list;
};

class TclInsertFiller : public TclCmd
{
 public:
  explicit TclInsertFiller(const char* cmd_name);
  ~TclInsertFiller() override = default;

  unsigned check() override { return 1; };

  unsigned exec() override;

 private:
  std::vector<std::pair<std::string, ValueType>> _config_list;
};

class TclCheckAntenna : public TclCmd
{
 public:
  explicit TclCheckAntenna(const char* cmd_name);
  ~TclCheckAntenna() override = default;

  unsigned check() override { return 1; };

  unsigned exec() override;

 private:
  std::vector<std::pair<std::string, ValueType>> _config_list;
};

class TclInsertMetal : public TclCmd
{
 public:
  explicit TclInsertMetal(const char* cmd_name);
  ~TclInsertMetal() override = default;

  unsigned check() override { return 1; };

  unsigned exec() override;

 private:
  std::vector<std::pair<std::string, ValueType>> _config_list;
};

class TclFlattenDef : public TclCmd
{
 public:
  explicit TclFlattenDef(const char* cmd_name);
  ~TclFlattenDef() override = default;

  unsigned check() override { return 1; };

  unsigned exec() override;

 private:
  std::vector<std::pair<std::string, ValueType>> _config_list;
};

class TclDestroyMJ : public TclCmd
{
 public:
  explicit TclDestroyMJ(const char* cmd_name);
  ~TclDestroyMJ() override = default;

  unsigned check() override { return 1; };

  unsigned exec() override;
};

#endif

}  // namespace tcl
