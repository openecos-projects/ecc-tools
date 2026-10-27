// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <set>
#include <string>
#include <vector>

#include "Database.hpp"
#include "SdcTclCmd.hpp"

namespace ista::sdc {

class TclGetFan : public SdcTclCmd
{
 public:
  TclGetFan(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;

 private:
  bool _is_fanin = false;

  std::set<std::string> getQueryPins(const std::vector<Tcl_Obj*>& objects);
  bool isLoad(Pin& pin);
  PinDirection getDirection(Pin& pin);
  std::set<std::string> findFan(const std::set<std::string>& roots, bool boundary_only, bool only_cells);
  bool isRegisterBoundary(Pin& pin);
  std::vector<std::string> getAdjacentPins(Pin& pin, bool fanin);
  bool isSearchPin(Pin& pin);
  bool isDriver(Pin& pin);
};

}  // namespace ista::sdc
