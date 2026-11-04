// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// iEDA is licensed under Mulan PSL v2.

#include "PowerSourceReader.hpp"

#include <filesystem>

namespace iemir {

PowerSource PowerSourceReader::parseRecord(const std::vector<std::string>& fields, int32_t micron_dbu,
                                           const std::string& context, bool infer_legacy_name)
{
  auto fail = [&](const std::string& reason) { throw std::runtime_error(context + ": " + reason); };
  if (micron_dbu <= 0) fail("invalid design units");
  if (fields.size() < 5 || fields.size() > 6) fail("expected name x y layer net-or-type [net=NAME]; electrical attributes are unsupported by the ideal-source solver");

  auto coordinate = [&](const std::string& token) {
    double value = 0.0;
    std::size_t end = 0;
    try {
      value = std::stod(token, &end);
    } catch (const std::exception&) {
      fail("invalid coordinate: " + token);
    }
    double dbu = std::round(value * micron_dbu);
    if (end != token.size() || !std::isfinite(value) || !std::isfinite(dbu)
        || dbu < std::numeric_limits<int32_t>::min() || dbu > std::numeric_limits<int32_t>::max()) {
      fail("coordinate outside finite DBU range: " + token);
    }
    return static_cast<int32_t>(dbu);
  };
  PowerSource source;
  source.set_name(fields[0]);
  source.set_x(coordinate(fields[1]));
  source.set_y(coordinate(fields[2]));
  source.set_layer_name(fields[3]);
  std::string type = fields[4];
  std::transform(type.begin(), type.end(), type.begin(), [](unsigned char ch) { return std::toupper(ch); });
  if (type == "POWER" || type == "GROUND") {
    source.set_net_type(type == "POWER" ? PowerNetType::kPower : PowerNetType::kGround);
    if (fields.size() == 6) {
      if (fields[5].rfind("net=", 0) != 0 || fields[5].size() == 4) fail("expected net=NAME; electrical attributes are unsupported");
      source.set_net_name(fields[5].substr(4));
    } else if (infer_legacy_name) {
      // Compatibility for existing benchmark inputs. Explicit net fields
      // never depend on the source's display name.
      std::string marker = "_" + type + "_";
      auto position = fields[0].rfind(marker);
      if (position != std::string::npos && position > 0) source.set_net_name(fields[0].substr(0, position));
    }
  } else {
    if (fields.size() != 5) fail("net specified twice or unsupported source attributes");
    source.set_net_name(fields[4]);
  }
  return source;
}

std::vector<PowerSource> PowerSourceReader::read(const std::string& path, int32_t micron_dbu)
{
  return readFiles({path}, micron_dbu, true);
}

std::vector<PowerSource> PowerSourceReader::read(const std::vector<std::string>& paths, int32_t micron_dbu)
{
  return readFiles(paths, micron_dbu, false);
}

std::vector<PowerSource> PowerSourceReader::readFiles(const std::vector<std::string>& paths, int32_t micron_dbu, bool infer_legacy_name)
{
  if (micron_dbu <= 0) throw std::runtime_error("Invalid design units for supply contacts");
  if (paths.empty()) throw std::runtime_error("No supply contact files");
  std::vector<PowerSource> sources;
  std::set<std::string> names;
  for (const auto& path : paths) {
    auto suffix = std::filesystem::path(path).extension().string();
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char c) { return std::tolower(c); });
    if (suffix == ".pad" || suffix == ".pcell")
      throw std::runtime_error(path + ": local pad definitions are unsupported; provide pre-generated PLOC with absolute coordinates");
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot read supply contacts: " + path);
    const auto first = sources.size();
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
      ++line_number;
      line = line.substr(0, line.find('#'));
      std::istringstream stream(line);
      std::vector<std::string> fields;
      for (std::string field; stream >> field;) fields.push_back(field);
      if (fields.empty()) continue;
      const auto context = path + ": line " + std::to_string(line_number);
      if (fields.size() == 1 && fields[0] == "*PLOC") continue;
      if (fields[0][0] == '*')
        throw std::runtime_error(context + ": unsupported source section; provide pre-generated PLOC with absolute coordinates");
      auto source = parseRecord(fields, micron_dbu, context, infer_legacy_name);
      if (!names.insert(source.get_name()).second)
        throw std::runtime_error(context + ": duplicate supply source name: " + source.get_name());
      sources.push_back(source);
    }
    if (input.bad()) throw std::runtime_error("Failed while reading supply contacts: " + path);
    if (sources.size() == first) throw std::runtime_error(path + ": no supply contacts");
  }
  return sources;
}

}  // namespace iemir
