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
 * @file Topology.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-01
 * @brief CTS topology formation entry for sink branches and source trunk.
 */

#include "synthesis/topology/Topology.hh"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Logger.hh"
#include "config/Config.hh"
#include "design/Clock.hh"
#include "design/ClockLayout.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "geometry/Geometry.hh"
#include "io/Wrapper.hh"
#include "stage/StageSummary.hh"
#include "synthesis/distribution/ClockDistribution.hh"
#include "synthesis/realization/ClockTreeRealization.hh"
#include "synthesis/topology/SourceTrunkStage.hh"
#include "synthesis/topology/layout/ClockLayoutBuilder.hh"
#include "synthesis/topology/buffer/BufferInsertion.hh"
#include "synthesis/topology/sink/SinkBranch.hh"
#include "synthesis/topology/trunk/SourceTrunk.hh"
#include "synthesis/trace/domain_status/DomainStatusRecorder.hh"

namespace icts {
namespace {

constexpr std::size_t kMinSynthesisSinkCount = 2U;

auto recordSynthesisBuild(SynthesisTraceSummary& summary, const Topology::Build& build) -> void
{
  summary.selected_htree_level_count = std::max(summary.selected_htree_level_count, build.summary.selected_htree_level_count);
  if (build.summary.selected_htree_depth.has_value()) {
    summary.selected_htree_depth = std::max(summary.selected_htree_depth, *build.summary.selected_htree_depth);
  }
  summary.htree_inserted_buffer_count += build.summary.htree_inserted_buffer_count;
  summary.htree_inserted_net_count += build.summary.htree_inserted_net_count;
}

auto recordSourceTrunkBuild(SynthesisTraceSummary& summary, const topology::SourceTrunkBuild& build) -> void
{
  if (build.summary.selected_depth.has_value()) {
    summary.selected_htree_depth = std::max(summary.selected_htree_depth, *build.summary.selected_depth);
  }
  summary.selected_htree_level_count = std::max(summary.selected_htree_level_count, build.summary.selected_level_count);
  summary.htree_inserted_buffer_count += build.summary.inserted_buffer_count;
  summary.htree_inserted_net_count += build.summary.inserted_net_count;
}

auto sourceTrunkSynthesisPhase(SourceTrunkStage stage) -> ClockLayoutPhase
{
  switch (stage) {
    case SourceTrunkStage::kSegment:
      return ClockLayoutPhase::kSourceToRootSegment;
    case SourceTrunkStage::kHTree:
      return ClockLayoutPhase::kSourceToRootHTree;
    case SourceTrunkStage::kUnknown:
      return ClockLayoutPhase::kUnknown;
  }
  return ClockLayoutPhase::kUnknown;
}

auto makeLogContext(const Clock& clock, const std::string& sink_domain, const std::string& stage, const std::string& object_name_prefix) -> HTree::LogContext
{
  return HTree::LogContext{
      .clock_name = clock.get_clock_name(),
      .clock_net_name = clock.get_clock_net_name(),
      .sink_domain = sink_domain,
      .stage = stage,
      .object_name_prefix = object_name_prefix,
  };
}

struct TrunkSourceStrengthening
{
  std::string cell_master;
  std::string input_pin_name;
  std::string output_pin_name;
};

// A clock gate with a weak output stage (e.g. an X1 clock gate) cannot legally
// drive a long source-to-root trunk: its liberty output-cap limit is below
// every characterized buffer-chain load, so the trunk label solver finds zero
// seed labels and reports source_trunk_label_no_legal_path. Strengthening the
// source with one buffer moves the trunk drive to the buffer, whose output
// limit covers the chain loads. Pick the strongest configured buffer whose
// input pin the weak source can still legally drive.
auto selectTrunkSourceStrengthening(Wrapper& wrapper, const Config& config, const Pin* clock_source) -> std::optional<TrunkSourceStrengthening>
{
  const auto source_drive_cap_pf = wrapper.queryClockSourceDriveCapLimit(config, clock_source);
  if (!source_drive_cap_pf.has_value()) {
    return std::nullopt;
  }
  std::optional<TrunkSourceStrengthening> selection;
  double best_drive_cap_pf = 0.0;
  for (const auto& cell_master : config.get_buffer_types()) {
    const auto ports = wrapper.queryBufferPorts(cell_master);
    const auto input_cap_pf = wrapper.queryCharInputPinCap(cell_master);
    if (!ports.has_value() || !input_cap_pf.has_value() || *input_cap_pf >= *source_drive_cap_pf) {
      continue;
    }
    auto drive_cap_pf = wrapper.queryCellOutPinCapLimit(cell_master);
    if (!drive_cap_pf.has_value()) {
      drive_cap_pf = wrapper.queryCellOutPinCapTableAxisMax(cell_master);
    }
    if (!drive_cap_pf.has_value()) {
      continue;
    }
    if (!selection.has_value() || *drive_cap_pf > best_drive_cap_pf) {
      selection = TrunkSourceStrengthening{
          .cell_master = cell_master,
          .input_pin_name = ports->input,
          .output_pin_name = ports->output,
      };
      best_drive_cap_pf = *drive_cap_pf;
    }
  }
  return selection;
}

auto clearClockSynthesizedMembership(Design& design, Clock& clock) -> void
{
  (void) ClockTreeRealization::restoreClockSourceNetToSynthesisFrontier(clock);
  design.removeClockSynthesizedObjects(clock);
}

auto collectRootInputs(const std::vector<ClockDistributionContext>& sink_domains) -> std::vector<Pin*>
{
  std::vector<Pin*> root_inputs;
  root_inputs.reserve(sink_domains.size());
  for (const auto& context : sink_domains) {
    if (context.root_input != nullptr) {
      root_inputs.push_back(context.root_input);
    }
  }
  return root_inputs;
}

auto collectSourceTrunkLengthsUm(Wrapper& wrapper, Pin* clock_source, const std::vector<Pin*>& root_inputs) -> std::optional<std::vector<double>>
{
  std::vector<double> lengths_um;
  if (clock_source == nullptr || root_inputs.empty()) {
    return lengths_um;
  }

  const auto dbu_per_um_value = wrapper.queryDbUnit();
  if (!dbu_per_um_value.has_value()) {
    return std::nullopt;
  }
  const auto dbu_per_um = static_cast<double>(*dbu_per_um_value);
  lengths_um.reserve(root_inputs.size());
  for (const auto* root_input : root_inputs) {
    if (root_input == nullptr) {
      continue;
    }
    const int distance_dbu = geometry::Manhattan(clock_source->get_location(), root_input->get_location());
    const double length_um = static_cast<double>(std::max(distance_dbu, 0)) / dbu_per_um;
    if (length_um > 0.0) {
      lengths_um.push_back(length_um);
    }
  }
  return lengths_um;
}

class ClockTopologySynthesis
{
 public:
  explicit ClockTopologySynthesis(const ClockTopologyInput& input)
      : _config(input.config),
        _design(input.design),
        _wrapper(input.wrapper),
        _fast_sta(input.fast_sta),
        _clock(input.clock),
        _clock_index(input.clock_index),
        _clock_layout(input.clock_layout),
        _summary(input.summary),
        _status_recorder(input.status_recorder),
        _characterization_library(input.characterization_library),
        _valid_sinks(input.valid_sinks),
        _sink_domains(input.sink_domains)
  {
  }

