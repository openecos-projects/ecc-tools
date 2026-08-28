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
#include "checker/check_connection.h"

#include <cassert>

int main()
{
  idm::NetGraph graph;
  graph.set_id(0);

  graph.add_vertex(42);
  assert(graph.get_vertex_num() == 1);
  assert(graph.get_edge_num() == 0);
  assert(!graph.has_ring());

  graph.add_edge(42, 100);
  graph.add_edge(100, 7);
  assert(graph.get_vertex_num() == 3);
  assert(graph.get_edge_num() == 2);
  assert(!graph.has_ring());

  graph.add_edge(7, 42);
  assert(graph.get_edge_num() == 3);
  assert(graph.has_ring());

  return 0;
}
