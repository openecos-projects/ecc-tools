// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory

#include "EccdbCommands.h"

#include <algorithm>
#include <array>
#include <any>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <json.hpp>

#include "CTSAPI.hh"
#include "CtsEccdbAdapter.hh"
#include "DRCInterface.hpp"
#include "EMIRInterface.hpp"
#include "FPInterface.hpp"
#include "Lib.hh"
#include "LibParserCpp.hh"
#include "LVSInterface.hpp"
#include "MJInterface.hpp"
#include "NetlistData.hpp"
#include "PWInterface.hpp"
#include "RTInterface.hpp"
#include "STAInterface.hpp"
#include "SpefParser.hh"
#include "VcdParser.hh"
#include "adapters/idrc/DRCInterfaceEccdb.h"
#include "adapters/iemir/EMIRInterfaceEccdb.h"
#include "adapters/ifp/FPInterfaceEccdb.h"
#include "adapters/ilvs/LVSInterfaceEccdb.h"
#include "adapters/imj/MJInterfaceEccdb.h"
#include "adapters/ipw/PWInterfaceEccdb.h"
#include "adapters/irt/RTInterfaceEccdb.h"
#include "adapters/ista/STAInterfaceEccdb.h"
#include "context/DatabaseContext.h"
#include "database/Database.h"
#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "import/verilog/VerilogNetlistImportService.h"
#include "tech/TechStore.h"

namespace ecc::scripting {
namespace {

using Json = nlohmann::json;
using ConfigMap = std::map<std::string, std::any>;

// Operation interfaces own process-wide singleton state.  A scripting caller
// may own many Database objects, but they cannot run native stages concurrently
// until those interfaces become session based.
std::mutex g_native_stage_mutex;

[[nodiscard]] std::string lower(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

[[nodiscard]] Json readJsonFile(const std::filesystem::path& path, const std::string_view purpose)
{
  if (path.empty()) {
    return Json::object();
  }
  std::ifstream input(path);
  if (!input) {
    throw std::invalid_argument(std::string{purpose} + " cannot open JSON file: " + path.string());
  }
  try {
    Json value;
    input >> value;
    if (!value.is_object()) {
      throw std::invalid_argument(std::string{purpose} + " must be a JSON object: " + path.string());
    }
    return value;
  } catch (const Json::exception& error) {
    throw std::invalid_argument(std::string{purpose} + " contains invalid JSON (" + path.string() + "): " + error.what());
  }
}

[[nodiscard]] Json readOptions(const std::string& options_json)
{
  try {
    const Json value = Json::parse(options_json.empty() ? "{}" : options_json);
    if (!value.is_object()) {
      throw std::invalid_argument("EccDB tool options must be a JSON object");
    }
    return value;
  } catch (const Json::exception& error) {
    throw std::invalid_argument(std::string{"EccDB tool options contain invalid JSON: "} + error.what());
  }
}

[[nodiscard]] Json toolSection(const Json& root, const std::string_view tool)
{
  const std::array<std::string, 3> names = {std::string{tool}, lower(std::string{tool}), [&] {
                                                    auto upper = std::string{tool};
                                                    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
                                                      return static_cast<char>(std::toupper(c));
                                                    });
                                                    return upper;
                                                  }()};
  for (const auto& name : names) {
    const auto it = root.find(name);
    if (it != root.end()) {
      if (!it->is_object()) {
        throw std::invalid_argument("EccDB tool section '" + name + "' must be a JSON object");
      }
      return *it;
    }
  }
  return root;
}

[[nodiscard]] std::string undashed(std::string_view key)
{
  if (!key.empty() && key.front() == '-') {
    key.remove_prefix(1);
  }
  return std::string{key};
}

[[nodiscard]] bool isFrontendKey(std::string_view key)
{
  const auto plain = undashed(key);
  return plain == "liberty_files" || plain == "sdc_file" || plain == "spef_file" || plain == "vcd_file" || plain == "work_dir"
         || plain == "simple" || plain == "operation" || plain == "peak" || plain == "average" || plain == "reference_verilog"
         || plain == "top_module";
}

[[nodiscard]] bool isIntegerOption(std::string_view key)
{
  static const std::set<std::string, std::less<>> keys{
      "-thread_number", "-output_inter_result", "-output_timing_reports", "-output_timing_features", "-timing_path_limit",
      "-min_slew_degradation", "-max_paths", "-nworst", "-max_path", "-path_report_number", "-add_ploc_from_top_def",
      "-ir_solver_max_iterations"};
  return keys.contains(key);
}

[[nodiscard]] bool isDoubleOption(std::string_view key)
{
  static const std::set<std::string, std::less<>> keys{
      "-slack_lesser_than", "-slack_greater_than", "-temperature_c", "-em_violation_threshold_percent", "-ir_solver_tolerance"};
  return keys.contains(key);
}

[[nodiscard]] int32_t parseInt32(const std::string_view text, const std::string_view key)
{
  int32_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    throw std::invalid_argument("EccDB option '" + std::string{key} + "' must be a signed 32-bit integer");
  }
  return value;
}

