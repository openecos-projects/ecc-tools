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
 * @file IstaConstraintReader.cc
 * @brief Import native STA facts without retaining an engine or parser in CTS.
 */
#include "IstaConstraintReader.hh"

#include <any>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "Database.hpp"
#include "STAInterface.hpp"
#include "TimingConstraints.hh"
#include "common/AnalysisType.hpp"
#include "common/TimingCaseValue.hpp"
#include "common/TransType.hpp"
#include "data_manager/DataManager.hpp"
#include "dm_config.h"
#include "idm.h"
#include "network/Network.hpp"
#include "network/Pin.hpp"
#include "sdc/ElectricalLimits.hpp"
#include "sdc/Sdc.hpp"
#include "sdc/TimingClock.hpp"

namespace icts {
namespace {

auto objectKind(ista::ClockSourceKind kind) -> SdcObjectKind
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

auto electricalScope(ista::LimitObject scope) -> SdcElectricalScope
{
  switch (scope) {
    case ista::LimitObject::kDesign:
      return SdcElectricalScope::kDesign;
    case ista::LimitObject::kPort:
      return SdcElectricalScope::kPort;
    case ista::LimitObject::kPin:
      return SdcElectricalScope::kPin;
    case ista::LimitObject::kLibPin:
      return SdcElectricalScope::kLibPin;
    case ista::LimitObject::kClock:
      return SdcElectricalScope::kClock;
  }
  return SdcElectricalScope::kDesign;
}

auto transitionKind(ista::TransType edge) -> SdcTransition
{
  switch (edge) {
    case ista::TransType::kRise:
      return SdcTransition::kRise;
    case ista::TransType::kFall:
      return SdcTransition::kFall;
    case ista::TransType::kNone:
      return SdcTransition::kBoth;
  }
  return SdcTransition::kBoth;
}

auto caseValue(ista::TimingCaseValue value) -> SdcCaseValue
{
  switch (value) {
    case ista::TimingCaseValue::kZero:
      return SdcCaseValue::kZero;
    case ista::TimingCaseValue::kOne:
      return SdcCaseValue::kOne;
    case ista::TimingCaseValue::kRise:
      return SdcCaseValue::kRise;
    case ista::TimingCaseValue::kFall:
      return SdcCaseValue::kFall;
    case ista::TimingCaseValue::kStatic:
      return SdcCaseValue::kStatic;
    case ista::TimingCaseValue::kNone:
      return SdcCaseValue::kNone;
  }
  return SdcCaseValue::kNone;
}

auto objectName(const ista::Database& database, SdcObjectKind kind, const std::string& name) -> std::string
{
  if (kind != SdcObjectKind::kPin && kind != SdcObjectKind::kPort) {
    return name;
  }
  const auto pin = database.network().pins().find(name);
  if (pin == database.network().pins().end()) {
    return name;
  }
  const auto& value = pin->second;
  return value.get_is_port() ? value.get_pin_name() : value.get_instance_name() + "/" + value.get_pin_name();
}

auto clockDeclaration(const ista::Database& database, const std::string& name, const ista::TimingClock& clock) -> SdcClockDecl
{
  SdcClockDecl decl;
  decl.clock_name = name;
  decl.period_ns = clock.get_period();
  decl.period_resolved = decl.period_ns > 0.0;
  decl.is_virtual = clock.get_source_objects().empty();
  const auto& generated = clock.get_generated_clock_definition();
  decl.kind = generated.has_value() ? SdcClockDecl::Kind::kGenerated : SdcClockDecl::Kind::kPrimary;
  decl.master_clock_name = clock.get_master_clock_name();
  if (generated.has_value()) {
    decl.divide_by = generated->divide_by.value_or(1.0);
    decl.multiply_by = generated->multiply_by.value_or(1.0);
    decl.divide_by_explicit = generated->divide_by.has_value();
    decl.multiply_by_explicit = generated->multiply_by.has_value();
    decl.invert = generated->invert;
    decl.preinvert = generated->preinvert;
    decl.combinational = generated->combinational;
    decl.add = generated->add;
    decl.duty_cycle_percent = generated->duty_cycle;
    decl.generated_edges = generated->edges.value_or(std::vector<double>{});
    decl.generated_edge_shifts_ns = generated->edge_shifts.value_or(std::vector<double>{});
    if (!generated->master_source.empty()) {
      decl.generated_sources.push_back({.kind = objectKind(generated->master_source_kind),
                                        .name = objectName(database, objectKind(generated->master_source_kind), generated->master_source)});
    }
  }
  decl.waveform_ns = clock.get_waveform();
  decl.waveform_resolved = decl.period_resolved && !decl.waveform_ns.empty();
  const auto& kinds = generated.has_value() ? generated->target_kinds : clock.get_source_kinds();
  const auto& objects = clock.get_source_objects();
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    const auto kind = index < kinds.size() ? objectKind(kinds[index]) : SdcObjectKind::kUnknown;
    decl.targets.push_back({.kind = kind, .name = objectName(database, kind, objects[index])});
  }
  return decl;
}

auto rejected(SdcConstraintStatusCode status, const std::string& detail) -> SdcClockData
{
  SdcClockData data;
  data.status = status;
  data.issues.push_back({status, "native_sta_import", detail});
  data.diagnostics.push_back("native_sta_import:" + detail);
  return data;
}

struct OwnedStaSession
{
  ista::STAInterface* sta = nullptr;
  ~OwnedStaSession() { sta->destroySTA(); }
};

}  // namespace

