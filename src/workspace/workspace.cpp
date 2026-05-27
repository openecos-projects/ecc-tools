// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the
// Mulan PSL v2.
// ***************************************************************************************

#include "workspace.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace ieda_workspace {

namespace fs = std::filesystem;

namespace {

constexpr const char* kFloorplan = "Floorplan";
constexpr const char* kFixFanout = "fixFanout";
constexpr const char* kPlace = "place";
constexpr const char* kCts = "CTS";
constexpr const char* kLegalization = "legalization";
constexpr const char* kRoute = "route";
constexpr const char* kDrc = "drc";
constexpr const char* kFiller = "filler";
constexpr const char* kPnp = "PNP";
constexpr const char* kOptDrv = "optDrv";
constexpr const char* kOptHold = "optHold";
constexpr const char* kOptSetup = "optSetup";
constexpr const char* kRcx = "RCX";
constexpr const char* kSta = "sta";

std::string pathString(const fs::path& path)
{
  return path.lexically_normal().string();
}

std::string envValue(const char* name)
{
  const char* value = std::getenv(name);
  return value == nullptr ? "" : std::string(value);
}

json readJsonFile(const fs::path& path)
{
  std::ifstream stream(path);
  if (!stream.is_open()) {
    throw std::runtime_error("Failed to open json file: " + pathString(path));
  }

  if (fs::file_size(path) == 0) {
    return json::object();
  }

  json data = json::object();
  stream >> data;
  return data;
}

void writeJsonFile(const fs::path& path, const json& data)
{
  fs::create_directories(path.parent_path());
  std::ofstream stream(path);
  if (!stream.is_open()) {
    throw std::runtime_error("Failed to write json file: " + pathString(path));
  }
  stream << data.dump(4) << std::endl;
}

std::string mapValue(const std::map<std::string, std::string>& values, const std::string& key)
{
  auto it = values.find(key);
  return it == values.end() ? "" : it->second;
}

std::string boolString(bool value)
{
  return value ? "true" : "false";
}

}  // namespace

WorkspaceManager& WorkspaceManager::getInstance()
{
  static WorkspaceManager manager;
  return manager;
}

void WorkspaceManager::reset()
{
  _workspace = Workspace();
}

bool WorkspaceManager::load(const std::string& home_or_workspace_dir)
{
  reset();
  try {
    resolveWorkspaceDir(fs::path(home_or_workspace_dir));
    loadHome();
    loadParameters();
    loadFlow();
    loadChecklist();
    buildPdk();
    resolveOriginFiles();
    buildSteps();
    return true;
  } catch (const std::exception& e) {
    std::cerr << "[workspace] " << e.what() << std::endl;
    reset();
    return false;
  }
}

bool WorkspaceManager::prepare(bool force_rebuild_config)
{
  try {
    if (_workspace.directory.empty()) {
      throw std::runtime_error("workspace is not loaded");
    }

    writeDefaultSdcIfNeeded();
    for (const auto& step : _workspace.steps) {
      createStepDirectories(step);
      if (force_rebuild_config || !hasStepConfigs(step)) {
        writeStepConfigs(step);
      }
    }
    return true;
  } catch (const std::exception& e) {
    std::cerr << "[workspace] " << e.what() << std::endl;
    return false;
  }
}

void WorkspaceManager::resolveWorkspaceDir(const fs::path& home_or_workspace_dir)
{
  fs::path input = fs::absolute(home_or_workspace_dir);
  if (input.filename() == "home") {
    _workspace.home_dir = input;
    _workspace.directory = input.parent_path();
  } else {
    _workspace.directory = input;
    _workspace.home_dir = input / "home";
  }

  _workspace.parameters_path = _workspace.home_dir / "parameters.json";
  _workspace.flow_path = _workspace.home_dir / "flow.json";
  _workspace.home_json_path = _workspace.home_dir / "home.json";
  _workspace.checklist_path = _workspace.home_dir / "checklist.json";
}

void WorkspaceManager::loadHome()
{
  if (fs::exists(_workspace.home_json_path)) {
    _workspace.home = readJsonFile(_workspace.home_json_path);
  }
}

void WorkspaceManager::loadParameters()
{
  _workspace.parameters = readJsonFile(_workspace.parameters_path);
  _workspace.design_name = scalarString(_workspace.parameters.value("Design", ""), "design");
  _workspace.top_module = scalarString(_workspace.parameters.value("Top module", ""), _workspace.design_name);
}

void WorkspaceManager::loadFlow()
{
  _workspace.flow = readJsonFile(_workspace.flow_path);
  if (!_workspace.flow.contains("steps") || !_workspace.flow["steps"].is_array()) {
    throw std::runtime_error("flow.json has no steps array: " + pathString(_workspace.flow_path));
  }
}

void WorkspaceManager::loadChecklist()
{
  if (fs::exists(_workspace.checklist_path)) {
    _workspace.checklist = readJsonFile(_workspace.checklist_path);
  }
}

