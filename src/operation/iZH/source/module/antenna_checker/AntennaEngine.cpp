// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************

#include "AntennaEngine.hpp"

#include "ACModel.hpp"
#include "AntennaChecker.hpp"
#include "AntennaFixer.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "RoutingContext.hpp"
#include "Utility.hpp"
#include "WireEnvIndex.hpp"

#include "IdbDesign.h"
#include "IdbNet.h"
#include "idm.h"

namespace izh {

void AntennaEngine::initInst()
{
  if (_ae_instance == nullptr) {
    _ae_instance = new AntennaEngine();
  }
}

AntennaEngine& AntennaEngine::getInst()
{
  if (_ae_instance == nullptr) {
    ZHLOG.error(Loc::current(), "The instance not initialized!");
    abort();
  }
  return *_ae_instance;
}

void AntennaEngine::destroyInst()
{
  if (_ae_instance != nullptr) {
    delete _ae_instance;
    _ae_instance = nullptr;
  }
}

AFComParam AntennaEngine::initParam(std::map<std::string, std::any>& config_map)
{
  AFComParam param;
  auto consumeString = [&](const std::string& key, std::string& out) {
    auto it = config_map.find(key);
    if (it == config_map.end()) {
      return;
    }
    if (const std::string* value = std::any_cast<std::string>(&it->second)) {
      out = *value;
    }
    config_map.erase(it);
  };
  auto consumeInt = [&](const std::string& key, int32_t& out) {
    auto it = config_map.find(key);
    if (it == config_map.end()) {
      return;
    }
    if (const int32_t* value = std::any_cast<int32_t>(&it->second)) {
      out = *value;
    } else if (const int* value = std::any_cast<int>(&it->second)) {
      out = *value;
    } else if (const std::string* value = std::any_cast<std::string>(&it->second)) {
      try {
        out = std::stoi(*value);
      } catch (...) {
      }
    }
    config_map.erase(it);
  };
  auto consumeBool = [&](const std::string& key, bool& out) {
    auto it = config_map.find(key);
    if (it == config_map.end()) {
      return;
    }
    if (const bool* value = std::any_cast<bool>(&it->second)) {
      out = *value;
    } else if (const int32_t* value = std::any_cast<int32_t>(&it->second)) {
      out = (*value != 0);
    } else if (const int* value = std::any_cast<int>(&it->second)) {
      out = (*value != 0);
    } else if (const std::string* value = std::any_cast<std::string>(&it->second)) {
      out = (*value != "0" && *value != "false" && *value != "False");
    }
    config_map.erase(it);
  };

  std::string report_dir;
  consumeString("report_dir", report_dir);
  consumeString("-antenna_report_dir", report_dir);
  param.set_report_dir(report_dir);

  bool enable_fix = true;
  consumeBool("-enable_antenna_fix", enable_fix);
  param.set_enable_fix(enable_fix);

  int32_t max_iter = 3;
  consumeInt("-antenna_max_iter", max_iter);
  if (max_iter <= 0) {
    max_iter = 1;
  }
  param.set_max_iter(max_iter);

  int32_t search_radius = 0;
  consumeInt("-antenna_search_radius", search_radius);
  param.set_search_radius(search_radius);

  int32_t max_jog = 0;
  consumeInt("-antenna_max_jog", max_jog);
  param.set_max_jog(max_jog);

  std::string diode_cells;
  consumeString("-antenna_diode_cells", diode_cells);
  if (!diode_cells.empty()) {
    std::string normalized = diode_cells;
    for (char& ch : normalized) {
      if (std::string(" \t\r\n,;").find(ch) != std::string::npos) {
        ch = ' ';
      }
    }
    std::stringstream stream(normalized);
    std::string name;
    while (stream >> name) {
      param.get_diode_name_list().push_back(name);
    }
  }

  bool enable_drc = false;
  consumeBool("-antenna_enable_drc", enable_drc);
  param.set_enable_drc(enable_drc);

  config_map.erase("-stage");
  config_map.erase("-resolve_congestion");
  if (!config_map.empty()) {
    ZHLOG.warn(Loc::current(), "The checkAndFixAntenna config has not been consumed yet!");
  }
  return param;
}

AntennaResult AntennaEngine::checkAndFix(std::map<std::string, std::any> config_map)
{
  Monitor monitor;
  ZHLOG.info(Loc::current(), "Starting...");

  AFComParam param = initParam(config_map);
  AntennaChecker::initInst();

  std::map<std::string, std::any> check_map;
  check_map["report_dir"] = param.get_report_dir();
  AntennaResult result = ZHAC.checkToResult(check_map, true);
  AFIterStat first;
  first.iter = 0;
  first.violation_num = result.get_violation_num();
  result.get_iter_stat_list().push_back(first);

  if (!param.get_enable_fix() || result.get_violation_num() == 0) {
    writeFixReport(result, param);
    AntennaChecker::destroyInst();
    ZHLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
    return result;
  }

  idb::IdbDesign* design = nullptr;
  if (dmInst != nullptr && dmInst->get_idb_def_service() != nullptr) {
    design = dmInst->get_idb_def_service()->get_design();
  }
  if (design == nullptr || design->get_net_list() == nullptr) {
    writeFixReport(result, param);
    AntennaChecker::destroyInst();
    return result;
  }

  RoutingContext ctx = RoutingContext::build(design, param);
  WireEnvIndex index;
  index.rebuild(design, ctx);
  AntennaFixer fixer;
  ACModel ac_model;
  ac_model.get_report_dir() = param.get_report_dir();
  ac_model.get_micron_dbu() = ctx.get_micron_dbu();
  ac_model.get_violation_list() = result.get_violation_list();
  ac_model.set_violation_num(result.get_violation_num());

  for (int32_t iter = 1; iter <= param.get_max_iter(); ++iter) {
    AFIterStat stat;
    stat.iter = iter;
    std::vector<ACViolation> current = result.get_violation_list();
    if (current.empty()) {
      break;
    }

    std::unordered_set<std::string> touched;
    std::map<std::string, std::vector<ACViolation>> by_net;
    for (const ACViolation& v : current) {
      by_net[v.net_name].push_back(v);
    }

    for (auto& [net_name, violations] : by_net) {
      idb::IdbNet* net = design->get_net_list()->find_net(net_name);
      if (net == nullptr) {
        continue;
      }
      std::unordered_set<std::string> fixed_pins;
      for (const ACViolation& v : violations) {
        std::string pin_key = v.inst_name + "/" + v.pin_name;
        if (!v.pin_name.empty() && fixed_pins.count(pin_key) > 0) {
          continue;
        }
        AFFixKind kind = fixer.classify(v, ctx);
        bool applied = false;
        if (kind == AFFixKind::kHopUp) {
          applied = fixer.applyHopUp(design, net, v, ctx, index, true, stat);
        }
        if (!applied) {
          applied = fixer.applyDiode(design, net, v, ctx, index, stat, result.get_logged_no_antenna_cell());
        }
        if (applied) {
          touched.insert(net_name);
          if (!v.pin_name.empty()) {
            fixed_pins.insert(pin_key);
          }
        }
      }
    }

    if (touched.empty()) {
      stat.violation_num = result.get_violation_num();
      result.get_iter_stat_list().push_back(stat);
      break;
    }

    index.rebuild(design, ctx);
    std::vector<idb::IdbNet*> nets;
    for (const std::string& name : touched) {
      idb::IdbNet* net = design->get_net_list()->find_net(name);
      if (net != nullptr) {
        nets.push_back(net);
      }
    }
    ZHAC.checkNets(ac_model, nets);
    result.get_violation_list() = ac_model.get_violation_list();
    stat.violation_num = result.get_violation_num();
    result.get_iter_stat_list().push_back(stat);
    ZHLOG.info(Loc::current(), "antenna fix iter ", iter, " remaining violations: ", stat.violation_num);
    if (stat.violation_num == 0) {
      break;
    }
  }

  writeFixReport(result, param);
  AntennaChecker::destroyInst();
  ZHLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
  return result;
}

void AntennaEngine::writeFixReport(const AntennaResult& result, const AFComParam& param) const
{
  if (param.get_report_dir().empty()) {
    return;
  }
  const std::string report_path = param.get_report_dir() + "/antenna_check.rpt";
  std::ofstream rpt(report_path, std::ios::app);
  if (!rpt.is_open()) {
    ZHLOG.warn(Loc::current(), "Cannot open antenna report file: ", report_path);
    return;
  }
  rpt << "========================================================================\n";
  rpt << "                      Antenna Fix Summary                               \n";
  rpt << "========================================================================\n";
  for (const AFIterStat& stat : result.get_iter_stat_list()) {
    rpt << "Iter " << stat.iter << ": violations=" << stat.violation_num << " hop_up=" << stat.hop_up_applied << "/"
        << (stat.hop_up_applied + stat.hop_up_rejected) << " jog=" << stat.jog_applied << "/" << (stat.jog_applied + stat.jog_rejected)
        << " diode=" << stat.diode_applied << "/" << (stat.diode_applied + stat.diode_rejected) << "\n";
  }
  rpt << "Remaining violations: " << result.get_violation_num() << "\n";
  rpt.close();
}

AntennaEngine* AntennaEngine::_ae_instance = nullptr;

}  // namespace izh