[[nodiscard]] double parseDouble(const std::string& text, const std::string_view key)
{
  std::size_t end = 0;
  try {
    const double value = std::stod(text, &end);
    if (end != text.size() || !std::isfinite(value)) {
      throw std::invalid_argument("invalid numeric option");
    }
    return value;
  } catch (const std::exception&) {
    throw std::invalid_argument("EccDB option '" + std::string{key} + "' must be a finite number");
  }
}

[[nodiscard]] int32_t jsonInt32(const Json& value, const std::string_view key)
{
  if (value.is_string()) {
    return parseInt32(value.get<std::string>(), key);
  }
  if (value.is_boolean()) {
    return value.get<bool>() ? 1 : 0;
  }
  if (value.is_number_unsigned()) {
    const auto integer = value.get<uint64_t>();
    if (integer > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
      throw std::invalid_argument("EccDB option '" + std::string{key} + "' is outside int32 range");
    }
    return static_cast<int32_t>(integer);
  }
  if (value.is_number_integer()) {
    const auto integer = value.get<int64_t>();
    if (integer < std::numeric_limits<int32_t>::min() || integer > std::numeric_limits<int32_t>::max()) {
      throw std::invalid_argument("EccDB option '" + std::string{key} + "' is outside int32 range");
    }
    return static_cast<int32_t>(integer);
  }
  throw std::invalid_argument("EccDB option '" + std::string{key} + "' must be an integer");
}

[[nodiscard]] double jsonDouble(const Json& value, const std::string_view key)
{
  if (value.is_string()) {
    return parseDouble(value.get<std::string>(), key);
  }
  if (!value.is_number()) {
    throw std::invalid_argument("EccDB option '" + std::string{key} + "' must be a number");
  }
  const double result = value.get<double>();
  if (!std::isfinite(result)) {
    throw std::invalid_argument("EccDB option '" + std::string{key} + "' must be finite");
  }
  return result;
}

[[nodiscard]] std::string configKey(std::string key)
{
  if (key.empty()) {
    throw std::invalid_argument("EccDB tool configuration has an empty key");
  }
  if (key.front() != '-') {
    key.insert(key.begin(), '-');
  }
  return key;
}

[[nodiscard]] Json canonicalizeToolOptions(const Json& options, const std::string_view source)
{
  Json result = Json::object();
  for (auto it = options.begin(); it != options.end(); ++it) {
    const std::string key = isFrontendKey(it.key()) ? undashed(it.key()) : configKey(it.key());
    if (const auto existing = result.find(key); existing != result.end() && *existing != it.value()) {
      throw std::invalid_argument("EccDB " + std::string{source} + " contains conflicting aliases for option '" + key + "'");
    }
    result[key] = it.value();
  }
  return result;
}