void WorkspaceManager::buildPdk()
{
  const std::string pdk_name = scalarString(_workspace.parameters.value("PDK", ""), "");
  std::string pdk_root = scalarString(_workspace.parameters.value("PDK Root", ""), "");
  if (pdk_root.empty()) {
    pdk_root = envValue("CHIPCOMPILER_ICS55_PDK_ROOT");
  }
  if (pdk_root.empty()) {
    pdk_root = envValue("ICS55_PDK_ROOT");
  }

  std::string pdk_lower = pdk_name;
  std::transform(pdk_lower.begin(), pdk_lower.end(), pdk_lower.begin(), [](unsigned char c) { return std::tolower(c); });
  _workspace.pdk.name = pdk_lower;
  _workspace.pdk.root = pdk_root;

  if (pdk_lower == "ics55") {
    const fs::path root = fs::path(pdk_root);
    const fs::path stdcell_dir = root / "IP/STD_cell/ics55_LLSC_H7C_V1p10C100";
    _workspace.pdk.version = "V1p10C100";
    _workspace.pdk.tech_lef = pathString(root / "prtech/techLEF/N551P6M_ecos.lef");
    _workspace.pdk.lef_paths = {
        pathString(stdcell_dir / "ics55_LLSC_H7CR/lef/ics55_LLSC_H7CR_ecos.lef"),
        pathString(stdcell_dir / "ics55_LLSC_H7CL/lef/ics55_LLSC_H7CL_ecos.lef"),
    };
    _workspace.pdk.lib_paths = {
        pathString(stdcell_dir / "ics55_LLSC_H7CR/liberty/ics55_LLSC_H7CR_ss_rcworst_1p08_125_nldm.lib"),
        pathString(stdcell_dir / "ics55_LLSC_H7CL/liberty/ics55_LLSC_H7CL_ss_rcworst_1p08_125_nldm.lib"),
    };
    _workspace.pdk.mapping_file = pathString(root / "corners/ICsprout_55LLULP_1P6M_5lc_V1p1_cell.map");
    _workspace.pdk.corners = json::array(
        {{{"name", "TYPICAL"},
          {"ecc_tf", pathString(root / "corners/TYP.json")},
          {"itf_file", pathString(root / "corners/TYP.itf")},
          {"captab_file", pathString(root / "corners/TYP.captab")},
          {"spef_file", pathString(root / "corners/TYP.spef")}},
         {{"name", "RCbest"},
          {"ecc_tf", pathString(root / "corners/RCbest.json")},
          {"itf_file", pathString(root / "corners/RCbest.itf")},
          {"captab_file", pathString(root / "corners/RCbest.captab")},
          {"spef_file", pathString(root / "corners/RCbest.spef")}},
         {{"name", "RCworst"},
          {"ecc_tf", pathString(root / "corners/RCworst.json")},
          {"itf_file", pathString(root / "corners/RCworst.itf")},
          {"captab_file", pathString(root / "corners/RCworst.captab")},
          {"spef_file", pathString(root / "corners/RCworst.spef")}},
         {{"name", "Cbest"},
          {"ecc_tf", pathString(root / "corners/Cbest.json")},
          {"itf_file", pathString(root / "corners/Cbest.itf")},
          {"captab_file", pathString(root / "corners/Cbest.captab")},
          {"spef_file", pathString(root / "corners/Cbest.spef")}},
         {{"name", "Cworst"},
          {"ecc_tf", pathString(root / "corners/Cworst.json")},
          {"itf_file", pathString(root / "corners/Cworst.itf")},
          {"captab_file", pathString(root / "corners/Cworst.captab")},
          {"spef_file", pathString(root / "corners/Cworst.spef")}}});
    _workspace.pdk.site_core = "core7";
    _workspace.pdk.site_io = "core7";
    _workspace.pdk.site_corner = "core7";
    _workspace.pdk.tap_cell = "FILLTAPH7R";
    _workspace.pdk.end_cap = "FILLTAPH7R";
    _workspace.pdk.buffers = {"BUFX8H7L", "BUFX12H7L", "BUFX16H7L", "BUFX20H7L"};
    _workspace.pdk.fillers = {"FILLER64H7R", "FILLER32H7R", "FILLER16H7R", "FILLER8H7R",
                              "FILLER4H7R",  "FILLER2H7R",  "FILLER1H7R"};
    _workspace.pdk.tie_high_cell = "TIEHIH7R";
    _workspace.pdk.tie_high_port = "Z";
    _workspace.pdk.tie_low_cell = "TIELOH7R";
    _workspace.pdk.tie_low_port = "Z";
  }
}

void WorkspaceManager::resolveOriginFiles()
{
  _workspace.origin_def = findOriginFile(".def.gz", _workspace.design_name + ".def.gz");
  if (!fs::exists(_workspace.origin_def)) {
    _workspace.origin_def = findOriginFile(".def", _workspace.design_name + ".def");
  }

  _workspace.origin_verilog = findOriginFile(".v.gz", _workspace.design_name + ".v.gz");
  if (!fs::exists(_workspace.origin_verilog)) {
    _workspace.origin_verilog = findOriginFile(".v", _workspace.design_name + ".v");
  }

  _workspace.pdk.sdc_path = findOriginFile(".sdc", _workspace.design_name + ".sdc");
  _workspace.pdk.spef_path = findOriginFile(".spef", _workspace.design_name + ".spef");
  if (!_workspace.pdk.spef_path.empty() && !fs::exists(_workspace.pdk.spef_path)) {
    _workspace.pdk.spef_path.clear();
  }
}

void WorkspaceManager::buildSteps()
{
  _workspace.steps.clear();

  std::string input_def = pathString(_workspace.origin_def);
  std::string input_verilog = pathString(_workspace.origin_verilog);
  std::string input_db;

  for (const auto& step_json : _workspace.flow["steps"]) {
    WorkspaceStep step;
    step.name = scalarString(step_json.value("name", ""), "");
    step.tool = scalarString(step_json.value("tool", ""), "");
    if (step.name.empty() || step.tool.empty()) {
      continue;
    }

    buildStep(step, input_def, input_verilog, input_db);
    _workspace.steps.push_back(step);

    input_def = mapValue(step.output, "def");
    input_verilog = mapValue(step.output, "verilog");
    input_db = mapValue(step.output, "db");
  }
}

