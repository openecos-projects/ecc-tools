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
 * @file StaClockSlewDelayPropagation.cc
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief The clock slew and propagation together using BFS method.
 * @version 0.1
 * @date 2024-12-26
 */

#include "StaClockSlewDelayPropagation.hh"

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "StaDelayPropagation.hh"
#include "StaSlewPropagation.hh"
#include "ThreadPool/ThreadPool.h"

namespace ista {

namespace {

bool shouldTraceClockSlewBfsName(const std::string& vertex_name) {
  const char* trace_env = std::getenv("IEDA_TRACE_CLOCK_SLEW_BFS");
  if (!trace_env || !*trace_env) {
    return false;
  }

  std::stringstream ss(trace_env);
  std::string item;
  while (std::getline(ss, item, ',')) {
    item.erase(std::remove_if(item.begin(), item.end(), ::isspace),
               item.end());
    if (!item.empty() && vertex_name.find(item) != std::string::npos) {
      return true;
    }
  }

  return false;
}

bool shouldTraceClockSlewBfsVertex(StaVertex* vertex) {
  return vertex && shouldTraceClockSlewBfsName(vertex->getName());
}

bool shouldTraceClockSlewBfsArc(StaArc* arc) {
  return arc &&
         (shouldTraceClockSlewBfsVertex(arc->get_src()) ||
          shouldTraceClockSlewBfsVertex(arc->get_snk()));
}

const char* arcKindName(StaArc* arc) {
  if (!arc) {
    return "null";
  }
  if (arc->isNetArc()) {
    return "net";
  }
  if (arc->isInstArc()) {
    return "inst";
  }
  return "unknown";
}

bool propagateIdealClockSlewData(StaArc* arc) {
  if (!arc) {
    return false;
  }

  auto* src_vertex = arc->get_src();
  auto* snk_vertex = arc->get_snk();
  if (!src_vertex || !snk_vertex) {
    return false;
  }

  bool has_propagated_slew = false;
  StaData* slew_data = nullptr;
  FOREACH_SLEW_DATA(src_vertex, slew_data) {
    auto* src_slew_data = dynamic_cast<StaSlewData*>(slew_data);
    if (!src_slew_data) {
      continue;
    }

    auto* new_slew_data = src_slew_data->copy();
    if (arc->isNegativeArc()) {
      new_slew_data->flipTransType();
    }

    new_slew_data->set_bwd(src_slew_data);
    src_slew_data->add_fwd(new_slew_data);
    snk_vertex->addData(new_slew_data);
    has_propagated_slew = true;
  }

  if (!has_propagated_slew) {
    snk_vertex->initSlewData();
  }

  return has_propagated_slew;
}

}  // namespace

/**
 * @brief propagate the arc to calc slew and delay of the snk vertex.
 *
 * @param the_arc
 * @return unsigned
 */
unsigned StaClockSlewDelayPropagation::operator()(StaArc* the_arc) {
  if (shouldTraceClockSlewBfsArc(the_arc)) {
    LOG_INFO << "[clock_slew_delay][arc-exec] kind=" << arcKindName(the_arc)
             << " src=" << the_arc->get_src()->getName()
             << " snk=" << the_arc->get_snk()->getName()
             << " disable=" << the_arc->is_disable_arc()
             << " loop_disable=" << the_arc->is_loop_disable();
  }

  std::lock_guard<std::mutex> lk(the_arc->get_snk()->get_fwd_mutex());
  StaSlewPropagation slew_propagation;
  StaDelayPropagation delay_propagation;

  slew_propagation(the_arc);
  delay_propagation(the_arc);

  if (shouldTraceClockSlewBfsArc(the_arc)) {
    LOG_INFO << "[clock_slew_delay][arc-done] kind=" << arcKindName(the_arc)
             << " src=" << the_arc->get_src()->getName()
             << " snk=" << the_arc->get_snk()->getName();
  }
  return 1;
}

/**
 * @brief propagate the vertex, and get the next bfs vertexes.
 *
 * @param the_vertex
 * @return unsigned
 */
unsigned StaClockSlewDelayPropagation::operator()(StaVertex* the_vertex) {
  if (shouldTraceClockSlewBfsVertex(the_vertex)) {
    LOG_INFO << "[clock_slew_delay][vertex-visit] vertex="
             << the_vertex->getName() << " is_clock=" << the_vertex->is_clock()
             << " is_sdc_clock_pin=" << the_vertex->is_sdc_clock_pin()
             << " is_start=" << the_vertex->is_start()
             << " src_arc_count=" << the_vertex->get_src_arcs().size();
  }

  if (the_vertex->is_const()) {
    return 1;
  }

  auto* vertex_own_cell = the_vertex->getOwnCell();

  // clock propagation end at the clock vertex.
  if (the_vertex->is_clock() && vertex_own_cell && !vertex_own_cell->isICG()) {
    if (shouldTraceClockSlewBfsVertex(the_vertex)) {
      LOG_INFO << "[clock_slew_delay][vertex-stop-clock-end] vertex="
               << the_vertex->getName();
    }
    the_vertex->set_is_slew_prop();
    the_vertex->set_is_delay_prop();
    return 1;
  }

  unsigned is_ok = 1;
  FOREACH_SRC_ARC(the_vertex, src_arc) {
    if (shouldTraceClockSlewBfsArc(src_arc)) {
      LOG_INFO << "[clock_slew_delay][vertex-arc] src_vertex="
               << the_vertex->getName() << " kind=" << arcKindName(src_arc)
               << " src=" << src_arc->get_src()->getName()
               << " snk=" << src_arc->get_snk()->getName()
               << " disable=" << src_arc->is_disable_arc()
               << " loop_disable=" << src_arc->is_loop_disable()
               << " is_delay_arc=" << src_arc->isDelayArc();
    }

    if (!src_arc->isDelayArc()) {
      continue;
    }

    if (src_arc->is_loop_disable()) {
      continue;
    }

    if (src_arc->isInstArc() &&
        !src_arc->get_snk()->get_design_obj()->get_net()) {
      // skip the instance output not connected to the net.
      continue;
    }

    // get the next bfs vertex and add it to the queue.
    auto* snk_vertex = src_arc->get_snk();
    if (!isIdealClock()) {
      is_ok = src_arc->exec(*this);
      if (!is_ok) {
        LOG_FATAL << "slew propgation error";
        break;
      }
    } else {
      const bool copied_source_slew = propagateIdealClockSlewData(src_arc);
      if (shouldTraceClockSlewBfsArc(src_arc)) {
        LOG_INFO << "[clock_slew_delay][ideal-clock-slew] src="
                 << src_arc->get_src()->getName()
                 << " snk=" << src_arc->get_snk()->getName()
                 << " copied=" << copied_source_slew;
      }
    }

    addNextBFSQueue(snk_vertex);
    if (shouldTraceClockSlewBfsArc(src_arc)) {
      LOG_INFO << "[clock_slew_delay][enqueue] vertex="
               << snk_vertex->getName();
    }
  }

  the_vertex->set_is_slew_prop();
  the_vertex->set_is_delay_prop();

  return 1;
}

/**
 * @brief propagate from the clock source vertex.
 *
 * @return unsigned
 */
unsigned StaClockSlewDelayPropagation::operator()(StaGraph*) {
  ieda::Stats stats;
  LOG_INFO << "clock slew delay propagation start";
  unsigned is_ok = 1;

  Sta* ista = getSta();
  auto& clocks = ista->get_clocks();

  for (auto& clock : clocks) {
    _propagate_clock = clock.get();

    auto& vertexes = clock->get_clock_vertexes();
    for (auto* vertex : vertexes) {
      if (shouldTraceClockSlewBfsVertex(vertex)) {
        LOG_INFO << "[clock_slew_delay][seed] vertex=" << vertex->getName()
                 << " clock=" << clock->get_clock_name()
                 << " src_arc_count=" << vertex->get_src_arcs().size();
      }
      vertex->initSlewData();
      _bfs_queue.emplace_back(vertex);
    }
  }

  // lambda for propagate the current queue.
  auto propagate_current_queue = [this](auto& current_queue) {
    LOG_INFO << "propagating current clock queue vertexes number is "
             << current_queue.size();
#if 0
    // create thread pool
    unsigned num_threads = getNumThreads();
    ThreadPool pool(num_threads);

    for (auto* the_vertex : current_queue) {
      // bfs start from the root vertex, traverse to the clock pin vertex.
      if (the_vertex->get_src_arcs().empty()) {
        continue;
      }

      pool.enqueue([this](StaVertex* the_vertex) { return the_vertex->exec(*this); },
                   the_vertex);
    }
#else
    for (auto* the_vertex : current_queue) {
      the_vertex->exec(*this);
    }
#endif
  };

  // do the bfs traverse for calc the clock slew/delay.
  do {
    propagate_current_queue(_bfs_queue);
    _bfs_queue.clear();

    // swap to the next bfs queue.
    swapNextBFSQueue();

  } while (!_bfs_queue.empty());

  LOG_INFO << "clock slew delay propagation end";

  double memory_delta = stats.memoryDelta();
  LOG_INFO << "clock slew delay propagation memory usage " << memory_delta
           << "MB";
  double time_delta = stats.elapsedRunTime();
  LOG_INFO << "clock slew delay propagation time elapsed " << time_delta << "s";

  return is_ok;
}

}  // namespace ista
