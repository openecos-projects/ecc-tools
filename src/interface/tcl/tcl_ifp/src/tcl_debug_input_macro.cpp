#include "FPInterface.hpp"
#include "tcl_fp.h"
#include "tcl_util.h"

namespace tcl {

TclDebugInputMacro::TclDebugInputMacro(const char* cmd_name) : TclCmd(cmd_name)
{
  _config_list.emplace_back("-path", ValueType::kString);
  TclUtil::addOption(this, _config_list);
}

unsigned TclDebugInputMacro::exec()
{
  if (!check()) {
    return 0;
  }
  FPI.debugInputMacro(TclUtil::getConfigMap(this, _config_list));
  return 1;
}

}  // namespace tcl
