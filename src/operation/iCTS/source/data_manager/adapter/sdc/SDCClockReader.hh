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
 * @file SDCClockReader.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-15
 * @brief Side-effect-free SDC clock subset reader for iCTS.
 */

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <tuple>
#include <vector>

#include "timing/TimingConstraints.hh"

namespace idb {
class IdbDesign;
class LibCell;
}  // namespace idb

namespace icts {

using SdcLibertyCellLookup = std::function<idb::LibCell*(const std::string&)>;

enum class ClockTracePropagationKind
{
  kBuffer,
  kInverter,
};

struct ClockTracePropagationStep
{
  std::string clock_name;
  std::string inst_name;
  std::string input_pin_name;
  std::string output_pin_name;
  std::string input_net_name;
  std::string output_net_name;
  ClockTracePropagationKind kind = ClockTracePropagationKind::kBuffer;
  std::string ownership_reason;
};

struct ClockTraceRecord
{
  std::string clock_name;
  std::string net_name;
  std::string status;
  std::string target_kind;
  std::size_t sequential_clock_sinks = 0U;
  std::size_t macro_clock_sinks = 0U;
  std::string trace_path;
  std::string reason;
  std::string clock_kind = "unknown";
  std::string master_clock_name = "n/a";
  std::string dominance = "undetermined";
  std::vector<ClockTracePropagationStep> propagation_steps;
};

struct ClockTracePreclusteredSinkAnchor
{
  std::string leaf_net_name;
  std::string driver_inst_name;
  std::string input_pin_name;
  std::string output_pin_name;
  std::string input_net_name;
};

struct ClockTraceClockTarget
{
  std::string clock_name;
  std::string clock_net_name;
  bool preclustered_sink_reuse = false;
  std::vector<ClockTracePreclusteredSinkAnchor> preclustered_sink_anchors;
  std::vector<std::string> terminal_net_names;
  std::vector<ClockTracePropagationStep> propagation_steps;
};

struct ClockTraceOutput
{
  std::vector<ClockTraceClockTarget> clock_targets;
};

struct ClockTraceSummary
{
  std::vector<ClockTraceRecord> records;
  std::vector<ClockTraceRecord> unowned_clock_like_records;
};

enum class ClockTraceBuildStatusCode
{
  kOk,
  kAmbiguousOwnership,
};

struct ClockTraceBuild
{
  ClockTraceOutput output;
  ClockTraceSummary summary;
  ClockTraceBuildStatusCode status = ClockTraceBuildStatusCode::kOk;
  std::string message;

  [[nodiscard]] auto ok() const -> bool { return status == ClockTraceBuildStatusCode::kOk; }
};

struct SdcClockTraceInput
{
  const SdcClockData* clock_data = nullptr;
  std::size_t max_fanout = 0U;
};

class SdcClockReader
{
 public:
  SdcClockReader();
  explicit SdcClockReader(std::string sdc_path, SdcUnits units = {});

  auto readClockData() const -> SdcClockData;
  auto readDeclarationsOnly() const -> std::vector<std::tuple<std::string, std::string, double, bool>>;
  // Derive only unresolved generated declarations after the caller identifies
  // their unique master on its graph. All input/output quantities are already ns.
  static auto resolveGeneratedClocks(SdcClockData& clock_data) -> void;
  static auto traceClockTargets(const SdcClockData& clock_data, idb::IdbDesign* idb_design, const SdcLibertyCellLookup& liberty_cell_lookup,
                                std::size_t max_fanout) -> ClockTraceBuild;

 private:
  std::string _sdc_path;
  SdcUnits _units;
};

}  // namespace icts
