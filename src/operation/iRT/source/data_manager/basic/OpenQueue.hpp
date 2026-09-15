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
#pragma once

#include "Utility.hpp"

namespace irt {

template <class T>
class OpenQueue
{
 public:
  OpenQueue() = default;
  ~OpenQueue() = default;

  T* top() { return _heap.empty() ? nullptr : _heap.front(); }
  T* pop()
  {
    if (_heap.empty()) {
      return nullptr;
    }
    T* node = _heap.front();
    node->set_open_queue_idx(-1);
    if (_heap.size() == 1) {
      _heap.pop_back();
      return node;
    }
    _heap.front() = _heap.back();
    _heap.front()->set_open_queue_idx(0);
    _heap.pop_back();
    siftDown(0);
    return node;
  }
  void push(T* node)
  {
    int32_t idx = node->get_open_queue_idx();
    if (idx == -1) {
      idx = static_cast<int32_t>(_heap.size());
      _heap.push_back(node);
      node->set_open_queue_idx(idx);
    }
    int32_t new_idx = siftUp(idx);
    if (new_idx == idx) {
      siftDown(idx);
    }
  }
  void clear()
  {
    for (T* node : _heap) {
      node->set_open_queue_idx(-1);
    }
    _heap.clear();
  }
  void release()
  {
    clear();
    std::vector<T*>().swap(_heap);
  }

 private:
  static bool higherPriority(T* a, T* b)
  {
    if (Utility::equalDoubleByError(a->getTotalCost(), b->getTotalCost(), RT_ERROR)) {
      if (Utility::equalDoubleByError(a->get_estimated_cost(), b->get_estimated_cost(), RT_ERROR)) {
        return a->get_neighbor_node_num() > b->get_neighbor_node_num();
      }
      return a->get_estimated_cost() < b->get_estimated_cost();
    }
    return a->getTotalCost() < b->getTotalCost();
  }
  void swapNode(int32_t a, int32_t b)
  {
    std::swap(_heap[a], _heap[b]);
    _heap[a]->set_open_queue_idx(a);
    _heap[b]->set_open_queue_idx(b);
  }
  int32_t siftUp(int32_t idx)
  {
    while (idx > 0) {
      int32_t parent = (idx - 1) / 2;
      if (!higherPriority(_heap[idx], _heap[parent])) {
        break;
      }
      swapNode(idx, parent);
      idx = parent;
    }
    return idx;
  }
  void siftDown(int32_t idx)
  {
    int32_t size = static_cast<int32_t>(_heap.size());
    while (idx * 2 + 1 < size) {
      int32_t child = idx * 2 + 1;
      if (child + 1 < size && higherPriority(_heap[child + 1], _heap[child])) {
        child++;
      }
      if (!higherPriority(_heap[child], _heap[idx])) {
        break;
      }
      swapNode(idx, child);
      idx = child;
    }
  }

  std::vector<T*> _heap;
};

}  // namespace irt
