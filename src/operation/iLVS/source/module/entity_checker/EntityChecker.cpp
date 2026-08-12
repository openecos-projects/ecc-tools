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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "EntityChecker.hpp"

#include "ECSummary.hpp"
#include "LVSHeader.hpp"
#include "Logger.hpp"
#include "Utility.hpp"

namespace ilvs {

// public

void EntityChecker::initInst()
{
  if (_ec_instance == nullptr) {
    _ec_instance = new EntityChecker();
  }
}

EntityChecker& EntityChecker::getInst()
{
  if (_ec_instance == nullptr) {
    LVSLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_ec_instance;
}

void EntityChecker::destroyInst()
{
  if (_ec_instance != nullptr) {
    delete _ec_instance;
    _ec_instance = nullptr;
  }
}

// function

void EntityChecker::check()
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  ECModel ec_model = initECModel();
  checkIO(ec_model);
  checkInstance(ec_model);
  checkNet(ec_model);
  updateSummary(ec_model);

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

ECModel EntityChecker::initECModel()
{
  Database& database = LVSDM.getDatabase();
  NetlistData& netlist_data = database.get_netlist_data();
  DefData& def_data = database.get_def_data();

  ECModel ec_model;
  ec_model.set_netlist_io_name_list(getComparedIONameList(netlist_data));
  ec_model.set_def_io_name_list(getComparedIONameList(def_data));
  ec_model.set_netlist_instance_name_list(
      std::vector<std::string>(netlist_data.get_instance_name_set().begin(), netlist_data.get_instance_name_set().end()));
  ec_model.set_def_instance_name_list(std::vector<std::string>(def_data.get_instance_name_set().begin(), def_data.get_instance_name_set().end()));
  ec_model.set_netlist_net_name_list(LVSUTIL.getSortedKeyNameList(netlist_data.get_net_map()));
  ec_model.set_def_net_name_list(LVSUTIL.getSortedKeyNameList(def_data.get_net_map()));
  return ec_model;
}

std::vector<std::string> EntityChecker::getComparedIONameList(const DesignData& design_data)
{
  std::vector<std::string> compared_io_name_list;
  for (std::string& io_terminal_name : LVSUTIL.getSortedUniqueList(design_data.get_io_terminal_name_list())) {
    if (!isPowerGroundIO(design_data, io_terminal_name)) {
      compared_io_name_list.push_back(io_terminal_name);
    }
  }
  return compared_io_name_list;
}

bool EntityChecker::isPowerGroundIO(const DesignData& design_data, const std::string& io_terminal_name)
{
  const std::map<std::string, ConnectType>& terminal_connect_type_map = design_data.get_terminal_connect_type_map();
  std::vector<std::string> candidate_terminal_name_list = {io_terminal_name};
  if (LVSUTIL.isIOName(io_terminal_name)) {
    candidate_terminal_name_list.push_back(LVSUTIL.getIOPinName(io_terminal_name));
  } else {
    candidate_terminal_name_list.push_back(LVSUTIL.getIOName(io_terminal_name));
  }
  for (std::string& candidate_terminal_name : candidate_terminal_name_list) {
    auto terminal_connect_type_iter = terminal_connect_type_map.find(candidate_terminal_name);
    if (terminal_connect_type_iter == terminal_connect_type_map.end()) {
      continue;
    }
    ConnectType connect_type = terminal_connect_type_iter->second;
    if (connect_type == ConnectType::kPower || connect_type == ConnectType::kGround) {
      return true;
    }
  }
  return false;
}

void EntityChecker::checkIO(ECModel& ec_model)
{
  std::vector<std::string>& netlist_io_name_list = ec_model.get_netlist_io_name_list();
  std::vector<std::string>& def_io_name_list = ec_model.get_def_io_name_list();
  std::vector<Violation>& violation_list = ec_model.get_violation_list();

  std::vector<std::string> missing_io_name_list = getDifference(netlist_io_name_list, def_io_name_list);
  std::vector<std::string> unexpected_io_name_list = getDifference(def_io_name_list, netlist_io_name_list);
  for (std::string& io_terminal_name : missing_io_name_list) {
    Violation violation;
    violation.set_violation_type(ViolationType::kIO);
    violation.get_terminal_name_list().push_back(LVSUTIL.getString("NETLIST/", io_terminal_name));
    violation_list.push_back(std::move(violation));
  }
  for (std::string& io_terminal_name : unexpected_io_name_list) {
    Violation violation;
    violation.set_violation_type(ViolationType::kIO);
    violation.get_terminal_name_list().push_back(LVSUTIL.getString("DEF/", io_terminal_name));
    violation_list.push_back(std::move(violation));
  }
}

std::vector<std::string> EntityChecker::getDifference(const std::vector<std::string>& first_name_list,
                                                       const std::vector<std::string>& second_name_list)
{
  std::vector<std::string> difference_name_list;
  std::set_difference(first_name_list.begin(), first_name_list.end(), second_name_list.begin(), second_name_list.end(),
                      std::back_inserter(difference_name_list));
  return difference_name_list;
}

void EntityChecker::checkInstance(ECModel& ec_model)
{
  std::vector<std::string>& netlist_instance_name_list = ec_model.get_netlist_instance_name_list();
  std::vector<std::string>& def_instance_name_list = ec_model.get_def_instance_name_list();
  std::vector<Violation>& violation_list = ec_model.get_violation_list();

  std::vector<std::string> missing_instance_name_list = getDifference(netlist_instance_name_list, def_instance_name_list);
  std::vector<std::string> unexpected_instance_name_list = getDifference(def_instance_name_list, netlist_instance_name_list);
  for (std::string& instance_name : missing_instance_name_list) {
    Violation violation;
    violation.set_violation_type(ViolationType::kInstance);
    violation.set_instance_name(LVSUTIL.getString("NETLIST/", instance_name));
    violation_list.push_back(std::move(violation));
  }
  for (std::string& instance_name : unexpected_instance_name_list) {
    Violation violation;
    violation.set_violation_type(ViolationType::kInstance);
    violation.set_instance_name(LVSUTIL.getString("DEF/", instance_name));
    violation_list.push_back(std::move(violation));
  }
}

void EntityChecker::checkNet(ECModel& ec_model)
{
  Database& database = LVSDM.getDatabase();
  NetlistData& netlist_data = database.get_netlist_data();
  DefData& def_data = database.get_def_data();
  std::vector<std::string>& netlist_net_name_list = ec_model.get_netlist_net_name_list();
  std::vector<std::string>& def_net_name_list = ec_model.get_def_net_name_list();
  std::vector<Violation>& violation_list = ec_model.get_violation_list();

  std::vector<std::string> missing_net_name_list = getDifference(netlist_net_name_list, def_net_name_list);
  std::vector<std::string> unexpected_net_name_list = getDifference(def_net_name_list, netlist_net_name_list);
  for (std::string& net_name : missing_net_name_list) {
    Violation violation;
    violation.set_violation_type(ViolationType::kNet);
    violation.set_net_name(LVSUTIL.getString("NETLIST/", net_name));
    violation.set_terminal_name_list(netlist_data.get_net_map().at(net_name).get_terminal_name_list());
    violation_list.push_back(std::move(violation));
  }
  for (std::string& net_name : unexpected_net_name_list) {
    Violation violation;
    violation.set_violation_type(ViolationType::kNet);
    violation.set_net_name(LVSUTIL.getString("DEF/", net_name));
    violation.set_terminal_name_list(def_data.get_net_map().at(net_name).get_terminal_name_list());
    violation_list.push_back(std::move(violation));
  }
  for (std::string& net_name : netlist_net_name_list) {
    auto def_net_iter = def_data.get_net_map().find(net_name);
    if (def_net_iter == def_data.get_net_map().end()) {
      continue;
    }
    std::vector<std::string> netlist_terminal_name_list =
        LVSUTIL.getSortedUniqueList(netlist_data.get_net_map().at(net_name).get_terminal_name_list());
    std::vector<std::string> def_terminal_name_list = LVSUTIL.getSortedUniqueList(def_net_iter->second.get_terminal_name_list());
    if (netlist_terminal_name_list == def_terminal_name_list) {
      continue;
    }
    Violation violation;
    violation.set_violation_type(ViolationType::kNet);
    violation.set_net_name(net_name);
    for (std::string& terminal_name : getDifference(netlist_terminal_name_list, def_terminal_name_list)) {
      violation.get_terminal_name_list().push_back(LVSUTIL.getString("NETLIST/", terminal_name));
    }
    for (std::string& terminal_name : getDifference(def_terminal_name_list, netlist_terminal_name_list)) {
      violation.get_terminal_name_list().push_back(LVSUTIL.getString("DEF/", terminal_name));
    }
    violation_list.push_back(std::move(violation));
  }
}

void EntityChecker::updateSummary(ECModel& ec_model)
{
  ECSummary& ec_summary = LVSDM.getDatabase().get_summary().ec_summary;
  ec_summary.reset();

  ec_summary.netlist_io_num = ec_model.get_netlist_io_name_list().size();
  ec_summary.def_io_num = ec_model.get_def_io_name_list().size();
  ec_summary.netlist_instance_num = ec_model.get_netlist_instance_name_list().size();
  ec_summary.def_instance_num = ec_model.get_def_instance_name_list().size();
  ec_summary.netlist_net_num = ec_model.get_netlist_net_name_list().size();
  ec_summary.def_net_num = ec_model.get_def_net_name_list().size();

  std::vector<Violation>& violation_list = ec_model.get_violation_list();
  for (Violation& violation : violation_list) {
    if (violation.get_violation_type() == ViolationType::kIO) {
      ec_summary.io_difference_num++;
    } else if (violation.get_violation_type() == ViolationType::kInstance) {
      ec_summary.instance_difference_num++;
    } else if (violation.get_violation_type() == ViolationType::kNet) {
      ec_summary.net_difference_num++;
    }
  }
  ec_summary.violation_list = std::move(violation_list);
}

// private

EntityChecker* EntityChecker::_ec_instance = nullptr;

}  // namespace ilvs
