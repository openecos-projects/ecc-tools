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

class ProcessViaEtchTable
{
 public:
  ProcessViaEtchTable() = default;
  ~ProcessViaEtchTable() = default;
  // getter
  ProcessEffectType get_effect_type() const { return _effect_type; }
  ProcessTable2D& get_length_table() { return _length_table; }
  ProcessTable2D& get_width_table() { return _width_table; }
  const ProcessTable2D& get_length_table() const { return _length_table; }
  const ProcessTable2D& get_width_table() const { return _width_table; }
  // setter
  void set_effect_type(ProcessEffectType effect_type) { _effect_type = effect_type; }
  void set_length_table(const ProcessTable2D& length_table) { _length_table = length_table; }
  void set_width_table(const ProcessTable2D& width_table) { _width_table = width_table; }
  // function

 private:
  ProcessEffectType _effect_type = ProcessEffectType::kNone;
  ProcessTable2D _length_table;
  ProcessTable2D _width_table;
};

}  // namespace ircx
