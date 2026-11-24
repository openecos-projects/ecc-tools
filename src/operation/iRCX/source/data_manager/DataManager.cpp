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
#include "DataManager.hpp"

#include <tuple>

#include "CapTableConfig.hpp"
#include "CapTableEntry.hpp"
#include "Corner.hpp"
#include "CornerData.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "ProcessReader.hpp"
#include "ProcessBinding.hpp"
#include "RCXHeader.hpp"
#include "RCXInterface.hpp"
#include "Utility.hpp"

namespace ircx {

// public

void DataManager::initInst()
{
  if (_dm_instance == nullptr) {
    _dm_instance = new DataManager();
  }
}

DataManager& DataManager::getInst()
{
  if (_dm_instance == nullptr) {
    RCXLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_dm_instance;
}

void DataManager::destroyInst()
{
  if (_dm_instance != nullptr) {
    delete _dm_instance;
    _dm_instance = nullptr;
  }
}

// function

void DataManager::input(std::map<std::string, std::any>& config_map)
{
  Monitor monitor;
  RCXLOG.info(Loc::current(), "Starting...");
  // Import into a fresh database. A failed source load cannot change the previous
  // design bindings or accumulate design nets when the caller retries.
  auto staged_database = std::make_unique<Database>();
  // The guard also restores state when an embedding host receives a logger error.
  struct InputTransaction
  {
    Config& config;
    std::unique_ptr<Database>& database;
    Config previous_config;
    std::unique_ptr<Database> previous_database;
    bool committed = false;
    ~InputTransaction()
    {
      if (committed)
        return;
      database = std::move(previous_database);
      config = std::move(previous_config);
      omp_set_num_threads(config.thread_number);
    }
  } transaction{_config, _database, _config, std::move(_database)};
  _database = std::move(staged_database);
  _config = Config{};
  RCXI.input(config_map);
  buildDatabase();
  buildConfig();
  printConfig();
  printDatabase();
  transaction.committed = true;
  RCXLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::output()
{
  Monitor monitor;
  RCXLOG.info(Loc::current(), "Starting...");
  RCXI.output();
  RCXLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

DataManager* DataManager::_dm_instance = nullptr;

#if 1  // build

void DataManager::buildConfig()
{
  /////////////////////////////////////////////
  // **********        RCX        ********** //
  _config.temp_directory_path = std::filesystem::absolute(_config.temp_directory_path);
  _config.temp_directory_path += "/";
  _config.log_file_path = _config.temp_directory_path + "rcx.log";
  // **********    DataManager    ********** //
  _config.dm_temp_directory_path = _config.temp_directory_path + "data_manager/";
  // ********** TopoBuilder  ********** //
  _config.tb_temp_directory_path = _config.temp_directory_path + "topo_builder/";
  // ********** EnvBuilder ******** //
  _config.eb_temp_directory_path = _config.temp_directory_path + "env_builder/";
  // ********** VarProcessor ******** //
  _config.vp_temp_directory_path = _config.temp_directory_path + "var_processor/";
  // ********** ResExtractor ****** //
  _config.re_temp_directory_path = _config.temp_directory_path + "res_extractor/";
  // ********** CapExtractor **** //
  _config.ce_temp_directory_path = _config.temp_directory_path + "cap_extractor/";
  // **********     SPEFWriter    ********** //
  _config.sw_temp_directory_path = _config.temp_directory_path + "spef_writer/";
  /////////////////////////////////////////////
  // **********        RCX        ********** //
  RCXUTIL.removeDir(_config.temp_directory_path);
  RCXUTIL.createDir(_config.temp_directory_path);
  RCXUTIL.createDirByFile(_config.log_file_path);
  // **********    DataManager    ********** //
  RCXUTIL.createDir(_config.dm_temp_directory_path);
  // ********** TopoBuilder  ********** //
  RCXUTIL.createDir(_config.tb_temp_directory_path);
  // ********** EnvBuilder ******** //
  RCXUTIL.createDir(_config.eb_temp_directory_path);
  // ********** VarProcessor ******** //
  RCXUTIL.createDir(_config.vp_temp_directory_path);
  // ********** ResExtractor ****** //
  RCXUTIL.createDir(_config.re_temp_directory_path);
  // ********** CapExtractor **** //
  RCXUTIL.createDir(_config.ce_temp_directory_path);
  // **********     SPEFWriter    ********** //
  RCXUTIL.createDir(_config.sw_temp_directory_path);
  /////////////////////////////////////////////
  RCXLOG.openLogFileStream(_config.log_file_path);
}

void DataManager::buildDatabase()
{
  if (_database->get_layout_data().get_is_empty()) {
    return;
  }

  auto corners = buildCornerDataList();
  LayerTable layers = _database->get_layer_table();
  layers.get_process_idx_to_name_map().clear();
  layers.get_process_name_to_idx_map().clear();
  layers.get_design_name_to_process_name_map().clear();
  layers.get_process_name_to_design_name_map().clear();
  const auto register_layer = [&](const std::string& name) {
    if (!layers.get_process_name_to_idx_map().contains(name))
      layers.register_process_layer(static_cast<int32_t>(layers.get_process_name_to_idx_map().size()), name);
  };
  for (const auto& corner : corners) {
    for (const auto& conductor : corner.get_process_conductor_list()) register_layer(conductor.get_layer_name());
    for (const auto& via : corner.get_process_via_list()) register_layer(via.get_layer_name());
  }
  buildLayerMapping(layers);
  validateProcessBindings(corners, layers, _database->get_layout_data());
  // Publish only after every source, temperature corner and mapping has loaded.
  _database->get_corner_data_list() = std::move(corners);
  _database->get_layer_table() = std::move(layers);
}

std::vector<CornerData> DataManager::buildCornerDataList()
{
  std::vector<CornerData> result;
  using SourceKey = std::tuple<std::string, std::size_t, std::size_t>;
  std::map<SourceKey, std::shared_ptr<const ProcessTechnology>> sources;
  for (Corner& corner : _config.corner_list) {
    const auto path = std::filesystem::weakly_canonical(corner.get_process_file_path()).string();
    const auto& options = corner.get_process_options();
    const SourceKey key{path, options.max_nesting, options.max_expression_nodes};
    auto& model = sources[key];
    if (!model) {
      auto parsed = readProcessFile(path, corner.get_process_options());
      std::ostringstream errors;
      for (const auto& diagnostic : parsed.diagnostics) {
        const auto message = diagnostic.source_name + ":" + std::to_string(diagnostic.begin.line) + ":"
            + std::to_string(diagnostic.begin.column) + " [" + diagnostic.code + "] " + diagnostic.field_path + ": " + diagnostic.message;
        if (diagnostic.severity == ProcessSeverity::kError) errors << message << '\n';
        else RCXLOG.warn(Loc::current(), message);
      }
      if (!parsed)
        RCXLOG.error(Loc::current(), errors.str());
      model = std::make_shared<const ProcessTechnology>(std::move(*parsed.technology));
    }
    CornerData prototype;
    prototype.set_process_technology(model);
    buildCapTable(prototype, corner.get_captab_file_path());
    for (double temperature : corner.get_tmpr_list()) {
      auto data = prototype;
      data.set_corner_name(getTmprCornerName(corner.get_corner_name(), temperature));
      data.set_tmpr(temperature);
      result.push_back(std::move(data));
    }
  }
  return result;
}

std::string DataManager::getTmprCornerName(std::string corner_name, double tmpr)
{
  std::ostringstream tmpr_stream;
  tmpr_stream << std::setprecision(12) << tmpr;
  std::string tmpr_name = tmpr_stream.str();
  for (char& tmpr_char : tmpr_name) {
    if (tmpr_char == '.') {
      tmpr_char = 'p';
    } else if (tmpr_char == '-') {
      tmpr_char = 'm';
    }
  }
  return RCXUTIL.getString(corner_name, "_", tmpr_name, "C");
}

void DataManager::buildCapTable(CornerData& corner_data, std::string captab_file_path)
{
  std::ifstream* captab_file_stream = RCXUTIL.getInputFileStream(captab_file_path);
  if (!captab_file_stream->is_open()) {
    RCXUTIL.closeFileStream(captab_file_stream);
    return;
  }

  std::string header;
  std::vector<std::string> data_line_list;
  std::string line;
  while (std::getline(*captab_file_stream, line)) {
    line = RCXUTIL.getTrimmedString(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    bool is_header = line.size() > 2 && (line[0] == 'A' || line[0] == 'B') && line[1] == ' ' && line.find("OVER") != std::string::npos;
    if (is_header) {
      if (!header.empty()) {
        buildCapTableConfig(corner_data, header, data_line_list);
      }
      header = line;
      data_line_list.clear();
    } else if (!header.empty()) {
      data_line_list.push_back(line);
    }
  }
  if (!header.empty()) {
    buildCapTableConfig(corner_data, header, data_line_list);
  }
  RCXUTIL.closeFileStream(captab_file_stream);
}

void DataManager::buildCapTableConfig(CornerData& corner_data, const std::string& header, const std::vector<std::string>& data_line_list)
{
  std::istringstream header_stream(header);
  std::string type_name;
  std::string layer_name;
  std::string over_keyword;
  std::string over_layer_name;
  if (!(header_stream >> type_name >> layer_name >> over_keyword >> over_layer_name)) {
    return;
  }

  CapTableConfig cap_table_config;
  cap_table_config.set_type(GetCapTableType()(type_name));
  cap_table_config.set_layer_name(layer_name);
  cap_table_config.set_over_layer_name(over_layer_name);

  std::string under_keyword;
  std::string under_layer_name;
  if (header_stream >> under_keyword >> under_layer_name && under_keyword == "UNDER") {
    cap_table_config.set_under_layer_name(under_layer_name);
  }

  for (const std::string& data_line : data_line_list) {
    std::istringstream data_stream(data_line);
    double spacing = 0.0;
    double coupling_capacitance = 0.0;
    double ground_capacitance = 0.0;
    if (!(data_stream >> spacing >> coupling_capacitance >> ground_capacitance)) {
      continue;
    }

    CapTableEntry cap_table_entry;
    cap_table_entry.set_spacing(spacing);
    cap_table_entry.set_coupling_capacitance(coupling_capacitance);
    cap_table_entry.set_ground_capacitance(ground_capacitance);
    cap_table_config.get_entry_list().push_back(std::move(cap_table_entry));
  }
  if (!cap_table_config.get_entry_list().empty()) {
    corner_data.get_cap_table_config_list().push_back(std::move(cap_table_config));
  }
}

void DataManager::buildLayerMapping(LayerTable& layer_table)
{
  std::ifstream stream(_config.mapping_file_path);
  if (!stream)
    RCXLOG.error(Loc::current(), "cannot open layer mapping file: " + _config.mapping_file_path);
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(stream, line)) {
    ++line_number;
    if (const auto comment = line.find('#'); comment != std::string::npos) line.resize(comment);
    if (const auto comment = line.find("//"); comment != std::string::npos) line.resize(comment);
    std::istringstream mapping_stream(line);
    std::string design, process, extra;
    if (!(mapping_stream >> design)) continue;
    if (!(mapping_stream >> process) || (mapping_stream >> extra))
      RCXLOG.error(Loc::current(), _config.mapping_file_path + ":" + std::to_string(line_number) + ": expected design and process layer names");
    const auto& mapping = layer_table.get_design_name_to_process_name_map();
    if (auto previous = mapping.find(design); previous != mapping.end() && previous->second != process)
      RCXLOG.error(Loc::current(), "conflicting process mapping for design layer " + design);
    layer_table.register_mapping(design, process);
  }
  if (stream.bad())
    RCXLOG.error(Loc::current(), "cannot read layer mapping file: " + _config.mapping_file_path);
}

void DataManager::printConfig()
{
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(0), "RCX_CONFIG_INPUT");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "config_file_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), _config.config_file_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "thread_number");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), _config.thread_number);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "output_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), _config.output_directory_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "report_geometry");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), _config.report_geometry);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "mapping_file_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), _config.mapping_file_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "corner_list");
  for (Corner& corner : _config.corner_list) {
    RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "corner_name");
    RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), corner.get_corner_name());
    RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "tmpr_list");
    for (double tmpr : corner.get_tmpr_list()) {
      RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), tmpr);
    }
    RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "process_file_path");
    RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), corner.get_process_file_path());
    RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "captab_file_path");
    RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), corner.get_captab_file_path());
  }
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(0), "RCX_CONFIG_BUILD");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "temp_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), _config.temp_directory_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "log_file_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), _config.log_file_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "DataManager");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "dm_temp_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), _config.dm_temp_directory_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "TopoBuilder");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "tb_temp_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), _config.tb_temp_directory_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "EnvBuilder");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "eb_temp_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), _config.eb_temp_directory_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "VarProcessor");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "vp_temp_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), _config.vp_temp_directory_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "ResExtractor");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "re_temp_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), _config.re_temp_directory_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "CapExtractor");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "ce_temp_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), _config.ce_temp_directory_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "SPEFWriter");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "sw_temp_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), _config.sw_temp_directory_path);
}

void DataManager::printDatabase()
{
  RCXLOG.info(Loc::current(), "Database");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "design_name");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), _database->get_design_name());
}

#endif

}  // namespace ircx
