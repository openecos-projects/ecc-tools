// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include "frontend/SdcTclCmd.hpp"

namespace ista::sdc {

class TclGetName : public SdcTclCmd
{
 public:
  TclGetName(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

}  // namespace ista::sdc
