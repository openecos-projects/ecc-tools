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

#include "frontend/SdcTclCmd.hpp"
#include "TimingException.hpp"

namespace ista {
class Database;
}

namespace ista::sdc {

class TclPathException : public SdcTclCmd
{
 public:
  TclPathException(const char* cmd_name, ClientData client_data, bool comment_option);

 protected:
  TimingException parsePathSelector(Database& database, bool require_selector);
  void applyAnalysisQualifiers(TimingException& exception);
};

void resetPathExceptions(Database& database, const TimingException& filter);

class TclSetFalsePath : public TclPathException
{
 public:
  TclSetFalsePath(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetPathDelay : public TclPathException
{
 protected:
  TclSetPathDelay(const char* cmd_name, ClientData client_data);
  unsigned executePathDelay(TimingExceptionType type);
};

class TclSetMaxDelay : public TclSetPathDelay
{
 public:
  TclSetMaxDelay(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetMinDelay : public TclSetPathDelay
{
 public:
  TclSetMinDelay(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetMulticyclePath : public TclPathException
{
 public:
  TclSetMulticyclePath(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclResetPath : public TclPathException
{
 public:
  TclResetPath(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

}  // namespace ista::sdc
