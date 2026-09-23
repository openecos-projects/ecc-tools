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

#include "EMTech.hpp"
#include "EMIRInterface.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "PTPXPowerReader.hpp"
#include "Utility.hpp"

namespace {

std::string trim(const std::string& text)
{
  std::size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
    begin++;
  }
  std::size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    end--;
  }
  return text.substr(begin, end - begin);
}

std::string toUpper(std::string text)
{
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return text;
}

std::string normalizeRuleName(const std::string& name)
{
  std::string result;
  for (char ch : name) {
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
      result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
  }
  return result;
}

std::vector<std::string> getRuleAliases(const std::string& name)
{
  std::vector<std::string> aliases;
  std::string normalized_name = normalizeRuleName(name);
  if (normalized_name.empty()) {
    return aliases;
  }
  aliases.push_back(normalized_name);
  std::smatch match;
  if (std::regex_match(normalized_name, match, std::regex("^M([0-9]+)$"))) {
    aliases.push_back("MET" + match.str(1));
  } else if (std::regex_match(normalized_name, match, std::regex("^MET([0-9]+)$"))) {
    aliases.push_back("M" + match.str(1));
  } else if (std::regex_match(normalized_name, match, std::regex("^V([0-9]+)$"))) {
    aliases.push_back("VIA" + match.str(1));
  } else if (std::regex_match(normalized_name, match, std::regex("^VIA([0-9]+)$"))) {
    aliases.push_back("V" + match.str(1));
  } else if (normalized_name == "TM") {
    aliases.push_back("T4M2");
  } else if (normalized_name == "T4M2") {
    aliases.push_back("TM");
  } else if (normalized_name == "TV") {
    aliases.push_back("T4V2");
  } else if (normalized_name == "T4V2") {
    aliases.push_back("TV");
  }
  std::sort(aliases.begin(), aliases.end());
  aliases.erase(std::unique(aliases.begin(), aliases.end()), aliases.end());
  return aliases;
}

bool parseDouble(const std::string& text, double& value)
{
  char* end = nullptr;
  value = std::strtod(text.c_str(), &end);
  return end != text.c_str();
}

void addMetalRule(iemir::EMTech& em_tech, const std::string& name, double em_limit_ma_per_um, double em_adjust_um)
{
  if (name.empty() || em_limit_ma_per_um <= 0.0) {
    return;
  }
  for (const std::string& alias : getRuleAliases(name)) {
    iemir::EMMetalRule rule;
    rule.set_name(alias);
    rule.set_em_limit_ma_per_um(em_limit_ma_per_um);
    rule.set_em_adjust_um(em_adjust_um);
    em_tech.get_metal_rule_map()[alias] = rule;
  }
}

void addViaRule(iemir::EMTech& em_tech, const std::string& name, double em_limit_ma, double reference_area_um2)
{
  if (name.empty() || em_limit_ma <= 0.0) {
    return;
  }
  for (const std::string& alias : getRuleAliases(name)) {
    iemir::EMViaRule rule;
    rule.set_name(alias);
    rule.set_em_limit_ma(em_limit_ma);
    rule.set_reference_area_um2(reference_area_um2);
    em_tech.get_via_rule_map()[alias] = rule;
  }
}

std::vector<std::string> tokenizeRedHawkTechFile(const std::string& file_path)
{
  std::ifstream input(file_path);
  std::vector<std::string> tokens;
  std::string token;
  std::string line;
  while (std::getline(input, line)) {
    std::size_t comment_pos = line.find('#');
    if (comment_pos != std::string::npos) {
      line = line.substr(0, comment_pos);
    }
    for (char ch : line) {
      if (ch == '{' || ch == '}') {
        if (!token.empty()) {
          tokens.push_back(token);
          token.clear();
        }
        tokens.emplace_back(1, ch);
      } else if (std::isspace(static_cast<unsigned char>(ch))) {
        if (!token.empty()) {
          tokens.push_back(token);
          token.clear();
        }
      } else {
        token.push_back(ch);
      }
    }
    if (!token.empty()) {
      tokens.push_back(token);
      token.clear();
    }
  }
  return tokens;
}

}  // namespace

namespace iemir {

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
    EMIRLOG.error(Loc::current(), "The instance not initialized!");
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
  EMIRLOG.info(Loc::current(), "Starting...");

  EMIRI.input(config_map);
  buildConfig();
  buildDatabase();
  printConfig();
  printDatabase();

  EMIRLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::output()
{
  Monitor monitor;
  EMIRLOG.info(Loc::current(), "Starting...");

  EMIRLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

DataManager* DataManager::_dm_instance = nullptr;

#if 1  // build

void DataManager::buildConfig()
{
  /////////////////////////////////////////////
  // **********       EMIR        ********** //
  _config.temp_directory_path = std::filesystem::absolute(_config.temp_directory_path);
  _config.temp_directory_path += "/";
  if (!_config.redhawk_tech_file_path.empty()) {
    _config.redhawk_tech_file_path = std::filesystem::absolute(_config.redhawk_tech_file_path);
  }
  if (!_config.ptpx_instance_power_file_path.empty()) {
    _config.ptpx_instance_power_file_path = std::filesystem::absolute(_config.ptpx_instance_power_file_path);
  }
  if (!_config.ploc_file_path.empty()) {
    _config.ploc_file_path = std::filesystem::absolute(_config.ploc_file_path);
  }
  if (!_config.redhawk_res_network_file_path.empty()) {
    _config.redhawk_res_network_file_path = std::filesystem::absolute(_config.redhawk_res_network_file_path);
  }
  if (!_config.em_limit_file_path.empty()) {
    _config.em_limit_file_path = std::filesystem::absolute(_config.em_limit_file_path);
  }
  _config.log_file_path = _config.temp_directory_path + "emir.log";
  // **********    DataManager    ********** //
  _config.dm_temp_directory_path = _config.temp_directory_path + "data_manager/";
  // **********   GraphBuilder    ********** //
  _config.gb_temp_directory_path = _config.temp_directory_path + "graph_builder/";
  // **********    IRAnalyzer     ********** //
  _config.ia_temp_directory_path = _config.temp_directory_path + "ir_analyzer/";
  // **********    EMAnalyzer     ********** //
  _config.ea_temp_directory_path = _config.temp_directory_path + "em_analyzer/";
  // **********   EMIRReporter    ********** //
  _config.er_temp_directory_path = _config.temp_directory_path + "emir_reporter/";
  /////////////////////////////////////////////
  // **********       EMIR        ********** //
  EMIRUTIL.removeDir(_config.temp_directory_path);
  EMIRUTIL.createDir(_config.temp_directory_path);
  EMIRUTIL.createDirByFile(_config.log_file_path);
  // **********    DataManager    ********** //
  EMIRUTIL.createDir(_config.dm_temp_directory_path);
  // **********   GraphBuilder    ********** //
  EMIRUTIL.createDir(_config.gb_temp_directory_path);
  // **********    IRAnalyzer     ********** //
  EMIRUTIL.createDir(_config.ia_temp_directory_path);
  // **********    EMAnalyzer     ********** //
  EMIRUTIL.createDir(_config.ea_temp_directory_path);
  // **********   EMIRReporter    ********** //
  EMIRUTIL.createDir(_config.er_temp_directory_path);
  /////////////////////////////////////////////
  EMIRLOG.openLogFileStream(_config.log_file_path);
}

void DataManager::buildDatabase()
{
  readInstancePower();
  readPowerSourceFile();
  readEMTech();
}

void DataManager::readPowerSourceFile()
{
  std::vector<PowerSource>& source_list = _database.get_power_source_list();
  source_list.clear();
  if (_config.ploc_file_path.empty()) {
    return;
  }
  if (!std::filesystem::is_regular_file(_config.ploc_file_path)) {
    EMIRLOG.error(Loc::current(), "The PLOC file is missing: ", _config.ploc_file_path);
  }
  std::ifstream input(_config.ploc_file_path);
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    line_number++;
    std::size_t comment_pos = line.find('#');
    if (comment_pos != std::string::npos) {
      line = line.substr(0, comment_pos);
    }
    line = trim(line);
    if (line.empty()) {
      continue;
    }
    std::string name;
    std::string layer_name;
    std::string type_name;
    double x_um = 0.0;
    double y_um = 0.0;
    std::istringstream iss(line);
    if (!(iss >> name >> x_um >> y_um >> layer_name >> type_name)) {
      EMIRLOG.error(Loc::current(), "Invalid PLOC record at line ", line_number, ": ", line);
    }
    type_name = toUpper(type_name);
    PowerNetType net_type = type_name == "POWER" ? PowerNetType::kPower
                                                 : (type_name == "GROUND" ? PowerNetType::kGround : PowerNetType::kNone);
    if (net_type == PowerNetType::kNone) {
      EMIRLOG.error(Loc::current(), "Invalid PLOC source type at line ", line_number, ": ", type_name);
    }
    std::string marker = net_type == PowerNetType::kPower ? "_POWER_" : "_GROUND_";
    std::size_t marker_pos = name.rfind(marker);
    PowerSource source;
    source.set_name(name);
    source.set_net_name(marker_pos == std::string::npos ? "" : name.substr(0, marker_pos));
    source.set_layer_name(layer_name);
    source.set_net_type(net_type);
    source.set_x(static_cast<int32_t>(std::llround(x_um * _database.get_micron_dbu())));
    source.set_y(static_cast<int32_t>(std::llround(y_um * _database.get_micron_dbu())));
    source_list.push_back(source);
  }
  if (source_list.empty()) {
    EMIRLOG.error(Loc::current(), "The PLOC file does not contain any source: ", _config.ploc_file_path);
  }
  EMIRLOG.info(Loc::current(), "Loaded ", source_list.size(), " source locations from ", _config.ploc_file_path);
}

void DataManager::readInstancePower()
{
  if (_config.ptpx_instance_power_file_path.empty()) {
    EMIRLOG.error(Loc::current(), "ptpx_instance_power_file_path is required!");
  }
  std::vector<PTPXPowerRecord> records;
  try {
    records = PTPXPowerReader::read(_config.ptpx_instance_power_file_path);
  } catch (const std::exception& error) {
    EMIRLOG.error(Loc::current(), error.what());
  }

  std::map<uint64_t, InstancePower>& instance_power_map = _database.get_instance_power_map();
  std::map<std::string, uint64_t>& instance_name_to_id_map = _database.get_instance_name_to_id_map();
  instance_power_map.clear();
  std::size_t unknown_record_num = 0;
  double report_total_power = 0.0;
  double matched_total_power = 0.0;
  for (const PTPXPowerRecord& record : records) {
    report_total_power += record.total_power;
    auto instance_iter = instance_name_to_id_map.find(record.instance_name);
    if (instance_iter == instance_name_to_id_map.end() && !record.instance_name.empty() && record.instance_name.front() == '\\') {
      instance_iter = instance_name_to_id_map.find(record.instance_name.substr(1));
    }
    if (instance_iter == instance_name_to_id_map.end()) {
      unknown_record_num++;
      continue;
    }
    InstancePower instance_power;
    instance_power.set_instance_id(instance_iter->second);
    instance_power.set_voltage(record.voltage);
    instance_power.set_internal_power(record.internal_power);
    instance_power.set_switching_power(record.switching_power);
    instance_power.set_leakage_power(record.leakage_power);
    instance_power.set_average_current(record.average_current);
    instance_power_map[instance_iter->second] = std::move(instance_power);
    matched_total_power += record.total_power;
  }
  if (instance_power_map.empty() || report_total_power <= 0.0 || matched_total_power <= 0.0) {
    EMIRLOG.error(Loc::current(), "No non-zero PT-PX instance power matched the DEF design.");
  }
  double power_coverage = 100.0 * matched_total_power / report_total_power;
  EMIRLOG.info(Loc::current(), "Loaded shared PT-PX power: matched_instances=", instance_power_map.size(), ", unknown_records=",
               unknown_record_num, ", power_coverage=", power_coverage, "% from ", _config.ptpx_instance_power_file_path);
  if (power_coverage < 95.0) {
    EMIRLOG.error(Loc::current(), "PT-PX instance power coverage is below 95%: ", power_coverage, "%");
  }
}

void DataManager::readEMTech()
{
  _database.get_em_tech().clear();
  if (_config.redhawk_tech_file_path.empty() && _config.em_limit_file_path.empty()) {
    EMIRLOG.warn(Loc::current(), "EM tech is not configured; EM_Ratio will be reported as 0%.");
    return;
  }
  if (!_config.redhawk_tech_file_path.empty()) {
    readRedHawkTechFile(_config.redhawk_tech_file_path);
  }
  if (!_config.em_limit_file_path.empty()) {
    readEMLimitFile(_config.em_limit_file_path);
  }
  if (!_database.get_em_tech().get_has_rule()) {
    EMIRLOG.warn(Loc::current(), "No EM rule was parsed; EM_Ratio will be reported as 0%.");
    return;
  }
  EMIRLOG.info(Loc::current(), "Loaded EM tech rules from ", _database.get_em_tech().get_source_file_path(), ": metal=",
               _database.get_em_tech().get_metal_rule_map().size(), ", via=", _database.get_em_tech().get_via_rule_map().size());
}

void DataManager::readRedHawkTechFile(const std::string& redhawk_tech_file_path)
{
  if (!std::filesystem::is_regular_file(redhawk_tech_file_path)) {
    EMIRLOG.error(Loc::current(), "The RedHawk tech file is missing: ", redhawk_tech_file_path);
  }
  EMTech& em_tech = _database.get_em_tech();
  em_tech.set_source_file_path(redhawk_tech_file_path);
  std::vector<std::string> tokens = tokenizeRedHawkTechFile(redhawk_tech_file_path);
  for (std::size_t token_idx = 0; token_idx + 1 < tokens.size(); token_idx++) {
    if (toUpper(tokens[token_idx]) != "HALF_NODE_SCALE_FACTOR") {
      continue;
    }
    double scale_factor = 0.0;
    if (parseDouble(tokens[token_idx + 1], scale_factor) && scale_factor > 0.0) {
      em_tech.set_half_node_scale_factor(scale_factor);
    }
  }
  for (std::size_t token_idx = 0; token_idx + 2 < tokens.size(); token_idx++) {
    std::string block_type = toUpper(tokens[token_idx]);
    if (block_type != "METAL" && block_type != "VIA") {
      continue;
    }
    std::string rule_name = tokens[token_idx + 1];
    if (tokens[token_idx + 2] != "{") {
      continue;
    }
    std::size_t cursor = token_idx + 3;
    int32_t depth = 1;
    double em_limit = 0.0;
    double em_adjust = 0.0;
    double area = 0.0;
    while (cursor < tokens.size() && depth > 0) {
      std::string key = toUpper(tokens[cursor]);
      if (tokens[cursor] == "{") {
        depth++;
      } else if (tokens[cursor] == "}") {
        depth--;
      } else if (depth == 1 && cursor + 1 < tokens.size()) {
        double value = 0.0;
        if ((key == "EM" || key == "EM_ADJUST" || key == "AREA") && parseDouble(tokens[cursor + 1], value)) {
          if (key == "EM") {
            em_limit = value;
          } else if (key == "EM_ADJUST") {
            em_adjust = value;
          } else if (key == "AREA") {
            area = value;
          }
          cursor++;
        }
      }
      cursor++;
    }
    if (block_type == "METAL") {
      addMetalRule(em_tech, rule_name, em_limit, em_adjust);
    } else {
      addViaRule(em_tech, rule_name, em_limit, area);
    }
    if (cursor > 0) {
      token_idx = cursor - 1;
    }
  }
}

void DataManager::readEMLimitFile(const std::string& em_limit_file_path)
{
  if (!std::filesystem::is_regular_file(em_limit_file_path)) {
    EMIRLOG.error(Loc::current(), "The EM limit file is missing: ", em_limit_file_path);
  }
  EMTech& em_tech = _database.get_em_tech();
  em_tech.set_source_file_path(em_limit_file_path);
  std::ifstream input(em_limit_file_path);
  std::string line;
  while (std::getline(input, line)) {
    std::size_t comment_pos = line.find('#');
    if (comment_pos != std::string::npos) {
      line = line.substr(0, comment_pos);
    }
    line = trim(line);
    if (line.empty()) {
      continue;
    }
    std::istringstream iss(line);
    std::string name;
    double em_limit = 0.0;
    double em_adjust = 0.0;
    iss >> name >> em_limit;
    if (name.empty() || em_limit <= 0.0) {
      continue;
    }
    iss >> em_adjust;
    std::string normalized_name = normalizeRuleName(name);
    if (normalized_name.rfind("M", 0) == 0 || normalized_name.rfind("MET", 0) == 0 || normalized_name == "TM" || normalized_name == "T4M2"
        || normalized_name == "RDL") {
      addMetalRule(em_tech, name, em_limit, em_adjust);
    } else {
      addViaRule(em_tech, name, em_limit, 0.0);
    }
  }
}

#endif

#if 1  // exhibit

void DataManager::printConfig()
{
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(0), "EMIR_CONFIG_INPUT");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "temp_directory_path");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _config.temp_directory_path);
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "ptpx_instance_power_file_path");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _config.ptpx_instance_power_file_path);
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "redhawk_res_network_file_path");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _config.redhawk_res_network_file_path);
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "ploc_file_path");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _config.ploc_file_path);
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "redhawk_tech_file_path");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _config.redhawk_tech_file_path);
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "em_limit_file_path");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _config.em_limit_file_path);
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "em_violation_threshold_percent");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _config.em_violation_threshold_percent);
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "thread_number");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _config.thread_number);
}

void DataManager::printDatabase()
{
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(0), "EMIR_DATABASE_INPUT");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "design_name");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _database.get_design_name());
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "power_net_num");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _database.get_power_net_map().size());
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "instance_power_num");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _database.get_instance_power_map().size());
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "power_source_num");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _database.get_power_source_list().size());
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "em_tech_metal_rule_num");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _database.get_em_tech().get_metal_rule_map().size());
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "em_tech_via_rule_num");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _database.get_em_tech().get_via_rule_map().size());
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "half_node_scale_factor");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _database.get_em_tech().get_half_node_scale_factor());
}

#endif

}  // namespace iemir