[[nodiscard]] Json mergedToolOptions(const std::filesystem::path& config, const std::string_view tool, const std::string& options_json)
{
  const Json config_root = readJsonFile(config, "EccDB tool configuration");
  const Json options_root = readOptions(options_json);
  Json merged = canonicalizeToolOptions(toolSection(config_root, tool), "tool configuration");
  const Json overrides = canonicalizeToolOptions(toolSection(options_root, tool), "tool options");
  for (auto it = overrides.begin(); it != overrides.end(); ++it) {
    merged[it.key()] = it.value();
  }
  return merged;
}

[[nodiscard]] ConfigMap configMapFromJson(const Json& options)
{
  ConfigMap result;
  for (auto it = options.begin(); it != options.end(); ++it) {
    if (isFrontendKey(it.key())) {
      continue;
    }
    const auto key = configKey(it.key());
    const Json& value = it.value();
    if (value.is_null()) {
      continue;
    }
    if (isIntegerOption(key)) {
      result[key] = jsonInt32(value, key);
      continue;
    }
    if (isDoubleOption(key)) {
      result[key] = jsonDouble(value, key);
      continue;
    }
    if (value.is_string()) {
      result[key] = value.get<std::string>();
    } else if (value.is_boolean()) {
      result[key] = int32_t{value.get<bool>() ? 1 : 0};
    } else if (value.is_number_integer() || value.is_number_unsigned()) {
      result[key] = jsonInt32(value, key);
    } else if (value.is_number_float()) {
      result[key] = jsonDouble(value, key);
    } else if (value.is_array()) {
      if (value.empty()) {
        result[key] = std::vector<std::string>{};
      } else if (std::all_of(value.begin(), value.end(), [](const Json& item) { return item.is_string(); })) {
        result[key] = value.get<std::vector<std::string>>();
      } else if (std::all_of(value.begin(), value.end(), [](const Json& item) {
                   return item.is_boolean() || item.is_number_integer() || item.is_number_unsigned();
                 })) {
        std::vector<int32_t> values;
        values.reserve(value.size());
        for (const auto& item : value) values.push_back(jsonInt32(item, key));
        result[key] = std::move(values);
      } else if (std::all_of(value.begin(), value.end(), [](const Json& item) { return item.is_number(); })) {
        std::vector<double> values;
        values.reserve(value.size());
        for (const auto& item : value) values.push_back(jsonDouble(item, key));
        result[key] = std::move(values);
      } else {
        throw std::invalid_argument("EccDB option '" + key + "' has an unsupported mixed JSON array");
      }
    } else {
      throw std::invalid_argument("EccDB option '" + key + "' must be a scalar or homogeneous array");
    }
  }
  return result;
}

[[nodiscard]] const Json* findValue(const Json& options, const std::string_view name)
{
  if (const auto it = options.find(std::string{name}); it != options.end()) return &*it;
  const std::string dashed = "-" + std::string{name};
  if (const auto it = options.find(dashed); it != options.end()) return &*it;
  return nullptr;
}

[[nodiscard]] std::optional<std::string> optionalString(const Json& options, const std::string_view name)
{
  const Json* value = findValue(options, name);
  if (value == nullptr || value->is_null()) return std::nullopt;
  if (!value->is_string()) {
    throw std::invalid_argument("EccDB option '" + std::string{name} + "' must be a string");
  }
  return value->get<std::string>();
}

[[nodiscard]] std::string requiredString(const Json& options, const std::string_view name)
{
  const auto value = optionalString(options, name);
  if (!value || value->empty()) {
    throw std::invalid_argument("EccDB native stage requires non-empty option '" + std::string{name} + "'");
  }
  return *value;
}

[[nodiscard]] bool boolOption(const Json& options, const std::string_view name, const bool fallback = false)
{
  const Json* value = findValue(options, name);
  if (value == nullptr || value->is_null()) return fallback;
  if (value->is_boolean()) return value->get<bool>();
  if (value->is_number_integer() || value->is_number_unsigned()) return jsonInt32(*value, name) != 0;
  if (value->is_string()) {
    const auto value_text = lower(value->get<std::string>());
    if (value_text == "1" || value_text == "true" || value_text == "yes") return true;
    if (value_text == "0" || value_text == "false" || value_text == "no") return false;
  }
  throw std::invalid_argument("EccDB option '" + std::string{name} + "' must be a boolean");
}

