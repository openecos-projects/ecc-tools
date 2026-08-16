// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "VcdParser.hh"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

namespace vcd {

namespace {

class SignalState
{
 public:
  SignalState() = default;
  ~SignalState() = default;

  void update(int64_t time, char value)
  {
    value = getCanonicalValue(value);
    if (_has_value) {
      int64_t duration = time - _last_time;
      if (duration > 0 && _last_value == '1') {
        _high_duration += static_cast<double>(duration);
      }
      if (duration > 0 && value != _last_value) {
        _transition_count += isUnknown(value) || isUnknown(_last_value) ? 0.5 : 1.0;
      }
    }
    _last_time = time;
    _last_value = value;
    _has_value = true;
  }

  bool get_has_value() const { return _has_value; }

  VcdSignalActivity getActivity(int64_t end_time, double simulation_duration) const
  {
    VcdSignalActivity activity;
    if (!_has_value || simulation_duration <= 0.0) {
      return activity;
    }

    double high_duration = _high_duration;
    if (_last_value == '1' && end_time > _last_time) {
      high_duration += static_cast<double>(end_time - _last_time);
    }
    activity.set_transition_density(_transition_count / simulation_duration);
    activity.set_static_probability(high_duration / static_cast<double>(end_time - _start_time));
    return activity;
  }

  void set_start_time(int64_t start_time) { _start_time = start_time; }

 private:
  char getCanonicalValue(char value) const
  {
    if (value == '0' || value == '1') {
      return value;
    }
    if (value == 'z' || value == 'Z') {
      return 'z';
    }
    return 'x';
  }

  bool isUnknown(char value) const { return value == 'x' || value == 'z'; }

  bool _has_value = false;
  int64_t _start_time = 0;
  int64_t _last_time = 0;
  char _last_value = 'x';
  double _transition_count = 0.0;
  double _high_duration = 0.0;
};

class VcdFileParser
{
 public:
  VcdFileParser() = default;
  ~VcdFileParser() = default;

  bool read(const std::string& file_path, std::map<std::string, VcdSignalActivity>& signal_activity_map)
  {
    std::ifstream input_file(file_path);
    if (!input_file.is_open()) {
      return false;
    }

    std::string token;
    while (input_file >> token) {
      parseToken(token, input_file);
    }
    buildSignalActivityMap(signal_activity_map);
    return true;
  }

 private:
  void parseToken(std::string& token, std::istream& input_stream)
  {
    if (token == "$timescale") {
      parseTimeScale(input_stream);
      return;
    }
    if (token == "$scope") {
      parseScope(input_stream);
      return;
    }
    if (token == "$upscope") {
      parseUpScope(input_stream);
      return;
    }
    if (token == "$var") {
      parseVariable(input_stream);
      return;
    }
    if (token == "$dumpvars" || token == "$dumpall" || token == "$dumpon" || token == "$dumpoff") {
      parseDumpValueList(input_stream);
      return;
    }
    if (!token.empty() && token.front() == '$') {
      skipDirective(input_stream);
      return;
    }
    if (!token.empty() && token.front() == '#') {
      parseTime(token);
      return;
    }
    parseValue(token, input_stream);
  }

  void parseTimeScale(std::istream& input_stream)
  {
    std::string time_scale;
    std::string token;
    while (input_stream >> token && token != "$end") {
      time_scale += token;
    }
    if (time_scale.empty()) {
      return;
    }

    char* end = nullptr;
    errno = 0;
    double resolution = std::strtod(time_scale.c_str(), &end);
    if (errno != 0 || end == time_scale.c_str()) {
      return;
    }
    std::string time_unit = end;
    std::transform(time_unit.begin(), time_unit.end(), time_unit.begin(), [](unsigned char character) { return std::tolower(character); });
    double unit_scale = getTimeUnitScale(time_unit);
    if (unit_scale <= 0.0) {
      return;
    }
    _time_resolution = resolution;
    _time_unit_scale = unit_scale;
  }

