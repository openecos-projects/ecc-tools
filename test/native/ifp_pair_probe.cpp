// SPDX-License-Identifier: MulanPSL-2.0
//
// One-process iFP provider differential probe.  It intentionally observes the
// shared iFP work model before and after the algorithm, then lets each
// provider perform its normal writeback.  Backend-local object IDs are never
// serialized: model comparison is based on the values iFP actually consumes.

#include <algorithm>
#include <any>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "DataManager.hpp"
#include "FPInterface.hpp"
#include "adapters/ifp/FPInterfaceEccdb.h"
#include "context/DatabaseContext.h"
#include "database/Database.h"
#include "design/DesignStore.h"
#include "geometry/GeometryEntity.h"
#include "idm.h"
#include "json.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;

struct InputSpec
{
  std::vector<std::string> lef_files;
  std::string def_file;
  std::filesystem::path config_file;
  bool binary_checkpoint = false;
};

[[nodiscard]] Json eccdbSummary(const eccdb::Database& database)
{
  const auto& context = database.context();
  const auto& design = context.design();
  const auto& nets = design.netlistStorage();
  const auto& global = design.globalStorage();
  const auto shape_count = design.geometryRegistry().registry().view<const eccdb::GeometryShape>().size();
  return Json{{"backend", "eccdb"},
              {"design", global.info().name},
              {"revision", context.revision()},
              {"instances", nets.instanceCount()},
              {"instance_terms", nets.instancePinCount()},
              {"block_terms", nets.ioPinCount()},
              {"block_pins", nets.blockPinCount()},
              {"nets", nets.netCount()},
              {"wires", design.routingStorage().wireCount()},
              {"design_shapes", shape_count}};
}

void writeBinaryCheckpoint(const eccdb::Database& database, const std::filesystem::path& directory)
{
  std::filesystem::create_directories(directory);
  database.writeBinary({directory / "technology.edb", directory / "library.edb", directory / "design.edb"});
}

Json rectJson(const ifp::PlanarRect& value)
{
  return Json::array({value.get_ll_x(), value.get_ll_y(), value.get_ur_x(), value.get_ur_y()});
}

Json portJson(const ifp::IOPort& port)
{
  return Json{{"layer", port.get_layer_name()},
              {"bounds", rectJson(port)},
              {"origin", Json::array({port.get_x(), port.get_y()})},
              {"placed", port.get_placed()}};
}

