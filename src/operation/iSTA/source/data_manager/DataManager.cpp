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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "DataManager.hpp"

#include "Logger.hpp"
#include "Monitor.hpp"
#include "STAInterface.hpp"
#include "Utility.hpp"

namespace ista {

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
    STALOG.error(Loc::current(), "The instance not initialized!");
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
  STALOG.info(Loc::current(), "Starting...");
  STAI.input(config_map);
  buildConfig();
  buildDatabase();
  printConfig();
  printDatabase();
  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::output()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");
  STAI.output();
  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

DataManager* DataManager::_dm_instance = nullptr;

#if 1  // build

void DataManager::buildConfig()
{
  /////////////////////////////////////////////
  // **********        STA        ********** //
  _config.temp_directory_path = std::filesystem::absolute(_config.temp_directory_path);
  _config.temp_directory_path += "/";
  _config.log_file_path = _config.temp_directory_path + "sta.log";
  _config.path_report_number = std::max(_config.path_report_number, 1);
  _config.endpoint_path_report_number = std::max(_config.endpoint_path_report_number, 1);
  _config.timing_path_limit = std::max(_config.timing_path_limit, 0);
  // **********    DataManager    ********** //
  _config.dm_temp_directory_path = _config.temp_directory_path + "data_manager/";
  // **********   GraphBuilder    ********** //
  _config.gb_temp_directory_path = _config.temp_directory_path + "graph_builder/";
  // ********* DelayCalculator   ********* //
  _config.dc_temp_directory_path = _config.temp_directory_path + "delay_calculator/";
  // ******** ClockPropagator    ********* //
  _config.cp_temp_directory_path = _config.temp_directory_path + "clock_propagator/";
  // ********* TimingPropagator   ********* //
  _config.tp_temp_directory_path = _config.temp_directory_path + "timing_propagator/";
  // ********* PowerPropagator    ********* //
  _config.pp_temp_directory_path = _config.temp_directory_path + "power_propagator/";
  // ********** TimingAnalyzer   ********* //
  _config.ta_temp_directory_path = _config.temp_directory_path + "timing_analyzer/";
  // ********** PowerAnalyzer    ********* //
  _config.pa_temp_directory_path = _config.temp_directory_path + "power_analyzer/";
  // ******* TimingCharacterizer ******* //
  _config.tc_temp_directory_path = _config.temp_directory_path + "timing_characterizer/";
  // **********  TimingReporter   ********** //
  _config.tr_temp_directory_path = _config.temp_directory_path + "timing_reporter/";
  // **********  PowerReporter    ********** //
  _config.pr_temp_directory_path = _config.temp_directory_path + "power_reporter/";
  // ************  SDFWriter  ************* //
  _config.sw_temp_directory_path = _config.temp_directory_path + "sdf_writer/";
  /////////////////////////////////////////////
  // **********        STA        ********** //
  STAUTIL.removeDir(_config.temp_directory_path);
  STAUTIL.createDir(_config.temp_directory_path);
  STAUTIL.createDirByFile(_config.log_file_path);
  // **********    DataManager    ********** //
  STAUTIL.createDir(_config.dm_temp_directory_path);
  // **********   GraphBuilder    ********** //
  STAUTIL.createDir(_config.gb_temp_directory_path);
  // ********* DelayCalculator   ********* //
  STAUTIL.createDir(_config.dc_temp_directory_path);
  // ******** ClockPropagator    ********* //
  STAUTIL.createDir(_config.cp_temp_directory_path);
  // ********* TimingPropagator   ********* //
  STAUTIL.createDir(_config.tp_temp_directory_path);
  // ********* PowerPropagator    ********* //
  STAUTIL.createDir(_config.pp_temp_directory_path);
  // ********** TimingAnalyzer   ********* //
  STAUTIL.createDir(_config.ta_temp_directory_path);
  // ********** PowerAnalyzer    ********* //
  STAUTIL.createDir(_config.pa_temp_directory_path);
  // ******* TimingCharacterizer ******* //
  STAUTIL.createDir(_config.tc_temp_directory_path);
  // **********  TimingReporter   ********** //
  STAUTIL.createDir(_config.tr_temp_directory_path);
  // **********  PowerReporter    ********** //
  STAUTIL.createDir(_config.pr_temp_directory_path);
  // ************  SDFWriter  ************* //
  STAUTIL.createDir(_config.sw_temp_directory_path);
  /////////////////////////////////////////////
  STALOG.openLogFileStream(_config.log_file_path);
}

void DataManager::buildDatabase()
{
  buildInstanceList();
  buildNetList();
  buildInstanceTimingInfo();
  readConstraint();
}

void DataManager::buildInstanceList()
{
  makeInstanceList();
}

void DataManager::makeInstanceList()
{
  Database& database = _database;
  for (std::pair<const std::string, Instance>& instance_pair : database.get_instance_map()) {
    instance_pair.second.get_pin_name_list().clear();
  }

  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    Pin& pin = pin_pair.second;
    if (!isInstancePin(pin)) {
      continue;
    }

    makeUniqueName(database.get_instance_map()[pin.get_instance_name()].get_pin_name_list(), pin_pair.first);
  }
}

