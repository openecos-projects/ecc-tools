#include "DataManager.hpp"
#include "object/ObjectCommands.hpp"
#include "object/ObjectQuery.hpp"
namespace ipw::sdc {
TclGetLibCells::TclGetLibCells(const char* name, ClientData data) : SdcTclCmd(name, data)
{
  addOption(new ecc::TclStringOption("patterns", 1));
  addObjectQueryOptions(*this, false, true, true);
}
unsigned TclGetLibCells::exec()
{
  auto* argument = getOptionOrArg("patterns");
  const ObjectQueryOptions options = getObjectQueryOptions(*this);
  if (const auto error = getObjectQueryError(options, argument->is_set_val())) return setTclError("get_lib_cells " + *error), 0;
  const std::string patterns = argument->is_set_val() ? argument->getStringVal() : "*";
  auto result = findObjects(PWDM.getDatabase(), parseObjectPatterns(patterns, options.regexp), QueryObjectType::kLibCell, options);
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val()) warn("no library cells matched: " + patterns);
  setResult(std::move(result));
  return 1;
}
}  // namespace ipw::sdc