  void parseScope(std::istream& input_stream)
  {
    std::string scope_type;
    std::string scope_name;
    if (!(input_stream >> scope_type) || !(input_stream >> scope_name)) {
      return;
    }
    _scope_name_list.push_back(scope_name);
    skipDirective(input_stream);
  }

  void parseUpScope(std::istream& input_stream)
  {
    if (!_scope_name_list.empty()) {
      _scope_name_list.pop_back();
    }
    skipDirective(input_stream);
  }

  void parseVariable(std::istream& input_stream)
  {
    std::vector<std::string> token_list;
    std::string token;
    while (input_stream >> token && token != "$end") {
      token_list.push_back(token);
    }
    if (token_list.size() < 4 || (token_list[0] != "wire" && token_list[0] != "reg")) {
      return;
    }

    int32_t bit_num = getInteger(token_list[1]);
    if (bit_num <= 0) {
      return;
    }
    std::string signal_name = token_list[3];
    if (token_list.size() > 4 && !token_list[4].empty() && token_list[4].front() == '[') {
      signal_name += token_list[4];
    }
    std::string scope_path = getScopePath();
    std::string full_signal_name = scope_path.empty() ? signal_name : scope_path + "/" + signal_name;
    std::vector<std::string> bit_signal_name_list = getBitSignalNameList(full_signal_name, bit_num);
    _identifier_signal_name_list_map[token_list[2]].push_back(bit_signal_name_list);
    for (std::string& bit_signal_name : bit_signal_name_list) {
      if (_signal_state_map.count(bit_signal_name) == 0) {
        SignalState state;
        state.set_start_time(_start_time);
        _signal_state_map[bit_signal_name] = state;
      }
    }
  }

  void parseDumpValueList(std::istream& input_stream)
  {
    std::string token;
    while (input_stream >> token && token != "$end") {
      if (!token.empty() && token.front() == '#') {
        parseTime(token);
      } else {
        parseValue(token, input_stream);
      }
    }
  }

  void skipDirective(std::istream& input_stream)
  {
    std::string token;
    while (input_stream >> token && token != "$end") {
    }
  }

  void parseTime(std::string& token)
  {
    int64_t time = getInteger64(token.substr(1));
    if (!_has_time) {
      _start_time = time;
      _has_time = true;
      for (std::pair<const std::string, SignalState>& state_pair : _signal_state_map) {
        state_pair.second.set_start_time(_start_time);
      }
    }
    _current_time = time;
    _end_time = time;
  }

  void parseValue(std::string& token, std::istream& input_stream)
  {
    if (token.empty()) {
      return;
    }
    if (!_has_time) {
      parseTime(_zero_time_token);
    }

    char value_type = static_cast<char>(std::tolower(static_cast<unsigned char>(token.front())));
    if (value_type == 'b' || value_type == 'o' || value_type == 'h') {
      std::string identifier;
      if (input_stream >> identifier) {
        updateVectorValue(identifier, token);
      }
      return;
    }
    if (value_type == 'r' || value_type == 's') {
      std::string identifier;
      input_stream >> identifier;
      return;
    }
    if (value_type != '0' && value_type != '1' && value_type != 'x' && value_type != 'z') {
      return;
    }

    std::string identifier = token.substr(1);
    if (identifier.empty()) {
      input_stream >> identifier;
    }
    updateScalarValue(identifier, value_type);
  }

  void updateScalarValue(std::string& identifier, char value)
  {
    if (_identifier_signal_name_list_map.count(identifier) == 0) {
      return;
    }
    std::vector<std::vector<std::string>>& signal_name_list_list = _identifier_signal_name_list_map[identifier];
    for (std::vector<std::string>& signal_name_list : signal_name_list_list) {
      for (std::string& signal_name : signal_name_list) {
        _signal_state_map[signal_name].update(_current_time, value);
      }
    }
  }

