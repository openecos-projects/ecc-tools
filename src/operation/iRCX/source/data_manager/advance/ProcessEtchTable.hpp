// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) You may obtain a copy of Mulan PSL v2.
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "ProcessEffectType.hpp"
#include "ProcessTable2D.hpp"

namespace ircx {

class ProcessEtchTable
{
 public:
  ProcessEtchTable() = default;
  ~ProcessEtchTable() = default;
  // getter
  ProcessEffectType get_effect_type() const { return _effect_type; }
  ProcessTable2D& get_table() { return _table; }
  const ProcessTable2D& get_table() const { return _table; }
  // setter
  void set_effect_type(ProcessEffectType effect_type) { _effect_type = effect_type; }
  void set_table(const ProcessTable2D& table) { _table = table; }
  // function

 private:
  ProcessEffectType _effect_type = ProcessEffectType::kNone;
  ProcessTable2D _table;
};

}  // namespace ircx
