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
 * @file CmdSetIdealNetwork.cc
 * @author
 * @brief The sdc set_ideal_network cmd implemention.
 * @version 0.1
 * @date 2026-04-02
 */

#include "Cmd.hh"

#include "netlist/DesignObject.hh"
#include "sdc/SdcCollection.hh"
#include "sdc/SdcConstrain.hh"
#include "sta/Sta.hh"

namespace ista {

CmdSetIdealNetwork::CmdSetIdealNetwork(const char* cmd_name) : TclCmd(cmd_name) {
  auto* no_propagation_option = new TclSwitchOption("-no_propagation");
  addOption(no_propagation_option);

  auto* objects_arg = new TclStringOption("objects", 1, nullptr);
  addOption(objects_arg);
}

unsigned CmdSetIdealNetwork::check() { return 1; }

unsigned CmdSetIdealNetwork::exec() {
  if (!check()) {
    return 0;
  }

  auto* objects_option = getOptionOrArg("objects");
  auto* objects_str = objects_option->getStringVal();

  auto* ista = Sta::getOrCreateSta();
  auto* design_nl = ista->get_netlist();

  auto object_list = FindObjOfSdc(objects_str, design_nl);
  LOG_FATAL_IF(object_list.empty()) << "object list is empty.";

  for (auto& object : object_list) {
    std::visit(
        overloaded{
            [](SdcCommandObj* sdc_obj) {
              LOG_FATAL << "set_ideal_network does not support sdc object "
                        << typeid(*sdc_obj).name();
            },
            [](DesignObject* design_obj) { design_obj->set_ideal_network(); },
        },
        object);
  }

  return 1;
}

}  // namespace ista