  void updateVectorValue(std::string& identifier, std::string& value_token)
  {
    if (_identifier_signal_name_list_map.count(identifier) == 0) {
      return;
    }
    std::vector<std::vector<std::string>>& signal_name_list_list = _identifier_signal_name_list_map[identifier];
    for (std::vector<std::string>& signal_name_list : signal_name_list_list) {
      std::vector<char> value_list = getBitValueList(value_token, signal_name_list.size());
      for (std::size_t bit_idx = 0; bit_idx < signal_name_list.size(); bit_idx++) {
        _signal_state_map[signal_name_list[bit_idx]].update(_current_time, value_list[bit_idx]);
      }
    }
  }

  void buildSignalActivityMap(std::map<std::string, VcdSignalActivity>& signal_activity_map)
  {
    signal_activity_map.clear();
    if (!_has_time || _end_time <= _start_time) {
      return;
    }
    double simulation_duration = static_cast<double>(_end_time - _start_time) * _time_resolution * _time_unit_scale;
    if (simulation_duration <= 0.0) {
      return;
    }
    for (std::pair<const std::string, SignalState>& state_pair : _signal_state_map) {
      if (!state_pair.second.get_has_value()) {
        continue;
      }
      signal_activity_map[state_pair.first] = state_pair.second.getActivity(_end_time, simulation_duration);
    }
  }

  std::string getScopePath()
  {
    std::string scope_path;
    for (std::string& scope_name : _scope_name_list) {
      if (!scope_path.empty()) {
        scope_path += "/";
      }
      scope_path += scope_name;
    }
    return scope_path;
  }

  std::vector<std::string> getBitSignalNameList(std::string& signal_name, int32_t bit_num)
  {
    std::vector<std::string> signal_name_list;
    int32_t left_index = bit_num - 1;
    int32_t right_index = 0;
    if (!getBitRange(signal_name, left_index, right_index)) {
      if (bit_num == 1) {
        signal_name_list.push_back(signal_name);
        return signal_name_list;
      }
      for (int32_t bit_idx = left_index; bit_idx >= right_index; bit_idx--) {
        signal_name_list.push_back(signal_name + "[" + std::to_string(bit_idx) + "]");
      }
      return signal_name_list;
    }
    if (left_index >= right_index) {
      for (int32_t bit_idx = left_index; bit_idx >= right_index; bit_idx--) {
        signal_name_list.push_back(signal_name);
        if (bit_num > 1) {
          signal_name_list.back() = signal_name.substr(0, signal_name.find('[')) + "[" + std::to_string(bit_idx) + "]";
        }
      }
    } else {
      for (int32_t bit_idx = left_index; bit_idx <= right_index; bit_idx++) {
        signal_name_list.push_back(signal_name);
        if (bit_num > 1) {
          signal_name_list.back() = signal_name.substr(0, signal_name.find('[')) + "[" + std::to_string(bit_idx) + "]";
        }
      }
    }
    return signal_name_list;
  }

  bool getBitRange(std::string& signal_name, int32_t& left_index, int32_t& right_index)
  {
    std::size_t left_bracket = signal_name.rfind('[');
    std::size_t right_bracket = signal_name.rfind(']');
    if (left_bracket == std::string::npos || right_bracket == std::string::npos || left_bracket >= right_bracket) {
      return false;
    }
    std::string range = signal_name.substr(left_bracket + 1, right_bracket - left_bracket - 1);
    std::size_t colon = range.find(':');
    if (colon == std::string::npos) {
      int32_t index = getInteger(range);
      if (index == std::numeric_limits<int32_t>::min()) {
        return false;
      }
      left_index = index;
      right_index = index;
      return true;
    }
    left_index = getInteger(range.substr(0, colon));
    right_index = getInteger(range.substr(colon + 1));
    return left_index != std::numeric_limits<int32_t>::min() && right_index != std::numeric_limits<int32_t>::min();
  }

