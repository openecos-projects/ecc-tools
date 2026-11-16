// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
#include "SdcTclCmd.hpp"
#include "exception/PathExceptionCommand.hpp"
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

class TclResetPath : public TclPathException
{
 public:
  TclResetPath(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};
}  // namespace ista::sdc
