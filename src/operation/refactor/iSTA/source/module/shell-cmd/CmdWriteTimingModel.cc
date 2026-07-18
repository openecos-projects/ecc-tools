// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file CmdWriteTimingModel.cc
 * @brief
 */
#include "ShellCmd.hh"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

#include "api/TimingEngine.hh"

namespace ista {

namespace {

AnalysisMode parseTimingModelAnalysisMode(std::string analysis_mode) {
  std::transform(analysis_mode.begin(), analysis_mode.end(),
                 analysis_mode.begin(), [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });

  if (analysis_mode.empty() || analysis_mode == "max") {
    return AnalysisMode::kMax;
  }
  if (analysis_mode == "min") {
    return AnalysisMode::kMin;
  }

  LOG_ERROR << "write_timing_model only supports -analysis_mode max|min.";
  return AnalysisMode::kMaxMin;
}

}  // namespace

CmdWriteTimingModel::CmdWriteTimingModel(const char* cmd_name)
    : TclCmd(cmd_name) {
  auto* output_lib_path_option =
      new TclStringOption("-output_lib_path", 1, nullptr);
  addOption(output_lib_path_option);

  auto* analysis_mode_option =
      new TclStringOption("-analysis_mode", 0, "max");
  addOption(analysis_mode_option);
}

unsigned CmdWriteTimingModel::check() {
  TclOption* output_lib_path_option = getOptionOrArg("-output_lib_path");
  LOG_FATAL_IF(!output_lib_path_option);
  return 1;
}

unsigned CmdWriteTimingModel::exec() {
  if (!check()) {
    return 0;
  }

  namespace fs = std::filesystem;

  TclOption* output_lib_path_option = getOptionOrArg("-output_lib_path");
  auto* output_lib_path = output_lib_path_option->getStringVal();

  TclOption* analysis_mode_option = getOptionOrArg("-analysis_mode");
  std::string analysis_mode =
      analysis_mode_option ? analysis_mode_option->getStringVal() : "max";
  const auto mode = parseTimingModelAnalysisMode(analysis_mode);
  if (mode == AnalysisMode::kMaxMin) {
    return 0;
  }

  auto* timing_engine = TimingEngine::getOrCreateTimingEngine();
  const fs::path output_lib(output_lib_path);
  if (!output_lib.parent_path().empty()) {
    std::error_code ec;
    fs::create_directories(output_lib.parent_path(), ec);
    if (ec) {
      LOG_ERROR << "failed to create output directory "
                << output_lib.parent_path().string()
                << ", error=" << ec.message();
      return 0;
    }

    const auto workspace = output_lib.parent_path().string();
    timing_engine->set_design_work_space(workspace.c_str());
  }

  timing_engine->get_ista()->set_analysis_mode(mode);
  timing_engine->extractTimingModel(mode, output_lib_path);

  std::error_code ec;
  if (!fs::exists(output_lib, ec) || ec) {
    LOG_ERROR << "failed to generate timing model " << output_lib.string();
    return 0;
  }

  const auto output_size = fs::file_size(output_lib, ec);
  if (ec || output_size == 0) {
    LOG_ERROR << "generated timing model is empty: " << output_lib.string();
    return 0;
  }

  return 1;
}

}  // namespace ista