Json modelJson(const ifp::Database& database)
{
  Json result{{"snapshot_schema", 2},
              {"database_info",
               {{"design_name", database.get_design_name()},
                {"micron_dbu", database.get_micron_dbu()},
                {"manufacturing_grid", database.get_manufacture_grid()},
                {"cell_area", database.get_cell_area()},
                {"die", rectJson(database.get_die())},
                {"core", rectJson(database.get_core())},
                {"core_site", database.get_core().get_core_site_name()},
                {"io_site", database.get_core().get_io_site_name()},
                {"corner_site", database.get_core().get_corner_site_name()},
                {"die_updated", database.is_die_updated()},
                {"core_updated", database.is_core_updated()},
                {"track_updated", database.is_track_updated()}}},
              {"index_maps",
               {{"instances", database.get_instance_name_to_idx_map()},
                {"routing_layers", database.get_routing_layer_name_to_idx_map()},
                {"io_pins", database.get_io_pin_name_to_idx_map()},
                {"pg_nets", database.get_pg_net_name_to_idx_map()}}},
              {"sites", Json::array()},
              {"masters", Json::array()},
              {"routing_layers", Json::array()},
              {"tracks", Json::array()},
              {"rows", Json::array()},
              {"new_rows", Json::array()},
              {"instances", Json::array()},
              {"io_pins", Json::array()},
              {"nets", Json::array()},
              {"pg_nets", Json::array()},
              {"pg_segments", Json::array()}};

  for (const auto& [name, site] : database.get_site_map()) {
    result["sites"].push_back({{"name", name}, {"stored_name", site.get_name()},
                               {"width", site.get_width()}, {"height", site.get_height()}});
  }
  for (const auto& [name, master] : database.get_cell_master_map()) {
    result["masters"].push_back({{"name", name},
                                 {"stored_name", master.get_name()},
                                 {"width", master.get_width()},
                                 {"height", master.get_height()},
                                 {"pad", master.get_pad()},
                                 {"pad_filler", master.get_pad_filler()},
                                 {"spacer", master.get_spacer()},
                                 {"corner", master.get_corner()}});
  }
  for (const auto& layer : database.get_routing_layer_list()) {
    result["routing_layers"].push_back({{"name", layer.get_name()},
                                        {"layer_idx", layer.get_layer_idx()},
                                        {"order", layer.get_order()},
                                        {"pitch_x", layer.get_pitch_x()},
                                        {"pitch_y", layer.get_pitch_y()},
                                        {"min_width", layer.get_min_width()},
                                        {"min_area", layer.get_min_area()},
                                        {"prefer_track_pitch", layer.get_prefer_track_pitch()},
                                        {"nonprefer_track_pitch", layer.get_nonprefer_track_pitch()},
                                        {"prefer_track_offset", layer.get_prefer_track_offset()},
                                        {"spacing", layer.get_spacing()},
                                        {"prefer_direction", static_cast<int>(layer.get_prefer_direction())}});
  }
  for (const auto& track : database.get_new_track_list()) {
    result["tracks"].push_back({{"layer", track.get_layer_name()},
                                {"x_offset", track.get_x_offset()},
                                {"x_pitch", track.get_x_pitch()},
                                {"y_offset", track.get_y_offset()},
                                {"y_pitch", track.get_y_pitch()}});
  }
  const auto append_row = [](Json& output, const ifp::Row& row) {
    output.push_back({{"name", row.get_name()},
                      {"site", row.get_site_name()},
                      {"site_origin_x", row.get_site_origin_x()},
                      {"y", row.get_y()},
                      {"orientation", static_cast<int>(row.get_orient())},
                      {"bounds", rectJson(row)}});
  };
  for (const auto& row : database.get_row_list()) append_row(result["rows"], row);
  for (const auto& row : database.get_new_row_list()) append_row(result["new_rows"], row);

  for (const auto& instance : database.get_instance_list()) {
    Json item{{"name", instance.get_name()},
              {"master", instance.get_master_name()},
              {"orientation", static_cast<int>(instance.get_orient())},
              {"origin", Json::array({instance.get_x(), instance.get_y()})},
              {"width", instance.get_width()},
              {"height", instance.get_height()},
              {"bounds", rectJson(instance.get_bounding_rect())},
              {"placement_halo", rectJson(instance.get_placement_halo_rect())},
              {"routing_halo", rectJson(instance.get_routing_halo_rect())},
              {"macro", instance.get_macro()},
              {"fixed", instance.get_fixed()},
              {"cover", instance.get_cover()},
              {"placed", instance.get_placed()},
              {"new_instance", instance.is_new_instance()},
              {"placement_updated", instance.is_placement_updated()},
              {"pin_shapes", Json::array()}};
    for (const auto& shape : instance.get_pin_shape_list()) {
      item["pin_shapes"].push_back(
          {{"pin", shape.get_pin_name()}, {"layer", shape.get_layer_name()}, {"bounds", rectJson(shape)}});
    }
    result["instances"].push_back(std::move(item));
  }

  for (const auto& pin : database.get_io_pin_list()) {
    Json item{{"name", pin.get_name()},
              {"instance", pin.get_instance_name()},
              {"orientation", static_cast<int>(pin.get_orient())},
              {"origin", Json::array({pin.get_x(), pin.get_y()})},
              {"offset", Json::array({pin.get_offset_x(), pin.get_offset_y()})},
              {"port_exist", pin.get_port_exist()},
              {"special_net", pin.get_special_net()},
              {"placed", pin.get_placed()},
              {"fixed", pin.get_fixed()},
              {"direct_location", pin.get_direct_location()},
              {"offset_updated", pin.is_offset_updated()},
              {"port_exist_updated", pin.is_port_exist_updated()},
              {"updated", pin.is_updated()},
              {"ports", Json::array()},
              {"new_ports", Json::array()}};
    for (const auto& port : pin.get_port_list()) item["ports"].push_back(portJson(port));
    for (const auto& port : pin.get_new_port_list()) item["new_ports"].push_back(portJson(port));
    result["io_pins"].push_back(std::move(item));
  }

  for (const auto& net : database.get_net_list()) {
    Json item{{"name", net.get_name()},
              {"pin_num", net.get_pin_num()},
              {"clock", net.get_clock()},
              {"pdn", net.get_pdn()},
              {"power", net.get_power()},
              {"ground", net.get_ground()},
              {"pins", Json::array()}};
    for (const auto& pin : net.get_net_pin_list()) {
      item["pins"].push_back({{"instance", pin.get_instance_name()},
                              {"pin", pin.get_pin_name()},
                              {"origin", Json::array({pin.get_x(), pin.get_y()})},
                              {"offset", Json::array({pin.get_offset_x(), pin.get_offset_y()})},
                              {"placed", pin.get_placed()},
                              {"io", pin.get_io()}});
    }
    result["nets"].push_back(std::move(item));
  }

  for (const auto& net : database.get_pg_net_list()) {
    Json item{{"name", net.get_name()},
              {"type", static_cast<int>(net.get_type())},
              {"io_pins", Json::array()},
              {"instance_pin_names", net.get_instance_pin_name_list()}};
    for (const auto& [name, direction] : net.get_io_pin_name_to_direction_map()) {
      item["io_pins"].push_back({{"name", name}, {"direction", static_cast<int>(direction)}});
    }
    result["pg_nets"].push_back(std::move(item));
  }

  // iFP has no persistent TechViaRule model.  Its observable generated-via
  // contract is the layer triplet and cut-array dimensions carried by a via
  // PGSegment, so retain every one of those fields here.
  for (const auto& segment : database.get_pg_segment_list()) {
    result["pg_segments"].push_back({{"net", segment.get_net_name()},
                                     {"layer", segment.get_layer_name()},
                                     {"type", static_cast<int>(segment.get_type())},
                                     {"width", segment.get_width()},
                                     {"start", Json::array({segment.get_start_x(), segment.get_start_y()})},
                                     {"end", Json::array({segment.get_end_x(), segment.get_end_y()})},
                                     {"bottom_layer", segment.get_bottom_layer_name()},
                                     {"cut_layer", segment.get_cut_layer_name()},
                                     {"top_layer", segment.get_top_layer_name()},
                                     {"via_width", segment.get_via_width()},
                                     {"via_height", segment.get_via_height()},
                                     {"generated", segment.get_generated()}});
  }
  return result;
}

