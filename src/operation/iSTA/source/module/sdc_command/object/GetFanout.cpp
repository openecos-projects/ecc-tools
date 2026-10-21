#include "DataManager.hpp"
#include "ObjectCommands.hpp"
#include "ObjectQuery.hpp"

namespace ista::sdc {

TclGetFanout::TclGetFanout(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringListOption("-from", 0));
  addOption(new ecc::TclSwitchOption("-flat"));
  addOption(new ecc::TclSwitchOption("-endpoints_only"));
  addOption(new ecc::TclSwitchOption("-only_cells"));
  addOption(new ecc::TclSwitchOption("-only_pins"));
}

unsigned TclGetFanout::exec()
{
  ecc::TclOption* from = getOptionOrArg("-from");
  if (from == nullptr || !from->is_set_val()) {
    setTclError("get_fanout requires -from");
    return 0;
  }
  const bool only_cells = getOptionOrArg("-only_cells")->is_set_val();
  const bool only_pins = getOptionOrArg("-only_pins")->is_set_val();
  if (only_cells && only_pins) {
    setTclError("get_fanout accepts only one of -only_cells and -only_pins");
    return 0;
  }
  setResult(findFanout(STADM.getDatabase(), from->getStringList(), getOptionOrArg("-endpoints_only")->is_set_val(), only_cells));
  return 1;
}

}  // namespace ista::sdc
