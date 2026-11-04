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
#include "DriverAnalyzer.hpp"

#include "DataManager.hpp"
#include "MJInterface.hpp"
#include "Utility.hpp"

namespace imj {

void DriverAnalyzer::initInst()
{
  if (_da_instance == nullptr) {
    _da_instance = new DriverAnalyzer();
  }
}

DriverAnalyzer& DriverAnalyzer::getInst()
{
  if (_da_instance == nullptr) {
    MJLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_da_instance;
}

void DriverAnalyzer::destroyInst()
{
  delete _da_instance;
  _da_instance = nullptr;
}

void DriverAnalyzer::analyze()
{
  Monitor monitor;
  MJLOG.info(Loc::current(), "Starting...");
  DAModel da_model = initDAModel();
  excludeIOBoundaryNetList(da_model);
  buildGraph(da_model);
  inferPinTypeList(da_model);
  analyzeNetList(da_model);
  writeReport(da_model);
  MJLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

DAModel DriverAnalyzer::initDAModel()
{
  DAModel da_model;
  MJI.wrapDriverDesign(da_model);
  return da_model;
}

void DriverAnalyzer::excludeIOBoundaryNetList(DAModel& da_model)
{
  std::vector<DANet>& net_list = da_model.get_net_list();
  int32_t retained_idx = 0;
  for (int32_t net_idx = 0; net_idx < static_cast<int32_t>(net_list.size()); net_idx++) {
    if (isIOBoundaryNet(net_list[net_idx])) {
      da_model.set_excluded_io_boundary_net_num(da_model.get_excluded_io_boundary_net_num() + 1);
      continue;
    }
    if (retained_idx != net_idx) {
      net_list[retained_idx] = std::move(net_list[net_idx]);
    }
    retained_idx++;
  }
  net_list.resize(retained_idx);
}

bool DriverAnalyzer::isIOBoundaryNet(DANet& net)
{
  std::set<std::string> io_pin_name_set;
  std::set<std::pair<std::string, std::string>> instance_pin_name_set;
  for (DAPin& pin : net.get_pin_list()) {
    if (pin.get_is_io()) {
      io_pin_name_set.insert(pin.get_pin_name());
    } else {
      if (!pin.get_is_pad()) {
        return false;
      }
      instance_pin_name_set.emplace(pin.get_instance_name(), pin.get_pin_name());
    }
  }
  return io_pin_name_set.size() == 1 && instance_pin_name_set.size() == 1;
}

void DriverAnalyzer::buildGraph(DAModel& da_model)
{
  std::map<std::tuple<bool, std::string, std::string>, int32_t> node_idx_map;
  for (int32_t net_idx = 0; net_idx < static_cast<int32_t>(da_model.get_net_list().size()); net_idx++) {
    DANet& net = da_model.get_net_list()[net_idx];
    std::set<int32_t> node_idx_set;
    for (DAPin& pin : net.get_pin_list()) {
      auto [node_iter, inserted] = node_idx_map.emplace(std::make_tuple(pin.get_is_io(), pin.get_instance_name(), pin.get_pin_name()),
                                                        static_cast<int32_t>(da_model.get_node_list().size()));
      if (inserted) {
        DANode node;
        node.set_pin(pin);
        node.set_type(pin.get_type());
        da_model.get_node_list().push_back(node);
      }
      int32_t node_idx = node_iter->second;
      if (node_idx_set.insert(node_idx).second) {
        net.get_node_idx_list().push_back(node_idx);
        da_model.get_node_list()[node_idx].get_net_idx_list().push_back(net_idx);
      }
    }
  }
}

void DriverAnalyzer::inferPinTypeList(DAModel& da_model)
{
  std::queue<int32_t> net_queue;
  std::vector<bool> is_queued_list(da_model.get_net_list().size(), true);
  for (int32_t net_idx = 0; net_idx < static_cast<int32_t>(da_model.get_net_list().size()); net_idx++) {
    net_queue.push(net_idx);
  }
  while (!net_queue.empty()) {
    int32_t net_idx = net_queue.front();
    net_queue.pop();
    DANet& net = da_model.get_net_list()[net_idx];
    updateNetState(da_model, net);
    DAPinType inferred_type = DAPinType::kNone;
    if (net.get_driver_num() == 1) {
      inferred_type = DAPinType::kLoad;
    } else if (net.get_driver_num() == 0 && net.get_unresolved_num() == 1) {
      inferred_type = DAPinType::kDriver;
    }
    if (inferred_type != DAPinType::kNone) {
      for (int32_t node_idx : net.get_node_idx_list()) {
        DANode& node = da_model.get_node_list()[node_idx];
        if (node.get_type() == DAPinType::kDriver || node.get_type() == DAPinType::kLoad) {
          continue;
        }
        node.set_type(inferred_type);
        for (int32_t adjacent_net_idx : node.get_net_idx_list()) {
          if (!is_queued_list[adjacent_net_idx]) {
            net_queue.push(adjacent_net_idx);
            is_queued_list[adjacent_net_idx] = true;
          }
        }
      }
    }
    is_queued_list[net_idx] = false;
  }
}

void DriverAnalyzer::updateNetState(DAModel& da_model, DANet& net)
{
  net.set_driver_num(0);
  net.set_unresolved_num(0);
  for (int32_t node_idx : net.get_node_idx_list()) {
    DAPinType type = da_model.get_node_list()[node_idx].get_type();
    if (type == DAPinType::kDriver) {
      net.set_driver_num(net.get_driver_num() + 1);
    } else if (type != DAPinType::kLoad) {
      net.set_unresolved_num(net.get_unresolved_num() + 1);
    }
  }
}

void DriverAnalyzer::analyzeNetList(DAModel& da_model)
{
  for (DANode& node : da_model.get_node_list()) {
    if (node.isInferred()) {
      da_model.set_inferred_pin_num(da_model.get_inferred_pin_num() + 1);
    }
  }
  for (DANet& net : da_model.get_net_list()) {
    updateNetState(da_model, net);
    if (net.get_driver_num() > 1) {
      da_model.set_multi_driver_net_num(da_model.get_multi_driver_net_num() + 1);
    } else if (net.get_unresolved_num() > 0) {
      da_model.set_unresolved_net_num(da_model.get_unresolved_net_num() + 1);
    } else if (net.get_driver_num() == 0) {
      da_model.set_undriven_net_num(da_model.get_undriven_net_num() + 1);
    }
  }
}

#if 1  // exhibit

void DriverAnalyzer::writeReport(DAModel& da_model)
{
  std::string path = MJDM.getConfig().da_temp_directory_path + "driver_report.rpt";
  std::ofstream* stream = MJUTIL.getOutputFileStream(path);
  *stream << "Design : " << da_model.get_design_name() << "\nSignalNets : " << da_model.get_net_list().size()
          << "\nExcludedPGNets : " << da_model.get_excluded_pg_net_num() << "\nExcludedIOBoundaryNets : " << da_model.get_excluded_io_boundary_net_num()
          << "\nMultiDriverNets : " << da_model.get_multi_driver_net_num() << "\nUnresolvedNets : " << da_model.get_unresolved_net_num()
          << "\nUndrivenNets : " << da_model.get_undriven_net_num() << "\nInferredPins : " << da_model.get_inferred_pin_num()
          << "\n\nINOUT and unknown directions are inferred under the single-driver assumption.\n"
          << "Ambiguous directions remain unresolved; original pin directions are not modified.\n"
          << "Multiple output pins indicate structural drivers; enable exclusivity is not evaluated.\n";
  for (DANet& net : da_model.get_net_list()) {
    bool has_inferred_pin = false;
    for (int32_t node_idx : net.get_node_idx_list()) {
      has_inferred_pin = has_inferred_pin || da_model.get_node_list()[node_idx].isInferred();
    }
    if (net.get_driver_num() == 1 && net.get_unresolved_num() == 0 && !has_inferred_pin) {
      continue;
    }
    std::string status = net.get_driver_num() > 1       ? "MULTI_DRIVER"
                         : net.get_unresolved_num() > 0 ? "UNRESOLVED"
                         : net.get_driver_num() == 0    ? "UNDRIVEN"
                                                        : "SINGLE_DRIVER";
    *stream << "\nNet : " << net.get_name() << "\nStatus : " << status << "\nDrivers : " << net.get_driver_num()
            << "\nUnresolved : " << net.get_unresolved_num() << "\n";
    for (int32_t node_idx : net.get_node_idx_list()) {
      DANode& node = da_model.get_node_list()[node_idx];
      DAPin& pin = node.get_pin();
      if (node.get_type() == DAPinType::kLoad && !node.isInferred()) {
        continue;
      }
      *stream << "  " << (pin.get_is_io() ? "PORT " : "INSTANCE ")
              << (pin.get_is_io() ? pin.get_pin_name() : pin.get_instance_name() + "/" + pin.get_pin_name()) << " " << GetDAPinTypeName()(node.get_type());
      if (node.isInferred()) {
        *stream << " (inferred from " << GetDAPinTypeName()(pin.get_type()) << ")";
      }
      *stream << "\n";
    }
  }
  MJUTIL.closeFileStream(stream);
  if (da_model.get_multi_driver_net_num() > 0 || da_model.get_unresolved_net_num() > 0) {
    MJLOG.warn(Loc::current(), "Multi-driver nets=", da_model.get_multi_driver_net_num(), ", unresolved nets=", da_model.get_unresolved_net_num());
  } else {
    MJLOG.info(Loc::current(), "No multiple drivers found. Undriven nets=", da_model.get_undriven_net_num());
  }
}

#endif

DriverAnalyzer* DriverAnalyzer::_da_instance = nullptr;

}  // namespace imj
