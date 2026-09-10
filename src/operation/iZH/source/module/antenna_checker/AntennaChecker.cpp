// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include "AntennaChecker.hpp"

#include "AntennaPathAnalyzer.hpp"
#include "AntennaResult.hpp"
#include "AntennaRuleEvaluator.hpp"
#include "Utility.hpp"

#include "IdbDesign.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbUnits.h"
#include "idm.h"

namespace izh {

void AntennaChecker::initInst()
{
  if (_ac_instance == nullptr) {
    _ac_instance = new AntennaChecker();
  }
}

AntennaChecker& AntennaChecker::getInst()
{
  if (_ac_instance == nullptr) {
    ZHLOG.error(Loc::current(), "The instance not initialized!");
    abort();
  }

  return *_ac_instance;
}

void AntennaChecker::destroyInst()
{
  if (_ac_instance != nullptr) {
    delete _ac_instance;
    _ac_instance = nullptr;
  }
}

void AntennaChecker::check(std::map<std::string, std::any> config_map)
{
  Monitor monitor;

  ZHLOG.info(Loc::current(), "Starting...");

  ACModel ac_model = initACModel(config_map);
  runACModel(ac_model);
  reportACModel(ac_model);

  ZHLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

AntennaResult AntennaChecker::checkToResult(std::map<std::string, std::any> config_map, bool write_report)
{
  ACModel ac_model = initACModel(config_map);
  runACModel(ac_model);
  if (write_report) {
    reportACModel(ac_model);
  }
  AntennaResult result;
  result.get_violation_list() = ac_model.get_violation_list();
  return result;
}

void AntennaChecker::checkNets(ACModel& ac_model, const std::vector<idb::IdbNet*>& net_list)
{
  idb::IdbDesign* design = nullptr;
  if (dmInst != nullptr) {
    auto* def_service = dmInst->get_idb_def_service();
    if (def_service != nullptr) {
      design = def_service->get_design();
    }
  }
  if (!design || !design->get_layout()) {
    return;
  }

  if (ac_model.get_rule_list().empty()) {
    AntennaRuleEvaluator::initLayers(ac_model, design);
  }

  std::vector<idb::IdbNet*> signal_nets;
  signal_nets.reserve(net_list.size());
  for (idb::IdbNet* net : net_list) {
    if (net && net->is_signal()) {
      signal_nets.push_back(net);
    }
  }

  const int nthreads = std::max(1u, std::thread::hardware_concurrency());
  std::vector<std::vector<ACViolation>> local_violations(nthreads);

#pragma omp parallel for schedule(dynamic, 16) num_threads(nthreads)
  for (long long i = 0; i < static_cast<long long>(signal_nets.size()); ++i) {
    const int tid = omp_get_thread_num();
    AntennaPathAnalyzer::checkNet(ac_model, design, signal_nets[static_cast<size_t>(i)], local_violations[tid]);
  }

  std::vector<ACViolation>& violation_list = ac_model.get_violation_list();
  std::unordered_set<std::string> touched;
  for (idb::IdbNet* net : signal_nets) {
    touched.insert(net->get_net_name());
  }
  violation_list.erase(std::remove_if(violation_list.begin(), violation_list.end(),
                                      [&](const ACViolation& v) { return touched.count(v.net_name) > 0; }),
                       violation_list.end());

  for (auto& v : local_violations) {
    violation_list.insert(violation_list.end(), std::make_move_iterator(v.begin()), std::make_move_iterator(v.end()));
  }

  std::sort(violation_list.begin(), violation_list.end(), [](const ACViolation& a, const ACViolation& b) {
    return std::tie(a.net_name, a.layer_name, a.type, a.ratio, a.threshold, a.lx, a.ly, a.hx, a.hy)
           < std::tie(b.net_name, b.layer_name, b.type, b.ratio, b.threshold, b.lx, b.ly, b.hx, b.hy);
  });
  ac_model.set_violation_num(static_cast<int32_t>(violation_list.size()));
}

ACModel AntennaChecker::initACModel(std::map<std::string, std::any>& config_map)
{
  ACModel ac_model;
  auto it = config_map.find("report_dir");
  if (it != config_map.end()) {
    if (const std::string* dir = std::any_cast<std::string>(&it->second)) {
      ac_model.get_report_dir() = *dir;
    } else {
      ZHLOG.warn(Loc::current(), "config_map[\"report_dir\"] is not a string");
    }

    config_map.erase(it);
  }

  if (!config_map.empty()) {
    ZHLOG.warn(Loc::current(), "The checkAntenna config has not been consumed yet!");
  }

  initDatabaseInfo(ac_model);
  return ac_model;
}

void AntennaChecker::runACModel(ACModel& ac_model)
{
  idb::IdbDesign* design = nullptr;
  if (dmInst != nullptr) {
    auto* def_service = dmInst->get_idb_def_service();
    if (def_service != nullptr) {
      design = def_service->get_design();
    }
  }
  int64_t& signal_net_cnt = ac_model.get_signal_net_cnt();
  std::vector<ACViolation>& violation_list = ac_model.get_violation_list();
  std::atomic<int64_t>& pins_missing_antenna_info = ac_model.get_pins_missing_antenna_info();
  std::atomic<int64_t>& pins_with_gate_area = ac_model.get_pins_with_gate_area();
  std::atomic<int64_t>& comps_without_gate = ac_model.get_comps_without_gate();
  std::atomic<int64_t>& skipped_segments = ac_model.get_skipped_segments();
  std::atomic<int64_t>& conductors_out_of_range = ac_model.get_conductors_out_of_range();
  std::atomic<int64_t>& partial_areas_dropped = ac_model.get_partial_areas_dropped();

  signal_net_cnt = 0;
  violation_list.clear();
  pins_missing_antenna_info.store(0, std::memory_order_relaxed);
  pins_with_gate_area.store(0, std::memory_order_relaxed);
  comps_without_gate.store(0, std::memory_order_relaxed);
  skipped_segments.store(0, std::memory_order_relaxed);
  conductors_out_of_range.store(0, std::memory_order_relaxed);
  partial_areas_dropped.store(0, std::memory_order_relaxed);

  if (!design || !design->get_layout()) {
    return;
  }

  AntennaRuleEvaluator::initLayers(ac_model, design);

  if (ac_model.get_rule_list().empty()) {
    ZHLOG.info(Loc::current(), "No antenna rules defined in technology; skipping antenna check");
    return;
  }

  idb::IdbNetList* net_list = design->get_net_list();
  if (!net_list) {
    return;
  }

  std::vector<idb::IdbNet*> signal_nets;
  for (idb::IdbNet* net : net_list->get_net_list()) {
    if (net && net->is_signal()) {
      signal_nets.push_back(net);
    }
  }

  signal_net_cnt = static_cast<int64_t>(signal_nets.size());

  const int nthreads = std::max(1u, std::thread::hardware_concurrency());
  std::vector<std::vector<ACViolation>> local_violations(nthreads);

#pragma omp parallel for schedule(dynamic, 16) num_threads(nthreads)
  for (long long i = 0; i < static_cast<long long>(signal_nets.size()); ++i) {
    const int tid = omp_get_thread_num();
    AntennaPathAnalyzer::checkNet(ac_model, design, signal_nets[static_cast<size_t>(i)], local_violations[tid]);
  }

  for (auto& v : local_violations) {
    violation_list.insert(violation_list.end(), std::make_move_iterator(v.begin()), std::make_move_iterator(v.end()));
  }

  std::sort(violation_list.begin(), violation_list.end(), [](const ACViolation& a, const ACViolation& b) {
    return std::tie(a.net_name, a.layer_name, a.type, a.ratio, a.threshold, a.lx, a.ly, a.hx, a.hy)
           < std::tie(b.net_name, b.layer_name, b.type, b.ratio, b.threshold, b.lx, b.ly, b.hx, b.hy);
  });
  ac_model.set_violation_num(static_cast<int32_t>(violation_list.size()));

  if (pins_missing_antenna_info.load() > 0) {
    ZHLOG.warn(Loc::current(), pins_missing_antenna_info.load(),
               " instance pins have no antenna gate/diffusion area annotation; affected nets may be under-checked");
  }

  if (!signal_nets.empty() && pins_with_gate_area.load() == 0) {
    ZHLOG.warn(Loc::current(), "No instance pin in ", signal_nets.size(),
               " signal nets provided ANTENNAGATEAREA, so no antenna ratio could be evaluated. "
               "A zero violation count here means the check did not run, not that the design is clean.");
  }

  if (comps_without_gate.load() > 0) {
    ZHLOG.warn(Loc::current(), comps_without_gate.load(),
               " conductor components had no connected gate state at their layer event and were excluded "
               "from damage accumulation");
  }

  if (conductors_out_of_range.load() > 0) {
    ZHLOG.warn(Loc::current(), conductors_out_of_range.load(),
               " pin shapes or wire segments had a layer order outside the technology range and were skipped");
  }

  if (skipped_segments.load() > 0) {
    ZHLOG.warn(Loc::current(), skipped_segments.load(), " segments were neither wire nor via and were skipped");
  }

  if (partial_areas_dropped.load() > 0) {
    ZHLOG.warn(Loc::current(), partial_areas_dropped.load(),
               " pin partial antenna area annotations referenced invalid/unknown layers and were dropped");
  }
}

void AntennaChecker::reportACModel(const ACModel& ac_model)
{
  ZHLOG.info(Loc::current(), "violation count: ", ac_model.get_violation_num());

  writeReport(ac_model);

  ZHLOG.info(Loc::current(), "ZH checkAntenna");
  ZHLOG.info(Loc::current(), "Found ", ac_model.get_violation_num(), " antenna violations");
}

void AntennaChecker::initDatabaseInfo(ACModel& ac_model)
{
  idb::IdbDesign* design = nullptr;
  if (dmInst != nullptr) {
    auto* def_service = dmInst->get_idb_def_service();
    if (def_service != nullptr) {
      design = def_service->get_design();
    }
  }

  if (design && design->get_layout() && design->get_layout()->get_units()) {
    const int dbu = design->get_layout()->get_units()->get_micron_dbu();
    if (dbu > 0) {
      ac_model.get_micron_dbu() = dbu;
    }
  }
}

void AntennaChecker::writeReport(const ACModel& ac_model) const
{
  if (!ac_model.get_report_dir().empty()) {
    const std::string report_path = ac_model.get_report_dir() + "/antenna_check.rpt";
    std::ofstream rpt(report_path);

    if (rpt.is_open()) {
      rpt << "========================================================================\n";
      rpt << "                      Antenna Violations Report                         \n";
      rpt << "========================================================================\n";
      rpt << "Total Violations: " << ac_model.get_violation_num() << "\n\n";

      if (ac_model.get_violation_num() > 0) {
        rpt << "Details:\n";
        rpt << "------------------------------------------------------------------------\n";

        for (const auto& v : ac_model.get_violation_list()) {
          rpt << "Net: " << v.net_name << " | Layer: " << v.layer_name << " | Type: " << acViolationTypeToString(v.type) << "\n"
              << "  Ratio: " << v.ratio << " (Threshold: " << v.threshold << ")\n"
              << "  Location: (" << v.lx << ", " << v.ly << ") to (" << v.hx << ", " << v.hy << ")\n\n";
        }
      }

      rpt.close();
    } else {
      ZHLOG.warn(Loc::current(), "Cannot open antenna report file: ", report_path);
    }
  }
}

AntennaChecker* AntennaChecker::_ac_instance = nullptr;

}  // namespace izh
