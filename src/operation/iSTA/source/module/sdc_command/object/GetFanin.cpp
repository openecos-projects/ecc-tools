#include "DataManager.hpp"
#include "ObjectCommands.hpp"
#include "ObjectQuery.hpp"

namespace ista::sdc {

TclGetFanin::TclGetFanin(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringListOption("-to", 0));
  addOption(new ecc::TclSwitchOption("-flat"));
  addOption(new ecc::TclSwitchOption("-startpoints_only"));
  addOption(new ecc::TclSwitchOption("-only_cells"));
  addOption(new ecc::TclSwitchOption("-only_pins"));
}

unsigned TclGetFanin::exec()
{
  ecc::TclOption* to = getOptionOrArg("-to");
  if (to == nullptr || !to->is_set_val()) {
    setTclError("get_fanin requires -to");
    return 0;
  }
  const bool only_cells = getOptionOrArg("-only_cells")->is_set_val();
  const bool only_pins = getOptionOrArg("-only_pins")->is_set_val();
  if (only_cells && only_pins) {
    setTclError("get_fanin accepts only one of -only_cells and -only_pins");
    return 0;
  }
  setResult(findFanin(STADM.getDatabase(), to->getStringList(), getOptionOrArg("-startpoints_only")->is_set_val(), only_cells));
  return 1;
}

}  // namespace ista::sdc
