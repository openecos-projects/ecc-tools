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
#include "STAInterface.hpp"
#include "tcl_ista_util.hpp"
#include "tcl_sta.h"

namespace tcl {

TclCreateClock::TclCreateClock(const char* cmd_name) : TclCmd(cmd_name)
{
  addOption(new TclStringOption("-name", 0));
  addOption(new TclDoubleOption("-period", 0));
  addOption(new TclDoubleListOption("-waveform", 0));
  addOption(new TclStringListOption("objects", 1));
}

unsigned TclCreateClock::exec()
{
  TclOption* name_option = getOptionOrArg("-name");
  TclOption* period_option = getOptionOrArg("-period");
  TclOption* waveform_option = getOptionOrArg("-waveform");
  TclOption* object_option = getOptionOrArg("objects");
  if (!name_option->is_set_val() || !period_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("create_clock requires -name, -period, and a port collection");
    return 0;
  }

  const double period = period_option->getDoubleVal();
  double rise_edge = 0.0;
  double fall_edge = period / 2.0;
  if (waveform_option->is_set_val()) {
    const std::vector<double> waveform = waveform_option->getDoubleList();
    if (waveform.size() != 2) {
      setTclError("create_clock -waveform must contain exactly two values");
      return 0;
    }
    rise_edge = waveform[0];
    fall_edge = waveform[1];
  }

  if (std::string error_message;
      !STAI.createClock(name_option->getStringVal(), period, rise_edge, fall_edge, object_option->getStringList(), error_message)) {
    setTclError(error_message);
    return 0;
  }
  return 1;
}

}  // namespace tcl
