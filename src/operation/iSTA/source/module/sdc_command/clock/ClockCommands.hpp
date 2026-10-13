// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#pragma once

#include "frontend/SdcTclCmd.hpp"

namespace ista::sdc {

class TclSetClockTransition : public SdcTclCmd
{
 public:
  TclSetClockTransition(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetClockLatency : public SdcTclCmd
{
 public:
  TclSetClockLatency(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclRemoveClockLatency : public SdcTclCmd
{
 public:
  TclRemoveClockLatency(const char* cmd_name, ClientData client_data);
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

class TclSetClockUncertainty : public SdcTclCmd
{
 public:
  TclSetClockUncertainty(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclRemoveClockUncertainty : public SdcTclCmd
{
 public:
  TclRemoveClockUncertainty(const char* cmd_name, ClientData client_data);
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

class TclRemovePropagatedClock : public SdcTclCmd
{
 public:
  TclRemovePropagatedClock(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

}  // namespace ista::sdc
