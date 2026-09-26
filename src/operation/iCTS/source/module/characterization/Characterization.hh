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
 * @file Characterization.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-20
 * @brief Public characterization module contract for CTS segment and H-tree characterization.
 */

#pragma once

// IWYU pragma: begin_exports
#include "characterization/buffer_cell/CharacterizationBufferCell.hh"
#include "characterization/builder/CharBuilder.hh"
#include "characterization/pattern/PatternCombiner.hh"
#include "characterization/pruning/Frontier.hh"
#include "characterization/pruning/HTreeTraits.hh"
#include "characterization/pruning/HashJoinEngine.hh"
#include "characterization/pruning/SegmentTraits.hh"
#include "characterization/table/HTreeTopologyCharTable.hh"
#include "characterization/table/SegmentCharTable.hh"
// IWYU pragma: end_exports

#include <optional>

namespace icts {

struct CharacterizationWirelengthUnitLimits
{
  // Existing physical default used by CharBuilder when no caller provides a
  // unit: ten times the strongest usable buffer height.
  std::optional<double> physical_scale_unit_um = std::nullopt;
  // Largest unit whose wire capacitance plus the largest buffer input
  // capacitance still fits in the characterization cap lattice.
  std::optional<double> electrical_ceiling_um = std::nullopt;
};

auto ResolveCharacterizationWirelengthUnitLimits(const CharBuilder::Input& input, const CharBuilder::Config& config) -> CharacterizationWirelengthUnitLimits;

// Longest clock-route segment the characterization sweep can still represent. A driven
// segment pays its own wire capacitance plus the input capacitance of the buffer that
// drives it, and the cap lattice stops at max_cap; past this length every sweep point
// overflows the lattice and the sweep returns no segment characters at all. Callers
// that choose a length unit must stay at or below this bound. Returns no value when an
// input to the bound is unavailable, and zero when no positive length is drivable.
auto ResolveMaxCharacterizationSegmentLengthUm(const CharBuilder::Input& input, const CharBuilder::Config& config) -> std::optional<double>;

}  // namespace icts
