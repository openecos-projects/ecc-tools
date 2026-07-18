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
/**
 * @file StaCheck.cc
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief The timing check implemention.
 * @version 0.1
 * @date 2021-03-01
 */

#include "StaCheck.hh"

#include <cstddef>
#include <ostream>
#include <queue>

#include "log/Log.hh"
#include "sta/StaArc.hh"
#include "sta/StaVertex.hh"

namespace ista {
namespace {

auto disableCombLoopArc(StaArc* arc, bool is_fwd) -> bool
{
  if (arc == nullptr || !arc->isDelayArc() || arc->is_loop_disable()) {
    return false;
  }

  arc->set_is_loop_disable(true);
  (void) is_fwd;
  return true;
}

auto traverseDataPath(StaVertex* the_vertex, bool is_fwd, std::size_t& disabled_loop_count) -> unsigned
{
  if (the_vertex == nullptr) {
    return 0;
  }
  if (is_fwd && the_vertex->is_end()) {
    return 0;
  }
  if (!is_fwd && the_vertex->is_start()) {
    return 0;
  }
  if (the_vertex->isBlack()) {
    return 0;
  }
  if (the_vertex->isGray()) {
    return 1;
  }

  the_vertex->setGray();
  auto& next_arcs = is_fwd ? the_vertex->get_src_arcs() : the_vertex->get_snk_arcs();
  for (auto* arc : next_arcs) {
    if (arc == nullptr || !arc->isDelayArc() || arc->is_loop_disable()) {
      continue;
    }

    auto* next_vertex = is_fwd ? arc->get_snk() : arc->get_src();
    if (next_vertex == nullptr || next_vertex->isBlack()) {
      continue;
    }
    if (next_vertex->isGray()) {
      if (disableCombLoopArc(arc, is_fwd)) {
        ++disabled_loop_count;
      }
      continue;
    }
    if (traverseDataPath(next_vertex, is_fwd, disabled_loop_count)) {
      if (disableCombLoopArc(arc, is_fwd)) {
        ++disabled_loop_count;
      }
      continue;
    }
  }

  the_vertex->setBlack();
  return 0;
}

auto breakCombLoopsFromStarts(StaGraph* the_graph) -> std::size_t
{
  std::size_t disabled_loop_count = 0U;
  the_graph->resetVertexColor();

  StaVertex* start_vertex;
  FOREACH_START_VERTEX(the_graph, start_vertex) {
    if (start_vertex == nullptr) {
      continue;
    }
    for (auto* arc : start_vertex->get_src_arcs()) {
      if (arc == nullptr || !arc->isDelayArc() || arc->is_loop_disable()) {
        continue;
      }
      traverseDataPath(arc->get_snk(), true, disabled_loop_count);
    }
  }
  return disabled_loop_count;
}

auto breakCombLoopsFromEnds(StaGraph* the_graph) -> std::size_t
{
  std::size_t disabled_loop_count = 0U;
  the_graph->resetVertexColor();

  StaVertex* end_vertex;
  FOREACH_END_VERTEX(the_graph, end_vertex) {
    if (end_vertex == nullptr) {
      continue;
    }
    for (auto* arc : end_vertex->get_snk_arcs()) {
      if (arc == nullptr || !arc->isDelayArc() || arc->is_loop_disable()) {
        continue;
      }
      traverseDataPath(arc->get_src(), false, disabled_loop_count);
    }
  }
  return disabled_loop_count;
}

}  // namespace

/**
 * @brief print loop record.
 *
 */
void StaCombLoopCheck::printAndBreakLoop(bool is_fwd) {
  const char* direction = is_fwd ? " <- " : " -> ";
  std::string loop_name;
  auto* loop_point = _loop_record.front();
  StaVertex* last_vertex = nullptr;
  while (!_loop_record.empty()) {
    auto* the_vertex = _loop_record.front();
    loop_name += the_vertex->getName() + direction;
    _loop_record.pop();

    // found loop point and break loop.
    if (_loop_record.front() == loop_point) {
      loop_name += _loop_record.front()->getName();

      StaArc* loop_arc = nullptr;
      if (is_fwd) {
        loop_arc = last_vertex->getSrcArc(the_vertex).front();
      } else {
        loop_arc = last_vertex->getSnkArc(the_vertex).front();
      }
      loop_arc->set_is_loop_disable(true);

      break;
    }

    last_vertex = the_vertex;
  }

  // clear queue.
  std::queue<StaVertex*> empty_vertex;
  _loop_record.swap(empty_vertex);

  LOG_INFO << loop_name;
}

/**
 * @brief The combination loop check implemention.
 *
 * @param the_graph
 * @return unsigned return 1 if found loop, else return 0.
 */
unsigned StaCombLoopCheck::operator()(StaGraph* the_graph) {
  if (the_graph == nullptr) {
    return 0;
  }

  LOG_INFO << "found loop fwd start";
  const auto fwd_disabled_loop_count = breakCombLoopsFromStarts(the_graph);
  LOG_INFO << "found loop fwd end";

  LOG_INFO << "found loop bwd start";
  const auto bwd_disabled_loop_count = breakCombLoopsFromEnds(the_graph);
  the_graph->resetVertexColor();
  LOG_INFO << "found loop bwd end";

  LOG_INFO << "comb loop check disabled " << (fwd_disabled_loop_count + bwd_disabled_loop_count) << " delay arc(s), fwd="
           << fwd_disabled_loop_count << ", bwd=" << bwd_disabled_loop_count;
  return 1;
}

}  // namespace ista
