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
#include "DRCCase.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace idrc {

namespace {

struct LayerRef
{
  bool is_routing = true;
  int32_t idx = -1;
};

struct PendingExpect
{
  int32_t line_no = 0;
  std::vector<std::string> token_list;
};

class CaseParser
{
 public:
  explicit CaseParser(const std::string& case_path) { _drc_case.case_path = case_path; }
  DRCCase parse();

 private:
  DRCCase _drc_case;
  int32_t _line_no = 0;
  int32_t _layer_order = 0;
  bool _has_die = false;
  bool _expect_none = false;
  std::map<std::string, LayerRef> _layer_map;
  std::vector<PendingExpect> _pending_expect_list;

  [[noreturn]] void error(const std::string& message) const
  {
    std::string location = _line_no > 0 ? _drc_case.case_path + ":" + std::to_string(_line_no) : _drc_case.case_path;
    throw std::runtime_error(location + ": " + message);
  }
  int32_t parseInt(const std::string& token) const;
  std::map<std::string, std::string> parseAttributeMap(const std::vector<std::string>& token_list, size_t begin) const;
  PlanarRect parseRect(const std::vector<std::string>& token_list, size_t begin, bool allow_empty) const;
  const LayerRef& getLayer(const std::string& layer_name) const;
  ViolationType getViolationType(const std::string& type_name) const;
  int32_t getNetIdx(const std::string& net_name, bool allow_obs);
  void parseStatement(const std::vector<std::string>& token_list);
  void parseRouting(const std::vector<std::string>& token_list);
  void parseCut(const std::vector<std::string>& token_list);
  void parseShape(const std::vector<std::string>& token_list, bool is_env);
  void parseExpect(const PendingExpect& pending_expect);
  void checkCase();
};

const std::map<std::string, ViolationType>& getViolationTypeMap()
{
  static const std::map<std::string, ViolationType> violation_type_map = [] {
    std::map<std::string, ViolationType> type_map;
    for (int32_t i = static_cast<int32_t>(ViolationType::kNone) + 1; i <= static_cast<int32_t>(ViolationType::kSameLayerCutSpacing); i++) {
      ViolationType violation_type = static_cast<ViolationType>(i);
      type_map[GetViolationTypeName()(violation_type)] = violation_type;
    }
    return type_map;
  }();
  return violation_type_map;
}

std::vector<std::string> splitTokens(std::string line)
{
  size_t comment_pos = line.find('#');
  if (comment_pos != std::string::npos) {
    line.erase(comment_pos);
  }
  std::vector<std::string> token_list;
  std::istringstream line_stream(line);
  std::string token;
  while (line_stream >> token) {
    token_list.push_back(token);
  }
  return token_list;
}

std::vector<std::string> splitByComma(const std::string& text)
{
  std::vector<std::string> item_list;
  std::stringstream text_stream(text);
  std::string item;
  while (std::getline(text_stream, item, ',')) {
    item_list.push_back(item);
  }
  return item_list;
}

DRCCase CaseParser::parse()
{
  std::ifstream case_file(_drc_case.case_path);
  if (!case_file.is_open()) {
    throw std::runtime_error("can not open case file '" + _drc_case.case_path + "'");
  }
  _drc_case.database.set_micron_dbu(1000);
  _drc_case.database.set_manufacture_grid(1);
  std::string line;
  while (std::getline(case_file, line)) {
    _line_no++;
    std::vector<std::string> token_list = splitTokens(line);
    if (!token_list.empty()) {
      parseStatement(token_list);
    }
  }
  for (const PendingExpect& pending_expect : _pending_expect_list) {
    _line_no = pending_expect.line_no;
    parseExpect(pending_expect);
  }
  _line_no = 0;
  checkCase();
  std::sort(_drc_case.expected_violation_list.begin(), _drc_case.expected_violation_list.end());
  return std::move(_drc_case);
}

int32_t CaseParser::parseInt(const std::string& token) const
{
  int32_t value = 0;
  auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
  if (ec != std::errc() || ptr != token.data() + token.size()) {
    error("'" + token + "' is not an integer");
  }
  return value;
}

std::map<std::string, std::string> CaseParser::parseAttributeMap(const std::vector<std::string>& token_list, size_t begin) const
{
  std::map<std::string, std::string> attribute_map;
  for (size_t i = begin; i < token_list.size(); i++) {
    size_t equal_pos = token_list[i].find('=');
    if (equal_pos == std::string::npos || equal_pos == 0 || equal_pos + 1 == token_list[i].size()) {
      error("expected key=value, got '" + token_list[i] + "'");
    }
    std::string key = token_list[i].substr(0, equal_pos);
    if (!attribute_map.emplace(key, token_list[i].substr(equal_pos + 1)).second) {
      error("duplicated attribute '" + key + "'");
    }
  }
  return attribute_map;
}

PlanarRect CaseParser::parseRect(const std::vector<std::string>& token_list, size_t begin, bool allow_empty) const
{
  if (token_list.size() < begin + 4) {
    error("expected four coordinates 'llx lly urx ury'");
  }
  PlanarRect rect(parseInt(token_list[begin]), parseInt(token_list[begin + 1]), parseInt(token_list[begin + 2]), parseInt(token_list[begin + 3]));
  bool is_valid = allow_empty ? (rect.get_ll_x() <= rect.get_ur_x() && rect.get_ll_y() <= rect.get_ur_y())
                              : (rect.get_ll_x() < rect.get_ur_x() && rect.get_ll_y() < rect.get_ur_y());
  if (!is_valid) {
    error("rectangle must satisfy llx " + std::string(allow_empty ? "<=" : "<") + " urx and lly " + (allow_empty ? "<=" : "<") + " ury");
  }
  return rect;
}

const LayerRef& CaseParser::getLayer(const std::string& layer_name) const
{
  auto iter = _layer_map.find(layer_name);
  if (iter == _layer_map.end()) {
    error("unknown layer '" + layer_name + "'");
  }
  return iter->second;
}

ViolationType CaseParser::getViolationType(const std::string& type_name) const
{
  const std::map<std::string, ViolationType>& violation_type_map = getViolationTypeMap();
  auto iter = violation_type_map.find(type_name);
  if (iter == violation_type_map.end()) {
    error("unknown violation type '" + type_name + "'");
  }
  return iter->second;
}

int32_t CaseParser::getNetIdx(const std::string& net_name, bool allow_obs)
{
  if (net_name == "obs") {
    if (!allow_obs) {
      error("'obs' is only valid for env shapes");
    }
    return -1;
  }
  static const std::regex net_name_regex("[A-Za-z_][A-Za-z0-9_]*");
  if (!std::regex_match(net_name, net_name_regex)) {
    error("invalid net name '" + net_name + "'");
  }
  auto [iter, is_new] = _drc_case.net_name_to_idx_map.emplace(net_name, static_cast<int32_t>(_drc_case.net_name_list.size()));
  if (is_new) {
    _drc_case.net_name_list.push_back(net_name);
  }
  return iter->second;
}

void CaseParser::parseStatement(const std::vector<std::string>& token_list)
{
  const std::string& keyword = token_list.front();
  if (keyword == "case") {
    if (token_list.size() != 2) {
      error("expected 'case <name>'");
    }
  } else if (keyword == "dbu") {
    if (token_list.size() != 2) {
      error("expected 'dbu <int>'");
    }
    _drc_case.database.set_micron_dbu(parseInt(token_list[1]));
  } else if (keyword == "die") {
    if (_has_die) {
      error("die is declared twice");
    }
    if (token_list.size() != 5) {
      error("expected 'die <llx> <lly> <urx> <ury>'");
    }
    PlanarRect die_rect = parseRect(token_list, 1, false);
    _drc_case.database.get_die().set_ll(die_rect.get_ll());
    _drc_case.database.get_die().set_ur(die_rect.get_ur());
    _has_die = true;
  } else if (keyword == "routing") {
    parseRouting(token_list);
  } else if (keyword == "cut") {
    parseCut(token_list);
  } else if (keyword == "rule") {
    if (token_list.size() < 2) {
      error("expected 'rule <type>...'");
    }
    for (size_t i = 1; i < token_list.size(); i++) {
      ViolationType violation_type = getViolationType(token_list[i]);
      if (violation_type != ViolationType::kMetalShort && violation_type != ViolationType::kOutOfDie) {
        error("'rule' only enables parameterless rules (metal_short, out_of_die); '" + token_list[i]
              + "' is configured by its layer attributes or is not supported yet");
      }
      _drc_case.database.get_exist_rule_set().insert(violation_type);
    }
  } else if (keyword == "result") {
    parseShape(token_list, false);
  } else if (keyword == "env") {
    parseShape(token_list, true);
  } else if (keyword == "check") {
    if (token_list.size() < 2) {
      error("expected 'check <type>...'");
    }
    for (size_t i = 1; i < token_list.size(); i++) {
      _drc_case.check_type_set.insert(getViolationType(token_list[i]));
    }
  } else if (keyword == "expect") {
    if (token_list.size() == 2 && token_list[1] == "none") {
      _expect_none = true;
    } else {
      _pending_expect_list.push_back({_line_no, token_list});
    }
  } else if (keyword == "vary") {
    std::map<std::string, std::string> attribute_map = parseAttributeMap(token_list, 1);
    if (attribute_map.size() != 1 || !attribute_map.contains("pitch")) {
      error("expected 'vary pitch=<int>'");
    }
    int32_t pitch = parseInt(attribute_map["pitch"]);
    if (pitch <= 0) {
      error("vary pitch must be positive");
    }
    _drc_case.vary_pitch_list.push_back(pitch);
  } else {
    error("unknown statement '" + keyword + "'");
  }
}

void CaseParser::parseRouting(const std::vector<std::string>& token_list)
{
  if (token_list.size() < 2) {
    error("expected 'routing <name> dir=<H|V> pitch=<int> [spacing=<int>] [min_width=<int>] [min_area=<int>]'");
  }
  const std::string& layer_name = token_list[1];
  std::vector<RoutingLayer>& routing_layer_list = _drc_case.database.get_routing_layer_list();
  if (!_layer_map.emplace(layer_name, LayerRef{true, static_cast<int32_t>(routing_layer_list.size())}).second) {
    error("layer '" + layer_name + "' is declared twice");
  }
  std::map<std::string, std::string> attribute_map = parseAttributeMap(token_list, 2);
  std::set<ViolationType>& exist_rule_set = _drc_case.database.get_exist_rule_set();

  RoutingLayer routing_layer;
  routing_layer.set_layer_idx(static_cast<int32_t>(routing_layer_list.size()));
  routing_layer.set_layer_order(_layer_order++);
  routing_layer.set_layer_name(layer_name);
  for (auto& [key, value] : attribute_map) {
    if (key == "dir") {
      if (value == "H") {
        routing_layer.set_prefer_direction(Direction::kHorizontal);
      } else if (value == "V") {
        routing_layer.set_prefer_direction(Direction::kVertical);
      } else {
        error("dir must be H or V");
      }
    } else if (key == "pitch") {
      routing_layer.set_pitch(parseInt(value));
    } else if (key == "spacing") {
      ParallelRunLengthSpacingRule& prl_spacing_rule = routing_layer.get_parallel_run_length_spacing_rule();
      prl_spacing_rule.spacing_list.push_back(LayerSpacing{LayerSpacingType::kSpacingDefault, parseInt(value), 0, 0});
      prl_spacing_rule.has_spacing_list = true;
      exist_rule_set.insert(ViolationType::kParallelRunLengthSpacing);
    } else if (key == "min_width") {
      routing_layer.get_minimum_width_rule().min_width = parseInt(value);
      exist_rule_set.insert(ViolationType::kMinimumWidth);
    } else if (key == "min_area") {
      routing_layer.get_minimum_area_rule().min_area = parseInt(value);
      exist_rule_set.insert(ViolationType::kMinimumArea);
    } else {
      error("unsupported routing layer attribute '" + key + "'");
    }
  }
  if (!attribute_map.contains("dir") || !attribute_map.contains("pitch")) {
    error("routing layer '" + layer_name + "' requires dir= and pitch=");
  }
  routing_layer_list.push_back(std::move(routing_layer));
  _drc_case.routing_layer_name_list.push_back(layer_name);
}

void CaseParser::parseCut(const std::vector<std::string>& token_list)
{
  if (token_list.size() != 2) {
    error("expected 'cut <name>' (cut rules are not supported yet)");
  }
  const std::string& layer_name = token_list[1];
  std::vector<CutLayer>& cut_layer_list = _drc_case.database.get_cut_layer_list();
  if (!_layer_map.emplace(layer_name, LayerRef{false, static_cast<int32_t>(cut_layer_list.size())}).second) {
    error("layer '" + layer_name + "' is declared twice");
  }
  CutLayer cut_layer;
  cut_layer.set_layer_idx(static_cast<int32_t>(cut_layer_list.size()));
  cut_layer.set_layer_order(_layer_order++);
  cut_layer.set_layer_name(layer_name);
  cut_layer_list.push_back(std::move(cut_layer));
  _drc_case.cut_layer_name_list.push_back(layer_name);
}

void CaseParser::parseShape(const std::vector<std::string>& token_list, bool is_env)
{
  if (token_list.size() != 7) {
    error(std::string("expected '") + (is_env ? "env <net|obs>" : "result <net>") + " <layer> <llx> <lly> <urx> <ury>'");
  }
  int32_t net_idx = getNetIdx(token_list[1], is_env);
  const LayerRef& layer_ref = getLayer(token_list[2]);
  DRCShape drc_shape(net_idx, LayerRect(parseRect(token_list, 3, false), layer_ref.idx), layer_ref.is_routing);
  if (!is_env) {
    drc_shape.set_source_type(ids::Shape::SourceType::kRegularWire);
    _drc_case.result_shape_list.push_back(std::move(drc_shape));
  } else {
    drc_shape.set_source_type(net_idx == -1 ? ids::Shape::SourceType::kInstanceObs : ids::Shape::SourceType::kInstancePin);
    _drc_case.env_shape_list.push_back(std::move(drc_shape));
  }
}

void CaseParser::parseExpect(const PendingExpect& pending_expect)
{
  const std::vector<std::string>& token_list = pending_expect.token_list;
  if (token_list.size() != 9) {
    error("expected 'expect <type> <layer> <llx> <lly> <urx> <ury> nets=<net>[,<net>] required=<int>' or 'expect none'");
  }
  const LayerRef& layer_ref = getLayer(token_list[2]);
  std::map<std::string, std::string> attribute_map = parseAttributeMap(token_list, 7);
  if (attribute_map.size() != 2 || !attribute_map.contains("nets") || !attribute_map.contains("required")) {
    error("expect needs exactly nets= and required=");
  }
  std::set<int32_t> violation_net_set;
  for (const std::string& net_name : splitByComma(attribute_map["nets"])) {
    if (net_name == "obs") {
      violation_net_set.insert(-1);
      continue;
    }
    auto iter = _drc_case.net_name_to_idx_map.find(net_name);
    if (iter == _drc_case.net_name_to_idx_map.end()) {
      error("expect refers to net '" + net_name + "' that has no shape");
    }
    violation_net_set.insert(iter->second);
  }
  Violation violation;
  violation.set_violation_type(getViolationType(token_list[1]));
  violation.set_is_routing(layer_ref.is_routing);
  violation.set_layer_idx(layer_ref.idx);
  violation.set_rect(parseRect(token_list, 3, true));
  violation.set_violation_net_set(violation_net_set);
  violation.set_required_size(parseInt(attribute_map["required"]));
  _drc_case.expected_violation_list.push_back(formatViolation(_drc_case, violation));
}

void CaseParser::checkCase()
{
  if (!_has_die) {
    error("missing 'die'");
  }
  if (_drc_case.database.get_routing_layer_list().empty() || _drc_case.database.get_cut_layer_list().empty()) {
    error("iDRC needs at least one routing layer and one cut layer");
  }
  if (_expect_none == !_pending_expect_list.empty()) {
    error(_expect_none ? "'expect none' can not be combined with other expectations" : "no expectation; write 'expect none' if the case must be clean");
  }
}

}  // namespace

