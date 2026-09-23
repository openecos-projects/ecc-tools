#include "DataManager.hpp"
#include "object/ObjectCommands.hpp"
#include "object/ObjectQuery.hpp"
namespace ipw::sdc {
TclGetLibPins::TclGetLibPins(const char* name, ClientData data) : SdcTclCmd(name, data)
{
  addOption(new ecc::TclStringOption("patterns", 1));
  addObjectQueryOptions(*this, false, true, true);
}
unsigned TclGetLibPins::exec()
{
  auto* argument = getOptionOrArg("patterns");
  const ObjectQueryOptions options = getObjectQueryOptions(*this);
  if (const auto error = getObjectQueryError(options, argument->is_set_val())) return setTclError("get_lib_pins " + *error), 0;
  const std::string patterns = argument->is_set_val() ? argument->getStringVal() : "*/*";
  auto result = findObjects(PWDM.getDatabase(), parseObjectPatterns(patterns, options.regexp), QueryObjectType::kLibPin, options);
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val()) warn("no library pins matched: " + patterns);
  setResult(std::move(result));
  return 1;
}
}  // namespace ipw::sdc
