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
 * @file SolverNetBuilder.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <string>
#include <vector>

#include "SolverPipelineData.hh"
#include "SolverRuntimeContext.hh"

namespace icts {

class SolverNetBuilder
{
 public:
  explicit SolverNetBuilder(const SolverRuntimeContext& runtime) : _runtime(runtime) {}

  Net* connectNet(SolverPipelineState& state, Pin* driver, const std::vector<Pin*>& loads, const std::string& stage_tag,
                  bool allow_long_wire_buffering = true) const;
  Net* connectNamedNet(SolverPipelineState& state, const std::string& net_name, Pin* driver, const std::vector<Pin*>& loads,
                       const std::string& stage_tag, bool allow_long_wire_buffering = true) const;
  Net* createNetRecord(SolverPipelineState& state, Pin* driver, const std::vector<Pin*>& loads, const std::string& stage_tag,
                       bool allow_long_wire_buffering = true) const;
  Net* createNamedNetRecord(SolverPipelineState& state, const std::string& net_name, Pin* driver, const std::vector<Pin*>& loads,
                            const std::string& stage_tag, bool allow_long_wire_buffering = true) const;
  void registerBuffer(SolverPipelineState& state, Inst* buffer, int depth) const;
  void finalizeLeafDepth(SolverPipelineState& state, Pin* leaf_load, int depth) const;
  int childDepth(const SolverPipelineState& state, Pin* child) const;

 private:
  const SolverRuntimeContext& _runtime;
};

}  // namespace icts
