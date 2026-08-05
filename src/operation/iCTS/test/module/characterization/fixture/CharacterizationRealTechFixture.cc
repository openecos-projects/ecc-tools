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
 * @file CharacterizationRealTechFixture.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-18
 * @brief Compiled helpers for real-tech characterization tests.
 */

#include "module/characterization/fixture/CharacterizationRealTechFixture.hh"

#include <cmath>
#include <filesystem>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#include "ClockRouteSegmentRC.hh"
#include "Logger.hh"
#include "characterization/Characterization.hh"
#include "data_manager/DataManager.hh"
#include "data_manager/config/Config.hh"
#include "data_manager/realtech/setup/RealTechDesignSetup.hh"
#include "idm.h"
#include "io/Wrapper.hh"
#include "toolkit/io/TestArtifactIO.hh"

namespace icts_test::characterization::realtech {

namespace {

auto BuildRuntimeCharacterizationBufferCells(const std::vector<std::string>& buffer_types) -> std::vector<icts::CharacterizationBufferCell>
{
  std::vector<icts::CharacterizationBufferCell> buffer_cells;
  buffer_cells.reserve(buffer_types.size());
  for (const auto& cell_master : buffer_types) {
    const auto ports = CTSDM.getWrapper().queryBufferPorts(cell_master);
    const auto input_cap_pf = CTSDM.getWrapper().queryCharInputPinCap(cell_master);
    if (!ports.has_value() || !input_cap_pf.has_value()) {
      continue;
    }
    buffer_cells.push_back(icts::CharacterizationBufferCell{
        .cell_master = cell_master,
        .max_cap_pf = 0.0,
        .input_cap_pf = *input_cap_pf,
        .input_slew_limit_ns = CTSDM.getWrapper().queryCellInPinSlewLimit(cell_master),
        .input_slew_table_axis_max_ns = CTSDM.getWrapper().queryCellInPinSlewTableAxisMax(cell_master),
        .output_cap_limit_pf = CTSDM.getWrapper().queryCellOutPinCapLimit(cell_master),
        .output_cap_table_axis_max_pf = CTSDM.getWrapper().queryCellOutPinCapTableAxisMax(cell_master),
        .cell_height_um = CTSDM.getWrapper().queryCellHeightUm(cell_master),
        .input_pin = ports->input,
        .output_pin = ports->output,
    });
  }
  return buffer_cells;
}

}  // namespace

auto CaptureConfigState() -> ConfigState
{
  ConfigState state{};
  state.skew_bound = CTSDM.getConfig().get_skew_bound();
  state.max_buf_tran = CTSDM.getConfig().get_max_buf_tran();
  state.root_input_slew = CTSDM.getConfig().get_root_input_slew();
  state.max_sink_tran = CTSDM.getConfig().get_max_sink_tran();
  state.max_cap = CTSDM.getConfig().get_max_cap();
  state.has_max_buf_tran = CTSDM.getConfig().has_max_buf_tran();
  state.has_max_cap = CTSDM.getConfig().has_max_cap();
  state.wirelength_unit_um = CTSDM.getConfig().get_wirelength_unit_um();
  state.wirelength_iterations = CTSDM.getConfig().get_wirelength_iterations();
  state.slew_steps = CTSDM.getConfig().get_slew_steps();
  state.cap_steps = CTSDM.getConfig().get_cap_steps();
  state.wire_width = CTSDM.getConfig().get_wire_width();
  state.max_fanout = CTSDM.getConfig().get_max_fanout();
  state.routing_layers = CTSDM.getConfig().get_routing_layers();
  state.buffer_types = CTSDM.getConfig().get_buffer_types();
  state.char_buf_redundancy_pct = CTSDM.getConfig().get_char_buf_redundancy_pct();
  state.force_branch_buffer = CTSDM.getConfig().is_force_branch_buffer();
  state.enable_sink_clustering = CTSDM.getConfig().is_enable_sink_clustering();
  state.work_dir = CTSDM.getConfig().get_work_dir();
  state.log_file = CTSDM.getConfig().get_log_file();
  state.visualization_dir = CTSDM.getConfig().get_visualization_dir();
  state.statistics_dir = CTSDM.getConfig().get_statistics_dir();
  return state;
}

auto ApplyConfigState(const ConfigState& state) -> void
{
  CTSDM.getConfig().reset();
  CTSDM.getConfig().set_skew_bound(state.skew_bound);
  if (state.has_max_buf_tran) {
    CTSDM.getConfig().set_max_buf_tran(state.max_buf_tran);
  }
  CTSDM.getConfig().set_root_input_slew(state.root_input_slew);
  CTSDM.getConfig().set_max_sink_tran(state.max_sink_tran);
  if (state.has_max_cap) {
    CTSDM.getConfig().set_max_cap(state.max_cap);
  }
  CTSDM.getConfig().set_wirelength_unit_um(state.wirelength_unit_um);
  CTSDM.getConfig().set_wirelength_iterations(state.wirelength_iterations);
  CTSDM.getConfig().set_slew_steps(state.slew_steps);
  CTSDM.getConfig().set_cap_steps(state.cap_steps);
  CTSDM.getConfig().set_wire_width(state.wire_width);
  CTSDM.getConfig().set_max_fanout(state.max_fanout);
  CTSDM.getConfig().set_routing_layers(state.routing_layers);
  CTSDM.getConfig().set_buffer_types(state.buffer_types);
  CTSDM.getConfig().set_char_buf_redundancy_pct(state.char_buf_redundancy_pct);
  CTSDM.getConfig().set_force_branch_buffer(state.force_branch_buffer);
  CTSDM.getConfig().set_enable_sink_clustering(state.enable_sink_clustering);
  CTSDM.getConfig().set_work_dir(state.work_dir);
  CTSDM.getConfig().set_log_file(state.log_file);
  CTSDM.getConfig().set_visualization_dir(state.visualization_dir);
  CTSDM.getConfig().set_statistics_dir(state.statistics_dir);
}

auto MakeRuntimeCharBuilderContract() -> RuntimeCharBuilderContract
{
  RuntimeCharBuilderContract contract;
  if (CTSDM.getConfig().has_max_buf_tran() && CTSDM.getConfig().get_max_buf_tran() > 0.0) {
    contract.config.max_slew_ns = CTSDM.getConfig().get_max_buf_tran();
  }
  if (CTSDM.getConfig().has_max_cap() && CTSDM.getConfig().get_max_cap() > 0.0) {
    contract.config.max_cap_pf = CTSDM.getConfig().get_max_cap();
  }
  if (CTSDM.getConfig().get_wirelength_unit_um() > 0.0) {
    contract.config.wirelength_unit_um = CTSDM.getConfig().get_wirelength_unit_um();
  }
  contract.config.wirelength_iterations = CTSDM.getConfig().get_wirelength_iterations();
  contract.config.slew_steps = CTSDM.getConfig().get_slew_steps();
  contract.config.cap_steps = CTSDM.getConfig().get_cap_steps();
  contract.input.buffer_types = CTSDM.getConfig().get_buffer_types();
  contract.input.characterization_buffer_cells = BuildRuntimeCharacterizationBufferCells(contract.input.buffer_types);
  contract.config.char_buf_redundancy_pct = CTSDM.getConfig().get_char_buf_redundancy_pct();

  const auto& routing_layers = CTSDM.getConfig().get_routing_layers();
  if (!routing_layers.empty()) {
    contract.config.routing_layer = static_cast<int>(routing_layers.front());
  }
  if (CTSDM.getConfig().get_wire_width() > 0.0) {
    contract.config.wire_width_um = CTSDM.getConfig().get_wire_width();
  }
  contract.input.clock_route_segment_rc = CTSDM.getWrapper().queryConfiguredClockRouteSegmentRc(CTSDM.getConfig());
  const auto dbu_per_um = CTSDM.getWrapper().queryDbUnit();
  if (dbu_per_um > 0) {
    contract.input.dbu_per_um = dbu_per_um;
  }
  contract.input.root_input_slew_ns = std::max(0.0, CTSDM.getConfig().get_root_input_slew());
  contract.input.wrapper = &CTSDM.getWrapper();
  contract.input.fast_sta = &CTSDM.getFastSTA();
  return contract;
}

auto MakeRealTechCharConfigState(const ConfigState& baseline_state, std::optional<std::vector<std::string>> buffer_types, double max_buf_tran_ns,
                                 double max_cap_pf, bool omit_wirelength_unit, bool force_branch_buffer) -> ConfigState
{
  auto configured_state = baseline_state;
  configured_state.has_max_buf_tran = max_buf_tran_ns > 0.0;
  configured_state.max_buf_tran = max_buf_tran_ns;
  configured_state.has_max_cap = max_cap_pf > 0.0;
  configured_state.max_cap = max_cap_pf;
  configured_state.wirelength_unit_um = omit_wirelength_unit ? 0.0 : kRealTechCharWirelengthUnitUm;
  configured_state.wirelength_iterations = kRealTechCharWirelengthIterations;
  configured_state.slew_steps = kRealTechCharSlewSteps;
  configured_state.cap_steps = kRealTechCharCapSteps;
  configured_state.char_buf_redundancy_pct = 0.0;
  configured_state.force_branch_buffer = force_branch_buffer;
  if (buffer_types.has_value()) {
    configured_state.buffer_types = *buffer_types;
  }
  return configured_state;
}

RealTechCharFixture::RealTechCharFixture() = default;

RealTechCharFixture::~RealTechCharFixture()
{
  restore();
}

auto RealTechCharFixture::prepare(const std::string& scenario_name, std::optional<std::vector<std::string>> buffer_types, double max_buf_tran_ns,
                                  double max_cap_pf, bool omit_wirelength_unit, bool force_branch_buffer) -> std::optional<std::string>
{
  if (_is_prepared) {
    restore();
  }

  const auto& setup_state = icts_test::data_manager::realtech::EnsureRealTechSetup();
  if (setup_state.mode != icts_test::data_manager::realtech::RealTechMode::kRealTech || !setup_state.setup_succeeded) {
    return setup_state.summary;
  }

  const auto& cts_config_path = setup_state.cts_config_path;
  if (cts_config_path.empty() || !std::filesystem::exists(cts_config_path)) {
    return "Cannot resolve real-tech CTS config path from setup state.";
  }

  const auto original_config_state = CaptureConfigState();
  auto configured_state
      = MakeRealTechCharConfigState(original_config_state, std::move(buffer_types), max_buf_tran_ns, max_cap_pf, omit_wirelength_unit, force_branch_buffer);
  _original_config_state = original_config_state;
  ApplyConfigState(configured_state);

  const auto output_dir = icts_test::toolkit::io::ResolveOutputDir() / "characterization" / "realtech" / scenario_name;
  std::error_code error_code;
  std::filesystem::create_directories(output_dir, error_code);
  if (error_code) {
    return "Cannot create real-tech characterization output directory.";
  }

  CTSLOG.openLogFileStream((output_dir / "cts.log").string());
  CTSLOG.info(icts::Loc::current(), "Characterization scenario: ", scenario_name, ", omit wirelength unit=", omit_wirelength_unit ? "true" : "false",
              ", force branch buffer=", force_branch_buffer ? "true" : "false", ".");
  _is_prepared = true;
  return std::nullopt;
}

auto RealTechCharFixture::restore() -> void
{
  if (!_is_prepared || !_original_config_state.has_value()) {
    return;
  }

  ApplyConfigState(*_original_config_state);
  CTSDM.getWrapper().reset();
  if (auto* idb_builder = dmInst->get_idb_builder(); idb_builder != nullptr) {
    CTSDM.getWrapper().init(idb_builder);
  }
  _is_prepared = false;
  _original_config_state.reset();
  CTSLOG.closeLogFileStream();
}

auto JoinStrings(const std::vector<std::string>& values) -> std::string
{
  std::ostringstream output_stream;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      output_stream << ",";
    }
    output_stream << values.at(index);
  }
  return output_stream.str();
}

