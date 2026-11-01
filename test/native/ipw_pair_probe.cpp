// SPDX-License-Identifier: MulanPSL-2.0
//
// Run one iPW provider in a fresh process and expose two diagnostic
// boundaries: the model consumed by iPW after wrapping, and the calculated
// power model before teardown.  Object IDs deliberately never leave this
// program.  The paired Python runner compares these name-keyed snapshots.

#include <algorithm>
#include <any>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

#include "DataManager.hpp"
#include "Lib.hh"
#include "PWInterface.hpp"
#include "adapters/ipw/PWInterfaceEccdb.h"
#include "database/Database.h"
#include "idm.h"
#include "json.hpp"
#include "spef/SpefParser.hh"
#include "VcdParser.hh"

namespace {

using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;

struct InputSpec
{
  std::vector<std::string> lef_files;
  std::vector<std::string> liberty_files;
  std::string def_file;
  std::string sdc_file;
  std::string spef_file;
  std::string vcd_file;
};

struct Options
{
  std::string backend;
  std::filesystem::path manifest_file;
  std::filesystem::path output_directory;
  std::optional<std::string> def_file;
  std::optional<std::string> spef_file;
  std::optional<std::string> vcd_file;
  int threads = 2;
  std::string seed = "20261022";
};

class Fnv1a64
{
 public:
  void text(std::string_view value)
  {
    number(static_cast<uint64_t>(value.size()));
    for (const unsigned char byte : value) {
      _value ^= byte;
      _value *= kPrime;
    }
  }

  template <typename Value>
  void number(Value value)
  {
    static_assert(std::is_integral_v<Value> || std::is_enum_v<Value>);
    uint64_t bits = static_cast<uint64_t>(value);
    for (unsigned shift = 0; shift != 64; shift += 8) {
      _value ^= (bits >> shift) & 0xffU;
      _value *= kPrime;
    }
  }

  void real(double value) { number(std::bit_cast<uint64_t>(value)); }

  [[nodiscard]] std::string hex() const
  {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << _value;
    return stream.str();
  }