void WorkspaceManager::buildStep(WorkspaceStep& step,
                                 const std::string& input_def,
                                 const std::string& input_verilog,
                                 const std::string& input_db) const
{
  step.directory = _workspace.directory / (step.name + "_" + step.tool);

  const std::string workspace_dir = pathString(_workspace.directory);
  const std::string step_dir = pathString(step.directory);
  const std::string design = _workspace.design_name;
  const std::string top = _workspace.top_module;

  step.config = {
      {"dir", workspace_dir + "/config"},
      {"flow", workspace_dir + "/config/flow_config.json"},
      {"db", workspace_dir + "/config/db_default_config.json"},
      {kCts, workspace_dir + "/config/cts_default_config.json"},
      {kDrc, workspace_dir + "/config/drc_default_config.json"},
      {kFloorplan, workspace_dir + "/config/fp_default_config.json"},
      {kFixFanout, workspace_dir + "/config/no_default_config_fixfanout.json"},
      {kPlace, workspace_dir + "/config/pl_default_config.json"},
      {kPnp, workspace_dir + "/config/pnp_default_config.json"},
      {kRoute, workspace_dir + "/config/rt_default_config.json"},
      {kOptDrv, workspace_dir + "/config/to_default_config_drv.json"},
      {kOptHold, workspace_dir + "/config/to_default_config_hold.json"},
      {kOptSetup, workspace_dir + "/config/to_default_config_setup.json"},
      {kLegalization, workspace_dir + "/config/pl_default_config.json"},
      {kFiller, workspace_dir + "/config/pl_default_config.json"},
      {kRcx, workspace_dir + "/config/rcx.json"},
  };

  step.input = {{"def", input_def}, {"verilog", input_verilog}, {"db", input_db}};

  step.output = {
      {"dir", step_dir + "/output"},
      {"def", step_dir + "/output/" + design + "_" + step.name + ".def.gz"},
      {"verilog", step_dir + "/output/" + design + "_" + step.name + ".v"},
      {"gds", step_dir + "/output/" + design + "_" + step.name + ".gds"},
      {"db", step_dir + "/output/" + design + "_" + step.name + "_db"},
      {"image", step_dir + "/output/" + design + "_" + step.name + ".png"},
      {"json", step_dir + "/output/" + design + "_" + step.name + ".json"},
      {"lef", step_dir + "/output/" + design + "_" + step.name + ".lef"},
      {"lib", step_dir + "/output/" + design + "_" + step.name + ".lib"},
  };

  step.data = {
      {"dir", step_dir + "/data"},
      {kFloorplan, step_dir + "/data/fp"},
      {kPnp, step_dir + "/data/pnp"},
      {kPlace, step_dir + "/data/pl"},
      {kLegalization, step_dir + "/data/pl"},
      {kFiller, step_dir + "/data/pl"},
      {kCts, step_dir + "/data/cts"},
      {kFixFanout, step_dir + "/data/no"},
      {kOptDrv, step_dir + "/data/to"},
      {kOptHold, step_dir + "/data/to"},
      {kOptSetup, step_dir + "/data/to"},
      {kRoute, step_dir + "/data/rt"},
      {kSta, step_dir + "/data/sta"},
      {kDrc, step_dir + "/data/drc"},
      {kRcx, step_dir + "/data/rcx"},
  };

  step.feature = {
      {"dir", step_dir + "/feature"},
      {"db", step_dir + "/feature/" + step.name + ".db.json"},
      {"step", step_dir + "/feature/" + step.name + ".step.json"},
      {"map", step_dir + "/feature/" + step.name + ".map.json"},
      {"timing", step_dir + "/data/sta/" + top + ".rpt.json"},
  };

  step.report = {
      {"dir", step_dir + "/report"},
      {"db", step_dir + "/report/" + step.name + ".db.rpt"},
      {"step", step_dir + "/report/" + step.name + ".rpt"},
      {"sta.timing", step_dir + "/data/sta/" + top + ".rpt"},
      {"sta.hold", step_dir + "/data/sta/" + top + "_hold.skew"},
      {"sta.setup", step_dir + "/data/sta/" + top + "_setup.skew"},
      {"sta.cap", step_dir + "/data/sta/" + top + ".cap"},
      {"sta.fanout", step_dir + "/data/sta/" + top + ".fanout"},
      {"sta.trans", step_dir + "/data/sta/" + top + ".trans"},
  };

  step.log = {{"dir", step_dir + "/log"}, {"file", step_dir + "/log/" + step.name + ".log"}};
  step.script = {{"dir", step_dir + "/script"}, {"main", step_dir + "/script/" + step.name + "_main.tcl"}};
  step.analysis = {{"dir", step_dir + "/analysis"},
                   {"metrics", step_dir + "/analysis/" + step.name + "_metrics.json"},
                   {"statis_csv", step_dir + "/analysis/" + step.name + "_statis.csv"}};
}

void WorkspaceManager::createStepDirectories(const WorkspaceStep& step) const
{
  fs::create_directories(step.directory);
  for (const auto* group : {&step.config, &step.output, &step.data, &step.feature, &step.report, &step.log, &step.script, &step.analysis}) {
    for (const auto& [key, path] : *group) {
      if (key == "dir" || group == &step.data) {
        fs::create_directories(path);
      }
    }
  }

  fs::create_directories(step.directory / "data/pl/density");
  fs::create_directories(step.directory / "data/pl/gui");
  fs::create_directories(step.directory / "data/pl/log");
  fs::create_directories(step.directory / "data/pl/plot");
  fs::create_directories(step.directory / "data/pl/report");
}

