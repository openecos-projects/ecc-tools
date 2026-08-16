#include "SpefText.hh"

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace spef {
namespace {

auto stripValuePunctuation(std::string_view value) -> std::string_view
{
  while (!value.empty() && (value.back() == ',' || value.back() == ';')) {
    value.remove_suffix(1);
  }
  return value;
}

}  // namespace

namespace text {

auto trimView(std::string_view value) -> std::string_view
{
  const auto first = value.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string_view::npos) {
    return {};
  }

  const auto last = value.find_last_not_of(" \t\n\r\f\v");
  return value.substr(first, last - first + 1);
}

auto startsWith(std::string_view value,
                std::string_view prefix) -> bool
{
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

auto takeToken(std::string_view& value) -> std::string_view
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

auto parseDouble(std::string_view value) -> std::optional<double>
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

auto parseInt(std::string_view value) -> std::optional<int>
{
  const auto number = parseDouble(value);
  if (!number.has_value()) {
    return std::nullopt;
  }
  return static_cast<int>(*number);
}

auto joinHeaderValues(const std::vector<std::string>& values) -> std::string
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

auto startsWithNameIndex(const std::string& name) -> bool
{
  return name.size() >= 2
         && name.front() == '*'
         && std::isdigit(static_cast<unsigned char>(name[1]));
}

}  // namespace text

double toDouble(const char* text)
{
  return parseParValue(text).selected();
}

ParValue parseParValue(const char* text)
{
  ParValue value;
  if (text == nullptr) {
    return value;
  }

  auto parse_one = [](const char*& cursor) {
    errno = 0;
    char* end = nullptr;
    const double number = std::strtod(cursor, &end);
    if (errno != 0 || end == cursor) {
      return 0.0;
    }
    cursor = end;
    return number;
  };

  const char* cursor = text;
  value.first = parse_one(cursor);
  value.second = value.first;
  value.third = value.first;
  if (*cursor != ':') {
    return value;
  }

  ++cursor;
  value.second = parse_one(cursor);
  if (*cursor != ':') {
    return value;
  }

  ++cursor;
  value.third = parse_one(cursor);
  value.is_triple = true;
  return value;
}

int toInt(const char* text)
{
  if (text == nullptr) {
    return 0;
  }
  return static_cast<int>(std::strtol(text, nullptr, 10));
}

std::size_t toSize(const char* text)
{
  if (text == nullptr) {
    return 0;
  }
  return static_cast<std::size_t>(std::strtoull(text, nullptr, 10));
}

std::string tokenToString(const char* text)
{
  return text == nullptr ? std::string{} : std::string{text};
}

std::string stripQuotes(std::string text)
{
  if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
    std::string result;
    result.reserve(text.size() - 2);
    for (std::size_t index = 1; index + 1 < text.size(); ++index) {
      if (text[index] == '\\' && index + 2 < text.size()) {
        result.push_back(text[++index]);
      } else {
        result.push_back(text[index]);
      }
    }
    return result;
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
  if (!text::startsWithNameIndex(name)) {
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

  for (auto& port : exchange.ports) {
    port.name = expandName(exchange, port.name);
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
    for (auto& inductance : net.inductances) {
      inductance.node1 = expandName(exchange, inductance.node1);
      if (!inductance.node2.empty()) {
        inductance.node2 = expandName(exchange, inductance.node2);
      }
    }
  }

  for (auto& reduced_net : exchange.reduced_nets) {
    reduced_net.name = expandName(exchange, reduced_net.name);
    for (auto& driver : reduced_net.drivers) {
      driver.pin_name = expandName(exchange, driver.pin_name);
      for (auto& load : driver.loads) {
        load.pin_name = expandName(exchange, load.pin_name);
      }
    }
  }
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

}  // namespace spef
