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
 * @file CTSAPI.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-07
 * @brief iCTS API
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "CTSStatus.hh"

namespace ecc_feature {
struct CTSSummary;
}  // namespace ecc_feature

namespace icts {

#define CTS_API_INST (icts::CTSAPI::getInst())

struct CTSTimingClock
{
  std::string clock;
  std::size_t sink_count = 0U;
  double target_skew_ns = 0.0;
  double initial_skew_ns = 0.0;
  double optimized_skew_ns = 0.0;
  double min_insertion_latency_ns = 0.0;
  double max_insertion_latency_ns = 0.0;
  double mean_insertion_latency_ns = 0.0;
  bool target_met = false;
};

struct CTSClockGraphTimingState
{
  std::string analysis;
  std::string source_edge;
  std::string local_edge;
  std::uint64_t predecessor = UINT64_MAX;
  double arrival_ns = 0.0;
  double slew_ns = 0.0;
  bool arrival_valid = false;
  bool slew_valid = false;
};

struct CTSClockGraphNode
{
  std::uint64_t id = 0U;
  std::string name;
  std::string instance_name;
  std::string pin_name;
  std::string master;
  std::string kind;
  std::string cell_role;
  std::int32_t x_dbu = 0;
  std::int32_t y_dbu = 0;
  bool position_valid = false;
  bool sink = false;
  bool inactive = false;
  bool arrival_valid = false;
  bool slew_valid = false;
  double arrival_ns = 0.0;
  double slew_ns = 0.0;
  std::vector<CTSClockGraphTimingState> timing_states;
};

struct CTSClockGraphEdge
{
  std::uint64_t from = 0U;
  std::uint64_t to = 0U;
  bool cell_arc = false;
  bool negative_unate = false;
  std::string net_name;
  std::string cell_master;
};

struct CTSClockNetwork
{
  std::string name;
  std::string net_name;
  std::vector<std::uint64_t> roots;
  std::vector<CTSClockGraphNode> nodes;
  std::vector<CTSClockGraphEdge> edges;
  bool timing_available = false;
  std::string diagnostic;
};

struct CTSClockGraph
{
  std::string timing_source = "cts_fast_sta";
  std::string arrival_reference = "source_rise_late_physical_insertion";
  std::vector<CTSClockNetwork> clocks;
};

class CTSAPI
{
 public:
  static auto getInst() -> CTSAPI&
  {
    static CTSAPI inst;
    return inst;
  }

  // CTS CLI
  static auto runCTS(const std::string& config_file, const std::string& work_dir = "") -> CTSStatus;
  static auto runCTS() -> CTSStatus;
  static auto report(const std::string& save_dir) -> CTSStatus;

  // Lifecycle API
  static auto destroyCTS() -> CTSStatus;
  static auto init(const std::string& config_file, const std::string& work_dir = "") -> CTSStatus;
  static auto lastStatus() -> CTSStatus;

  // Feature API
  static auto outputSummary() -> ecc_feature::CTSSummary;
  static auto outputClockTiming() -> std::vector<CTSTimingClock>;
  static auto outputClockGraph() -> CTSClockGraph;
  CTSAPI(const CTSAPI& other) = delete;
  CTSAPI(CTSAPI&& other) = delete;
  auto operator=(const CTSAPI& other) -> CTSAPI& = delete;
  auto operator=(CTSAPI&& other) -> CTSAPI& = delete;

 private:
  CTSAPI();
  ~CTSAPI();

  auto setLastStatus(CTSStatus status) -> CTSStatus;

  CTSStatus _last_status;
  bool _initialized = false;
};

}  // namespace icts
