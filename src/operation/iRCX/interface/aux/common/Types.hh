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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "RCXHeader.hpp"

namespace ircx {

using F64 = double;
using I64 = int64_t;
using Size = size_t;

using Dbu = int32_t;
using Micron = double;

using GtlPointI = GTLPointInt;
using GtlRectI = GTLRectInt;

inline constexpr Size kMaxSize = SIZE_MAX;

namespace unit {

inline Micron toMicron(Dbu value, Dbu dbu_per_micron)
{
  return value / 1.0 / dbu_per_micron;
}

inline Dbu toDbu(Micron value, Dbu dbu_per_micron)
{
  return static_cast<Dbu>(std::llround(value * dbu_per_micron));
}

}  // namespace unit

}  // namespace ircx
