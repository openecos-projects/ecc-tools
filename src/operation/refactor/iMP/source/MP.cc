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
#include "MP.hh"

#include <boost/foreach.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <functional>
#include <memory>

#include "BlkClustering.hh"
#include "HierPlacer.hh"
#include "Logger.hpp"
#include "MacroAligner.hh"
#include "NetWeightPre.hh"
#include "Refinement.hh"

namespace imp {

// victorzhou 202040906
void MP::runMP(std::string config, std::string output_tcl)
{
  INFO("victorzhou 202041008 MP::runMP");

  boost::property_tree::ptree proot;
  try {
    boost::property_tree::read_json(config, proot);
  } catch (std::exception& e) {
    std::cout << e.what() << std::endl;
    return;
  }

  float macro_halo_micron = std::stof(proot.get<std::string>("macro_halo_micron"));
  float dead_space_ratio = std::stof(proot.get<std::string>("dead_space_ratio"));
  float weight_wl = std::stof(proot.get<std::string>("weight_wl"));
  float weight_ol = std::stof(proot.get<std::string>("weight_ol"));
  float weight_ob = std::stof(proot.get<std::string>("weight_ob"));
  float weight_periphery = std::stof(proot.get<std::string>("weight_periphery"));
  float weight_blockage = std::stof(proot.get<std::string>("weight_blockage"));
  float weight_io = std::stof(proot.get<std::string>("unicaweight_iost"));
  size_t max_iters = std::stoull(proot.get<std::string>("max_iters"));
  float cool_rate = std::stof(proot.get<std::string>("cool_rate"));
  float init_temperature = std::stof(proot.get<std::string>("init_temperature"));

  INFO("victorzhou 202041008 MP::runMP,macro_halo_micron:", macro_halo_micron);

  /*float macro_halo_micron = 2.0;
  float dead_space_ratio = 0.8;
  float weight_wl = 1.0;
  float weight_ol = 0.05;
  float weight_ob = 0.02;
  float weight_periphery = 0.05;
  float weight_blockage = 0.0;
  float weight_io = 0.0;
  size_t max_iters = 1000;
  float cool_rate = 0.96;
  float init_temperature = 2000.0;*/

  std::unordered_set<std::string> critical_nets_name;
  std::unordered_set<std::string> non_critical_nets_name;
  // timingDrivenNetWeight(root().netlist(), critical_nets_name, non_critical_nets_name);

  BlkClustering2 clustering{.l1_nparts = 200,
                            .level_num = 1,
                            .parser = _parser,
                            .critical_nets_name = critical_nets_name,
                            .non_critical_nets_name = non_critical_nets_name};  // one level place
  // BlkClustering2 clustering{.l1_nparts = 10, .l2_nparts = 20, .level_num = 2, .parser = _parser};  // two-level place
  clustering(root());

  // calculateAndPrintHedgeWeights(root().netlist());

  auto placer = SAHierPlacer<int32_t>{.macro_halo_micron = macro_halo_micron,
                                      .dead_space_ratio = dead_space_ratio,
                                      .weight_wl = weight_wl,
                                      .weight_ol = weight_ol,
                                      .weight_ob = weight_ob,
                                      .weight_periphery = weight_periphery,
                                      .weight_blk = weight_blockage,
                                      .weight_io = weight_io,
                                      .max_iters = max_iters,
                                      .cool_rate = cool_rate,
                                      .init_temperature = init_temperature,
                                      .cluster_level_num = clustering.level_num,
                                      .parser = _parser};

  placer(root());

  // writePlacement(root(), file_name + ".txt");
  auto macro_aligner = MacroAligner<int32_t>();
  macro_aligner(root());
  // writePlacement(root(), file_name + "_aligned.txt");
  writePlacementTcl(root(), output_tcl + ".tcl", root().netlist().property()->get_database_unit());
  _parser->write();  // write back to idb
}

void MP::runRef(std::string output_tcl) {
  std::cout << " --------------Macro Refinement-----------------"<<std::endl;

  output_tcl = "refinement.tcl";

  int method = 2; // 0: Bounding box, 1: MP-tree, 2: Grids

  float macro_halo_micron = 1.0;
  std::string original_pin_dir = "R0";
  int exp_space_x = 10;
  int exp_space_y = 2;
  int search_space_x = 10;
  int search_space_y = 2;
  int gap = 100;
  int virtual_macro_size = 10;
  bool beikaobei = false;
  float h_weight = 1.0;
  float v_weight = 1.0;
  bool consider_std = false;

  Refinement refinement(_parser);

  refinement.initPostProcessingData(
      macro_halo_micron, 
      original_pin_dir, 
      exp_space_x, 
      exp_space_y, 
      search_space_x, 
      search_space_y, 
      gap, 
      virtual_macro_size, 
      beikaobei, 
      h_weight, 
      v_weight, 
      consider_std
  );

  std::string tcl_file_path = "/data/project_share/huangzhipeng/Flow_T28/pd_data/pr_1000M/run/test_0926/asic_top_macro_loc_86529777664_26.tcl";

  refinement.readTcl(tcl_file_path);

  refinement.runRefinement(method, output_tcl);

  refinement.writeTcl(output_tcl);

  std::cout << "Refinement process completed." << std::endl;
}

}  // namespace imp