 private:
  static constexpr uint64_t kOffset = 1469598103934665603ULL;
  static constexpr uint64_t kPrime = 1099511628211ULL;
  uint64_t _value = kOffset;
};

void addExpression(Fnv1a64& hash, ipw::LogicExpression& expression)
{
  auto& terms = expression.get_term_list();
  hash.number(static_cast<uint64_t>(terms.size()));
  for (auto& term : terms) {
    hash.number(term.get_operation_type());
    hash.text(term.get_port_name());
  }
}

void addTable(Fnv1a64& hash, const ipw::TimingTable& table)
{
  hash.number(table.get_variable_type1());
  const auto& axes = table.get_axis_list();
  hash.number(static_cast<uint64_t>(axes.size()));
  for (const auto& axis : axes) {
    hash.number(static_cast<uint64_t>(axis.size()));
    for (const double value : axis) hash.real(value);
  }
  const auto& values = table.get_value_list();
  hash.number(static_cast<uint64_t>(values.size()));
  for (const double value : values) hash.real(value);
}

void addTimingArc(Fnv1a64& hash, ipw::TimingArc& arc)
{
  hash.number(arc.get_sense());
  hash.number(arc.get_trigger_trans_type());
  hash.text(arc.get_library_name());
  hash.real(arc.get_time_unit_scale());
  hash.real(arc.get_cap_unit_scale());
  hash.real(arc.get_slew_derate());
  hash.real(arc.get_slew_lower_threshold_pct_rise());
  hash.real(arc.get_slew_upper_threshold_pct_rise());
  hash.real(arc.get_slew_lower_threshold_pct_fall());
  hash.real(arc.get_slew_upper_threshold_pct_fall());
  hash.real(arc.get_input_threshold_pct_rise());
  hash.real(arc.get_output_threshold_pct_rise());
  hash.real(arc.get_input_threshold_pct_fall());
  hash.real(arc.get_output_threshold_pct_fall());
  const auto append_tables = [&hash](auto& tables) {
    hash.number(static_cast<uint64_t>(tables.size()));
    for (auto& [edge, table] : tables) {
      hash.number(edge);
      addTable(hash, table);
    }
  };
  append_tables(arc.get_delay_table_map());
  append_tables(arc.get_slew_table_map());
}

struct CellDigest
{
  std::string fingerprint;
  uint64_t table_count = 0;
  uint64_t table_value_count = 0;
};

CellDigest addTimingCell(Fnv1a64& hash, const std::string& name, ipw::TimingCell& cell)
{
  CellDigest result;
  const auto begin = hash.hex();
  hash.text(name);
  hash.text(cell.get_library_name());
  hash.real(cell.get_nom_voltage());
  hash.real(cell.get_cell_leakage_power());
  hash.text(cell.get_clock_port_name());
  hash.text(cell.get_data_port_name());
  hash.number(cell.get_is_sequential());
  hash.number(cell.get_is_sequential_for_power());
  hash.number(cell.get_is_clock_gating());
  hash.number(cell.get_is_macro());
  hash.real(cell.get_slew_lower_threshold_pct_rise());
  hash.real(cell.get_slew_upper_threshold_pct_rise());
  hash.real(cell.get_slew_lower_threshold_pct_fall());
  hash.real(cell.get_slew_upper_threshold_pct_fall());
  hash.real(cell.get_input_threshold_pct_rise());
  hash.real(cell.get_output_threshold_pct_rise());
  hash.real(cell.get_input_threshold_pct_fall());
  hash.real(cell.get_output_threshold_pct_fall());

  auto& ports = cell.get_port_map();
  hash.number(static_cast<uint64_t>(ports.size()));
  for (auto& [port_name, port] : ports) {
    hash.text(port_name);
    hash.real(port.get_capacitance());
    hash.number(port.get_is_input());
    hash.number(port.get_is_output());
    hash.number(port.get_is_clock());
    addExpression(hash, port.get_function_expression());
    auto& trans_capacitance = port.get_trans_capacitance_map();
    hash.number(static_cast<uint64_t>(trans_capacitance.size()));
    for (const auto& [analysis, edges] : trans_capacitance) {
      hash.number(analysis);
      hash.number(static_cast<uint64_t>(edges.size()));
      for (const auto& [edge, capacitance] : edges) {
        hash.number(edge);
        hash.real(capacitance);
      }
    }
  }

  auto& cell_arcs = cell.get_cell_arc_list();
  hash.number(static_cast<uint64_t>(cell_arcs.size()));
  for (auto& cell_arc : cell_arcs) {
    hash.text(cell_arc.get_source_port());
    hash.text(cell_arc.get_sink_port());
    hash.number(cell_arc.get_is_timing_graph_arc());
    hash.number(cell_arc.get_is_clock_arc());
    hash.number(cell_arc.get_is_disable_arc());
    auto& arcs = cell_arc.get_timing_arc_list();
    hash.number(static_cast<uint64_t>(arcs.size()));
    for (auto& arc : arcs) {
      addTimingArc(hash, arc);
      for (const auto& [_, table] : arc.get_delay_table_map()) {
        ++result.table_count;
        result.table_value_count += table.get_value_list().size();
      }
      for (const auto& [_, table] : arc.get_slew_table_map()) {
        ++result.table_count;
        result.table_value_count += table.get_value_list().size();
      }
    }
  }

  auto& power_arcs = cell.get_power_arc_list();
  hash.number(static_cast<uint64_t>(power_arcs.size()));
  for (auto& arc : power_arcs) {
    hash.text(arc.get_source_port());
    hash.text(arc.get_sink_port());
    hash.text(arc.get_related_pg_port());
    hash.number(arc.get_source_sense());
    hash.number(arc.get_source_transition());
    hash.number(arc.get_sink_transition());
    hash.real(arc.get_time_unit_scale());
    hash.real(arc.get_cap_unit_scale());
    addExpression(hash, arc.get_when_expression());
    auto& tables = arc.get_energy_table_map();
    hash.number(static_cast<uint64_t>(tables.size()));
    for (const auto& [edge, table] : tables) {
      hash.number(edge);
      addTable(hash, table);
      ++result.table_count;
      result.table_value_count += table.get_value_list().size();
    }
  }

  auto& leakage = cell.get_leakage_power_list();
  hash.number(static_cast<uint64_t>(leakage.size()));
  for (auto& item : leakage) {
    hash.text(item.get_related_pg_port());
    hash.real(item.get_leakage_power());
    addExpression(hash, item.get_when_expression());
  }

  auto& sequential = cell.get_sequentials();
  hash.number(static_cast<uint64_t>(sequential.size()));
  for (auto& item : sequential) {
    hash.text(item.get_state_port());
    hash.text(item.get_inverted_state_port());
    hash.number(item.get_is_latch());
    addExpression(hash, item.get_data());
    addExpression(hash, item.get_clock());
    addExpression(hash, item.get_clear());
    addExpression(hash, item.get_preset());
  }
  // The per-cell digest must not use the aggregate state, so derive it from a
  // second compact canonical stream.  The aggregate digest still covers every
  // field above; this short diagnostic is used to localize a mismatch.
  Fnv1a64 cell_hash;
  cell_hash.text(begin);
  cell_hash.text(name);
  cell_hash.text(cell.get_library_name());
  cell_hash.number(static_cast<uint64_t>(ports.size()));
  cell_hash.number(static_cast<uint64_t>(cell_arcs.size()));
  cell_hash.number(static_cast<uint64_t>(power_arcs.size()));
  cell_hash.number(static_cast<uint64_t>(leakage.size()));
  cell_hash.number(static_cast<uint64_t>(sequential.size()));
  cell_hash.number(result.table_count);
  cell_hash.number(result.table_value_count);
  result.fingerprint = cell_hash.hex();
  return result;
}

Json powerValueJson(ipw::PowerValue& value)
{
  return {{"internal_w", value.get_internal_power()}, {"switching_w", value.get_switching_power()},
          {"leakage_w", value.get_leakage_power()}, {"total_w", value.get_total_power()}};
}

Json activityJson(const ipw::PowerActivity& activity)
{
  return {{"rise_density", activity.get_rise_transition_density()}, {"fall_density", activity.get_fall_transition_density()},
          {"static_probability", activity.get_static_probability()}, {"origin", static_cast<int>(activity.get_origin())},
          {"valid", activity.get_is_valid()}};
}

Json timingLibraryJson(ipw::TimingLibrary& library)
{
  const auto& leakage_power_unit = library.get_leakage_power_unit();
  Json result{{"leakage_power_unit", leakage_power_unit ? Json(*leakage_power_unit) : Json(nullptr)},
              {"nom_voltage", library.get_nom_voltage()},
              {"slew_lower_threshold_pct_rise", library.get_slew_lower_threshold_pct_rise()},
              {"slew_upper_threshold_pct_rise", library.get_slew_upper_threshold_pct_rise()},
              {"slew_lower_threshold_pct_fall", library.get_slew_lower_threshold_pct_fall()},
              {"slew_upper_threshold_pct_fall", library.get_slew_upper_threshold_pct_fall()},
              {"input_threshold_pct_rise", library.get_input_threshold_pct_rise()},
              {"output_threshold_pct_rise", library.get_output_threshold_pct_rise()},
              {"input_threshold_pct_fall", library.get_input_threshold_pct_fall()},
              {"output_threshold_pct_fall", library.get_output_threshold_pct_fall()},
              {"slew_derate_from_library", library.get_slew_derate_from_library()},
              {"cells", Json::object()}};
  Fnv1a64 hash;
  auto& cells = library.get_cell_map();
  hash.number(static_cast<uint64_t>(cells.size()));
  uint64_t tables = 0;
  uint64_t table_values = 0;
  for (auto& [name, cell] : cells) {
    hash.text(name);
    const auto digest = addTimingCell(hash, name, cell);
    tables += digest.table_count;
    table_values += digest.table_value_count;
    result["cells"][name] = {{"library", cell.get_library_name()},
                             {"ports", cell.get_port_map().size()},
                             {"timing_arcs", cell.get_cell_arc_list().size()},
                             {"power_arcs", cell.get_power_arc_list().size()},
                             {"leakage_entries", cell.get_leakage_power_list().size()},
                             {"sequential_entries", cell.get_sequentials().size()},
                             {"table_count", digest.table_count},
                             {"table_value_count", digest.table_value_count},
                             {"semantic_fingerprint_fnv1a64", digest.fingerprint}};
  }
  result["cell_count"] = cells.size();
  result["table_count"] = tables;
  result["table_value_count"] = table_values;
  result["semantic_fingerprint_fnv1a64"] = hash.hex();
  result["all_table_payloads_hashed"] = true;
  return result;
}

Json timingConstraintsJson(ipw::TimingConstraint& constraints)
{
  Json result{{"clocks", Json::object()}, {"ports", Json::object()}, {"case_analysis", Json::object()},
              {"effective_case_analysis", Json::object()}};
  for (auto& [name, clock] : constraints.get_clock_map()) {
    Json transitions = Json::array();
    for (const auto& [analysis, edges] : clock.get_transition_map()) {
      for (const auto& [edge, value] : edges) transitions.push_back({{"analysis", static_cast<int>(analysis)}, {"edge", static_cast<int>(edge)}, {"value", value}});
    }
    result["clocks"][name] = {{"sources", clock.get_source_list()}, {"period", clock.get_period()}, {"rise_edge", clock.get_rise_edge()},
                              {"fall_edge", clock.get_fall_edge()}, {"propagated", clock.get_is_propagated()},
                              {"master_clock", clock.get_master_clock_name()}, {"master_source", clock.get_master_source()},
                              {"generated_combinational", clock.get_is_generated_combinational()}, {"transitions", transitions}};
  }
  constexpr ipw::AnalysisType analyses[] = {ipw::AnalysisType::kNone, ipw::AnalysisType::kMax, ipw::AnalysisType::kMin};
  constexpr ipw::TransType edges[] = {ipw::TransType::kNone, ipw::TransType::kRise, ipw::TransType::kFall};
  for (auto& [name, port] : constraints.get_port_constraint_map()) {
    Json values = Json::array();
    for (const auto analysis : analyses) {
      for (const auto edge : edges) {
        Json entry{{"analysis", static_cast<int>(analysis)}, {"edge", static_cast<int>(edge)},
                   {"has_input_transition", port.get_has_input_transition(analysis, edge)},
                   {"input_transition", port.get_input_transition(analysis, edge)},
                   {"has_driving_cell", port.get_has_driving_cell(analysis, edge)},
                   {"load", port.get_load(analysis, edge)}};
        if (auto* cell = port.get_driving_cell(analysis, edge); cell != nullptr) {
          entry["driving_cell"] = {{"library", cell->get_library_name()}, {"cell", cell->get_cell_name()},
                                   {"from", cell->get_from_pin()}, {"to", cell->get_to_pin()},
                                   {"input_rise", cell->get_input_transition_rise()}, {"input_fall", cell->get_input_transition_fall()}};
        }
        values.push_back(std::move(entry));
      }
    }
    result["ports"][name] = std::move(values);
  }
  for (const auto& [name, value] : constraints.get_case_analysis_map()) result["case_analysis"][name] = static_cast<int>(value);
  for (const auto& [name, value] : constraints.get_effective_case_analysis_map()) result["effective_case_analysis"][name] = static_cast<int>(value);
  return result;
}

Json parasiticsJson(ipw::Database& database)
{
  Json result = Json::object();
  for (auto& [name, net] : database.get_parasitic_net_map()) {
    Fnv1a64 hash;
    hash.text(net.get_net_name());
    for (const auto& [node, cap] : net.get_node_capacitance_map()) {
      hash.text(node);
      hash.real(cap);
    }
    for (auto& resistor : net.get_resistor_list()) {
      hash.text(resistor.get_source_node());
      hash.text(resistor.get_sink_node());
      hash.real(resistor.get_resistance());
    }
    result[name] = {{"stored_name", net.get_net_name()}, {"node_count", net.get_node_capacitance_map().size()},
                    {"resistor_count", net.get_resistor_list().size()}, {"semantic_fingerprint_fnv1a64", hash.hex()}};
  }
  return result;
}

Json inputModelJson(ipw::Database& database)
{
  Json result{{"snapshot_schema", 1}, {"design_name", database.get_design_name()}, {"instances", Json::object()},
              {"pins", Json::object()}, {"nets", Json::object()}, {"vcd_activity", Json::object()}};
  for (auto& [name, instance] : database.get_instance_map()) {
    result["instances"][name] = {{"cell", instance.get_cell_name()}, {"pins", instance.get_pin_name_list()},
                                  {"clock_pin", instance.get_clock_pin_name()}, {"output_pin", instance.get_output_pin_name()},
                                  {"data_pin", instance.get_data_pin_name()}, {"sequential", instance.get_is_sequential()},
                                  {"clock_gating", instance.get_is_clock_gating()}, {"io_cell", instance.get_is_io_cell()}};
  }
  for (auto& [name, pin] : database.get_pin_map()) {
    result["pins"][name] = {{"pin", pin.get_pin_name()}, {"instance", pin.get_instance_name()}, {"net", pin.get_net_name()},
                             {"direction", static_cast<int>(pin.get_direction())}, {"is_port", pin.get_is_port()}};
  }
  for (auto& [name, net] : database.get_net_map()) {
    result["nets"][name] = {{"pins", net.get_pin_name_list()}, {"drivers", net.get_driver_pin_list()}, {"loads", net.get_load_pin_list()}};
  }
  for (auto& [name, pin] : database.get_pin_map()) {
    if (const auto* activity = database.findVcdActivity(static_cast<uint32_t>(pin.get_index())); activity != nullptr) {
      result["vcd_activity"][name] = activityJson(*activity);
    }
  }
  result["timing_library"] = timingLibraryJson(database.get_timing_library());
  result["timing_constraints"] = timingConstraintsJson(database.get_timing_constraint());
  result["parasitics"] = parasiticsJson(database);
  result["counts"] = {{"instances", database.get_instance_map().size()}, {"pins", database.get_pin_map().size()},
                      {"nets", database.get_net_map().size()}, {"vcd_activity", result["vcd_activity"].size()},
                      {"parasitic_nets", database.get_parasitic_net_map().size()}};
  return result;
}

Json calculatedPowerJson(ipw::Database& database)
{
  Json result{{"instance_power", Json::object()}, {"net_power", Json::object()}, {"power_activity", Json::object()},
              {"summary_groups", Json::object()}};
  auto& instance_power_list = database.get_instance_power_list();
  for (auto& [name, instance] : database.get_instance_map()) {
    auto& power = instance_power_list[instance.get_instance_id()];
    result["instance_power"][name] = {{"group", static_cast<int>(power.get_power_group_type())}, {"voltage_v", power.get_voltage()},
                                       {"power", powerValueJson(power.get_power_value())}};
  }
  auto& net_power_list = database.get_net_power_list();
  auto& net_power_written = database.get_net_power_written();
  for (auto& [name, net] : database.get_net_map()) {
    const auto net_index = net.get_index();
    if (net_power_written[net_index] == 0) {
      continue;
    }
    const auto& power = net_power_list[net_index];
    result["net_power"][name] = {{"voltage_v", power.get_voltage()}, {"net_load", power.get_total_net_load()},
                                  {"static_probability", power.get_static_probability()}, {"transition_density", power.get_transition_density()},
                                  {"switching_w", power.get_switching_power()}, {"activity_origin", static_cast<int>(power.get_activity_origin())},
                                  {"activity_valid", power.get_is_activity_valid()}};
  }
  for (auto& [name, pin] : database.get_pin_map()) {
    if (const auto* activity = database.findPowerActivity(static_cast<uint32_t>(pin.get_index())); activity != nullptr) {
      result["power_activity"][name] = activityJson(*activity);
    }
  }
  auto& summary = database.get_power_summary();
  result["summary_total"] = powerValueJson(summary.get_total_power_value());
  for (auto& [group, value] : summary.get_group_power_map()) result["summary_groups"][std::to_string(static_cast<int>(group))] = powerValueJson(value);
  result["counts"] = {{"instance_power", result["instance_power"].size()}, {"net_power", result["net_power"].size()},
                      {"power_activity", result["power_activity"].size()}};
  return result;
}

Json memoryJson()
{
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  std::ifstream statm("/proc/self/statm");
  uint64_t pages = 0;
  uint64_t resident = 0;
  statm >> pages >> resident;
  return {{"rss_kib", resident * static_cast<uint64_t>(sysconf(_SC_PAGESIZE)) / 1024}, {"peak_rss_kib", usage.ru_maxrss}};
}

void writeJson(const std::filesystem::path& file, const Json& value)
{
  std::ofstream output(file);
  if (!output) throw std::runtime_error("cannot write " + file.string());
  output << value.dump(2) << '\n';
  if (!output) throw std::runtime_error("failed while writing " + file.string());
}

Json readJson(const std::filesystem::path& file)
{
  std::ifstream input(file);
  if (!input) throw std::runtime_error("cannot open " + file.string());
  return Json::parse(input);
}

std::filesystem::path resolvePath(const std::filesystem::path& base, const std::string& value)
{
  const std::filesystem::path path(value);
  return (path.is_absolute() ? path : base / path).lexically_normal();
}

std::vector<std::string> stringList(const Json& object, const char* key, const std::filesystem::path& base)
{
  std::vector<std::string> result;
  if (!object.contains(key)) return result;
  for (const auto& value : object.at(key)) result.push_back(resolvePath(base, value.get<std::string>()).string());
  return result;
}

InputSpec readInput(const Options& options)
{
  const Json manifest = readJson(options.manifest_file);
  const auto base = options.manifest_file.parent_path();
  InputSpec input;
  input.lef_files = stringList(manifest, "lef_files", base);
  input.liberty_files = stringList(manifest, "liberty_files", base);
  if (input.lef_files.empty() || input.liberty_files.empty()) throw std::invalid_argument("manifest requires non-empty lef_files and liberty_files");
  input.def_file = resolvePath(base, options.def_file.value_or(manifest.at("def_file").get<std::string>())).string();
  if (manifest.contains("sdc_file")) input.sdc_file = resolvePath(base, manifest.at("sdc_file").get<std::string>()).string();
  if (options.spef_file) input.spef_file = resolvePath(base, *options.spef_file).string();
  else if (manifest.contains("spef_file")) input.spef_file = resolvePath(base, manifest.at("spef_file").get<std::string>()).string();
  if (options.vcd_file) input.vcd_file = resolvePath(base, *options.vcd_file).string();
  else if (manifest.contains("vcd_file")) input.vcd_file = resolvePath(base, manifest.at("vcd_file").get<std::string>()).string();
  return input;
}

Options parseOptions(int argc, char** argv)
{
  if (argc < 4) {
    throw std::invalid_argument("usage: ipw_pair_probe <idb|eccdb> <manifest.json> <output-directory> [--def-file=PATH] [--spef-file=PATH] [--vcd-file=PATH] [--threads=N] [--seed=N]");
  }
  Options result{.backend = argv[1], .manifest_file = std::filesystem::absolute(argv[2]), .output_directory = std::filesystem::absolute(argv[3])};
  for (int index = 4; index < argc; ++index) {
    const std::string_view option(argv[index]);
    const auto parse = [&](std::string_view prefix) -> std::optional<std::string> {
      return option.starts_with(prefix) ? std::optional<std::string>(option.substr(prefix.size())) : std::nullopt;
    };
    if (const auto value = parse("--def-file=")) result.def_file = *value;
    else if (const auto value = parse("--spef-file=")) result.spef_file = *value;
    else if (const auto value = parse("--vcd-file=")) result.vcd_file = *value;
    else if (const auto value = parse("--threads=")) result.threads = std::stoi(*value);
    else if (const auto value = parse("--seed=")) result.seed = *value;
    else throw std::invalid_argument("unknown option: " + std::string(option));
  }
  if (result.backend != "idb" && result.backend != "eccdb") throw std::invalid_argument("backend must be idb or eccdb");
  if (result.threads <= 0) throw std::invalid_argument("threads must be positive");
  return result;
}

std::vector<std::unique_ptr<idb::LibLibrary>> loadNativeLibraries(const std::vector<std::string>& files)
{
  std::vector<std::unique_ptr<idb::LibLibrary>> result;
  result.reserve(files.size());
  for (const auto& file : files) {
    idb::Lib parser;
    auto reader = parser.loadLibertyWithCppParser(file.c_str());
    if (!reader.linkLib()) throw std::runtime_error("Liberty link failed: " + file);
    result.push_back(reader.get_library_builder()->takeLib());
  }
  return result;
}

template <typename Function>
void timed(Json& report, const char* phase, Function&& action)
{
  const auto begin = Clock::now();
  action();
  report["timing_ms"][phase] = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

}  // namespace

int main(int argc, char** argv)
{
  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
  Json report{{"status", "running"}, {"timing_ms", Json::object()}};
  bool session_started = false;
  try {
    const Options options = parseOptions(argc, argv);
    std::filesystem::create_directories(options.output_directory);
    setenv("IPW_POWER_SIM_SEED", options.seed.c_str(), 1);
    report["backend"] = options.backend;
    report["manifest"] = options.manifest_file.string();
    report["threads"] = options.threads;
    report["power_sim_seed"] = options.seed;
    const InputSpec input = readInput(options);
    report["input"] = {{"lef_files", input.lef_files}, {"liberty_files", input.liberty_files}, {"def_file", input.def_file},
                       {"sdc_file", input.sdc_file}, {"spef_file", input.spef_file}, {"vcd_file", input.vcd_file}};

    std::unique_ptr<eccdb::Database> native_database;
    std::vector<std::unique_ptr<idb::LibLibrary>> native_libraries;
    std::unique_ptr<spef::SpefReader> native_spef;
    std::unique_ptr<vcd::VcdReader> native_vcd;
    if (options.backend == "eccdb") {
      timed(report, "import", [&] {
        eccdb::LefDefInput database_input;
        for (const auto& lef : input.lef_files) database_input.lef_files.emplace_back(lef);
        database_input.def_file = input.def_file;
        native_database = std::make_unique<eccdb::Database>(eccdb::Database::open({.input = std::move(database_input)}));
      });
      timed(report, "liberty_load", [&] { native_libraries = loadNativeLibraries(input.liberty_files); });
      if (!input.spef_file.empty()) {
        timed(report, "spef_load", [&] {
          native_spef = std::make_unique<spef::SpefReader>();
          if (!native_spef->read(input.spef_file)) throw std::runtime_error("SPEF load failed: " + input.spef_file);
          native_spef->expandName();
        });
      }
      if (!input.vcd_file.empty()) {
        timed(report, "vcd_load", [&] {
          native_vcd = std::make_unique<vcd::VcdReader>();
          if (!native_vcd->read(input.vcd_file)) throw std::runtime_error("VCD load failed: " + input.vcd_file);
        });
      }
      ipw::NativePowerInputs power_inputs;
      for (const auto& library : native_libraries) power_inputs.liberty_libraries.push_back(library.get());
      power_inputs.sdc_path = input.sdc_file;
      power_inputs.spef_path = input.spef_file;
      power_inputs.spef_reader = native_spef.get();
      power_inputs.vcd_reader = native_vcd.get();
      eccdb::adapters::bindPwSource(PWI, &native_database->context(), std::move(power_inputs));
      native_database->writeDef(options.output_directory / "import.def");
    } else {
      timed(report, "import", [&] {
        if (!dmInst->readLef({input.lef_files.front()}, true)) throw std::runtime_error("iDB technology LEF import failed");
        if (input.lef_files.size() > 1u) {
          const std::vector<std::string> library_lefs(input.lef_files.begin() + 1, input.lef_files.end());
          if (!dmInst->readLef(library_lefs)) throw std::runtime_error("iDB library LEF import failed");
        }
        if (!dmInst->readDef(input.def_file)) throw std::runtime_error("iDB DEF import failed");
      });
      timed(report, "liberty_load", [&] {
        if (!dmInst->readLib(input.liberty_files)) throw std::runtime_error("iDB Liberty import failed");
      });
      dmInst->get_config().set_sdc_path(input.sdc_file);
      if (!input.spef_file.empty() && !dmInst->readSpef(input.spef_file)) throw std::runtime_error("iDB SPEF import failed");
      if (!input.vcd_file.empty() && !dmInst->readVcd(input.vcd_file)) throw std::runtime_error("iDB VCD import failed");
      eccdb::adapters::bindPwSource(PWI, nullptr);
      if (!dmInst->saveDef((options.output_directory / "import.def").string())) throw std::runtime_error("iDB DEF export failed");
    }

    std::map<std::string, std::any> config{{"-temp_directory_path", (options.output_directory / "work").string()},
                                           {"-thread_number", options.threads}};
    // abortPW() handles a partially initialized singleton.  Mark the session
    // before init so an exception from wrapping cannot leave shared state in
    // this process between diagnostic phases.
    session_started = true;
    timed(report, "init", [&] { PWI.initPW(config); });
    timed(report, "input_snapshot", [&] { writeJson(options.output_directory / "input-model.json", inputModelJson(PWDM.getDatabase())); });
    timed(report, "algorithm", [&] { PWI.runPW(); });
    timed(report, "calculated_snapshot", [&] { writeJson(options.output_directory / "calculated-power.json", calculatedPowerJson(PWDM.getDatabase())); });
    timed(report, "destroy", [&] { PWI.destroyPW(); });
    session_started = false;
    eccdb::adapters::bindPwSource(PWI, nullptr);
    report["status"] = "passed";
  } catch (const std::exception& error) {
    report["status"] = "failed";
    report["error"] = error.what();
    if (session_started) PWI.abortPW();
    eccdb::adapters::bindPwSource(PWI, nullptr);
  }
  report["memory"] = memoryJson();
  try {
    const auto output = argc >= 4 ? std::filesystem::absolute(argv[3]) / "result.json" : std::filesystem::path("result.json");
    std::filesystem::create_directories(output.parent_path());
    writeJson(output, report);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << report.dump(2) << '\n';
  return report.at("status") == "passed" ? 0 : 1;
}
