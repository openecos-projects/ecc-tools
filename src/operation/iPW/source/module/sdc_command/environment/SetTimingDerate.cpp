// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "DataManager.hpp"
#include "environment/EnvironmentCommands.hpp"
#include "object/ObjectQuery.hpp"

namespace ipw::sdc {
TclSetTimingDerate::TclSetTimingDerate(const char* name) : SdcTclCmd(name)
{
  for (const auto* option :
       {"-early",
        "-late",
        "-clock",
        "-data",
        "-rise",
        "-fall",
        "-cell_delay",
        "-net_delay",
        "-cell_check",
        "-increment",
        "-static",
        "-dynamic",
        "-min_period",
        "-min_pulse_width"})
    addOption(new ecc::TclSwitchOption(option));
  addOption(new ecc::TclStringListOption("-delay_corner", 0));
  addOption(new ecc::TclDoubleOption("derate", 1));
  addOption(new ecc::TclStringListOption("objects", 1));
}

unsigned TclSetTimingDerate::exec()
{
  const auto selected = [&](const char* option) { return getOptionOrArg(option)->is_set_val(); };
  const bool period = selected("-min_period"), width = selected("-min_pulse_width"), pulse = period || width;
  if (pulse
      && (selected("-early") || selected("-late") || selected("-clock") || selected("-data") || selected("-cell_check")
          || selected("-cell_delay") || selected("-net_delay")))
    throw std::invalid_argument(
        "minimum period/pulse-width derates use rise/fall, without early/late or delay/check qualifiers"
    );
  if (!selected("derate") || (!pulse && selected("-early") == selected("-late")))
    throw std::invalid_argument("set_timing_derate requires a factor and exactly one of -early/-late");
  const double factor = getOptionOrArg("derate")->getDoubleVal();
  const bool increment = selected("-increment"), check = selected("-cell_check");
  if (selected("-dynamic"))
    throw std::invalid_argument("dynamic net derating requires crosstalk delay analysis, which is not available");
  if (selected("-static") && !selected("-net_delay"))
    throw std::invalid_argument("-static requires -net_delay");
  // All evaluated net delays are static. The effective record is therefore
  // the same whether -static is explicit or the component is unrestricted.
  if (!std::isfinite(factor) || (!increment && factor < 0.))
    throw std::invalid_argument("set_timing_derate baseline must be non-negative; increments may be negative");
  if (check && (selected("-clock") || selected("-data") || selected("-cell_delay") || selected("-net_delay")))
    throw std::invalid_argument("-cell_check cannot be combined with clock/data or cell/net delay options");
  if (selected("-delay_corner")) {
    const auto corners = getOptionObjects("-delay_corner");
    if (corners.empty())
      throw std::invalid_argument("set_timing_derate requires a non-empty delay corner selection");
    for (auto* corner : corners)
      if (std::string_view(Tcl_GetString(corner)) != "default"
          || (SdcObject::getType(corner) != SdcObjectType::kNone
              && SdcObject::getType(corner) != SdcObjectType::kDelayCorner))
        throw std::invalid_argument("unknown delay corner; iSTA currently evaluates the single default corner");
  }
  const auto analysis = pulse ? AnalysisType::kNone : selected("-early") ? AnalysisType::kMin : AnalysisType::kMax;
  auto& database = PWDM.getDatabase();
  auto& derates = database.get_timing_constraint().timingDerates();
  std::vector<std::pair<DerateObject, std::string>> targets;
  if (!selected("objects")) {
    targets.emplace_back(DerateObject::kDesign, "");
  } else {
    for (auto* object : getOptionObjects("objects")) {
      const std::string name = Tcl_GetString(object);
      const auto type = SdcObject::getType(object);
      bool found = false;
      for (const auto& [tag, query, target] :
           {std::tuple{SdcObjectType::kCell, QueryObjectType::kCell, DerateObject::kCell},
            std::tuple{SdcObjectType::kLibCell, QueryObjectType::kLibCell, DerateObject::kLibCell},
            std::tuple{SdcObjectType::kNet, QueryObjectType::kNet, DerateObject::kNet}}) {
        if (type != SdcObjectType::kNone && type != tag)
          continue;
        ObjectQueryOptions options;
        options.exact = type != SdcObjectType::kNone;
        const auto names = findObjects(database, {name}, query, options);
        for (const auto& match : names) {
          const bool hierarchy = target == DerateObject::kCell && !database.get_instance_map().contains(match);
          if (hierarchy && !check && !pulse && !selected("-cell_delay") && !selected("-net_delay"))
            throw std::invalid_argument("hierarchical derates require an explicit delay or check kind");
          targets.emplace_back(
              hierarchy ? DerateObject::kHierarchy : target,
              target == DerateObject::kNet ? match : match
          );
        }
        if (!names.empty()) {
          found = true;
          break;
        }
      }
      if (!found)
        throw std::invalid_argument("derate requires existing cells, library cells or nets: " + name);
    }
    if (targets.empty())
      throw std::invalid_argument("derate requires a non-empty object collection");
  }
  // The entire command is validated before any factor is changed.
  for (const auto& [object, name] : targets)
    for (bool clock : {false, true}) {
      if ((check || pulse) && (clock || object == DerateObject::kNet))
        continue;
      if (clock ? (selected("-data") && !selected("-clock")) : (selected("-clock") && !selected("-data")))
        continue;
      for (auto edge : {TransType::kRise, TransType::kFall}) {
        if (edge == TransType::kRise ? (selected("-fall") && !selected("-rise"))
                                     : (selected("-rise") && !selected("-fall")))
          continue;
        for (auto kind :
             {DerateDelay::kCell, DerateDelay::kNet, DerateDelay::kCheck, DerateDelay::kPeriod, DerateDelay::kWidth}) {
          if (pulse) {
            if (!(period && kind == DerateDelay::kPeriod) && !(width && kind == DerateDelay::kWidth))
              continue;
          } else if (kind == DerateDelay::kPeriod || kind == DerateDelay::kWidth
                     || check != (kind == DerateDelay::kCheck))
            continue;
          if (!check && !pulse) {
            if (object == DerateObject::kNet && kind != DerateDelay::kNet)
              continue;
            if ((object == DerateObject::kCell || object == DerateObject::kLibCell) && kind != DerateDelay::kCell)
              continue;
            if (kind == DerateDelay::kCell ? (selected("-net_delay") && !selected("-cell_delay"))
                                           : (selected("-cell_delay") && !selected("-net_delay")))
              continue;
          }
          derates.set(analysis, clock, edge, kind, factor, object, name, increment);
        }
      }
    }
  return 1;
}
}  // namespace ipw::sdc
