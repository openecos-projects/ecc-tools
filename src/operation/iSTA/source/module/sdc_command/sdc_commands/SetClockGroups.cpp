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
#include "DataManager.hpp"
#include "SdcCommands.hpp"

namespace ista::sdc {

TclSetClockGroups::TclSetClockGroups(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringListListOption("-group", 0));
  addOption(new ecc::TclStringOption("-name", 0));
  addOption(new ecc::TclStringOption("-comment", 0));
  for (const char* option : {"-asynchronous", "-logically_exclusive", "-physically_exclusive", "-exclusive", "-allow_paths"}) {
    addOption(new ecc::TclSwitchOption(option));
  }
}

unsigned TclSetClockGroups::exec()
{
  int mode_count = 0;
  for (const char* option : {"-asynchronous", "-logically_exclusive", "-physically_exclusive", "-exclusive"}) {
    mode_count += getOptionOrArg(option)->is_set_val();
  }
  if (mode_count != 1 || !getOptionOrArg("-group")->is_set_val()) {
    setTclError("set_clock_groups requires one relationship mode and at least one -group");
    return 0;
  }
  const bool allow_paths = getOptionOrArg("-allow_paths")->is_set_val();
  if (allow_paths && !getOptionOrArg("-asynchronous")->is_set_val()) {
    setTclError("-allow_paths requires -asynchronous");
    return 0;
  }
  std::vector<std::set<std::string>> groups;
  std::set<std::string> seen;
  for (const std::vector<std::string>& names : getOptionOrArg("-group")->getStringListList()) {
    if (names.empty()) {
      setTclError("empty clock group");
      return 0;
    }
    std::set<std::string> group;
    for (const std::string& name : names) {
      if (!STADM.getDatabase().get_timing_constraint().get_clock_map().contains(name)) {
        setTclError("unknown clock in group: " + name);
        return 0;
      }
      if (seen.contains(name) && !group.contains(name)) {
        setTclError("clock belongs to multiple groups: " + name);
        return 0;
      }
      group.insert(name);
      seen.insert(name);
    }
    groups.push_back(std::move(group));
  }
  TimingClockGroup relation;
  relation.set_groups(groups);
  relation.set_allow_paths(allow_paths);
  STADM.getDatabase().get_timing_constraint().get_clock_group_list().push_back(std::move(relation));
  return 1;
}

}  // namespace ista::sdc
