#include "gds_layer_map.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

namespace idb {

std::string GdsLayerMap::trim(const std::string& value)
{
  size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }
  size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(begin, end - begin);
}

std::string GdsLayerMap::normalize(const std::string& value)
{
  std::string result = trim(value);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return result;
}

bool GdsLayerMap::parse_uint(const std::string& token, uint32_t& value)
{
  std::string text = trim(token);
  if (text.empty() || text[0] == '-') {
    return false;
  }
  try {
    size_t parsed = 0;
    unsigned long long parsed_value = std::stoull(text, &parsed, 10);
    if (parsed != text.size() || parsed_value > 65535) {
      return false;
    }
    value = static_cast<uint32_t>(parsed_value);
    return true;
  } catch (...) {
    return false;
  }
}

std::string GdsLayerMap::make_key(const std::string& layer_name, const std::string& purpose)
{
  return normalize(layer_name) + "\n" + normalize(purpose);
}

bool GdsLayerMap::add_entry(const std::string& layer_name, const std::string& purpose, const GdsLayerMapValue& value, size_t line_number)
{
  std::string layer = normalize(layer_name);
  std::string type = normalize(purpose);
  if (layer.empty() || type.empty()) {
    _error = "empty layer name or purpose at line " + std::to_string(line_number);
    return false;
  }

  auto key = make_key(layer, type);
  auto [it, inserted] = _entries.emplace(key, value);
  if (!inserted && (it->second.layer != value.layer || it->second.datatype != value.datatype)) {
    _error = "conflicting mapping for " + layer + "/" + type + " at line " + std::to_string(line_number);
    return false;
  }
  return true;
}

bool GdsLayerMap::load(const std::string& path)
{
  _entries.clear();
  _error.clear();

  std::ifstream input(path);
  if (!input.is_open()) {
    _error = "cannot open layer map: " + path;
    return false;
  }

  std::string line;
  size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    auto comment = line.find('#');
    if (comment != std::string::npos) {
      line.erase(comment);
    }
    line = trim(line);
    if (line.empty()) {
      continue;
    }

    std::istringstream stream(line);
    std::vector<std::string> fields;
    std::string field;
    while (stream >> field) {
      fields.push_back(field);
    }
    if (fields.size() != 4) {
      _error = "expected four columns at line " + std::to_string(line_number);
      return false;
    }

    GdsLayerMapValue value;
    if (!parse_uint(fields[2], value.layer) || !parse_uint(fields[3], value.datatype)) {
      _error = "invalid GDS layer/datatype at line " + std::to_string(line_number);
      return false;
    }

    std::string purposes = fields[1];
    size_t begin = 0;
    while (begin <= purposes.size()) {
      size_t end = purposes.find(',', begin);
      std::string purpose = purposes.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
      if (!add_entry(fields[0], purpose, value, line_number)) {
        return false;
      }
      if (end == std::string::npos) {
        break;
      }
      begin = end + 1;
    }
  }

  if (input.bad()) {
    _error = "error while reading layer map: " + path;
    return false;
  }
  return true;
}

bool GdsLayerMap::resolve(const std::string& layer_name, const std::string& purpose, GdsLayerMapValue& value) const
{
  auto it = _entries.find(make_key(layer_name, purpose));
  if (it != _entries.end()) {
    value = it->second;
    return true;
  }

  // Standard technology layer maps commonly provide only the "drawing"
  // purpose.  ECC emits logical purposes such as NET, VIA, PIN and LEFOBS;
  // use the drawing record for those purposes when no exact record exists.
  if (normalize(purpose) != "DRAWING") {
    it = _entries.find(make_key(layer_name, "drawing"));
    if (it != _entries.end()) {
      value = it->second;
      return true;
    }
  }

  it = _entries.find(make_key(layer_name, "ALL"));
  if (it != _entries.end()) {
    value = it->second;
    return true;
  }
  return false;
}

}  // namespace idb
