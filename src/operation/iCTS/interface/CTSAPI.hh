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

#include <functional>
#include <string>
#include <vector>

#include "CTSStatus.hh"

namespace ecc_feature {
struct CTSSummary;
}  // namespace ecc_feature

namespace eccdb {
class DatabaseContext;
}

namespace idb {
class LibCell;
}

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

class CTSAPI
{
 public:
  using LibertyCellLookup = std::function<idb::LibCell*(const std::string&)>;
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
  // These non-owning sources are intentionally configured at the public API
  // boundary so native callers never need an iDB design bridge.
  auto setDesignSource(eccdb::DatabaseContext* context) -> void;
  auto setDesignSource(eccdb::DatabaseContext& context) -> void { setDesignSource(&context); }
  auto setSdcPath(std::string sdc_path) -> void;
  auto setLibertyCellLookup(LibertyCellLookup liberty_cell_lookup) -> void;
  static auto destroyCTS() -> CTSStatus;
  // Destroy transient CTS state and release all caller-owned native inputs.
  // Use this when a toolchain run is abandoned or its DatabaseContext is about
  // to be released.
  static auto abortCTS() -> CTSStatus;
  static auto init(const std::string& config_file, const std::string& work_dir = "") -> CTSStatus;
  static auto lastStatus() -> CTSStatus;

  // Feature API
  static auto outputSummary() -> ecc_feature::CTSSummary;
  static auto outputClockTiming() -> std::vector<CTSTimingClock>;
  CTSAPI(const CTSAPI& other) = delete;
  CTSAPI(CTSAPI&& other) = delete;
  auto operator=(const CTSAPI& other) -> CTSAPI& = delete;
  auto operator=(CTSAPI&& other) -> CTSAPI& = delete;

 private:
  CTSAPI();
  ~CTSAPI();

  auto setLastStatus(CTSStatus status) -> CTSStatus;

  CTSStatus _last_status;
  eccdb::DatabaseContext* _database_context = nullptr;
  std::string _sdc_path;
  LibertyCellLookup _liberty_cell_lookup;
  bool _initialized = false;
};

}  // namespace icts
