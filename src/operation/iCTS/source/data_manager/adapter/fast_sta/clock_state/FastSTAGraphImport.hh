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
 * @file FastSTAGraphImport.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Import physical timing and Liberty facts into the resident FastSTA graph.
 */

#pragma once

#include <string>

namespace icts {
struct FastStaContext;
struct FastStaEnvironment;
struct FastStaLibertyCell;
struct WrapperTimingGraph;

namespace fast_sta {
auto MergeLibertyCell(FastStaLibertyCell model, FastStaContext& context) -> void;
auto AppendTimingGraph(const FastStaEnvironment& environment, const WrapperTimingGraph& graph, FastStaContext& context, std::string& failure_reason) -> bool;
}  // namespace fast_sta
}  // namespace icts
