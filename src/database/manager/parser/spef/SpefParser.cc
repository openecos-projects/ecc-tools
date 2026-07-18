#include "SpefParser.hh"

#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <string_view>
#include <utility>

#include "log/Log.hh"

int spef_parse(spef::ParserContext* context);
void spef_restart(FILE* input_file);
extern FILE* spef_in;

namespace spef {
namespace {

std::string joinHeaderValues(const std::vector<std::string>& values)
{
  std::string result;
  for (const auto& value : values) {
    if (!result.empty()) {
      result += ' ';
    }
    result += value;
  }
  return result;
}

bool startsWithNameIndex(const std::string& name)
{
  return name.size() >= 2
         && name.front() == '*'
         && std::isdigit(static_cast<unsigned char>(name[1]));
}

std::string_view trimView(std::string_view value)
{
  const auto first = value.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string_view::npos) {
    return {};
  }

  const auto last = value.find_last_not_of(" \t\n\r\f\v");
  return value.substr(first, last - first + 1);
}

bool startsWith(std::string_view value,
                std::string_view prefix)
{
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::string_view takeToken(std::string_view& value)
{
  value = trimView(value);
  if (value.empty()) {
    return {};
  }

  const auto end = value.find_first_of(" \t\n\r\f\v");
  if (end == std::string_view::npos) {
    const auto token = value;
    value = {};
    return token;
  }

  const auto token = value.substr(0, end);
  value = value.substr(end + 1);
  return token;
}

std::string_view stripValuePunctuation(std::string_view value)
{
  while (!value.empty() && (value.back() == ',' || value.back() == ';')) {
    value.remove_suffix(1);
  }
  return value;
}

std::optional<double> parseDouble(std::string_view value)
{
  value = stripValuePunctuation(trimView(value));
  if (value.empty()) {
    return std::nullopt;
  }

  const std::string text(value);
  char* end = nullptr;
  errno = 0;
  const double number = std::strtod(text.c_str(), &end);
  if (errno != 0 || end != text.c_str() + text.size()) {
    return std::nullopt;
  }
  return number;
}

std::optional<int> parseInt(std::string_view value)
{
  const auto number = parseDouble(value);
  if (!number.has_value()) {
    return std::nullopt;
  }
  return static_cast<int>(*number);
}

bool hasCoord(const Coord& coord)
{
  return coord.x >= 0.0 && coord.y >= 0.0;
}

void refreshBoxFlag(GeometryAttr& geometry)
{
  geometry.has_box = hasCoord(geometry.ll_coordinate) && hasCoord(geometry.ur_coordinate);
}

void mergeGeometry(GeometryAttr& target,
                   const GeometryAttr& source)
{
  if (source.has_box) {
    target.has_box = true;
    target.ll_coordinate = source.ll_coordinate;
    target.ur_coordinate = source.ur_coordinate;
  }
  if (source.has_layer) {
    target.has_layer = true;
    target.layer = source.layer;
  }
  if (source.has_length) {
    target.has_length = true;
    target.length = source.length;
  }
  if (source.has_width) {
    target.has_width = true;
    target.width = source.width;
  }
  if (source.has_area) {
    target.has_area = true;
    target.area = source.area;
  }
  if (source.has_direction) {
    target.has_direction = true;
    target.direction = source.direction;
  }
}

GeometryAttr parseGeometryAnnotation(std::string_view comment)
{
  GeometryAttr geometry;
  std::optional<double> llx;
  std::optional<double> lly;
  std::optional<double> urx;
  std::optional<double> ury;

  while (true) {
    const auto token = takeToken(comment);
    if (token.empty()) {
      break;
    }
    if (token.front() != '$') {
      continue;
    }

    const auto equal_pos = token.find('=');
    if (equal_pos == std::string_view::npos || equal_pos <= 1) {
      continue;
    }

    const std::string_view key = token.substr(1, equal_pos - 1);
    const std::string_view value = token.substr(equal_pos + 1);

    if (key == "llx") {
      llx = parseDouble(value);
    } else if (key == "lly") {
      lly = parseDouble(value);
    } else if (key == "urx") {
      urx = parseDouble(value);
    } else if (key == "ury") {
      ury = parseDouble(value);
    } else if (key == "lvl" || key == "layer") {
      if (const auto layer = parseInt(value)) {
        geometry.has_layer = true;
        geometry.layer = *layer;
      }
    } else if (key == "l" || key == "length") {
      if (const auto length = parseDouble(value)) {
        geometry.has_length = true;
        geometry.length = *length;
      }
    } else if (key == "w" || key == "width") {
      if (const auto width = parseDouble(value)) {
        geometry.has_width = true;
        geometry.width = *width;
      }
    } else if (key == "a" || key == "area") {
      if (const auto area = parseDouble(value)) {
        geometry.has_area = true;
        geometry.area = *area;
      }
    } else if (key == "dir" || key == "direction") {
      if (const auto direction = parseInt(value)) {
        geometry.has_direction = true;
        geometry.direction = *direction;
      }
    }
  }

  if (llx.has_value() && lly.has_value() && urx.has_value() && ury.has_value()) {
    geometry.has_box = true;
    geometry.ll_coordinate = Coord{*llx, *lly};
    geometry.ur_coordinate = Coord{*urx, *ury};
  }

  return geometry;
}

bool parseLayerMapLine(std::string_view line,
                       Exchange& exchange)
{
  line = trimView(line);
  const auto index_token = takeToken(line);
  if (index_token.size() < 2 || index_token.front() != '*'
      || !std::isdigit(static_cast<unsigned char>(index_token[1]))) {
    return false;
  }

  const auto level = parseInt(index_token.substr(1));
  const auto layer_name = takeToken(line);
  if (!level.has_value() || layer_name.empty()) {
    return false;
  }

  LayerMapEntry entry;
  entry.level = *level;
  entry.layer_name = removeEscapes(stripQuotes(std::string(layer_name)));
  entry.raw_info = std::string(trimView(line));
  exchange.layer_map[entry.level] = std::move(entry);
  return true;
}

enum class AnnotationSection
{
  kNone,
  kConn,
  kCap,
  kRes
};

void applyConnGeometry(ConnEntry& conn,
                       const GeometryAttr& geometry)
{
  mergeGeometry(conn.geometry, geometry);
  if (geometry.has_box) {
    conn.ll_coordinate = geometry.ll_coordinate;
    conn.ur_coordinate = geometry.ur_coordinate;
  }
  if (geometry.has_layer) {
    conn.layer = geometry.layer;
  }
}

void applyResCapGeometry(ResCap& res_cap,
                         const GeometryAttr& geometry)
{
  mergeGeometry(res_cap.geometry, geometry);
}

void augmentAnnotations(Exchange& exchange)
{
  std::ifstream file(exchange.file_name);
  if (!file.is_open()) {
    LOG_WARNING << "open SPEF annotation scan file failed: " << exchange.file_name;
    return;
  }

  AnnotationSection section = AnnotationSection::kNone;
  Net* current_net = nullptr;
  std::size_t next_net_idx = 0;
  std::size_t conn_idx = 0;
  std::size_t cap_idx = 0;
  std::size_t res_idx = 0;
  bool in_layer_map = false;

  auto begin_net = [&]() {
    current_net = next_net_idx < exchange.nets.size() ? &exchange.nets[next_net_idx++] : nullptr;
    section = AnnotationSection::kNone;
    conn_idx = 0;
    cap_idx = 0;
    res_idx = 0;
  };

  std::string line_storage;
  while (std::getline(file, line_storage)) {
    std::string_view line(line_storage);
    std::string_view normalized = trimView(line);
    if (startsWith(normalized, "//")) {
      normalized.remove_prefix(2);
      normalized = trimView(normalized);
    }

    if (startsWith(normalized, "*LAYER_MAP")) {
      in_layer_map = true;
      continue;
    }
    if (in_layer_map) {
      if (parseLayerMapLine(normalized, exchange) || normalized.empty()) {
        continue;
      }
      in_layer_map = false;
    }

    const auto comment_pos = line.find("//");
    const std::string_view content = trimView(
        comment_pos == std::string_view::npos ? line : line.substr(0, comment_pos));
    const std::string_view comment =
        comment_pos == std::string_view::npos ? std::string_view{} : line.substr(comment_pos + 2);

    if (content.empty()) {
      continue;
    }

    if (startsWith(content, "*D_NET")) {
      begin_net();
      continue;
    }
    if (startsWith(content, "*END")) {
      current_net = nullptr;
      section = AnnotationSection::kNone;
      continue;
    }
    if (startsWith(content, "*CONN")) {
      section = AnnotationSection::kConn;
      conn_idx = 0;
      continue;
    }
    if (startsWith(content, "*CAP")) {
      section = AnnotationSection::kCap;
      cap_idx = 0;
      continue;
    }
    if (startsWith(content, "*RES")) {
      section = AnnotationSection::kRes;
      res_idx = 0;
      continue;
    }

    const bool is_numbered_entry =
        !content.empty() && std::isdigit(static_cast<unsigned char>(content.front()));
    const bool is_conn_entry = startsWith(content, "*P")
                               || startsWith(content, "*I")
                               || startsWith(content, "*N");
    if (current_net == nullptr || (!is_numbered_entry && !is_conn_entry)) {
      continue;
    }

    const GeometryAttr geometry = parseGeometryAnnotation(comment);
    if (section == AnnotationSection::kConn && is_conn_entry) {
      if (conn_idx < current_net->conns.size()) {
        applyConnGeometry(current_net->conns[conn_idx], geometry);
      }
      ++conn_idx;
    } else if (section == AnnotationSection::kCap && is_numbered_entry) {
      if (cap_idx < current_net->caps.size()) {
        applyResCapGeometry(current_net->caps[cap_idx], geometry);
      }
      ++cap_idx;
    } else if (section == AnnotationSection::kRes && is_numbered_entry) {
      if (res_idx < current_net->ress.size()) {
        applyResCapGeometry(current_net->ress[res_idx], geometry);
      }
      ++res_idx;
    }
  }
}

}  // namespace

ParserContext::ParserContext(std::string file_name) : exchange_(std::move(file_name)) {}

void ParserContext::setSection(SectionType section)
{
  current_section_ = section;
  if (section == SectionType::kEnd) {
    finishNet();
  }
}

void ParserContext::startHeader(std::string key)
{
  pending_header_key_ = std::move(key);
  pending_header_values_.clear();
}

void ParserContext::addHeaderValue(std::string value)
{
  pending_header_values_.push_back(std::move(value));
}

void ParserContext::finishHeader()
{
  if (pending_header_key_.empty()) {
    return;
  }
  exchange_.header.push_back(
      HeaderEntry{pending_header_key_, joinHeaderValues(pending_header_values_)});
  pending_header_key_.clear();
  pending_header_values_.clear();
}

void ParserContext::addNameMap(std::string index_name, std::string mapped_name)
{
  const std::size_t index = parseNameIndex(index_name);
  exchange_.index_to_name_map[index] = mapped_name;
  exchange_.name_to_index_map[std::move(mapped_name)] = index;
}

void ParserContext::addPort(std::string name, ConnectionDirection direction, Coord coordinate)
{
  if (startsWithNameIndex(name)) {
    name.erase(0, 1);
  }
  exchange_.ports.push_back(PortEntry{std::move(name), direction, coordinate});
}

void ParserContext::startNet(std::string name, double lcap, std::size_t line_no)
{
  if (has_current_net_) {
    finishNet();
  }
  current_net_ = Net{};
  current_net_.name = std::move(name);
  current_net_.lcap = lcap;
  current_net_.line_no = line_no;
  has_current_net_ = true;
}

void ParserContext::finishNet()
{
  if (!has_current_net_) {
    return;
  }
  exchange_.nets.push_back(std::move(current_net_));
  current_net_ = Net{};
  has_current_net_ = false;
}

void ParserContext::startConn(ConnectionType type, std::string name, ConnectionDirection direction)
{
  current_conn_ = ConnEntry{};
  current_conn_.conn_type = type;
  current_conn_.pin_port_name = std::move(name);
  current_conn_.conn_direction = direction;
  if (type == ConnectionType::kInternal && direction == ConnectionDirection::kUninitialized) {
    current_conn_.conn_direction = ConnectionDirection::kInternal;
  }
  has_current_conn_ = true;
}

void ParserContext::setConnCoordinate(Coord coordinate)
{
  if (has_current_conn_) {
    current_conn_.coordinate = coordinate;
  }
}

void ParserContext::setConnLoad(double load)
{
  if (has_current_conn_) {
    current_conn_.load = load;
  }
}

void ParserContext::setConnDrivingCell(std::string driving_cell)
{
  if (has_current_conn_) {
    current_conn_.driving_cell = std::move(driving_cell);
  }
}

void ParserContext::setConnLowerLeft(Coord coordinate)
{
  if (has_current_conn_) {
    current_conn_.ll_coordinate = coordinate;
    current_conn_.geometry.ll_coordinate = coordinate;
    refreshBoxFlag(current_conn_.geometry);
  }
}

void ParserContext::setConnUpperRight(Coord coordinate)
{
  if (has_current_conn_) {
    current_conn_.ur_coordinate = coordinate;
    current_conn_.geometry.ur_coordinate = coordinate;
    refreshBoxFlag(current_conn_.geometry);
  }
}

void ParserContext::setConnLayer(int layer)
{
  if (has_current_conn_) {
    current_conn_.layer = layer;
    current_conn_.geometry.has_layer = true;
    current_conn_.geometry.layer = layer;
  }
}

void ParserContext::finishConn()
{
  if (!has_current_net_ || !has_current_conn_) {
    return;
  }
  current_net_.conns.push_back(std::move(current_conn_));
  current_conn_ = ConnEntry{};
  has_current_conn_ = false;
}

void ParserContext::addCap(std::string node1, std::string node2, double cap)
{
  if (has_current_net_) {
    current_net_.caps.push_back(ResCap{std::move(node1), std::move(node2), cap});
  }
}

void ParserContext::addRes(std::string node1, std::string node2, double res)
{
  if (has_current_net_) {
    current_net_.ress.push_back(ResCap{std::move(node1), std::move(node2), res});
  }
}

void ParserContext::addCapOrRes(std::string node1, std::string node2, double value)
{
  if (current_section_ == SectionType::kCap) {
    addCap(std::move(node1), std::move(node2), value);
  } else if (current_section_ == SectionType::kRes) {
    addRes(std::move(node1), std::move(node2), value);
  }
}

void ParserContext::setError(std::string message)
{
  if (error_message_.empty()) {
    error_message_ = std::move(message);
  }
}

std::size_t ParserContext::parseNameIndex(const std::string& index_name)
{
  const std::size_t begin = index_name.front() == '*' ? 1 : 0;
  const std::size_t end = index_name.find(':', begin);
  return static_cast<std::size_t>(
      std::strtoull(index_name.substr(begin, end - begin).c_str(), nullptr, 10));
}

double toDouble(const char* text)
{
  if (text == nullptr) {
    return 0.0;
  }
  errno = 0;
  char* end = nullptr;
  const double value = std::strtod(text, &end);
  if (errno != 0 || end == text) {
    return 0.0;
  }
  return value;
}

int toInt(const char* text)
{
  if (text == nullptr) {
    return 0;
  }
  return static_cast<int>(std::strtol(text, nullptr, 10));
}

std::string tokenToString(const char* text)
{
  return text == nullptr ? std::string{} : std::string{text};
}

std::string stripQuotes(std::string text)
{
  if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
    return text.substr(1, text.size() - 2);
  }
  return text;
}

