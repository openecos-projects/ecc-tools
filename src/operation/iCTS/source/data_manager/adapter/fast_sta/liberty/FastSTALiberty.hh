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
 * @file FastSTALiberty.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief Fast STA Liberty timing and power records extracted for CTS cells.
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "FastSTACondition.hh"
#include "FastSTALibertyModel.hh"

namespace icts {

class Wrapper;

enum class FastStaCheckStatus
{
  kMeasured,
  kInactive,
  kUnavailable,
  kInvalidCondition
};

struct FastStaCheckValue
{
  FastStaCheckStatus status = FastStaCheckStatus::kUnavailable;
  double requirement_ns = 0.0;
};

class FastStaLiberty
{
 public:
  FastStaLiberty() = delete;

  static auto extractBufferCell(Wrapper& wrapper, const std::string& cell_master) -> std::optional<FastStaLibertyCell>;
  static auto extractCellArc(Wrapper& wrapper, const std::string& cell_master, const std::string& input_port, const std::string& output_port)
      -> std::optional<FastStaLibertyCell>;
  static auto extractLaunchArc(Wrapper& wrapper, const std::string& cell_master, const std::string& clock_port, const std::string& output_port,
                               FastStaTransition clock_transition) -> std::optional<FastStaLibertyCell>;
  static auto queryTimingCheck(Wrapper& wrapper, const std::string& cell_master, const std::string& clock_port, const std::string& data_port,
                               FastStaTimingCheckKind kind, FastStaTransition clock_transition, FastStaTransition data_transition, double clock_slew_ns,
                               double data_slew_ns, std::size_t& conditional_fallback_count, std::size_t& conditional_extrema_count,
                               const FastStaCondition::PinValueLookup& case_lookup) -> FastStaCheckValue;
};

}  // namespace icts
