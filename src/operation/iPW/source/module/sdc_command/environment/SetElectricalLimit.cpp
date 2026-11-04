// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "DataManager.hpp"
#include "environment/EnvironmentCommands.hpp"
#include "object/ObjectQuery.hpp"

namespace ipw::sdc {
namespace {
std::vector<std::pair<LimitObject, std::string>> resolveTargets(
    Database& database,
    const std::vector<Tcl_Obj*>& objects
)
{
  std::vector<std::pair<LimitObject, std::string>> targets;
  for (auto* object : objects) {
    const std::string name = Tcl_GetString(object);
    const auto type = SdcObject::getType(object);
    if (type == SdcObjectType::kDesign || (type == SdcObjectType::kNone && name == database.get_design_name())) {
      if (name.empty() || name != database.get_design_name())
        throw std::invalid_argument("electrical limit design no longer exists: " + name);
      targets.emplace_back(LimitObject::kDesign, name);
      continue;
    }
    bool found = false;
    for (const auto& [tag, query, limit] :
         {std::tuple{SdcObjectType::kPort, QueryObjectType::kPort, LimitObject::kPort},
          std::tuple{SdcObjectType::kPin, QueryObjectType::kPin, LimitObject::kPin},
          std::tuple{SdcObjectType::kClock, QueryObjectType::kClock, LimitObject::kClock},
          std::tuple{SdcObjectType::kLibPin, QueryObjectType::kLibPin, LimitObject::kLibPin}}) {
      if (type != SdcObjectType::kNone && type != tag)
        continue;
      ObjectQueryOptions options;
      options.exact = type != SdcObjectType::kNone;
      const auto names = findObjects(database, {name}, query, options);
      for (const auto& match : names) {
        targets.emplace_back(limit, match);
      }
      if (!names.empty()) {
        found = true;
        break;
      }
    }
    if (!found)
      throw std::invalid_argument("electrical limit requires design, port, pin, lib_pin or clock objects: " + name);
  }
  if (targets.empty())
    throw std::invalid_argument("electrical limit requires a non-empty collection");
  return targets;
}
}  // namespace

TclSetElectricalLimit::TclSetElectricalLimit(const char* name)
    : SdcTclCmd(name), _transition(std::string(name) == "set_max_transition")
{
  for (const auto* option : {"-clock_path", "-data_path", "-rise", "-fall", "-force"})
    addOption(new ecc::TclSwitchOption(option));
  addOption(new ecc::TclDoubleOption("limit", 1));
  addOption(new ecc::TclStringListOption("objects", 1));
}

unsigned TclSetElectricalLimit::exec()
{
  if (!getOptionOrArg("limit")->is_set_val() || !getOptionOrArg("objects")->is_set_val())
    throw std::invalid_argument("electrical limit requires a value and an object collection");
  const double value = getOptionOrArg("limit")->getDoubleVal();
  if (!std::isfinite(value) || value < 0.)
    throw std::invalid_argument("electrical limit must be finite and non-negative");
  const bool clock = getOptionOrArg("-clock_path")->is_set_val();
  const bool data = getOptionOrArg("-data_path")->is_set_val();
  const bool rise = getOptionOrArg("-rise")->is_set_val();
  const bool fall = getOptionOrArg("-fall")->is_set_val();
  const bool force = getOptionOrArg("-force")->is_set_val();
  auto& db = PWDM.getDatabase();
  const auto targets = resolveTargets(db, getOptionObjects("objects"));
  // Resolve and validate the entire command before editing any constraints.
  for (const auto& [type, name] : targets) {
    if (force && (type == LimitObject::kDesign || type == LimitObject::kClock))
      throw std::invalid_argument("-force requires port, pin or lib_pin objects");
    if (type != LimitObject::kClock && (clock || data || rise || fall))
      throw std::invalid_argument("-clock_path/-data_path/-rise/-fall require clock objects");
  }
  const auto rule = _transition ? ElectricalRule::kTransition : ElectricalRule::kCapacitance;
  for (const auto& [type, name] : targets) {
    if (type != LimitObject::kClock) {
      db.get_timing_constraint().electricalLimits().set(
          {rule, type, name, false, TransType::kNone, value, force && type != LimitObject::kLibPin}
      );
      continue;
    }
    for (bool clock_path : {false, true}) {
      if (clock_path ? (data && !clock) : (clock && !data))
        continue;
      for (auto edge : {TransType::kRise, TransType::kFall}) {
        if (edge == TransType::kRise ? (fall && !rise) : (rise && !fall))
          continue;
        db.get_timing_constraint().electricalLimits().set({rule, type, name, clock_path, edge, value});
      }
    }
  }
  return 1;
}
}  // namespace ipw::sdc