void DataManager::buildInstanceTimingInfo()
{
  Database& database = _database;
  for (std::pair<const std::string, Instance>& instance_pair : database.get_instance_map()) {
    makeInstanceTimingInfo(instance_pair.second);
  }
}

void DataManager::makeInstanceTimingInfo(Instance& instance)
{
  Database& database = _database;
  std::map<std::string, TimingCell>& timing_cell_map = database.get_timing_library().get_cell_map();
  if (timing_cell_map.count(instance.get_cell_name()) == 0) {
    return;
  }

  TimingCell& timing_cell = timing_cell_map[instance.get_cell_name()];
  instance.set_is_sequential(timing_cell.get_is_sequential());
  instance.set_is_clock_gating(timing_cell.get_is_clock_gating());
  instance.set_has_clear_arc(timing_cell.get_has_clear_arc());
  instance.set_has_preset_arc(timing_cell.get_has_preset_arc());
  TimingCellArc* clock_to_q_arc = findClockToQArc(timing_cell);
  if (clock_to_q_arc != nullptr) {
    instance.set_output_pin_name(getInstancePinName(instance, clock_to_q_arc->get_sink_port()));
    instance.set_clock_to_q_delay(clock_to_q_arc->get_delay());
    instance.set_clock_to_q_arc(*clock_to_q_arc);
  } else {
    instance.set_output_pin_name(findOutputPinName(instance, timing_cell));
  }
  instance.get_check_arc_list().clear();
  for (TimingCheckArc& timing_check_arc : timing_cell.get_check_arc_list()) {
    instance.get_check_arc_list().push_back(makeInstanceTimingCheckArc(instance, timing_check_arc));
  }

  TimingCheckArc* representative_check_arc = nullptr;
  for (TimingCheckArc& timing_check_arc : timing_cell.get_check_arc_list()) {
    if (representative_check_arc == nullptr || timing_check_arc.get_check_type() == TimingCheckType::kSetup) {
      representative_check_arc = &timing_check_arc;
    }
    if (timing_check_arc.get_check_type() == TimingCheckType::kSetup) {
      break;
    }
  }
  if (representative_check_arc != nullptr) {
    instance.set_clock_pin_name(getInstancePinName(instance, representative_check_arc->get_clock_port()));
    instance.set_data_pin_name(getInstancePinName(instance, representative_check_arc->get_data_port()));
  }
}

TimingCheckArc DataManager::makeInstanceTimingCheckArc(Instance& instance, TimingCheckArc& timing_check_arc)
{
  TimingCheckArc instance_timing_check_arc;
  instance_timing_check_arc.set_clock_port(getInstancePinName(instance, timing_check_arc.get_clock_port()));
  instance_timing_check_arc.set_data_port(getInstancePinName(instance, timing_check_arc.get_data_port()));
  instance_timing_check_arc.set_check_type(timing_check_arc.get_check_type());
  instance_timing_check_arc.set_check_time(timing_check_arc.get_check_time());
  instance_timing_check_arc.set_timing_arc_list(timing_check_arc.get_timing_arc_list());
  instance_timing_check_arc.set_clock_trans_type(timing_check_arc.get_clock_trans_type());
  return instance_timing_check_arc;
}

TimingCellArc* DataManager::findClockToQArc(TimingCell& timing_cell)
{
  for (TimingCellArc& timing_cell_arc : timing_cell.get_cell_arc_list()) {
    if (timing_cell_arc.get_is_clock_arc()) {
      return &timing_cell_arc;
    }
  }
  return nullptr;
}

