// SPDX-License-Identifier: MulanPSL-2.0
//
// Run one iMJ provider in a fresh process.  The probe deliberately exports
// the design immediately before and after the stage instead of introducing a
// third database representation for diagnostics.  The paired Python runner
// parses those DEFs into name-preserving semantic snapshots.

#include <any>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "MJInterface.hpp"
#include "MJInterfaceEccdb.h"
#include "AntennaIdb.h"
#include "context/DatabaseContext.h"
#include "database/Database.h"
#include "design/DesignStore.h"
#include "geometry/GeometryEntity.h"
#include "idm.h"
#include "json.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;
using ConfigMap = std::map<std::string, std::any>;

struct InputSpec
{
  std::vector<std::string> lef_files;
  std::string def_file;
};

struct Options
{
  std::string backend;
  std::string stage;
  std::filesystem::path manifest_file;
  std::filesystem::path output_directory;
  std::filesystem::path options_file;
  std::optional<std::filesystem::path> def_override;
};

[[nodiscard]] Json readJson(const std::filesystem::path& file)
{
  std::ifstream input(file);
  if (!input) throw std::runtime_error("cannot open " + file.string());
  return Json::parse(input);
}

void writeJson(const std::filesystem::path& file, const Json& value)
{
  std::ofstream output(file);
  if (!output) throw std::runtime_error("cannot write " + file.string());
  output << value.dump(2) << '\n';
  if (!output) throw std::runtime_error("failed while writing " + file.string());
}

[[nodiscard]] std::filesystem::path resolvePath(const std::filesystem::path& base, const std::string& text)
{
  const std::filesystem::path path(text);
  return std::filesystem::absolute(path.is_absolute() ? path : base / path).lexically_normal();
}

[[nodiscard]] InputSpec readInputSpec(const std::filesystem::path& manifest_file,
                                      const std::optional<std::filesystem::path>& def_override)
{
  const Json manifest = readJson(manifest_file);
  InputSpec result;
  const auto base = manifest_file.parent_path();
  for (const auto& lef : manifest.at("lef_files")) {
    result.lef_files.push_back(resolvePath(base, lef.get<std::string>()).string());
  }
  if (result.lef_files.empty()) throw std::invalid_argument("manifest lef_files must not be empty");
  result.def_file = def_override ? std::filesystem::absolute(*def_override).lexically_normal().string()
                                 : resolvePath(base, manifest.at("def_file").get<std::string>()).string();
  return result;
}

[[nodiscard]] Options parseOptions(int argc, char** argv)
{
  Options result;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto value = [&]() -> std::string_view {
      if (++index >= argc) throw std::invalid_argument("missing value for " + std::string(argument));
      return argv[index];
    };
    if (argument == "--backend") result.backend = value();
    else if (argument == "--stage") result.stage = value();
    else if (argument == "--manifest") result.manifest_file = value();
    else if (argument == "--out") result.output_directory = value();
    else if (argument == "--options") result.options_file = value();
    else if (argument == "--def-file") result.def_override = std::filesystem::path(value());
    else if (argument == "--help" || argument == "-h") {
      std::cout << "usage: imj_pair_probe --backend <idb|eccdb> --stage <filler|metal|antenna> "
                   "--manifest <manifest.json> --out <directory> --options <flat-options.json> [--def-file <input.def>]\n";
      std::exit(0);
    } else {
      throw std::invalid_argument("unknown option: " + std::string(argument));
    }
  }
  if (result.backend != "idb" && result.backend != "eccdb") throw std::invalid_argument("--backend must be idb or eccdb");
  if (result.stage != "filler" && result.stage != "metal" && result.stage != "antenna") {
    throw std::invalid_argument("--stage must be filler, metal, or antenna");
  }
  if (result.manifest_file.empty() || result.output_directory.empty() || result.options_file.empty()) {
    throw std::invalid_argument("--manifest, --out, and --options are required");
  }
  result.manifest_file = std::filesystem::absolute(result.manifest_file).lexically_normal();
  result.output_directory = std::filesystem::absolute(result.output_directory).lexically_normal();
  result.options_file = std::filesystem::absolute(result.options_file).lexically_normal();
  if (result.def_override) *result.def_override = std::filesystem::absolute(*result.def_override).lexically_normal();
  return result;
}

[[nodiscard]] ConfigMap configMap(const Json& source)
{
  if (!source.is_object()) throw std::invalid_argument("iMJ options JSON must be one flat object");
  ConfigMap result;
  for (const auto& [unprefixed_key, value] : source.items()) {
    const std::string key = unprefixed_key.starts_with('-') ? unprefixed_key : "-" + unprefixed_key;
    if (value.is_string()) result.emplace(key, value.get<std::string>());
    else if (value.is_boolean()) result.emplace(key, value.get<bool>());
    else if (value.is_number_integer()) {
      const auto raw = value.get<int64_t>();
      if (raw < std::numeric_limits<int32_t>::min() || raw > std::numeric_limits<int32_t>::max()) {
        throw std::invalid_argument("iMJ integer option is outside int32 range: " + key);
      }
      result.emplace(key, static_cast<int32_t>(raw));
    } else if (value.is_number_float()) result.emplace(key, value.get<double>());
    else throw std::invalid_argument("iMJ option must be a scalar: " + key);
  }
  return result;
}

