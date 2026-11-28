// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#include "InstancePowerReader.hpp"

namespace {

constexpr const char* kHeader
    = "instance_name\tvoltage_v\tinternal_power_w\tswitching_power_w\tleakage_power_w\ttotal_power_w\taverage_current_a";

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

std::vector<std::string> splitTab(const std::string& line)
{
  std::vector<std::string> result;
  std::istringstream stream(line);
  std::string token;
  while (std::getline(stream, token, '\t')) {
    result.push_back(token);
  }
  if (!line.empty() && line.back() == '\t') {
    result.emplace_back();
  }
  return result;
}

double parseNonnegative(const std::string& text, const std::string& field, std::size_t line_number)
{
  std::size_t parsed = 0;
  double value = 0.0;
  try {
    value = std::stod(text, &parsed);
  } catch (const std::exception&) {
    throw std::runtime_error("Invalid " + field + " at line " + std::to_string(line_number) + ": " + text);
  }
  if (parsed != text.size() || !std::isfinite(value) || value < 0.0) {
    throw std::runtime_error("Invalid " + field + " at line " + std::to_string(line_number) + ": " + text);
  }
  return value;
}

bool closeEnough(double lhs, double rhs)
{
  return std::abs(lhs - rhs) <= std::max(1.0e-18, 1.0e-6 * std::max(std::abs(lhs), std::abs(rhs)));
}

}  // namespace

namespace iemir {

std::vector<InstancePowerRecord> InstancePowerReader::read(const std::string& file_path, const std::map<std::string, std::string>& supply_ground_pins)
{
  for (const auto& [power, ground] : supply_ground_pins) {
    if (power.empty() || ground.empty() || power == ground) {
      throw std::runtime_error("supply_ground_pins requires distinct, nonempty power/ground pin names");
    }
  }
  std::ifstream input(file_path);
  if (!input.is_open()) {
    throw std::runtime_error("Cannot open Instance power file: " + file_path);
  }

  std::vector<InstancePowerRecord> records;
  std::map<std::string, std::set<std::string>> instance_pins;
  std::string line;
  std::size_t line_number = 0;
  bool saw_header = false;
  bool per_supply = false;
  while (std::getline(input, line)) {
    line_number++;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!saw_header && (line == kHeader || line == std::string(kHeader) + "\tpower_pin\tground_pin")) {
      saw_header = true;
      per_supply = line != kHeader;
      continue;
    }
    std::string stripped = trim(line);
    if (stripped.empty() || stripped.front() == '#') {
      continue;
    }
    if (!saw_header) {
      throw std::runtime_error("Missing instance-power column header before line " + std::to_string(line_number));
    }

    std::vector<std::string> fields = splitTab(line);
    if (fields.size() != (per_supply ? 9U : 7U)) {
      throw std::runtime_error("Invalid instance-power field count at line " + std::to_string(line_number));
    }
    InstancePowerRecord record;
    record.instance_name = trim(fields[0]);
    if (record.instance_name.empty()) {
      throw std::runtime_error("Empty instance name at line " + std::to_string(line_number));
    }
    if (per_supply) {
      record.power_pin = trim(fields[7]);
      record.ground_pin = trim(fields[8]);
      if (!record.power_pin.empty() && record.ground_pin.empty()) {
        auto ground = supply_ground_pins.find(record.power_pin);
        if (ground != supply_ground_pins.end()) {
          record.ground_pin = ground->second;
        }
      }
      if ((record.power_pin.empty() && !record.ground_pin.empty()) || (!record.power_pin.empty() && record.power_pin == record.ground_pin)) {
        throw std::runtime_error("Invalid power/ground pin pair at line " + std::to_string(line_number));
      }
    }
    auto& pins = instance_pins[record.instance_name];
    if ((!pins.empty() && (record.power_pin.empty() || pins.contains(""))) || !pins.insert(record.power_pin).second) {
      throw std::runtime_error("Duplicate or mixed generic/per-supply power for instance " + record.instance_name);
    }
    record.voltage = parseNonnegative(fields[1], "voltage_v", line_number);
    record.internal_power = parseNonnegative(fields[2], "internal_power_w", line_number);
    record.switching_power = parseNonnegative(fields[3], "switching_power_w", line_number);
    record.leakage_power = parseNonnegative(fields[4], "leakage_power_w", line_number);
    record.total_power = parseNonnegative(fields[5], "total_power_w", line_number);
    record.average_current = parseNonnegative(fields[6], "average_current_a", line_number);
    if (record.voltage <= 0.0) {
      throw std::runtime_error("Non-positive voltage at line " + std::to_string(line_number));
    }
    double component_power = record.internal_power + record.switching_power + record.leakage_power;
    if (!closeEnough(component_power, record.total_power)) {
      throw std::runtime_error("Power components do not match total_power_w at line " + std::to_string(line_number));
    }
    if (!closeEnough(record.voltage * record.average_current, record.total_power)) {
      throw std::runtime_error("voltage_v * average_current_a does not match total_power_w at line " + std::to_string(line_number));
    }
    if (!record.power_pin.empty() && record.ground_pin.empty() && record.average_current != 0.0) {
      throw std::runtime_error("Missing return ground for " + record.instance_name + "/" + record.power_pin
                               + "; provide supply_ground_pins explicitly (Liberty has no unambiguous related_ground_pin)");
    }
    records.push_back(std::move(record));
  }
  if (!saw_header) {
    throw std::runtime_error("Missing instance-power column header in " + file_path);
  }
  if (records.empty()) {
    throw std::runtime_error("No Instance power records in " + file_path);
  }
  return records;
}

}  // namespace iemir
