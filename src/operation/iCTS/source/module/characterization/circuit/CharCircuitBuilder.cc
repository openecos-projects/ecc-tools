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
 * @file CharCircuitBuilder.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-20
 * @brief Temporary characterization circuit and parasitic setup.
 */

#include "characterization/circuit/CharCircuitBuilder.hh"

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "FastSTA.hh"
#include "Logger.hh"
#include "characterization/buffer_cell/CharacterizationBufferCell.hh"
#include "characterization/builder/CharBuilderImpl.hh"
#include "characterization/builder/CharFeasibilityChecker.hh"

namespace icts::char_builder::detail {

auto CharCircuitBuilder::createCharCircuit(const TopologyDesc& topo, const std::vector<std::string>& buf_masters) -> bool
{
  _impl._temp_inst_names.clear();
  _impl._temp_net_names.clear();
  _impl._fast_sta_char_context_id = ::icts::kInvalidFastStaCharContextId;

  const std::string id_prefix = "cts_char_" + std::to_string(_impl._char_circuit_id) + "_";

  const auto& source_buf = _impl._sorted_buffers.back();
  const auto& sink_buf = _impl._sorted_buffers.front();

  _impl._source_inst_name = id_prefix + "source";
  _impl._source_in_pin = _impl._source_inst_name + "/" + source_buf.input_pin;
  _impl._source_out_pin = _impl._source_inst_name + "/" + source_buf.output_pin;
  _impl._timing_observation_pin.clear();

  _impl._sink_inst_name = id_prefix + "sink";
  _impl._sink_in_pin = _impl._sink_inst_name + "/" + sink_buf.input_pin;

  for (size_t i = 0; i < buf_masters.size(); ++i) {
    const std::string inst_name = id_prefix + "buf_" + std::to_string(i);
    _impl._temp_inst_names.push_back(inst_name);
  }

  for (size_t i = 0; i < topo.wire_segments_um.size(); ++i) {
    const std::string net_name = id_prefix + "net_" + std::to_string(i);
    _impl._temp_net_names.push_back(net_name);
  }

  for (size_t bi = 0; bi < buf_masters.size(); ++bi) {
    const ::icts::CharacterizationBufferCell* buffer_cell_ptr = _impl.feasibilityChecker().findCharacterizationBufferCell(buf_masters.at(bi));
    if (buffer_cell_ptr == nullptr) {
      _impl._build_failure_reason = "characterization_buffer_cell_unavailable:" + buf_masters.at(bi);
      return false;
    }
    const auto& buffer_cell = *buffer_cell_ptr;

    if (bi + 1U == buf_masters.size()) {
      _impl._timing_observation_pin = _impl._temp_inst_names.at(bi) + "/" + buffer_cell.output_pin;
    }
  }

  if (_impl._timing_observation_pin.empty()) {
    _impl._timing_observation_pin = _impl._sink_in_pin;
  }

  _impl._char_clock_name = id_prefix + "clk";
  if (_impl._wrapper == nullptr) {
    CTSLOG.error(Loc::current(), "CharCircuitBuilder: Wrapper dependency is not configured.");
  }
  if (_impl._fast_sta == nullptr) {
    CTSLOG.error(Loc::current(), "CharCircuitBuilder: FastSTA dependency is not configured.");
  }
  const auto context_build = _impl._fast_sta->buildCharContext(::icts::FastStaCharTopologySpec{
      .wrapper = _impl._wrapper,
      .source_cell_master = source_buf.cell_master,
      .sink_cell_master = sink_buf.cell_master,
      .buffer_cell_masters = buf_masters,
      .wire_segments_um = topo.wire_segments_um,
      .dbu_per_um = _impl._dbu_per_um,
      .routing_layer = _impl._routing_layer,
      .wire_width_um = _impl._wire_width_um,
      .clock_period_ns = 10.0,
      .root_input_slew_ns = _impl._root_input_slew_ns,
  });
  if (!context_build.context_id.has_value()) {
    _impl._build_failure_reason = context_build.failure_reason.empty() ? "fast_sta_characterization_context_unavailable" : context_build.failure_reason;
    return false;
  }
  _impl._fast_sta_char_context_id = context_build.context_id.value();

  ++_impl._char_circuit_id;
  return true;
}

auto CharCircuitBuilder::setCharParasitics(double load_pf) const -> void
{
  if (_impl._fast_sta_char_context_id == ::icts::kInvalidFastStaCharContextId) {
    CTSLOG.error(Loc::current(), "Fast STA characterization context is not prepared before parasitic load update.");
  }
  if (_impl._fast_sta == nullptr) {
    CTSLOG.error(Loc::current(), "CharCircuitBuilder: FastSTA dependency is not configured.");
  }
  if (!_impl._fast_sta->setCharLoad(_impl._fast_sta_char_context_id, load_pf)) {
    CTSLOG.error(Loc::current(), "Fast STA characterization load update failed.");
  }
}

auto CharCircuitBuilder::destroyCharCircuit() -> void
{
  if (_impl._fast_sta_char_context_id != ::icts::kInvalidFastStaCharContextId) {
    if (_impl._fast_sta == nullptr) {
      CTSLOG.error(Loc::current(), "CharCircuitBuilder: FastSTA dependency is not configured.");
    }
    if (!_impl._fast_sta->eraseCharContext(_impl._fast_sta_char_context_id)) {
      CTSLOG.error(Loc::current(), "Fast STA characterization context release failed.");
    }
    _impl._fast_sta_char_context_id = ::icts::kInvalidFastStaCharContextId;
  }
  _impl._sink_inst_name.clear();
  _impl._source_inst_name.clear();
  _impl._temp_net_names.clear();
  _impl._temp_inst_names.clear();
  _impl._source_in_pin.clear();
  _impl._source_out_pin.clear();
  _impl._sink_in_pin.clear();
  _impl._timing_observation_pin.clear();
}

}  // namespace icts::char_builder::detail
