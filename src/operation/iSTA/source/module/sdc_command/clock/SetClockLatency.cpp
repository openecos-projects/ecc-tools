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

TclSetClockLatency::TclSetClockLatency(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringOption("-clock", 0));
  for (const char* option : {"-rise", "-fall", "-min", "-max", "-source", "-early", "-late"}) {
    addOption(new ecc::TclSwitchOption(option));
  }
  addOption(new ecc::TclDoubleOption("delay", 1));
  addOption(new ecc::TclStringOption("objects", 1));
}

unsigned TclSetClockLatency::exec()
{
  ecc::TclOption* delay_option = getOptionOrArg("delay");
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!delay_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("set_clock_latency requires a delay and an object collection");
    return 0;
  }
  const double latency = delay_option->getDoubleVal();
  if (!std::isfinite(latency)) {
    setTclError("set_clock_latency delay must be finite");
    return 0;
  }

  const bool source = getOptionOrArg("-source")->is_set_val();
  const bool early = getOptionOrArg("-early")->is_set_val();
  const bool late = getOptionOrArg("-late")->is_set_val();
  if (!source && (early || late)) {
    setTclError("set_clock_latency -early/-late require -source");
    return 0;
  }
  if (getOptionOrArg("-clock")->is_set_val()) {
    warn("set_clock_latency -clock is ignored for clock objects");
  }

  Database& database = STADM.getDatabase();
  std::set<std::string> clocks;
  try {
    clocks = findClocks(database, parseObjectPatterns(object_option->getStringVal(), false));
  } catch (const std::exception& error) {
    setTclError(std::string("set_clock_latency currently supports clock objects: ") + error.what());
    return 0;
  }

  const bool rise = getOptionOrArg("-rise")->is_set_val();
  const bool fall = getOptionOrArg("-fall")->is_set_val();
  const bool min = getOptionOrArg("-min")->is_set_val();
  const bool max = getOptionOrArg("-max")->is_set_val();
  const bool select_min = min || early;
  const bool select_max = max || late;
  for (const std::string& name : clocks) {
    TimingClock& clock = database.get_timing_constraint().get_clock_map().at(name);
    for (AnalysisType analysis_type : {AnalysisType::kMin, AnalysisType::kMax}) {
      if ((analysis_type == AnalysisType::kMin && select_max && !select_min)
          || (analysis_type == AnalysisType::kMax && select_min && !select_max)) {
        continue;
      }
      for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
        if ((trans_type == TransType::kRise && fall && !rise) || (trans_type == TransType::kFall && rise && !fall)) {
          continue;
        }
        if (source) {
          clock.set_source_latency(analysis_type, trans_type, latency);
        } else {
          clock.set_network_latency(analysis_type, trans_type, latency);
        }
      }
    }
  }
  return 1;
}

}  // namespace ista::sdc
