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
 * @file CTSAPI.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-07
 * @brief iCTS API implementation.
 */
#include "CTSAPI.hh"

#include <memory>
#include <string>

#include "evaluation/qor/QorEvaluation.hh"
#include "feature_icts.h"
#include "flow/Flow.hh"
#include "flow/setup/Setup.hh"

namespace icts {
namespace {

auto buildFeatureSummary(const QorSummary& flow_summary) -> ieda_feature::CTSSummary
{
  ieda_feature::CTSSummary summary{};
  summary.buffer_num = flow_summary.buffer_num;
  summary.buffer_area = flow_summary.buffer_area;
  summary.clock_path_min_buffer = flow_summary.clock_path_min_buffer;
  summary.clock_path_max_buffer = flow_summary.clock_path_max_buffer;
  summary.max_level_of_clock_tree = flow_summary.feature_max_clock_network_level;
  summary.max_clock_wirelength = flow_summary.max_clock_wirelength;
  summary.total_clock_wirelength = flow_summary.total_clock_wirelength;
  return summary;
}

}  // namespace

CTSAPI::CTSAPI() : _runtime(std::make_unique<CTSRuntime>()), _flow(std::make_unique<Flow>(*_runtime))
{
}

CTSAPI::~CTSAPI() = default;

auto CTSAPI::runtime() -> CTSRuntime&
{
  return *_runtime;
}

auto CTSAPI::flow() -> Flow&
{
  return *_flow;
}

auto CTSAPI::runCTS() -> void
{
  getInst().flow().runCTS();
}

auto CTSAPI::report(const std::string& save_dir) -> void
{
  getInst().flow().emitReports(save_dir);
}

auto CTSAPI::resetAPI() -> void
{
  auto& api = getInst();
  api.runtime().reset();
  api.flow().reset();
}

auto CTSAPI::init(const std::string& config_file, const std::string& work_dir) -> void
{
  resetAPI();
  auto& api = getInst();
  auto& runtime = api.runtime();
  const auto setup_result = Setup::initializeRuntime(SetupInput{
      .config = &runtime.config,
      .design = &runtime.design,
      .wrapper = &runtime.wrapper,
      .reporter = &runtime.reporter,
      .config_file = config_file,
      .work_dir = work_dir,
  });
  auto& flow = api.flow();
  flow.setSetupReady(setup_result.success);
  if (!setup_result.success) {
    return;
  }
  flow.outputRuntimeSetup();
}

auto CTSAPI::outputSummary() -> ieda_feature::CTSSummary
{
  return buildFeatureSummary(getInst().flow().outputSummary());
}

}  // namespace icts
