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

#include "SdcTclCmd.hpp"

namespace ista::sdc {

class TclCurrentDesign : public SdcTclCmd
{
 public:
  TclCurrentDesign(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclRemoveFromCollection : public SdcTclCmd
{
 public:
  TclRemoveFromCollection(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetClockTransition : public SdcTclCmd
{
 public:
  TclSetClockTransition(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetMaxFanout : public SdcTclCmd
{
 public:
  TclSetMaxFanout(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclGetPins : public SdcTclCmd
{
 public:
  TclGetPins(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclAllInputs : public SdcTclCmd
{
 public:
  TclAllInputs(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclAllOutputs : public SdcTclCmd
{
 public:
  TclAllOutputs(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetFalsePath : public SdcTclCmd
{
 public:
  TclSetFalsePath(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetClockGroups : public SdcTclCmd
{
 public:
  TclSetClockGroups(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclCreateGeneratedClock : public SdcTclCmd
{
 public:
  TclCreateGeneratedClock(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetCaseAnalysis : public SdcTclCmd
{
 public:
  TclSetCaseAnalysis(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetInputDelay : public SdcTclCmd
{
 public:
  TclSetInputDelay(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetOutputDelay : public SdcTclCmd
{
 public:
  TclSetOutputDelay(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetInputTransition : public SdcTclCmd
{
 public:
  TclSetInputTransition(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetLoad : public SdcTclCmd
{
 public:
  TclSetLoad(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetClockUncertainty : public SdcTclCmd
{
 public:
  TclSetClockUncertainty(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclGetPorts : public SdcTclCmd
{
 public:
  TclGetPorts(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclGetClocks : public SdcTclCmd
{
 public:
  TclGetClocks(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclCreateClock : public SdcTclCmd
{
 public:
  TclCreateClock(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetPropagatedClock : public SdcTclCmd
{
 public:
  TclSetPropagatedClock(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

}  // namespace ista::sdc