void writeJson(const std::filesystem::path& file, const Json& value)
{
  std::ofstream output(file);
  if (!output) throw std::runtime_error("cannot write " + file.string());
  output << value.dump(2) << '\n';
  if (!output) throw std::runtime_error("failed while writing " + file.string());
}

Json processMemory()
{
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  std::ifstream statm("/proc/self/statm");
  uint64_t total_pages = 0;
  uint64_t resident_pages = 0;
  statm >> total_pages >> resident_pages;
  return {{"rss_kib", resident_pages * static_cast<uint64_t>(sysconf(_SC_PAGESIZE)) / 1024},
          {"peak_rss_kib", usage.ru_maxrss}};
}

std::filesystem::path resolvePath(const std::filesystem::path& base, const std::string& value)
{
  const std::filesystem::path path(value);
  return path.is_absolute() ? path : (base / path).lexically_normal();
}

Json readJson(const std::filesystem::path& file)
{
  std::ifstream input(file);
  if (!input) throw std::runtime_error("cannot open " + file.string());
  return Json::parse(input);
}

InputSpec readInputSpec(const std::filesystem::path& manifest_file)
{
  const Json manifest = readJson(manifest_file);
  const auto base = manifest_file.parent_path();
  InputSpec result;
  for (const auto& lef : manifest.at("lef_files")) result.lef_files.push_back(resolvePath(base, lef.get<std::string>()).string());
  if (result.lef_files.empty()) throw std::invalid_argument("manifest lef_files must not be empty");
  result.def_file = resolvePath(base, manifest.at("def_file").get<std::string>()).string();
  result.config_file = resolvePath(base, manifest.at("config").get<std::string>());
  result.binary_checkpoint = manifest.value("binary_checkpoint", false);
  return result;
}

