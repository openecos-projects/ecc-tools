// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include "exception/PathExceptionCommand.hpp"
#include "frontend/SdcTclCmd.hpp"
#include "object/ObjectCommands.hpp"

namespace ista::sdc {

class TclRemoveClockLatency : public SdcTclCmd
{
 public:
  TclRemoveClockLatency(const char* cmd_name, ClientData client_data);
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

class TclRemovePropagatedClock : public SdcTclCmd
{
 public:
  TclRemovePropagatedClock(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclGetFullName : public SdcTclCmd
{
 public:
  TclGetFullName(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclGetGeneratedClocks : public TclGetClocks
{
 public:
  TclGetGeneratedClocks(const char* cmd_name, ClientData client_data);
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