std::string DataManager::getInstancePinName(Instance& instance, std::string& port_name)
{
  return instance.get_instance_name() + ":" + port_name;
}

std::string DataManager::findOutputPinName(Instance& instance, TimingCell& timing_cell)
{
  for (auto& [port_name, timing_cell_port] : timing_cell.get_port_map()) {
    if (timing_cell_port.get_is_output() && !timing_cell_port.get_is_clock()) {
      return getInstancePinName(instance, timing_cell_port.get_port_name());
    }
  }
  return "";
}

bool DataManager::isInstancePin(Pin& pin)
{
  return !pin.get_is_port();
}

void DataManager::makeUniqueName(std::vector<std::string>& list, const std::string& value)
{
  if (!STAUTIL.exist(list, value)) {
    list.push_back(value);
  }
}

void DataManager::buildNetList()
{
  makeNetList();
}

void DataManager::makeNetList()
{
  Database& database = _database;
  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    pin_pair.second.get_net_name().clear();
  }

  for (std::pair<const std::string, Net>& net_pair : database.get_net_map()) {
    makeNet(net_pair.first, net_pair.second);
  }
}

void DataManager::makeNet(const std::string& net_name, Net& net)
{
  Database& database = _database;
  net.get_driver_pin().clear();
  net.get_driver_pin_list().clear();
  net.get_load_pin_list().clear();

  for (std::string& pin_name : net.get_pin_name_list()) {
    Pin& pin = database.get_pin_map()[pin_name];
    pin.set_net_name(net_name);
    makeUniqueName(net.get_load_pin_list(), pin_name);
  }
}

void DataManager::readConstraint()
{
  Database& database = _database;
  std::string sdc_file_path = database.get_timing_constraint().get_sdc_file_path();
  database.get_timing_constraint().get_clock_map().clear();
  database.get_timing_constraint().get_port_constraint_map().clear();
  database.get_timing_constraint().get_case_analysis_map().clear();
  if (sdc_file_path.empty()) {
    return;
  }

  std::vector<std::vector<std::string>> command_list = readCommandList(sdc_file_path);
  for (std::vector<std::string>& token_list : command_list) {
    parseCommand(token_list);
  }
}

std::vector<std::vector<std::string>> DataManager::readCommandList(std::string& sdc_file_path)
{
  std::ifstream sdc_file(sdc_file_path);
  std::string content;
  std::string line;
  while (std::getline(sdc_file, line)) {
    std::string command_line = removeComment(line);
    bool is_continue = !command_line.empty() && command_line.back() == '\\';
    if (is_continue) {
      command_line.pop_back();
    }
    content += command_line;
    content += is_continue ? " " : "\n";
  }

  std::vector<std::string> token_list = tokenizeSdc(content);
  std::vector<std::vector<std::string>> command_list;
  std::vector<std::string> command_token_list;
  for (std::string& token : token_list) {
    if (token == "\n" || token == ";") {
      if (!command_token_list.empty()) {
        command_list.push_back(command_token_list);
        command_token_list.clear();
      }
    } else {
      command_token_list.push_back(token);
    }
  }
  if (!command_token_list.empty()) {
    command_list.push_back(command_token_list);
  }
  return resolveCommandList(command_list);
}

std::vector<std::vector<std::string>> DataManager::resolveCommandList(std::vector<std::vector<std::string>>& command_list)
{
  std::map<std::string, std::string> variable_map;
  std::vector<std::vector<std::string>> resolved_command_list;
  for (std::vector<std::string>& token_list : command_list) {
    if (token_list.empty()) {
      continue;
    }
    if (token_list.front() == "set") {
      updateVariableMap(token_list, variable_map);
      continue;
    }
    std::vector<std::string> resolved_token_list = resolveCommandTokenList(token_list, variable_map);
    if (!resolved_token_list.empty()) {
      resolved_command_list.push_back(resolved_token_list);
    }
  }
  return resolved_command_list;
}

std::vector<std::string> DataManager::resolveCommandTokenList(std::vector<std::string>& token_list, std::map<std::string, std::string>& variable_map)
{
  std::vector<std::string> resolved_token_list;
  for (std::size_t i = 0; i < token_list.size(); i++) {
    if (token_list[i].empty()) {
      continue;
    }
    if (token_list[i].front() == '[') {
      resolved_token_list.push_back(resolveBracketCommand(token_list, i, variable_map));
      continue;
    }
    resolved_token_list.push_back(resolveVariableToken(token_list[i], variable_map));
  }
  return resolved_token_list;
}

