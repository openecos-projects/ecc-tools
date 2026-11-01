// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <string>

#include "SdcTclCmd.hpp"

namespace ista::sdc {

class TclSetUnits : public SdcTclCmd
{
 public:
  TclSetUnits(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;

 private:
  double parseUnitScale(const std::string& value, const std::string& suffix);
  double getUnitScale(const std::string& option);
};

}  // namespace ista::sdc
