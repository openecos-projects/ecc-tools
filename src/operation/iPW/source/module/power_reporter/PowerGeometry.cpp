// Opt-in spatial publication of the exact instance_power.tsv values.
#include "PowerReporter.hpp"

#include "DataManager.hpp"

namespace ipw {

void PowerReporter::outputInstancePowerGeometry()
{
  const auto& config = PWDM.getConfig();
  const std::filesystem::path destination(config.replay_geometry_directory);
  const auto staging = destination.parent_path() / "power.tmp";
  std::filesystem::remove_all(staging);
  std::filesystem::create_directories(staging / "power_map");
  std::ifstream source(std::filesystem::path(config.pr_temp_directory_path) / "instance_power.tsv");
  if (!source) {
    throw std::runtime_error("Missing instance power report");
  }
  auto open = [&](const std::filesystem::path& name) {
    std::ofstream stream;
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream.open(staging / name);
    stream << std::setprecision(17);
    return stream;
  };
  auto geometry = open("instance_power_geometry.tsv");
  geometry << "# ECOS_REPLAY_POWER_GEOMETRY_V1\n"
           << "instance_name\tlx\tly\thx\thy\tinternal_power_w\tswitching_power_w\tleakage_power_w\ttotal_power_w\n";
  auto layout = open("power_map/layout.csv");
  layout << "pixel_row,pixel_col,lx,ly,ux,uy\n";
  const std::array<std::string, 4> components = {"internal", "switching", "leakage", "total"};
  std::array<std::ofstream, 4> maps;
  for (size_t i = 0; i < components.size(); ++i) {
    maps[i] = open("power_map/" + components[i] + "_power_w.csv");
  }
  size_t reported = 0, matched = 0;
  std::string line;
  while (std::getline(source, line)) {
    if (line.empty() || line[0] == '#' || line.starts_with("instance_name\t")) {
      continue;
    }
    ++reported;
    std::istringstream row(line);
    std::vector<std::string> fields;
    std::string field;
    while (std::getline(row, field, '\t')) fields.push_back(field);
    if (fields.size() != 7) throw std::runtime_error("Invalid instance power row");
    const auto bounds = config.replay_instance_bounds.find(fields[0]);
    if (bounds == config.replay_instance_bounds.end()) continue;
    const auto& b = bounds->second;
    geometry << fields[0];
    for (auto coordinate : b) geometry << '\t' << coordinate;
    for (size_t i = 0; i < components.size(); ++i) {
      size_t consumed = 0;
      const double value = std::stod(fields[i + 2], &consumed);
      if (consumed != fields[i + 2].size() || !std::isfinite(value) || value < 0.0) {
        throw std::runtime_error("Invalid instance power value");
      }
      geometry << '\t' << fields[i + 2];
      maps[i] << fields[i + 2] << '\n';
    }
    geometry << '\n';
    layout << matched++ << ",0," << b[0] << ',' << b[1] << ',' << b[2] << ',' << b[3] << '\n';
  }
  if (source.bad()) throw std::runtime_error("Cannot read instance power report");
  geometry.close();
  layout.close();
  for (auto& map : maps) map.close();
  auto metadata = open("metadata.json");
  metadata << nlohmann::json({{"schema", "ecos.power.geometry.v1"}, {"coordinate_unit", "dbu"},
                             {"value_unit", "W"}, {"reported_instances", reported}, {"matched_instances", matched},
                             {"unresolved_instances", reported - matched},
                             {"basis", PWDM.getDatabase().get_peak_power_summary().get_is_enabled()
                                           ? "per_instance_peak" : "average"}}).dump(2);
  metadata.close();
  // Readers only see the directory after all files have been closed successfully.
  std::filesystem::remove_all(destination);
  std::filesystem::rename(staging, destination);
}

}  // namespace ipw