void DataManager::updateVariableMap(std::vector<std::string>& token_list, std::map<std::string, std::string>& variable_map)
{
  if (token_list.size() < 3) {
    return;
  }
  std::string variable_name = token_list[1];
  std::string variable_value;
  if (!token_list[2].empty() && token_list[2].front() == '[') {
    std::size_t token_idx = 2;
    variable_value = resolveBracketCommand(token_list, token_idx, variable_map);
  } else {
    variable_value = getTokenListString(token_list, 2);
    variable_value = resolveVariableToken(variable_value, variable_map);
  }
  variable_map[variable_name] = variable_value;
}

std::string DataManager::resolveBracketCommand(std::vector<std::string>& token_list, std::size_t& token_idx, std::map<std::string, std::string>& variable_map)
{
  std::vector<std::string> bracket_token_list = getBracketTokenList(token_list, token_idx, variable_map);
  if (bracket_token_list.empty()) {
    return "";
  }
  if (bracket_token_list.front() == "expr") {
    return evalExpr(bracket_token_list);
  }
  if (bracket_token_list.front() == "get_ports" || bracket_token_list.front() == "get_pins" || bracket_token_list.front() == "get_clocks") {
    return getTokenListString(bracket_token_list, 1);
  }
  return getTokenListString(bracket_token_list, 0);
}

std::vector<std::string> DataManager::getBracketTokenList(std::vector<std::string>& token_list, std::size_t& token_idx,
                                                          std::map<std::string, std::string>& variable_map)
{
  std::vector<std::string> bracket_token_list;
  for (std::size_t i = token_idx; i < token_list.size(); i++) {
    std::string token = token_list[i];
    std::size_t left_bracket_num = std::count(token.begin(), token.end(), '[');
    std::size_t right_bracket_num = std::count(token.begin(), token.end(), ']');
    bool is_end = right_bracket_num > left_bracket_num;
    if (i == token_idx && !token.empty() && token.front() == '[') {
      token.erase(token.begin());
    }
    if (is_end) {
      token.pop_back();
    }
    if (!token.empty()) {
      bracket_token_list.push_back(resolveVariableToken(token, variable_map));
    }
    token_idx = i;
    if (is_end) {
      break;
    }
  }
  return bracket_token_list;
}

std::string DataManager::evalExpr(std::vector<std::string>& expr_token_list)
{
  return getExprValueString(calcExprValue(expr_token_list));
}

double DataManager::calcExprValue(std::vector<std::string>& expr_token_list)
{
  std::vector<double> value_list;
  std::vector<std::string> operator_list;
  for (std::size_t i = 1; i < expr_token_list.size(); i++) {
    if (isExprOperator(expr_token_list[i])) {
      operator_list.push_back(expr_token_list[i]);
    } else {
      value_list.push_back(std::stod(expr_token_list[i]));
    }
  }
  calcExprMulDiv(value_list, operator_list);
  if (value_list.empty()) {
    return 0.0;
  }
  double result = value_list.front();
  for (std::size_t i = 0; i < operator_list.size() && i + 1 < value_list.size(); i++) {
    if (operator_list[i] == "+") {
      result += value_list[i + 1];
    } else if (operator_list[i] == "-") {
      result -= value_list[i + 1];
    }
  }
  return result;
}

void DataManager::calcExprMulDiv(std::vector<double>& value_list, std::vector<std::string>& operator_list)
{
  for (std::size_t i = 0; i < operator_list.size() && i + 1 < value_list.size();) {
    if (operator_list[i] == "*" || operator_list[i] == "/") {
      if (operator_list[i] == "*") {
        value_list[i] *= value_list[i + 1];
      } else {
        value_list[i] /= value_list[i + 1];
      }
      value_list.erase(value_list.begin() + i + 1);
      operator_list.erase(operator_list.begin() + i);
      continue;
    }
    i++;
  }
}

std::string DataManager::getExprValueString(const double value)
{
  std::ostringstream oss;
  oss << std::setprecision(15) << value;
  return oss.str();
}

bool DataManager::isExprOperator(std::string& token)
{
  return token == "+" || token == "-" || token == "*" || token == "/";
}

