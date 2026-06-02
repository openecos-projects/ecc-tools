
#include "PyPlaceDB.h"
// #include "ContestDriver.h"
#include <algorithm>
#include <boost/polygon/polygon.hpp>
#include <cassert>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbPins.h"
#include "Lib.hh"
// #include "PowerEngine.hh"
#include "TimingEngine.hh"
#include "TimingIDBAdapter.hh"
#include "Type.hh"
#include "idm.h"
#include "netlist/Instance.hh"
#include "netlist/Pin.hh"
#include "sdc/SdcSetIODelay.hh"
#include "sdc/SdcSetInputTransition.hh"
#include "sdc/SdcSetLoad.hh"
// #include "ContestDriver.h"
#include "sta/StaArc.hh"
// #include "Power.hh"
#include <boost/polygon/polygon.hpp>
#include <vector>

namespace python_interface {

double intersectDistance(Box const& i1, Box const& i2, bool is_x)
{
  coordinate_type l;
  coordinate_type h;
  if (is_x) {
    l = std::max(i1.xl, i2.xl);
    h = std::min(i1.xh, i2.xh);
  } else {
    l = std::max(i1.yl, i2.yl);
    h = std::min(i1.yh, i2.yh);
  }
  return (l < h) ? (double) h - l : 0;
}

/// \return the intersection area of two boxes
double intersectArea(Box const& b1, Box const& b2)
{
  double dist[2] = {intersectDistance(b1, b2, true), intersectDistance(b1, b2, false)};
  return dist[0] * dist[1];
}

// /// \return true if a point is on boundary of the box
// inline bool onBoundary(Box const& b, Point const& p)
// {
//   return (onBoundary(b, p.x()) && contain(b, p.y())) || (onBoundary(b, p.y()) && contain(b.get(kX), p.x()));
// }

bool isInvailidNet(IdbNet* net)
{
  return net->is_ground() || net->is_power() || net->is_pdn() || net->is_clock()
         || net->get_instance_pin_list()->get_pin_list().size() == 0;
}

#if 1

using Graph = std::unordered_map<std::string, std::vector<std::string>>;

bool findOneCycleDFS(const std::string& u, const Graph& graph, std::unordered_map<std::string, int>& visit_status,
                     std::vector<std::string>& current_path_nodes, std::vector<std::string>& cycle_found_nodes)
{
  visit_status[u] = 1;  // Mark as visiting (in recursion stack for this path)
  current_path_nodes.push_back(u);

  if (graph.count(u)) {  // If node u has outgoing edges
    for (const std::string& v : graph.at(u)) {
      if (!graph.count(v) && visit_status.find(v) == visit_status.end()) {
        // Neighbor v is a sink node not explicitly in graph keys, ensure it's in visit_status
        // This case should ideally be handled by ensuring all_nodes are in visit_status initially
        // For robustness, if v is not in visit_status map, it means it's a new sink node.
        // It cannot cause a cycle by itself if it's a new sink.
        // However, for safety, one might initialize it.
        // visit_status[v] = 0; // Or handle as an error if all nodes aren't pre-initialized
      }

      if (visit_status.count(v)) {   // Make sure neighbor v is in the status map
        if (visit_status[v] == 1) {  // Cycle detected: back edge to a node in the current recursion stack
          cycle_found_nodes.clear();
          auto cycle_start_it = std::find(current_path_nodes.begin(), current_path_nodes.end(), v);
          if (cycle_start_it != current_path_nodes.end()) {
            // The cycle includes nodes from 'v' up to 'u' (which is current_path_nodes.back())
            for (auto it = cycle_start_it; it != current_path_nodes.end(); ++it) {
              cycle_found_nodes.push_back(*it);
            }
            cycle_found_nodes.push_back(v);  // Add 'v' again to close the cycle visually (v -> ... -> u -> v)
          } else {
            // This should not happen if v is in recursion_stack (status 1)
            // and current_path_nodes correctly reflects the recursion stack.
            // Fallback or error. For simplicity, we assume path is consistent.
            cycle_found_nodes.push_back(u);  // At least report current node
            cycle_found_nodes.push_back(v);  // and the problematic neighbor
          }
          return true;  // Cycle found
        }
        if (visit_status[v] == 0) {  // If neighbor is unvisited
          if (findOneCycleDFS(v, graph, visit_status, current_path_nodes, cycle_found_nodes)) {
            return true;  // Propagate cycle found signal
          }
        }
        // If visit_status[v] == 2, node v has been fully processed in a previous DFS path, no cycle through it from here.
      } else {
        // This case implies 'v' was a neighbor but not in 'all_nodes' initially, which
        // should be caught by graph consistency checks earlier or by ensuring all_nodes are in visit_status.
        // std::cerr << "Warning: Neighbor " << v << " of node " << u << " not found in visit_status map during DFS." << std::endl;
      }
    }
  }

  current_path_nodes.pop_back();  // Backtrack
  visit_status[u] = 2;            // Mark as fully visited (finished processing descendants)
  return false;                   // No cycle found along this path
}

/**
 * @brief Perform topological sorting and leveling on a given Directed Acyclic Graph (DAG).
 *
 * @param graph Adjacency list representation of the graph. Keys are node names, and values are lists of neighbors.
 * The caller is responsible for constructing this graph based on the design database.
 * @param start_nodes An optional list of nodes considered as starting points for the topological sort (typically primary inputs or
 * flip-flop outputs). If empty, the function will treat all nodes with in-degree 0 as starting points. These nodes will have their levels
 * initialized to 0.
 *
 * @return std::tuple containing:
 * - std::vector<std::string>: List of nodes in topological order. Empty if a cycle is detected.
 * - std::unordered_map<std::string, int>: Levels of each node (longest path length from starting nodes). Empty if a cycle is detected.
 * - bool: True if the graph is a DAG and the operation succeeds; false if a cycle is detected.
 */
std::tuple<std::vector<std::string>, std::unordered_map<std::string, int>, bool> topologicalSortAndLevelize(
    const std::unordered_set<std::string>& all_nodes, const Graph& graph, idm::DataManager* db,
    const std::vector<std::string>& start_nodes = {})
{
  // --- Initialization ---
  std::unordered_map<std::string, int> in_degree;  // Stores the in-degree of each node
  std::unordered_map<std::string, int> levels;     // Stores the level of each node

  // 1. Collect all nodes and calculate in-degrees (based on the provided graph)
  // Note: The caller must ensure that the graph structure is correct and reflects the dependencies between nodes.
  for (const auto& pair : graph) {
    for (const std::string& neighbor : pair.second) {
      in_degree[neighbor]++;
    }
  }
  // Ensure all nodes are initialized in in_degree and levels
  for (const std::string& node : all_nodes) {
    // Initialize levels to -1 or another marker value, indicating not yet visited/computed
    // Source nodes will be set to 0 in the next step
    levels[node] = -1;
    // Ensure nodes that only appear as targets (with no outgoing edges) are included in in_degree
    if (in_degree.find(node) == in_degree.end()) {
      in_degree[node] = 0;
    }
  }

  // 2. Initialize the queue
  std::queue<std::string> q;
  if (start_nodes.empty()) {
    // If no starting nodes are specified, add all nodes with in-degree 0 to the queue
    for (const std::string& node : all_nodes) {
      if (in_degree[node] == 0) {
        q.push(node);
        levels[node] = 0;  // Source nodes have level 0
      }
    }
  } else {
    // If starting nodes are specified (e.g., from start_points_str in your code snippet)
    // Add these specified starting nodes to the queue and set their levels to 0
    // Assume these start_nodes are valid starting points in the graph (in-degree 0 or logical starting points)
    for (const std::string& start_node : start_nodes) {
      if (all_nodes.count(start_node)) {  // Ensure the starting node exists in the graph
                                          // Typically, specified starting nodes should have in-degree 0 or be logical starting points
                                          // if (in_degree[start_node] == 0) { // Optionally add this check
        q.push(start_node);
        levels[start_node] = 0;
        // } else {
        //     std::cerr << "Warning: Specified starting node " << start_node << " has non-zero in-degree." << std::endl;
        // }
      } else {
        std::cerr << "Warning: Specified starting node " << start_node << " is not in the graph." << std::endl;
      }
    }
    // Initialize levels for other nodes (if not already set by starting nodes)
    for (const std::string& node : all_nodes) {
      if (levels[node] == -1) {  // Only initialize nodes not set as starting nodes
                                 // For non-starting nodes, keep -1 or set to 0 or another initial value as needed
                                 // If all nodes are expected to have non-negative levels, keep -1 and handle during updates
      }
    }
  }

  std::vector<std::string> topological_order;  // Stores the result of the topological sort

  // --- Process nodes (Core logic of Kahn's Algorithm) ---
  while (!q.empty()) {
    // Dequeue a node
    std::string u = q.front();
    q.pop();
    topological_order.push_back(u);

    // Check if node u exists in the graph keys (i.e., has outgoing edges)
    if (graph.count(u)) {
      // Process all neighbors v of node u
      for (const std::string& v : graph.at(u)) {
        // Check if neighbor v exists in all_nodes (ensure data consistency)
        if (all_nodes.count(v)) {
          // Update the level of neighbor v
          // Level[v] should be the maximum level of all its predecessors + 1
          // Only update if the current path provides a longer path
          if (levels[u] != -1) {  // Ensure the predecessor's level is computed
            levels[v] = std::max(levels[v], levels[u] + 1);
          }

          // Decrease the in-degree of neighbor v by 1
          in_degree[v]--;

          // If the in-degree of neighbor v becomes 0, add it to the queue
          if (in_degree[v] == 0) {
            q.push(v);
          }
        } else {
          std::cerr << "Error: Graph data inconsistency, neighbor " << v << " of node " << u << " not found in node list." << std::endl;
          // Handle the error as needed, e.g., return failure
        }
      }
    }
  }

  // --- Check for cycles ---
  if (topological_order.size() == all_nodes.size()) {
    // Topological sort succeeded, the graph is a DAG
    // Clean up levels of unreachable nodes (if they remain -1)
    for (auto it = levels.begin(); it != levels.end();) {
      if (it->second == -1) {
        // Handle unreachable nodes, e.g., remove or set to a specific value
        std::cerr << "Warning: Node " << it->first << " is unreachable in the topological sort, level not computed." << std::endl;
        it = levels.erase(it);  // For example, remove
      } else {
        ++it;
      }
    }
    return std::make_tuple(topological_order, levels, true);
  } else {
    // The number of nodes in the topological order is less than the total number of nodes,
    // indicating a cycle (or nodes unreachable due to a cycle).
    std::cerr << "Error: Kahn's algorithm detected a cycle or unreachable nodes! Topological sort failed. Count: "
              << topological_order.size() << "/" << all_nodes.size() << ". Attempting DFS to find a cycle." << std::endl;

    std::vector<std::string> cycle_path_report;
    std::unordered_map<std::string, int> dfs_visit_status;

    for (const std::string& node_name : all_nodes) {
      dfs_visit_status[node_name] = 0;
    }

    bool cycle_found_by_dfs = false;
    for (const std::string& node : all_nodes) {
      if (dfs_visit_status[node] == 0 && (in_degree.count(node) && in_degree.at(node) > 0)) {
        std::vector<std::string> current_dfs_path;
        if (findOneCycleDFS(node, graph, dfs_visit_status, current_dfs_path, cycle_path_report)) {
          cycle_found_by_dfs = true;
          std::cerr << "DFS发现一个环: " << std::endl;  // Newline after initial message
          int nodes_on_line = 0;
          std::string indent = "    ";  // Indentation for subsequent lines
          for (size_t i = 0; i < cycle_path_report.size(); ++i) {
            if (nodes_on_line == 0 && i > 0) {
              std::cerr << indent;
            }
            std::cerr << cycle_path_report[i] << "("
                      << db->get_idb_design()->get_instance_list()->find_instance(cycle_path_report[i])->get_cell_master()->get_name()
                      << ")";
            nodes_on_line++;

            if (i < cycle_path_report.size() - 1) {
              std::cerr << " -> ";
            }

            if (nodes_on_line >= 3 && i < cycle_path_report.size() - 1) {
              std::cerr << std::endl;
              nodes_on_line = 0;
            }
          }
          std::cerr << std::endl;  // Final newline for this cycle report
          break;
        }
      }
    }

    if (!cycle_found_by_dfs && cycle_path_report.empty()) {
      std::cerr << "Kahn算法指示有环，但DFS未从剩余入度 > 0 的节点中分离出特定路径。列出所有剩余入度 > 0 的节点:" << std::endl;
      int nodes_on_line = 0;
      std::string indent = "    ";
      bool first_node_printed = false;
      for (const std::string& n : all_nodes) {
        if (in_degree.count(n) && in_degree.at(n) > 0) {
          cycle_path_report.push_back(n);                  // Still populate for return
          if (nodes_on_line == 0 && first_node_printed) {  // Indent if not the first line of this section
            std::cerr << indent;
          } else if (nodes_on_line == 0 && !first_node_printed) {
            std::cerr << indent;  // Indent the very first line of nodes too
          }
          std::cerr << "- 节点: " << n << " (最终入度: " << in_degree.at(n) << ")  ";
          nodes_on_line++;
          first_node_printed = true;
          if (nodes_on_line >= 2) {  // Print 2 such verbose items per line
            std::cerr << std::endl;
            nodes_on_line = 0;
          }
        }
      }
      if (nodes_on_line > 0) {  // Ensure a final newline if the last line wasn't full
        std::cerr << std::endl;
      }

      if (cycle_path_report.empty() && topological_order.size() != all_nodes.size()) {
        std::cerr << "警告：Kahn算法失败，但没有入度 > 0 的节点且DFS未找到特定环。这可能表示未处理的断开组件。" << std::endl;
        std::cerr << "不在拓扑顺序中的节点:" << std::endl;
        nodes_on_line = 0;  // Reset for this section
        first_node_printed = false;
        for (const std::string& n : all_nodes) {
          if (std::find(topological_order.begin(), topological_order.end(), n) == topological_order.end()) {
            cycle_path_report.push_back(n);
            if (nodes_on_line == 0 && first_node_printed) {
              std::cerr << indent;
            } else if (nodes_on_line == 0 && !first_node_printed) {
              std::cerr << indent;
            }
            std::cerr << n << "  ";
            nodes_on_line++;
            first_node_printed = true;
            if (nodes_on_line >= 4) {  // 4 nodes per line for this list
              std::cerr << std::endl;
              nodes_on_line = 0;
            }
          }
        }
        if (nodes_on_line > 0) {
          std::cerr << std::endl;
        }
      }
    }

    return std::make_tuple(cycle_path_report, std::unordered_map<std::string, int>(), false);
  }
}

struct TempLibArc
{
  int _lib_arc_idx;
  ista::LibArc* _lib_arc_ptr;
  explicit TempLibArc(int lib_arc_idx, ista::LibArc* lib_arc_ptr) : _lib_arc_idx(lib_arc_idx), _lib_arc_ptr(lib_arc_ptr) {}
  int senseToInt() const
  {
    switch (_lib_arc_ptr->get_timing_sense()) {
      case ista::LibArc::TimingSense::kPositiveUnate:
        return 1;
      case ista::LibArc::TimingSense::kNonUnate:
        return 0;
      case ista::LibArc::TimingSense::kNegativeUnate:
        return -1;
      default:
        return 1;
    }
  }
  int typeToInt() const
  {
    switch (_lib_arc_ptr->get_timing_type()) {
      case ista::LibArc::TimingType::kRisingEdge:
        return 1;
      case ista::LibArc::TimingType::kFallingEdge:
        return -1;
      default:
        return 0;
    }
  }
};

// 统一的LUT表格初始化函数
void PyPlaceDB::init_lut_table_unified(pybind11::list& flat_luts_values, pybind11::list& flat_luts_axis1_table,
                                       pybind11::list& flat_luts_axis2_table, pybind11::list& flat_luts_dim, ista::LibTable* table,
                                       ista::LibCell* lib_cell, bool is_constraint_table)
{
  pybind11::list luts_dim;
  pybind11::list luts_values;
  pybind11::list luts_axis1_table;
  pybind11::list luts_axis2_table;

  if (table == nullptr) {
    flat_luts_values.append(luts_values);
    flat_luts_axis1_table.append(luts_axis1_table);
    flat_luts_axis2_table.append(luts_axis2_table);
    luts_dim.append(0);
    luts_dim.append(0);
    flat_luts_dim.append(luts_dim);
    return;
  }
  auto& axes = table->get_axes();
  auto time_unit = lib_cell->get_owner_lib()->get_time_unit();
  double time_coeff = 1.0;
  if (TimeUnit::kPS == time_unit) {
  } else if (TimeUnit::kFS == time_unit) {
    time_coeff /= 1000;
  } else if (TimeUnit::kNS == time_unit) {
    time_coeff *= 1000;
  } else {
    std::cerr << "Error: unsupported time unit: " << static_cast<int>(time_unit) << std::endl;
    exit(1);
  }

  double cap_coeff = 1.0;
  if (!is_constraint_table) {
    auto cap_unit = lib_cell->get_owner_lib()->get_cap_unit();
    if (CapacitiveUnit::kPF == cap_unit) {
    } else if (CapacitiveUnit::kFF == cap_unit) {
      cap_coeff /= 1000;
    } else if (CapacitiveUnit::kF == cap_unit) {
      cap_coeff *= 1e12;
    } else {
      std::cerr << "Error: unsupported cap unit: " << static_cast<int>(cap_unit) << std::endl;
      exit(1);
    }
  }
  // 根据表格类型确定轴的含义和顺序
  int AXIS1, AXIS2;
  int num_axis1, num_axis2;
  if (table->get_table_values().size() == 1) {
    // scalar constraint table
    luts_dim.append(1);
    luts_dim.append(1);
    luts_values.append(table->get_table_values().at(0)->getFloatValue() * time_coeff);
    flat_luts_values.append(luts_values);
    flat_luts_axis1_table.append(luts_axis1_table);
    flat_luts_axis2_table.append(luts_axis2_table);
    flat_luts_dim.append(luts_dim);
    return;
  }
  if (axes.size() == 1) {
    // 维度信息
    assert(!is_constraint_table);
    bool first_var_is_intrans
        = table->get_table_template()->get_template_variable1() == ista::LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION;
    double coeff = first_var_is_intrans ? time_coeff : cap_coeff;
    int size1 = table->get_axes().at(0)->get_axis_size();

    for (auto& vpt : table->getAxis(0).get_axis_values()) {
      auto val = vpt.get()->getFloatValue();
      val *= coeff;
      if (first_var_is_intrans) {
        luts_axis1_table.append(val);
      } else {
        luts_axis2_table.append(val);
      }
    }
    // 填充一维表格的数据值
    for (auto& vptr : table->get_table_values()) {
      double val = vptr.get()->getFloatValue();
      val *= coeff;
      luts_values.append(val);
    }
    if (first_var_is_intrans) {
      luts_dim.append(size1);
      luts_dim.append(0);
    } else {
      luts_dim.append(0);
      luts_dim.append(size1);
    }

    flat_luts_values.append(luts_values);
    flat_luts_axis1_table.append(luts_axis1_table);
    flat_luts_axis2_table.append(luts_axis2_table);
    flat_luts_dim.append(luts_dim);
    return;
  }

  if (is_constraint_table) {
    // 约束表格：轴1是clk transition，轴2是data transition
    AXIS1 = table->get_table_template()->get_template_variable1() == ista::LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION ? 0 : 1;
    AXIS2 = !AXIS1;
    num_axis1 = table->get_axes().at(AXIS1)->get_axis_size();  // clk transition
    num_axis2 = table->get_axes().at(AXIS2)->get_axis_size();  // data transition
  } else {
    // 延迟/转换表格：轴1是transition，轴2是capacitance
    AXIS1 = table->get_table_template()->get_template_variable1() == ista::LibLutTableTemplate::Variable::INPUT_NET_TRANSITION ? 0 : 1;
    AXIS2 = !AXIS1;
    num_axis1 = table->get_axes().at(AXIS1)->get_axis_size();  // transition
    num_axis2 = table->get_axes().at(AXIS2)->get_axis_size();  // capacitance
  }

  luts_dim.append(num_axis1);
  luts_dim.append(num_axis2);

  // 处理轴1的值（transition 或 data_transition）
  for (auto& value : table->getAxis(AXIS1).get_axis_values()) {
    auto val = value.get()->getFloatValue() * time_coeff;
    luts_axis1_table.append(val);
  }

  // 处理轴2的值（capacitance 或 clk_transition）
  for (auto& value : table->getAxis(AXIS2).get_axis_values()) {
    auto val = value.get()->getFloatValue();
    if (is_constraint_table) {
      val *= time_coeff;  // 约束表格两个轴都是时间单位
    } else {
      val *= cap_coeff;  // 延迟表格轴2是电容单位
    }
    luts_axis2_table.append(val);
  }

  // 处理表格数据值，考虑转置
  std::vector<std::unique_ptr<LibAttrValue>>& table_values = table->get_table_values();
  bool need_transpose = (AXIS1 == 1);  // 如果轴1是第二个轴，需要转置

  if (need_transpose) {
    // 转置：原始数据按轴2主导存储，转换为按轴1主导存储
    for (int i = 0; i < num_axis1; i++) {
      for (int j = 0; j < num_axis2; j++) {
        int original_index = j * num_axis1 + i;  // 轴2主导的索引
        auto val = table_values[original_index].get()->getFloatValue() * time_coeff;
        luts_values.append(val);
      }
    }
  } else {
    // 不需要转置，按原始顺序处理
    for (auto& value : table_values) {
      auto val = value.get()->getFloatValue() * time_coeff;
      luts_values.append(val);
    }
  }

  flat_luts_values.append(luts_values);
  flat_luts_axis1_table.append(luts_axis1_table);
  flat_luts_axis2_table.append(luts_axis2_table);
  flat_luts_dim.append(luts_dim);
}

void PyPlaceDB::init_timing(idm::DataManager* db, std::unordered_map<std::string, int>& mPin2ID,
                            std::unordered_map<std::string, int>& mClkPin2ID, std::map<std::string, index_type>& mNodeName2ID,
                            std::vector<IdbInstance*>& inst_resort_list, int ext_blockage_num)
{
  /*************************************************************************/
  std::map<std::string, double> sdc_inrdelays;       //
  std::map<std::string, double> sdc_infdelays;       //
  std::map<std::string, double> sdc_inrtrans;        //
  std::map<std::string, double> sdc_inftrans;        //
  std::map<std::string, double> sdc_outcaps;         //
  std::map<std::string, double> sdc_endpoints_rRAT;  //
  std::map<std::string, double> sdc_endpoints_fRAT;  //
  std::map<std::string, double> name2clk_rtran;      //
  std::map<std::string, double> name2clk_ftran;      //
  std::map<std::string, std::string> name2clk_pin_names;
  /*--------------------------------sdc init------------------------------------------*/
  std::set<std::string> FFs_str;  // mNodeName2ID
  auto timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto ista = timing_engine->get_ista();
  SdcConstrain* the_constrain = ista->getConstrain();
  auto& sdc_io_constraints = the_constrain->get_sdc_io_constraints();
  for (auto& io_constraint : sdc_io_constraints) {
    if (io_constraint->isSetInputDelay()) {
      auto set_io_delay = dynamic_cast<SdcSetIODelay*>(io_constraint.get());
      auto& objs = set_io_delay->get_objs();
      double delay_value = set_io_delay->get_delay_value();
      for (auto* obj : objs) {
        std::string pin_or_port_name = obj->getFullName();
        if (set_io_delay->isRise() && set_io_delay->isMax()) {
          sdc_inrdelays[pin_or_port_name] = delay_value;
        } else if (set_io_delay->isRise() && set_io_delay->isMin()) {
        } else if (set_io_delay->isFall() && set_io_delay->isMax()) {
          sdc_infdelays[pin_or_port_name] = delay_value;
        } else if (set_io_delay->isFall() && set_io_delay->isMin()) {
        }
      }
    }
    if (io_constraint->isSetInputTransition()) {
      auto* set_input_transition = dynamic_cast<SdcSetInputTransition*>(io_constraint.get());
      double slew = set_input_transition->get_transition_value();
      auto& objs = set_input_transition->get_objs();
      for (auto* obj : objs) {
        if (set_input_transition->isMax() && set_input_transition->isRise()) {
          sdc_inrtrans[obj->getFullName()] = slew;
        } else if (set_input_transition->isMax() && set_input_transition->isFall()) {
          sdc_inftrans[obj->getFullName()] = slew;
        } else if (set_input_transition->isMin() && set_input_transition->isRise()) {
          // inrtrans[obj->getFullName()].append(slew);
        } else if (set_input_transition->isMin() && set_input_transition->isFall()) {
          // inftrans[obj->getFullName()].append(slew);
        }
      }
    }
    if (io_constraint->isSetLoad()) {
      auto* set_load = dynamic_cast<SdcSetLoad*>(io_constraint.get());
      double load = set_load->get_load_value();
      auto& objs = set_load->get_objs();
      for (auto* obj : objs) {
        if (set_load->isMax() && set_load->isRise()) {
          sdc_outcaps[obj->getFullName()] = load;
        } else if (set_load->isMax() && set_load->isFall()) {
          sdc_outcaps[obj->getFullName()] = load;
        } else if (set_load->isMin() && set_load->isRise()) {
          // outcaps[obj->getFullName()].append(load);
        } else if (set_load->isMin() && set_load->isFall()) {
          // outcaps[obj->getFullName()].append(load);
        }
      }
    }

    if (io_constraint->isSetOutputDelay()) {
      auto* set_io_delay = dynamic_cast<SdcSetIODelay*>(io_constraint.get());
      double out_delay = NS_TO_PS(set_io_delay->get_delay_value());
      auto& objs = set_io_delay->get_objs();
      for (auto* obj : objs) {
        auto sta_clk_vertex = ista->findVertex(obj->getFullName().c_str());  // input can only find vertex
        double clock_period = DBL_MAX;
        if (sta_clk_vertex && sta_clk_vertex->getClockBucket().bucket_size()) {
          clock_period = NS_TO_PS(sta_clk_vertex->getPropClock()->getPeriodNs());
          // end_points_str.push_back(pin->get_pin_name());
          // sdc_endpoints_fRAT[pin->get_pin_name()] = clock_period;
          // sdc_endpoints_rRAT[pin->get_pin_name()] = clock_period;
          // sdc_outcaps[pin->get_pin_name()] = 0;  // FIXME
        }
        // TODO: Need to calc clock period, clock_uncertainty
        if (set_io_delay->isMax() && set_io_delay->isRise()) {
          sdc_endpoints_rRAT[obj->getFullName()] = clock_period - out_delay;  // clock_uncertainty
        } else if (set_io_delay->isMax() && set_io_delay->isFall()) {
          sdc_endpoints_fRAT[obj->getFullName()] = clock_period - out_delay;  // clock_uncertainty
        } else if (set_io_delay->isMin() && set_io_delay->isRise()) {
          // outcaps[obj->getFullName()].append(load);
        } else if (set_io_delay->isMin() && set_io_delay->isFall()) {
          // outcaps[obj->getFullName()].append(load);
        }
      }
    }
  }

  /*************************************************************************/
  /*--------------------------------timing init------------------------------------------*/
  auto& _sta = ista;

  auto db_deisgn = db->get_idb_design();
  if (mClkPin2ID.empty()) {
    int clk_pin_id = 0;
    for (auto instance : db_deisgn->get_instance_list()->get_instance_list()) {
      string instance_name = instance->get_name();
      auto* design_netlist = ista->get_netlist();
      auto* instance_temp = design_netlist->findInstance(instance_name.c_str());
      ista::Pin* sta_pin = nullptr;
      FOREACH_INSTANCE_PIN(instance_temp, sta_pin)
      {
        if (sta_pin->get_cell_port()->isClock()) {
          mClkPin2ID[instance_name + sta_pin->get_name()] = clk_pin_id++;
        }
      }
    }
  }
  /*--------------------------------topo init------------------------------------------*/
  /* topo*/

  // pybind11::list cells_by_level;          //
  // pybind11::list cells_by_reverse_level;  //

  std::vector<string> start_points_str;  // PIs and FFs' output pins
  std::vector<string> clock_pins_str;    // FFs' clock pins
  std::vector<string> end_points_str;    // POs and FFs' input pins
  int tot_unconstrained_io_pins = 0;
  for (auto pin : db_deisgn->get_io_pin_list()->get_pin_list()) {
    if (pin->get_net() == nullptr || pin->get_net()->is_ground() || pin->get_net()->is_power() || pin->get_net()->is_pdn()
        || pin->get_net()->is_clock()) {
      continue;
    }
    if (pin->is_primary_input()) {
      start_points_str.push_back(pin->get_pin_name());
      // DEBUG

      auto net = pin->get_net();
      if (net) {
        auto netname = net->get_net_name();
        std::cout << "Primary input pin found: " << pin->get_pin_name() << " on net: " << netname << std::endl;
      }
      if (sdc_infdelays.count(pin->get_pin_name()) == 0) {
        std::cout << "Warning: Primary input pin " << pin->get_pin_name() << " has no fall delay constraint., set to default -1e8"
                  << std::endl;
        sdc_infdelays[pin->get_pin_name()] = -1e8;
      }
      if (sdc_inrdelays.count(pin->get_pin_name()) == 0) {
        std::cout << "Warning: Primary input pin " << pin->get_pin_name() << " has no rise delay constraint., set to default -1e8"
                  << std::endl;
        sdc_inrdelays[pin->get_pin_name()] = -1e8;
      }
      auto instance = pin->get_instance();
      if (instance) {
        auto instname = instance->get_name();
        // std::cout << "Instance for primary input pin: " << instname << std::endl;
      }
      // DEBUG
    } else if (pin->is_primary_output()) {
      // auto sta_clk_vertex = ista->findVertex(pin->get_pin_name().c_str());  // input can only find vertex
      end_points_str.push_back(pin->get_pin_name());
      if (sdc_outcaps.count(pin->get_pin_name()) == 0) {
        sdc_outcaps[pin->get_pin_name()] = 0;
      }
      if (sdc_endpoints_fRAT.count(pin->get_pin_name()) == 0) {
        tot_unconstrained_io_pins += 1;
        printf("Error: Primary output pin %s has no fRAT constraint, set to default 9e7 ps\n", pin->get_pin_name().c_str());
        sdc_endpoints_fRAT[pin->get_pin_name()] = 9e7;  // NOTE: less than 1e8 in timing propagation
      }
      if (sdc_endpoints_rRAT.count(pin->get_pin_name()) == 0) {
        tot_unconstrained_io_pins += 1;
        printf("Error: Primary output pin %s has no rRAT constraint, set to default 9e7 ps\n", pin->get_pin_name().c_str());
        sdc_endpoints_rRAT[pin->get_pin_name()] = 9e7;
      }
      // if (sta_clk_vertex && sta_clk_vertex->getClockBucket().bucket_size()) {
      //
      //   sdc_endpoints_fRAT[pin->get_pin_name()] = NS_TO_PS(clock_period);
      //   sdc_endpoints_rRAT[pin->get_pin_name()] = NS_TO_PS(clock_period);
      // }
    }
  }
  printf("Total primary output pins without RAT constraints: %d\n", tot_unconstrained_io_pins);
  for (auto instance : db_deisgn->get_instance_list()->get_instance_list()) {
    auto lib_cell = _sta->findLibertyCell(instance->get_cell_master()->get_name().c_str());
    bool is_clock = false;
    string instance_name = instance->get_name();

    ista::Pin* clock_pin = nullptr;
    auto* design_netlist = ista->get_netlist();
    auto* instance_temp = design_netlist->findInstance(instance_name.c_str());
    auto& the_graph = ista->get_graph();
    ista::Pin* sta_pin = nullptr;
    if (instance_temp == nullptr) {
      std::cout << "Warning: Instance " << instance_name << " not found in iSTA netlist." << std::endl;
      continue;
    }
    FOREACH_INSTANCE_PIN(instance_temp, sta_pin)
    {
      if (sta_pin->get_cell_port()->isClock()) {
        // DEBUG
        auto curnet = sta_pin->get_net();
        FFs_str.insert(instance_name);
        if (curnet) {
          auto netname = curnet->getFullName();
          // std::cout << "Clock pin: " << sta_pin->getFullName() << " in net: " << netname << " isClock:" << curnet->isClockNet()
          //           << std::endl;

        } else {
          // std::cout << "Clock pin: " << sta_pin->getFullName() << " in net: NULL" << std::endl;
        }
        // DEBUG
        clock_pin = sta_pin;  // Found a clock pin
        clock_pins_str.push_back(string(sta_pin->get_own_instance()->get_name()) + sta_pin->get_name());
        auto sta_clk_vertex = the_graph.findVertex(clock_pin).value();  // input can only find vertex
        if (!sta_clk_vertex->getClockBucket().empty() && sta_clk_vertex->getPropClock()) {
          is_clock = true;
          break;
        } else {
          std::cout << "Warning: Clock pin " << clock_pin->getFullName() << " has no associated clock in STA." << std::endl;
        }
      }
    }

    if (is_clock) {
      auto sta_clk_vertex = the_graph.findVertex(clock_pin).value();  // input can only find vertex
      StaArc* setup_arc = nullptr;
      for (auto* snk_arc : sta_clk_vertex->get_src_arcs()) {
        if (snk_arc->isSetupArc()) {
          setup_arc = snk_arc;
          break;
        }
      }

      assert(setup_arc != nullptr);
      // double setup_time_rise = FS_TO_PS(setup_arc->get_arc_delay(AnalysisMode::kMax, TransType::kRise));
      // double setup_time_fall = FS_TO_PS(setup_arc->get_arc_delay(AnalysisMode::kMax, TransType::kFall));
      assert(sta_clk_vertex->getPropClock());

      // note: us ```PS``` in the db.
      double clock_period = NS_TO_PS(sta_clk_vertex->getPropClock()->getPeriodNs());
      for (auto pin : instance->get_pin_list()->get_pin_list()) {
        string pin_full_name = instance->get_name() + pin->get_pin_name();
        // DEBUG
        if (pin_full_name.find("CLK") != string::npos || pin_full_name.find("clk") != string::npos) {
          // clock pin
          // std::cout << "Clock pin found: " << pin_full_name << std::endl;
        }
        // DEBUG
        if (mPin2ID.count(pin_full_name)) {
          assert(clock_pin->get_name() != pin->get_pin_name());
          // driven pin
          if (pin->is_net_pin() && pin->get_term()->get_direction() == IdbConnectDirection::kOutput) {
            start_points_str.push_back(pin_full_name);
            // NOTE: clk -> q arc has been considered in timing propagation
            sdc_inrdelays[pin_full_name] = 0;  //
            sdc_infdelays[pin_full_name] = 0;  //
            sdc_inrtrans[pin_full_name] = 0;   //
            sdc_inftrans[pin_full_name] = 0;   //
          } else if (pin->is_net_pin() && pin->get_term()->get_direction() == IdbConnectDirection::kInput) {
            end_points_str.push_back(pin_full_name);
            sdc_outcaps[pin_full_name] = 0;
            // NOTE: clk -> d arc has been considered in timing propagation
            sdc_endpoints_fRAT[pin_full_name] = clock_period;  // - setup time - clock_uncertainty
            sdc_endpoints_rRAT[pin_full_name] = clock_period;  // - setup time - clock_uncertainty
          }
        } else if (mClkPin2ID.count(pin_full_name)) {
          // clock pin
          if (pin->is_net_pin() && pin->get_term()->get_direction() == IdbConnectDirection::kInput) {
            string _sta_pin_name = instance->get_name() + ":" + pin->get_pin_name();
            double r_clk_slew_ns = timing_engine->getSlew(_sta_pin_name.c_str(), AnalysisMode::kMax, TransType::kRise);
            double f_clk_slew_ns = timing_engine->getSlew(_sta_pin_name.c_str(), AnalysisMode::kMax, TransType::kFall);
            name2clk_pin_names[pin_full_name] = _sta_pin_name;
            name2clk_rtran[pin_full_name] = NS_TO_PS(r_clk_slew_ns);  // Note: clk in slew from iSTA
            name2clk_ftran[pin_full_name] = NS_TO_PS(f_clk_slew_ns);
          }
        }
      }
    }
  }
  // assert(name2clk_rtran.size() == FFs_str.size() * 2);

  for (auto& ff_name : FFs_str) {
    if (mNodeName2ID.count(ff_name)) {
      FF_ids.append(mNodeName2ID[ff_name]);
    }
  }
  if (!name2clk_ftran.empty()) {
    int num_clk_pins = mClkPin2ID.size();
    std::vector<double> rtrans_vector(num_clk_pins);
    std::vector<double> ftrans_vector(num_clk_pins);
    std::vector<std::string> clk_pin_names_vector(num_clk_pins);
    for (const auto& [clk_pin_name, clk_pin_id] : mClkPin2ID) {
      if (clk_pin_id < num_clk_pins && name2clk_rtran.count(clk_pin_name)) {
        rtrans_vector[clk_pin_id] = name2clk_rtran[clk_pin_name];
        ftrans_vector[clk_pin_id] = name2clk_ftran[clk_pin_name];
        clk_pin_names_vector[clk_pin_id] = name2clk_pin_names[clk_pin_name];
      }
    }
    clk_pin_rtran = pybind11::cast(rtrans_vector);
    clk_pin_ftran = pybind11::cast(ftrans_vector);
    clk_pin_names = pybind11::cast(clk_pin_names_vector);
  }
  for (auto& pin_name : start_points_str) {
    if (mPin2ID.count(pin_name)) {
      start_points.append(mPin2ID[pin_name]);
      inrdelays.append(sdc_inrdelays[pin_name]);  //
      infdelays.append(sdc_infdelays[pin_name]);  //
      inrtrans.append(sdc_inrtrans[pin_name]);    //
      inftrans.append(sdc_inftrans[pin_name]);    //
    }
  }
  for (auto& pin_name : clock_pins_str) {
    if (mClkPin2ID.count(pin_name)) {
      clock_pins.append(mClkPin2ID[pin_name]);
    }
  }
  for (auto& pin_name : end_points_str) {
    if (mPin2ID.count(pin_name)) {
      end_points.append(mPin2ID[pin_name]);
      outcaps.append(sdc_outcaps[pin_name]);  //
      // printf("End point: %s, pin id: %d, sdc_endpoints_rRAT: %.3f\n", pin_name.c_str(), mPin2ID[pin_name], sdc_endpoints_rRAT[pin_name]);
      endpoints_rRAT.append(sdc_endpoints_rRAT[pin_name]);  //
      endpoints_fRAT.append(sdc_endpoints_fRAT[pin_name]);  //
    }
  }

  Graph forward_graph;  //
  Graph reverse_graph;  //
  std::unordered_set<std::string> all_nodes;
  for (auto inst : db_deisgn->get_instance_list()->get_instance_list()) {
    string inst_name = inst->get_name();
    if (!FFs_str.count(inst_name) && mNodeName2ID.count(inst_name)) {
      all_nodes.insert(inst_name);
    }
  }
  for (auto net : db_deisgn->get_net_list()->get_net_list()) {
    assert(net->get_driving_pin());
    if (isInvailidNet(net)) {
      continue;
    }
    auto from_inst = net->get_driving_pin()->get_instance();
    if (from_inst == nullptr) {
      continue;
    }
    auto from_inst_name = net->get_driving_pin()->get_instance()->get_name();

    if (FFs_str.count(from_inst_name) || from_inst->is_flip_flop()) {
      continue;
    }
    for (auto pin : net->get_load_pins()) {
      // TODO: Fix clock gate cell.
      // FIXME: clock_instance may be the clock gate cell, which is not a flip flop.
      // pin->get_instance()->is_clock_instance()
      if (pin->get_instance() == nullptr || pin->get_instance()->is_flip_flop()) {
        continue;
      }
      string to_inst_name = pin->get_instance()->get_name();
      if (FFs_str.count(to_inst_name)) {
        continue;
      }
      // printf(" %s -> %s\n", from_inst_name.c_str(), to_inst_name.c_str());
      forward_graph[from_inst_name].push_back(to_inst_name);
      reverse_graph[to_inst_name].push_back(from_inst_name);
    }
  }
  auto [_t1, forward_node_levels, forward_is_dag] = topologicalSortAndLevelize(all_nodes, forward_graph, db);
  auto [_t2, reverse_node_levels, reverse_is_dag] = topologicalSortAndLevelize(all_nodes, reverse_graph, db);
  assert(forward_is_dag && reverse_is_dag);
  std::map<int, std::vector<std::string>> forward_level_to_nodes;
  std::map<int, std::vector<std::string>> reverse_level_to_nodes;
  for (const auto& pair : forward_node_levels) {
    forward_level_to_nodes[pair.second].push_back(pair.first);
  }
  for (const auto& pair : reverse_node_levels) {
    reverse_level_to_nodes[pair.second].push_back(pair.first);
  }

  // pybind11::list cells_by_level;          //
  // pybind11::list cells_by_reverse_level;  //
  int level_cells_idx = 0;
  for (auto& [level, inst_list] : forward_level_to_nodes) {
    flat_cells_by_level_start.append(level_cells_idx);
    for (auto& inst_name : inst_list) {
      if (mNodeName2ID.count(inst_name)) {
        flat_cells_by_level.append(mNodeName2ID[inst_name]);
        level_cells_idx++;
      }
    }
  }
  flat_cells_by_level_start.append(level_cells_idx);

  int reverse_level_cells_idx = 0;
  for (auto& [level, inst_list] : reverse_level_to_nodes) {
    flat_cells_by_reverse_level_start.append(reverse_level_cells_idx);
    for (auto& inst_name : inst_list) {
      if (mNodeName2ID.count(inst_name)) {
        flat_cells_by_reverse_level.append(mNodeName2ID[inst_name]);
        reverse_level_cells_idx++;
      }
    }
  }
  flat_cells_by_reverse_level_start.append(reverse_level_cells_idx);

  /*--------------------------------net arcs init------------------------------------------*/
  int net_arc_count = 0;
  for (auto net : db_deisgn->get_net_list()->get_net_list()) {
    if (isInvailidNet(net)) {
      continue;
    }
    net_flat_arcs_start.append(net_arc_count);  //
    string pin_full_name;
    if (net->get_driving_pin()->is_io_pin()) {
      pin_full_name = net->get_driving_pin()->get_pin_name();
    } else {
      pin_full_name = net->get_driving_pin()->get_instance()->get_name() + net->get_driving_pin()->get_pin_name();
    }
    assert(mPin2ID.count(pin_full_name));
    int from_pin_id = mPin2ID[pin_full_name];
    int arc_num = 0;  //
    net2driver_pin_map.append(from_pin_id);
    for (auto pin : net->get_load_pins()) {
      string pin_full_name;
      if (pin->is_io_pin()) {
        pin_full_name = pin->get_pin_name();
      } else {
        pin_full_name = pin->get_instance()->get_name() + pin->get_pin_name();
      }
      assert(mPin2ID.count(pin_full_name));
      int to_pin_id = mPin2ID[pin_full_name];
      pybind11::list arc;
      arc.append(from_pin_id);
      arc.append(to_pin_id);
      net_flat_arcs.append(arc);
      arc_num++;
    }
    net_arc_count += arc_num;
  }
  net_flat_arcs_start.append(net_arc_count);

  /*--------------------------------cell main type init------------------------------------------*/
  vector<LibLibrary*> equiv_libs;
  auto& all_libs = _sta->getAllLib();
  for (auto& lib : all_libs) {
    equiv_libs.push_back(lib.get());
  }

  _sta->makeClassifiedCells(equiv_libs);

  int cell_flat_arc_count = 0;
  std::unordered_set<size_t> main_types;
  std::map<size_t, std::vector<LibCell*>> main_type_libcells;
  std::map<size_t, std::unordered_map<std::string, int>> main_type_with_width;
  std::unordered_map<std::string, size_t> cell_type2main_type;

  std::map<size_t, int> main_type2main_id;
  std::unordered_map<std::string, int> cell_type2cell_id;  // cell_id = libcell_start[main_id] + width
  int main_idx = 0;
  for (auto node : inst_resort_list) {
    string cell_type = node->get_cell_master()->get_name();
    // if ("sg13g2_o21ai_1" == cell_type) {
    //   std::cout << "sg13g2_o21ai_1" << std::endl;
    // }
    auto lib_cell = _sta->findLibertyCell(cell_type.c_str());
    if (cell_type2main_type.count(lib_cell->get_cell_name()) == 0) {
      size_t main_type = main_idx;
      std::vector<LibCell*> lib_cells;
      main_types.insert(main_type);
      lib_cells.push_back(lib_cell);
      if (_sta->classifyCells(lib_cell)) {
        for (auto lib_cell_other : *(_sta->classifyCells(lib_cell))) {
          lib_cells.push_back(lib_cell_other);
        };
      }
      for (auto cell_t : lib_cells) {
        // assert(cell_t->get_cell_leakage_power());
        cell_type2main_type[cell_t->get_cell_name()] = main_type;
      }
      main_type_libcells[main_type] = lib_cells;
      main_idx++;
    }
  }
  int main_id_idx = 0;
  int cell_id_idx = 0;
  for (auto& [main_type, lib_cells] : main_type_libcells) {
    main_type2main_id[main_type] = main_id_idx;
    std::sort(lib_cells.begin(), lib_cells.end(),
              [](LibCell* a, LibCell* b) { return a->get_cell_leakage_power() < b->get_cell_leakage_power(); });  //
    // if (lib_cells.size() >= 1000) {
    //   std::cerr << "Error: too many cells in one main type, please check the library." << std::endl;
    //   exit(1);
    // }
    for (size_t size = 0; size < lib_cells.size(); ++size) {
      const auto lib_cell = lib_cells[size];
      // main_type_libcells[main_type].push_back(lib_cell);
      main_type_with_width[main_type][lib_cell->get_cell_name()] = size;
      cell_type2main_type[lib_cell->get_cell_name()] = main_type;
      cell_type2cell_id[lib_cell->get_cell_name()] = cell_id_idx;
      cell_id_idx++;
    }
    main_id_idx++;
  }

  /*--------------------------------lib cell arcs info------------------------------------------*/
  /*
    info2arc_idx:   [main_type, from_lib_pin, to_lib_pin] -> arc_idx

    flat_luts_values: torch.Tensor      # [ARC_NUM, MaxT, MaxC]
    flat_luts_trans_table : torch.Tensor  # [ARC_NUM, MaxT]
    flat_luts_cap_table : torch.Tensor   # [ARC_NUM, MaxC]
    flat_luts_dim: torch.Tensor        # [ARC_NUM, 3] - Actual dims [trans_dim]

    cells [main_type, size, lib_arc_idx] -> [, arc_idx]

    cell_type_arc_start :

    main_type -> cell_types -> arc_idx ->
  */
  std::unordered_map<std::string, std::vector<TempLibArc>> info2lib_arcs;
  int main_type_id = 0;
  int lib_cell_idx = 0;
  int arc_idx = 0;
  int lib_pin_idx = 0;
  std::unordered_map<std::string, int> libpin_name2libpin_offset;
  for (auto& [main_type, lib_cells] : main_type_libcells) {
    // auto lib_cell_t = lib_cells.at(0);
    // for (auto& arc_set : lib_cell_t->get_cell_arcs()) {
    //   for (auto& arc : arc_set->get_arcs()) {
    //     auto from_lib_pin = arc->get_src_port();
    //     auto to_lib_pin = arc->get_snk_port();
    //     string info = main_type + "_" + from_lib_pin + "_" + to_lib_pin;
    //     info2arc_idx[info] = arc_idx++;
    //     if (arc->get_timing_type() != ista::LibArc::TimingType::kCombFall
    //         || arc->get_timing_type() != ista::LibArc::TimingType::kCombRise) {
    //       continue;
    //     }

    //     arc->get_timing_sense();
    //   }
    // }

    //
    main_id_2_cell_id_start.append(lib_cell_idx);
    pybind11::list flat_luts_values[4];       // Forward delay flat LUT values
    pybind11::list flat_luts_trans_table[4];  // Forward delay flat LUT transition table
    pybind11::list flat_luts_cap_table[4];    // Forward delay flat LUT capacitance table
    pybind11::list flat_luts_dim[4];

    // const std::map<std::string_view, LibLutTableTemplate::Variable> LibLutTableTemplate::_str2var
    // = {{"total_output_net_capacitance", LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE},
    //    {"input_net_transition", LibLutTableTemplate::Variable::INPUT_NET_TRANSITION},
    //    {"related_pin_transition", LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION},
    //    {"constrained_pin_transition", LibLutTableTemplate::Variable::CONSTRAINED_PIN_TRANSITION},
    //    {"input_transition_time", LibLutTableTemplate::Variable::INPUT_TRANSITION_TIME},
    //    {"time", LibLutTableTemplate::Variable::TIME},
    //    {"input_voltage", LibLutTableTemplate::Variable::INPUT_VOLTAGE},
    //    {"output_voltage", LibLutTableTemplate::Variable::OUTPUT_VOLTAGE},
    //    {"input_noise_height", LibLutTableTemplate::Variable::INPUT_NOISE_HEIGHT},
    //    {"input_noise_width", LibLutTableTemplate::Variable::INPUT_NOISE_WIDTH},
    //    {"normalized_voltage", LibLutTableTemplate::Variable::NORMALIZED_VOLTAGE}};

    double default_cap = 0;
    double default_slew = 0;
    for (auto lib_cell : lib_cells) {
      // if ("sg13g2_o21ai_1" == lib_cell->get_cell_name()) {
      //   std::cout << "sg13g2_o21ai_1" << std::endl;
      // }
      cell_id_2_arc_id_start.append(arc_idx);
      cell_id_2_libpin_id_start.append(lib_pin_idx);
      for (auto& arc_set : lib_cell->get_cell_arcs()) {
        for (auto& arc : arc_set->get_arcs()) {
          auto from_lib_pin = arc->get_src_port();
          auto to_lib_pin = arc->get_snk_port();
          string cell_type_name = lib_cell->get_cell_name();
          string info = cell_type_name + "_" + from_lib_pin + "_" + to_lib_pin;
          // clang-format off
          if (   arc->get_timing_type() == ista::LibArc::TimingType::kCombFall 
              || arc->get_timing_type() == ista::LibArc::TimingType::kCombRise
              || arc->get_timing_type() == ista::LibArc::TimingType::kComb
              || arc->get_timing_type() == ista::LibArc::TimingType::kFallingEdge
              || arc->get_timing_type() == ista::LibArc::TimingType::kRisingEdge
              || arc->get_timing_type() == ista::LibArc::TimingType::kDefault
            ) {
            // clang-format on

            // Group the current arc by its 'info' key.
            auto [iter, inserted] = info2lib_arcs.try_emplace(info, std::vector<TempLibArc>{});
            iter->second.emplace_back(arc_idx++, arc.get());
            if (cell_type_name == "MUX2D0BWP40P140UHVT" && from_lib_pin == "S" && to_lib_pin == "Z") {
              std::cout << "find MUX2D0BWP40P140UHVT S->Z arc" << std::endl;
            }
            auto* lib_delay_model = dynamic_cast<LibDelayTableModel*>(arc->get_table_model());
            auto fall_delay_table = lib_delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kCellFall));
            auto rise_delay_table = lib_delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kCellRise));
            auto fall_trans_table = lib_delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kFallTransition));
            auto rise_trans_table = lib_delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kRiseTransition));

            // assert(fall_delay_table != nullptr);
            // assert(rise_delay_table != nullptr);

            init_lut_table_unified(f_delay_flat_luts_values, f_delay_flat_luts_trans_table, f_delay_flat_luts_cap_table,
                                   f_delay_flat_luts_dim, fall_delay_table, lib_cell, false);
            init_lut_table_unified(r_delay_flat_luts_values, r_delay_flat_luts_trans_table, r_delay_flat_luts_cap_table,
                                   r_delay_flat_luts_dim, rise_delay_table, lib_cell, false);
            init_lut_table_unified(f_trans_flat_luts_values, f_trans_flat_luts_trans_table, f_trans_flat_luts_cap_table,
                                   f_trans_flat_luts_dim, fall_trans_table, lib_cell, false);
            init_lut_table_unified(r_trans_flat_luts_values, r_trans_flat_luts_trans_table, r_trans_flat_luts_cap_table,
                                   r_trans_flat_luts_dim, rise_trans_table, lib_cell, false);
          } else if (arc->get_timing_type() == ista::LibArc::TimingType::kSetupRising
                     || arc->get_timing_type() == ista::LibArc::TimingType::kSetupFalling) {
            // 把rise setup 存到 r_delay

            // ----- 坐标轴 -----
            // 把clk trans 存到 trans
            // 把data trans 存到 cap

            // Group the current arc by its 'info' key.
            auto [iter, inserted] = info2lib_arcs.try_emplace(info, std::vector<TempLibArc>{});
            iter->second.emplace_back(arc_idx++, arc.get());

            auto* lib_check_model = dynamic_cast<LibCheckTableModel*>(arc->get_table_model());
            auto rise_setup_constraint_table = lib_check_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kRiseConstrain));
            auto fall_setup_constraint_table = lib_check_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kFallConstrain));
            auto rise_hold_constraint_table = nullptr;
            auto fall_hold_constraint_table = nullptr;
            // lib_delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kFallTransition));
            // lib_delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kRiseTransition));
            // assert(fall_delay_table != nullptr);
            // assert(rise_delay_table != nullptr);

            init_lut_table_unified(r_delay_flat_luts_values, r_delay_flat_luts_trans_table, r_delay_flat_luts_cap_table,
                                   r_delay_flat_luts_dim, rise_setup_constraint_table, lib_cell, true);
            init_lut_table_unified(f_delay_flat_luts_values, f_delay_flat_luts_trans_table, f_delay_flat_luts_cap_table,
                                   f_delay_flat_luts_dim, fall_setup_constraint_table, lib_cell, true);
            init_lut_table_unified(r_trans_flat_luts_values, r_trans_flat_luts_trans_table, r_trans_flat_luts_cap_table,
                                   r_trans_flat_luts_dim, rise_hold_constraint_table, lib_cell, true);
            init_lut_table_unified(f_trans_flat_luts_values, f_trans_flat_luts_trans_table, f_trans_flat_luts_cap_table,
                                   f_trans_flat_luts_dim, fall_hold_constraint_table, lib_cell, true);
          }
        }
      }
      int pin_offset = 0;
      for (auto& libpin : lib_cell->get_cell_ports()) {
        string libcell_name = lib_cell->get_cell_name();
        string libpin_name = libpin->get_port_name();
        string info = libcell_name + "_" + libpin_name;
        libpin_name2libpin_offset[info] = pin_offset++;
        lib_pin_idx++;
        if (libpin->isInput()) {
          auto cap = libpin->get_port_cap();
          auto rcap = libpin->get_port_cap(AnalysisMode::kMaxMin, TransType::kRise);
          auto fcap = libpin->get_port_cap(AnalysisMode::kMaxMin, TransType::kFall);
          flat_lib_pin_cap.append(cap);
          if (rcap.has_value() && fcap.has_value()) {
            flat_lib_pin_rcap.append(rcap.value());
            flat_lib_pin_fcap.append(fcap.value());
          } else {
            flat_lib_pin_rcap.append(cap);
            flat_lib_pin_fcap.append(cap);
          }
        } else {
          flat_lib_pin_cap.append(0);
          flat_lib_pin_rcap.append(0);
          flat_lib_pin_fcap.append(0);
        }
        double cap_limit = libpin->get_port_cap_limit(AnalysisMode::kMax).has_value()
                               ? libpin->get_port_cap_limit(AnalysisMode::kMax).value()
                               : default_cap;
        double slew_limit = libpin->get_port_slew_limit(AnalysisMode::kMax).has_value()
                                ? libpin->get_port_slew_limit(AnalysisMode::kMax).value()
                                : default_slew;
        flat_lib_pin_cap_limit.append(cap_limit);
        flat_lib_pin_slew_limit.append(slew_limit);
      }

      for (auto& libpin : lib_cell->get_cell_buses()) {
        // && lib_cell->get_cell_port_or_port_bus(const char *port_name)
        string libcell_name = lib_cell->get_cell_name();
        string libpin_name = libpin->get_port_name();
        string info = libcell_name + "_" + libpin_name;
        // if (libcell_name == "TS5N28HPCPLVTA256X64M2FW") {
        //   std::cout << info << std::endl;
        //   printf("HHH\n");
        // }
        libpin_name2libpin_offset[info] = pin_offset++;
        lib_pin_idx++;
        if (libpin->isInput()) {
          auto cap = libpin->get_port_cap();
          auto rcap = libpin->get_port_cap(AnalysisMode::kMaxMin, TransType::kRise);
          auto fcap = libpin->get_port_cap(AnalysisMode::kMaxMin, TransType::kFall);
          flat_lib_pin_cap.append(cap);
          if (rcap.has_value() && fcap.has_value()) {
            flat_lib_pin_rcap.append(rcap.value());
            flat_lib_pin_fcap.append(fcap.value());
          } else {
            flat_lib_pin_rcap.append(cap);
            flat_lib_pin_fcap.append(cap);
          }
        } else {
          flat_lib_pin_cap.append(0);
          flat_lib_pin_rcap.append(0);
          flat_lib_pin_fcap.append(0);
        }
        double cap_limit = libpin->get_port_cap_limit(AnalysisMode::kMax).has_value()
                               ? libpin->get_port_cap_limit(AnalysisMode::kMax).value()
                               : default_cap;
        double slew_limit = libpin->get_port_slew_limit(AnalysisMode::kMax).has_value()
                                ? libpin->get_port_slew_limit(AnalysisMode::kMax).value()
                                : default_slew;
        flat_lib_pin_cap_limit.append(cap_limit);
        flat_lib_pin_slew_limit.append(slew_limit);
      }
      // arc_idx += num_arcs;
    }

    lib_cell_idx += lib_cells.size();
  }
  assert(f_delay_flat_luts_values.size() == arc_idx);
  cell_id_2_arc_id_start.append(arc_idx);
  cell_id_2_libpin_id_start.append(lib_pin_idx);
  main_id_2_cell_id_start.append(lib_cell_idx);

  // DEBUG
  for (auto& [info, arcs] : info2lib_arcs) {
    // printf("info: %s, arc num: %d\n", info.c_str(), static_cast<int>(arcs.size()));
  }
  // DEBUG

  /*--------------------pin2libpin_offset-------------------------------*/
  for (IdbNet* net : db_deisgn->get_net_list()->get_net_list()) {
    if (isInvailidNet(net)) {
      continue;
    }
    for (IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
      auto lib_cell = pin->get_instance()->get_cell_master();
      string libcell_name = lib_cell->get_name();
      string libpin_name = pin->get_term_name();
      auto bracket_pos = libpin_name.find('[');
      if (bracket_pos != string::npos) {
        libpin_name = libpin_name.substr(0, bracket_pos);
      }
      string info = libcell_name + "_" + libpin_name;  // sg13g2_o21ai_1
      assert(libpin_name2libpin_offset.count(info));
      pin_2_libpin_offset.append(libpin_name2libpin_offset[info]);
      // Pin const& pin = db.pin(i);
    }

    for (IdbPin* pin : net->get_io_pins()->get_pin_list()) {
      pin_2_libpin_offset.append(-1);
      // Pin const& pin = db.pin(i);
    }
  }
  /*---------------------------------------------------*/
  //

  /*--------------------------------cell arcs init------------------------------------------*/
  /*--------------------------------cell arcs ------------------------------------------*/
  /*
  inst_flat_arcs: [inpin, outpin, lib_cell_idx, lib_cell_arc_idx, arc_type]
                  arc_type: 0 for neg, 1 for postive
  inst_flat_arcs_start
   inst_main_id;  // [num_main_type, ] cell_main_id + cell_width -> cell_id
   inst_size;  // [num_main_type, ] cell_main_id + cell_width -> cell_id
  */
  int inst_flat_arcs_idx = 0;
  // hadle cell arcs
  for (int i = 0; i < mNodeName2ID.size() - num_terminal_NIs - ext_blockage_num; ++i) {
    auto node_name = node_names[i].cast<std::string>();
    IdbInstance* node = inst_resort_list[mNodeName2ID[node_name]];
    inst_flat_arcs_start.append(inst_flat_arcs_idx);

    string cell_type = node->get_cell_master()->get_name();
    size_t main_type = cell_type2main_type[cell_type];
    std::vector<IdbPin*> input_pins;
    std::vector<IdbPin*> output_pins;
    inst_main_id.append(main_type2main_id[main_type]);
    inst_size.append(main_type_with_width[main_type][cell_type]);
    for (auto pin : node->get_pin_list()->get_pin_list()) {
      if (pin->get_term()->get_direction() == IdbConnectDirection::kInput) {
        input_pins.push_back(pin);
      } else if (pin->get_term()->get_direction() == IdbConnectDirection::kOutput) {
        output_pins.push_back(pin);
      } else if (pin->get_term()->get_direction() == IdbConnectDirection::kInOut) {
        // FIXME: handle inout pins
        // Inout pins are treated as input and output
        // input_pins.push_back(pin);
        // output_pins.push_back(pin);
      }
    }

    bool isFF = FFs_str.count(node_name);

    auto append_arc = [&](int from_pin_id, int to_pin_id, int lib_cell_id, const TempLibArc& lib_arc, pybind11::list& target_list) {
      pybind11::list arc;
      arc.append(from_pin_id);
      arc.append(to_pin_id);
      arc.append(lib_cell_id);
      arc.append(lib_arc._lib_arc_idx);
      arc.append(lib_arc.senseToInt());
      arc.append(lib_arc.typeToInt());
      target_list.append(arc);
    };
    // handle clk->D arcs (constraint arcs)
    if (isFF) {
      string clock_pin_name = "";
      for (int i = 0; i < input_pins.size(); i++) {
        auto input_pin = input_pins[i];
        string sta_full_pin_name = node_name + ":" + input_pin->get_pin_name();
        auto pin_vertex = _sta->findVertex(sta_full_pin_name.c_str());  // ensure the pin is in the sta graph
        if (pin_vertex->is_clock()) {
          clock_pin_name = input_pin->get_pin_name();
        }
      }
      for (IdbPin* input_pin : input_pins) {
        string to_lib_pin_name = input_pin->get_pin_name();
        string info = cell_type + "_" + clock_pin_name + "_" + to_lib_pin_name;
        if (info2lib_arcs.count(info)) {
          for (const auto& lib_arc : info2lib_arcs[info]) {
            int from_lib_pin_id = mClkPin2ID[node_name + clock_pin_name];
            if (mPin2ID.count(node_name + to_lib_pin_name) == 0) {
              std::cout << "Warning: pin is floating id for " << node_name + to_lib_pin_name << std::endl;
              continue;
            }
            assert(mPin2ID.count(node_name + to_lib_pin_name));
            int to_lib_pin_id = mPin2ID[node_name + to_lib_pin_name];
            int lib_cell_id = cell_type2cell_id[cell_type];
            append_arc(from_lib_pin_id, to_lib_pin_id, lib_cell_id, lib_arc, endpoints_constraint_arcs);
          }
        }
      }
    }

    for (int i = 0; i < input_pins.size(); i++) {
      for (int j = 0; j < output_pins.size(); j++) {
        auto input_pin = input_pins[i];
        auto output_pin = output_pins[j];
        string from_lib_pin = input_pin->get_pin_name();
        string to_lib_pin = output_pin->get_pin_name();
        string info_from_lib_pin = getLibPinName(from_lib_pin);
        string info_to_lib_pin = getLibPinName(to_lib_pin);
        string info = cell_type + "_" + info_from_lib_pin + "_" + info_to_lib_pin;
        if (info2lib_arcs.count(info)) {
          for (const auto& lib_arc : info2lib_arcs[info]) {
            int from_lib_pin_id = 0;
            if (isFF) {  // Handle clk->q arcs
              if (!mClkPin2ID.count(node_name + from_lib_pin)) {
                // FIXME:This isn't a clock pin, skip to avoid double counting
                continue;
              }
              from_lib_pin_id = mClkPin2ID[node_name + from_lib_pin];
            } else {  // Handle combinational arcs
              from_lib_pin_id = mPin2ID[node_name + from_lib_pin];
            }
            if (mPin2ID.count(node_name + to_lib_pin) == 0) {
              std::cout << "Warning: pin is floating id for " << node_name + to_lib_pin << std::endl;
              continue;
            }
            assert(mPin2ID.count(node_name + to_lib_pin));
            int to_lib_pin_id = mPin2ID[node_name + to_lib_pin];
            int lib_cell_id = cell_type2cell_id[cell_type];

            if (node_name == "U1831" && from_lib_pin == "S" && to_lib_pin == "Z") {
              printf("%s %s->%s arc, from_pin_id: %d, to_pin_id: %d, lib_cell_id: %d, lib_arc_idx: %d\n", node_name.c_str(),
                     from_lib_pin.c_str(), to_lib_pin.c_str(), from_lib_pin_id, to_lib_pin_id, lib_cell_id, lib_arc._lib_arc_idx);
              // std::cout << node_name<<  B1->ZN lib_arc_idx" << lib_arc._lib_arc_idx << std::endl;
            }

            append_arc(from_lib_pin_id, to_lib_pin_id, lib_cell_id, lib_arc, inst_flat_arcs);
            inst_flat_arcs_idx++;
          }
        }
      }
    }
  }
  // blockage
  for (int i = 0; i < ext_blockage_num; i++) {
    inst_flat_arcs_start.append(inst_flat_arcs_idx);
    inst_main_id.append(-1);
    inst_size.append(-1);
  }
  // IO PINS
  for (int i = mNodeName2ID.size() - num_terminal_NIs; i < mNodeName2ID.size(); ++i) {
    inst_flat_arcs_start.append(inst_flat_arcs_idx);
    inst_main_id.append(-1);
    inst_size.append(-1);
  }
  inst_flat_arcs_start.append(inst_flat_arcs_idx);
  /*---------------------------RC ---------------------------*/

  IdbLayerRouting* routing_layer = dynamic_cast<IdbLayerRouting*>(db->get_idb_layout()->get_layers()->get_routing_layers().at(1));
  double segment_width = (double) routing_layer->get_width() / db->get_idb_layout()->get_units()->get_micron_dbu();
  double lef_capacitance = routing_layer->get_capacitance();
  double lef_edge_capacitance = routing_layer->get_edge_capacitance();
  c_unit = (lef_capacitance * segment_width) + (lef_edge_capacitance * 2);  // pF per micron
  double lef_resistance = routing_layer->get_resistance();

  r_unit = lef_resistance / segment_width;  // ohm per micron
}

#endif
}  // namespace python_interface
   // namespace python_interface