auto IstaConstraintReader::read(const std::string& work_dir) -> SdcClockData
{
  // Check ownership before even initializing the shared STA logger/database.
  auto& sta = ista::STAInterface::getInst();
  if (ista::DataManager::isInitialized() || sta.isInitialized()) {
    return rejected(SdcConstraintStatusCode::kFileError, "ista_session_in_use");
  }
  const auto& path = dmInst->get_config().get_sdc_path();
  std::error_code error;
  if (path.empty() || !std::filesystem::is_regular_file(path, error)) {
    return rejected(SdcConstraintStatusCode::kFileError, "missing_sdc_file:" + path);
  }
  if (dmInst->get_idb_design() == nullptr || dmInst->get_idb_layout() == nullptr) {
    return rejected(SdcConstraintStatusCode::kFileError, "loaded_idb_design_required");
  }
  const auto generation = dmInst->get_liberty_generation();
  if (generation == nullptr || generation->get_libraries().empty()) {
    return rejected(SdcConstraintStatusCode::kFileError, "loaded_liberty_generation_required");
  }
  if (generation->get_paths() != dmInst->get_config().get_lib_paths()) {
    return rejected(SdcConstraintStatusCode::kFileError, "stale_liberty_generation");
  }
  const OwnedStaSession owned{&sta};
  std::string diagnostic;
  const std::map<std::string, std::any> config = {
      {"-temp_directory_path", (std::filesystem::path(work_dir) / "native_sta_constraints").string()},
      {"-thread_number", dmInst->get_config().get_thread_number()},
      {"-output_timing_reports", int32_t{0}},
      {"-output_timing_features", int32_t{0}},
  };
  if (!sta.tryInitConstraints(config, diagnostic)) {
    return rejected(SdcConstraintStatusCode::kUnsupported, diagnostic);
  }
  return snapshot(ista::DataManager::getInst().getDatabase());
}

auto IstaConstraintReader::snapshot(ista::Database& database) -> SdcClockData
{
  SdcClockData data;
  auto& sdc = database.sdc();
  for (auto& [name, clock] : sdc.get_clock_map()) {
    data.clocks.push_back(clockDeclaration(database, name, clock));
    if (clock.get_is_propagated()) {
      data.propagated_clocks.push_back({.kind = SdcObjectKind::kClock, .name = name});
    }
    for (const auto& [analysis, transitions] : clock.get_transition_map()) {
      for (const auto& [transition, value] : transitions) {
        data.clock_transitions.push_back({.clocks = {{.kind = SdcObjectKind::kClock, .name = name}},
                                          .value_ns = value,
                                          .transition = transitionKind(transition),
                                          .min = analysis == ista::AnalysisType::kMin,
                                          .max = analysis == ista::AnalysisType::kMax});
      }
    }
  }
  for (const auto& [name, value] : sdc.get_case_analysis_map()) {
    const auto pin = database.network().pins().find(name);
    const auto kind = pin != database.network().pins().end() && pin->second.get_is_port() ? SdcObjectKind::kPort : SdcObjectKind::kPin;
    data.case_analyses.push_back({.value = caseValue(value), .objects = {{.kind = kind, .name = objectName(database, kind, name)}}});
  }
  const auto fanout = sdc.get_max_fanout();
  if (fanout.has_value()) {
    data.electrical_limits.push_back(
        {.rule = SdcElectricalRule::kFanout, .scope = SdcElectricalScope::kDesign, .object_name = database.network().name(), .value = *fanout});
  }
  for (const auto& [name, value] : sdc.get_port_max_fanout_map()) {
    data.electrical_limits.push_back({.rule = SdcElectricalRule::kFanout, .scope = SdcElectricalScope::kPort, .object_name = name, .value = value});
    data.ignored.push_back("scoped_fanout_limit_not_applied_to_global_budget:" + name);
  }
  // iSTA has already converted SDC values through the main Liberty units to ns/pF.
  for (const auto& limit : sdc.electricalLimits().entries()) {
    data.electrical_limits.push_back(
        {.rule = limit.rule == ista::ElectricalRule::kTransition ? SdcElectricalRule::kTransition : SdcElectricalRule::kCapacitance,
         .scope = electricalScope(limit.object),
         .object_name = limit.object == ista::LimitObject::kPin ? objectName(database, SdcObjectKind::kPin, limit.name) : limit.name,
         .value = limit.value,
         .clock_path = limit.clock_path,
         .transition = transitionKind(limit.edge),
         .force = limit.force});
    if (limit.object != ista::LimitObject::kDesign) {
      data.ignored.push_back("scoped_electrical_limit_not_applied_to_global_budget:" + limit.name);
    }
  }
  return data;
}

}  // namespace icts
