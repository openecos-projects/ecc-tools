// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#include "DataManager.hpp"
#include "clock/ClockCommands.hpp"
#include "object/ObjectQuery.hpp"

namespace ista::sdc {

namespace {

const char* getSelector(SdcTclCmd& command, std::initializer_list<const char*> options)
{
  const char* selected = nullptr;
  for (const char* option : options) {
    if (!command.getOptionOrArg(option)->is_set_val()) continue;
    if (selected != nullptr) throw std::invalid_argument(std::string(command.get_cmd_name()) + " accepts only one selector per side");
    selected = option;
  }
  return selected;
}

TransType getSelectorTransition(const char* selector)
{
  if (selector == nullptr) return TransType::kNone;
  const std::string_view option(selector);
  if (option.starts_with("-rise")) return TransType::kRise;
  if (option.starts_with("-fall")) return TransType::kFall;
  return TransType::kNone;
}

}  // namespace

TclRemoveClockUncertainty::TclRemoveClockUncertainty(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  for (const char* option : {"-from", "-rise_from", "-fall_from", "-to", "-rise_to", "-fall_to"}) {
    addOption(new ecc::TclStringListOption(option, 0));
  }
  for (const char* option : {"-rise", "-fall", "-setup", "-hold"}) {
    addOption(new ecc::TclSwitchOption(option));
  }
  addOption(new ecc::TclStringOption("objects", 1));
}

unsigned TclRemoveClockUncertainty::exec()
{
  Database& database = STADM.getDatabase();
  ecc::TclOption* object_option = getOptionOrArg("objects");
  const bool setup = getOptionOrArg("-setup")->is_set_val();
  const bool hold = getOptionOrArg("-hold")->is_set_val();
  const bool remove_setup = setup || !hold;
  const bool remove_hold = hold || !setup;
  try {
    const char* from_selector = getSelector(*this, {"-from", "-rise_from", "-fall_from"});
    const char* to_selector = getSelector(*this, {"-to", "-rise_to", "-fall_to"});
    if ((from_selector == nullptr) != (to_selector == nullptr)) {
      throw std::invalid_argument(std::string(get_cmd_name()) + " requires -from and -to together");
    }
    if (from_selector != nullptr) {
      if (object_option->is_set_val()) throw std::invalid_argument(std::string(get_cmd_name()) + " selectors and object list are mutually exclusive");
      const std::set<std::string> from_clocks = findClocks(database, getOptionOrArg(from_selector)->getStringList());
      const std::set<std::string> to_clocks = findClocks(database, getOptionOrArg(to_selector)->getStringList());
      TransType to_transition = getSelectorTransition(to_selector);
      if (to_transition == TransType::kNone) {
        if (getOptionOrArg("-rise")->is_set_val() && getOptionOrArg("-fall")->is_set_val()) {
          throw std::invalid_argument(std::string(get_cmd_name()) + " accepts only one of -rise and -fall");
        }
        if (getOptionOrArg("-rise")->is_set_val()) to_transition = TransType::kRise;
        if (getOptionOrArg("-fall")->is_set_val()) to_transition = TransType::kFall;
      }
      auto& uncertainties = database.get_timing_constraint().get_clock_uncertainty_list();
      for (TimingClockUncertainty& uncertainty : uncertainties) {
        if (uncertainty.get_from_clocks() != from_clocks || uncertainty.get_to_clocks() != to_clocks
            || uncertainty.get_from_trans_type() != getSelectorTransition(from_selector)
            || uncertainty.get_to_trans_type() != to_transition) {
          continue;
        }
        if (remove_setup) uncertainty.set_setup(false);
        if (remove_hold) uncertainty.set_hold(false);
      }
      std::erase_if(uncertainties, [](const TimingClockUncertainty& uncertainty) {
        return !uncertainty.get_setup() && !uncertainty.get_hold();
      });
      return 1;
    }

    if (!object_option->is_set_val()) throw std::invalid_argument(std::string(get_cmd_name()) + " requires an object collection");
    if (getOptionOrArg("-rise")->is_set_val() || getOptionOrArg("-fall")->is_set_val()) {
      throw std::invalid_argument(std::string(get_cmd_name()) + " -rise/-fall require inter-clock selectors");
    }
    for (const std::string& name : findClocks(database, parseObjectPatterns(object_option->getStringVal(), false))) {
      TimingClock& clock = database.get_timing_constraint().get_clock_map().at(name);
      if (remove_setup) clock.set_setup_uncertainty(0.0);
      if (remove_hold) clock.set_hold_uncertainty(0.0);
    }
  } catch (const std::exception& error) {
    setTclError(error.what());
    return 0;
  }
  return 1;
}

}  // namespace ista::sdc
