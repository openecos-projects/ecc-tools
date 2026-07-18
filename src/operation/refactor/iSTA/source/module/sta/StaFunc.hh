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
 * @file StaFunc.h
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief The sta functor class.
 * @version 0.1
 * @date 2021-02-17
 */
#pragma once

#include <algorithm>
#include <mutex>
#include <optional>
#include <stack>
#include <unordered_set>
#include <vector>

#include "Sta.hh"
#include "StaGraph.hh"
#include "Type.hh"
#include "log/Log.hh"
#include "sdc/SdcConstrain.hh"

namespace ista {

class StaVertx;
class StaGraph;
class StaClock;

/**
 * @brief The base functor of sta.
 *
 */
class StaFunc {
 public:
  StaFunc();
  virtual ~StaFunc();
  virtual unsigned operator()(StaGraph* the_graph);
  virtual unsigned operator()(StaVertex* the_vertex);
  virtual unsigned operator()(StaArc* the_arc);
  virtual unsigned operator()(StaClock* the_clock);

  virtual AnalysisMode get_analysis_mode();
  unsigned getNumThreads();
  Sta* getSta() { return _ista; }

  void set_is_trace_path() { _is_trace_path = true; }
  [[nodiscard]] bool isTracePath() const { return _is_trace_path; }
  void reset_is_trace_path() { _is_trace_path = false; }

  void set_is_incremental() { _is_incremental = true; }
  [[nodiscard]] bool isIncremental() const { return _is_incremental; }
  void set_data_epoch_filter(uint64_t data_epoch) {
    _data_epoch_filter = data_epoch;
  }
  void reset_data_epoch_filter() { _data_epoch_filter.reset(); }
  const std::optional<uint64_t>& get_data_epoch_filter() const {
    return _data_epoch_filter;
  }

  void PrintTraceRecord();

 protected:
  void addTracePathVertex(StaVertex* the_vertex) {
    _trace_path_record.push(the_vertex);
  }

 private:
  Sta* _ista;

  unsigned _is_trace_path : 1 = 0;
  unsigned _is_incremental : 1 = 0;
  unsigned _reserved : 30;
  std::optional<uint64_t> _data_epoch_filter;

  std::stack<StaVertex*> _trace_path_record;
};

/**
 * @brief The func for BFS processing.
 * 
 */
class StaBFSFunc {
  protected:
  StaBFSFunc() = default;
  StaBFSFunc(const StaBFSFunc& other) {
    std::lock_guard<std::mutex> lk(other._next_bfs_queue_mutex);
    _bfs_queue = other._bfs_queue;
    _next_bfs_queue = other._next_bfs_queue;
    _next_bfs_queue_set = other._next_bfs_queue_set;
  }
  StaBFSFunc& operator=(const StaBFSFunc& other) {
    if (this == &other) {
      return *this;
    }
    std::scoped_lock lk(_next_bfs_queue_mutex, other._next_bfs_queue_mutex);
    _bfs_queue = other._bfs_queue;
    _next_bfs_queue = other._next_bfs_queue;
    _next_bfs_queue_set = other._next_bfs_queue_set;
    return *this;
  }
  ~StaBFSFunc() = default;

  void addNextBFSQueue(StaVertex* the_vertex) {
      std::lock_guard<std::mutex> lk(_next_bfs_queue_mutex);

      if (_next_bfs_queue_set.insert(the_vertex).second) {
        _next_bfs_queue.push_back(the_vertex);
      }
  }

  void swapNextBFSQueue() {
    std::lock_guard<std::mutex> lk(_next_bfs_queue_mutex);
    std::swap(_bfs_queue, _next_bfs_queue);
    _next_bfs_queue_set.clear();
  }

  std::vector<StaVertex*> _bfs_queue; //!< The current bfs queue
  std::vector<StaVertex*> _next_bfs_queue; //!< For next bfs use.
  std::unordered_set<StaVertex*> _next_bfs_queue_set; //!< Fast duplicate check for the next bfs queue.
  mutable std::mutex _next_bfs_queue_mutex; //!< Protects _next_bfs_queue for this propagation object.
};

}  // namespace ista