auto WriteScenarioReport(const std::string& scenario_name, const std::string& file_name, const std::string& content) -> bool
{
  const auto output_dir = icts_test::toolkit::io::ResolveOutputDir() / "characterization" / "realtech" / scenario_name;
  std::error_code error_code;
  std::filesystem::create_directories(output_dir, error_code);
  if (error_code) {
    return false;
  }
  return icts_test::toolkit::io::WriteTextArtifact(output_dir / file_name, content);
}

auto CollectConfiguredBufferLimitInfo() -> std::vector<BufferLimitInfo>
{
  std::vector<BufferLimitInfo> infos;
  std::set<std::string> seen_cell_masters;

  for (const auto& cell_master : CTSDM.getConfig().get_buffer_types()) {
    if (!seen_cell_masters.insert(cell_master).second) {
      continue;
    }

    const auto ports = CTSDM.getWrapper().queryBufferPorts(cell_master);
    if (!ports.has_value()) {
      continue;
    }

    infos.push_back(BufferLimitInfo{
        .cell_master = cell_master,
        .input_pin = ports->input,
        .output_pin = ports->output,
        .port_slew_limit_ns = CTSDM.getWrapper().queryCellInPinSlewLimit(cell_master),
        .table_slew_limit_ns = CTSDM.getWrapper().queryCellInPinSlewTableAxisMax(cell_master),
        .port_cap_limit_pf = CTSDM.getWrapper().queryCellOutPinCapLimit(cell_master),
        .table_cap_limit_pf = CTSDM.getWrapper().queryCellOutPinCapTableAxisMax(cell_master),
    });
  }

  return infos;
}

