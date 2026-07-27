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
 * @file PlotSpefTool.cc
 * @brief plot_spef implementation detail.
 */
#include "PlotSpefTool.hh"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

#include "ParallelUtils.hh"
#include "SpefParser.hh"
#include "builder/PlotSpefModelBuilder.hh"
#include "config/PlotSpefConfig.hh"
#include "gds/PlotSpefGdsWriter.hh"
#include "internal/InternalPlotSpefWriter.hh"
#include "log/Log.hh"
#include "lyp/PlotSpefLypWriter.hh"
#include "model/PlotSpefModel.hh"
#include "report/PlotSpefCgEdgeReport.hh"
#include "select/PlotSpefSelect.hh"

namespace ircx {
namespace {

auto edgeName(const plot_spef::EdgeRow& row) -> std::string
{
  return row.net_name + ":" + std::to_string(row.res_index);
}

auto cleanEdgeGdsDir(const plot_spef::Config& config) -> bool
{
  const auto edge_dir = std::filesystem::path(config.output_dir) / "edge_gds";
  std::error_code error_code;
  if (!std::filesystem::exists(edge_dir)) {
    std::filesystem::create_directories(edge_dir, error_code);
    if (error_code) {
      LOG_ERROR << "plot_spef failed: cannot create edge GDS directory "
                << edge_dir.string() << ": " << error_code.message();
      return false;
    }
    return true;
  }

  for (const auto& entry : std::filesystem::directory_iterator(edge_dir, error_code)) {
    if (error_code) {
      LOG_ERROR << "plot_spef failed: cannot scan edge GDS directory "
                << edge_dir.string() << ": " << error_code.message();
      return false;
    }
    if (!entry.is_regular_file(error_code)
        || entry.path().extension() != ".gds") {
      error_code.clear();
      continue;
    }
    std::filesystem::remove(entry.path(), error_code);
    if (error_code) {
      LOG_ERROR << "plot_spef failed: cannot remove stale edge GDS "
                << entry.path().string() << ": " << error_code.message();
      return false;
    }
  }
  return true;
}

auto edgeGdsBatchThreadCount(Size row_count,
                             const plot_spef::Config& config) -> int
{
  return parallel::threadCount(row_count, config.cores);
}

auto writeEdgeGdsBatch(const plot_spef::Model& model,
                       const spef::Exchange& exchange,
                       const plot_spef::Config& config) -> bool
{
  const auto rows = plot_spef::collectCoupledEdgeRows(model);
  if (rows.empty()) {
    LOG_ERROR << "plot_spef warning: no coupling-cap edge assignments found.";
    return true;
  }

  if (!cleanEdgeGdsDir(config)) {
    return false;
  }

  std::atomic_bool success{true};
  const int threads = edgeGdsBatchThreadCount(rows.size(), config);
  const auto row_count = static_cast<std::int64_t>(rows.size());
#pragma omp parallel num_threads(threads)
  {
    const plot_spef::GdsWriter writer;
#pragma omp for schedule(dynamic, 1)
    for (std::int64_t row_index = 0; row_index < row_count; ++row_index) {
      if (!success.load(std::memory_order_relaxed)) {
        continue;
      }

      auto edge_config = config;
      edge_config.edge_name = edgeName(rows[static_cast<Size>(row_index)]);
      edge_config.log_gds_file = false;
      edge_config.cores = 1;
      const auto visibility = plot_spef::makeEdgeVisibleObjects(model, exchange, edge_config);
      if (!writer.write(model, visibility, edge_config)) {
        success.store(false, std::memory_order_relaxed);
      }
    }
  }
  if (!success.load(std::memory_order_relaxed)) {
    return false;
  }

  LOG_INFO << "plot_spef wrote " << rows.size()
           << " edge GDS files using " << threads
           << " thread(s) to " << config.output_dir << "/edge_gds";
  return true;
}

}  // namespace

auto PlotSpefTool::run(plot_spef::Config config) -> bool
{
  const plot_spef::ConfigValidator validator;
  if (!validator.validate(config)) {
    return false;
  }

  spef::SpefReader reader;
  if (!reader.read(config.spef_file)) {
    LOG_ERROR << "plot_spef failed: read external SPEF failed: " << config.spef_file;
    return false;
  }
  reader.expandName();

  const spef::Exchange* exchange = reader.getSpefFile();
  if (exchange == nullptr) {
    LOG_ERROR << "plot_spef failed: SPEF reader returned empty data.";
    return false;
  }

  const plot_spef::ModelBuilder model_builder;
  auto model = model_builder.build(*exchange, config);
  const auto visibility = plot_spef::makeVisibleObjects(model, *exchange, config);
  const bool batch_edge_gds = config.output_edge_gds && !config.hasEdgeFilter();

  const plot_spef::GdsWriter writer;
  if (!writer.write(model, visibility, config)) {
    return false;
  }

  if (!config.hasEdgeFilter()) {
    const plot_spef::LypWriter lyp_writer;
    if (!lyp_writer.write(model, visibility, config)) {
      return false;
    }
  }

  if (batch_edge_gds && !writeEdgeGdsBatch(model, *exchange, config)) {
    return false;
  }

  LOG_INFO << "plot_spef wrote output to " << config.output_dir;
  return true;
}

auto PlotSpefTool::run(const RCXData& data,
                       plot_spef::Config config) -> bool
{
  return writeInternalPlotSpef(data, config);
}

}  // namespace ircx