std::filesystem::path materializeConfig(const std::filesystem::path& source, const std::filesystem::path& output_directory)
{
  Json config = readJson(source);
  const auto base = source.parent_path();
  const auto make_absolute = [&](const char* section, const char* key) {
    if (!config.contains(section) || !config[section].contains(key) || !config[section][key].is_string()) return;
    const auto value = config[section][key].get<std::string>();
    if (!value.empty()) config[section][key] = resolvePath(base, value).string();
  };
  make_absolute("macro_placer", "file_path");
  make_absolute("io_placer", "file_path");
  config["ifp"]["temp_directory_path"] = (output_directory / "work").string();
  const auto target = output_directory / "ifp-config.json";
  writeJson(target, config);
  return target;
}

template <typename Function>
void timed(Json& report, const char* key, Function&& function)
{
  const auto begin = Clock::now();
  function();
  report["timing_ms"][key] = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

constexpr std::string_view kNoIoOrderControl = "none";
constexpr std::string_view kStableExactNameIoOrderControl = "stable-exact-name";

bool exactByteLess(const std::string& left, const std::string& right)
{
  return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
                                      [](unsigned char lhs, unsigned char rhs) { return lhs < rhs; });
}

std::string parseIoOrderControl(int argc, char** argv)
{
  if (argc == 4) return std::string(kNoIoOrderControl);

  constexpr std::string_view kPrefix = "--io-order-control=";
  const std::string_view option(argv[4]);
  if (!option.starts_with(kPrefix)) {
    throw std::invalid_argument("optional argument must be --io-order-control=none or --io-order-control=stable-exact-name");
  }

  const std::string_view value = option.substr(kPrefix.size());
  if (value != kNoIoOrderControl && value != kStableExactNameIoOrderControl) {
    throw std::invalid_argument("io order control must be none or stable-exact-name");
  }
  return std::string(value);
}

void applyStableExactNameIoOrderControl(ifp::Database& database, const std::filesystem::path& output_directory)
{
  auto& io_pins = database.get_io_pin_list();
  std::vector<std::string> raw_names;
  raw_names.reserve(io_pins.size());
  std::set<std::string> seen_names;
  for (const auto& pin : io_pins) {
    const auto& name = pin.get_name();
    if (!seen_names.insert(name).second) {
      throw std::runtime_error("stable-exact-name IO order control rejects duplicate exact IO pin name: " + name);
    }
    raw_names.push_back(name);
  }

  std::stable_sort(io_pins.begin(), io_pins.end(), [](const ifp::IOPin& left, const ifp::IOPin& right) {
    return exactByteLess(left.get_name(), right.get_name());
  });

  auto& name_to_index = database.get_io_pin_name_to_idx_map();
  name_to_index.clear();
  std::vector<std::string> controlled_names;
  controlled_names.reserve(io_pins.size());
  for (std::size_t index = 0; index < io_pins.size(); ++index) {
    const auto& name = io_pins[index].get_name();
    name_to_index.emplace(name, static_cast<int32_t>(index));
    controlled_names.push_back(name);
  }

  writeJson(output_directory / "io-order-control.json",
            Json{{"mode", std::string(kStableExactNameIoOrderControl)},
                 {"raw_names", raw_names},
                 {"controlled_names", controlled_names},
                 {"index_rebuilt", true},
                 {"pin_count", io_pins.size()}});
}

}  // namespace

