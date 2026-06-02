#define GLOG_NO_ABBREVIATED_SEVERITIES

#include "BlkClustering.hh"

#include "Block.hh"
#include "Hmetis.hh"
#include "HyperGraphAlgorithm.hh"
#include "IDBParserEngine.hh"
#include "IdbNet.h"
#include "Logger.hpp"
#include "Net.hh"
#include "Pin.hh"
#include "idm.h"

namespace imp {
void BlkClustering::operator()(Block& block)
{
  auto netlist = block.netlist();
  size_t nparts = block.level() == 1 ? l1_nparts : l2_nparts;
  if (netlist.vSize() <= nparts || block.level() > 2)
    return;

  auto&& [eptr, eind] = vectorize(netlist);
  HMetis partition{.seed = 0};
  auto parts = partition(block.get_name(), eptr, eind, nparts);
  int i = 0;

  auto sub_block = [&](const Netlist& graph, const std::vector<size_t>& sub_vertices) {
    auto&& [sub_netlist, cuts] = sub_graph(graph, sub_vertices);
    int64_t sum_area = std::accumulate(sub_netlist.vbegin(), sub_netlist.vend(), int64_t(0),
                                       [](auto&& a, auto&& b) { return a + (int64_t) geo::area(b.property()->boundingbox()); });
    int32_t w = std::sqrt(sum_area);
    int32_t h = w;
    auto new_block = std::make_shared<imp::Block>(block.get_name() + "_" + std::to_string(i++),
                                                  std::make_shared<imp::Netlist>(std::move(sub_netlist)), block.shared_from_this());
    new_block->set_shape_curve(imp::geo::make_box(0, 0, w, h));
    INFO(new_block->get_name(), " num_v: ", new_block->netlist().vSize(), " num_cuts: ", cuts.size(),
         " num_e: ", new_block->netlist().heSize());
    return new_block;
  };

  auto clusters = clustering(netlist, parts, sub_block);
  block.set_netlist(std::make_shared<Netlist>(std::move(clusters)));
}

void BlkClustering2::operator()(Block& root_cluster)
{
  multiLevelClustering(root_cluster);
  INFO("MultiLevel-Clustering success");
}

void BlkClustering2::multiLevelClustering(Block& root_cluster)
{
  root_cluster.preorder_op([this](Block& blk) { this->singleLevelClustering(blk); });
}

void BlkClustering2::singleLevelClustering(Block& block)
{
  paramCheck();
  auto netlist = block.netlist();
  size_t nparts = block.level() == 1 ? l1_nparts : l2_nparts;
  if (netlist.vSize() <= nparts || block.level() > level_num)
    return;

  auto get_net_weight = [](std::shared_ptr<Net> net)->float{return net->get_net_weight();};
  auto&& [eptr, eind, vweight, heweight] = vectorize(netlist, NoneWeight<std::shared_ptr<Object>>, get_net_weight);
  std::vector<int> heweightInt;
  heweightInt.reserve(heweight.size());
  for (auto weight : heweight) {
      heweightInt.push_back(static_cast<int>(weight * 100));
  }
  HMetis partition{.seed = 0, .ufactor = 1.0};
  auto parts = partition(block.get_name(), eptr, eind, nparts);
  // auto parts = partition(block.get_name(), eptr, eind, nparts, {}, heweightInt);

  // extract io-cell as single cluster at level 1
  size_t single_cluster_id = nparts;
  if (block.level() == 1) {
    std::unordered_map<size_t, std::unordered_set<size_t>> cluster_to_cells;
    for (size_t i = 0; i < parts.size(); ++i) {
        cluster_to_cells[parts[i]].insert(i);
    }
    size_t num_terminals = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
      auto vertex_prop = block.netlist().vertex_at(i).property();
      if (vertex_prop->isInstance()) {
        auto& inst = dynamic_cast<Instance&>(*vertex_prop);
        if (inst.get_cell_master().isIOCell()) {
          num_terminals++;
          size_t original_cluster_id = parts[i];
          cluster_to_cells[original_cluster_id].erase(i);
          if (!cluster_to_cells[original_cluster_id].empty()) {
            parts[i] = single_cluster_id++;  // extract io as a single cluster;
          }
        }
      }
    }
    INFO("num_terminals : ", num_terminals);
  }

