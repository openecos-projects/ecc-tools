// SPDX-License-Identifier: MulanPSL-2.0
#include "SdcReader.hpp"

#include <string>

#include "Logger.hpp"
#include "SdcCommand.hpp"
#include "Singleton.hpp"
#include "sdc/Sdc.hpp"

namespace ista {
void readSdc(Sdc& constraints)
{
  const std::string sdc_file_path = constraints.get_sdc_file_path();
  constraints.clearConstraints();
  if (sdc_file_path.empty()) {
    return;
  }

  auto& sdc_command{Singleton<SdcCommand>::getInst()};
  if (sdc_command.evalScriptFile(sdc_file_path) != TCL_OK) {
    for (const SdcError& error : sdc_command.getErrors()) {
      STALOG.warn(Loc::current(), "SDC command failed in '", sdc_file_path, "' at line ", error.line_number, ": ", error.message);
    }
    STALOG.error(Loc::current(), "SDC contains invalid or unsupported constraints; timing analysis stopped");
  }
}
}  // namespace ista
