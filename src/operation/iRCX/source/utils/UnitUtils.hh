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

#include "Types.hh"

namespace ircx {

inline Micron dbuToMicronScale(Dbu micron_to_dbu)
{
  return Micron(1.0) / static_cast<Micron>(micron_to_dbu);
}

inline Micron dbuToMicron(Dbu value, Dbu micron_to_dbu)
{
  return static_cast<Micron>(value) * dbuToMicronScale(micron_to_dbu);
}

inline Dbu micronToDbu(Micron value, Dbu micron_to_dbu)
{
  return static_cast<Dbu>(value * static_cast<Micron>(micron_to_dbu));
}

}  // namespace ircx