std::string DataManager::resolveVariableToken(std::string token, std::map<std::string, std::string>& variable_map)
{
  if (token.empty()) {
    return token;
  }
  std::string resolved_token;
  for (std::size_t i = 0; i < token.size(); i++) {
    if (token[i] != '$') {
      resolved_token.push_back(token[i]);
      continue;
    }
    std::string variable_name;
    i++;
    while (i < token.size() && (std::isalnum(static_cast<unsigned char>(token[i])) || token[i] == '_')) {
      variable_name.push_back(token[i]);
      i++;
    }
    i--;
    if (variable_map.count(variable_name) > 0) {
      resolved_token += variable_map[variable_name];
    } else {
      resolved_token += "$" + variable_name;
    }
  }
  return resolved_token;
}

std::string DataManager::getTokenListString(std::vector<std::string>& token_list, std::size_t begin_idx)
{
  std::string token_list_string;
  for (std::size_t i = begin_idx; i < token_list.size(); i++) {
    if (!token_list_string.empty()) {
      token_list_string += " ";
    }
    token_list_string += token_list[i];
  }
  return token_list_string;
}

std::vector<std::string> DataManager::tokenizeSdc(std::string& content)
{
  std::vector<std::string> token_list;
  std::string token;
  bool in_brace = false;
  bool in_quote = false;
  for (char ch : content) {
    if (in_brace) {
      if (ch == '}') {
        token_list.push_back(token);
        token.clear();
        in_brace = false;
      } else {
        token.push_back(ch);
      }
      continue;
    }
    if (in_quote) {
      if (ch == '"') {
        token_list.push_back(token);
        token.clear();
        in_quote = false;
      } else {
        token.push_back(ch);
      }
      continue;
    }
    if (ch == '{') {
      if (!token.empty()) {
        token_list.push_back(token);
        token.clear();
      }
      in_brace = true;
    } else if (ch == '"') {
      if (!token.empty()) {
        token_list.push_back(token);
        token.clear();
      }
      in_quote = true;
    } else if (std::isspace(static_cast<unsigned char>(ch)) || ch == ';') {
      if (!token.empty()) {
        token_list.push_back(token);
        token.clear();
      }
      if (ch == '\n' || ch == ';') {
        token_list.emplace_back(ch == '\n' ? "\n" : ";");
      }
    } else {
      token.push_back(ch);
    }
  }
  if (!token.empty()) {
    token_list.push_back(token);
  }
  return token_list;
}

std::string DataManager::removeComment(std::string& line)
{
  std::string result;
  bool in_brace = false;
  bool in_quote = false;
  for (char ch : line) {
    if (ch == '{' && !in_quote) {
      in_brace = true;
    } else if (ch == '}' && !in_quote) {
      in_brace = false;
    } else if (ch == '"' && !in_brace) {
      in_quote = !in_quote;
    }
    if (ch == '#' && !in_brace && !in_quote) {
      break;
    }
    result.push_back(ch);
  }
  return result;
}

void DataManager::parseCommand(std::vector<std::string>& token_list)
{
  if (token_list.empty()) {
    return;
  }
  if (token_list.front() == "create_clock") {
    parseCreateClock(token_list);
  } else if (token_list.front() == "set_case_analysis") {
    parseSetCaseAnalysis(token_list);
  } else if (token_list.front() == "set_input_delay") {
    parseSetInputDelay(token_list);
  } else if (token_list.front() == "set_output_delay") {
    parseSetOutputDelay(token_list);
  } else if (token_list.front() == "set_input_transition") {
    parseSetInputTransition(token_list);
  } else if (token_list.front() == "set_load") {
    parseSetLoad(token_list);
  }
}

void DataManager::parseSetCaseAnalysis(std::vector<std::string>& token_list)
{
  if (token_list.size() < 2 || (token_list[1] != "0" && token_list[1] != "1")) {
    return;
  }
  bool case_value = token_list[1] == "1";
  std::vector<std::string> object_list = getObjectList(token_list);
  for (std::string& pin_name : resolveObjectList(object_list)) {
    _database.get_timing_constraint().get_case_analysis_map()[pin_name] = case_value;
  }
}

