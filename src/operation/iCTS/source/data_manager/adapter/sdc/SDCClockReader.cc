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
 * @file SDCClockReader.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-15
 * @brief Shared SDC frontend adapter implementation for iCTS.
 */

#include "SDCClockReader.hh"

#if __has_include(<tcl8.6/tcl.h>)
#include <tcl8.6/tcl.h>
#else
#include <tcl.h>
#endif

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "Database.hpp"
#include "Logger.hh"
#include "SDCCaseAnalysis.hh"
#include "SdcCommand.hpp"
#include "TimingConstraints.hh"
#include "clock_trace/ClockTraceResolver.hh"
#include "common/AnalysisType.hpp"
#include "common/TransType.hpp"
#include "data_manager/DataManager.hpp"
#include "dm_config.h"
#include "idm.h"
#include "sdc/Sdc.hpp"
#include "sdc/TimingClock.hpp"

namespace icts {
namespace {

auto ToSdcObjectKind(ista::ClockSourceKind kind) -> SdcObjectKind
{
  switch (kind) {
    case ista::ClockSourceKind::kPin:
      return SdcObjectKind::kPin;
    case ista::ClockSourceKind::kNet:
      return SdcObjectKind::kNet;
    case ista::ClockSourceKind::kPort:
      return SdcObjectKind::kPort;
  }
  return SdcObjectKind::kUnknown;
}

auto configuredSdcPath() -> std::string
{
  return dmInst->get_config().get_sdc_path();
}

}  // namespace

SdcClockReader::SdcClockReader() : SdcClockReader(configuredSdcPath())
{
}

SdcClockReader::SdcClockReader(std::string sdc_path) : _sdc_path(std::move(sdc_path))
{
}

auto SdcClockReader::readClockData() const -> SdcClockData
{
  SdcClockData data;
  if (_sdc_path.empty()) {
    data.status = SdcConstraintStatusCode::kFileError;
    data.issues.push_back({data.status, "read_sdc", "empty_sdc_path"});
    data.diagnostics.emplace_back("read_sdc:empty_sdc_path");
    CTSLOG.warn(Loc::current(), "SdcClockReader: SDC path is empty; no clock declarations are available.");
    return data;
  }
  if (!std::filesystem::exists(_sdc_path)) {
    data.status = SdcConstraintStatusCode::kFileError;
    data.issues.push_back({data.status, "read_sdc", "missing_sdc_file:" + _sdc_path});
    data.diagnostics.emplace_back("read_sdc:missing_sdc_file:" + _sdc_path);
    CTSLOG.warn(Loc::current(), "SdcClockReader: SDC file does not exist: ", _sdc_path);
    return data;
  }

  if (ista::DataManager::isInitialized()) {
    data.status = SdcConstraintStatusCode::kFileError;
    data.issues.push_back({data.status, "read_sdc", "ista_session_in_use"});
    data.diagnostics.emplace_back("read_sdc:ista_session_in_use");
    CTSLOG.warn(Loc::current(), "SdcClockReader: an existing STA session owns the shared database.");
    return data;
  }
  ista::DataManager::initInst();
  // This declaration read owns the database. Destroy it after the interpreter,
  // on success, parse failure, or exception; never release another tool's state.
  struct OwnedStaDatabase
  {
    ~OwnedStaDatabase() { ista::DataManager::destroyInst(); }
  } owned_database;

  ista::SdcCommand interpreter;
  ista::sdc::registerSdcCommands(interpreter);
  interpreter.setIgnoreUnknownCommands(true);
  sdc_reader::RegisterCaseAnalysisRecorder(interpreter, data);
  if (interpreter.evalScriptFile(_sdc_path) != TCL_OK || !data.ok()) {
    if (data.ok()) {
      data.status = SdcConstraintStatusCode::kFileError;
      const auto& errors = interpreter.getErrors();
      const std::string detail = errors.empty() ? "ista_sdc_parse_failed" : errors.front().message;
      data.issues.push_back({data.status, "read_sdc", detail});
      data.diagnostics.emplace_back("read_sdc:" + detail);
    }
    CTSLOG.warn(Loc::current(), "SdcClockReader: SDC rejected: ", data.diagnostics.front());
    return data;
  }
  for (const auto& command : interpreter.getIgnoredCommands()) {
    data.ignored.push_back(command + ":unsupported_sdc_command");
  }
  auto& clocks = ista::DataManager::getInst().getDatabase().sdc().get_clock_map();
  for (auto& [name, clock] : clocks) {
    SdcClockDecl decl;
    decl.clock_name = name;
    decl.period_ns = clock.get_period();
    decl.period_resolved = decl.period_ns > 0.0;
    decl.is_virtual = clock.get_source_objects().empty();
    decl.kind = clock.get_is_generated() ? SdcClockDecl::Kind::kGenerated : SdcClockDecl::Kind::kPrimary;
    decl.master_clock_name = clock.get_master_clock_name();
    if (const auto& generated = clock.get_generated_clock_definition(); generated.has_value()) {
      if (generated->divide_by.has_value()) {
        decl.divide_by = static_cast<int>(*generated->divide_by);
      }
      if (generated->multiply_by.has_value()) {
        decl.multiply_by = static_cast<int>(*generated->multiply_by);
      }
      decl.invert = generated->invert;
      if (!generated->master_source.empty()) {
        decl.generated_sources.push_back(SdcObjectRef{.kind = ToSdcObjectKind(generated->master_source_kind), .pattern = generated->master_source});
      }
    }
    decl.waveform_ns = clock.get_waveform();
    decl.waveform_explicit = !decl.waveform_ns.empty();
    decl.waveform_resolved = decl.waveform_explicit;
    const auto& generated_definition = clock.get_generated_clock_definition();
    const auto& source_kinds = generated_definition.has_value() ? generated_definition->target_kinds : clock.get_source_kinds();
    const auto& sources = clock.get_source_objects();
    for (std::size_t index = 0; index < sources.size(); ++index) {
      const auto kind = index < source_kinds.size() ? ToSdcObjectKind(source_kinds[index]) : SdcObjectKind::kPort;
      decl.targets.push_back(SdcObjectRef{.kind = kind, .pattern = sources[index]});
    }
    data.clocks.push_back(std::move(decl));
    if (clock.get_is_propagated()) {
      data.propagated_clocks.push_back(SdcObjectRef{.kind = SdcObjectKind::kClock, .pattern = name});
    }
    for (const auto& [analysis, transitions] : clock.get_transition_map()) {
      for (const auto& [transition, value] : transitions) {
        SdcClockTransition constraint;
        constraint.clocks.push_back(SdcObjectRef{.kind = SdcObjectKind::kClock, .pattern = name});
        constraint.value_ns = value;
        constraint.transition = transition == ista::TransType::kRise ? SdcTransition::kRise : SdcTransition::kFall;
        constraint.min = analysis == ista::AnalysisType::kMin;
        constraint.max = analysis == ista::AnalysisType::kMax;
        data.clock_transitions.push_back(std::move(constraint));
      }
    }
  }
  for (const auto& diagnostic : data.diagnostics) {
    CTSLOG.warn(Loc::current(), "SdcClockReader: ", diagnostic);
  }
  // Constructs iCTS does not read. Reported so nothing is dropped silently, but they
  // leave the clock model iCTS consumes intact, so they do not reject the run.
  for (const auto& ignored : data.ignored) {
    CTSLOG.warn(Loc::current(), "SdcClockReader: ignoring SDC construct outside iCTS scope: ", ignored);
  }
  return data;
}

auto SdcClockReader::traceClockTargets(const SdcClockData& clock_data, idb::IdbDesign* idb_design, const SdcLibertyCellLookup& liberty_cell_lookup,
                                       std::size_t max_fanout) -> ClockTraceBuild
{
  return ClockTraceResolver::resolve(clock_data, idb_design, liberty_cell_lookup, max_fanout);
}

}  // namespace icts