  auto synthesize() -> bool
  {
    auto root_inputs = collectRootInputs(*_sink_domains);
    const auto source_trunk_lengths_um = collectSourceTrunkLengthsUm(*_wrapper, _clock->get_clock_source(), root_inputs);
    if (!source_trunk_lengths_um.has_value()) {
      CTSLOG.warn(Loc::current(), "Topology: DBU-per-micron is unavailable for clock \"", _clock->get_clock_name(), "\".");
      return false;
    }
    for (const auto& context : *_sink_domains) {
      if (!buildAndCommitSinkDomain(context, *source_trunk_lengths_um)) {
        return false;
      }
    }
    return buildAndCommitSourceTrunk(root_inputs);
  }

 private:
  auto commitSinkDomainBuild(const ClockDistributionContext& context, Topology::Build& synthesis_build, std::string& failure_reason) -> bool
  {
    auto pending_clock_layout = ClockLayoutBuilder::makeSinkDomainLayout(*_clock, _clock_index, context, synthesis_build);
    if (!ClockTreeRealization::commitInsertedObjects(InsertedObjectCommitInput{
            .design = _design,
            .clock = _clock,
            .inserted_insts = &synthesis_build.output.inserted_insts,
            .inserted_pins = &synthesis_build.output.inserted_pins,
            .inserted_nets = &synthesis_build.output.inserted_nets,
            .propagation_arcs = &synthesis_build.output.propagation_arcs,
        })) {
      ClockTreeRealization::reconnectNet(NetConnectionInput{
          .net = context.downstream_net,
          .driver = context.downstream_net->get_driver(),
          .loads = context.sinks,
      });
      failure_reason = "failed to commit inserted synthesis objects";
      Topology::resetClockTopology(*_design, *_clock);
      return false;
    }

    ClockLayoutBuilder::merge(*_clock_layout, pending_clock_layout);
    recordSynthesisBuild(*_summary, synthesis_build);
    return true;
  }

