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
 * @file StaClusterTiming.cc
 * @author shy long (longshy@pcl.ac.cn)
 * @brief The class for macro place cluster timing analysis.
 * @version 0.1
 * @date 2024-05-31
 */

#include "StaClusterTiming.hh"

namespace ista {

namespace {

std::optional<unsigned> findSourcePortBusIndex(Port& source_port) {
  auto* source_bus = source_port.get_port_bus();
  if (!source_bus) {
    return std::nullopt;
  }

  auto [bus_name, index] = Str::matchBusName(source_port.get_name());
  if (index) {
    return static_cast<unsigned>(*index);
  }

  for (unsigned bus_index = 0; bus_index < source_bus->get_size(); ++bus_index) {
    if (source_bus->getPort(bus_index) == &source_port) {
      return bus_index;
    }
  }

  return std::nullopt;
}

void rebindSubnetPortBus(Netlist& subnetlist, Port& subnet_port,
                         Port& source_port) {
  auto* source_bus = source_port.get_port_bus();
  if (!source_bus) {
    return;
  }

  auto bus_index = findSourcePortBusIndex(source_port);
  if (!bus_index) {
    return;
  }

  auto* subnet_bus = subnetlist.findPortBus(source_bus->get_name());
  if (!subnet_bus) {
    PortBus new_bus(source_bus->get_name(), source_bus->get_left(),
                    source_bus->get_right(), source_bus->get_size(),
                    source_bus->get_port_dir());
    subnet_bus = &subnetlist.addPortBus(std::move(new_bus));
  }

  subnet_bus->addPort(*bus_index, &subnet_port);
}

Port* findOrCopySubnetPort(Netlist& subnetlist, Port& source_port) {
  if (auto* existing_port = subnetlist.findPort(source_port.get_name())) {
    rebindSubnetPortBus(subnetlist, *existing_port, source_port);
    return existing_port;
  }

  Port new_port(source_port);
  auto& created_port = subnetlist.addPort(std::move(new_port));
  rebindSubnetPortBus(subnetlist, created_port, source_port);
  return &created_port;
}

void connectObjectToSubnetNet(Net& subnet_net, DesignObject& object) {
  if (auto* current_net = object.get_net();
      current_net && current_net != &subnet_net &&
      current_net->isNetPinPort(&object)) {
    current_net->removePinPort(&object);
  }

  if (!subnet_net.isNetPinPort(&object)) {
    subnet_net.addPinPort(&object);
  } else {
    object.set_net(&subnet_net);
  }
}

const char* getSubnetPortNetName(Port& subnet_port) {
  if (auto* subnet_net = subnet_port.get_net()) {
    return subnet_net->get_name();
  }

  return subnet_port.get_name();
}

}  // namespace

/**
 * @brief according to the clusters to add hierarchical sub netlists.
 *
 */
void StaClusterTiming::addHierSubNetlist() {
  auto* design_netlist = getSta()->get_netlist();
  std::vector<Netlist*> hier_sub_netlists;
  int cluster_index = 1;
  for (auto& cluster_instance : _cluster_instances) {
    if (cluster_instance.size() == 1) {
      ++cluster_index;
      continue;
    }
    // _netlist.set_name(design_name);
    auto* sub_netlist = new Netlist();
    std::string design_name = "cluster" + std::to_string(cluster_index);
    sub_netlist->set_name(design_name.c_str());

    for (auto& instance_name : cluster_instance) {
      auto* instance = design_netlist->findInstance(instance_name.c_str());
      // the cluster_instance contains the port.
      if (!instance) {
        auto* port = design_netlist->findPort(instance_name.c_str());
        if (!port) {
          LOG_INFO << "Physical-use port, don't connect net";
          continue;
        }
        // LOG_FATAL_IF(!port) << "Can not find inst/port according to
        // cluster.";
        auto* port_connect_net = port->get_net();
        if (port_connect_net->isConnectedToAllPorts()) {
          const char* port_connect_net_name = port_connect_net->get_name();
          auto* the_net = findRemainingNetConnectedPort(port_connect_net_name);
          if (the_net) {
            auto& new_port = addRemainingPortsConnectedPort(
                Port(port->get_name(), dynamic_cast<Port*>(port)->get_port_dir()));
            the_net->addPinPort(&new_port);
          } else {
            auto& new_net =
                addRemainingNetsConnectedPort(Net(port_connect_net_name));
            auto& new_port = addRemainingPortsConnectedPort(
                Port(port->get_name(), dynamic_cast<Port*>(port)->get_port_dir()));
            new_net.addPinPort(&new_port);
            // the_net = &new_net;
          }
        } else {
          continue;
        }

        continue;
      }

      Instance new_inst(*instance);

      bool is_boundary_instance = isBoundaryInstance(new_inst, cluster_instance);
      if (is_boundary_instance) {
        addPortForBoundaryInstance(new_inst, cluster_instance, *sub_netlist);
        sub_netlist->addInstance(std::move(new_inst));
      } else {
        addPortForSubnetlist(new_inst, *sub_netlist);
        sub_netlist->addInstance(std::move(new_inst));
      }
    }
    hier_sub_netlists.emplace_back(sub_netlist);

    _cluster_boundary_net_set.push_back(_boundary_net_set.size());
    _boundary_net_set.clear();
    _boundary_pin_set.clear();
    _boundary_net_vector.clear();
    _boundary_pin_vector.clear();

    cluster_index++;
    // if (cluster_index == 2) {
    //   break;
    // }
  }
  design_netlist->set_hier_sub_netlists(hier_sub_netlists);

  bool debug_one_instance_cluster = true;
  if (debug_one_instance_cluster) {
    for (auto& cluster_instance : _cluster_instances) {
      if (cluster_instance.size() == 1) {
        for (auto& instance_name : cluster_instance) {
          auto* instance = design_netlist->findInstance(instance_name.c_str());
          // the cluster_instance contains the port.
          if (!instance) {
            auto* port = design_netlist->findPort(instance_name.c_str());
            LOG_FATAL_IF(!port)
                << "Can not find inst/port according to cluster.";
            auto* port_connect_net = port->get_net();
            if (port_connect_net->isConnectedToAllPorts()) {
              const char* port_connect_net_name = port_connect_net->get_name();
              auto* the_net =
                  findRemainingNetConnectedPort(port_connect_net_name);
              if (the_net) {
                auto& new_port = addRemainingPortsConnectedPort(
                    Port(port->get_name(),
                         dynamic_cast<Port*>(port)->get_port_dir()));
                the_net->addPinPort(&new_port);
              } else {
                auto& new_net =
                    addRemainingNetsConnectedPort(Net(port_connect_net_name));
                auto& new_port = addRemainingPortsConnectedPort(
                    Port(port->get_name(),
                         dynamic_cast<Port*>(port)->get_port_dir()));
                new_net.addPinPort(&new_port);
                // the_net = &new_net;
              }
            } else {
              continue;
            }

            continue;
          }
          Instance new_inst(*instance);

          Pin* pin;
          FOREACH_INSTANCE_PIN(&new_inst, pin) {
            auto* net = pin->get_net();
            if (!net) {
              continue;
            }
            const char* net_name = net->get_name();
            auto* the_net = findRemainingNetConnectedInst(net_name);
            if (!the_net) {
              auto& new_net = addRemainingNetsConnectedInst(Net(net_name));
              if (net->isClockNet()) {
                new_net.set_is_clock_net();
              }
              auto& pin_ports = net->get_pin_ports();
              for (const auto& pin_port : pin_ports) {
                // std::string own_instance_name("");
                // if (pin_port->isPin()) {
                //   own_instance_name =
                //       pin_port->get_own_instance()->getFullName();
                // }

                if (pin_port->isPort()) {
                  auto& new_port = addRemainingPortsConnectedInst(
                      Port(pin_port->get_name(),
                           dynamic_cast<Port*>(pin_port)->get_port_dir()));
                  new_net.addPinPort(&new_port);
                  // } else if (pin_port->isPin() &&
                  //            Str::equal(new_inst.get_name(),
                  //                       own_instance_name.c_str())) {
                  // new_net.addPinPort(pin) not new_net.addPinPort(pin_port);
                  // because net point to the original net.
                }
              }
              new_net.addPinPort(pin);
              the_net = &new_net;
              pin->set_net(the_net);
            } else {
              the_net->addPinPort(pin);
              pin->set_net(the_net);
            }
          }

          addRemainingInstances(std::move(new_inst));
        }
      }
    }
  }
}
// namespace ista

/**
 * @brief builid subnetlists to instance of the top netlist.
 *
 */
void StaClusterTiming ::buildSubnetlistToInst() {
  auto* design_netlist = getSta()->get_netlist();
  getSta()->resetAllRcNet();
  design_netlist->reset();

  auto hier_sub_netlists = design_netlist->get_hier_sub_netlists();
  for (const auto& hier_sub_netlist : hier_sub_netlists) {
    const char* liberty_cell_name = hier_sub_netlist->get_name();
    auto* inst_cell = getSta()->findLibertyCell(liberty_cell_name);
    // inst_name is same with liberty_cell_name.
    const char* inst_name = liberty_cell_name;
    Instance inst(inst_name, inst_cell);
    Port* port;
    FOREACH_PORT(hier_sub_netlist, port) {
      const char* port_name = port->get_name();
      // consider hier_subnetlist's port is LibertyPort,not consider
      // hier_subnetlist's port is LibertyPortBus. as the current ETM
      // generated model does not include the pin is LibertyPortBus.
      auto* library_port_or_port_bus =
          inst_cell->get_cell_port_or_port_bus(port_name);

      if (!library_port_or_port_bus->isLibertyPortBus()) {
        auto* library_port = dynamic_cast<LibPort*>(library_port_or_port_bus);
        std::string pin_name = port_name;
        auto* inst_pin = inst.addPin(pin_name.c_str(), library_port);

        // addNet
        if (port->get_is_virtual_port()) {
          const char* net_name = port_name;
          Net* the_net = design_netlist->findNet(net_name);
          if (the_net) {
            the_net->addPinPort(inst_pin);
          } else {
            // DLOG_INFO << "create net " << net_name;
            auto& created_net = design_netlist->addNet(Net(net_name));

            created_net.addPinPort(inst_pin);
            the_net = &created_net;
          }

        } else {
          // if port is not virtual port,design netlist need add net/port
          // with the original boundary-net name captured on the copied port.
          const char* net_name = getSubnetPortNetName(*port);
          Net* the_net = design_netlist->findNet(net_name);
          if (the_net) {
            the_net->addPinPort(inst_pin);
          } else {
            // DLOG_INFO << "create net " << net_name;
            auto& created_net = design_netlist->addNet(Net(net_name));

            created_net.addPinPort(inst_pin);
            the_net = &created_net;
          }
          auto* top_port = findOrCopySubnetPort(*design_netlist, *port);
          connectObjectToSubnetNet(*the_net, *top_port);
        }

      } else {
        LOG_INFO_FIRST_N(5)
            << "the ETM generated timing modle has LibertyPotBus.";
      }
    }
    design_netlist->addInstance(std::move(inst));
  }

  // when cluster only have one instance.
  for (auto& remaining_instance : _remaining_instances) {
    for (auto& pin : remaining_instance.get_pins()) {
      auto& net_between_cluster = pin->get_net_name_between_clusters();
      if (!net_between_cluster.empty()) {
        auto& net_name_between_clusters = pin->get_net_name_between_clusters();
        Net* net_between_clusters =
            design_netlist->findNet(net_name_between_clusters.c_str());
        LOG_FATAL_IF(!net_between_clusters);
        pin->set_net(net_between_clusters);
        net_between_clusters->addPinPort(pin.get());

      } else {
        auto* connect_net = pin->get_net();
        if (!connect_net) {
          continue;
        }
        // for debugging purpose.
        // auto pin_name = pin->getFullName();
        Net* the_net = design_netlist->findNet(connect_net->get_name());
        if (the_net) {
          connect_net = the_net;
          pin->set_net(the_net);
        } else {
          {
            Net& ret_net = design_netlist->addNet(std::move(*connect_net));
            connect_net = &ret_net;
            pin->set_net(connect_net);
          }
          for (auto& pin_port : connect_net->get_pin_ports()) {
            if (pin_port->isPort()) {
              auto* the_port = design_netlist->findPort(pin_port->get_name());
              if (!the_port) {
                // transfer ownership of port to design_netlist and get the
                // reference of ret_port.
                Port& ret_port = design_netlist->addPort(
                    std::move((*dynamic_cast<Port*>(pin_port))));
                // Reset the port pointer in connect_net to the new pointer
                // "&ret_port"
                pin_port = &ret_port;
              }
            }
          }
        }
      }
    }
    design_netlist->addInstance(std::move(remaining_instance));
  }

  // when cluster only have one port,and the port connects to the net(the net is
  // isConnectedToAllPorts)
  for (auto& remaining_net_connected_port : _remaining_nets_connected_port) {
    Net* the_net =
        design_netlist->findNet(remaining_net_connected_port.get_name());
    if (!the_net) {
      Net& ret_net =
          design_netlist->addNet(std::move(remaining_net_connected_port));
      for (auto& pin_port : ret_net.get_pin_ports()) {
        if (pin_port->isPort()) {
          auto* the_port = design_netlist->findPort(pin_port->get_name());
          if (!the_port) {
            Port& ret_port = design_netlist->addPort(
                std::move((*dynamic_cast<Port*>(pin_port))));
            pin_port = &ret_port;
          }
        }
      }
    }
  }
}

/**
 * @brief add port for the subnetlist.
 *
 * @param inst
 * @param subnetlist
 */
void StaClusterTiming::addPortForSubnetlist(Instance& inst,
                                            Netlist& subnetlist) {
  for (auto& pin : inst.get_pins()) {
    Net* connect_net = pin->get_net();
    if (!connect_net) {
      LOG_INFO << "pin " << pin->getFullName() << " connect net is not exist";
      continue;
    }
    // subnetlist addNet.
    Net* the_net = subnetlist.findNet(connect_net->get_name());

    /**pin update the connecte net, connect the new net.**/
    if (!the_net) {
      const char* net_name = connect_net->get_name();
      Net new_net = Net(net_name);

      // for debugging purposes
      // auto& pin_ports = connect_net->get_pin_ports();
      // for (auto& pin_port : pin_ports) {
      //   LOG_INFO << "Debug:pin_port " << pin_port->get_name() << " "
      //            << pin_port;
      // }

      auto& created_net = subnetlist.addNet(std::move(new_net));
      the_net = &created_net;
    }
    connectObjectToSubnetNet(*the_net, *pin);

    // subnetlist addPort.
    auto& pin_ports = connect_net->get_pin_ports();
    for (auto& pin_port : pin_ports) {
      if (pin_port->isPort()) {
        Port* pin_port1 = dynamic_cast<Port*>(pin_port);
        auto* created_port = findOrCopySubnetPort(subnetlist, *pin_port1);
        connectObjectToSubnetNet(*the_net, *created_port);
      }
    }

    /**pin does not update the connecte net, connect the design netlist's
     * net.**/
    // if (!the_net) {
    //   Net new_net = Net(*connect_net);
    //   subnetlist.addNet(std::move(new_net));
    // }
    // // subnetlist addPort.
    // auto& pin_ports = connect_net->get_pin_ports();
    // for (auto& pin_port : pin_ports) {
    //   if (pin_port->isPort()) {
    //     // Port* pin_port1 = dynamic_cast<Port*>(pin_port);
    //     // subnetlist.addPort(std::move(*pin_port1));
    //     Port* pin_port1 = dynamic_cast<Port*>(pin_port);
    //     Port new_port = Port(*pin_port1);
    //     subnetlist.addPort(std::move(new_port));
    //   }
    // }
  }
}

/**
 * @brief judge whether a instance in a cluster is connected to another
 * instance in another cluster.
 *
 * @param inst
 * @param instance_own_cluster
 * @param cluster_instances
 * @return true
 * @return false
 */
bool StaClusterTiming::isBoundaryInstance(
    Instance& inst, std::set<std::string> instance_own_cluster) {
  bool is_boundary_instance = false;

  for (auto& pin : inst.get_pins()) {
    Net* connect_net = pin->get_net();
    if (!connect_net) {
      LOG_INFO << "pin " << pin->getFullName() << " connect net is not exist";
      continue;
    }

    auto& pin_ports = connect_net->get_pin_ports();
    for (auto& pin_port : pin_ports) {
      if (pin_port->isPort() ||
          (pin_port->isPin() && dynamic_cast<Pin*>(pin_port)->getFullName() ==
                                    pin.get()->getFullName())) {
        continue;
      }
      std::string own_instance_name =
          pin_port->get_own_instance()->getFullName();
      if (!instance_own_cluster.contains(own_instance_name)) {
        // std::cout << "pin: " << pin->getFullName()
        //           << ";connected net: " << connect_net->get_name() <<
        //           std::endl;
        // for debugging purposes.
        _boundary_net_set.insert(connect_net->get_name());
        _boundary_pin_set.insert(pin->getFullName());
        _boundary_net_vector.emplace_back(connect_net->get_name());
        _boundary_pin_vector.emplace_back(pin->getFullName());
        is_boundary_instance = true;
        break;
      }
    }
    // if (is_boundary_instance) {
    //   break;
    // }
  }

  return is_boundary_instance;
}

/**
 * @brief judge whether a net is boundary net.
 *
 *
 * @param inst
 * @param instance_own_cluster
 * @param cluster_instances
 * @return true
 * @return false
 */
bool StaClusterTiming::isBoundaryNet(
    Net& net, std::set<std::string> instance_own_cluster) {
  bool is_boundary_net = false;
  auto& pin_ports = net.get_pin_ports();
  for (auto* pin_port : pin_ports) {
    if (pin_port->isPort()) {
      continue;
    } else {  // for pin.
      std::string own_instance_name =
          pin_port->get_own_instance()->getFullName();
      if (!instance_own_cluster.contains(own_instance_name)) {
        is_boundary_net = true;
        break;
      }
    }
  }
  return is_boundary_net;
}

/**
 * @brief add the virtual port for the boundary instance.
 *
 * @param boundary_inst
 * @param boundary_inst_own_cluster
 * @param subnetlist
 */
void StaClusterTiming::addPortForBoundaryInstance(
    Instance& boundary_inst, std::set<std::string> boundary_inst_own_cluster,
    Netlist& subnetlist) {
  // if (Str::equal(boundary_inst.get_name(), "u3") == true) {
  //   for (auto& pin : boundary_inst.get_pins()) {
  //     auto the_found_pin = boundary_inst.getPin(pin->get_name());
  //     LOG_INFO << pin << pin.get() << ":" << pin->get_name();
  //   }
  // }
  // net with multi-load pin shouold build one port.
  for (auto& pin : boundary_inst.get_pins()) {
    Net* connect_net = pin->get_net();
    if (!connect_net) {
      LOG_INFO << "pin " << pin->getFullName() << " connect net is not exist";
      continue;
    }
    auto& pin_ports = connect_net->get_pin_ports();
    unsigned is_boundary_net =
        isBoundaryNet(*connect_net, boundary_inst_own_cluster);

    std::list<DesignObject*> boundary_inst_remaining_pins;
    std::list<Port*> copied_output_ports;
    Port virtual_port;
    Net virtual_net;
    Port* virtual_port_ptr = nullptr;
    Net* virtual_net_ptr = nullptr;
    if (pin->isInput()) {
      for (auto& pin_port : pin_ports) {
        if (pin_port->isPort()) {
          Port* pin_port1 = dynamic_cast<Port*>(pin_port);
          auto* found_port = findOrCopySubnetPort(subnetlist, *pin_port1);
          auto* the_net = subnetlist.findNet(connect_net->get_name());
          if (!the_net) {
            auto& created_net = subnetlist.addNet(Net(connect_net->get_name()));
            the_net = &created_net;
          }
          connectObjectToSubnetNet(*the_net, *found_port);
          continue;
        }

        if (dynamic_cast<Pin*>(pin_port)->getFullName() ==
            pin.get()->getFullName()) {
          continue;
        }

        // collect other load pins in the same instance.(fit multi_load_pin
        // net).
        std::string own_instance_name =
            pin_port->get_own_instance()->getFullName();
        if (pin_port->isPin() && pin_port->isInput() &&
            Str::equal(boundary_inst.get_name(), own_instance_name.c_str()) ==
                true) {
          auto the_found_pin = boundary_inst.getPin(pin_port->get_name());
          LOG_FATAL_IF(!the_found_pin)
              << "Unable to find the load pin in instance.";
          boundary_inst_remaining_pins.emplace_back(*the_found_pin);
        }

        // the driver pin and the load pin are in the same cluster, the other
        // load pin in other cluster(s).the driver pin build virtual port,there
        // add the load pin to the virtual net.(fit multi_load_pin net).
        if (pin_port->isPin() && pin_port->isOutput() &&
            boundary_inst_own_cluster.contains(own_instance_name) &&
            is_boundary_net) {
          std::string dcl_name = connect_net->get_name();
          dcl_name = Str::replace(dcl_name, "/", "_");
          dcl_name = Str::replace(dcl_name, "\\[", "");
          dcl_name = Str::replace(dcl_name, "\\]", "");
          dcl_name = Str::replace(dcl_name, "\\.", "");
          auto* the_net = subnetlist.findNet(dcl_name.c_str());

          if (the_net) {
            virtual_net_ptr = the_net;
          } else {
            subnetlist.addNet(Net(dcl_name.c_str()));
            virtual_net_ptr = subnetlist.findNet(dcl_name.c_str());
          }
          LOG_FATAL_IF(!virtual_net_ptr);
        }

        // build the virtual port and virtual net between the net's dirver pin
        // and the net's load pin.
        if (pin_port->isPin() && pin_port->isOutput() &&
            !boundary_inst_own_cluster.contains(own_instance_name)) {
          // the virtual port name should not contaiins escape when adding the
          // virtual port.(because wirte net need escapeName resulting in port
          // name and net name is not consistent.)
          std::string dcl_name = connect_net->get_name();
          dcl_name = Str::replace(dcl_name, "/", "_");
          dcl_name = Str::replace(dcl_name, "\\[", "");
          dcl_name = Str::replace(dcl_name, "\\]", "");
          dcl_name = Str::replace(dcl_name, "\\.", "");

          // flag the instance'pin to connect to the virtual net.
          dynamic_cast<Pin*>(pin_port)->set_net_name_between_clusters(
              dcl_name.c_str());

          // need virtual port on the connect net.
          PortDir dcl_type = PortDir::kIn;

          auto* the_net = subnetlist.findNet(dcl_name.c_str());
          auto* the_port = subnetlist.findPort(dcl_name.c_str());
          if (the_net && the_port) {
            virtual_net_ptr = the_net;
            virtual_port_ptr = the_port;
          } else {
            virtual_port = Port(dcl_name.c_str(), dcl_type);
            virtual_port.set_is_virtual_port(1);
            virtual_net = Net(dcl_name.c_str());
            subnetlist.addPort(std::move(virtual_port));
            subnetlist.addNet(std::move(virtual_net));
            virtual_port_ptr = subnetlist.findPort(dcl_name.c_str());
            virtual_net_ptr = subnetlist.findNet(dcl_name.c_str());
            virtual_net_ptr->addPinPort(virtual_port_ptr);
            virtual_port_ptr->set_net(virtual_net_ptr);
          }

          LOG_FATAL_IF(!virtual_net_ptr || !virtual_port_ptr);
        }
      }
    } else if (pin->isOutput()) {
      bool first = true;
      for (auto& pin_port : pin_ports) {
        if (pin_port->isPort()) {
          Port* pin_port1 = dynamic_cast<Port*>(pin_port);
          auto* copied_port = findOrCopySubnetPort(subnetlist, *pin_port1);
          copied_output_ports.push_back(copied_port);
          continue;
        }

        if (dynamic_cast<Pin*>(pin_port)->getFullName() ==
            pin.get()->getFullName()) {
          continue;
        }

        std::string own_instance_name =
            pin_port->get_own_instance()->getFullName();

        if (!boundary_inst_own_cluster.contains(own_instance_name)) {
          std::string dcl_name = connect_net->get_name();
          dcl_name = Str::replace(dcl_name, "/", "_");
          dcl_name = Str::replace(dcl_name, "\\[", "");
          dcl_name = Str::replace(dcl_name, "\\]", "");
          dcl_name = Str::replace(dcl_name, "\\.", "");

          // flag the instance'pin to connect to the virtual net.
          dynamic_cast<Pin*>(pin_port)->set_net_name_between_clusters(
              dcl_name.c_str());
          // need virtual port on the connect net.
          if (first) {
            PortDir dcl_type = PortDir::kOut;

            auto* the_net = subnetlist.findNet(dcl_name.c_str());
            if (the_net) {
              virtual_net_ptr = the_net;
            } else {
              virtual_net = Net(dcl_name.c_str());
              subnetlist.addNet(std::move(virtual_net));
              virtual_net_ptr = subnetlist.findNet(dcl_name.c_str());
            }

            virtual_port = Port(dcl_name.c_str(), dcl_type);
            virtual_port.set_is_virtual_port(1);
            subnetlist.addPort(std::move(virtual_port));
            virtual_port_ptr = subnetlist.findPort(dcl_name.c_str());

            LOG_FATAL_IF(!virtual_net_ptr || !virtual_port_ptr);
            virtual_net_ptr->addPinPort(virtual_port_ptr);
            virtual_port_ptr->set_net(virtual_net_ptr);
          }
          first = false;
        }
      }
    }

    if (virtual_net_ptr) {
      for (auto* copied_output_port : copied_output_ports) {
        connectObjectToSubnetNet(*virtual_net_ptr, *copied_output_port);
      }

      connectObjectToSubnetNet(*virtual_net_ptr, *pin);

      if (!boundary_inst_remaining_pins.empty()) {
        for (const auto& boundary_inst_remaining_pin :
             boundary_inst_remaining_pins) {
          connectObjectToSubnetNet(*virtual_net_ptr, *boundary_inst_remaining_pin);
        }
      }

    } else {
      Net* the_net = subnetlist.findNet(connect_net->get_name());
      /**pin update the connecte net, connect the new net.**/
      if (!the_net) {
        const char* net_name = connect_net->get_name();
        Net new_net = Net(net_name);

        // for debugging purposes
        // auto& pin_ports = connect_net->get_pin_ports();
        // for (auto& pin_port : pin_ports) {
        //   LOG_INFO << "Debug:pin_port " << pin_port->get_name() << " "
        //            << pin_port;
        // }

        auto& created_net = subnetlist.addNet(std::move(new_net));
        the_net = &created_net;
      }

      for (auto* copied_output_port : copied_output_ports) {
        connectObjectToSubnetNet(*the_net, *copied_output_port);
      }

      connectObjectToSubnetNet(*the_net, *pin);

      /**pin does not update the connecte net, connect the design netlist's
       * net.**/
      // if (!the_net) {
      //   Net new_net = Net(*connect_net);
      //   subnetlist.addNet(std::move(new_net));
      // }
    }
  }
}

}  // namespace ista