[[nodiscard]] std::vector<std::string> stringList(const Json& options, const std::string_view name, const bool required = false)
{
  const Json* value = findValue(options, name);
  if (value == nullptr || value->is_null()) {
    if (required) throw std::invalid_argument("EccDB native stage requires option '" + std::string{name} + "'");
    return {};
  }
  if (value->is_string()) {
    const auto path = value->get<std::string>();
    if (path.empty() && required) throw std::invalid_argument("EccDB option '" + std::string{name} + "' must not be empty");
    return path.empty() ? std::vector<std::string>{} : std::vector<std::string>{path};
  }
  if (!value->is_array() || !std::all_of(value->begin(), value->end(), [](const Json& item) { return item.is_string(); })) {
    throw std::invalid_argument("EccDB option '" + std::string{name} + "' must be a string or string array");
  }
  const auto paths = value->get<std::vector<std::string>>();
  if (required && paths.empty()) throw std::invalid_argument("EccDB option '" + std::string{name} + "' must not be empty");
  return paths;
}

class LibertyInputs
{
 public:
  explicit LibertyInputs(const std::vector<std::string>& paths)
  {
    _libraries.reserve(paths.size());
    _raw.reserve(paths.size());
    for (const auto& path : paths) {
      if (path.empty()) throw std::invalid_argument("liberty_files contains an empty path");
      idb::LibertyReader reader(path.c_str());
      if (!reader.readLib() || !reader.linkLib()) {
        throw std::runtime_error("failed to parse or link Liberty file: " + path);
      }
      auto library = reader.takeLib();
      if (library == nullptr) {
        throw std::runtime_error("Liberty parser returned no library for: " + path);
      }
      _raw.push_back(library.get());
      _libraries.push_back(std::move(library));
    }
  }

  [[nodiscard]] const std::vector<idb::LibLibrary*>& raw() const noexcept { return _raw; }

  [[nodiscard]] idb::LibCell* findCell(const std::string& master_name) const noexcept
  {
    for (auto* library : _raw) {
      if (library == nullptr) continue;
      if (auto* cell = library->findCell(master_name.c_str()); cell != nullptr) return cell;
    }
    return nullptr;
  }

 private:
  std::vector<std::unique_ptr<idb::LibLibrary>> _libraries;
  std::vector<idb::LibLibrary*> _raw;
};

[[nodiscard]] std::unique_ptr<spef::SpefReader> loadSpef(const Json& options)
{
  const auto path = optionalString(options, "spef_file");
  if (!path || path->empty()) return {};
  auto reader = std::make_unique<spef::SpefReader>();
  if (!reader->read(*path)) {
    throw std::runtime_error("failed to read SPEF file: " + *path);
  }
  reader->expandName();
  return reader;
}

[[nodiscard]] std::unique_ptr<vcd::VcdReader> loadVcd(const Json& options)
{
  const auto path = optionalString(options, "vcd_file");
  if (!path || path->empty()) return {};
  auto reader = std::make_unique<vcd::VcdReader>();
  if (!reader->read(*path)) {
    throw std::runtime_error("failed to read VCD file: " + *path);
  }
  return reader;
}

template <typename Abort, typename Detach, typename Stage>
void runNativeStage(Abort&& abort, Detach&& detach, Stage&& stage)
{
  try {
    stage();
  } catch (...) {
    try {
      abort();
    } catch (...) {
    }
    try {
      detach();
    } catch (...) {
    }
    throw;
  }
  detach();
}

