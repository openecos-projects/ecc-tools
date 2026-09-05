#include <malloc.h>
#include <omp.h>
#include <sys/resource.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "DataManager.hpp"
#include "Database.hpp"
#include "Logger.hpp"
#include "RTInterface.hpp"
#include "design/DesignStore.h"
#include "idm.h"
#include "def/DefDesignImporter.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Options
{
  std::filesystem::path lef;
  std::filesystem::path def;
  std::filesystem::path output;
  std::string source;
};

struct Memory
{
  uint64_t rss_kib = 0;
  uint64_t peak_rss_kib = 0;
  uint64_t allocator_kib = 0;
};

struct SnapshotCounts
{
  uint64_t routing_layers = 0;
  uint64_t cut_layers = 0;
  uint64_t via_masters = 0;
  uint64_t nets = 0;
  uint64_t pins = 0;
  uint64_t pin_routing_shapes = 0;
  uint64_t pin_cut_shapes = 0;
  uint64_t routing_obstacles = 0;
  uint64_t cut_obstacles = 0;
};

uint64_t statusValue(std::string_view field)
{
  std::ifstream input("/proc/self/status");
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream fields(line);
    std::string name;
    uint64_t value = 0;
    std::string unit;
    if ((fields >> name >> value >> unit) && name == field && unit == "kB") {
      return value;
    }
  }
  return 0;
}

Memory memory()
{
  const auto allocator = mallinfo2();
  uint64_t rss_kib = 0;
  std::ifstream input("/proc/self/smaps_rollup");
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream fields(line);
    std::string name;
    uint64_t value = 0;
    std::string unit;
    if ((fields >> name >> value >> unit) && name == "Rss:" && unit == "kB") {
      rss_kib = value;
      break;
    }
  }
  if (rss_kib == 0u) {
    rss_kib = statusValue("VmRSS:");
  }
  return Memory{.rss_kib = rss_kib,
                .peak_rss_kib = statusValue("VmHWM:"),
                .allocator_kib = (static_cast<uint64_t>(allocator.uordblks) + static_cast<uint64_t>(allocator.hblkhd)) / 1024u};
}

Memory settledMemory()
{
  static_cast<void>(malloc_trim(0));
  return memory();
}

uint64_t elapsedNs(Clock::time_point begin)
{
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count());
}

Options parseOptions(int argc, char** argv)
{
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    auto value = [&](std::string_view name) -> std::string {
      if (index + 1 >= argc) {
        throw std::runtime_error("missing value for " + std::string(name));
      }
      return argv[++index];
    };
    if (argument == "--lef") {
      options.lef = value(argument);
    } else if (argument == "--def") {
      options.def = value(argument);
    } else if (argument == "--source") {
      options.source = value(argument);
    } else if (argument == "--output") {
      options.output = value(argument);
    } else if (argument == "--help" || argument == "-h") {
      std::cout << "usage: irt_input_benchmark --lef FILE --def FILE --source entt|idb [--output FILE]\n";
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + std::string(argument));
    }
  }
  if (!std::filesystem::is_regular_file(options.lef)) {
    throw std::runtime_error("LEF is not a regular file: " + options.lef.string());
  }
  if (!std::filesystem::is_regular_file(options.def)) {
    throw std::runtime_error("DEF is not a regular file: " + options.def.string());
  }
  if (options.source != "entt" && options.source != "idb") {
    throw std::runtime_error("--source must be entt or idb");
  }
  return options;
}

SnapshotCounts countSnapshot(irt::Database& database)
{
  SnapshotCounts counts;
  counts.routing_layers = database.get_routing_layer_list().size();
  counts.cut_layers = database.get_cut_layer_list().size();
  for (auto& layer_vias : database.get_layer_via_master_list()) {
    counts.via_masters += layer_vias.size();
  }
  counts.nets = database.get_net_list().size();
  for (auto& net : database.get_net_list()) {
    counts.pins += net.get_pin_list().size();
    for (auto& pin : net.get_pin_list()) {
      counts.pin_routing_shapes += pin.get_routing_shape_list().size();
      counts.pin_cut_shapes += pin.get_cut_shape_list().size();
    }
  }
  counts.routing_obstacles = database.get_routing_obstacle_list().size();
  counts.cut_obstacles = database.get_cut_obstacle_list().size();
  return counts;
}

void loadIdb(const Options& options)
{
  dmInst->reset();
  std::vector<std::string> lef_files{options.lef.string()};
  if (!dmInst->readLef(lef_files, true)) {
    throw std::runtime_error("iDB LEF read failed");
  }
  if (!dmInst->readDef(options.def.string())) {
    throw std::runtime_error("iDB DEF read failed");
  }
}

