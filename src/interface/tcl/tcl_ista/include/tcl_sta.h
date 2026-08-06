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

#if 1  // sta

class TclInitSTA : public TclCmd
{
 public:
  explicit TclInitSTA(const char* cmd_name);
  ~TclInitSTA() override = default;

  unsigned check() override { return 1; }

  unsigned exec() override;

 private:
  std::vector<std::pair<std::string, ValueType>> _config_list;
};

class TclRunSTA : public TclCmd
{
 public:
  explicit TclRunSTA(const char* cmd_name);
  ~TclRunSTA() override = default;

  unsigned check() override { return 1; }

  unsigned exec() override;

 private:
  std::vector<std::pair<std::string, ValueType>> _config_list;
};

class TclRemoveWireLoadModel : public TclCmd
{
 public:
  explicit TclRemoveWireLoadModel(const char* cmd_name);
  ~TclRemoveWireLoadModel() override = default;

  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclUpdateTiming : public TclCmd
{
 public:
  explicit TclUpdateTiming(const char* cmd_name);
  ~TclUpdateTiming() override = default;

  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclWriteSDF : public TclCmd
{
 public:
  explicit TclWriteSDF(const char* cmd_name);
  ~TclWriteSDF() override = default;

  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclReportTiming : public TclCmd
{
 public:
  explicit TclReportTiming(const char* cmd_name);
  ~TclReportTiming() override = default;

  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclCreateClock : public TclCmd
{
 public:
  explicit TclCreateClock(const char* cmd_name);
  ~TclCreateClock() override = default;

  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetPropagatedClock : public TclCmd
{
 public:
  explicit TclSetPropagatedClock(const char* cmd_name);
  ~TclSetPropagatedClock() override = default;

  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclGetPorts : public TclCmd
{
 public:
  explicit TclGetPorts(const char* cmd_name);
  ~TclGetPorts() override = default;

  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclGetClocks : public TclCmd
{
 public:
  explicit TclGetClocks(const char* cmd_name);
  ~TclGetClocks() override = default;

  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclExtractLib : public TclCmd
{
 public:
  explicit TclExtractLib(const char* cmd_name);
  ~TclExtractLib() override = default;

  unsigned check() override { return 1; }

  unsigned exec() override;

 private:
  std::vector<std::pair<std::string, ValueType>> _config_list;
};

class TclDestroySTA : public TclCmd
{
 public:
  explicit TclDestroySTA(const char* cmd_name);
  ~TclDestroySTA() override = default;

  unsigned check() override { return 1; }

  unsigned exec() override;

 private:
  std::vector<std::pair<std::string, ValueType>> _config_list;
};

#endif

}  // namespace tcl
