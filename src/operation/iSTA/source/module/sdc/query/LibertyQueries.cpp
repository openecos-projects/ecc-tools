#include <string>
#include <utility>

#include "DataManager.hpp"
#include "ObjectQueryOptions.hpp"
#include "query/ObjectCommands.hpp"
#include "query/ObjectQuery.hpp"

namespace ista::sdc {

TclGetLibs::TclGetLibs(const char* name, ClientData data) : SdcTclCmd(name, data)
{
  addOption(new ecc::TclStringOption("patterns", 1));
  addObjectQueryOptions(*this, false, true, true);
}
unsigned TclGetLibs::exec()
{
  auto* argument = getOptionOrArg("patterns");
  const ObjectQueryOptions options = getObjectQueryOptions(*this);
  if (const auto error = getObjectQueryError(options, argument->is_set_val()))
    return setTclError("get_libs " + *error), 0;
  const std::string patterns = argument->is_set_val() ? argument->getStringVal() : "*";
  auto result = findQueryObjects(interpreter(), STADM.getDatabase(), getOptionObject("patterns"), QueryObjectType::kLibrary, options);
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val())
    warn("no libraries matched: " + patterns);
  setResult(std::move(result), SdcObjectType::kLibrary);
  return 1;
}

TclGetLibCells::TclGetLibCells(const char* name, ClientData data) : SdcTclCmd(name, data)
{
  addOption(new ecc::TclStringOption("patterns", 1));
  addObjectQueryOptions(*this, false, true, true);
}
unsigned TclGetLibCells::exec()
{
  auto* argument = getOptionOrArg("patterns");
  const ObjectQueryOptions options = getObjectQueryOptions(*this);
  if (const auto error = getObjectQueryError(options, argument->is_set_val()))
    return setTclError("get_lib_cells " + *error), 0;
  const std::string patterns = argument->is_set_val() ? argument->getStringVal() : "*";
  auto result = findQueryObjects(interpreter(), STADM.getDatabase(), getOptionObject("patterns"), QueryObjectType::kLibCell, options);
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val())
    warn("no library cells matched: " + patterns);
  setResult(std::move(result), SdcObjectType::kLibCell);
  return 1;
}

TclGetLibPins::TclGetLibPins(const char* name, ClientData data) : SdcTclCmd(name, data)
{
  addOption(new ecc::TclStringOption("patterns", 1));
  addObjectQueryOptions(*this, false, true, true);
}
unsigned TclGetLibPins::exec()
{
  auto* argument = getOptionOrArg("patterns");
  const ObjectQueryOptions options = getObjectQueryOptions(*this);
  if (const auto error = getObjectQueryError(options, argument->is_set_val()))
    return setTclError("get_lib_pins " + *error), 0;
  const std::string patterns = argument->is_set_val() ? argument->getStringVal() : "*/*";
  auto result = findQueryObjects(interpreter(), STADM.getDatabase(), getOptionObject("patterns"), QueryObjectType::kLibPin, options, "*/*");
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val())
    warn("no library pins matched: " + patterns);
  setResult(std::move(result), SdcObjectType::kLibPin);
  return 1;
}

}  // namespace ista::sdc