void DataManager::parseCreateClock(std::vector<std::string>& token_list)
{
  TimingClock timing_clock;
  timing_clock.set_clock_name(getOptionValue(token_list, "-name"));
  timing_clock.set_period(getOptionDoubleValue(token_list, "-period", 0.0));
  std::vector<std::string> object_list = getObjectList(token_list);
  std::vector<std::string> source_list = resolveObjectList(object_list);
  if (timing_clock.get_clock_name().empty() && !source_list.empty()) {
    timing_clock.set_clock_name(source_list.front());
  }
  timing_clock.set_source_list(source_list);
  timing_clock.set_rise_edge(0.0);
  timing_clock.set_fall_edge(timing_clock.get_period() / 2.0);
  updateClock(timing_clock);
}

void DataManager::parseSetInputDelay(std::vector<std::string>& token_list)
{
  const double delay_value = getCommandDoubleValue(token_list);
  const bool set_min = hasOption(token_list, "-min");
  const bool set_max = hasOption(token_list, "-max");
  std::string clock_name = getClockName(token_list);
  std::vector<std::string> object_list = getObjectList(token_list);
  for (std::string& port_name : resolveObjectList(object_list)) {
    TimingPortConstraint& port_constraint = getPortConstraint(port_name);
    port_constraint.set_clock_name(clock_name);
    if (set_min && !set_max) {
      port_constraint.set_input_delay_min(delay_value);
      port_constraint.set_has_input_delay_min(true);
    } else if (set_max && !set_min) {
      port_constraint.set_input_delay_max(delay_value);
      port_constraint.set_has_input_delay_max(true);
    } else {
      port_constraint.set_input_delay_min(delay_value);
      port_constraint.set_input_delay_max(delay_value);
      port_constraint.set_has_input_delay_min(true);
      port_constraint.set_has_input_delay_max(true);
    }
  }
}

void DataManager::parseSetOutputDelay(std::vector<std::string>& token_list)
{
  const double delay_value = getCommandDoubleValue(token_list);
  const bool set_min = hasOption(token_list, "-min");
  const bool set_max = hasOption(token_list, "-max");
  std::string clock_name = getClockName(token_list);
  std::vector<std::string> object_list = getObjectList(token_list);
  for (std::string& port_name : resolveObjectList(object_list)) {
    TimingPortConstraint& port_constraint = getPortConstraint(port_name);
    port_constraint.set_clock_name(clock_name);
    if (set_min && !set_max) {
      port_constraint.set_output_delay_min(delay_value);
      port_constraint.set_has_output_delay_min(true);
    } else if (set_max && !set_min) {
      port_constraint.set_output_delay_max(delay_value);
      port_constraint.set_has_output_delay_max(true);
    } else {
      port_constraint.set_output_delay_min(delay_value);
      port_constraint.set_output_delay_max(delay_value);
      port_constraint.set_has_output_delay_min(true);
      port_constraint.set_has_output_delay_max(true);
    }
  }
}

void DataManager::parseSetInputTransition(std::vector<std::string>& token_list)
{
  const double transition_value = getCommandDoubleValue(token_list);
  std::vector<std::string> object_list = getObjectList(token_list);
  for (std::string& port_name : resolveObjectList(object_list)) {
    TimingPortConstraint& port_constraint = getPortConstraint(port_name);
    port_constraint.set_input_transition(transition_value);
    port_constraint.set_has_input_transition(true);
  }
}

void DataManager::parseSetLoad(std::vector<std::string>& token_list)
{
  const double load_value = getCommandDoubleValue(token_list);
  std::vector<std::string> object_list = getObjectList(token_list);
  for (std::string& port_name : resolveObjectList(object_list)) {
    TimingPortConstraint& port_constraint = getPortConstraint(port_name);
    port_constraint.set_load(load_value);
    port_constraint.set_has_load(true);
  }
}

double DataManager::getCommandDoubleValue(std::vector<std::string>& token_list)
{
  for (size_t i = 1; i < token_list.size(); i++) {
    if (token_list[i].empty() || token_list[i].front() == '-') {
      if (token_list[i] == "-clock" || token_list[i] == "-name") {
        i++;
      }
      continue;
    }
    char* end = nullptr;
    double value = std::strtod(token_list[i].c_str(), &end);
    if (end != token_list[i].c_str() && *end == '\0') {
      return value;
    }
  }
  return 0.0;
}

std::string DataManager::getOptionValue(std::vector<std::string>& token_list, const std::string& option)
{
  for (size_t i = 0; i + 1 < token_list.size(); i++) {
    if (token_list[i] == option) {
      return token_list[i + 1];
    }
  }
  return "";
}