void runFp(eccdb::Database& database, const std::filesystem::path& config, const Json& options)
{
  if (config.empty()) {
    throw std::invalid_argument("fp requires a native iFP JSON configuration path");
  }
  if (!std::filesystem::is_regular_file(config)) {
    throw std::invalid_argument("fp configuration does not exist: " + config.string());
  }
  for (auto it = options.begin(); it != options.end(); ++it) {
    const auto key = undashed(it.key());
    if (key != "simple") {
      throw std::invalid_argument("fp configuration overrides are unsupported; update its iFP JSON file instead");
    }
  }
  auto& interface = FPI;
  runNativeStage([&] { interface.abortFP(); }, [&] { eccdb::adapters::bindFpSource(interface, nullptr); }, [&] {
    eccdb::adapters::bindFpSource(interface, &database.context());
    interface.initFP({{"-config", config.string()}});
    if (boolOption(options, "simple")) {
      interface.runSimpleFP();
    } else {
      interface.runFP();
    }
    interface.destroyFP();
  });
}

void runCts(eccdb::Database& database, const std::filesystem::path& config, const Json& options)
{
  if (config.empty() || !std::filesystem::is_regular_file(config)) {
    throw std::invalid_argument("cts requires an existing native iCTS JSON configuration path");
  }
  const auto liberty_paths = stringList(options, "liberty_files", true);
  const auto sdc_path = requiredString(options, "sdc_file");
  if (!std::filesystem::is_regular_file(sdc_path)) {
    throw std::invalid_argument("cts sdc_file does not exist: " + sdc_path);
  }
  LibertyInputs liberty{liberty_paths};
  auto source = std::make_shared<eccdb::adapter::icts::CtsEccdbAdapter>(
      database.context(), [&liberty](const std::string& master_name) { return liberty.findCell(master_name); }, sdc_path);
  auto& api = icts::CTSAPI::getInst();
  const auto work_dir = optionalString(options, "work_dir").value_or(config.parent_path().string());
  runNativeStage(
      [&] { (void) icts::CTSAPI::abortCTS(); },
      [&] {
        api.setDesignSource(nullptr);
      },
      [&] {
        api.setDesignSource(source);
        const auto initialized = icts::CTSAPI::init(config.string(), work_dir);
        if (!initialized.ok()) throw std::runtime_error("CTS initialization failed: " + initialized.message);
        const auto executed = icts::CTSAPI::runCTS();
        if (!executed.ok()) throw std::runtime_error("CTS execution failed: " + executed.message);
        const auto destroyed = icts::CTSAPI::destroyCTS();
        if (!destroyed.ok()) throw std::runtime_error("CTS writeback failed: " + destroyed.message);
      });
}

void runRt(eccdb::Database& database, const ConfigMap& config)
{
  auto& interface = RTI;
  runNativeStage(
      [&] { interface.abortRT(); },
      [&] { eccdb::adapters::bindRtSource(interface, static_cast<eccdb::DatabaseContext*>(nullptr)); },
      [&] {
        eccdb::adapters::bindRtSource(interface, &database.context());
        interface.initRT(config);
        interface.runRT();
        interface.destroyRT();
      });
}

void runDrc(eccdb::Database& database, const ConfigMap& config)
{
  auto& interface = DRCI;
  runNativeStage([&] { interface.abortDRC(); }, [&] { eccdb::adapters::bindDrcSource(interface, nullptr); }, [&] {
    eccdb::adapters::bindDrcSource(interface, &database.context());
    interface.initDRC(config, true);
    interface.runDRC();
    interface.destroyDRC();
  });
}

void runSta(eccdb::Database& database, const Json& options, const ConfigMap& config)
{
  LibertyInputs liberty{stringList(options, "liberty_files", true)};
  auto spef = loadSpef(options);
  auto& interface = STAI;
  runNativeStage(
      [&] { interface.abortSTA(); },
      [&] { eccdb::adapters::bindStaSource(interface, nullptr); },
      [&] {
        eccdb::adapters::bindStaSource(interface, &database.context(),
                                  {.liberty_libraries = liberty.raw(),
                                   .spef_reader = spef.get(),
                                   .sdc_path = optionalString(options, "sdc_file").value_or(""),
                                   .spef_path = optionalString(options, "spef_file").value_or("")});
        interface.initSTA(config);
        interface.runSTA();
        interface.destroySTA();
      });
}