DRCCase parseDRCCase(const std::string& case_path)
{
  return CaseParser(case_path).parse();
}

std::string formatViolation(const DRCCase& drc_case, const Violation& violation)
{
  const std::vector<std::string>& layer_name_list = violation.get_is_routing() ? drc_case.routing_layer_name_list : drc_case.cut_layer_name_list;
  int32_t layer_idx = violation.get_layer_idx();
  std::string layer_name = "<layer#" + std::to_string(layer_idx) + ">";
  if (0 <= layer_idx && layer_idx < static_cast<int32_t>(layer_name_list.size())) {
    layer_name = layer_name_list[layer_idx];
  }
  std::vector<std::string> net_name_list;
  for (int32_t net_idx : violation.get_violation_net_set()) {
    if (net_idx == -1) {
      net_name_list.push_back("obs");
    } else if (0 <= net_idx && net_idx < static_cast<int32_t>(drc_case.net_name_list.size())) {
      net_name_list.push_back(drc_case.net_name_list[net_idx]);
    } else {
      net_name_list.push_back("<net#" + std::to_string(net_idx) + ">");
    }
  }
  std::sort(net_name_list.begin(), net_name_list.end());
  std::string nets;
  for (size_t i = 0; i < net_name_list.size(); i++) {
    nets += (i == 0 ? "" : ",") + net_name_list[i];
  }
  std::ostringstream line;
  line << GetViolationTypeName()(violation.get_violation_type()) << " " << layer_name << " " << violation.get_ll_x() << " " << violation.get_ll_y() << " "
       << violation.get_ur_x() << " " << violation.get_ur_y() << " nets=" << nets << " required=" << violation.get_required_size();
  return line.str();
}

std::vector<std::string> formatViolationList(const DRCCase& drc_case, const std::vector<Violation>& violation_list)
{
  std::vector<std::string> line_list;
  line_list.reserve(violation_list.size());
  for (const Violation& violation : violation_list) {
    line_list.push_back(formatViolation(drc_case, violation));
  }
  std::sort(line_list.begin(), line_list.end());
  return line_list;
}

}  // namespace idrc
