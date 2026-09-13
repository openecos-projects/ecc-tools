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
 * @file TimingConstraints.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-05
 * @brief Owned SDC clock, event, and timing-exception facts in nanoseconds.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace icts {

struct SdcUnits
{
  double time_unit_ns = 1.0;
  double capacitance_unit_pf = 1.0;

  auto operator==(const SdcUnits&) const -> bool = default;
};

enum class SdcObjectKind
{
  kPort,
  kPin,
  kNet,
  kClock,
  kUnknown,
};

struct SdcObjectRef
{
  SdcObjectKind kind = SdcObjectKind::kUnknown;
  std::string pattern = "";
  bool from_collection_cmd = false;

  auto operator==(const SdcObjectRef&) const -> bool = default;
};

struct SdcClockDecl
{
  enum class Kind
  {
    kPrimary,
    kGenerated,
  };

  Kind kind = Kind::kPrimary;
  std::string clock_name = "";
  std::vector<SdcObjectRef> targets;
  std::vector<SdcObjectRef> generated_sources;
  std::string master_clock_name = "";
  double period_ns = 0.0;
  bool period_resolved = false;
  int divide_by = 1;
  int multiply_by = 1;
  bool invert = false;
  bool is_virtual = false;
  // Alternating rise/fall edges. Generated waveforms remain unresolved until
  // their source clock has been identified by the timing graph when necessary.
  std::vector<double> waveform_ns;
  std::vector<int> generated_edges;
  std::vector<double> generated_edge_shifts_ns;
  bool waveform_explicit = false;
  bool waveform_resolved = false;
  bool divide_by_explicit = false;
  bool multiply_by_explicit = false;
  bool combinational = false;
  bool add = false;
  std::optional<double> duty_cycle_percent = std::nullopt;

  auto operator==(const SdcClockDecl&) const -> bool = default;
};

struct SdcCaseAnalysis
{
  int value = 0;
  std::vector<SdcObjectRef> objects;

  auto operator==(const SdcCaseAnalysis&) const -> bool = default;
};

enum class SdcTransition
{
  kBoth,
  kRise,
  kFall,
};

struct SdcPathSelector
{
  std::vector<SdcObjectRef> objects;
  SdcTransition transition = SdcTransition::kBoth;

  auto operator==(const SdcPathSelector&) const -> bool = default;
};

struct SdcPathSelection
{
  // An absent from/to selector is unrestricted. Each through group is an OR
  // of its objects; successive groups must match in the declared path order.
  SdcPathSelector from{};
  std::vector<SdcPathSelector> through;
  SdcPathSelector to{};

  auto operator==(const SdcPathSelection&) const -> bool = default;
};

enum class SdcExceptionKind
{
  kFalsePath,
  kMulticyclePath,
  kMinDelay,
  kMaxDelay,
};

enum class SdcMulticycleReference
{
  kStart,
  kEnd,
};

struct SdcPathException
{
  SdcExceptionKind kind = SdcExceptionKind::kFalsePath;
  SdcPathSelection path{};
  bool setup = true;
  bool hold = true;
  SdcTransition transition = SdcTransition::kBoth;
  int cycles = 1;
  // The parser normalizes the default: setup -> end, hold-only -> start.
  SdcMulticycleReference reference = SdcMulticycleReference::kEnd;
  bool reference_explicit = false;
  double delay_ns = 0.0;
  bool datapath_only = false;
  bool ignore_clock_latency = false;
  bool reset_path = false;
  bool match_start_end = false;

  auto operator==(const SdcPathException&) const -> bool = default;
};

enum class SdcClockGroupKind
{
  kAsynchronous,
  kLogicallyExclusive,
  kPhysicallyExclusive,
};

struct SdcClockGroup
{
  SdcClockGroupKind kind = SdcClockGroupKind::kAsynchronous;
  std::string name = "";
  std::vector<std::vector<SdcObjectRef>> groups;
  bool allow_paths = false;

  auto operator==(const SdcClockGroup&) const -> bool = default;
};

struct SdcClockLatency
{
  std::vector<SdcObjectRef> objects;
  std::vector<SdcObjectRef> clocks;
  double value_ns = 0.0;
  SdcTransition transition = SdcTransition::kBoth;
  bool min = true;
  bool max = true;
  bool source = false;
  // Source latency has independent min/max and early/late applicability.
  bool early = true;
  bool late = true;

  auto operator==(const SdcClockLatency&) const -> bool = default;
};

struct SdcClockUncertainty
{
  std::vector<SdcObjectRef> objects;
  SdcPathSelector from{};
  SdcPathSelector to{};
  double value_ns = 0.0;
  bool setup = true;
  bool hold = true;

  auto operator==(const SdcClockUncertainty&) const -> bool = default;
};

struct SdcClockTransition
{
  std::vector<SdcObjectRef> clocks;
  double value_ns = 0.0;
  SdcTransition transition = SdcTransition::kBoth;
  bool min = true;
  bool max = true;

  auto operator==(const SdcClockTransition&) const -> bool = default;
};

struct SdcIODelay
{
  std::vector<SdcObjectRef> objects;
  std::vector<SdcObjectRef> clocks;
  std::vector<SdcObjectRef> reference_pins;
  double value_ns = 0.0;
  SdcTransition transition = SdcTransition::kBoth;
  bool min = true;
  bool max = true;
  bool clock_fall = false;
  bool add_delay = false;
  bool source_latency_included = false;
  bool network_latency_included = false;

  auto operator==(const SdcIODelay&) const -> bool = default;
};

struct SdcInputTransition
{
  std::vector<SdcObjectRef> objects;
  double value_ns = 0.0;
  SdcTransition transition = SdcTransition::kBoth;
  bool min = true;
  bool max = true;

  auto operator==(const SdcInputTransition&) const -> bool = default;
};

struct SdcLoad
{
  std::vector<SdcObjectRef> objects;
  double value_pf = 0.0;
  SdcTransition transition = SdcTransition::kBoth;
  bool min = true;
  bool max = true;
  bool pin_load = false;
  bool wire_load = false;
  bool subtract_pin_load = false;

  auto operator==(const SdcLoad&) const -> bool = default;
};

enum class SdcConstraintStatusCode
{
  kOk,
  kMalformed,
  kUnsupported,
  kUnresolvedReference,
  kFileError,
};

struct SdcConstraintIssue
{
  SdcConstraintStatusCode code = SdcConstraintStatusCode::kOk;
  std::string command = "";
  std::string detail = "";

  auto operator==(const SdcConstraintIssue&) const -> bool = default;
};

struct SdcClockData
{
  std::vector<SdcClockDecl> clocks;
  std::vector<SdcCaseAnalysis> case_analyses;
  std::vector<std::string> diagnostics;
  std::vector<SdcPathException> path_exceptions;
  std::vector<SdcClockGroup> clock_groups;
  std::vector<SdcClockLatency> clock_latencies;
  std::vector<SdcClockUncertainty> clock_uncertainties;
  std::vector<SdcClockTransition> clock_transitions;
  std::vector<SdcIODelay> input_delays;
  std::vector<SdcIODelay> output_delays;
  std::vector<SdcInputTransition> input_transitions;
  std::vector<SdcLoad> loads;
  std::vector<SdcObjectRef> propagated_clocks;
  SdcConstraintStatusCode status = SdcConstraintStatusCode::kOk;
  std::vector<SdcConstraintIssue> issues;

  auto operator==(const SdcClockData&) const -> bool = default;
  [[nodiscard]] auto ok() const -> bool { return status == SdcConstraintStatusCode::kOk; }
};

}  // namespace icts
