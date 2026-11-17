// Opt-in spatial publication of the exact instance_power.tsv values.
#include "PowerReporter.hpp"

#include "DataManager.hpp"
#include "GeometryDisplayWriter.h"
#include <memory>

namespace ipw {

void PowerReporter::outputInstancePowerGeometry()
{
  const auto& config = PWDM.getConfig();
  const std::filesystem::path destination(config.replay_geometry_directory);
  const auto staging = destination.parent_path() / "power.tmp";
  std::filesystem::remove_all(staging);
  std::filesystem::create_directories(staging / "power_map");

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
  using Writer = ecc::geometry::GeometryDisplayWriter;
  auto& database = PWDM.getDatabase();
  auto& powers = database.get_instance_power_list();
  auto& peak = database.get_peak_power_summary();
  const bool is_peak = peak.get_is_enabled();
  size_t reported = 0, matched = 0;
  Writer names(
      std::filesystem::path(config.pr_temp_directory_path) / "power-instances.gdisplay",
      {{"kind", "power_instances"}, {"key", "power:instances"}, {"label", "Power instances"}},
      Writer::Encoding::kRecords
  );
  std::array<std::unique_ptr<Writer>, 4> binary;
  for (size_t i = 0; i < components.size(); ++i) {
    binary[i] = std::make_unique<Writer>(
        std::filesystem::path(config.pr_temp_directory_path) / (components[i] + "-power.gdisplay"),
        nlohmann::json{
            {"kind", "power"},
            {"key", "power:" + components[i]},
            {"label", components[i] + " power [W]"},
            {"category", "power_map"},
            {"unit", "W"},
            {"coordinate_unit", "dbu"},
            {"component", components[i]},
            {"rows", config.replay_instance_bounds.size()},
            {"columns", 1},
            {"basis", is_peak ? "per_instance_peak" : "average"}},
        Writer::Encoding::kCells
    );
  }
  for (auto& [name, instance] : database.get_instance_map()) {
    auto& normal = powers.at(instance.get_instance_id());
    auto* value = &normal.get_power_value();
    if (is_peak) {
      auto* p = peak.findInstancePeakValue(instance.get_instance_id());
      if (!p)
        continue;
      value = &p->get_power_value();
    }
    ++reported;
    const auto bounds = config.replay_instance_bounds.find(name);
    if (bounds == config.replay_instance_bounds.end())
      continue;
    const auto& b = bounds->second;
    const double internal = std::max(0.0, value->get_internal_power());
    const std::array<double, 4> values
        = {internal,
           value->get_switching_power(),
           value->get_leakage_power(),
           internal + value->get_switching_power() + value->get_leakage_power()};
    names.record(name);
    geometry << name;
    for (auto coordinate : b)
      geometry << '\t' << coordinate;
    for (size_t i = 0; i < components.size(); ++i) {
      if (!std::isfinite(values[i]) || values[i] < 0.0)
        throw std::runtime_error("Invalid instance power value");
      geometry << '\t' << values[i];
      maps[i] << values[i] << '\n';
      binary[i]->cell(matched, 0, b, values[i]);
    }
    geometry << '\n';
    layout << matched++ << ",0," << b[0] << ',' << b[1] << ',' << b[2] << ',' << b[3] << '\n';
  }
  names.finish();
  for (auto& writer : binary)
    writer->finish();
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