void runPw(eccdb::Database& database, const Json& options, const ConfigMap& config)
{
  // This checkout exposes the averaged PW flow through runPW(). Peak power
  // belongs to a newer Operation API and must not silently run the average flow.
  if (boolOption(options, "peak")) {
    throw std::invalid_argument("this version of iPW supports averaged power only; peak is unavailable");
  }
  LibertyInputs liberty{stringList(options, "liberty_files", true)};
  auto spef = loadSpef(options);
  auto vcd = loadVcd(options);
  auto& interface = PWI;
  runNativeStage(
      [&] { interface.abortPW(); },
      [&] { eccdb::adapters::bindPwSource(interface, nullptr); },
      [&] {
        eccdb::adapters::bindPwSource(interface, &database.context(),
                                 {.liberty_libraries = liberty.raw(),
                                  .spef_reader = spef.get(),
                                  .vcd_reader = vcd.get(),
                                  .sdc_path = optionalString(options, "sdc_file").value_or(""),
                                  .spef_path = optionalString(options, "spef_file").value_or("")});
        interface.initPW(config);
        interface.runPW();
        interface.destroyPW();
      });
}

void runEmir(eccdb::Database& database, const ConfigMap& config)
{
  auto& interface = EMIRI;
  runNativeStage([&] { interface.abortEMIR(); }, [&] { eccdb::adapters::bindEmirSource(interface, nullptr); }, [&] {
    eccdb::adapters::bindEmirSource(interface, &database.context());
    interface.initEMIR(config);
    interface.runEMIR();
    interface.destroyEMIR();
  });
}

void runMj(eccdb::Database& database, const Json& options, const ConfigMap& config)
{
  const auto operation = lower(requiredString(options, "operation"));
  auto& interface = MJI;
  runNativeStage([&] { interface.abortMJ(); }, [&] { eccdb::adapters::bindMjSource(interface, nullptr); }, [&] {
    eccdb::adapters::bindMjSource(interface, &database.context());
    interface.initMJ(config);
    if (operation == "filler") {
      interface.insertFiller(config);
    } else if (operation == "metal") {
      interface.insertMetal(config);
    } else if (operation == "antenna") {
      interface.checkAntenna(config);
    } else {
      throw std::invalid_argument("mj operation must be one of: filler, metal, antenna");
    }
    interface.destroyMJ();
  });
}

void runLvs(eccdb::Database& database, const Json& options, const ConfigMap& config)
{
  const auto reference_file = requiredString(options, "reference_verilog");
  if (!std::filesystem::is_regular_file(reference_file)) {
    throw std::invalid_argument("lvs reference_verilog does not exist: " + reference_file);
  }

  const auto& context = database.context();
  auto* technology = context.technology();
  auto* library = context.library();
  if (technology == nullptr || library == nullptr) {
    throw std::invalid_argument("lvs requires loaded EccDB technology and library stores");
  }

  // Share the loaded library, but import the independent logical reference
  // into its own DesignStore. Never import it over the physical design, and
  // never construct a reference from that design's current connections.
  ilvs::NetlistData reference;
  {
    eccdb::DesignStore reference_design(technology->techRegistry(), library->libraryRegistry());
    eccdb::DatabaseContext reference_context(*technology, *library, reference_design);
    eccdb::VerilogNetlistImportService importer(reference_context);
    static_cast<void>(importer.import(reference_file, optionalString(options, "top_module").value_or("")));
    reference = eccdb::adapters::wrapLvsNetlist(reference_context);
  }
  const auto& design = context.design();
  if (design.globalStorage().hasInfo() && reference.get_design_name() != design.globalStorage().info().name) {
    throw std::invalid_argument("lvs reference top '" + reference.get_design_name() + "' differs from physical design '"
                                + design.globalStorage().info().name + "'");
  }

  auto& interface = LVSI;
  runNativeStage(
      [&] { interface.abortLVS(); },
      [&] {
        eccdb::adapters::bindLvsSource(interface, nullptr, nullptr);
      },
      [&] {
        eccdb::adapters::bindLvsSource(interface, &database.context(), &reference);
        interface.initLVS(config);
        interface.runLVS();
        interface.destroyLVS();
      });
}

}  // namespace