  auto buildAndCommitSinkDomain(const ClockDistributionContext& context, const std::vector<double>& source_trunk_lengths_um) -> bool
  {
    const auto* const sink_domain_label = ToString(context.sink_domain);
    if (context.sinks.size() < kMinSynthesisSinkCount) {
      ClockLayoutBuilder::appendDirectSinkDomain(*_clock_layout, *_clock, _clock_index, context);
      _status_recorder->append(*_clock, DomainStatus::kFinished, context.sink_domain, _valid_sinks, context.sinks.size(), "direct");
      return true;
    }

    Topology::Input synthesis_input{
        .config = _config,
        .design = _design,
        .wrapper = _wrapper,
        .fast_sta = _fast_sta,
        .root_net = context.downstream_net,
        .object_name_prefix = context.domain_prefix,
        .characterization_library = _characterization_library,
        .additional_characterization_lengths_um = source_trunk_lengths_um,
        .clock_period_ns = _clock->get_clock_period_ns(),
        .clock_period_source = _clock->get_clock_period_source(),
        .log_context = makeLogContext(*_clock, sink_domain_label, "downstream_htree", context.domain_prefix),
    };
    Topology::Config synthesis_config{
        .enable_sink_clustering = _clock->is_preclustered_sink_reuse() ? false : _config->is_enable_sink_clustering(),
    };

    std::string failure_reason;
    auto synthesis_build = Topology::build(synthesis_input, synthesis_config);
    if (!synthesis_build.summary.success) {
      failure_reason = synthesis_build.summary.failure_reason.empty() ? "sink-domain synthesis failed" : synthesis_build.summary.failure_reason;
    } else {
      (void) commitSinkDomainBuild(context, synthesis_build, failure_reason);
    }

    if (!failure_reason.empty()) {
      _status_recorder->append(*_clock, DomainStatus::kFailed, context.sink_domain, _valid_sinks, context.sinks.size(), failure_reason);
      CTSLOG.warn(Loc::current(), "Topology: clock \"", _clock->get_clock_name(), "\" sink domain ", sink_domain_label, " failed: ", failure_reason);
      Topology::resetClockTopology(*_design, *_clock);
      return false;
    }

    _status_recorder->append(*_clock, DomainStatus::kFinished, context.sink_domain, _valid_sinks, context.sinks.size(), "synthesis");
    return true;
  }

