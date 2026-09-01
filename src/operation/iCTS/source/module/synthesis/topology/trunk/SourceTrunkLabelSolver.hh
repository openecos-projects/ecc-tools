// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the License at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file SourceTrunkLabelSolver.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-08-26
 * @brief Canonical label-setting solver for one source-trunk segment.
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "BufferingPattern.hh"
#include "SegmentChar.hh"

namespace icts::source_trunk {

enum class LabelSolverStatus
{
  kFinished,
  kNoLegalPath,
  kGeneratedLabelBudgetExceeded,
  kRetainedLabelBudgetExceeded,
  kMissingPattern,
};

struct LabelSolverInput
{
  const std::vector<SegmentChar>* primitive_chars = nullptr;
  const std::vector<BufferingPattern>* primitive_patterns = nullptr;
  unsigned target_length_idx = 0U;
  unsigned required_load_cap_idx = 0U;
  unsigned source_drive_cap_idx = 0U;
  std::optional<unsigned> min_input_slew_idx = std::nullopt;
};

struct LabelSolverConfig
{
  std::size_t max_generated_labels = 50000000U;
  std::size_t max_retained_labels = 30000000U;
};

struct LabelSolverSummary
{
  std::size_t visited_length_count = 0U;
  std::size_t visited_state_count = 0U;
  std::size_t generated_label_count = 0U;
  std::size_t retained_label_count = 0U;
  std::size_t final_candidate_count = 0U;
  std::size_t final_pareto_count = 0U;
  std::size_t selected_primitive_count = 0U;
};

struct LabelSolverBuild
{
  LabelSolverStatus status = LabelSolverStatus::kNoLegalPath;
  std::string failure_reason;
  std::optional<SegmentChar> best_char = std::nullopt;
  std::optional<BufferingPattern> best_pattern = std::nullopt;
  LabelSolverSummary summary;

  auto ok() const -> bool { return status == LabelSolverStatus::kFinished; }
};

auto SolveLabels(const LabelSolverInput& input, const LabelSolverConfig& config = {}) -> LabelSolverBuild;

}  // namespace icts::source_trunk