auto CollectUsableBufferMasters(const std::vector<BufferLimitInfo>& infos) -> std::vector<std::string>
{
  std::vector<std::string> masters;
  for (const auto& info : infos) {
    const bool has_slew_limit = HasPositiveValue(info.port_slew_limit_ns) || HasPositiveValue(info.table_slew_limit_ns);
    const bool has_cap_limit = HasPositiveValue(info.port_cap_limit_pf) || HasPositiveValue(info.table_cap_limit_pf);
    if (has_slew_limit && has_cap_limit) {
      masters.push_back(info.cell_master);
    }
  }
  return masters;
}

auto LookupBufferInfo(const std::vector<BufferLimitInfo>& infos, const std::string& cell_master) -> const BufferLimitInfo*
{
  auto it = std::ranges::find_if(infos, [&cell_master](const BufferLimitInfo& info) -> bool { return info.cell_master == cell_master; });
  return it == infos.end() ? nullptr : &(*it);
}

auto MinPositiveResolvedLimit(const std::vector<BufferLimitInfo>& infos, const std::vector<std::string>& selected_masters, bool for_slew) -> double
{
  double port_min = std::numeric_limits<double>::infinity();
  double table_min = std::numeric_limits<double>::infinity();

  for (const auto& cell_master : selected_masters) {
    const auto* info = LookupBufferInfo(infos, cell_master);
    if (info == nullptr) {
      continue;
    }

    const auto& port_value = for_slew ? info->port_slew_limit_ns : info->port_cap_limit_pf;
    const auto& table_value = for_slew ? info->table_slew_limit_ns : info->table_cap_limit_pf;
    if (port_value.has_value() && port_value.value() > 0.0) {
      port_min = std::min(port_min, port_value.value());
    }
    if (table_value.has_value() && table_value.value() > 0.0) {
      table_min = std::min(table_min, table_value.value());
    }
  }

  if (std::isfinite(port_min)) {
    return port_min;
  }
  if (std::isfinite(table_min)) {
    return table_min;
  }
  return 0.0;
}

