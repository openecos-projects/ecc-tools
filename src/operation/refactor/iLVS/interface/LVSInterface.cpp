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
#include "LVSInterface.hpp"

#include "DataManager.hpp"
#include "LVSChecker.hpp"
#include "LVSReporter.hpp"
#include "LVSSnapshotIO.hpp"
#include "NetlistExtractor.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"
#include "idm.h"

namespace ilvs {

// public

LVSInterface& LVSInterface::getInst()
{
  if (_lvs_interface_instance == nullptr) {
    _lvs_interface_instance = new LVSInterface();
  }
  return *_lvs_interface_instance;
}

void LVSInterface::destroyInst()
{
  if (_lvs_interface_instance != nullptr) {
    delete _lvs_interface_instance;
    _lvs_interface_instance = nullptr;
  }
}

#if 1  // 外部调用LVS的API

#if 1  // iLVS

void LVSInterface::initLVS(std::map<std::string, std::any> config_map)
{
  Logger::initInst();
  // clang-format off
  LVSLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  LVSLOG.info(Loc::current(), "______________    _________    _____________________________________  ");
  LVSLOG.info(Loc::current(), "___(_)__  /__ |  / /_  ___/    __  ___/__  __/__    |__  __ \\__  __/ ");
  LVSLOG.info(Loc::current(), "__  /__  / __ | / /_____ \\     _____ \\__  /  __  /| |_  /_/ /_  /   ");
  LVSLOG.info(Loc::current(), "_  / _  /____ |/ / ____/ /     ____/ /_  /   _  ___ |  _, _/_  /      ");
  LVSLOG.info(Loc::current(), "/_/  /_____/____/  /____/      /____/ /_/    /_/  |_/_/ |_| /_/       ");
  LVSLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  LVSLOG.printLogFilePath();
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  DataManager::initInst();
  LVSDM.input(config_map);

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void LVSInterface::runLVS()
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  Database& database = LVSDM.getDatabase();
  if (!database.has_netlist() || !database.has_def()) {
    LVSLOG.error(Loc::current(), "run_lvs requires iLVS database data after def_init or read_lvs!");
  }

  LVSChecker::initInst();
  CheckResult& check_result = database.get_check_result();
  const Netlist& netlist = database.get_netlist();
  const Netlist& def = database.get_def();
  check_result = LVSLC.check(netlist, def);
  LVSChecker::destroyInst();

  LVSReporter::initInst();
  const std::vector<fort::char_table> summary_table_list = LVSLR.getSummaryTableList(check_result, netlist, def);
  LVSLR.report(check_result, netlist, def, database.get_report_directory_path());
  for (const fort::char_table& summary_table : summary_table_list) {
    LVSUTIL.printTableList({summary_table});
  }
  LVSReporter::destroyInst();

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void LVSInterface::writeNetlist(const std::string& file_path)
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  if (idb_design == nullptr) {
    LVSLOG.error(Loc::current(), "write_lvs_netlist requires a Verilog-backed IDB design!");
  }
  if (dmInst->get_config().get_verilog_path().empty()) {
    LVSLOG.error(Loc::current(), "write_lvs_netlist requires verilog_init before iLVS snapshot extraction!");
  }
  NetlistExtractor::initInst();
  LVSSnapshotIO::initInst();
  Netlist netlist = LVSNE.extractLogical(idb_design);
  std::string error_message;
  if (!LVSSIO.write(netlist, LVSSnapshotType::kLogical, file_path, error_message)) {
    LVSLOG.error(Loc::current(), "Failed to write logical iLVS snapshot '", file_path, "': ", error_message);
  }
  LVSLOG.info(Loc::current(), "Wrote logical iLVS snapshot '", file_path, "' with ", netlist.net_map.size(), " nets.");
  LVSSnapshotIO::destroyInst();
  NetlistExtractor::destroyInst();

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void LVSInterface::writeDef(const std::string& file_path)
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  if (idb_design == nullptr) {
    LVSLOG.error(Loc::current(), "write_lvs_def requires a DEF-backed IDB design!");
  }
  if (dmInst->get_config().get_def_path().empty()) {
    LVSLOG.error(Loc::current(), "write_lvs_def requires def_init before iLVS snapshot extraction!");
  }
  NetlistExtractor::initInst();
  LVSSnapshotIO::initInst();
  Netlist netlist = LVSNE.extractPhysical(idb_design);
  std::string error_message;
  if (!LVSSIO.write(netlist, LVSSnapshotType::kPhysical, file_path, error_message)) {
    LVSLOG.error(Loc::current(), "Failed to write physical iLVS snapshot '", file_path, "': ", error_message);
  }
  LVSLOG.info(Loc::current(), "Wrote physical iLVS snapshot '", file_path, "' with ", netlist.net_map.size(), " nets and ",
              netlist.physical_graph.node_num, " graph nodes.");
  LVSSnapshotIO::destroyInst();
  NetlistExtractor::destroyInst();

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void LVSInterface::readSnapshots(const std::string& netlist_file_path, const std::string& def_file_path)
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  LVSSnapshotIO::initInst();
  Netlist netlist;
  Netlist def;
  std::string error_message;
  if (!LVSSIO.read(netlist_file_path, LVSSnapshotType::kLogical, netlist, error_message)) {
    LVSLOG.error(Loc::current(), "Failed to read logical iLVS snapshot '", netlist_file_path, "': ", error_message);
  }
  if (!LVSSIO.read(def_file_path, LVSSnapshotType::kPhysical, def, error_message)) {
    LVSLOG.error(Loc::current(), "Failed to read physical iLVS snapshot '", def_file_path, "': ", error_message);
  }
  if (netlist.design_name.empty() || def.design_name.empty()) {
    LVSLOG.error(Loc::current(), "iLVS snapshots must both contain a design name!");
  }
  if (netlist.design_name != def.design_name) {
    LVSLOG.error(Loc::current(), "iLVS snapshot design names differ: netlist='", netlist.design_name, "' def='", def.design_name, "'!");
  }

  Database& database = LVSDM.getDatabase();
  database.set_netlist(std::move(netlist));
  database.set_def(std::move(def));
  LVSLOG.info(Loc::current(), "Loaded iLVS snapshots: netlist_nets=", database.get_netlist().net_map.size(), " def_nets=",
              database.get_def().net_map.size(), " def_graph_nodes=", database.get_def().physical_graph.node_num, ".");
  LVSSnapshotIO::destroyInst();

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void LVSInterface::destroyLVS()
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  LVSDM.output();
  DataManager::destroyInst();

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());

