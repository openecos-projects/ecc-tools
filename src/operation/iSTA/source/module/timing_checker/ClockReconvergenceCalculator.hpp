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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "graph/TimingStateQuery.hpp"
namespace ista {
// Compares already selected launch/capture clocks. Does not choose checks or path winners.
class ClockReconvergenceCalculator
{
 public:
  // Snapshot mode is valid only while the graph and clock waveforms are fixed.
  // The default remains live for callers that update clocks between queries.
  using CacheMode = ClockPinValuesQuery::CacheMode;
  explicit ClockReconvergenceCalculator(Database& database, CacheMode cache = CacheMode::kNone)
      : _database(database), _cache_mode(cache), _clock_pin_values(database, cache) {}
  double correction(const std::string& launch_pin, TransType launch_trans_type, std::string_view launch_clock, const std::string& capture_pin,
                    TransType capture_trans_type, std::string_view capture_clock, AnalysisType analysis_type, std::string& common_pin_name,
                    std::optional<TransType> launch_source = std::nullopt, std::optional<TransType> capture_source = std::nullopt);

 private:
  struct ClockPathPoint
  {
    int32_t vertex = -1;
    TransType transition;
    std::optional<TransType> source;
  };
  using ClockPathKey = std::tuple<std::string, std::string, AnalysisType, TransType, std::optional<TransType>>;
  struct IndexedClockPath
  {
    std::vector<ClockPathPoint> points;
    std::size_t prefix = 0;
  };
  struct PrefixNode
  {
    std::size_t depth = 0;
    std::vector<std::size_t> ancestors;
  };
  Database& _database;
  CacheMode _cache_mode;
  ClockPinValuesQuery _clock_pin_values;
  std::map<ClockPathKey, IndexedClockPath, std::less<>> _clock_paths;
  // Prefixes contain vertex IDs only, matching the original common-path test.
  // Each cached path retains its own transitions and source-edge metadata.
  std::map<std::pair<std::size_t, int32_t>, std::size_t> _prefix_ids;
  std::vector<PrefixNode> _prefixes;
  const IndexedClockPath& indexedClockPath(const std::string& pin, std::string_view clock, AnalysisType analysis, TransType transition,
                                           std::optional<TransType> source);
  std::size_t commonPrefixDepth(std::size_t launch, std::size_t capture) const;
  std::size_t ancestor(std::size_t prefix, std::size_t level) const;
  double getClockCommonPathDelayDelta(const ClockPathPoint& common_pin, std::string_view clock_name);
  std::vector<ClockPathPoint> getClockPathPinList(const std::string& clock_pin_name, std::string_view clock_name, AnalysisType analysis_type,
                                                  TransType trans_type, std::optional<TransType> source);
  AnalysisType getCaptureAnalysisType(AnalysisType analysis_type);
};
}  // namespace ista
