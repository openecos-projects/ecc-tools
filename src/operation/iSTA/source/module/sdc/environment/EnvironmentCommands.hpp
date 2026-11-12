// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#pragma once

#include "SdcTclCmd.hpp"

namespace ista::sdc {

class TclSetElectricalLimit : public SdcTclCmd
{
 public:
  TclSetElectricalLimit(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;

 private:
  bool _transition;
};

class TclSetTimingDerate : public SdcTclCmd
{
 public:
  TclSetTimingDerate(const char* cmd_name, ClientData client_data);
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

class TclSetDrivingCell : public SdcTclCmd
{
 public:
  TclSetDrivingCell(const char* cmd_name, ClientData client_data);
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

class TclSetMaxFanout : public SdcTclCmd
{
 public:
  TclSetMaxFanout(const char* cmd_name, ClientData client_data);
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

}  // namespace ista::sdc
