#include <string>
#include <utility>
#include <vector>

#include "DataManager.hpp"
#include "ObjectQueryOptions.hpp"
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

TclGetGeneratedClocks::TclGetGeneratedClocks(const char* name, ClientData data) : TclGetClocks(name, data)
{
}
unsigned TclGetGeneratedClocks::exec()
{
  auto* argument = getOptionOrArg("clocks");
  const ObjectQueryOptions options = getObjectQueryOptions(*this);
  if (const auto error = getObjectQueryError(options, false))
    return setTclError("get_generated_clocks " + *error), 0;
  const std::string patterns = argument->is_set_val() ? argument->getStringVal() : "*";
  std::vector<std::string> result = findQueryObjects(interpreter(), STADM.getDatabase(), getOptionObject("clocks"), QueryObjectType::kClock, options);
  std::erase_if(result, [](const std::string& clock) { return !STADM.getDatabase().sdc().get_clock_map().at(clock).get_is_generated(); });
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val())
    warn("no generated clocks matched: " + patterns);
  setResult(std::move(result), SdcObjectType::kClock);
  return 1;
}

}  // namespace ista::sdc
