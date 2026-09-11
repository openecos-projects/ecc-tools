// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#include "PTPXPowerReader.hpp"

namespace {

constexpr const char* kFormatMarker = "# iEMIR_PTPX_INSTANCE_POWER_V1";
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

std::vector<PTPXPowerRecord> PTPXPowerReader::read(const std::string& file_path)
{
  std::ifstream input(file_path);
  if (!input.is_open()) {
    throw std::runtime_error("Cannot open PT-PX instance power file: " + file_path);
  }

  std::vector<PTPXPowerRecord> records;
  std::unordered_set<std::string> instance_names;
  std::string line;
  std::size_t line_number = 0;
  bool saw_marker = false;
  bool saw_header = false;
  while (std::getline(input, line)) {
    line_number++;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!saw_header && line == kFormatMarker) {
      saw_marker = true;
      continue;
    }
    if (!saw_header && line == kHeader) {
      saw_header = true;
      continue;
    }
    std::string stripped = trim(line);
    if (stripped.empty() || stripped.front() == '#') {
      continue;
    }
    if (!saw_header) {
      throw std::runtime_error("Missing PT-PX column header before line " + std::to_string(line_number));
    }

    std::vector<std::string> fields = splitTab(line);
    if (fields.size() != 7) {
      throw std::runtime_error("Expected 7 tab-separated fields at line " + std::to_string(line_number));
    }
    PTPXPowerRecord record;
    record.instance_name = trim(fields[0]);
    if (record.instance_name.empty()) {
      throw std::runtime_error("Empty instance name at line " + std::to_string(line_number));
    }
    if (!instance_names.insert(record.instance_name).second) {
      throw std::runtime_error("Duplicate instance name at line " + std::to_string(line_number) + ": " + record.instance_name);
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
    records.push_back(std::move(record));
  }
  if (!saw_marker) {
    throw std::runtime_error("Missing PT-PX format marker: " + std::string(kFormatMarker));
  }
  if (!saw_header) {
    throw std::runtime_error("Missing PT-PX column header in " + file_path);
  }
  if (records.empty()) {
    throw std::runtime_error("No PT-PX instance power records in " + file_path);
  }
  return records;
}

}  // namespace iemir