auto ResolveDefaultWirelengthUnitUm(const std::vector<BufferLimitInfo>& infos, const std::vector<std::string>& selected_masters) -> double
{
  double strongest_drive_cap_pf = -1.0;
  double resolved_unit_um = 0.0;

  for (const auto& cell_master : selected_masters) {
    const auto* info = LookupBufferInfo(infos, cell_master);
    if (info == nullptr) {
      continue;
    }

    double drive_cap_pf = 0.0;
    if (info->port_cap_limit_pf.has_value() && info->port_cap_limit_pf.value() > 0.0) {
      drive_cap_pf = info->port_cap_limit_pf.value();
    } else if (info->table_cap_limit_pf.has_value() && info->table_cap_limit_pf.value() > 0.0) {
      drive_cap_pf = info->table_cap_limit_pf.value();
    } else {
      continue;
    }

    const auto cell_height_um = CTSDM.getWrapper().queryCellHeightUm(cell_master);
    if (!cell_height_um.has_value() || cell_height_um.value() <= 0.0) {
      continue;
    }

    if (drive_cap_pf > strongest_drive_cap_pf) {
      strongest_drive_cap_pf = drive_cap_pf;
      resolved_unit_um = cell_height_um.value() * 10.0;
    }
  }

  return resolved_unit_um;
}

}  // namespace icts_test::characterization::realtech
