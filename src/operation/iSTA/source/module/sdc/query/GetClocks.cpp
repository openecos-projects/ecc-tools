#include <string>
#include <utility>
#include <vector>

#include "DataManager.hpp"
#include "query/ObjectQueryOptions.hpp"
#include "query/ObjectCommands.hpp"
#include "query/ObjectQuery.hpp"

namespace ista::sdc {

TclGetClocks::TclGetClocks(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringOption("clocks", 1));
  addObjectQueryOptions(*this, false, false, false);
}

unsigned TclGetClocks::exec()
{
  ecc::TclOption* objects = getOptionOrArg("clocks");
  const ObjectQueryOptions options = getObjectQueryOptions(*this);
  if (const auto error = getObjectQueryError(options, false))
    return setTclError("get_clocks " + *error), 0;
  const std::string patterns = objects->is_set_val() ? objects->getStringVal() : "*";
  std::vector<std::string> result = findQueryObjects(interpreter(), STADM.getDatabase(), getOptionObject("clocks"), QueryObjectType::kClock, options);
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val()) {
    warn("no clocks matched: " + patterns);
  }
  setResult(std::move(result), SdcObjectType::kClock);
  return 1;
}

}  // namespace ista::sdc
