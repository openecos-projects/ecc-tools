#include <string>
#include <utility>
#include <vector>

#include "DataManager.hpp"
#include "query/ObjectQueryOptions.hpp"
#include "shell/ObjectCommands.hpp"
#include "query/ObjectQuery.hpp"

namespace ista::sdc {
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
