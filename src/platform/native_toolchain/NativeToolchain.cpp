// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// Licensed under Mulan PSL v2. See the project LICENSE for details.
#include "NativeToolchain.h"

#include <any>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <sys/resource.h>
#include <unistd.h>

#include "CTSAPI.hh"
#include "DRCInterface.hpp"
#include "EMIRInterface.hpp"
#include "FPInterface.hpp"
#include "builder.h"
#include "LVSInterface.hpp"
#include "Lib.hh"
#include "NetlistData.hpp"
#include "RCXInterface.hpp"
#include "RTInterface.hpp"
#include "STAInterface.hpp"
#include "ZHInterface.hpp"
#include "eccdb.h"
#include "json.hpp"
#include "spef/SpefParser.hh"

namespace ecc {
namespace {
using Json = nlohmann::json;
using ConfigMap = std::map<std::string, std::any>;
using Clock = std::chrono::steady_clock;
NativeToolchain* active_flow = nullptr;

Json memoryUsage()
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

void addOptions(ConfigMap& values, const Json& input)
{
  for (const auto& [name, value] : input.items()) {
    const auto key = name.starts_with('-') ? name : "-" + name;
    if (value.is_object()) addOptions(values, value);
    else if (value.is_string()) {
      if (key == "-thread_number" || key == "-thread_num" || key == "-enable_timing"
          || key == "-output_inter_result" || key == "-output_csv") {
        values[key] = std::stoi(value.get<std::string>());
      } else values[key] = value.get<std::string>();
    }
    else if (value.is_boolean()) values[key] = value.get<bool>();
    else if (value.is_number_integer()) values[key] = value.get<int>();
    else if (value.is_number_float()) values[key] = value.get<double>();
    else if (value.is_array()) {
      if (value.empty() || value.front().is_string()) values[key] = value.get<std::vector<std::string>>();
      else if (value.front().is_number_integer()) values[key] = value.get<std::vector<int>>();
      else if (value.front().is_number()) values[key] = value.get<std::vector<double>>();
      // Structured arrays (RCX corners, PDN connections, etc.) are consumed
      // by the existing tool-specific JSON parser through -config.
    }
  }
}

Json readJson(const std::string& file)
{
  std::ifstream input(file);
  if (!input) throw std::runtime_error("Cannot open tool config: " + file);
  return Json::parse(input);
}

template <class Function>
void timed(Json& result, const char* phase, Function&& action)
{
  const auto begin = Clock::now();
  try {
    action();
  } catch (const std::exception& error) {
    throw std::runtime_error(std::string(phase) + ": " + error.what());
  }
  result["phase_ms"][phase] = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

void requireCts(const icts::CTSStatus& status)
{
  if (!status.ok()) throw std::runtime_error("CTS: " + status.message);
}
}  // namespace

struct NativeToolchain::State : eccdb::DatabaseObserver
{
  explicit State(eccdb::Database value) : database(std::move(value)) {}
  eccdb::Database database;
  std::vector<std::string> lef_files;
  std::vector<std::unique_ptr<idb::LibLibrary>> libraries;
  std::unique_ptr<spef::SpefReader> spef;
  std::unique_ptr<ilvs::NetlistData> reference;
  std::string sdc_path;
  std::string spef_path;
  uint64_t spef_revision = 0;
  bool failed = false;
  uint64_t changes = 0;
  uint64_t changed_instances = 0;
  uint64_t changed_nets = 0;
  uint64_t changed_shapes = 0;
  void afterChange(const eccdb::DatabaseChange& change) noexcept override
  {
    ++changes;
    changed_instances += change.instances.size();
    changed_nets += change.nets.size();
    changed_shapes += change.shapes.size();
  }
};

NativeToolchain::NativeToolchain(const std::vector<std::string>& lef_files, const std::string& def_file)
{
  if (active_flow) throw std::logic_error("Operation singletons support only one active NativeToolchain");
  // Both CLI and Python flows need tool failures to reach the stage runner,
  // rather than terminating the process inside a legacy logger.
  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
  eccdb::LefDefInput input;
  for (const auto& file : lef_files) input.lef_files.emplace_back(file);
  input.def_file = def_file;
  _state = std::make_unique<State>(eccdb::Database::open({.input = std::move(input)}));
  _state->lef_files = lef_files;
  context().addObserver(*_state);
  active_flow = this;
}

NativeToolchain::~NativeToolchain()
{
  RTI.setDesignSource(nullptr);
  DRCI.setDesignSource(nullptr);
  FPI.setDesignSource(nullptr);
  CTS_API_INST.setDesignSource(nullptr);
  CTS_API_INST.setLibertyCellLookup({});
  CTS_API_INST.setSdcPath("");
  ZHI.setDesignSource(nullptr);
  RCXI.setDesignSource(nullptr);
  STAI.setDesignSource(nullptr);
  STAI.setTimingInputs({});
  LVSI.setDesignSource(nullptr);
  LVSI.setReferenceNetlist(nullptr);
  EMIRI.setDesignSource(nullptr);
  context().removeObserver(*_state);
  active_flow = nullptr;
}

NativeToolchain::NativeToolchain(const std::string& binary_directory)
{
  if (active_flow) throw std::logic_error("Operation singletons support only one active NativeToolchain");
  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
  const std::filesystem::path root(binary_directory);
  eccdb::BinaryInput input{.files = {root / "technology.edb", root / "library.edb", root / "design.edb"}};
  _state = std::make_unique<State>(eccdb::Database::open({.input = std::move(input)}));
  context().addObserver(*_state);
  active_flow = this;
}

eccdb::DatabaseContext& NativeToolchain::context() const { return _state->database.context(); }

void NativeToolchain::loadLiberty(const std::vector<std::string>& files)
{
  // Libraries are external timing inputs, owned once for both CTS and STA.
  std::vector<std::unique_ptr<idb::LibLibrary>> libraries;
  for (const auto& file : files) {
    idb::Lib parser;
    auto reader = parser.loadLibertyWithCppParser(file.c_str());
    if (!reader.linkLib()) throw std::runtime_error("Liberty link failed: " + file);
    libraries.push_back(reader.get_library_builder()->takeLib());
  }
  _state->libraries = std::move(libraries);
  CTS_API_INST.setLibertyCellLookup([this](const std::string& name) -> idb::LibCell* {
    for (auto& library : _state->libraries) {
      if (auto* cell = library->findCell(name.c_str())) return cell;
    }
    return nullptr;
  });
}

void NativeToolchain::loadSdc(const std::string& file)
{
  _state->sdc_path = file;
  CTS_API_INST.setSdcPath(file);
}

void NativeToolchain::loadSpef(const std::string& file)
{
  auto reader = std::make_unique<spef::SpefReader>();
  if (!reader->read(file)) throw std::runtime_error("SPEF load failed: " + file);
  reader->expandName();
  _state->spef = std::move(reader);
  _state->spef_path = file;
  _state->spef_revision = context().revision();
}

void NativeToolchain::loadReferenceVerilog(const std::string& file, const std::string& top)
{
  if (_state->lef_files.empty()) {
    throw std::logic_error("Verilog reference parsing requires the original LEF inputs; start this LVS flow from LEF/DEF");
  }
  // Existing Verilog parser provides only the external LVS reference. Its
  // temporary iDB view is projected into the tool's NetlistData and released;
  // the physical design throughout all stages remains the owned EccDB.
  idb::IdbBuilder reader;
  auto lefs = _state->lef_files;
  if (!reader.buildLef(lefs, true)) throw std::runtime_error("Reference master library load failed");
  auto* netlist = reader.buildVerilog(file, top);
  if (!netlist) throw std::runtime_error("Reference Verilog load failed: " + file);
  _state->reference = std::make_unique<ilvs::NetlistData>(LVSI.wrapNetlistData(netlist->get_design()));
  LVSI.setReferenceNetlist(_state->reference.get());
}

std::string NativeToolchain::summary() const
{
  const auto& design = context().design();
  const auto& nets = design.netlistStorage();
  const auto& global = design.globalStorage();
  const auto count = design.geometryRegistry().registry().view<const eccdb::GeometryShape>().size();
  return Json{{"backend", "eccdb"}, {"design", global.info().name}, {"revision", context().revision()},
              {"instances", nets.instanceCount()}, {"instance_terms", nets.instancePinCount()},
              {"block_terms", nets.ioPinCount()}, {"block_pins", nets.blockPinCount()},
              {"nets", nets.netCount()}, {"wires", design.routingStorage().wireCount()},
              {"design_shapes", count}}.dump();
}

std::string NativeToolchain::inspectInstance(const std::string& name) const
{
  const auto& nets = context().design().netlistStorage();
  const auto id = nets.findInstance(name);
  if (!id) throw std::invalid_argument("Unknown instance: " + name);
  const auto& value = nets.instance(id);
  const auto& library = *context().library();
  Json terms = Json::array();
  for (const auto term_id : nets.instanceTerms(id)) {
    const auto& term = nets.instanceTerm(term_id);
    terms.push_back({{"id", term_id.packed()}, {"master_term_id", term.master_term.packed()},
                     {"name", library.masterTermStorage().masterTerm(term.master_term).name},
                     {"net", term.net ? Json(nets.net(term.net).name) : Json(nullptr)},
                     {"special_net", term.special_net ? Json(nets.net(term.special_net).name) : Json(nullptr)}});
  }
  return Json{{"id", id.packed()}, {"registry", "design"}, {"name", value.name},
              {"master", library.cellMasterStorage().cellMaster(value.master).name},
              {"master_id", value.master.packed()}, {"origin", {value.origin.x, value.origin.y}},
              {"orientation", static_cast<unsigned>(value.orientation)},
              {"placement_status", static_cast<unsigned>(value.placement_status)}, {"terms", terms}}.dump();
}

std::string NativeToolchain::inspectNet(const std::string& name) const
{
  const auto& design = context().design();
  const auto& nets = design.netlistStorage();
  const auto id = nets.findNet(name);
  if (!id) throw std::invalid_argument("Unknown net: " + name);
  Json endpoints = Json::array();
  for (const auto term_id : nets.instanceTerms(id)) {
    const auto& term = nets.instanceTerm(term_id);
    endpoints.push_back({{"term_id", term_id.packed()}, {"instance", nets.instance(term.instance).name},
                         {"pin", context().library()->masterTermStorage().masterTerm(term.master_term).name},
                         {"master_term_id", term.master_term.packed()}});
  }
  return Json{{"id", id.packed()}, {"registry", "design"}, {"name", nets.net(id).name},
              {"special", nets.isSpecialNet(id)}, {"instance_terms", endpoints},
              {"block_term_count", nets.blockTerms(id).size()},
              {"wire_count", design.routingStorage().wireIds(id).size()}}.dump();
}

void NativeToolchain::writeDef(const std::string& file) const { _state->database.writeDef(file); }

void NativeToolchain::writeBinary(const std::string& directory) const
{
  const std::filesystem::path root(directory);
  std::filesystem::create_directories(root);
  _state->database.writeBinary({root / "technology.edb", root / "library.edb", root / "design.edb"});
}

std::string NativeToolchain::run(const std::string& stage, const std::string& config_file, const std::string& options_json)
{
  if (_state->failed) throw std::logic_error("A failed flow must be restarted from its input checkpoint");
  ConfigMap config;
  if (!config_file.empty()) {
    addOptions(config, readJson(config_file));
    config["-config"] = config_file;
  }
  addOptions(config, Json::parse(options_json));
  Json result{{"stage", stage}, {"backend", "eccdb"}, {"before", Json::parse(summary())},
              {"memory_before", memoryUsage()}};
  const auto begin = Clock::now();
  const auto changes_before = _state->changes;
  auto& source = context();
  try {
    if (stage == "routing") {
      RTI.setDesignSource(&source);
      timed(result, "init", [&] { RTI.initRT(config); });
      timed(result, "algorithm", [&] { RTI.runRT(); });
      timed(result, "writeback_destroy", [&] { RTI.destroyRT(); });
    } else if (stage == "drc") {
      DRCI.setDesignSource(&source);
      timed(result, "init", [&] { DRCI.initDRC(config); });
      timed(result, "algorithm", [&] { DRCI.checkDef(); });
      result["violation_counts"] = DRCI.lastViolationCounts();
      std::size_t violation_count = 0;
      for (const auto& [type, count] : DRCI.lastViolationCounts()) violation_count += count;
      result["violation_count"] = violation_count;
      result["drc_clean"] = violation_count == 0;
      // The generic status below reports execution, while this field reports
      // the signoff outcome.  A completed run may therefore have violations.
      result["drc_status"] = violation_count == 0 ? "clean" : "violations";
      timed(result, "destroy", [&] { DRCI.destroyDRC(); });
    } else if (stage == "floorplan") {
      FPI.setDesignSource(&source);
      timed(result, "init", [&] { FPI.initFP(config); });
      timed(result, "algorithm", [&] { FPI.runFP(); });
      timed(result, "writeback_destroy", [&] { FPI.destroyFP(); });
    } else if (stage == "cts") {
      CTS_API_INST.setDesignSource(&source);
      const auto work = config.contains("-work_dir") ? std::any_cast<std::string>(config.at("-work_dir")) : "";
      timed(result, "init", [&] { requireCts(CTS_API_INST.init(config_file, work)); });
      timed(result, "algorithm_writeback", [&] {
        const auto status = CTS_API_INST.runCTS();
        requireCts(status);
        result["cts_no_op"] = status.code == icts::CTSStatusCode::kNoOp;
        result["cts_message"] = status.message;
        result["diagnostics"] = status.diagnostics;
      });
      result["clock_timing"] = Json::array();
      for (const auto& clock : CTS_API_INST.outputClockTiming()) {
        result["clock_timing"].push_back({{"clock", clock.clock}, {"sink_count", clock.sink_count},
                                        {"initial_skew_ns", clock.initial_skew_ns},
                                        {"optimized_skew_ns", clock.optimized_skew_ns},
                                        {"target_met", clock.target_met}});
      }
      timed(result, "destroy", [&] { requireCts(CTS_API_INST.destroyCTS()); });
    } else if (stage == "rcx") {
      RCXI.setDesignSource(&source);
      std::vector<std::string> spef_files;
      timed(result, "init", [&] { RCXI.initRCX(config); });
      timed(result, "algorithm", [&] { RCXI.runRCX(); });
      spef_files = RCXI.getSpefOutputPaths();
      timed(result, "destroy", [&] { RCXI.destroyRCX(); });
      if (spef_files.empty()) throw std::runtime_error("RCX produced no SPEF corner");
      timed(result, "load_parasitics", [&] { loadSpef(spef_files.front()); });
      result["spef_files"] = spef_files;
      result["timing_spef"] = _state->spef_path;
    } else if (stage == "sta") {
      if (_state->spef && _state->spef_revision != source.revision()) {
        throw std::logic_error("The design changed after SPEF was loaded; run RCX again before STA");
      }
      STAI.setDesignSource(&source);
      ista::NativeTimingInputs inputs;
      for (const auto& library : _state->libraries) inputs.liberty_libraries.push_back(library.get());
      inputs.sdc_path = _state->sdc_path;
      inputs.spef_path = _state->spef_path;
      inputs.spef_reader = _state->spef.get();
      result["timing_spef"] = _state->spef_path;
      result["spef_design_revision"] = _state->spef_revision;
      STAI.setTimingInputs(std::move(inputs));
      timed(result, "init", [&] { STAI.initSTA(config); });
      timed(result, "algorithm", [&] { STAI.runSTA(); });
      timed(result, "destroy", [&] { STAI.destroySTA(); });
    } else if (stage == "lvs") {
      LVSI.setDesignSource(&source);
      LVSI.setReferenceNetlist(_state->reference.get());
      timed(result, "init", [&] { LVSI.initLVS(config); });
      timed(result, "algorithm", [&] { LVSI.runLVS(); });
      timed(result, "destroy", [&] { LVSI.destroyLVS(); });
    } else if (stage == "emir") {
      EMIRI.setDesignSource(&source);
      timed(result, "init", [&] { EMIRI.initEMIR(config); });
      timed(result, "algorithm", [&] { EMIRI.runEMIR(); });
      timed(result, "destroy", [&] { EMIRI.destroyEMIR(); });
    } else if (stage == "filler" || stage == "metal" || stage == "antenna") {
      ZHI.setDesignSource(&source);
      timed(result, "algorithm_writeback", [&] {
        if (stage == "filler") ZHI.insertFiller(config);
        else if (stage == "metal") ZHI.insertMetal(config);
        else {
          ZHI.checkAntenna(config);
          const auto& antenna = ZHI.lastAntennaSummary();
          result["antenna"] = {
              {"checked", antenna.checked},
              {"net_count", antenna.net_count},
              {"connected_net_count", antenna.connected_net_count},
              {"wire_count", antenna.wire_count},
              {"violation_count", antenna.violation_count},
              {"rule_file", antenna.rule_file},
              {"violations", antenna.violations}};
        }
      });
    } else throw std::invalid_argument("Unknown native tool stage: " + stage);
  } catch (...) {
    // Do not call the normal destroy/writeback path on incomplete results.
    // Cleanup is best effort: an error from a destructor must never replace
    // the stage exception that the caller needs to diagnose.
    try {
      if (stage == "routing") RTI.abortRT();
      else if (stage == "drc") DRCI.abortDRC();
      else if (stage == "floorplan") FPI.abortFP();
      else if (stage == "cts") CTS_API_INST.abortCTS();
      else if (stage == "rcx") RCXI.abortRCX();
      else if (stage == "sta") STAI.abortSTA();
      else if (stage == "lvs") LVSI.abortLVS();
      else if (stage == "emir") EMIRI.abortEMIR();
    } catch (...) {
      // Preserve the original exception and mark the flow failed below.
    }
    _state->failed = true;
    throw;
  }
  result["elapsed_ms"] = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
  result["after"] = Json::parse(summary());
  result["memory_after"] = memoryUsage();
  result["change_notifications"] = _state->changes - changes_before;
  result["status"] = "passed";
  return result.dump();
}

}  // namespace ecc