  std::vector<char> getBitValueList(std::string& value_token, std::size_t bit_num)
  {
    std::vector<char> raw_value_list;
    char value_type = static_cast<char>(std::tolower(static_cast<unsigned char>(value_token.front())));
    std::string value = value_token.substr(1);
    if (value_type == 'b') {
      for (char character : value) {
        raw_value_list.push_back(character);
      }
    } else if (value_type == 'o') {
      for (char character : value) {
        appendOctalValue(raw_value_list, character);
      }
    } else {
      for (char character : value) {
        appendHexValue(raw_value_list, character);
      }
    }
    if (raw_value_list.empty()) {
      raw_value_list.push_back('x');
    }
    if (raw_value_list.size() == 1 && (raw_value_list.front() == 'x' || raw_value_list.front() == 'z')) {
      raw_value_list.resize(bit_num, raw_value_list.front());
    }
    if (raw_value_list.size() > bit_num) {
      std::size_t erase_num = raw_value_list.size() - bit_num;
      raw_value_list.erase(raw_value_list.begin(), raw_value_list.begin() + erase_num);
    }
    char padding_value = raw_value_list.front() == 'x' || raw_value_list.front() == 'z' ? raw_value_list.front() : '0';
    while (raw_value_list.size() < bit_num) {
      raw_value_list.insert(raw_value_list.begin(), padding_value);
    }
    for (char& character : raw_value_list) {
      character = getCanonicalValue(character);
    }
    return raw_value_list;
  }

  void appendOctalValue(std::vector<char>& value_list, char value)
  {
    value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    if (value == 'x' || value == 'z') {
      value_list.insert(value_list.end(), 3, value);
      return;
    }
    if (value < '0' || value > '7') {
      value_list.insert(value_list.end(), 3, 'x');
      return;
    }
    int32_t number = value - '0';
    for (int32_t bit_idx = 2; bit_idx >= 0; bit_idx--) {
      value_list.push_back((number & (1 << bit_idx)) == 0 ? '0' : '1');
    }
  }

  void appendHexValue(std::vector<char>& value_list, char value)
  {
    value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    if (value == 'x' || value == 'z') {
      value_list.insert(value_list.end(), 4, value);
      return;
    }
    int32_t number = 0;
    if (value >= '0' && value <= '9') {
      number = value - '0';
    } else if (value >= 'a' && value <= 'f') {
      number = value - 'a' + 10;
    } else {
      value_list.insert(value_list.end(), 4, 'x');
      return;
    }
    for (int32_t bit_idx = 3; bit_idx >= 0; bit_idx--) {
      value_list.push_back((number & (1 << bit_idx)) == 0 ? '0' : '1');
    }
  }

  char getCanonicalValue(char value)
  {
    value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    if (value == '0' || value == '1' || value == 'z') {
      return value;
    }
    return 'x';
  }

  double getTimeUnitScale(std::string& time_unit)
  {
    if (time_unit == "s") {
      return 1E9;
    }
    if (time_unit == "ms") {
      return 1E6;
    }
    if (time_unit == "us") {
      return 1E3;
    }
    if (time_unit == "ns") {
      return 1.0;
    }
    if (time_unit == "ps") {
      return 1E-3;
    }
    if (time_unit == "fs") {
      return 1E-6;
    }
    return 0.0;
  }

  int32_t getInteger(std::string value)
  {
    char* end = nullptr;
    errno = 0;
    long number = std::strtol(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0' || number < std::numeric_limits<int32_t>::min()
        || number > std::numeric_limits<int32_t>::max()) {
      return std::numeric_limits<int32_t>::min();
    }
    return static_cast<int32_t>(number);
  }

  int64_t getInteger64(std::string value)
  {
    char* end = nullptr;
    errno = 0;
    long long number = std::strtoll(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0') {
      return 0;
    }
    return static_cast<int64_t>(number);
  }

  std::vector<std::string> _scope_name_list;
  std::map<std::string, std::vector<std::vector<std::string>>> _identifier_signal_name_list_map;
  std::map<std::string, SignalState> _signal_state_map;
  std::string _zero_time_token = "#0";
  int64_t _start_time = 0;
  int64_t _current_time = 0;
  int64_t _end_time = 0;
  bool _has_time = false;
  double _time_resolution = 1.0;
  double _time_unit_scale = 1.0;
};

}  // namespace

bool VcdReader::read(const std::string& file_path)
{
  VcdFileParser parser;
  return parser.read(file_path, _signal_activity_map);
}

}  // namespace vcd
