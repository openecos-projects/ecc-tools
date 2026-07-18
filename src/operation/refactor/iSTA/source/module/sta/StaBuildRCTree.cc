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
 * @file StaBuildRCTree.cc
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief The implemention of build rc tree.
 * @version 0.1
 * @date 2021-04-14
 */
// #include <gperftools/profiler.h>

#include "StaBuildRCTree.hh"

#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "ThreadPool/ThreadPool.h"
#include "delay/ElmoreDelayCalc.hh"
#include "delay/ReduceDelayCal.hh"
#include "log/Log.hh"
#include "netlist/Netlist.hh"

// #define CUDA_DELAY 1

namespace ista {

StaBuildRCTree::StaBuildRCTree(std::string&& spef_file_name,
                               DelayCalcMethod calc_method)
    : _spef_file_name(std::move(spef_file_name)), _calc_method(calc_method) {}

/**
 * @brief Create the rc net for delay calculation.
 *
 * @param net
 * @return std::unique_ptr<RcNet>
 */
std::unique_ptr<RcNet> StaBuildRCTree::createRcNet(Net* net) {
  const char* net_name = net->get_name();
  std::string rc_net_name = net_name;
  std::unique_ptr<RcNet> rc_net;
  if (_calc_method == DelayCalcMethod::kArnoldi) {
    rc_net = std::make_unique<ArnoldiNet>(net);
  } else {
    rc_net = std::make_unique<RcNet>(net);
  }
  return rc_net;
}

/**
 * @brief Build the net rc tree connected to the vertex.
 *
 * @param the_vertex
 * @return unsigned
 */
unsigned StaBuildRCTree::operator()(StaGraph* the_graph) {
  ieda::Stats stats;
  LOG_INFO << "build rc tree start";

  LOG_INFO << "read spef " << _spef_file_name << " start";
  spef::SpefReader spef_parser;
  if (!spef_parser.read(_spef_file_name)) {
    LOG_FATAL << "Parse the spef file error.";
    return 0;
  }

  LOG_INFO << "read spef " << _spef_file_name << " end";

  spef_parser.expandName();
  auto rc_net_common_info = std::make_unique<RCNetCommonInfo>();
  rc_net_common_info->set_spef_cap_unit(spef_parser.getSpefCapUnit());
  rc_net_common_info->set_spef_resistance_unit(spef_parser.getSpefResUnit());
  RcNet::set_rc_net_common_info(std::move(rc_net_common_info));

  // ProfilerStart("rc_tree.prof");
  unsigned is_ok = 1;

  // build rc net
  Netlist* design_nl = the_graph->get_nl();
  Net* net;
  FOREACH_NET(design_nl, net) {
    auto rc_net = createRcNet(net);
    // DLOG_INFO << net->get_name() << "build rc tree";
    getSta()->addRcNet(net, std::move(rc_net));
  }

  // rc net update timing information.
  std::atomic<unsigned> max_node = 0;
  std::string net_name;

#if 1
#if CUDA_DELAY
  std::vector<RcNet*> all_nets;
#endif
  {
    ThreadPool pool(getNumThreads());
    auto* spef_file = spef_parser.getSpefFile();

    std::mutex all_nets_mutex;
    FOREACH_SPEF_NET(spef_file, spef_net) {
      const auto cap_size = static_cast<unsigned>(spef_net.caps.size());
      // record max node net name.
      if (cap_size > max_node) {
        max_node = cap_size;
        net_name = spef_net.name;
      }

      if (cap_size > 100) {
        LOG_INFO_FIRST_N(10)
            << "beyond node num 100 net name " << spef_net.name
            << " node num " << cap_size;
      }

#if CUDA_DELAY
      // enqueue and store future
      pool.enqueue(
          [design_nl, &all_nets, &all_nets_mutex,
           this](const spef::Net* spef_net) {
            auto* design_net = design_nl->findNet(spef_net->name.c_str());
            if (design_net) {
              auto* rc_net = getSta()->getRcNet(design_net);
              rc_net->updateRcTiming(*spef_net);
              if (rc_net->rct() && rc_net->rct()->get_root()) {
                std::lock_guard<std::mutex> lock(all_nets_mutex);
                if (rc_net) {
                  all_nets.emplace_back(rc_net);
                }
              }
            } else {
              LOG_FATAL << "build rc tree not found design net "
                        << spef_net->name;
            }
          },
          &spef_net);
#else
      // enqueue and store future
      pool.enqueue(
          [design_nl, this](const spef::Net* spef_net) {
            auto* design_net = design_nl->findNet(spef_net->name.c_str());
            if (design_net) {
              auto* rc_net = getSta()->getRcNet(design_net);
              // DLOG_INFO << "Update Rc tree timing " << spef_name;
              rc_net->updateRcTiming(*spef_net);
            } else {
              LOG_FATAL << "build rc tree not found design net "
                        << spef_net->name;
            }
          },
          &spef_net);
#endif
    }
  }

#if CUDA_DELAY
  calc_rc_timing(all_nets);
  // printGraphViz get result for debugging.
  // for (const auto net : all_nets) {
  //   if (net->rct()) {
  //     if (net->name() == "FE_OFN0_text_out_80" || net->name() == "CTS_10") {
  //       net->rct()->printGraphViz();
  //     }
  //   }
  // }
#endif
#else
  auto* spef_file = spef_parser.getSpefFile();
#if CUDA_DELAY
  std::vector<RcNet*> all_nets;
#endif
  FOREACH_SPEF_NET(spef_file, spef_net) {
    const auto cap_size = static_cast<unsigned>(spef_net.caps.size());
    // record max node net name.
    if (cap_size > max_node) {
      max_node = cap_size;
      net_name = spef_net.name;
    }

    std::string spef_name = spef_net.name;
    auto* design_net = design_nl->findNet(spef_name.c_str());
    if (design_net) {
      auto* rc_net = getSta()->getRcNet(design_net);
      // DLOG_INFO << "Update Rc tree timing " << spef_name;
      rc_net->updateRcTiming(spef_net);
#if CUDA_DELAY
      if (rc_net->rct()) {
        all_nets.emplace_back(rc_net);
      }
#endif
      // printYaml(spef_net);

    } else {
      LOG_FATAL << "build rc tree not found design net " << spef_name;
    }
  }
#if CUDA_DELAY
  calc_rc_timing(all_nets);
  // printGraphViz get result for debugging.
  // for (const auto net : all_nets) {
  //   if (net->rct()) {
  //     if (net->name() == "CTS_10") {
  //       net->rct()->printGraphViz();
  //     }
  //   }
  // }
#endif

#endif

  LOG_INFO << "net name " << net_name << " max node " << max_node;

  // ProfilerStop();

  // printYamlText("spef.yaml");

  LOG_INFO << "build rc tree end";

  double memory_delta = stats.memoryDelta();
  LOG_INFO << "build rc tree " << memory_delta << "MB";
  double time_delta = stats.elapsedRunTime();
  LOG_INFO << "build rc tree " << time_delta << "s";

  return is_ok;
}

/**
 * @brief print rc tree in yaml format.
 *
 * @param top_node
 * @param spef_net
 */
void StaBuildRCTree::printYaml(const spef::Net& spef_net) {
  static int node_id = 0;
  static std::map<int, std::string> layer_index_to_name{
      {1, "M1"},    {2, "M2"},    {3, "M3"},    {4, "M4"},    {5, "M5"},
      {6, "M6"},    {7, "M7"},    {8, "M8"},    {9, "M9"},    {10, "AP"},
      {11, "VIA1"}, {12, "VIA2"}, {13, "VIA3"}, {14, "VIA4"}, {15, "VIA5"},
      {16, "VIA6"}, {17, "VIA7"}, {18, "VIA8"}, {19, "RV"}};

  std::string net_id = Str::printf("net_%d", node_id++);

  YAML::Node net_node;
  _top_node[net_id] = net_node;

  net_node["NAME"] = spef_net.name;

  // print connection.
  YAML::Node conn_node;
  net_node["CONN"] = conn_node;
  unsigned i = 0;
  FOREACH_SPEF_CONN(spef_net, conn) {
    std::string conn_id = Str::printf("conn_%d", i++);
    YAML::Node one_node;
    conn_node[conn_id] = one_node;
    one_node["type"] =
        (conn.conn_type == spef::ConnectionType::kInternal) ? "I" : "P";
    one_node["node_name"] = conn.pin_port_name;
    one_node["direction"] =
        (conn.conn_direction == spef::ConnectionDirection::kOutput)
            ? "O"
            : (conn.conn_direction == spef::ConnectionDirection::kInput)
                  ? "I"
                  : "IO";
    one_node["coordinate_x"] = conn.coordinate.x;
    one_node["coordinate_y"] = conn.coordinate.y;
    one_node["load"] = conn.load;
    one_node["cell"] = conn.driving_cell;

    one_node["ll_coordinate_x"] = conn.ll_coordinate.x;
    one_node["ll_coordinate_y"] = conn.ll_coordinate.y;

    one_node["ur_coordinate_x"] = conn.ur_coordinate.x;
    one_node["ur_coordinate_y"] = conn.ur_coordinate.y;

    one_node["layer"] = conn.layer;
  }

  // print cap.
  YAML::Node cap_node;
  net_node["CAP"] = cap_node;
  i = 0;
  FOREACH_SPEF_CAP(spef_net, cap) {
    std::string cap_id = Str::printf("cap_%d", i++);
    YAML::Node one_node;
    cap_node[cap_id] = one_node;
    one_node["node1_name"] = cap.node1;
    one_node["node2_name"] = cap.node2;
    one_node["capacitance"] = cap.res_or_cap;
  }

  // print resistance
  YAML::Node resistance_node;
  net_node["RES"] = resistance_node;
  i = 0;
  FOREACH_SPEF_RES(spef_net, res) {
    std::string res_id = Str::printf("resistance_%d", i++);
    YAML::Node one_node;
    resistance_node[res_id] = one_node;
    one_node["node1_name"] = res.node1;
    one_node["node2_name"] = res.node2;
    one_node["resistance"] = res.res_or_cap;
  }
}

/**
 * @brief print yaml text.
 *
 * @param file_name
 */
void StaBuildRCTree::printYamlText(const char* file_name) {
  std::ofstream file(file_name, std::ios::trunc);
  file << _top_node << std::endl;
  file.close();
}

}  // namespace ista