std::vector<std::string> supportedTools()
{
  // RCX remains external; its extraction collateral is not represented by
  // this scripting boundary. LVS accepts an independent reference Verilog.
  return {"fp", "cts", "rt", "drc", "sta", "pw", "emir", "mj", "lvs"};
}

void runTool(eccdb::Database& database, const std::string& requested_tool,
             const std::filesystem::path& config, const std::string& options_json)
{
  const auto tool = lower(requested_tool);
  if (tool.empty()) {
    throw std::invalid_argument("EccDB tool name must not be empty");
  }
  if (tool == "rcx") {
    throw std::invalid_argument("native EccDB RCX is not supported; provide external SPEF to sta or pw");
  }
  const auto tools = supportedTools();
  if (std::find(tools.begin(), tools.end(), tool) == tools.end()) {
    throw std::invalid_argument("unsupported EccDB tool: " + requested_tool);
  }

  std::scoped_lock lock{g_native_stage_mutex};
  // iFP and iCTS own richer nested configuration documents. Passing either
  // through the generic map would duplicate its parser. CTS only accepts the
  // explicit frontend inputs below; change native CTS fields in its JSON file.
  const Json raw_options = toolSection(readOptions(options_json), tool);
  if (tool == "fp" || tool == "cts") {
    const Json options = canonicalizeToolOptions(raw_options, "tool options");
    if (tool == "cts") {
      for (auto it = options.begin(); it != options.end(); ++it) {
        if (!isFrontendKey(it.key())) {
          throw std::invalid_argument("cts configuration overrides are unsupported; update its iCTS JSON file instead");
        }
      }
      runCts(database, config, options);
    } else {
      runFp(database, config, options);
    }
    return;
  }

  const Json options = mergedToolOptions(config, tool, options_json);
  const ConfigMap tool_config = configMapFromJson(options);
  if (tool == "rt") {
    runRt(database, tool_config);
  } else if (tool == "drc") {
    runDrc(database, tool_config);
  } else if (tool == "sta") {
    runSta(database, options, tool_config);
  } else if (tool == "pw") {
    runPw(database, options, tool_config);
  } else if (tool == "emir") {
    runEmir(database, tool_config);
  } else if (tool == "mj") {
    runMj(database, options, tool_config);
  } else if (tool == "lvs") {
    runLvs(database, options, tool_config);
  } else {
    throw std::invalid_argument("unsupported EccDB tool: " + requested_tool);
  }
}

std::string databaseInfo(const eccdb::Database& database)
{
  const auto& context = database.context();
  const auto& design = context.design();
  const auto& netlist = design.netlistStorage();
  const auto& routing = design.routingStorage();

  Json result{{"backend", "eccdb"},
              {"schema_version", 1},
              {"context_revision", context.revision()},
              {"geometry_epoch", routing.geometryEpoch()},
              {"instances", netlist.instanceCount()},
              {"instance_terms", netlist.instancePinCount()},
              {"block_terms", netlist.ioPinCount()},
              {"nets", netlist.netCount()},
              {"regular_nets", netlist.regularNets().size()},
              {"special_nets", netlist.specialNets().size()},
              {"wires", routing.wireCount()},
              {"design_vias", routing.viaCount()},
              {"diagnostics", database.diagnostics().size()}};

  if (const auto* library = context.library(); library != nullptr) {
    result["cell_masters"] = library->cellMasterStorage().cellMasterCount();
    result["master_terms"] = library->masterTermStorage().masterTermCount();
  }
  result["has_technology"] = context.technology() != nullptr;
  result["has_library"] = context.library() != nullptr;
  return result.dump();
}

}  // namespace ecc::scripting
