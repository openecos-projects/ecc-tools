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
 * @file DataManager.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-30
 * @brief CTS process-wide state ownership and stage coordination contracts.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "adapter/fast_sta/FastSTA.hh"
#include "config/Config.hh"
#include "design/ClockLayout.hh"
#include "design/Design.hh"
#include "io/Wrapper.hh"
#include "stage/StageSummary.hh"

namespace icts {

#define CTSDM (icts::DataManager::getInst())

enum class DataManagerStatusCode
{
  kOk,
  kConfigError,
  kExternalDataError,
  kInvalidState,
  kCommitError
};

struct DataManagerStatus
{
  DataManagerStatusCode code = DataManagerStatusCode::kOk;
  std::string message;
  std::vector<std::string> diagnostics;
  std::vector<ClockGraphIssue> graph_issues;

  auto ok() const -> bool { return code == DataManagerStatusCode::kOk; }
};

struct DataManagerInput
{
  std::string config_file;
  std::string work_dir;
};

enum class CTSRunState
{
  kEmpty,
  kInputReady,
  kSynthesisCommitted,
  kOptimizationCommitted,
  kInstantiationCommitted,
  kEvaluationCommitted,
  kFailed
};

class DataManager final
{
  struct ConstructionKey
  {
  };

 public:
  explicit DataManager(ConstructionKey);

  static void initInst();
  static auto getInst() -> DataManager&;
  static void destroyInst();

  auto input(const DataManagerInput& input) -> DataManagerStatus;
  void reset();

  auto getState() const -> CTSRunState { return _state; }
  auto isInputReady() const -> bool { return _state >= CTSRunState::kInputReady && _state != CTSRunState::kFailed; }
  auto getLogPath() const -> const std::string& { return _config.get_log_file(); }

  auto getConfig() -> Config& { return _config; }
  auto getConfig() const -> const Config& { return _config; }
  auto getDesign() -> Design& { return *_design; }
  auto getDesign() const -> const Design& { return *_design; }
  auto cloneDesign() const -> std::unique_ptr<Design> { return _design->clone(); }
  auto getWrapper() -> Wrapper& { return _wrapper; }
  auto getWrapper() const -> const Wrapper& { return _wrapper; }
  auto getFastSTA() -> FastSTA& { return _fast_sta; }
  auto getFastSTA() const -> const FastSTA& { return _fast_sta; }
  auto getClockLayout() -> ClockLayout& { return _clock_layout; }
  auto getClockLayout() const -> const ClockLayout& { return _clock_layout; }

  auto commitSynthesis(std::unique_ptr<Design> design, ClockLayout clock_layout, const SynthesisTraceSummary& summary) -> DataManagerStatus;
  auto commitOptimization(std::unique_ptr<Design> design, ClockLayout clock_layout, const OptimizationSummary& summary) -> DataManagerStatus;
  auto commitInstantiation(const InstantiationSummary& summary) -> DataManagerStatus;
  auto commitEvaluation(EvaluationState state) -> DataManagerStatus;

  auto getSynthesisSummary() const -> const SynthesisTraceSummary& { return _synthesis_summary; }
  auto getOptimizationSummary() const -> const OptimizationSummary& { return _optimization_summary; }
  auto getInstantiationSummary() const -> const InstantiationSummary& { return _instantiation_summary; }
  auto getEvaluationState() const -> const EvaluationState& { return _evaluation_state; }
  auto hasCommittedEvaluation() const -> bool;
  auto getCommittedEvaluationState() const -> const EvaluationState*;

  static auto makeClockGraphFailureStatus(DataManagerStatusCode code, std::string message, const ClockDAG& clock_dag) -> DataManagerStatus;

  DataManager(const DataManager& other) = delete;
  DataManager(DataManager&& other) = delete;
  auto operator=(const DataManager& other) -> DataManager& = delete;
  auto operator=(DataManager&& other) -> DataManager& = delete;

 private:
  friend struct std::default_delete<DataManager>;

  ~DataManager() = default;

  auto readClockData() -> DataManagerStatus;
  auto replaceCommittedDesign(std::unique_ptr<Design> design) -> void;
  static auto okStatus(std::string message) -> DataManagerStatus;
  static auto failureStatus(DataManagerStatusCode code, std::string message) -> DataManagerStatus;
  static std::unique_ptr<DataManager> _instance;

  Config _config;
  std::unique_ptr<Design> _design;
  Wrapper _wrapper;
  FastSTA _fast_sta;
  ClockLayout _clock_layout;
  SynthesisTraceSummary _synthesis_summary;
  OptimizationSummary _optimization_summary;
  InstantiationSummary _instantiation_summary;
  EvaluationState _evaluation_state;
  CTSRunState _state = CTSRunState::kEmpty;
};

}  // namespace icts