bool WorkspaceManager::hasStepConfigs(const WorkspaceStep& step) const
{
  for (const auto& [key, path] : step.config) {
    if (key == "dir") {
      continue;
    }
    if (!fs::exists(path)) {
      return false;
    }
  }
  return true;
}

void WorkspaceManager::writeStepConfigs(const WorkspaceStep& step) const
{
  writeJsonFile(mapValue(step.config, "flow"), defaultFlowConfig(step));
  writeJsonFile(mapValue(step.config, "db"), defaultDbConfig(step));
  writeJsonFile(mapValue(step.config, kFloorplan), defaultFloorplanConfig());
  writeJsonFile(mapValue(step.config, kFixFanout), defaultNoConfig(step));
  writeJsonFile(mapValue(step.config, kPlace), defaultPlConfig());
  writeJsonFile(mapValue(step.config, kCts), defaultCtsConfig());
  writeJsonFile(mapValue(step.config, kRoute), defaultRtConfig(step));
  writeJsonFile(mapValue(step.config, kDrc), defaultDrcConfig());
  writeJsonFile(mapValue(step.config, kPnp), defaultPnpConfig());
  writeJsonFile(mapValue(step.config, kOptDrv), defaultToConfig("drv"));
  writeJsonFile(mapValue(step.config, kOptHold), defaultToConfig("hold"));
  writeJsonFile(mapValue(step.config, kOptSetup), defaultToConfig("setup"));
  writeJsonFile(mapValue(step.config, kRcx), defaultRcxConfig(step));
  writeJsonFile(step.directory / "subflow.json", buildSubflow(step.name));
  writeJsonFile(step.directory / "checklist.json", buildChecklist());
}

void WorkspaceManager::writeDefaultSdcIfNeeded()
{
  if (!_workspace.pdk.sdc_path.empty() && fs::exists(_workspace.pdk.sdc_path)) {
    return;
  }

  fs::create_directories(_workspace.directory / "origin");
  _workspace.pdk.sdc_path = pathString(_workspace.directory / "origin" / (_workspace.design_name + ".sdc"));
  std::ofstream stream(_workspace.pdk.sdc_path);
  if (!stream.is_open()) {
    throw std::runtime_error("failed to write default sdc: " + pathString(_workspace.pdk.sdc_path));
  }

  const std::string clock = scalarString(_workspace.parameters.value("Clock", ""), "");
  const double frequency = numberValue(_workspace.parameters.value("Frequency max [MHz]", 100), 100);
  stream << "# Auto-generated SDC file\n\n";
  stream << "set clk_name " << clock << " \n";
  stream << "set clk_port_name " << clock << "\n";
  stream << "set clk_freq_mhz " << frequency << "\n";
  stream << "set clk_period [expr 1000.0 / $clk_freq_mhz]\n";
  stream << "set clk_io_pct 0.2\n";
  stream << "set clk_port [get_ports $clk_port_name]\n";
  stream << "create_clock -name $clk_name -period $clk_period $clk_port\n";
}

bool WorkspaceManager::saveFlow() const
{
  try {
    writeJsonFile(_workspace.flow_path, _workspace.flow);
    return true;
  } catch (const std::exception& e) {
    std::cerr << "[workspace] " << e.what() << std::endl;
    return false;
  }
}

bool WorkspaceManager::updateStepState(const std::string& step_name,
                                       const std::string& tool,
                                       const std::string& state,
                                       const std::string& runtime,
                                       int peak_memory_mb)
{
  if (!_workspace.flow.contains("steps") || !_workspace.flow["steps"].is_array()) {
    return false;
  }

  for (auto& step : _workspace.flow["steps"]) {
    const std::string current_name = scalarString(step.value("name", ""), "");
    const std::string current_tool = scalarString(step.value("tool", ""), "");
    if (current_name == step_name && (tool.empty() || current_tool == tool)) {
      step["state"] = state;
      if (!runtime.empty()) {
        step["runtime"] = runtime;
      }
      if (peak_memory_mb >= 0) {
        step["peak memory (mb)"] = peak_memory_mb;
      }
      return saveFlow();
    }
  }

  return false;
}

const WorkspaceStep* WorkspaceManager::getStep(const std::string& step_name) const
{
  for (const auto& step : _workspace.steps) {
    if (step.name == step_name) {
      return &step;
    }
  }
  return nullptr;
}

const WorkspaceStep* WorkspaceManager::getStepByIndex(size_t index) const
{
  if (index >= _workspace.steps.size()) {
    return nullptr;
  }
  return &_workspace.steps[index];
}

std::string WorkspaceManager::getStepName(size_t index) const
{
  const auto* step = getStepByIndex(index);
  return step == nullptr ? "" : step->name;
}

