#include "PWInterface.hpp"
#include "tcl_pw.h"
#include "tcl_util.h"

namespace tcl {

TclReportPeakPowerWaveform::TclReportPeakPowerWaveform(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* path_option = new TclStringOption("-path", 0);
  addOption(path_option);
}

unsigned TclReportPeakPowerWaveform::exec()
{
  if (!check()) {
    return 0;
  }
  TclOption* path_option = getOptionOrArg("-path");

  std::string file_path;
  if (path_option->is_set_val()) {
    file_path = path_option->getStringVal();
  }
  PWI.reportPeakPowerWaveform(file_path);
  return 1;
}

}  // namespace tcl