double DataManager::getOptionDoubleValue(std::vector<std::string>& token_list, const std::string& option, double default_value)
{
  std::string option_value = getOptionValue(token_list, option);
  if (option_value.empty()) {
    return default_value;
  }
  return std::stod(option_value);
}

bool DataManager::hasOption(std::vector<std::string>& token_list, const std::string& option)
{
  return STAUTIL.exist(token_list, option);
}

std::string DataManager::getClockName(std::vector<std::string>& token_list)
{
  for (std::size_t i = 0; i + 1 < token_list.size(); i++) {
    if (token_list[i] != "-clock") {
      continue;
    }
    if (isClockCollectionCommand(token_list[i + 1])) {
      return getCollectionName(token_list, i + 1);
    }
    std::string clock_name = token_list[i + 1];
    if (!clock_name.empty() && clock_name.back() == ']') {
      clock_name.pop_back();
    }
    return clock_name;
  }
  return "";
}

std::string DataManager::getCollectionName(std::vector<std::string>& token_list, std::size_t collection_idx)
{
  std::vector<std::string> name_list;
  for (std::size_t i = collection_idx + 1; i < token_list.size(); i++) {
    std::string object_name = token_list[i];
    bool is_end = false;
    if (object_name == "]") {
      break;
    }
    pushObjectName(name_list, object_name);
    if (is_end) {
      break;
    }
  }
  return name_list.empty() ? "" : name_list.front();
}

std::vector<std::string> DataManager::getObjectList(std::vector<std::string>& token_list)
{
  for (std::size_t i = 1; i < token_list.size(); i++) {
    if (!isCollectionCommand(token_list[i])) {
      continue;
    }

    std::vector<std::string> object_list;
    for (std::size_t j = i + 1; j < token_list.size(); j++) {
      std::string object_name = token_list[j];
      bool is_end = false;
      if (object_name == "]") {
        break;
      }
      pushObjectName(object_list, object_name);
      if (is_end) {
        break;
      }
    }
    return object_list;
  }

  for (auto iter = token_list.rbegin(); iter != token_list.rend(); ++iter) {
    std::size_t token_idx = std::distance(iter, token_list.rend()) - 1;
    if (!iter->empty() && iter->front() != '-' && !isCommandOptionValue(token_list, token_idx)) {
      std::vector<std::string> object_list;
      pushObjectName(object_list, *iter);
      return object_list;
    }
  }
  return {};
}

void DataManager::pushObjectName(std::vector<std::string>& object_list, std::string object_name)
{
  std::istringstream iss(object_name);
  std::string split_object_name;
  while (iss >> split_object_name) {
    object_list.push_back(getObjectName(split_object_name));
  }
}

std::string DataManager::getObjectName(std::string& object_name)
{
  if (!object_name.empty() && object_name.front() == '\\') {
    object_name.erase(object_name.begin());
  }
  return object_name;
}

bool DataManager::isCollectionCommand(std::string& token)
{
  return token == "[get_ports" || token == "get_ports" || token == "[get_pins" || token == "get_pins";
}

bool DataManager::isClockCollectionCommand(std::string& token)
{
  return token == "[get_clocks" || token == "get_clocks";
}

bool DataManager::isCommandOptionValue(std::vector<std::string>& token_list, std::size_t token_idx)
{
  if (token_idx == 0 || token_idx >= token_list.size()) {
    return false;
  }
  std::string& prev_token = token_list[token_idx - 1];
  if (prev_token == "-name" || prev_token == "-clock" || prev_token == "-period") {
    return true;
  }
  char* end = nullptr;
  std::strtod(token_list[token_idx].c_str(), &end);
  return end != token_list[token_idx].c_str() && *end == '\0';
}

