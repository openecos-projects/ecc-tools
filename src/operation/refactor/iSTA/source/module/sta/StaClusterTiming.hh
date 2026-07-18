// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file StaClusterTiming.hh
 * @author shy long (longshy@pcl.ac.cn)
 * @brief The class for macro place cluster timing analysis.
 * @version 0.1
 * @date 2024-05-31
 */

#pragma once
#include <set>
#include <vector>

#include "StaFunc.hh"
#include "string/StrMap.hh"

namespace ista {

/**
 * @brief marco place cluster timing analysis.
 *
 */
class StaClusterTiming : public StaFunc {
 public:
  StaClusterTiming(std::vector<std::set<std::string>> cluster_instances)
      : _cluster_instances(cluster_instances) {}
  ~StaClusterTiming() = default;

  void addHierSubNetlist();
  void buildSubnetlistToInst();
  // for debugging purposes.
  std::vector<int> get_cluster_boundary_net_set() {
    return _cluster_boundary_net_set;
  }

 private:
  void addPortForSubnetlist(Instance& inst, Netlist& subnetlist);

  bool isBoundaryInstance(Instance& inst,
                          std::set<std::string> instance_own_cluster);
  bool isBoundaryNet(Net& net, std::set<std::string> instance_own_cluster);
  void addPortForBoundaryInstance(
      Instance& boundary_inst, std::set<std::string> boundary_inst_own_cluster,
      Netlist& subnetlist);
  void addRemainingInstances(Instance&& instance) {
    _remaining_instances.emplace_back(std::move(instance));
  }

  Net& addRemainingNetsConnectedInst(Net&& net) {
    _remaining_nets_connected_inst.emplace_back(std::move(net));
    Net* the_net = &(_remaining_nets_connected_inst.back());
    const char* net_name = the_net->get_name();
    _str2remainin_net_connected_inst[net_name] = the_net;
    return *the_net;
  }

  Net* findRemainingNetConnectedInst(const char* net_name) const {
    auto found_net = _str2remainin_net_connected_inst.find(net_name);

    if (found_net != _str2remainin_net_connected_inst.end()) {
      return found_net->second;
    }
    return nullptr;
  }

  Port& addRemainingPortsConnectedInst(Port&& port) {
    _remaining_ports_connected_inst.emplace_back(std::move(port));
    Port* the_port = &(_remaining_ports_connected_inst.back());
    return *the_port;
  }

  Net& addRemainingNetsConnectedPort(Net&& net) {
    _remaining_nets_connected_port.emplace_back(std::move(net));
    Net* the_net = &(_remaining_nets_connected_port.back());
    const char* net_name = the_net->get_name();
    _str2remainin_net_connected_port[net_name] = the_net;
    return *the_net;
  }

  Net* findRemainingNetConnectedPort(const char* net_name) const {
    auto found_net = _str2remainin_net_connected_port.find(net_name);

    if (found_net != _str2remainin_net_connected_port.end()) {
      return found_net->second;
    }
    return nullptr;
  }

  Port& addRemainingPortsConnectedPort(Port&& port) {
    _remaining_ports_connected_port.emplace_back(std::move(port));
    Port* the_port = &(_remaining_ports_connected_port.back());
    return *the_port;
  }

  std::vector<std::set<std::string>> _cluster_instances;
  std::list<Instance>
      _remaining_instances;  //!< collection of the cluster's instance, where
                             //!< the cluster only has one instance.
  std::list<Net>
      _remaining_nets_connected_inst;  //!< collection of the net connected to
                                       //!< the cluster's instance, where the
                                       //!< cluster only has one instance.
  std::list<Port>
      _remaining_ports_connected_inst;  //!< collection of the port connected to
                                        //!< the cluster's instance, where the
                                        //!< cluster only has one instance.
  std::list<Net>
      _remaining_nets_connected_port;  //!< collection of the net connected
                                       //!< to ports, not connected to
                                       //!< inst's pin, where the cluster
                                       //!< only has one port.
  std::list<Port>
      _remaining_ports_connected_port;  //!< collection of the port connected
                                        //!< to ports, not connected to
                                        //!< inst's pin, where the cluster
                                        //!< only has one port.
  StrMap<Net*> _str2remainin_net_connected_inst;
  StrMap<Net*> _str2remainin_net_connected_port;
  // for debugging purposes.
  std::set<std::string> _boundary_net_set;
  std::set<std::string> _boundary_pin_set;
  std::vector<std::string> _boundary_net_vector;
  std::vector<std::string> _boundary_pin_vector;
  std::vector<int> _cluster_boundary_net_set;
};

}  // namespace ista