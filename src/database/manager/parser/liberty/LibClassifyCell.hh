/**
 * @file LibClassifyCell.hh
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief classify the liberty cell according the liberty
 * cell、port、arc、function.
 * @version 0.1
 * @date 2023-12-05
 *
 * @copyright Copyright (c) 2023
 *
 */
#pragma once

#include <unordered_map>

#include "absl/container/btree_map.h"
#include "absl/container/inlined_vector.h"
#include "Lib.hh"

namespace idb {

/**
 * @brief class for classify the lib cell.
 *
 */
class LibClassifyCell
{
 public:
  void classifyLibCell(std::vector<LibLibrary*>& the_libs);
  absl::InlinedVector<LibCell*, 64>* getClassOfCell(LibCell* cell)
  {
    if (_func_same_cells.contains(cell)) {
      return &_func_same_cells[cell];
    }
    return nullptr;
  }

 private:
  std::size_t hashCellPort(LibPort* port);
  std::size_t hashCellPortFuncExpr(::LibertyExpr* expr);

  std::size_t calculateCellHash(LibCell* the_cell);

  bool comparePort(LibPort* port1, LibPort* port2);
  bool comparePortFunc(::LibertyExpr* expr1, ::LibertyExpr* expr2);
  bool comparePorts(LibCell* cell1, LibCell* cell2);

  bool compareTimingArc(LibArcSet* set1, LibArcSet* set2);
  bool compareTimingArcSets(LibCell* cell1, LibCell* cell2);

  bool compareFunction(LibCell* the_cell1, LibCell* the_cell2);

  void classifyOneLibCell(LibLibrary* the_lib, std::unordered_map<std::size_t, absl::InlinedVector<LibCell*, 64>>& hash_to_cells);

  absl::btree_map<LibCell*, absl::InlinedVector<LibCell*, 64>> _func_same_cells;  //!< The one cell map to the func same cell with
                                                                                     //!< different size.
};
}  // namespace idb