int main(int argc, char** argv)
{
  if (argc != 4 && argc != 5) {
    std::cerr << "usage: ifp_pair_probe <idb|eccdb> <manifest.json> <output-directory> [--io-order-control=none|stable-exact-name]\n";
    return 2;
  }

  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
  const std::string backend = argv[1];
  const auto manifest_file = std::filesystem::absolute(argv[2]);
  const auto output_directory = std::filesystem::absolute(argv[3]);
  std::filesystem::create_directories(output_directory);

  Json report{{"backend", backend}, {"status", "running"}, {"manifest", manifest_file.string()}};
  const auto wall_begin = Clock::now();
  std::unique_ptr<eccdb::Database> native_database;
  bool fp_attempted = false;
  std::string phase = "manifest";
  try {
    if (backend != "idb" && backend != "eccdb") throw std::invalid_argument("backend must be idb or eccdb");
    const std::string io_order_control = parseIoOrderControl(argc, argv);
    report["io_order_control"] = io_order_control;
    const auto input = readInputSpec(manifest_file);
    const auto config = materializeConfig(input.config_file, output_directory);
    report["input"] = {{"def_file", input.def_file}, {"lef_files", input.lef_files}, {"config", config.string()}};

    phase = "import";
    if (backend == "eccdb") {
      timed(report, "import", [&] {
        eccdb::LefDefInput database_input;
        for (const auto& lef : input.lef_files) database_input.lef_files.emplace_back(lef);
        database_input.def_file = input.def_file;
        native_database = std::make_unique<eccdb::Database>(eccdb::Database::open({.input = std::move(database_input)}));
      });
      report["summary_input"] = eccdbSummary(*native_database);
      if (input.binary_checkpoint) {
        timed(report, "binary_checkpoint", [&] { writeBinaryCheckpoint(*native_database, output_directory / "binary-input"); });
      }
    } else {
      timed(report, "import", [&] {
        if (!dmInst->readLef({input.lef_files.front()}, true)) throw std::runtime_error("iDB technology LEF import failed");
        if (input.lef_files.size() > 1u) {
          const std::vector<std::string> library_lefs(input.lef_files.begin() + 1, input.lef_files.end());
          if (!dmInst->readLef(library_lefs)) throw std::runtime_error("iDB library LEF import failed");
        }
        if (!dmInst->readDef(input.def_file)) throw std::runtime_error("iDB DEF import failed");
      });
    }

    phase = "import_export";
    timed(report, "import_export", [&] {
      const auto path = output_directory / "import.def";
      if (native_database) native_database->writeDef(path);
      else if (!dmInst->saveDef(path.string())) throw std::runtime_error("iDB import DEF export failed");
    });

    phase = "init";
    eccdb::adapters::bindFpSource(FPI, native_database ? &native_database->context() : nullptr);
    fp_attempted = true;
    timed(report, "init", [&] {
      std::map<std::string, std::any> values{{"-config", config.string()}};
      FPI.initFP(values);
    });

    phase = "input_model_dump";
    timed(report, "input_model_dump", [&] { writeJson(output_directory / "input-model.json", modelJson(FPDM.getDatabase())); });

    if (io_order_control == kStableExactNameIoOrderControl) {
      phase = "io_order_control";
      timed(report, "io_order_control", [&] { applyStableExactNameIoOrderControl(FPDM.getDatabase(), output_directory); });
      phase = "controlled_input_model_dump";
      timed(report, "controlled_input_model_dump", [&] { writeJson(output_directory / "controlled-input-model.json", modelJson(FPDM.getDatabase())); });
    }

    phase = "algorithm";
    timed(report, "algorithm", [&] { FPI.runFP(); });

    phase = "computed_model_dump";
    timed(report, "computed_model_dump", [&] { writeJson(output_directory / "computed-model.json", modelJson(FPDM.getDatabase())); });

    phase = "writeback";
    timed(report, "writeback", [&] { FPI.destroyFP(); });
    fp_attempted = false;
    if (native_database) {
      report["summary_output"] = eccdbSummary(*native_database);
      if (input.binary_checkpoint) {
        timed(report, "binary_output_checkpoint", [&] { writeBinaryCheckpoint(*native_database, output_directory / "binary-output"); });
      }
    }
    eccdb::adapters::bindFpSource(FPI, nullptr);

    phase = "output_export";
    timed(report, "output_export", [&] {
      const auto path = output_directory / "output.def";
      if (native_database) native_database->writeDef(path);
      else if (!dmInst->saveDef(path.string())) throw std::runtime_error("iDB output DEF export failed");
    });

    report["status"] = "passed";
  } catch (const std::exception& error) {
    report["status"] = "failed";
    report["failed_phase"] = phase;
    report["error"] = error.what();
    if (fp_attempted) {
      try {
        FPI.abortFP();
      } catch (...) {
      }
    }
    eccdb::adapters::bindFpSource(FPI, nullptr);
  }

  report["memory"] = processMemory();
  report["wall_ms"] = std::chrono::duration<double, std::milli>(Clock::now() - wall_begin).count();
  try {
    writeJson(output_directory / "result.json", report);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << report.dump(2) << '\n';
  return report.at("status") == "passed" ? 0 : 1;
}