std::string WorkspaceManager::getValue(const std::string& key) const
{
  if (key == "workspace.dir") {
    return pathString(_workspace.directory);
  }
  if (key == "home.dir") {
    return pathString(_workspace.home_dir);
  }
  if (key == "home.path") {
    return pathString(_workspace.home_json_path);
  }
  if (key == "parameters.path") {
    return pathString(_workspace.parameters_path);
  }
  if (key == "flow.path") {
    return pathString(_workspace.flow_path);
  }
  if (key == "checklist.path") {
    return pathString(_workspace.checklist_path);
  }
  if (key == "design.name") {
    return _workspace.design_name;
  }
  if (key == "design.top") {
    return _workspace.top_module;
  }
  if (key == "origin.def") {
    return pathString(_workspace.origin_def);
  }
  if (key == "origin.verilog") {
    return pathString(_workspace.origin_verilog);
  }
  if (key == "pdk.root") {
    return _workspace.pdk.root;
  }
  if (key == "pdk.tech_lef") {
    return _workspace.pdk.tech_lef;
  }
  if (key == "pdk.lefs") {
    std::string result;
    for (const auto& path : _workspace.pdk.lef_paths) {
      result += (result.empty() ? "" : " ") + path;
    }
    return result;
  }
  if (key == "pdk.libs") {
    std::string result;
    for (const auto& path : _workspace.pdk.lib_paths) {
      result += (result.empty() ? "" : " ") + path;
    }
    return result;
  }
  if (key == "pdk.sdc") {
    return _workspace.pdk.sdc_path.empty() ? "" : pathString(_workspace.pdk.sdc_path);
  }
  if (key == "pdk.spef") {
    return _workspace.pdk.spef_path.empty() ? "" : pathString(_workspace.pdk.spef_path);
  }
  if (key == "pdk.site_core") {
    return _workspace.pdk.site_core;
  }
  if (key == "pdk.site_io") {
    return _workspace.pdk.site_io;
  }
  if (key == "pdk.site_corner") {
    return _workspace.pdk.site_corner;
  }
  if (key == "pdk.tap_cell") {
    return _workspace.pdk.tap_cell;
  }
  if (key == "pdk.end_cap") {
    return _workspace.pdk.end_cap;
  }
  if (key == "pdk.buffers") {
    std::string result;
    for (const auto& buffer : _workspace.pdk.buffers) {
      result += (result.empty() ? "" : " ") + buffer;
    }
    return result;
  }
  if (key == "pdk.fillers") {
    std::string result;
    for (const auto& filler : _workspace.pdk.fillers) {
      result += (result.empty() ? "" : " ") + filler;
    }
    return result;
  }
  if (key == "step.count") {
    return std::to_string(_workspace.steps.size());
  }
  if (key.rfind("param.", 0) == 0) {
    std::string param_key = key.substr(6);
    std::replace(param_key.begin(), param_key.end(), '.', '/');
    json::json_pointer pointer("/" + param_key);
    if (_workspace.parameters.contains(pointer)) {
      return scalarString(_workspace.parameters[pointer], "");
    }
  }
  return "";
}

std::string WorkspaceManager::getStepValue(const std::string& step_name, const std::string& key) const
{
  const auto* step = getStep(step_name);
  if (step == nullptr) {
    return "";
  }

  if (key == "name") {
    return step->name;
  }
  if (key == "tool") {
    return step->tool;
  }
  if (key == "dir") {
    return pathString(step->directory);
  }

  const auto dot_pos = key.find('.');
  if (dot_pos == std::string::npos) {
    return "";
  }

  const std::string group = key.substr(0, dot_pos);
  const std::string leaf = key.substr(dot_pos + 1);

  if (group == "config") {
    return mapValue(step->config, leaf);
  }
  if (group == "input") {
    return mapValue(step->input, leaf);
  }
  if (group == "output") {
    return mapValue(step->output, leaf);
  }
  if (group == "data") {
    return mapValue(step->data, leaf);
  }
  if (group == "feature") {
    return mapValue(step->feature, leaf);
  }
  if (group == "report") {
    return mapValue(step->report, leaf);
  }
  if (group == "log") {
    return mapValue(step->log, leaf);
  }
  if (group == "script") {
    return mapValue(step->script, leaf);
  }
  if (group == "analysis") {
    return mapValue(step->analysis, leaf);
  }

  return "";
}

std::string WorkspaceManager::resolvePathValue(const std::string& path) const
{
  if (path.empty()) {
    return "";
  }
  fs::path fs_path(path);
  if (fs_path.is_absolute()) {
    return pathString(fs_path);
  }
  return pathString(_workspace.directory / fs_path);
}

std::string WorkspaceManager::relativeToRoot(const std::string& path, const fs::path& root) const
{
  if (path.empty()) {
    return "";
  }

  fs::path fs_path(path);
  if (!fs_path.is_absolute()) {
    return pathString(fs_path);
  }

  const fs::path normalized_path = fs_path.lexically_normal();
  const fs::path normalized_root = root.lexically_normal();
  fs::path relative = normalized_path.lexically_relative(normalized_root);
  auto first = relative.begin();
  if (!relative.empty() && first != relative.end() && first->string() != "..") {
    return "/" + pathString(relative);
  }

  return pathString(normalized_path);
}

std::string WorkspaceManager::workspaceConfigPath(const std::string& path) const
{
  return relativeToRoot(path, _workspace.directory);
}

std::string WorkspaceManager::pdkConfigPath(const std::string& path) const
{
  return relativeToRoot(path, fs::path(_workspace.pdk.root));
}

std::vector<std::string> WorkspaceManager::pdkConfigPaths(const std::vector<std::string>& paths) const
{
  std::vector<std::string> result;
  result.reserve(paths.size());
  for (const auto& path : paths) {
    result.push_back(pdkConfigPath(path));
  }
  return result;
}