[[nodiscard]] Json memoryUsage()
{
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  std::ifstream statm("/proc/self/statm");
  uint64_t total_pages = 0;
  uint64_t resident_pages = 0;
  statm >> total_pages >> resident_pages;
  return {{"rss_kib", resident_pages * static_cast<uint64_t>(sysconf(_SC_PAGESIZE)) / 1024},
          {"peak_rss_kib", usage.ru_maxrss}};
}

[[nodiscard]] Json eccdbSummary(const eccdb::Database& database)
{
  const auto& context = database.context();
  const auto& design = context.design();
  const auto& nets = design.netlistStorage();
  const auto& global = design.globalStorage();
  const auto shape_count = design.geometryRegistry().registry().view<const eccdb::GeometryShape>().size();
  return Json{{"backend", "eccdb"},
              {"design", global.info().name},
              {"revision", context.revision()},
              {"instances", nets.instanceCount()},
              {"instance_terms", nets.instancePinCount()},
              {"block_terms", nets.ioPinCount()},
              {"block_pins", nets.blockPinCount()},
              {"nets", nets.netCount()},
              {"wires", design.routingStorage().wireCount()},
              {"design_shapes", shape_count}};
}

template <typename Function>
void timed(Json& report, const char* phase, Function&& function)
{
  const auto begin = Clock::now();
  function();
  report["timing_ms"][phase] = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

[[nodiscard]] Json antennaSummaryJson(const imj::NativeAntennaSummary& value)
{
  return {{"checked", value.checked},
          {"coverage_complete", value.coverage_complete},
          {"net_count", value.net_count},
          {"connected_net_count", value.connected_net_count},
          {"wire_count", value.wire_count},
          {"violation_count", value.violation_count},
          {"rule_layer_count", value.rule_layer_count},
          {"covered_rule_layer_count", value.covered_rule_layer_count},
          {"skipped_rule_layer_count", value.skipped_rule_layer_count},
          {"nets_without_gate", value.nets_without_gate},
          {"nets_without_terminal_antenna_info", value.nets_without_terminal_antenna_info},
          {"unsupported_oxide_model_count", value.unsupported_oxide_model_count},
          {"pins_missing_antenna_info", value.pins_missing_antenna_info},
          {"pins_with_gate_area", value.pins_with_gate_area},
          {"components_without_gate", value.components_without_gate},
          {"skipped_segments", value.skipped_segments},
          {"conductors_out_of_range", value.conductors_out_of_range},
          {"partial_areas_dropped", value.partial_areas_dropped},
          {"unsupported_polygon_count", value.unsupported_polygon_count},
          {"unsupported_route_shape_count", value.unsupported_route_shape_count},
          {"via_geometry_count", value.via_geometry_count},
          {"rule_file", value.rule_file},
          {"report_path", value.report_path},
          {"coverage_diagnostics", value.coverage_diagnostics},
          {"violations", value.violations}};
}

// `MJI.insert*` takes its stage configuration separately from `initMJ`.
// Keep an explicit helper so configuration cannot accidentally disappear into
// the global DataManager setup above.
imj::NativeAntennaSummary invokeLegacyStage(const std::string& stage, ConfigMap stage_options,
                                           const std::filesystem::path& output_directory)
{
  imj::NativeAntennaSummary antenna_summary;
  ConfigMap init_options{{"-temp_directory_path", (output_directory / "legacy-work").string()}};
  eccdb::adapters::bindMjSource(MJI, nullptr);
  MJI.initMJ(std::move(init_options));
  try {
    if (stage == "filler") MJI.insertFiller(std::move(stage_options));
    else if (stage == "metal") MJI.insertMetal(std::move(stage_options));
    else antenna_summary = imj::checkAntennaIdb(std::move(stage_options), output_directory / "legacy-work/antennaCheck.step.json");
  } catch (...) {
    try {
      MJI.destroyMJ();
    } catch (...) {
    }
    throw;
  }
  MJI.destroyMJ();
  return antenna_summary;
}

// This is the native iMJ lifecycle itself, without a second cross-tool flow
// owner.  Keep the configuration behavior aligned with the retired runner:
// native iMJ receives the same values during init and during the stage call.
void invokeEccdbStage(eccdb::DatabaseContext& context,
                      const std::string& stage,
                      ConfigMap stage_options,
                      const std::filesystem::path& output_directory)
{
  stage_options["-temp_directory_path"] = (output_directory / "native-work").string();
  bool session_started = false;
  eccdb::adapters::bindMjSource(MJI, &context);
  try {
    // initMJ can allocate singleton state before it reports a failure.
    // Mark it first so the error path releases a partial session as well.
    session_started = true;
    MJI.initMJ(stage_options);
    if (stage == "filler") MJI.insertFiller(stage_options);
    else if (stage == "metal") MJI.insertMetal(stage_options);
    else MJI.checkAntenna(stage_options);
    MJI.destroyMJ();
    session_started = false;
  } catch (...) {
    if (session_started) MJI.abortMJ();
    eccdb::adapters::bindMjSource(MJI, nullptr);
    throw;
  }
  eccdb::adapters::bindMjSource(MJI, nullptr);
}

}  // namespace

