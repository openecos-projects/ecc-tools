// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <malloc.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "builder.h"
#include "design/DesignStore.h"
#include "design/netlist/model/NetlistComponents.h"
#include "def/DefDesignImporter.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "tech/common/TechLayerTypes.h"

namespace eccdb {
namespace {

enum class DatabaseKind : uint8_t
{
  kLegacyIdb,
  kEntt
};

struct CorpusFiles
{
  std::filesystem::path lef;
  std::filesystem::path def;
};

struct DatabaseCounts
{
  uint64_t layers = 0;
  uint64_t sites = 0;
  uint64_t masters = 0;
  uint64_t master_terms = 0;
  uint64_t rows = 0;
  uint64_t track_grids = 0;
  uint64_t gcell_grids = 0;
  uint64_t instances = 0;
  uint64_t instance_pins = 0;
  uint64_t io_pins = 0;
  uint64_t regular_nets = 0;
  uint64_t special_nets = 0;
  uint64_t vias = 0;
  uint64_t wires = 0;
  uint64_t regions = 0;
  uint64_t groups = 0;
  uint64_t blockages = 0;
  uint64_t fills = 0;
};

struct ProcessMemory
{
  uint64_t allocator_in_use_kib = 0;
  uint64_t rss_kib = 0;
  uint64_t pss_kib = 0;
  uint64_t private_dirty_kib = 0;
  uint64_t anonymous_kib = 0;
  uint64_t peak_rss_kib = 0;
};

struct Measurement
{
  DatabaseCounts counts;
  ProcessMemory baseline;
  ProcessMemory after_lef;
  ProcessMemory after_def;
  uint64_t lef_milliseconds = 0;
  uint64_t def_milliseconds = 0;
};

struct ChildResult
{
  Measurement measurement;
  uint32_t success = 0;
  char error[512]{};
};

CorpusFiles large01Files()
{
  std::filesystem::path root;
  if (const char* value = std::getenv("OPENROAD_SOURCE_DIR"); value != nullptr && *value != '\0') {
    root = value;
  } else {
    root = OPENROAD_SOURCE_DEFAULT;
  }
  return CorpusFiles{.lef = root / "src/gpl/test/nangate45.lef", .def = root / "src/gpl/test/large01.def"};
}

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
  result.allocator_in_use_kib = (static_cast<uint64_t>(allocator.uordblks) + static_cast<uint64_t>(allocator.hblkhd)) / 1024u;
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

DatabaseCounts countLegacyDatabase(::idb::IdbLayout& layout, ::idb::IdbDesign& design)
{
  DatabaseCounts result;
  result.layers = layout.get_layers()->get_layers().size();
  result.sites = layout.get_sites()->get_site_list().size();
  const auto& masters = layout.get_cell_master_list()->get_cell_master();
  result.masters = masters.size();
  for (auto* master : masters) {
    if (master != nullptr) {
      result.master_terms += master->get_term_list().size();
    }
  }
  result.rows = layout.get_rows()->get_row_num();
  result.track_grids = layout.get_track_grid_list()->get_track_grid_num();
  result.gcell_grids = layout.get_gcell_grid_list()->get_gcell_grid_num();
  const auto& instances = design.get_instance_list()->get_instance_list();
  result.instances = instances.size();
  for (auto* instance : instances) {
    if (instance != nullptr && instance->get_pin_list() != nullptr) {
      result.instance_pins += instance->get_pin_list()->get_pin_num();
    }
  }
  result.io_pins = design.get_io_pin_list()->get_pin_num();
  result.regular_nets = design.get_net_list()->get_num();
  result.special_nets = design.get_special_net_list()->get_num();
  result.vias = design.get_via_list()->get_num_via();
  for (auto* net : design.get_net_list()->get_net_list()) {
    if (net != nullptr && net->get_wire_list() != nullptr) {
      result.wires += net->get_wire_list()->get_wire_list().size();
    }
  }
  for (auto* net : design.get_special_net_list()->get_net_list()) {
    if (net != nullptr && net->get_wire_list() != nullptr) {
      result.wires += net->get_wire_list()->get_wire_list().size();
    }
  }
  result.regions = design.get_region_list()->get_num();
  result.groups = design.get_group_list()->get_num();
  result.blockages = design.get_blockage_list()->get_num();
  result.fills = design.get_fill_list()->get_num_fill();
  return result;
}

DatabaseCounts countEnttDatabase(const TechStore& technology, const LibraryStore& library, const DesignStore& design)
{
  DatabaseCounts result;
  const auto* layer_storage = technology.techRegistry().registry().storage<TechLayerInfo>();
  result.layers = layer_storage == nullptr ? 0u : layer_storage->size();
  result.sites = library.siteStorage().siteCount();
  result.masters = library.cellMasterStorage().cellMasterCount();
  result.master_terms = library.masterTermStorage().masterTermCount();
  result.rows = design.floorplanStorage().rowCount();
  result.track_grids = design.floorplanStorage().trackGridCount();
  result.gcell_grids = design.floorplanStorage().gcellGridCount();
  result.instances = design.netlistStorage().instanceCount();
  result.instance_pins = design.netlistStorage().instancePinCount();
  result.io_pins = design.netlistStorage().ioPinCount();
  const auto* special_net_storage = design.designRegistry().registry().storage<DesignSpecialNet>();
  result.special_nets = special_net_storage == nullptr ? 0u : special_net_storage->size();
  result.regular_nets = design.netlistStorage().netCount() - result.special_nets;
  result.vias = design.routingStorage().viaCount();
  result.wires = design.routingStorage().wireCount();
  result.regions = design.constraintStorage().regionCount();
  result.groups = design.constraintStorage().groupCount();
  result.blockages = design.constraintStorage().blockageCount();
  result.fills = design.fillStorage().fillCount();
  return result;
}

Measurement measureLegacyIdb(const CorpusFiles& files)
{
  Measurement result;
  result.baseline = settledProcessMemory();
  auto builder = std::make_unique<::idb::IdbBuilder>();

  const auto lef_start = std::chrono::steady_clock::now();
  std::vector<std::string> lef_files{files.lef.string()};
  auto* lef_service = builder->buildLef(lef_files, false);
  if (lef_service == nullptr || lef_service->get_layout() == nullptr) {
    throw std::runtime_error("legacy iDB failed to import LEF");
  }
  result.lef_milliseconds = elapsedMilliseconds(lef_start);
  result.after_lef = settledProcessMemory();

  const auto def_start = std::chrono::steady_clock::now();
  auto* def_service = builder->buildDef(files.def.string());
  if (def_service == nullptr || def_service->get_design() == nullptr || def_service->get_layout() == nullptr) {
    throw std::runtime_error("legacy iDB failed to import DEF");
  }
  result.def_milliseconds = elapsedMilliseconds(def_start);
  result.counts = countLegacyDatabase(*def_service->get_layout(), *def_service->get_design());
  result.after_def = settledProcessMemory();
  return result;
}

Measurement measureEntt(const CorpusFiles& files)
{
  Measurement result;
  result.baseline = settledProcessMemory();

  const auto lef_start = std::chrono::steady_clock::now();
  auto technology = std::make_unique<TechStore>();
  LefTechImporter(*technology).import(files.lef);
  auto library = std::make_unique<LibraryStore>(technology->techRegistry());
  LefLibraryImporter(*technology, *library).import(files.lef);
  result.lef_milliseconds = elapsedMilliseconds(lef_start);
  result.after_lef = settledProcessMemory();

  const auto def_start = std::chrono::steady_clock::now();
  auto design = std::make_unique<DesignStore>(technology->techRegistry(), library->libraryRegistry());
  DefDesignImporter importer(*design);
  importer.import(files.def);
  if (!importer.diagnostics().empty()) {
    throw std::runtime_error("EnTT DEF import produced diagnostics");
  }
  result.def_milliseconds = elapsedMilliseconds(def_start);
  result.counts = countEnttDatabase(*technology, *library, *design);
  result.after_def = settledProcessMemory();
  return result;
}

bool writeAll(int descriptor, const void* data, std::size_t size)
{
  const auto* bytes = static_cast<const char*>(data);
  while (size != 0) {
    const auto written = write(descriptor, bytes, size);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    bytes += written;
    size -= static_cast<std::size_t>(written);
  }
  return true;
}

bool readAll(int descriptor, void* data, std::size_t size)
{
  auto* bytes = static_cast<char*>(data);
  while (size != 0) {
    const auto received = read(descriptor, bytes, size);
    if (received == 0) {
      return false;
    }
    if (received < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    bytes += received;
    size -= static_cast<std::size_t>(received);
  }
  return true;
}

Measurement runIsolated(DatabaseKind kind, const CorpusFiles& files)
{
  std::cout.flush();
  std::cerr.flush();
  std::fflush(nullptr);
  int descriptors[2]{};
  if (pipe(descriptors) != 0) {
    throw std::runtime_error("pipe failed");
  }
  const pid_t child = fork();
  if (child < 0) {
    close(descriptors[0]);
    close(descriptors[1]);
    throw std::runtime_error("fork failed");
  }
  if (child == 0) {
    close(descriptors[0]);
    ChildResult result;
    try {
      result.measurement = kind == DatabaseKind::kLegacyIdb ? measureLegacyIdb(files) : measureEntt(files);
      result.success = 1;
    } catch (const std::exception& error) {
      std::snprintf(result.error, sizeof(result.error), "%s", error.what());
    } catch (...) {
      std::snprintf(result.error, sizeof(result.error), "unknown child-process failure");
    }
    const bool written = writeAll(descriptors[1], &result, sizeof(result));
    close(descriptors[1]);
    _exit(result.success != 0 && written ? 0 : 1);
  }

  close(descriptors[1]);
  ChildResult result;
  const bool received = readAll(descriptors[0], &result, sizeof(result));
  close(descriptors[0]);
  int status = 0;
  if (waitpid(child, &status, 0) < 0) {
    throw std::runtime_error("waitpid failed");
  }
  if (!received || !WIFEXITED(status) || WEXITSTATUS(status) != 0 || result.success == 0) {
    throw std::runtime_error(result.error[0] == '\0' ? "memory measurement child failed" : result.error);
  }
  return result.measurement;
}

int64_t difference(uint64_t after, uint64_t before)
{
  return static_cast<int64_t>(after) - static_cast<int64_t>(before);
}

double mib(int64_t kib)
{
  return static_cast<double>(kib) / 1024.0;
}

void expectSameCounts(const DatabaseCounts& legacy, const DatabaseCounts& entt)
{
  EXPECT_EQ(legacy.layers, entt.layers);
  EXPECT_EQ(legacy.sites, entt.sites);
  EXPECT_EQ(legacy.masters, entt.masters);
  EXPECT_EQ(legacy.master_terms, entt.master_terms);
  EXPECT_EQ(legacy.rows, entt.rows);
  EXPECT_EQ(legacy.track_grids, entt.track_grids);
  EXPECT_EQ(legacy.gcell_grids, entt.gcell_grids);
  EXPECT_EQ(legacy.instances, entt.instances);
  EXPECT_EQ(legacy.instance_pins, entt.instance_pins);
  EXPECT_EQ(legacy.io_pins, entt.io_pins);
  EXPECT_EQ(legacy.regular_nets, entt.regular_nets);
  EXPECT_EQ(legacy.special_nets, entt.special_nets);
  EXPECT_EQ(legacy.vias, entt.vias);
  EXPECT_EQ(legacy.wires, entt.wires);
  EXPECT_EQ(legacy.regions, entt.regions);
  EXPECT_EQ(legacy.groups, entt.groups);
  EXPECT_EQ(legacy.blockages, entt.blockages);
  EXPECT_EQ(legacy.fills, entt.fills);
}

void printComparison(const Measurement& legacy, const Measurement& entt)
{
  const auto printRow = [](std::string_view name, int64_t idb, int64_t refactor) {
    const double ratio = idb > 0 ? static_cast<double>(refactor) / static_cast<double>(idb) : 0.0;
    std::cout << std::left << std::setw(24) << name << std::right << std::setw(14) << mib(idb) << std::setw(14) << mib(refactor)
              << std::setw(12) << ratio << '\n';
  };
  const auto total = [](const Measurement& value, auto member) {
    return difference(value.after_def.*member, value.baseline.*member);
  };
  const auto lef = [](const Measurement& value, auto member) {
    return difference(value.after_lef.*member, value.baseline.*member);
  };
  const auto design = [](const Measurement& value, auto member) {
    return difference(value.after_def.*member, value.after_lef.*member);
  };

  std::cout << std::fixed << std::setprecision(2) << "\nOpenROAD large01 retained-memory comparison (MiB)\n"
            << "EnTT Design Entity: " << ActiveDesignEntitySchema::storage_bits << " bits ("
            << ActiveDesignEntitySchema::entity_bits << " entity + " << ActiveDesignEntitySchema::version_bits << " version)\n"
            << "normal query indexes retained on both databases\n"
            << "metric                         iDB          EnTT    EnTT/iDB\n";
  printRow("LEF RSS", lef(legacy, &ProcessMemory::rss_kib), lef(entt, &ProcessMemory::rss_kib));
  printRow("Design RSS", design(legacy, &ProcessMemory::rss_kib), design(entt, &ProcessMemory::rss_kib));
  printRow("Total RSS", total(legacy, &ProcessMemory::rss_kib), total(entt, &ProcessMemory::rss_kib));
  printRow("Total PSS", total(legacy, &ProcessMemory::pss_kib), total(entt, &ProcessMemory::pss_kib));
  printRow("Total PrivateDirty", total(legacy, &ProcessMemory::private_dirty_kib), total(entt, &ProcessMemory::private_dirty_kib));
  printRow("Total Anonymous", total(legacy, &ProcessMemory::anonymous_kib), total(entt, &ProcessMemory::anonymous_kib));
  printRow("Total GlibcInUse", total(legacy, &ProcessMemory::allocator_in_use_kib),
           total(entt, &ProcessMemory::allocator_in_use_kib));
  printRow("Peak RSS over baseline", difference(legacy.after_def.peak_rss_kib, legacy.baseline.rss_kib),
           difference(entt.after_def.peak_rss_kib, entt.baseline.rss_kib));
  std::cout << "import time (s): iDB LEF=" << static_cast<double>(legacy.lef_milliseconds) / 1000.0
            << ", DEF=" << static_cast<double>(legacy.def_milliseconds) / 1000.0
            << "; EnTT LEF=" << static_cast<double>(entt.lef_milliseconds) / 1000.0
            << ", DEF=" << static_cast<double>(entt.def_milliseconds) / 1000.0 << '\n'
            << "equal core rows: instances=" << legacy.counts.instances << ", instance pins=" << legacy.counts.instance_pins
            << ", IO pins=" << legacy.counts.io_pins << ", regular nets=" << legacy.counts.regular_nets
            << ", special nets=" << legacy.counts.special_nets << '\n';
}

TEST(DefMemoryComparisonTest, ComparesLarge01InIsolatedProcesses)
{
  if (std::getenv("ECCDB_RUN_LARGE_DEF_MEMORY_TEST") == nullptr) {
    GTEST_SKIP() << "set ECCDB_RUN_LARGE_DEF_MEMORY_TEST=1 to run the large DEF memory comparison";
  }
  const auto files = large01Files();
  ASSERT_TRUE(std::filesystem::is_regular_file(files.lef)) << files.lef;
  ASSERT_TRUE(std::filesystem::is_regular_file(files.def)) << files.def;

  const auto legacy = runIsolated(DatabaseKind::kLegacyIdb, files);
  const auto entt = runIsolated(DatabaseKind::kEntt, files);
  expectSameCounts(legacy.counts, entt.counts);
  EXPECT_EQ(legacy.counts.instances, 274700u);
  EXPECT_EQ(legacy.counts.io_pins, 1849u);
  EXPECT_EQ(legacy.counts.regular_nets, 290354u);
  EXPECT_EQ(legacy.counts.special_nets, 2u);
  EXPECT_EQ(legacy.counts.regular_nets + legacy.counts.special_nets, 290356u);
  EXPECT_GT(difference(legacy.after_def.rss_kib, legacy.baseline.rss_kib), 0);
  EXPECT_GT(difference(entt.after_def.rss_kib, entt.baseline.rss_kib), 0);
  printComparison(legacy, entt);

  testing::Test::RecordProperty("idb_total_rss_kib", difference(legacy.after_def.rss_kib, legacy.baseline.rss_kib));
  testing::Test::RecordProperty("entt_total_rss_kib", difference(entt.after_def.rss_kib, entt.baseline.rss_kib));
  testing::Test::RecordProperty("idb_design_rss_kib", difference(legacy.after_def.rss_kib, legacy.after_lef.rss_kib));
  testing::Test::RecordProperty("entt_design_rss_kib", difference(entt.after_def.rss_kib, entt.after_lef.rss_kib));
}

}  // namespace
}  // namespace eccdb