  auto buildAndCommitSourceTrunk(const std::vector<Pin*>& root_inputs) -> bool
  {
    const auto source_trunk_domain = SinkDomainKind::kSourceToRoot;
    const auto* const source_trunk_label = ToString(source_trunk_domain);
    auto* clock_source = _clock->get_clock_source();
    auto* clock_source_net = _clock->get_clock_source_net();
    if (clock_source_net == nullptr && clock_source != nullptr) {
      clock_source_net = clock_source->get_net();
      _clock->set_clock_source_net(clock_source_net);
    }
    if (clock_source == nullptr || clock_source_net == nullptr) {
      _status_recorder->append(*_clock, DomainStatus::kFailed, source_trunk_domain, _valid_sinks, root_inputs.size(), "missing clock source or source net");
      CTSLOG.warn(Loc::current(), "Topology: clock \"", _clock->get_clock_name(), "\" source trunk formation failed because the source pin or net is missing.");
      Topology::resetClockTopology(*_design, *_clock);
      return false;
    }

    const auto source_trunk_prefix = ClockTreeRealization::makeSinkDomainPrefix(*_clock, _clock_index, source_trunk_domain);
    topology::SourceTrunkInput source_trunk_input{
        .config = _config,
        .design = _design,
        .wrapper = _wrapper,
        .fast_sta = _fast_sta,
        .source_net = clock_source_net,
        .clock_source = clock_source,
        .root_inputs = root_inputs,
        .object_name_prefix = source_trunk_prefix,
        .characterization_library = _characterization_library,
        .clock_period_ns = _clock->get_clock_period_ns(),
        .clock_period_source = _clock->get_clock_period_source(),
        .log_context = makeLogContext(*_clock, source_trunk_label, "source_to_root", source_trunk_prefix),
    };
    auto source_trunk_build = topology::BuildSourceTrunkTree(source_trunk_input);
    // A weak clock-gate source (e.g. an X1 clock gate) may be unable to drive a
    // long source-to-root trunk within its liberty output-cap limit, leaving
    // the trunk label solver without any legal seed. Strengthen such a source
    // once: insert a buffer at the source and rebuild the trunk from the
    // buffer output, then commit both payloads together.
    if (!source_trunk_build.summary.success && source_trunk_build.summary.failure_reason == "source_trunk_label_no_legal_path"
        && clock_source->get_inst() != nullptr) {
      if (const auto strengthening = selectTrunkSourceStrengthening(*_wrapper, *_config, clock_source); strengthening.has_value()) {
        topology::SourceNetSideEffectGuard source_side_effects(*clock_source_net, clock_source, root_inputs);
        auto inst = std::make_unique<Inst>(topology::MakeObjectName(source_trunk_prefix, "root_strengthen_buf"), strengthening->cell_master,
                                           InstType::kBuffer, clock_source->get_location());
        auto* inst_ptr = inst.get();
        auto input_pin = std::make_unique<Pin>(strengthening->input_pin_name, PinType::kIn, clock_source->get_location(), inst_ptr, nullptr, false);
        auto* input_pin_ptr = input_pin.get();
        auto output_pin = std::make_unique<Pin>(strengthening->output_pin_name, PinType::kOut, clock_source->get_location(), inst_ptr, nullptr, false);
        auto* output_pin_ptr = output_pin.get();
        inst_ptr->add_pin(input_pin_ptr);
        inst_ptr->add_pin(output_pin_ptr);
        auto source_side_net = std::make_unique<Net>(topology::MakeObjectName(source_trunk_prefix, "root_strengthen_net"));
        auto* source_side_net_ptr = source_side_net.get();
        topology::ConnectNet(topology::TopologyNetConnectionInput{
            .net = source_side_net_ptr,
            .driver = clock_source,
            .sinks = {input_pin_ptr},
        });
        topology::ReconnectExistingNet(topology::TopologyNetConnectionInput{
            .net = clock_source_net,
            .driver = output_pin_ptr,
            .sinks = root_inputs,
        });
        auto retry_input = source_trunk_input;
        retry_input.clock_source = output_pin_ptr;
        auto retry_build = topology::BuildSourceTrunkTree(retry_input);
        if (retry_build.summary.success) {
          retry_build.output.inserted_insts.push_back(std::move(inst));
          retry_build.output.inserted_pins.push_back(std::move(input_pin));
          retry_build.output.inserted_pins.push_back(std::move(output_pin));
          retry_build.output.inserted_nets.push_back(std::move(source_side_net));
          retry_build.output.propagation_arcs.push_back(ClockPropagationArc{
              .inst = inst_ptr,
              .input_pin = input_pin_ptr,
              .output_pin = output_pin_ptr,
              .kind = ClockPropagationKind::kBuffer,
              .origin = ClockPropagationOrigin::kSynthesized,
              .path_buffer_weight = 1,
          });
          CTSLOG.warn(Loc::current(), "Topology: clock \"", _clock->get_clock_name(), "\" source trunk strengthened with buffer \"",
                      inst_ptr->get_name(), "\" (", strengthening->cell_master, ") after weak-source label failure.");
          source_trunk_build = std::move(retry_build);
        } else {
          source_side_effects.restore();
        }
      }
    }
    const auto source_trunk_phase = sourceTrunkSynthesisPhase(source_trunk_build.summary.stage);
    if (!source_trunk_build.summary.success) {
      const auto failure_reason
          = source_trunk_build.summary.failure_reason.empty() ? "source trunk formation failed" : source_trunk_build.summary.failure_reason;
      _status_recorder->append(*_clock, DomainStatus::kFailed, source_trunk_domain, _valid_sinks, root_inputs.size(), failure_reason);
      CTSLOG.warn(Loc::current(), "Topology: clock \"", _clock->get_clock_name(), "\" source trunk formation failed: ", failure_reason);
      Topology::resetClockTopology(*_design, *_clock);
      return false;
    }

    auto pending_clock_layout = ClockLayoutBuilder::makeSourceToRootLayout(*_clock, _clock_index, *clock_source_net, source_trunk_build, source_trunk_phase);
    if (!ClockTreeRealization::commitInsertedObjects(InsertedObjectCommitInput{
            .design = _design,
            .clock = _clock,
            .inserted_insts = &source_trunk_build.output.inserted_insts,
            .inserted_pins = &source_trunk_build.output.inserted_pins,
            .inserted_nets = &source_trunk_build.output.inserted_nets,
            .propagation_arcs = &source_trunk_build.output.propagation_arcs,
        })) {
      _status_recorder->append(*_clock, DomainStatus::kFailed, source_trunk_domain, _valid_sinks, root_inputs.size(), "failed to commit source trunk objects");
      CTSLOG.warn(Loc::current(), "Topology: clock \"", _clock->get_clock_name(), "\" source trunk formation failed while committing inserted objects.");
      Topology::resetClockTopology(*_design, *_clock);
      return false;
    }
    ClockLayoutBuilder::merge(*_clock_layout, pending_clock_layout);
    recordSourceTrunkBuild(*_summary, source_trunk_build);
    _status_recorder->append(*_clock, DomainStatus::kFinished, source_trunk_domain, _valid_sinks, root_inputs.size(),
                             ToString(source_trunk_build.summary.stage));
    return true;
  }

