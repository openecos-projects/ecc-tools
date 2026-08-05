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
 * @file QOREvaluation.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-26
 * @brief CTS clock-tree evaluation stage.
 */

#pragma once

#include <functional>

#include "stage/StageSummary.hh"

namespace icts {

class Config;
class Design;
class Wrapper;

struct QorEvaluationModel
{
  std::reference_wrapper<const Config> config;
  std::reference_wrapper<Design> design;
  std::reference_wrapper<Wrapper> wrapper;
  EvaluationState state;
};

class QorEvaluation
{
 public:
  QorEvaluation() = delete;

  static auto evaluate(QorEvaluationModel& model) -> void;
  static auto outputSummary(const EvaluationState& state) -> QorSummary;
  static auto isEvaluationReady(const EvaluationState& state) -> bool;
  static auto reset(EvaluationState& state) -> void;
};

}  // namespace icts
