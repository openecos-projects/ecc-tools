#include "RedHawkResNetworkReader.hpp"

namespace {

struct WireRecord
{
  std::string id;
  std::string layer_name;
  std::string net_name;
  double width_um = 0.0;
};

struct WireSegmentFields
{
  std::string id;
  std::string parent_id;
  double first_x_um = 0.0;
  double first_y_um = 0.0;
  double second_x_um = 0.0;
  double second_y_um = 0.0;
  double resistance_ohm = 0.0;
  std::size_t line_number = 0;
};

std::vector<std::string> splitWhitespace(const std::string& line)
{
  std::vector<std::string> fields;
  std::istringstream stream(line);
  std::string field;
  while (stream >> field) {
    fields.push_back(std::move(field));
  }
  return fields;
}

double parsePositiveDouble(const std::string& text, const std::string& field_name, std::size_t line_number)
{
  std::size_t parsed_size = 0;
  double value = 0.0;
  try {
    value = std::stod(text, &parsed_size);
  } catch (const std::exception&) {
    throw std::runtime_error("Invalid " + field_name + " at line " + std::to_string(line_number) + ": " + text);
  }
  if (parsed_size != text.size() || !std::isfinite(value) || value <= 0.0) {
    throw std::runtime_error("Invalid " + field_name + " at line " + std::to_string(line_number) + ": " + text);
  }
  return value;
}

double parseCoordinate(const std::string& text, const std::string& field_name, std::size_t line_number)
{
  std::size_t parsed_size = 0;
  double value = 0.0;
  try {
    value = std::stod(text, &parsed_size);
  } catch (const std::exception&) {
    throw std::runtime_error("Invalid " + field_name + " at line " + std::to_string(line_number) + ": " + text);
  }
  if (parsed_size != text.size() || !std::isfinite(value)) {
    throw std::runtime_error("Invalid " + field_name + " at line " + std::to_string(line_number) + ": " + text);
  }
  return value;
}

int32_t parsePositiveInteger(const std::string& text, const std::string& field_name, std::size_t line_number)
{
  std::size_t parsed_size = 0;
  long value = 0;
  try {
    value = std::stol(text, &parsed_size);
  } catch (const std::exception&) {
    throw std::runtime_error("Invalid " + field_name + " at line " + std::to_string(line_number) + ": " + text);
  }
  if (parsed_size != text.size() || value <= 0 || value > std::numeric_limits<int32_t>::max()) {
    throw std::runtime_error("Invalid " + field_name + " at line " + std::to_string(line_number) + ": " + text);
  }
  return static_cast<int32_t>(value);
}

std::string getParentWireId(const std::string& segment_id, std::size_t line_number)
{
  std::size_t delimiter_position = segment_id.rfind('_');
  if (delimiter_position == std::string::npos || delimiter_position == 0 || delimiter_position + 1 == segment_id.size()) {
    throw std::runtime_error("Invalid WS identifier at line " + std::to_string(line_number) + ": " + segment_id);
  }
  return segment_id.substr(0, delimiter_position);
}

}  // namespace

namespace iemir {

RedHawkResNetwork RedHawkResNetworkReader::read(const std::string& file_path)
{
  std::ifstream input(file_path);
  if (!input.is_open()) {
    throw std::runtime_error("Cannot open RedHawk res_network file: " + file_path);
  }

  RedHawkResNetwork network;
  std::vector<WireRecord> wires;
  std::unordered_map<std::string, std::size_t> wire_index_by_id;
  std::vector<WireSegmentFields> wire_segments;
  std::unordered_set<std::string> wire_segment_ids;
  std::unordered_set<std::string> via_ids;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    line_number++;
    std::vector<std::string> fields = splitWhitespace(line);
    if (fields.empty() || fields.front().front() == '#') {
      continue;
    }

    if (fields[0] == "W") {
      if (fields.size() != 15) {
        throw std::runtime_error("Expected 15 fields for W at line " + std::to_string(line_number));
      }
      WireRecord wire;
      wire.id = fields[1];
      wire.layer_name = fields[2];
      wire.net_name = fields[3];
      for (std::size_t index = 0; index < 4; index++) {
        parseCoordinate(fields[4 + index * 2], "W x coordinate", line_number);
        parseCoordinate(fields[5 + index * 2], "W y coordinate", line_number);
      }
      wire.width_um = parsePositiveDouble(fields[12], "W width", line_number);
      parsePositiveDouble(fields[13], "W resistance", line_number);
      if (fields[14].size() != 1 || (fields[14][0] != 'h' && fields[14][0] != 'v')) {
        throw std::runtime_error("Invalid W direction at line " + std::to_string(line_number) + ": " + fields[14]);
      }
      if (!wire_index_by_id.emplace(wire.id, wires.size()).second) {
        throw std::runtime_error("Duplicate W identifier at line " + std::to_string(line_number) + ": " + wire.id);
      }
      wires.push_back(std::move(wire));
      continue;
    }

    if (fields[0] == "WS") {
      if (fields.size() != 12) {
        throw std::runtime_error("Expected 12 fields for WS at line " + std::to_string(line_number));
      }
      WireSegmentFields segment;
      segment.id = fields[1];
      if (!wire_segment_ids.insert(segment.id).second) {
        throw std::runtime_error("Duplicate WS identifier at line " + std::to_string(line_number) + ": " + segment.id);
      }
      segment.parent_id = getParentWireId(segment.id, line_number);
      segment.first_x_um = parseCoordinate(fields[2], "WS first x coordinate", line_number);
      segment.first_y_um = parseCoordinate(fields[3], "WS first y coordinate", line_number);
      segment.second_x_um = parseCoordinate(fields[4], "WS second x coordinate", line_number);
      segment.second_y_um = parseCoordinate(fields[5], "WS second y coordinate", line_number);
      segment.resistance_ohm = parsePositiveDouble(fields[10], "WS resistance", line_number);
      segment.line_number = line_number;
      wire_segments.push_back(std::move(segment));
      continue;
    }

    if (fields[0] == "V") {
      if (fields.size() < 18) {
        throw std::runtime_error("Expected at least 18 fields for V at line " + std::to_string(line_number));
      }
      RedHawkViaRecord via;
      via.id = fields[1];
      via.layer_name = fields[2];
      via.via_name = fields[3];
      via.net_name = fields[4];
      via.x_um = parseCoordinate(fields[5], "V x coordinate", line_number);
      via.y_um = parseCoordinate(fields[6], "V y coordinate", line_number);
      via.cut_num = parsePositiveInteger(fields[7], "V cut count", line_number);
      via.cut_width_um = parsePositiveDouble(fields[8], "V cut width", line_number);
      via.cut_height_um = parsePositiveDouble(fields[9], "V cut height", line_number);
      via.resistance_ohm = parsePositiveDouble(fields[10], "V resistance", line_number);
      via.connected_wire_segment_ids.assign(fields.begin() + 18, fields.end());
      if (!via_ids.insert(via.id).second) {
        throw std::runtime_error("Duplicate V identifier at line " + std::to_string(line_number) + ": " + via.id);
      }
      network.vias.push_back(std::move(via));
      continue;
    }

    throw std::runtime_error("Unknown RedHawk res_network record at line " + std::to_string(line_number) + ": " + fields[0]);
  }