std::vector<std::string> DataManager::resolveObjectList(std::vector<std::string>& object_list)
{
  Database& database = _database;
  std::vector<std::string> resolved_object_list;
  for (std::string& object_name : object_list) {
    std::string resolved_object_name = object_name;
    if (resolved_object_name.rfind("[get_ports", 0) == 0) {
      resolved_object_name = resolved_object_name.substr(10);
    }
    if (resolved_object_name.rfind("[get_pins", 0) == 0) {
      resolved_object_name = resolved_object_name.substr(9);
      std::replace(resolved_object_name.begin(), resolved_object_name.end(), '/', ':');
    }
    if (database.get_pin_map().count(resolved_object_name) > 0) {
      resolved_object_list.push_back(resolved_object_name);
      continue;
    }
    if (!resolved_object_name.empty() && resolved_object_name.back() == ']') {
      std::string trim_object_name = resolved_object_name;
      trim_object_name.pop_back();
      if (database.get_pin_map().count(trim_object_name) > 0) {
        resolved_object_list.push_back(trim_object_name);
        continue;
      }
    }
    std::replace(resolved_object_name.begin(), resolved_object_name.end(), '/', ':');
    if (database.get_pin_map().count(resolved_object_name) > 0) {
      resolved_object_list.push_back(resolved_object_name);
    }
  }
  return resolved_object_list;
}

void DataManager::updateClock(TimingClock& timing_clock)
{
  Database& database = _database;
  if (timing_clock.get_clock_name().empty()) {
    return;
  }
  database.get_timing_constraint().get_clock_map()[timing_clock.get_clock_name()] = timing_clock;
}

TimingPortConstraint& DataManager::getPortConstraint(const std::string& port_name)
{
  Database& database = _database;
  TimingPortConstraint& port_constraint = database.get_timing_constraint().get_port_constraint_map()[port_name];
  port_constraint.set_port_name(port_name);
  return port_constraint;
}

void DataManager::printConfig()
{
  /////////////////////////////////////////////
  // **********        STA        ********** //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(0), "STA_CONFIG_INPUT");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.temp_directory_path);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "thread_number");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.thread_number);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "path_report_number");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.path_report_number);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "endpoint_path_report_number");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.endpoint_path_report_number);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "timing_report_delay_type");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.timing_report_delay_type);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "timing_report_start_end_type");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.timing_report_start_end_type);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "has_timing_report_slack_lesser_than");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.has_timing_report_slack_lesser_than);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "timing_report_slack_lesser_than");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.timing_report_slack_lesser_than);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "has_timing_report_slack_greater_than");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.has_timing_report_slack_greater_than);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "timing_report_slack_greater_than");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.timing_report_slack_greater_than);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "output_timing_reports");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.output_timing_reports);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "output_timing_features");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.output_timing_features);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "timing_path_limit");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.timing_path_limit);
  // **********        STA        ********** //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(0), "STA_CONFIG_BUILD");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "log_file_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.log_file_path);
  // **********    DataManager    ********** //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "DataManager");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "dm_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.dm_temp_directory_path);
  // **********   GraphBuilder    ********** //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "GraphBuilder");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "gb_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.gb_temp_directory_path);
  // ********* DelayCalculator   ********* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "DelayCalculator");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "dc_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.dc_temp_directory_path);
  // ******** ClockPropagator    ********* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "ClockPropagator");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "cp_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.cp_temp_directory_path);
  // ********* TimingPropagator   ********* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "TimingPropagator");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "tp_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.tp_temp_directory_path);
  // ********* PowerPropagator    ********* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "PowerPropagator");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "pp_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.pp_temp_directory_path);
  // ********** TimingAnalyzer   ********* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "TimingAnalyzer");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "ta_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.ta_temp_directory_path);
  // ********** PowerAnalyzer    ********* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "PowerAnalyzer");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "pa_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.pa_temp_directory_path);
  // ******* TimingCharacterizer ******* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "TimingCharacterizer");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "tc_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.tc_temp_directory_path);
  // **********  TimingReporter   ********** //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "TimingReporter");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "tr_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.tr_temp_directory_path);
  // **********  PowerReporter    ********** //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "PowerReporter");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "pr_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.pr_temp_directory_path);
  // ************  SDFWriter  ************* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "SDFWriter");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "sw_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.sw_temp_directory_path);
  /////////////////////////////////////////////
}

void DataManager::printDatabase()
{
  std::size_t port_num = 0;
  for (std::pair<const std::string, Pin>& pin_pair : _database.get_pin_map()) {
    if (pin_pair.second.get_is_port()) {
      port_num++;
    }
  }
  /////////////////////////////////////////////
  // **********        STA        ********** //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(0), "STA_DATABASE");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "design_name");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _database.get_design_name());
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "instance_num");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _database.get_instance_map().size());
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "port_num");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), port_num);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "pin_num");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _database.get_pin_map().size());
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "net_num");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _database.get_net_map().size());
  /////////////////////////////////////////////
}

#endif

}  // namespace ista