struct EnttContext
{
  std::unique_ptr<eccdb::TechStore> tech;
  std::unique_ptr<eccdb::LibraryStore> library;
  std::unique_ptr<eccdb::DesignStore> design;
};

EnttContext loadEntt(const Options& options)
{
  EnttContext context;
  context.tech = std::make_unique<eccdb::TechStore>();
  eccdb::LefTechImporter(*context.tech).import(options.lef);
  context.library = std::make_unique<eccdb::LibraryStore>(context.tech->techRegistry());
  eccdb::LefLibraryImporter(*context.tech, *context.library).import(options.lef);
  context.design = std::make_unique<eccdb::DesignStore>(context.tech->techRegistry(), context.library->libraryRegistry());
  eccdb::DefDesignImporter(*context.design).import(options.def);
  return context;
}

void writeJson(std::ostream& output, const Options& options, const Memory& before, const Memory& after, uint64_t elapsed_ns,
               const SnapshotCounts& counts)
{
  const uint64_t rss_delta_kib = after.rss_kib >= before.rss_kib ? after.rss_kib - before.rss_kib : 0u;
  const uint64_t allocator_delta_kib = after.allocator_kib >= before.allocator_kib ? after.allocator_kib - before.allocator_kib : 0u;
  const uint64_t materialized_shapes
      = counts.pin_routing_shapes + counts.pin_cut_shapes + counts.routing_obstacles + counts.cut_obstacles;
  output << "{\"case\":\"" << options.def.stem().string() << "\",\"source\":\"" << options.source
         << "\",\"operation\":\"irt_database_wrap\",\"threads\":" << omp_get_max_threads()
         << ",\"input_bytes\":" << std::filesystem::file_size(options.def)
         << ",\"elapsed_ns\":" << elapsed_ns << ",\"rss_before_kib\":" << before.rss_kib << ",\"rss_delta_kib\":"
         << rss_delta_kib << ",\"rss_after_kib\":" << after.rss_kib << ",\"peak_rss_kib\":" << after.peak_rss_kib
         << ",\"allocator_delta_kib\":" << allocator_delta_kib << ",\"allocator_after_kib\":" << after.allocator_kib
         << ",\"routing_layers\":" << counts.routing_layers << ",\"cut_layers\":" << counts.cut_layers
         << ",\"via_masters\":" << counts.via_masters << ",\"nets\":" << counts.nets << ",\"pins\":" << counts.pins
         << ",\"pin_routing_shapes\":" << counts.pin_routing_shapes << ",\"pin_cut_shapes\":" << counts.pin_cut_shapes
         << ",\"routing_obstacles\":" << counts.routing_obstacles << ",\"cut_obstacles\":" << counts.cut_obstacles
         << ",\"materialized_shapes\":" << materialized_shapes << ",\"throughput_shapes_per_s\":"
         << (elapsed_ns == 0u ? 0.0 : static_cast<double>(materialized_shapes) * 1.0e9 / elapsed_ns) << "}\n";
}

void run(const Options& options)
{
  EnttContext entt;
  if (options.source == "entt") {
    entt = loadEntt(options);
  } else {
    loadIdb(options);
  }

  irt::Logger::initInst();
  irt::DataManager::initInst();
  if (options.source == "entt") {
    RTI.setDesignSource(entt.design.get(), entt.tech.get(), entt.library.get());
  } else {
    RTI.setDesignSource(nullptr, nullptr, nullptr);
  }

  const Memory before = settledMemory();
  const auto begin = Clock::now();
  RTI.wrapDatabase();
  const uint64_t elapsed_ns = elapsedNs(begin);
  const Memory after = settledMemory();
  const SnapshotCounts counts = countSnapshot(RTDM.getDatabase());

  std::ostream* destination = &std::cout;
  std::ofstream file;
  if (!options.output.empty()) {
    if (const auto parent = options.output.parent_path(); !parent.empty()) {
      std::filesystem::create_directories(parent);
    }
    file.open(options.output, std::ios::out | std::ios::app);
    if (!file) {
      throw std::runtime_error("cannot open output: " + options.output.string());
    }
    destination = &file;
  }
  writeJson(*destination, options, before, after, elapsed_ns, counts);

  RTI.setDesignSource(nullptr, nullptr, nullptr);
  irt::DataManager::destroyInst();
  irt::Logger::destroyInst();
  if (options.source == "idb") {
    dmInst->reset();
  }
}

}  // namespace

int main(int argc, char** argv)
{
  try {
    run(parseOptions(argc, argv));
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "iRT input benchmark failed: " << error.what() << '\n';
    return 1;
  }
}