ConnectionDirection parseDirection(const char* text)
{
  if (text == nullptr) {
    return ConnectionDirection::kUninitialized;
  }
  if (std::strcmp(text, "I") == 0) {
    return ConnectionDirection::kInput;
  }
  if (std::strcmp(text, "O") == 0) {
    return ConnectionDirection::kOutput;
  }
  if (std::strcmp(text, "B") == 0) {
    return ConnectionDirection::kInout;
  }
  return ConnectionDirection::kUninitialized;
}

ConnectionType parseConnectionType(const char* text)
{
  if (text == nullptr) {
    return ConnectionType::kUninitialized;
  }
  if (std::strcmp(text, "*P") == 0) {
    return ConnectionType::kExternal;
  }
  if (std::strcmp(text, "*I") == 0 || std::strcmp(text, "*N") == 0) {
    return ConnectionType::kInternal;
  }
  return ConnectionType::kInternal;
}

std::string removeEscapes(const std::string& name)
{
  std::string result;
  result.reserve(name.size());
  for (char ch : name) {
    if (ch != '\\') {
      result.push_back(ch);
    }
  }
  return result;
}

std::string expandName(const Exchange& exchange, const std::string& name)
{
  if (!startsWithNameIndex(name)) {
    return name;
  }

  const std::size_t begin = 1;
  const std::size_t colon = name.find(':', begin);
  const std::size_t index = static_cast<std::size_t>(
      std::strtoull(name.substr(begin, colon - begin).c_str(), nullptr, 10));
  const auto map_it = exchange.index_to_name_map.find(index);
  if (map_it == exchange.index_to_name_map.end()) {
    return name;
  }

  std::string expanded = removeEscapes(map_it->second);
  if (colon != std::string::npos) {
    expanded += name.substr(colon);
  }
  return expanded;
}