std::string WorkspaceManager::findOriginFile(const std::string& extension, const std::string& fallback_name) const
{
  const fs::path origin_dir = _workspace.directory / "origin";
  if (fs::exists(origin_dir)) {
    for (const auto& entry : fs::directory_iterator(origin_dir)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const std::string filename = entry.path().filename().string();
      if (filename.size() >= extension.size() && filename.compare(filename.size() - extension.size(), extension.size(), extension) == 0) {
        return pathString(entry.path());
      }
    }
  }
  return pathString(origin_dir / fallback_name);
}

std::string WorkspaceManager::scalarString(const json& value, const std::string& fallback) const
{
  if (value.is_string()) {
    return value.get<std::string>();
  }
  if (value.is_number_integer()) {
    return std::to_string(value.get<long long>());
  }
  if (value.is_number_unsigned()) {
    return std::to_string(value.get<unsigned long long>());
  }
  if (value.is_number_float()) {
    return std::to_string(value.get<double>());
  }
  if (value.is_boolean()) {
    return boolString(value.get<bool>());
  }
  if (value.is_array()) {
    std::string result;
    for (const auto& item : value) {
      const std::string item_string = scalarString(item, "");
      if (!item_string.empty()) {
        result += (result.empty() ? "" : " ") + item_string;
      }
    }
    return result.empty() ? fallback : result;
  }
  return fallback;
}

std::vector<std::string> WorkspaceManager::stringArray(const json& value) const
{
  std::vector<std::string> result;
  if (!value.is_array()) {
    return result;
  }
  for (const auto& item : value) {
    result.push_back(scalarString(item, ""));
  }
  return result;
}