int main(int argc, char** argv)
{
  Json report{{"status", "running"}};
  const auto wall_begin = Clock::now();
  std::unique_ptr<eccdb::Database> native_database;
  std::filesystem::path output_directory;
  std::string phase = "arguments";
  try {
    setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
    const Options options = parseOptions(argc, argv);
    std::filesystem::create_directories(options.output_directory);
    output_directory = options.output_directory;
    const InputSpec input = readInputSpec(options.manifest_file, options.def_override);
    const Json stage_options = readJson(options.options_file);
    const ConfigMap legacy_options = configMap(stage_options);
    report["backend"] = options.backend;
    report["stage"] = options.stage;
    report["manifest"] = options.manifest_file.string();
    report["options_file"] = options.options_file.string();
    report["output_directory"] = options.output_directory.string();
    report["input"] = {{"def_file", input.def_file}, {"lef_files", input.lef_files}};

    phase = "import";
    if (options.backend == "eccdb") {
      timed(report, "import", [&] {
        eccdb::LefDefInput database_input;
        for (const auto& lef : input.lef_files) database_input.lef_files.emplace_back(lef);
        database_input.def_file = input.def_file;
        native_database = std::make_unique<eccdb::Database>(eccdb::Database::open({.input = std::move(database_input)}));
      });
      report["summary_input"] = eccdbSummary(*native_database);
    } else {
      timed(report, "import", [&] {
        if (!dmInst->readLef({input.lef_files.front()}, true)) throw std::runtime_error("iDB technology LEF import failed");
        if (input.lef_files.size() > 1u) {
          const std::vector<std::string> library_lefs(input.lef_files.begin() + 1, input.lef_files.end());
          if (!dmInst->readLef(library_lefs)) throw std::runtime_error("iDB library LEF import failed");
        }
        if (!dmInst->readDef(input.def_file)) throw std::runtime_error("iDB DEF import failed");
      });
    }

    phase = "before_export";
    const auto before_def = options.output_directory / "before.def";
    timed(report, "before_export", [&] {
      if (native_database) native_database->writeDef(before_def);
      else if (!dmInst->saveDef(before_def.string())) throw std::runtime_error("iDB before DEF export failed");
    });
    report["before_def"] = before_def.string();

    phase = "stage";
    imj::NativeAntennaSummary legacy_antenna_summary;
    if (native_database) {
      report["stage_result"] = {{"stage", options.stage}, {"backend", "eccdb"}, {"before", eccdbSummary(*native_database)}};
      timed(report, "stage", [&] { invokeEccdbStage(native_database->context(), options.stage, configMap(stage_options), options.output_directory); });
      report["summary_output"] = eccdbSummary(*native_database);
      report["stage_result"]["after"] = report["summary_output"];
      report["stage_result"]["status"] = "passed";
    } else {
      timed(report, "stage", [&] { legacy_antenna_summary = invokeLegacyStage(options.stage, legacy_options, options.output_directory); });
    }
    if (options.stage == "antenna") {
      report["antenna"] = antennaSummaryJson(native_database ? MJI.lastAntennaSummary() : legacy_antenna_summary);
    }

    phase = "after_export";
    const auto output_def = options.output_directory / "output.def";
    timed(report, "after_export", [&] {
      if (native_database) native_database->writeDef(output_def);
      else if (!dmInst->saveDef(output_def.string())) throw std::runtime_error("iDB output DEF export failed");
    });
    report["output_def"] = output_def.string();
    report["status"] = "passed";
  } catch (const std::exception& error) {
    report["status"] = "failed";
    report["failed_phase"] = phase;
    report["error"] = error.what();
    eccdb::adapters::bindMjSource(MJI, nullptr);
  }
  report["memory"] = memoryUsage();
  report["wall_ms"] = std::chrono::duration<double, std::milli>(Clock::now() - wall_begin).count();
  try {
    if (output_directory.empty()) throw std::runtime_error("cannot write result.json before a valid --out is parsed");
    writeJson(output_directory / "result.json", report);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << report.dump(2) << '\n';
  return report.at("status") == "passed" ? 0 : 1;
}
