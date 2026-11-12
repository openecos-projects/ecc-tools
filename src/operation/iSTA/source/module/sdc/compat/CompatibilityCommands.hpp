// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
#include "SdcTclCmd.hpp"
namespace ista { class SdcCommand; }
namespace ista::sdc {
class TclAllDelayCorners : public SdcTclCmd
{
 public:
  TclAllDelayCorners(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data) {}
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclGetName : public SdcTclCmd
{
 public:
  TclGetName(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};
void registerCompatibilityCommands(SdcCommand& interpreter);
}