double WorkspaceManager::numberValue(const json& value, double fallback) const
{
  if (value.is_number()) {
    return value.get<double>();
  }
  if (value.is_string()) {
    try {
      return std::stod(value.get<std::string>());
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

int WorkspaceManager::intValue(const json& value, int fallback) const
{
  if (value.is_number_integer()) {
    return value.get<int>();
  }
  if (value.is_number_float()) {
    return static_cast<int>(value.get<double>());
  }
  if (value.is_string()) {
    try {
      return std::stoi(value.get<std::string>());
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

json WorkspaceManager::defaultFlowConfig(const WorkspaceStep& step) const
{
  return {{"ConfigPath",
           {{"idb_path", workspaceConfigPath(mapValue(step.config, "db"))},
            {"ifp_path", workspaceConfigPath(mapValue(step.config, kFloorplan))},
            {"ipl_path", workspaceConfigPath(mapValue(step.config, kPlace))},
            {"irt_path", workspaceConfigPath(mapValue(step.config, kRoute))},
            {"idrc_path", workspaceConfigPath(mapValue(step.config, kDrc))},
            {"icts_path", workspaceConfigPath(mapValue(step.config, kCts))},
            {"ito_path", workspaceConfigPath(mapValue(step.config, kOptDrv))},
            {"ipnp_path", workspaceConfigPath(mapValue(step.config, kPnp))}}}};
}

json WorkspaceManager::defaultDbConfig(const WorkspaceStep& step) const
{
  return {{"INPUT",
           {{"tech_lef_path", pdkConfigPath(_workspace.pdk.tech_lef)},
            {"lef_paths", pdkConfigPaths(_workspace.pdk.lef_paths)},
            {"def_path", workspaceConfigPath(mapValue(step.input, "def"))},
            {"verilog_path", workspaceConfigPath(mapValue(step.input, "verilog"))},
            {"lib_path", pdkConfigPaths(_workspace.pdk.lib_paths)},
            {"sdc_path", workspaceConfigPath(getValue("pdk.sdc"))},
            {"spef", workspaceConfigPath(getValue("pdk.spef"))}}},
          {"OUTPUT", {{"output_dir_path", workspaceConfigPath(mapValue(step.output, "dir"))}}},
          {"LayerSettings", {{"routing_layer_1st", scalarString(_workspace.parameters.value("Bottom layer", ""), "")}}}};
}

json WorkspaceManager::defaultFloorplanConfig() const
{
  return {{"Floorplan",
           {{"Tap distance", 58},
            {"Auto place pin", {{"layer", "MET3"}, {"width", 300}, {"height", 600}, {"sides", json::array()}}},
            {"Tracks",
             json::array({{{"layer", "MET1"}, {"x start", 0}, {"x step", 200}, {"y start", 0}, {"y step", 200}},
                          {{"layer", "MET2"}, {"x start", 0}, {"x step", 200}, {"y start", 0}, {"y step", 200}},
                          {{"layer", "MET3"}, {"x start", 0}, {"x step", 200}, {"y start", 0}, {"y step", 200}},
                          {{"layer", "MET4"}, {"x start", 0}, {"x step", 200}, {"y start", 0}, {"y step", 200}},
                          {{"layer", "MET5"}, {"x start", 0}, {"x step", 200}, {"y start", 0}, {"y step", 200}},
                          {{"layer", "T4M2"}, {"x start", 0}, {"x step", 800}, {"y start", 0}, {"y step", 800}},
                          {{"layer", "RDL"}, {"x start", 0}, {"x step", 5000}, {"y start", 0}, {"y step", 5000}}})}}},
          {"PDN",
           {{"IO",
             json::array({{{"net name", "VDD"}, {"direction", "INOUT"}, {"is power", true}},
                          {{"net name", "VDDIO"}, {"direction", "INOUT"}, {"is power", true}},
                          {{"net name", "VSS"}, {"direction", "INOUT"}, {"is power", false}},
                          {{"net name", "VSSIO"}, {"direction", "INOUT"}, {"is power", false}}})},
            {"Global connect",
             json::array({{{"net name", "VDD"}, {"instance pin name", "VDD"}, {"is power", true}},
                          {{"net name", "VDD"}, {"instance pin name", "VDD1"}, {"is power", true}},
                          {{"net name", "VDD"}, {"instance pin name", "VNW"}, {"is power", true}},
                          {{"net name", "VDDIO"}, {"instance pin name", "VDDIO"}, {"is power", true}},
                          {{"net name", "VSS"}, {"instance pin name", "VSS"}, {"is power", false}},
                          {{"net name", "VSS"}, {"instance pin name", "VSS1"}, {"is power", false}},
                          {{"net name", "VSS"}, {"instance pin name", "VPW"}, {"is power", false}},
                          {{"net name", "VSSIO"}, {"instance pin name", "VSSIO"}, {"is power", false}}})},
            {"Grid", {{"layer", "MET1"}, {"power net", "VDD"}, {"ground net", "VSS"}, {"width", 0.16}, {"offset", 0}}},
            {"Stripe",
             json::array({{{"layer", "MET4"}, {"power net", "VDD"}, {"ground net", "VSS"}, {"width", 1}, {"pitch", 16}, {"offset", 0.5}},
                          {{"layer", "MET5"}, {"power net", "VDD"}, {"ground net", "VSS"}, {"width", 1}, {"pitch", 16}, {"offset", 0.5}}})},
            {"Connect layers", json::array({{{"layers", json::array({"MET1", "MET4"})}}, {{"layers", json::array({"MET4", "MET5"})}}})}}}};
}

json WorkspaceManager::defaultNoConfig(const WorkspaceStep& step) const
{
  const std::string insert_buffer = _workspace.pdk.buffers.empty() ? "" : _workspace.pdk.buffers.front();
  return {{"file_path",
           {{"design_work_space", workspaceConfigPath(mapValue(step.data, kFixFanout))},
            {"sdc_file", workspaceConfigPath(getValue("pdk.sdc"))},
            {"lib_files", pdkConfigPaths(_workspace.pdk.lib_paths)},
            {"lef_files", pdkConfigPaths(_workspace.pdk.lef_paths)},
            {"def_file", workspaceConfigPath(mapValue(step.input, "def"))},
            {"output_def", workspaceConfigPath(mapValue(step.output, "def"))},
            {"report_file", workspaceConfigPath(mapValue(step.report, "step"))},
            {"gds_file", workspaceConfigPath(mapValue(step.output, "gds"))}}},
          {"insert_buffer", insert_buffer},
          {"max_fanout", intValue(_workspace.parameters.value("Max fanout", 30), 30)}};
}

json WorkspaceManager::defaultPlConfig() const
{
  return {{"PL",
           {{"is_max_length_opt", 0},
            {"max_length_constraint", 1000000},
            {"is_timing_effort", 0},
            {"is_congestion_effort", 0},
            {"ignore_net_degree", 100},
            {"num_threads", 1},
            {"info_iter_num", 10},
            {"GP",
             {{"global_right_padding", intValue(_workspace.parameters.value("Global right padding", 0), 0)},
              {"Wirelength", {{"init_wirelength_coef", 0.25}, {"reference_hpwl", 446000000}, {"min_wirelength_force_bar", -300}}},
              {"Density",
               {{"target_density", 0.8}, {"is_adaptive_bin", 1}, {"bin_cnt_x", 128}, {"bin_cnt_y", 128}}},
              {"Nesterov",
               {{"max_iter", 2000},
                {"max_backtrack", 10},
                {"init_density_penalty", 0.00008},
                {"target_overflow", numberValue(_workspace.parameters.value("Target overflow", 0.1), 0.1)},
                {"initial_prev_coordi_update_coef", 100},
                {"min_precondition", 1.0},
                {"min_phi_coef", 0.95},
                {"max_phi_coef", 1.05}}}}},
            {"BUFFER", {{"max_buffer_num", 10000}, {"buffer_type", _workspace.pdk.buffers}}},
            {"LG", {{"max_displacement", 1000000}, {"global_right_padding", intValue(_workspace.parameters.value("Global right padding", 0), 0)}}},
            {"DP",
             {{"max_displacement", 1000000}, {"global_right_padding", intValue(_workspace.parameters.value("Global right padding", 0), 0)}, {"enable_networkflow", 0}}},
            {"Filler", {{"first_iter", _workspace.pdk.fillers}, {"second_iter", _workspace.pdk.fillers}, {"min_filler_width", 1}}}}}};
}

json WorkspaceManager::defaultCtsConfig() const
{
  return {{"skew_bound", "0.08"},
          {"max_buf_tran", "0.5"},
          {"root_input_slew", "0.0"},
          {"max_sink_tran", "0.5"},
          {"max_cap", "0.15"},
          {"max_fanout", "32"},
          {"max_length", "300"},
          {"wirelength_iterations", "3"},
          {"slew_steps", "10"},
          {"cap_steps", "10"},
          {"routing_layer", json::array({4, 5})},
          {"buffer_type", _workspace.pdk.buffers},
          {"use_netlist", "OFF"},
          {"net_list", json::array()}};
}

json WorkspaceManager::defaultRtConfig(const WorkspaceStep& step) const
{
  return {{"RT",
           {{"-temp_directory_path", workspaceConfigPath(mapValue(step.data, kRoute))},
            {"-bottom_routing_layer", scalarString(_workspace.parameters.value("Bottom layer", ""), "")},
            {"-top_routing_layer", scalarString(_workspace.parameters.value("Top layer", ""), "")},
            {"-thread_number", "50"},
            {"-enable_timing", "0"},
            {"-output_csv", "0"},
            {"-output_inter_result", "0"}}}};
}

json WorkspaceManager::defaultDrcConfig() const
{
  return json::object();
}

json WorkspaceManager::defaultPnpConfig() const
{
  return {{"timing", {{"design_workspace", ""}}},
          {"power", {{"power_net_name", ""}}},
          {"egr", {{"map_path", ""}}},
          {"grid",
           {{"power_layers", json::array()},
            {"follow_pin_layers", json::array()},
            {"follow_pin_width", 480.0},
            {"power_port_layer", ""},
            {"ho_region_num", 1},
            {"ver_region_num", 1}}},
          {"simulated_annealing",
           {{"initial_temp", 100.0},
            {"cooling_rate", 0.95},
            {"min_temp", 0.1},
            {"iterations_per_temp", 10},
            {"ir_drop_weight", 0.6},
            {"overflow_weight", 0.4},
            {"modifiable_layer_min", ""},
            {"modifiable_layer_max", ""}}},
          {"templates",
           {{"horizontal", json::array({{{"width", 1600.0}, {"pg_offset", 6800.0}, {"space", 13600.0}, {"offset", 1200.0}}})},
            {"vertical", json::array({{{"width", 1600.0}, {"pg_offset", 6800.0}, {"space", 13600.0}, {"offset", 1200.0}}})},
            {"layer_specific", json::object()}}}};
}

json WorkspaceManager::defaultToConfig(const std::string& type) const
{
  return {{"file_path",
           {{"design_work_space", ""},
            {"sdc_file", workspaceConfigPath(getValue("pdk.sdc"))},
            {"lib_files", pdkConfigPaths(_workspace.pdk.lib_paths)},
            {"lef_files", pdkConfigPaths(_workspace.pdk.lef_paths)},
            {"def_file", ""},
            {"output_def", ""},
            {"report_file", ""},
            {"gds_file", ""}}},
          {"specific_prefix",
           {{"drv", {{"make_buffer", "drv_buffer_"}, {"make_net", "drv_net_"}}},
            {"hold", {{"make_buffer", "hold_buffer_"}, {"make_net", "hold_net_"}}},
            {"setup", {{"make_buffer", "setup_buffer_"}, {"make_net", "setup_net_"}}}}},
          {"routing_tree", "flute"},
          {"setup_target_slack", 0.0},
          {"hold_target_slack", 0.4},
          {"max_insert_instance_percent", 0.2},
          {"max_core_utilization", 0.8},
          {"fix_fanout", false},
          {"optimize_drv", type == "drv"},
          {"optimize_hold", type == "hold"},
          {"optimize_setup", type == "setup"},
          {"DRV_insert_buffers", _workspace.pdk.buffers},
          {"setup_insert_buffers", _workspace.pdk.buffers},
          {"hold_insert_buffers", _workspace.pdk.buffers},
          {"number_of_decreasing_slack_iter", 5},
          {"max_allowed_buffering_fanout", 20},
          {"min_divide_fanout", 8},
          {"optimize_endpoints_percent", 1.0},
          {"drv_optimize_iter_number", 5}};
}

json WorkspaceManager::defaultRcxConfig(const WorkspaceStep& step) const
{
  json corners = _workspace.pdk.corners;
  json spefs = json::array();
  for (auto& corner : corners) {
    for (const auto& key : {"ecc_tf", "itf_file", "captab_file"}) {
      if (corner.contains(key)) {
        corner[key] = pdkConfigPath(scalarString(corner[key], ""));
      }
    }

    const std::string name = scalarString(corner.value("name", ""), "");
    if (!name.empty()) {
      const std::string spef = workspaceConfigPath(mapValue(step.output, "dir") + "/" + _workspace.design_name + "_" + name + ".spef");
      corner["spef_file"] = spef;
      spefs.push_back(spef);
    }
  }

  return {{"output", workspaceConfigPath(mapValue(step.output, "dir"))},
          {"mapping_file", pdkConfigPath(_workspace.pdk.mapping_file)},
          {"corners", corners},
          {"spef", spefs}};
}

json WorkspaceManager::buildSubflow(const std::string& step_name) const
{
  std::vector<std::string> names;
  if (step_name == kFloorplan) {
    names = {"load data", "init floorplan", "create tracks", "place io pins", "tap cell", "PDN", "set clock net", "save data"};
  } else if (step_name == kFixFanout) {
    names = {"load data", "run net optimization", "save data"};
  } else if (step_name == kPlace) {
    names = {"load data", "run placement", "save data"};
  } else if (step_name == kCts) {
    names = {"load data", "run CTS", "save data"};
  } else if (step_name == kLegalization) {
    names = {"load data", "run legalization", "save data"};
  } else if (step_name == kRoute) {
    names = {"load data", "run routing", "save data"};
  } else if (step_name == kDrc) {
    names = {"load data", "run DRC", "save data"};
  } else if (step_name == kFiller) {
    names = {"load data", "run filler", "save data"};
  }

  json steps = json::array();
  for (const auto& name : names) {
    steps.push_back({{"name", name}, {"state", "Unstart"}, {"runtime", ""}, {"info", json::object()}});
  }
  return {{"steps", steps}};
}

json WorkspaceManager::buildChecklist() const
{
  return {{"checklist", json::array()}};
}

}  // namespace ieda_workspace
