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
 * @file CmdLinkDesign.cc
 * @author Wang Hao (harry0789@qq.com)
 * @brief
 * @version 0.1
 * @date 2021-10-12
 */
#include "ShellCmd.hh"
#include "sta/Sta.hh"
#include "sta/StaClusterTiming.hh"

#define TEST_ASIC_TOP 1
#define TEST_EXAMPLE_1 0

namespace ista {

std::vector<std::set<std::string>> readClusterFromFile(
    const std::string& filename);

CmdLinkDesign::CmdLinkDesign(const char* cmd_name) : TclCmd(cmd_name) {
  auto* cell_name_option = new TclStringOption("cell_name", 1, nullptr);
  addOption(cell_name_option);
}

unsigned CmdLinkDesign::check() {
  TclOption* cell_name_option = getOptionOrArg("cell_name");
  LOG_FATAL_IF(!cell_name_option);
  return 1;
}

unsigned CmdLinkDesign::exec() {
  if (!check()) {
    return 0;
  }

  TclOption* cell_name_option = getOptionOrArg("cell_name");
  auto* cell_name = cell_name_option->getStringVal();

  Sta* ista = Sta::getOrCreateSta();
  ista->set_top_module_name(cell_name);

  ista->linkDesignWithRustParser(cell_name);

#if 0

  std::set<std::string> exclude_cell_names = {};
  // ista->get_netlist()->writeVerilog(
  //     "/home/longshuaiying/cluster_timing_model/asic_top/verilog/"
  //     "asic_top_flatten.v",
  //     exclude_cell_names);

  std::vector<std::set<std::string>> clusters = readClusterFromFile(
      "/home/longshuaiying/cluster_timing_model/asic_top/"
      "cluster_instances.txt");

  StaClusterTiming sta_cluster_timing(clusters);

  sta_cluster_timing.addHierSubNetlist();

  std::vector<Netlist*> hier_sub_netlists =
      ista->get_netlist()->get_hier_sub_netlists();
  auto& top_insts = ista->get_netlist()->get_instances();
  ista->get_netlist()->writeVerilog(
      "/home/longshuaiying/cluster_timing_model/asic_top/verilog/"
      "asic_top_write2.v",
      exclude_cell_names);

  auto findInstanceByName = [&top_insts](const std::string& name) -> Instance* {
    for (auto& inst : top_insts) {
      if (inst.get_name() == name) {
        return &inst;
      }
    }
    return nullptr;
  };

  // std::vector<int> sub_netlist_virtual_port_vec;
  int sub_netlist_index = 1;
  for (const auto& hier_sub_netlist : hier_sub_netlists) {
    std::set<std::string> exclude_cell_names1 = {};
    std::string cluster_verilog_file =
        std::string(
            "/home/longshuaiying/cluster_timing_model/asic_top/verilog/") +
        std::string("hier_sub_netlist") + std::to_string(sub_netlist_index) +
        ".v";
    //   int cluster_inst_nums = hier_sub_netlist->getInstanceNum();
    //   std::cout << std::endl;
    //   std::cout << "cluster_" << sub_netlist_index
    //             << " inst_nums:" << cluster_inst_nums << std::endl;

    //   bool is_cluster_inst_equal = true;
    //   for (auto& hier_inst : hier_sub_netlist->get_instances()) {
    //     auto top_inst = findInstanceByName(hier_inst.get_name());
    //     LOG_FATAL_IF(!top_inst);

    //     is_cluster_inst_equal = hier_inst.isEqual(*top_inst);
    //     if (!is_cluster_inst_equal) {
    //       break;
    //     }
    //   }
    //   if (is_cluster_inst_equal) {
    //     std::cout << "cluster_" << sub_netlist_index << " insts is equal."
    //               << std::endl;
    //   } else {
    //     std::cout << "cluster_" << sub_netlist_index << " insts is not
    //     equal."
    //               << std::endl;
    //   }

    //   int hier_vitual_port_index = 1;
    //   std::vector<std::string> hier_virtual_port_name_vec;
    //   std::set<std::string> hier_virtual_port_name_set;

    //   for (auto& hier_port : hier_sub_netlist->get_ports()) {
    //     auto top_port = ista->get_netlist()->findPort(hier_port.get_name());
    //     if (!top_port) {
    //       // std::cout << "hier_port "
    //       //           << ":" << hier_vitual_port_index << ":"
    //       //           << hier_port.get_name() << std::endl;
    //       hier_virtual_port_name_vec.emplace_back(hier_port.get_name());
    //       hier_virtual_port_name_set.insert(hier_port.get_name());
    //     }

    //     // ++hier_vitual_port_index;
    //   }

    //   std::cout << "cluster_" << sub_netlist_index
    //             << " hier_virtual_port_name_vec: "
    //             << hier_virtual_port_name_vec.size() << std::endl;
    //   std::cout << "cluster_" << sub_netlist_index
    //             << " hier_virtual_port_name_set: "
    //             << hier_virtual_port_name_set.size() << std::endl;
    //   sub_netlist_virtual_port_vec.push_back(hier_virtual_port_name_vec.size());

    hier_sub_netlist->writeVerilog(cluster_verilog_file.c_str(),
                                   exclude_cell_names1);

    //   std::unordered_map<std::string, int> count_map;
    //   for (const auto& item : hier_virtual_port_name_vec) {
    //     count_map[item]++;
    //   }
    //   std::cout << "Duplicate elements and their counts in "
    //                "hier_virtual_port_name_vec : "
    //             << std::endl;
    //   bool has_duplicates = false;
    //   int duplicate_count = 0;
    //   for (const auto& pair : count_map) {
    //     if (pair.second > 1) {
    //       std::cout << pair.first << ": " << pair.second - 1 << " times"
    //                 << std::endl;
    //       int count = pair.second - 1;
    //       duplicate_count = duplicate_count + count;
    //       has_duplicates = true;
    //     }
    //   }
    //   std::cout << "Duplicate elements: " << duplicate_count << std::endl;

    //   if (!has_duplicates) {
    //     std::cout << "No duplicate elements in hier_virtual_port_name_vec."
    //               << std::endl;
    //   }

    sub_netlist_index++;
    // if (sub_netlist_index == 2) {
    //   break;
    // }
    // }
    // for debug purposes
    // bool virtual_port_equal_boundary_net =
    //     (sub_netlist_virtual_port_vec ==
    //      sta_cluster_timing.get_cluster_boundary_net_set());

    // run cluster timing analysis in a flow control
    // auto* design_netlist = ista->get_netlist();
    // design_netlist->reset();

    // for (const auto& hier_sub_netlist : hier_sub_netlists) {
    //   StaCharacterTiming character_timing(analysis_mode, model_path);
    //   StaGraph the_graph;
    //   Vector<std::function<unsigned(StaGraph*)>> funcs = {StaBuildGraph()};
    //   for (auto& func : funcs) {
    //     the_graph.exec(func);
    //   }
    //   character_timing(&the_graph);
  }

  // sta_cluster_timing.buildSubnetlistToInst();
  // ista->get_netlist()->writeVerilog(
  //     "/home/longshuaiying/cluster_timing_model/asic_top/verilog/"
  //     "asic_top_write3.v",
  //     exclude_cell_names);

#elif TEST_EXAMPLE_1
  std::set<std::string> exclude_cell_names = {};
  ista->get_netlist()->writeVerilog(
      "/home/longshuaiying/cluster_timing_model/example1/verilog/"
      "example1_write1.v",
      exclude_cell_names);
  std::vector<std::set<std::string>> clusters = {
      {"r1", "u2"}, {"r2", "u1"}, {"r3"}};
  StaClusterTiming sta_cluster_timing(clusters);

  sta_cluster_timing.addHierSubNetlist();

  std::vector<Netlist*> hier_sub_netlists =
      ista->get_netlist()->get_hier_sub_netlists();
  ista->get_netlist()->writeVerilog(
      "/home/longshuaiying/cluster_timing_model/example1/verilog/"
      "example1_write2.v",
      exclude_cell_names);
  int sub_netlist_index = 1;
  for (const auto& hier_sub_netlist : hier_sub_netlists) {
    std::set<std::string> exclude_cell_names1 = {};
    std::string cluster_verilog_file =
        std::string(
            "/home/longshuaiying/cluster_timing_model/example1/verilog/") +
        std::string("hier_sub_netlist") + std::to_string(sub_netlist_index) +
        ".v";
    hier_sub_netlist->writeVerilog(cluster_verilog_file.c_str(),
                                   exclude_cell_names1);
    sub_netlist_index++;
  }
  sta_cluster_timing.buildSubnetlistToInst();
  ista->get_netlist()->writeVerilog(
      "/home/longshuaiying/cluster_timing_model/example1/verilog/"
      "example1_write3.v",
      exclude_cell_names);

#endif
  return 1;
}

std::vector<std::set<std::string>> readClusterFromFile(
    const std::string& filename) {
  std::vector<std::set<std::string>> allData;
  std::ifstream file(filename);

  if (!file.is_open()) {
    std::cerr << "Unable to open file." << std::endl;
    return allData;
  }

  std::string line;
  while (std::getline(file, line)) {
    std::set<std::string> lineData;
    size_t start = line.find('{');
    size_t end = line.find('}');

    if (start != std::string::npos && end != std::string::npos && start < end) {
      std::string content = line.substr(start + 1, end - start - 1);
      std::stringstream ss(content);
      std::string item;

      while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
          lineData.insert(item);
        }
      }
    }

    allData.push_back(lineData);
  }

  file.close();

  size_t inst_num = 0;
  bool first = true;
  for (const auto& set : allData) {
    if (first) {
      std::cout << "Total number of cluster1 instances: " << set.size()
                << std::endl;
      first = false;
    }

    inst_num += set.size();
  }

  std::cout << "Total number of instances: " << inst_num << std::endl;
  return allData;
}

}  // namespace ista