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
#include "api/TimingEngine.hh"
#include "gtest/gtest.h"
#include "log/Log.hh"
#include "sta/StaClusterTiming.hh"
#include "usage/usage.hh"

using namespace ista;
using namespace ieda;

namespace {

// get clusters results according to reading files.
std::vector<std::set<std::string>> readClusterFromFile(
    const std::string& filename);

class ClusterTimingTest : public testing::Test {
  void SetUp() final {
    char config[] = "test";
    char* argv[] = {config};
    Log::init(argv);
  }
  void TearDown() final { Log::end(); }
};

TEST_F(ClusterTimingTest, example1) {
  Stats stats;
  Sta* ista = Sta::getOrCreateSta();

  ista->set_num_threads(48);
  const char* design_work_space =
      "/home/longshuaiying/cluster_timing_model/example1/rpt";
  ista->set_design_work_space(design_work_space);
  std::vector<std::string> nangate45_lib_files{
      "/home/taosimin/nangate45/lib/NangateOpenCellLibrary_typical.lib"};
  //  /home/longshuaiying/cluster_timing_model/example1/liberty/cluster1.lib \
  //  /home/longshuaiying/cluster_timing_model/example1/liberty/cluster2.lib"
  ista->readLiberty(nangate45_lib_files);

  ista->readVerilogWithRustParser(
      "/home/longshuaiying/cluster_timing_model/example1/verilog/"
      "example1.v");
  const char* top_module_name = "top";
  ista->set_top_module_name(top_module_name);
  ista->linkDesignWithRustParser(top_module_name);

  std::set<std::string> exclude_cell_names = {};
  ista->get_netlist()->writeVerilog(
      "/home/longshuaiying/cluster_timing_model/example1/verilog/"
      "example1_write1.v",
      exclude_cell_names, true);
  std::vector<std::set<std::string>> clusters = {
      {"r1", "u2"}, {"r2", "u1"}, {"r3"}};
  StaClusterTiming sta_cluster_timing(clusters);

  sta_cluster_timing.addHierSubNetlist();

  std::vector<Netlist*> hier_sub_netlists =
      ista->get_netlist()->get_hier_sub_netlists();
  ASSERT_EQ(hier_sub_netlists.size(), 2U)
      << "expected both multi-instance clusters to be emitted as subnetlists";
  ista->get_netlist()->writeVerilog(
      "/home/longshuaiying/cluster_timing_model/example1/verilog/"
      "example1_write2.v",
      exclude_cell_names, true);
  int sub_netlist_index = 1;
  for (const auto& hier_sub_netlist : hier_sub_netlists) {
    std::set<std::string> exclude_cell_names1 = {};
    std::string cluster_verilog_file =
        std::string(
            "/home/longshuaiying/cluster_timing_model/example1/verilog/") +
        std::string("hier_sub_netlist") + std::to_string(sub_netlist_index) +
        ".v";
    hier_sub_netlist->writeVerilog(cluster_verilog_file.c_str(),
                                   exclude_cell_names1, true);
    sub_netlist_index++;
  }
  sta_cluster_timing.buildSubnetlistToInst();
  ista->get_netlist()->writeVerilog(
      "/home/longshuaiying/cluster_timing_model/example1/verilog/"
      "example1_write3.v",
      exclude_cell_names, true);

  double memory_delta = stats.memoryDelta();
  LOG_INFO << "extract timing lib memory usage " << memory_delta << "MB";
  double time_delta = stats.elapsedRunTime();
  LOG_INFO << "extract timing lib time elapsed " << time_delta << "s";
}

TEST_F(ClusterTimingTest, asic_top) {
  Stats stats;
  Sta* ista = Sta::getOrCreateSta();

  ista->set_num_threads(48);
  const char* design_work_space =
      "/home/longshuaiying/cluster_timing_model/asic_top/rpt";
  ista->set_design_work_space(design_work_space);

  std::vector<std::string> t28_lib_files = {
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140hvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140lvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140mblvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140mbssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140opphvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140opplvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140oppssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140oppuhvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140oppulvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/tcbn28hpcplusbwp30p140ssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140uhvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140ulvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140hvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140lvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140mbhvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140mblvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140mbssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140opphvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140opplvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140oppssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140oppuhvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140oppulvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/tcbn28hpcplusbwp35p140ssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140uhvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140ulvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140ehvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140hvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140lvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140mbhvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140mbssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140oppehvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140opphvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140opplvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140oppssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140oppuhvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/tcbn28hpcplusbwp40p140ssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140uhvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "ts5n28hpcplvta256x32m4fw_130a_ssg0p81v125c.lib",
      "/home/taosimin/T28/ccslib/"
      "ts5n28hpcplvta64x128m2fw_130a_ssg0p81v125c.lib",
      "/home/taosimin/T28/ccslib/tphn28hpcpgv18ssg0p81v1p62v125c.lib",
      "/home/taosimin/T28/ccslib/PLLTS28HPMLAINT_SS_0P81_125C.lib",
      "/home/longshuaiying/cluster_timing_model/asic_top/liberty/cluster1.lib"};
  for (int sub_netlist_index = 1; sub_netlist_index <= 100;
       ++sub_netlist_index) {
    std::string cluster_lib =
        "/home/longshuaiying/cluster_timing_model/asic_top/liberty/"
        "cluster" +
        std::to_string(sub_netlist_index) + ".lib";
    t28_lib_files.push_back(cluster_lib);
  }
  ista->readLiberty(t28_lib_files);
  ista->readVerilogWithRustParser(
      "/home/longshuaiying/cluster_timing_model/asic_top/verilog/"
      "asic_top_flatten.v");
  const char* top_module_name = "asic_top";
  ista->set_top_module_name(top_module_name);
  ista->linkDesignWithRustParser(top_module_name);

  std::set<std::string> exclude_cell_names = {};

  std::vector<std::set<std::string>> clusters = readClusterFromFile(
      "/home/longshuaiying/cluster_timing_model/asic_top/"
      "cluster_instances.txt");

  StaClusterTiming sta_cluster_timing(clusters);

  sta_cluster_timing.addHierSubNetlist();

  std::vector<Netlist*> hier_sub_netlists =
      ista->get_netlist()->get_hier_sub_netlists();

  auto& top_insts = ista->get_netlist()->get_instances();
  // ista->get_netlist()->writeVerilog(
  //     "/home/longshuaiying/cluster_timing_model/asic_top/verilog/"
  //     "asic_top_write2.v",
  //     exclude_cell_names);

  auto findInstanceByName = [&top_insts](const std::string& name) -> Instance* {
    for (auto& inst : top_insts) {
      if (inst.get_name() == name) {
        return &inst;
      }
    }
    return nullptr;
  };

  // the belowing code is to verify whether subnetlist is correctly built by
  // clusters.
  int sub_netlist_index = 2;
  std::vector<int> sub_netlist_virtual_port_vec;
  bool is_all_clusters_inst_equal = true;
  // for (const auto& hier_sub_netlist : hier_sub_netlists) {
  //   // print all sub_netlists.
  //   std::set<std::string> exclude_cell_names1 = {};
  //   std::string cluster_verilog_file =
  //       std::string(
  //           "/home/longshuaiying/cluster_timing_model/asic_top/verilog/") +
  //       std::string("hier_sub_netlist") + std::to_string(sub_netlist_index) +
  //       ".v";
  //   hier_sub_netlist->writeVerilog(cluster_verilog_file.c_str(),
  //                                  exclude_cell_names1);
  //   //////// 1.print cluster_inst_num. ////////
  //   int cluster_inst_nums = hier_sub_netlist->getInstanceNum();
  //   std::cout << "cluster_inst_nums" << cluster_inst_nums << std::endl;

  //   ///  2.compare insts in sub netlist is equal to insts in top netlist ///

  //   bool is_cluster_inst_equal = true;
  //   for (auto& hier_inst : hier_sub_netlist->get_instances()) {
  //     auto top_inst = findInstanceByName(hier_inst.get_name());
  //     LOG_FATAL_IF(!top_inst);
  //     is_cluster_inst_equal = hier_inst.isEqual(*top_inst);
  //     if (!is_cluster_inst_equal) {
  //       break;
  //     }
  //   }
  //   is_all_clusters_inst_equal =
  //       is_all_clusters_inst_equal && is_cluster_inst_equal;
  //   if (is_cluster_inst_equal) {
  //     std::cout << "cluster_" << sub_netlist_index << " insts is equal."
  //               << std::endl;
  //   } else {
  //     std::cout << "cluster_" << sub_netlist_index << " insts is not equal."
  //               << std::endl;
  //   }

  //   ///  3.find all virtual ports newed in sub netlist. ///
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
  //   ///  4.get all virtual ports nums newed in all subnetlist.
  //   sub_netlist_virtual_port_vec.push_back(hier_virtual_port_name_vec.size());

  //   ///  5.print the duplicate port name and port count. ///
  //   // std::cout << "hier_virtual_port_name_vec: "
  //   //           << hier_virtual_port_name_vec.size() << std::endl;
  //   // std::cout << "hier_virtual_port_name_set: "
  //   //           << hier_virtual_port_name_set.size() << std::endl;

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

  //   sub_netlist_index++;
  //   // if (sub_netlist_index == 2) {
  //   //   break;
  //   // }

  //   // 7.run cluster timing analysis in a flow control
  //   // auto* design_netlist = ista->get_netlist();
  //   // design_netlist->reset();

  //   // for (const auto& hier_sub_netlist : hier_sub_netlists) {
  //   //   StaCharacterTiming character_timing(analysis_mode, model_path);
  //   //   StaGraph the_graph;
  //   //   Vector<std::function<unsigned(StaGraph*)>> funcs =
  //   {StaBuildGraph()};
  //   //   for (auto& func : funcs) {
  //   //     the_graph.exec(func);
  //   //   }
  //   //   character_timing(&the_graph);
  // }
  // ///  6.compare subnetlist_virtual_port_vec with boundary_net set size can
  // ///  verify subnetlist is correctly build.///
  // bool virtual_port_equal_boundary_net =
  //     (sub_netlist_virtual_port_vec ==
  //      sta_cluster_timing.get_cluster_boundary_net_set());
  // if (is_all_clusters_inst_equal && virtual_port_equal_boundary_net) {
  //   std::cout << "All subnetlists are correctly build." << std::endl;
  // }

  sta_cluster_timing.buildSubnetlistToInst();
  // ista->get_netlist()->writeVerilog(
  //     "/home/longshuaiying/cluster_timing_model/asic_top/verilog/"
  //     "asic_top_write3.v",
  //     exclude_cell_names);

  double memory_delta = stats.memoryDelta();
  LOG_INFO << "extract timing lib memory usage " << memory_delta << "MB";
  double time_delta = stats.elapsedRunTime();
  LOG_INFO << "extract timing lib time elapsed " << time_delta << "s";
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

}  // namespace