  LVSLOG.printLogFilePath();
  // clang-format off
  LVSLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  LVSLOG.info(Loc::current(), "______________    _________    _____________________   _____________________  __  ");
  LVSLOG.info(Loc::current(), "___(_)__  /__ |  / /_  ___/    ___  ____/___  _/__  | / /___  _/_  ___/__  / / /  ");
  LVSLOG.info(Loc::current(), "__  /__  / __ | / /_____ \\     __  /_    __  / __   |/ / __  / _____ \\__  /_/ / ");
  LVSLOG.info(Loc::current(), "_  / _  /____ |/ / ____/ /     _  __/   __/ /  _  /|  / __/ /  ____/ /_  __  /    ");
  LVSLOG.info(Loc::current(), "/_/  /_____/____/  /____/      /_/      /___/  /_/ |_/  /___/  /____/ /_/ /_/     ");
  LVSLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  Logger::destroyInst();
}

#endif

#endif

#if 1  // LVS调用外部的API

#if 1  // TopData

#if 1  // input

void LVSInterface::input(std::map<std::string, std::any>& config_map)
{
  wrapConfig(config_map);
}

void LVSInterface::wrapConfig(std::map<std::string, std::any>& config_map)
{
  LVSDM.getConfig().temp_directory_path = LVSUTIL.getConfigValue<std::string>(config_map, "-temp_directory_path", "./lvs_temp_directory");
  LVSDM.getConfig().thread_number = LVSUTIL.getConfigValue<int32_t>(config_map, "-thread_number", 128);
  omp_set_num_threads(std::max(LVSDM.getConfig().thread_number, 1));
}

void LVSInterface::wrapDatabase()
{
  if (dmInst->get_config().get_def_path().empty()) {
    return;
  }

  idb::IdbDesign* netlist_idb_design = dmInst->get_netlist_idb_design();
  idb::IdbDesign* def_idb_design = dmInst->get_def_idb_design();
  if (netlist_idb_design == nullptr || def_idb_design == nullptr) {
    LVSLOG.error(Loc::current(), "Direct iLVS requires both logical and physical IDB design views!");
  }

  NetlistExtractor::initInst();
  Netlist netlist = LVSNE.extractLogical(netlist_idb_design);
  Netlist def = LVSNE.extractPhysical(def_idb_design);
  NetlistExtractor::destroyInst();
  if (netlist.design_name.empty() || def.design_name.empty()) {
    LVSLOG.error(Loc::current(), "Direct iLVS IDB views must both contain a design name!");
  }
  if (netlist.design_name != def.design_name) {
    LVSLOG.error(Loc::current(), "Direct iLVS IDB design names differ: netlist='", netlist.design_name, "' def='", def.design_name, "'!");
  }

  Database& database = LVSDM.getDatabase();
  database.set_netlist(std::move(netlist));
  database.set_def(std::move(def));
  if (netlist_idb_design == def_idb_design) {
    LVSLOG.info(Loc::current(), "Using the temporary shared IDB design for both netlist and DEF views.");
  }
  LVSLOG.info(Loc::current(), "Wrapped direct iLVS IDB views: netlist_nets=", database.get_netlist().net_map.size(), " def_nets=",
              database.get_def().net_map.size(), " def_graph_nodes=", database.get_def().physical_graph.node_num, ".");
}

#endif

#if 1  // output

void LVSInterface::output()
{
}

#endif

#endif

#endif

// private

LVSInterface* LVSInterface::_lvs_interface_instance = nullptr;

}  // namespace ilvs
