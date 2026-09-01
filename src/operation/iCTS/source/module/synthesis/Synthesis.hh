// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file Synthesis.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-30
 * @brief CTS synthesis entry facade.
 */

#pragma once

#include <memory>

#include "stage/StageSummary.hh"

namespace icts {

class ClockLayout;
class DataManager;
class Design;
struct DataManagerStatus;

auto CommitSynthesisCandidate(DataManager& data_manager, std::unique_ptr<Design> design, ClockLayout clock_layout, SynthesisTraceSummary summary,
                              DataManagerStatus& commit_status) -> SynthesisTraceSummary;

class Synthesis
{
 public:
  Synthesis() = delete;

  static auto run() -> SynthesisTraceSummary;
};

}  // namespace icts
