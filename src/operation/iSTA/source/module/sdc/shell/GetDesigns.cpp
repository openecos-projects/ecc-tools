// SPDX-License-Identifier: MulanPSL-2.0
#include "shell/ObjectCommands.hpp"

#include <utility>

#include "DataManager.hpp"
#include "query/ObjectQueryOptions.hpp"

namespace ista::sdc {
TclGetDesigns::TclGetDesigns(const char* name, ClientData data) : SdcTclCmd(name, data)
{
  addOption(new ecc::TclStringOption("patterns", 1));
  addObjectQueryOptions(*this, true, true, false);
}

unsigned TclGetDesigns::exec()
{
  auto* patterns = getOptionOrArg("patterns");
  if (!patterns->is_set_val())
    return setTclError("get_designs requires patterns"), 0;
  const auto options = getObjectQueryOptions(*this);
  if (const auto error = getObjectQueryError(options, true))
    return setTclError("get_designs " + *error), 0;
  // The database owns one loaded design; hierarchy instances are not designs.
  auto result = findQueryObjects(interpreter(), STADM.getDatabase(), getOptionObject("patterns"), QueryObjectType::kDesign, options);
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val())
    warn("no designs matched: " + std::string(patterns->getStringVal()));
  setResult(std::move(result), SdcObjectType::kDesign);
  return 1;
}
}  // namespace ista::sdc
