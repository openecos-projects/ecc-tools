// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include "Database.hpp"
#include "frontend/SdcTclCmd.hpp"

namespace ipw::sdc {

class TclGetFan : public SdcTclCmd
{
 public:
  TclGetFan(const char* cmd_name);
  unsigned check() override { return 1; }
  unsigned exec() override;

 private:
  bool _is_fanin = false;

  std::set<std::string> getQueryPins(const std::vector<std::string>& objects);
  bool isLoad(Pin& pin);
  PinDirection getDirection(Pin& pin);
  std::set<std::string> findFan(const std::set<std::string>& roots, bool boundary_only, bool only_cells);
  bool isRegisterBoundary(Pin& pin);
  std::vector<std::string> getAdjacentPins(Pin& pin, bool fanin);
  std::string getFullPinName(Pin& pin);
  bool isSearchPin(Pin& pin);
  bool isDriver(Pin& pin);
};

}  // namespace ipw::sdc
