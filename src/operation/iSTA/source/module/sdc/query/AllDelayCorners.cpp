// SPDX-License-Identifier: MulanPSL-2.0
#include "query/ObjectCommands.hpp"
#include "sdc/Sdc.hpp"

namespace ista::sdc {
unsigned TclAllDelayCorners::exec()
{
  // iSTA currently evaluates one library/parasitic context with min/max
  // analyses. Those analyses are not separate delay corners.
  setResult(std::vector<std::string>{std::string(Sdc::delayCornerName())}, SdcObjectType::kDelayCorner);
  return 1;
}
}  // namespace ista::sdc