void expandAllNames(Exchange& exchange)
{
  if (exchange.index_to_name_map.empty()) {
    return;
  }

  for (auto& net : exchange.nets) {
    const std::string current_net_name = net.name;
    net.name = expandName(exchange, net.name);
    for (auto& conn : net.conns) {
      conn.pin_port_name = expandName(exchange, conn.pin_port_name);
    }
    for (auto& cap : net.caps) {
      cap.node1 = expandName(exchange, cap.node1);
      if (!cap.node2.empty()) {
        cap.node2 = expandName(exchange, cap.node2);
      } else if (cap.node1 == current_net_name) {
        cap.node1 = net.name;
      }
    }
    for (auto& res : net.ress) {
      res.node1 = expandName(exchange, res.node1);
      if (!res.node2.empty()) {
        res.node2 = expandName(exchange, res.node2);
      }
    }
  }
}

Exchange* parseSpefFile(const char* spef_path)
{
  if (spef_path == nullptr) {
    return nullptr;
  }

  FILE* file = std::fopen(spef_path, "r");
  if (file == nullptr) {
    LOG_ERROR << "open spef file failed: " << spef_path;
    return nullptr;
  }

  ParserContext context(spef_path);
  spef_in = file;
  spef_restart(file);
  const int parse_status = spef_parse(&context);
  std::fclose(file);
  spef_in = nullptr;

  if (parse_status != 0 || !context.ok()) {
    LOG_ERROR << "parse spef file failed: " << spef_path << " " << context.errorMessage();
    return nullptr;
  }

  context.finishNet();
  auto* exchange = new Exchange(std::move(context.exchange()));
  augmentAnnotations(*exchange);
  return exchange;
}

std::string getSpefCapUnit(const Exchange& exchange)
{
  for (const auto& header : exchange.header) {
    if (header.key == "*C_UNIT") {
      return header.value;
    }
  }
  return "";
}

std::string getSpefResUnit(const Exchange& exchange)
{
  for (const auto& header : exchange.header) {
    if (header.key == "*R_UNIT") {
      return header.value;
    }
  }
  return "";
}

bool SpefReader::read(const std::string& file_path)
{
  spef_file_.reset(parseSpefFile(file_path.c_str()));
  return spef_file_ != nullptr;
}

void SpefReader::expandName()
{
  if (spef_file_ != nullptr) {
    expandAllNames(*spef_file_);
  }
}

std::string SpefReader::getSpefCapUnit() const
{
  return spef_file_ == nullptr ? std::string{} : spef::getSpefCapUnit(*spef_file_);
}

std::string SpefReader::getSpefResUnit() const
{
  return spef_file_ == nullptr ? std::string{} : spef::getSpefResUnit(*spef_file_);
}

}  // namespace spef
