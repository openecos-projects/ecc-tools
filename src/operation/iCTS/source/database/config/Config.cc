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
 * @file Config.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-11
 * @brief Configuration parser for iCTS.
 */
#include "database/config/Config.hh"

#include <glog/logging.h>

#include <cctype>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "Log.hh"
#include "LogFormat.hh"
#include "json.hpp"
#include "logger/Schema.hh"

namespace icts {
namespace {

auto parse_bool(const nlohmann::json& value, bool default_value) -> std::optional<bool>
{
  if (value.is_boolean()) {
    return value.get<bool>();
  }
  if (value.is_number_integer()) {
    return value.get<int>() != 0;
  }
  if (value.is_string()) {
    auto str = value.get<std::string>();
    for (auto& character : str) {
      character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    if (str == "true" || str == "on" || str == "yes" || str == "1") {
      return true;
    }
    if (str == "false" || str == "off" || str == "no" || str == "0") {
      return false;
    }
    return std::nullopt;
  }
  if (value.is_null()) {
    return default_value;
  }
  return std::nullopt;
}

auto parse_double(const nlohmann::json& value, double default_value) -> double
{
  if (value.is_number_float() || value.is_number_integer()) {
    return value.get<double>();
  }
  if (value.is_string()) {
    try {
      return std::stod(value.get<std::string>());
    } catch (...) {
      return default_value;
    }
  }
  return default_value;
}

auto parse_unsigned(const nlohmann::json& value, unsigned default_value) -> unsigned
{
  constexpr auto unsigned_max = static_cast<unsigned long>(std::numeric_limits<unsigned>::max());

  if (value.is_number_integer()) {
    const auto parsed = value.get<long long>();
    if (parsed < 0) {
      return default_value;
    }
    const auto parsed_unsigned = static_cast<unsigned long long>(parsed);
    if (parsed_unsigned > unsigned_max) {
      return default_value;
    }
    return static_cast<unsigned>(parsed_unsigned);
  }
  if (value.is_number_float()) {
    const auto parsed = value.get<double>();
    if (parsed < 0.0 || parsed > static_cast<double>(std::numeric_limits<unsigned>::max())) {
      return default_value;
    }
    return static_cast<unsigned>(parsed);
  }
  if (value.is_string()) {
    try {
      const auto parsed = std::stoul(value.get<std::string>());
      if (parsed > unsigned_max) {
        return default_value;
      }
      return static_cast<unsigned>(parsed);
    } catch (...) {
      return default_value;
    }
  }
  return default_value;
}

auto parse_string(const nlohmann::json& value, const std::string& default_value) -> std::string
{
  if (value.is_string()) {
    return value.get<std::string>();
  }
  if (value.is_number_integer()) {
    return std::to_string(value.get<int>());
  }
  if (value.is_number_float()) {
    return std::to_string(value.get<double>());
  }
  if (value.is_boolean()) {
    return value.get<bool>() ? "true" : "false";
  }
  return default_value;
}

auto parse_unsigned_list(const nlohmann::json& value) -> std::vector<unsigned>
{
  std::vector<unsigned> result;
  if (!value.is_array()) {
    return result;
  }
  for (const auto& item : value) {
    result.push_back(parse_unsigned(item, 0));
  }
  return result;
}

auto parse_string_list(const nlohmann::json& value) -> std::vector<std::string>
{
  std::vector<std::string> result;
  if (!value.is_array()) {
    return result;
  }
  for (const auto& item : value) {
    result.push_back(parse_string(item, ""));
  }
  return result;
}

auto ApplyDoubleIfPresent(const nlohmann::json& json, const char* key, Config& config, double (Config::*getter)() const,
                          void (Config::*setter)(double)) -> void
{
  if (json.contains(key)) {
    (config.*setter)(parse_double(json.at(key), (config.*getter)()));
  }
}

auto ApplyUnsignedIfPresent(const nlohmann::json& json, const char* key, Config& config, unsigned (Config::*getter)() const,
                            void (Config::*setter)(unsigned)) -> void
{
  if (json.contains(key)) {
    (config.*setter)(parse_unsigned(json.at(key), (config.*getter)()));
  }
}

auto ApplyBoolIfPresent(const nlohmann::json& json, const char* key, bool default_value, Config& config, void (Config::*setter)(bool),
                        const std::string& json_file) -> bool
{
  if (json.contains(key)) {
    const auto parsed = parse_bool(json.at(key), default_value);
    if (!parsed.has_value()) {
      config.set_last_error("invalid boolean value for key \"" + std::string(key) + "\" in " + json_file + ": " + json.at(key).dump());
      LOG_ERROR << "CTS config parse failed: " << config.get_last_error();
      return false;
    }
    (config.*setter)(*parsed);
  }
  return true;
}

auto ApplyRoutingLayersIfPresent(const nlohmann::json& json, Config& config) -> void
{
  if (json.contains("routing_layer")) {
    auto routing_layers = parse_unsigned_list(json.at("routing_layer"));
    if (!routing_layers.empty()) {
      config.set_routing_layers(routing_layers);
    }
  }
}

auto ApplyBufferTypesIfPresent(const nlohmann::json& json, Config& config) -> void
{
  if (json.contains("buffer_type")) {
    config.set_buffer_types(parse_string_list(json.at("buffer_type")));
  }
}

}  // namespace

auto Config::init(const std::string& config_file) -> bool
{
  reset();
  return parse(config_file);
}

auto Config::parse(const std::string& json_file) -> bool
{
  std::ifstream ifs(json_file);
  if (!ifs) {
    set_last_error("failed to open iCTS config file: " + json_file);
    LOG_ERROR << get_last_error();
    return false;
  }

  nlohmann::json json;
  try {
    ifs >> json;
  } catch (const nlohmann::json::exception& error) {
    set_last_error("failed to parse iCTS config file " + json_file + ": " + std::string(error.what()));
    LOG_ERROR << get_last_error();
    return false;
  }

  ApplyDoubleIfPresent(json, "skew_bound", *this, &Config::get_skew_bound, &Config::set_skew_bound);
  ApplyDoubleIfPresent(json, "max_buf_tran", *this, &Config::get_max_buf_tran, &Config::set_max_buf_tran);
  ApplyDoubleIfPresent(json, "root_input_slew", *this, &Config::get_root_input_slew, &Config::set_root_input_slew);
  ApplyDoubleIfPresent(json, "max_sink_tran", *this, &Config::get_max_sink_tran, &Config::set_max_sink_tran);
  ApplyDoubleIfPresent(json, "max_cap", *this, &Config::get_max_cap, &Config::set_max_cap);
  // max_length remains parseable as a placeholder knob; active lattice comes
  // from wirelength_unit_um + wirelength_iterations.
  ApplyDoubleIfPresent(json, "max_length", *this, &Config::get_max_length, &Config::set_max_length);
  ApplyDoubleIfPresent(json, "wirelength_unit_um", *this, &Config::get_wirelength_unit_um, &Config::set_wirelength_unit_um);
  ApplyUnsignedIfPresent(json, "wirelength_iterations", *this, &Config::get_wirelength_iterations, &Config::set_wirelength_iterations);
  ApplyUnsignedIfPresent(json, "slew_steps", *this, &Config::get_slew_steps, &Config::set_slew_steps);
  ApplyUnsignedIfPresent(json, "cap_steps", *this, &Config::get_cap_steps, &Config::set_cap_steps);
  ApplyDoubleIfPresent(json, "wire_width", *this, &Config::get_wire_width, &Config::set_wire_width);
  ApplyUnsignedIfPresent(json, "max_fanout", *this, &Config::get_max_fanout, &Config::set_max_fanout);
  ApplyRoutingLayersIfPresent(json, *this);
  ApplyBufferTypesIfPresent(json, *this);
  ApplyDoubleIfPresent(json, "char_buf_redundancy_pct", *this, &Config::get_char_buf_redundancy_pct, &Config::set_char_buf_redundancy_pct);
  if (!ApplyBoolIfPresent(json, "force_branch_buffer", is_force_branch_buffer(), *this, &Config::set_force_branch_buffer, json_file)) {
    return false;
  }
  ApplyUnsignedIfPresent(json, "htree_depth_explore_window", *this, &Config::get_htree_depth_explore_window,
                         &Config::set_htree_depth_explore_window);
  ApplyDoubleIfPresent(json, "htree_topology_tolerance", *this, &Config::get_htree_topology_tolerance,
                       &Config::set_htree_topology_tolerance);
  if (!ApplyBoolIfPresent(json, "enable_analytical_htree", is_enable_analytical_htree(), *this, &Config::set_enable_analytical_htree,
                          json_file)) {
    return false;
  }
  if (!ApplyBoolIfPresent(json, "enable_sink_clustering", is_enable_sink_clustering(), *this, &Config::set_enable_sink_clustering,
                          json_file)) {
    return false;
  }
  return true;
}

auto Config::buildRuntimeConfigRows() const -> logformat::TableRows
{
  const auto& routing_layers = get_routing_layers();
  const auto& buffer_types = get_buffer_types();
  const bool has_wirelength_unit = get_wirelength_unit_um() > 0.0;
  const bool has_wire_width = get_wire_width() > 0.0;

  return {
      {"skew_bound", logformat::FormatWithUnit(get_skew_bound(), "ns"), "clock skew target"},
      {"max_buf_tran", has_max_buf_tran() ? logformat::FormatWithUnit(get_max_buf_tran(), "ns") : "auto",
       has_max_buf_tran() ? "explicit runtime config" : "resolve from liberty slew limit/table-axis during characterization"},
      {"root_input_slew", logformat::FormatWithUnit(get_root_input_slew(), "ns"),
       "root-driver Liberty input slew and source-trunk input slew boundary; 0.0 means ideal/no lower-bound filter"},
      {"max_sink_tran", logformat::FormatWithUnit(get_max_sink_tran(), "ns"), "sink transition target"},
      {"max_cap", has_max_cap() ? logformat::FormatWithUnit(get_max_cap(), "pF") : "auto",
       has_max_cap() ? "explicit runtime config" : "resolve from liberty cap limit/table-axis during characterization"},
      {"max_length", logformat::FormatWithUnit(get_max_length(), "um"), "legacy compatibility knob; not the active step lattice"},
      {"wirelength_unit", has_wirelength_unit ? logformat::FormatWithUnit(get_wirelength_unit_um(), "um") : "unconfigured",
       has_wirelength_unit ? "active characterization lattice unit" : "must be supplied by a caller-owned characterization plan"},
      {"wirelength_iterations", std::to_string(get_wirelength_iterations()), "characterization length bins"},
      {"slew_steps", std::to_string(get_slew_steps()), "characterization slew bins"},
      {"cap_steps", std::to_string(get_cap_steps()), "characterization load-cap bins"},
      {"wire_width", has_wire_width ? logformat::FormatWithUnit(get_wire_width(), "um") : "library_default",
       has_wire_width ? "explicit RC width override" : "use technology default width in RC query"},
      {"routing_layers", routing_layers.empty() ? "unconfigured" : std::to_string(routing_layers.front()),
       routing_layers.empty() ? "routing layer must be supplied before RC queries"
                              : "configured order: " + logformat::JoinUnsigned(routing_layers)},
      {"max_fanout", std::to_string(get_max_fanout()), "fanout constraint"},
      {"buffer_types", std::to_string(buffer_types.size()),
       buffer_types.empty() ? "no configured buffers" : logformat::JoinStrings(buffer_types)},
      {"char_buf_redundancy", logformat::FormatPercent(get_char_buf_redundancy_pct()),
       get_char_buf_redundancy_pct() > 0.0 ? "near-neighbor max-cap pruning threshold" : "disabled"},
      {"force_branch_buffer", logformat::FormatBool(is_force_branch_buffer()),
       is_force_branch_buffer() ? "require terminal-buffered segment frontiers on every H-tree level" : "disabled"},
      {"htree_depth_explore_window", std::to_string(get_htree_depth_explore_window()),
       "flow-level H-tree explores up to this many descending depth candidates from the deepest topology"},
      {"htree_topology_tolerance", logformat::FormatPercent(get_htree_topology_tolerance()),
       "per-level H-tree topology segment length deviation allowed around the baseline"},
      {"enable_analytical_htree", logformat::FormatBool(is_enable_analytical_htree()),
       is_enable_analytical_htree() ? "experimental analytical H-tree candidate selection is enabled"
                                    : "native discrete H-tree search is used"},
      {"enable_sink_clustering", logformat::FormatBool(is_enable_sink_clustering()),
       is_enable_sink_clustering() ? "run sink linear clustering before H-tree synthesis" : "build H-tree directly on original sinks"},
  };
}

auto Config::emitRuntimeConfigReport(SchemaWriter& reporter, const std::string& title) const -> void
{
  reporter.emitTable(title, {"Item", "Value", "Detail"}, buildRuntimeConfigRows());
}

}  // namespace icts