  const Config* _config = nullptr;
  Design* _design = nullptr;
  Wrapper* _wrapper = nullptr;
  FastSTA* _fast_sta = nullptr;
  Clock* _clock = nullptr;
  std::size_t _clock_index = 0U;
  ClockLayout* _clock_layout = nullptr;
  SynthesisTraceSummary* _summary = nullptr;
  DomainStatusRecorder* _status_recorder = nullptr;
  CharacterizationLibrary* _characterization_library = nullptr;
  std::size_t _valid_sinks = 0U;
  const std::vector<ClockDistributionContext>* _sink_domains = nullptr;
};

}  // namespace

auto Topology::build(const Input& input, const Config& config) -> Build
{
  return topology::BuildSinkTree(input, config);
}

auto Topology::resetClockTopology(Design& design, Clock& clock) -> void
{
  clearClockSynthesizedMembership(design, clock);
}

auto Topology::formClock(const ClockTopologyInput& input) -> bool
{
  if (input.config == nullptr) {
    CTSLOG.error(Loc::current(), "Topology: clock topology config is null.");
  }
  if (input.design == nullptr) {
    CTSLOG.error(Loc::current(), "Topology: clock topology design is null.");
  }
  if (input.wrapper == nullptr) {
    CTSLOG.error(Loc::current(), "Topology: clock topology wrapper is null.");
  }
  if (input.fast_sta == nullptr) {
    CTSLOG.error(Loc::current(), "Topology: clock topology FastSTA is null.");
  }
  if (input.clock == nullptr) {
    CTSLOG.error(Loc::current(), "Topology: clock topology clock is null.");
  }
  if (input.clock_layout == nullptr) {
    CTSLOG.error(Loc::current(), "Topology: clock topology layout is null.");
  }
  if (input.summary == nullptr) {
    CTSLOG.error(Loc::current(), "Topology: clock topology summary is null.");
  }
  if (input.status_recorder == nullptr) {
    CTSLOG.error(Loc::current(), "Topology: clock topology status recorder is null.");
  }
  if (input.characterization_library == nullptr) {
    CTSLOG.error(Loc::current(), "Topology: clock topology characterization library is null.");
  }
  if (input.sink_domains == nullptr) {
    CTSLOG.error(Loc::current(), "Topology: clock topology sink domains are null.");
  }

  ClockTopologySynthesis synthesis(input);
  return synthesis.synthesize();
}

}  // namespace icts
