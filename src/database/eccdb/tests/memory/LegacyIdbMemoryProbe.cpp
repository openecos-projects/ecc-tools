// SPDX-License-Identifier: Apache-2.0

#include <malloc.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "builder.h"

namespace {

struct ProcessMemory
{
  uint64_t allocator_in_use_kib = 0;
  uint64_t rss_kib = 0;
  uint64_t pss_kib = 0;
  uint64_t private_dirty_kib = 0;
  uint64_t anonymous_kib = 0;
  uint64_t peak_rss_kib = 0;
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
  throw std::runtime_error("missing process status field: " + std::string(field));
}

ProcessMemory readProcessMemory()
{
  const auto allocator = mallinfo2();
  ProcessMemory result;
  result.allocator_in_use_kib
      = (static_cast<uint64_t>(allocator.uordblks) + static_cast<uint64_t>(allocator.hblkhd)) / 1024u;
  result.peak_rss_kib = statusValue("VmHWM:");

  std::ifstream input("/proc/self/smaps_rollup");
  if (!input) {
    throw std::runtime_error("cannot read /proc/self/smaps_rollup");
  }
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream fields(line);
    std::string name;
    uint64_t value = 0;
    std::string unit;
    if (!(fields >> name >> value >> unit) || unit != "kB") {
      continue;
    }
    if (name == "Rss:") {
      result.rss_kib = value;
    } else if (name == "Pss:") {
      result.pss_kib = value;
    } else if (name == "Private_Dirty:") {
      result.private_dirty_kib = value;
    } else if (name == "Anonymous:") {
      result.anonymous_kib = value;
    }
  }
  if (result.rss_kib == 0 || result.pss_kib == 0) {
    throw std::runtime_error("incomplete /proc/self/smaps_rollup data");
  }
  return result;
}

ProcessMemory settledProcessMemory()
{
  static_cast<void>(malloc_trim(0));
  return readProcessMemory();
}

uint64_t elapsedMilliseconds(std::chrono::steady_clock::time_point start)
{
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

void writeMemory(std::ostream& output, const ProcessMemory& value)
{
  output << "{\"allocator_in_use_kib\":" << value.allocator_in_use_kib << ",\"rss_kib\":" << value.rss_kib
         << ",\"pss_kib\":" << value.pss_kib << ",\"private_dirty_kib\":" << value.private_dirty_kib
         << ",\"anonymous_kib\":" << value.anonymous_kib << ",\"peak_rss_kib\":" << value.peak_rss_kib << '}';
}

}  // namespace

int main(int argc, char* argv[])
{
  if (argc != 3) {
    std::cerr << "usage: eccdb_legacy_idb_memory_probe <lef> <def>\n";
    return 2;
  }

  try {
    const std::filesystem::path lef = argv[1];
    const std::filesystem::path def = argv[2];
    if (!std::filesystem::is_regular_file(lef) || !std::filesystem::is_regular_file(def)) {
      throw std::runtime_error("LEF or DEF input is not a regular file");
    }

    const auto baseline = settledProcessMemory();
    auto builder = std::make_unique<idb::IdbBuilder>();

    const auto lef_start = std::chrono::steady_clock::now();
    std::vector<std::string> lef_files{lef.string()};
    auto* lef_service = builder->buildLef(lef_files, false);
    if (lef_service == nullptr || lef_service->get_layout() == nullptr) {
      throw std::runtime_error("legacy iDB failed to import LEF");
    }
    const auto lef_milliseconds = elapsedMilliseconds(lef_start);
    const auto after_lef = settledProcessMemory();

    const auto def_start = std::chrono::steady_clock::now();
    auto* def_service = builder->buildDef(def.string());
    if (def_service == nullptr || def_service->get_layout() == nullptr || def_service->get_design() == nullptr) {
      throw std::runtime_error("legacy iDB failed to import DEF");
    }
    const auto def_milliseconds = elapsedMilliseconds(def_start);
    const auto after_def = settledProcessMemory();

    auto* layout = def_service->get_layout();
    auto* design = def_service->get_design();
    uint64_t wire_count = 0;
    for (auto* net : design->get_net_list()->get_net_list()) {
      if (net != nullptr && net->get_wire_list() != nullptr) {
        wire_count += net->get_wire_list()->get_wire_list().size();
      }
    }
    for (auto* net : design->get_special_net_list()->get_net_list()) {
      if (net != nullptr && net->get_wire_list() != nullptr) {
        wire_count += net->get_wire_list()->get_wire_list().size();
      }
    }

    std::cout << "LEGACY_IDB_MEMORY_JSON={\"baseline\":";
    writeMemory(std::cout, baseline);
    std::cout << ",\"after_lef\":";
    writeMemory(std::cout, after_lef);
    std::cout << ",\"after_def\":";
    writeMemory(std::cout, after_def);
    std::cout << ",\"lef_milliseconds\":" << lef_milliseconds << ",\"def_milliseconds\":" << def_milliseconds
              << ",\"counts\":{\"layers\":" << layout->get_layers()->get_layers().size()
              << ",\"instances\":" << design->get_instance_list()->get_instance_list().size()
              << ",\"io_pins\":" << design->get_io_pin_list()->get_pin_num()
              << ",\"regular_nets\":" << design->get_net_list()->get_num()
              << ",\"special_nets\":" << design->get_special_net_list()->get_num() << ",\"vias\":"
              << design->get_via_list()->get_num_via() << ",\"wires\":" << wire_count << "}}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "legacy iDB memory probe failed: " << error.what() << '\n';
    return 1;
  }
}