  for (const WireSegmentFields& segment_fields : wire_segments) {
    auto wire_iter = wire_index_by_id.find(segment_fields.parent_id);
    if (wire_iter == wire_index_by_id.end()) {
      throw std::runtime_error("WS references unknown W at line " + std::to_string(segment_fields.line_number) + ": "
                               + segment_fields.parent_id);
    }
    WireRecord& wire = wires[wire_iter->second];
    RedHawkWireSegmentRecord segment;
    segment.id = segment_fields.id;
    segment.layer_name = wire.layer_name;
    segment.net_name = wire.net_name;
    segment.first_x_um = segment_fields.first_x_um;
    segment.first_y_um = segment_fields.first_y_um;
    segment.second_x_um = segment_fields.second_x_um;
    segment.second_y_um = segment_fields.second_y_um;
    segment.width_um = wire.width_um;
    segment.resistance_ohm = segment_fields.resistance_ohm;
    network.wire_segments.push_back(std::move(segment));
  }
  if (network.wire_segments.empty()) {
    throw std::runtime_error("No wire resistance records in RedHawk res_network file: " + file_path);
  }
  return network;
}

}  // namespace iemir

namespace iemir {

std::optional<std::pair<double, double>> RedHawkResNetworkReader::connectionCoordinate(
    const RedHawkViaRecord& via, const std::unordered_map<std::string, const RedHawkWireSegmentRecord*>& wire_segment_map,
    const std::string& layer_name)
{
  // The listed resistors describe the electrical junction. When multiple
  // segments share an endpoint, that endpoint takes precedence over the via
  // shape's center projection (which can lie on the adjoining pad segment).
  std::set<std::pair<double, double>> common_endpoints;
  std::size_t connected_segments = 0;
  for (const std::string& segment_id : via.connected_wire_segment_ids) {
    auto iter = wire_segment_map.find(segment_id);
    if (iter == wire_segment_map.end() || iter->second->layer_name != layer_name || iter->second->net_name != via.net_name) {
      continue;
    }
    const auto& segment = *iter->second;
    std::set<std::pair<double, double>> endpoints{{segment.first_x_um, segment.first_y_um}, {segment.second_x_um, segment.second_y_um}};
    if (connected_segments++ == 0) {
      common_endpoints = endpoints;
    } else {
      for (auto endpoint = common_endpoints.begin(); endpoint != common_endpoints.end();) {
        if (endpoints.count(*endpoint) == 0) {
          endpoint = common_endpoints.erase(endpoint);
        } else {
          ++endpoint;
        }
      }
    }
  }
  if (connected_segments > 1 && common_endpoints.size() == 1) {
    return *common_endpoints.begin();
  }
  std::optional<std::pair<double, double>> result;
  double minimum_distance_squared = std::numeric_limits<double>::max();
  for (const std::string& segment_id : via.connected_wire_segment_ids) {
    auto segment_iter = wire_segment_map.find(segment_id);
    if (segment_iter == wire_segment_map.end() || segment_iter->second->layer_name != layer_name
        || segment_iter->second->net_name != via.net_name) {
      continue;
    }
    const RedHawkWireSegmentRecord& segment = *segment_iter->second;
    double dx = segment.second_x_um - segment.first_x_um;
    double dy = segment.second_y_um - segment.first_y_um;
    double length_squared = dx * dx + dy * dy;
    if (length_squared <= std::numeric_limits<double>::epsilon()) {
      continue;
    }
    double projection = ((via.x_um - segment.first_x_um) * dx + (via.y_um - segment.first_y_um) * dy) / length_squared;
    projection = std::clamp(projection, 0.0, 1.0);
    double projected_x = segment.first_x_um + projection * dx;
    double projected_y = segment.first_y_um + projection * dy;
    double distance_squared = (via.x_um - projected_x) * (via.x_um - projected_x) + (via.y_um - projected_y) * (via.y_um - projected_y);
    if (distance_squared < minimum_distance_squared) {
      minimum_distance_squared = distance_squared;
      result = std::make_pair(projected_x, projected_y);
    }
  }
  return result;
}

}  // namespace iemir