  // extract macro as single cluster at last level
  if (block.level() == level_num) {
    std::unordered_map<size_t, std::unordered_set<size_t>> cluster_to_cells;
    for (size_t i = 0; i < parts.size(); ++i) {
        cluster_to_cells[parts[i]].insert(i);
    }
    // size_t single_macro_cluster_id = nparts;
    size_t num_macros = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
      auto vertex_prop = block.netlist().vertex_at(i).property();
      if (vertex_prop->isInstance()) {
        auto& inst = dynamic_cast<Instance&>(*vertex_prop);
        if (inst.get_cell_master().isMacro()) {
          num_macros += 1;
          size_t original_cluster_id = parts[i];
          cluster_to_cells[original_cluster_id].erase(i);
          if (!cluster_to_cells[original_cluster_id].empty()) {
            parts[i] = single_cluster_id++;  // extract macro as a single cluster;
          }
        }
      }
    }
    INFO("num_macros : ", num_macros);
  }

  reorderClusters(parts, block);

  int i = 0;

  auto sub_block = [&](const Netlist& graph, const std::vector<size_t>& sub_vertices) {
    auto&& [sub_netlist, cuts] = sub_graph(graph, sub_vertices);
    auto new_block = std::make_shared<imp::Block>(block.get_name() + "_" + std::to_string(i++),
                                                  std::make_shared<imp::Netlist>(std::move(sub_netlist)), block.shared_from_this());
    bool all_instances_fixed = true; // if all instance are fixed, set the cluster fixed
    for (const auto& v : new_block->netlist().vRange()) {
      auto inst = std::dynamic_pointer_cast<Instance>(v.property());
      if (inst && !inst->isFixed()) {
        all_instances_fixed = false;
        break;
      }
    }

    if (all_instances_fixed) {
      new_block->set_fixed();
    }
    // new_block->set_shape(imp::geo::make_box(0, 0, w, h));
    INFO(new_block->get_name(), 
       " num_v: ", new_block->netlist().vSize(), 
       " num_cuts: ", cuts.size(), 
       " num_e: ", new_block->netlist().heSize(),
       " - Fixed: ", new_block->isFixed());
    return new_block;
  };

  int critical_cut = 0;
  int non_critical_cut = 0;
  auto make_cluster_net = [&](const Netlist& graph, size_t id) {
    auto origin_net = graph.hyper_edge_at(id).property();
    auto net_ptr = std::make_shared<Net>("cluster_net");
    net_ptr->set_net_type(NET_TYPE::kSignal);
    if (critical_nets_name.find(origin_net->get_name()) != critical_nets_name.end()) critical_cut++;
    if (non_critical_nets_name.find(origin_net->get_name()) != non_critical_nets_name.end()) non_critical_cut++;
    if (origin_net->isIONet()) {
      // std::cout << "io net" << std::endl;
      net_ptr->set_net_weight(1.0 * origin_net->get_net_weight());  // give io-net double weights
    } else {
      net_ptr->set_net_weight(origin_net->get_net_weight());
    }
    auto parser = std::static_pointer_cast<IDBParser, ParserEngine>(this->parser.lock());

    auto idb_net = parser->get_net2idb().at(origin_net);
    parser->add_net2idb(net_ptr, idb_net);
    return net_ptr;
  };

  auto clusters = clustering(netlist, parts, sub_block, make_cluster_net);
  block.set_netlist(std::make_shared<Netlist>(std::move(clusters)));
  // std::cout<<"critical_cuts = "<<critical_cut<<std::endl;
  // std::cout<<"non_critical_cuts = "<<non_critical_cut<<std::endl;
}

void BlkClustering2::findClusterTypes(std::shared_ptr<Object> vertex_prop, std::vector<ClusterType>& clusterTypes, bool& isFixed) 
{
  if (vertex_prop->isInstance()) {
    auto& inst = dynamic_cast<Instance&>(*vertex_prop);
    if (inst.get_cell_master().isMacro()) {
      clusterTypes.push_back(MACRO);
    } else if (inst.get_cell_master().isIOCell()) {
      clusterTypes.push_back(IO);
    } else { // Perhaps it can be further subdivided
      clusterTypes.push_back(STD);
    }
    if (!inst.isFixed()) isFixed = false;
  } else if (vertex_prop->isBlock()) {
    auto sub_block = std::static_pointer_cast<Block, Object>(vertex_prop);
    for (auto&& sub_vertex : sub_block->netlist().vRange()) {
      findClusterTypes(sub_vertex.property(), clusterTypes, isFixed);
    }
  }
}

void BlkClustering2::reorderClusters(std::vector<size_t>& parts, Block& block)
{
  std::unordered_map<size_t, std::unordered_set<size_t>> cluster_to_cells;
  for (size_t i = 0; i < parts.size(); ++i) {
      cluster_to_cells[parts[i]].insert(i);
  }
  struct ClusterInfo {
    ClusterType type;
    bool isFixed;
    size_t ori_cluster_id;  // 对应原cluster的id
  };

  std::vector<ClusterInfo> clusterInfo;

  for (const auto& parts_set : cluster_to_cells) {
    std::vector<ClusterType> clusterTypes;
    bool isFixed = true;
    for (const auto& i : parts_set.second) {
      auto vertex_prop = block.netlist().vertex_at(i).property();
      findClusterTypes(vertex_prop, clusterTypes, isFixed);
    }
    assert(!clusterTypes.empty() && "clusterTypes should not be empty");
    ClusterType firstType = clusterTypes[0];
    for (const auto& type : clusterTypes) {
      if (type != firstType) {
        firstType = MIX;
        break;
      }
    }
    ClusterInfo info;
    info.type = firstType;
    info.isFixed = isFixed;
    info.ori_cluster_id = parts_set.first;
    clusterInfo.push_back(info);
  }

  std::sort(clusterInfo.begin(), clusterInfo.end(), [](const ClusterInfo& a, const ClusterInfo& b) {
      if (a.type != b.type) {
          return a.type < b.type;  // ClusterType 越小, 优先级越高, STD, MIX, MACRO, IO
      }
      if (a.isFixed != b.isFixed) {
          return !a.isFixed;
      }
      return a.ori_cluster_id < b.ori_cluster_id;
  });

  for (size_t j = 0; j < clusterInfo.size(); j++) {
    ClusterInfo info = clusterInfo[j];
    auto parts_id_set = cluster_to_cells[info.ori_cluster_id];
    for (const auto& i : parts_id_set) {
      parts[i] = j;
    }
  }
}

void BlkClustering2::paramCheck()
{
  if (level_num > 2) {
    ERROR("Only 1 or 2 level_num is supported now");
  }
}

}  // namespace imp
