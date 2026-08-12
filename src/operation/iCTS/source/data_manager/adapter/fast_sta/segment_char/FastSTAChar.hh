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
 * @file FastSTAChar.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief Characterization sample context construction for CTS fast STA.
 */

#pragma once

#include <optional>
#include <string>

#include "FastSTA.hh"
#include "clock_state/FastSTAClockState.hh"

namespace icts {

class FastStaChar
{
 public:
  FastStaChar() = delete;

  struct BuildResult
  {
    std::optional<FastStaClockContext> context = std::nullopt;
    std::string failure_reason;

    auto ok() const -> bool { return context.has_value(); }
  };

  static auto buildContext(const FastStaCharTopologySpec& spec) -> BuildResult;
  static auto setLoad(FastStaClockContext& context, double effective_load_pf) -> bool;
  static auto runSample(FastStaClockContext& context, double input_slew_ns) -> FastStaCharSampleResult;
};

}  // namespace icts
