// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <malloc.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "IdbObs.h"
#include "builder.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "LefPdkCorpus.h"

namespace eccdb {
namespace {

constexpr std::size_t kDefaultSampleCount = 5;
constexpr std::size_t kFullCorpusSampleCount = 3;

enum class ImportMode : uint8_t
{
  kLegacyIdb,
  kDirectEnttRectangularized
};

struct SemanticCounts
{
  uint64_t layers = 0;
  uint64_t sites = 0;
  uint64_t masters = 0;
  // Logical PIN names scoped to one CellMaster. Legacy iDB can retain more
  // than one raw TERM row for a malformed duplicate PIN declaration.
  uint64_t terms = 0;
  uint64_t ports = 0;
  uint64_t port_vias = 0;
};

struct GeometryCounts
{
  uint64_t groups = 0;
  uint64_t port_rects = 0;
  uint64_t port_polygons = 0;
  uint64_t port_polygon_points = 0;
  uint64_t obs_rects = 0;
  uint64_t obs_polygons = 0;
  uint64_t obs_polygon_points = 0;
};

struct DatabaseCounts
{
  SemanticCounts semantic;
  uint64_t raw_term_rows = 0;
  GeometryCounts geometry;
};

using LefDomainInputs = lef_test::LefPdkDomain;

struct LefInputs
{
  std::string name;
  std::vector<LefDomainInputs> domains;
  std::optional<SemanticCounts> expected;
  std::size_t sample_count = kDefaultSampleCount;
};

struct ProcessMemory
{
  uint64_t allocator_in_use_kib = 0;
  uint64_t peak_rss_kib = 0;
  uint64_t rss_kib = 0;
  uint64_t pss_kib = 0;
  uint64_t private_dirty_kib = 0;
  uint64_t anonymous_kib = 0;
};

struct Measurement
{
  DatabaseCounts counts;
  ProcessMemory baseline;
  ProcessMemory retained;
  uint64_t import_milliseconds = 0;
  uint64_t tech_import_milliseconds = 0;
  uint64_t library_import_milliseconds = 0;
  uint64_t parser_microseconds = 0;
  uint64_t site_callback_microseconds = 0;
  uint64_t macro_callback_microseconds = 0;
  uint64_t pin_callback_microseconds = 0;
  uint64_t obstruction_callback_microseconds = 0;
  uint64_t geometry_prepare_microseconds = 0;
  uint64_t geometry_write_microseconds = 0;
};

struct ChildResult
{
  Measurement measurement;
  uint32_t success = 0;
  char error[512]{};
};

LefInputs sky130Inputs()
{
  const auto root = std::filesystem::path{ECC_TOOLS_SOURCE_DIR};
  const auto lef_root = root / "scripts/foundry/sky130/lef";
  return LefInputs{.name = "Sky130 HD",
                   .domains = {{.name = "Sky130 HD",
                                .technology = lef_root / "sky130_fd_sc_hd.tlef",
                                .cells = {lef_root / "sky130_fd_sc_hd_merged.lef"},
                                .known_master = "sky130_fd_sc_hd__a2111o_1"}},
                   .expected = SemanticCounts{.layers = 13, .sites = 2, .masters = 437, .terms = 2662, .ports = 3775}};
}

LefInputs ihp130Inputs()
{
  const auto root = std::filesystem::path{ECC_TOOLS_SOURCE_DIR};
  const auto lef_root = root / "scripts/foundry/ihp130/ihp-sg13g2/libs.ref/sg13g2_stdcell/lef";
  return LefInputs{.name = "IHP130 SG13G2",
                   .domains = {{.name = "IHP130 SG13G2",
                                .technology = lef_root / "sg13g2_tech.lef",
                                .cells = {lef_root / "sg13g2_stdcell.lef"},
                                .known_master = "sg13g2_a21o_1"}},
                   .expected = SemanticCounts{.layers = 19, .sites = 1, .masters = 78, .terms = 405, .ports = 405}};
}

std::optional<LefInputs> ics55Inputs()
{
  const auto* value = std::getenv("ECC_TOOLS_ICS55_PDK");
  if (value == nullptr || value[0] == '\0') {
    return std::nullopt;
  }

  const auto root = std::filesystem::path{value};
  return LefInputs{.name = "ICsprout55 normal standard-cell and IO libraries",
                   .domains = {{.name = "ICsprout55",
                                .technology = root / "prtech/techLEF/N551P6M.lef",
                                .cells = {root / "IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CL/lef/ics55_LLSC_H7CL.lef",
                                          root / "IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CH/lef/ics55_LLSC_H7CH.lef",
                                          root / "IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CR/lef/ics55_LLSC_H7CR.lef",
                                          root / "IP/IO/ICsprout_55LLULP1233_IO_251013/lef/ICSIOA_N55_3P3_1P6M1TM.lef"},
                                .known_master = "ADDFX1P4H7L"}},
                   .sample_count = kFullCorpusSampleCount};
}

LefInputs fullSky130Inputs()
{
  const auto root = std::filesystem::path{ECC_TOOLS_SOURCE_DIR};
  return LefInputs{.name = "Sky130 full corpus (HD + HS domains)",
                   .domains = lef_test::fullSky130Corpus(root),
                   .expected = SemanticCounts{.layers = 26, .sites = 4, .masters = 855, .terms = 7306, .ports = 9425},
                   .sample_count = kFullCorpusSampleCount};
}

LefInputs fullIhp130Inputs()
{
  const auto root = std::filesystem::path{ECC_TOOLS_SOURCE_DIR};
  return LefInputs{.name = "IHP130 full SG13G2 corpus",
                   .domains = lef_test::fullIhp130Corpus(root),
                   .expected = SemanticCounts{.layers = 19, .sites = 2, .masters = 110, .terms = 2926, .ports = 4980},
                   .sample_count = kFullCorpusSampleCount};
}

ProcessMemory readProcessMemory()
{
  const auto allocator = mallinfo2();
  ProcessMemory result;
  result.allocator_in_use_kib = (static_cast<uint64_t>(allocator.uordblks) + static_cast<uint64_t>(allocator.hblkhd)) / 1024u;

  {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
      std::istringstream fields(line);
      std::string name;
      uint64_t value = 0;
      std::string unit;
      if ((fields >> name >> value >> unit) && name == "VmHWM:" && unit == "kB") {
        result.peak_rss_kib = value;
        break;
      }
    }
  }

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
  if (result.peak_rss_kib == 0 || result.rss_kib == 0 || result.pss_kib == 0) {
    throw std::runtime_error("incomplete /proc/self/smaps_rollup data");
  }
  return result;
}

ProcessMemory settledProcessMemory()
{
  static_cast<void>(malloc_trim(0));
  return readProcessMemory();
}

DatabaseCounts countLegacyDatabase(::idb::IdbLayout& layout)
{
  DatabaseCounts result;
  result.semantic.layers = layout.get_layers()->get_layers().size();
  result.semantic.sites = layout.get_sites()->get_site_list().size();
  const auto& masters = layout.get_cell_master_list()->get_cell_master();
  result.semantic.masters = masters.size();
  for (auto* master : masters) {
    if (master == nullptr) {
      continue;
    }
    const auto terms = master->get_term_list();
    std::unordered_set<std::string> term_names;
    term_names.reserve(terms.size());
    for (auto* term : terms) {
      if (term == nullptr) {
        continue;
      }
      ++result.raw_term_rows;
      if (term_names.emplace(term->get_name()).second) {
        ++result.semantic.terms;
      }
      const auto& ports = term->get_port_list();
      result.semantic.ports += ports.size();
      for (auto* port : ports) {
        if (port == nullptr) {
          continue;
        }
        result.semantic.port_vias += port->get_via_list().size();
        for (auto* shape : port->get_layer_shape()) {
          if (shape != nullptr) {
            result.geometry.port_rects += shape->get_rect_list().size();
          }
        }
      }
    }
    for (auto* obs : master->get_obs_list()) {
      if (obs == nullptr) {
        continue;
      }
      for (auto* obs_layer : obs->get_obs_layer_list()) {
        if (obs_layer != nullptr && obs_layer->get_shape() != nullptr) {
          result.geometry.obs_rects += obs_layer->get_shape()->get_rect_list().size();
        }
      }
    }
  }
  return result;
}

void countPortGeometry(GeometryCounts& result, const GeometryPool& geometry, const GeometryHandle& reference)
{
  result.port_rects += geometry.rectangles(reference).size();
  const auto polygon_count = geometry.polygonCount(reference);
  result.port_polygons += polygon_count;
  for (uint32_t index = 0; index < polygon_count; ++index) {
    result.port_polygon_points += geometry.polygonPoints(reference, index).size();
  }
}

void countObsGeometry(GeometryCounts& result, const GeometryPool& geometry, const GeometryHandle& reference)
{
  result.obs_rects += geometry.rectangles(reference).size();
  const auto polygon_count = geometry.polygonCount(reference);
  result.obs_polygons += polygon_count;
  for (uint32_t index = 0; index < polygon_count; ++index) {
    result.obs_polygon_points += geometry.polygonPoints(reference, index).size();
  }
}

DatabaseCounts countDirectDatabase(const TechStore& technology, const LibraryStore& library)
{
  DatabaseCounts result;
  result.geometry.groups = static_cast<uint64_t>(technology.geometryPool().entryCount()) + library.geometryPool().entryCount();
  const auto layers = technology.techRegistry().registry().view<const TechLayerInfo>();
  for ([[maybe_unused]] const auto entity : layers) {
    ++result.semantic.layers;
  }
  result.semantic.sites = library.siteStorage().siteCount();
  result.semantic.masters = library.cellMasterStorage().cellMasterCount();
  result.semantic.terms = library.masterTermStorage().masterTermCount();
  result.raw_term_rows = result.semantic.terms;
  result.semantic.ports = library.masterPortStorage().masterPortCount();

  const auto& registry = library.libraryRegistry().registry();
  const auto& geometry = library.geometryPool();
  const auto ports = registry.view<const LibraryMasterPort>();
  for (const auto entity : ports) {
    const auto& port = ports.get<const LibraryMasterPort>(entity);
    for (const auto& clause : port.layer_clauses) {
      countPortGeometry(result.geometry, geometry, clause.geometry);
    }
    result.semantic.port_vias += port.vias.size();
  }
  const auto obstructions = registry.view<const LibraryMasterObs>();
  for (const auto entity : obstructions) {
    const auto& obs = obstructions.get<const LibraryMasterObs>(entity);
    for (const auto& clause : obs.layer_clauses) {
      countObsGeometry(result.geometry, geometry, clause.geometry);
    }
  }
  return result;
}

void addCounts(DatabaseCounts& target, const DatabaseCounts& source)
{
  target.semantic.layers += source.semantic.layers;
  target.semantic.sites += source.semantic.sites;
  target.semantic.masters += source.semantic.masters;
  target.semantic.terms += source.semantic.terms;
  target.semantic.ports += source.semantic.ports;
  target.semantic.port_vias += source.semantic.port_vias;
  target.raw_term_rows += source.raw_term_rows;
  target.geometry.groups += source.geometry.groups;
  target.geometry.port_rects += source.geometry.port_rects;
  target.geometry.port_polygons += source.geometry.port_polygons;
  target.geometry.port_polygon_points += source.geometry.port_polygon_points;
  target.geometry.obs_rects += source.geometry.obs_rects;
  target.geometry.obs_polygons += source.geometry.obs_polygons;
  target.geometry.obs_polygon_points += source.geometry.obs_polygon_points;
}

Measurement measureLegacyIdb(const LefInputs& inputs)
{
  Measurement result;
  result.baseline = settledProcessMemory();
  const auto import_start = std::chrono::steady_clock::now();

  std::vector<std::unique_ptr<::idb::IdbBuilder>> builders;
  std::vector<::idb::IdbLayout*> layouts;
  builders.reserve(inputs.domains.size());
  layouts.reserve(inputs.domains.size());
  for (const auto& domain : inputs.domains) {
    auto builder = std::make_unique<::idb::IdbBuilder>();
    std::vector<std::string> technology_files{domain.technology.string()};
    auto* service = builder->buildLef(technology_files, true);
    if (service == nullptr || service->get_layout() == nullptr) {
      throw std::runtime_error("legacy iDB failed to import the " + domain.name + " technology LEF");
    }
    auto* layout = service->get_layout();
    layout->get_cell_master_list()->set_name_index_mode(::idb::IdbCellMasterList::NameIndexMode::kDisabled);

    std::vector<std::string> cell_files;
    cell_files.reserve(domain.cells.size());
    for (const auto& cell : domain.cells) {
      cell_files.push_back(cell.string());
    }
    service = builder->buildLef(cell_files, false);
    if (service == nullptr || service->get_layout() == nullptr) {
      throw std::runtime_error("legacy iDB failed to import the " + domain.name + " cell LEF");
    }
    layout = service->get_layout();
    if (layout->get_cell_master_list()->get_name_index_mode() != ::idb::IdbCellMasterList::NameIndexMode::kDisabled
        || layout->get_cell_master_list()->find_cell_master(domain.known_master) == nullptr) {
      throw std::runtime_error("legacy iDB name-index switch is not preserving lookup behavior");
    }
    layouts.push_back(layout);
    builders.push_back(std::move(builder));
  }

  result.retained = settledProcessMemory();
  for (auto* layout : layouts) {
    addCounts(result.counts, countLegacyDatabase(*layout));
  }
  result.import_milliseconds = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - import_start).count());
  return result;
}

Measurement measureDirectEnttRectangularized(const LefInputs& inputs)
{
  Measurement result;
  result.baseline = settledProcessMemory();
  const auto import_start = std::chrono::steady_clock::now();

  struct DirectDomain
  {
    std::unique_ptr<TechStore> technology;
    std::unique_ptr<LibraryStore> library;
  };
  std::vector<DirectDomain> domains;
  domains.reserve(inputs.domains.size());

  for (const auto& domain : inputs.domains) {
    const GeometryPoolOptions geometry_options{.polygon_mode = PolygonStorageMode::kRectangularized};
    auto technology = std::make_unique<TechStore>(TechStoreOptions{.geometry = geometry_options});
    {
      const auto start = std::chrono::steady_clock::now();
      LefTechImporter importer(*technology);
      importer.import(domain.technology);
      result.tech_import_milliseconds += static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    }
    auto library = std::make_unique<LibraryStore>(technology->techRegistry(), LibraryStoreOptions{.geometry = geometry_options});
    {
      LefLibraryImporter importer(*technology, *library);
      std::vector<std::filesystem::path> files;
      files.reserve(domain.cells.size() + 1u);
      files.push_back(domain.technology);
      files.insert(files.end(), domain.cells.begin(), domain.cells.end());
      const auto start = std::chrono::steady_clock::now();
      importer.import(files);
      result.library_import_milliseconds += static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
      result.parser_microseconds += importer.timing().parser_microseconds;
      result.site_callback_microseconds += importer.timing().site_callback_microseconds;
      result.macro_callback_microseconds += importer.timing().macro_callback_microseconds;
      result.pin_callback_microseconds += importer.timing().pin_callback_microseconds;
      result.obstruction_callback_microseconds += importer.timing().obstruction_callback_microseconds;
      result.geometry_prepare_microseconds += importer.timing().geometry_prepare_microseconds;
      result.geometry_write_microseconds += importer.timing().geometry_write_microseconds;
    }
    if (!library->cellMasterStorage().findCellMaster(domain.known_master)) {
      throw std::runtime_error("direct EnTT failed to preserve known CellMaster: " + domain.known_master);
    }
    domains.push_back(DirectDomain{.technology = std::move(technology), .library = std::move(library)});
  }

  result.retained = settledProcessMemory();
  for (const auto& domain : domains) {
    addCounts(result.counts, countDirectDatabase(*domain.technology, *domain.library));
  }
  result.import_milliseconds = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - import_start).count());
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

Measurement runIsolated(ImportMode mode, const LefInputs& inputs)
{
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
      result.measurement = mode == ImportMode::kLegacyIdb ? measureLegacyIdb(inputs) : measureDirectEnttRectangularized(inputs);
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

template <typename Getter>
int64_t median(const std::vector<Measurement>& samples, Getter getter)
{
  if (samples.empty()) {
    throw std::invalid_argument("cannot calculate a median from no measurements");
  }
  std::vector<int64_t> values;
  values.reserve(samples.size());
  std::transform(samples.begin(), samples.end(), std::back_inserter(values), getter);
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

void expectSameSemanticCounts(const DatabaseCounts& lhs, const DatabaseCounts& rhs)
{
  EXPECT_EQ(lhs.semantic.layers, rhs.semantic.layers);
  EXPECT_EQ(lhs.semantic.sites, rhs.semantic.sites);
  EXPECT_EQ(lhs.semantic.masters, rhs.semantic.masters);
  EXPECT_EQ(lhs.semantic.terms, rhs.semantic.terms);
  EXPECT_EQ(lhs.semantic.ports, rhs.semantic.ports);
  EXPECT_EQ(lhs.semantic.port_vias, rhs.semantic.port_vias);
}

void expectSameRawRectangleCounts(const DatabaseCounts& lhs, const DatabaseCounts& rhs)
{
  EXPECT_EQ(lhs.geometry.port_rects, rhs.geometry.port_rects);
  EXPECT_EQ(lhs.geometry.obs_rects, rhs.geometry.obs_rects);
  EXPECT_EQ(lhs.geometry.port_polygons, 0u);
  EXPECT_EQ(lhs.geometry.port_polygon_points, 0u);
  EXPECT_EQ(lhs.geometry.obs_polygons, 0u);
  EXPECT_EQ(lhs.geometry.obs_polygon_points, 0u);
}

void expectSameObservedCounts(const DatabaseCounts& lhs, const DatabaseCounts& rhs)
{
  expectSameSemanticCounts(lhs, rhs);
  EXPECT_EQ(lhs.raw_term_rows, rhs.raw_term_rows);
  EXPECT_EQ(lhs.geometry.port_rects, rhs.geometry.port_rects);
  EXPECT_EQ(lhs.geometry.port_polygons, rhs.geometry.port_polygons);
  EXPECT_EQ(lhs.geometry.port_polygon_points, rhs.geometry.port_polygon_points);
  EXPECT_EQ(lhs.geometry.obs_rects, rhs.geometry.obs_rects);
  EXPECT_EQ(lhs.geometry.obs_polygons, rhs.geometry.obs_polygons);
  EXPECT_EQ(lhs.geometry.obs_polygon_points, rhs.geometry.obs_polygon_points);
}

void expectExpectedSemanticCounts(const DatabaseCounts& actual, const SemanticCounts& expected)
{
  EXPECT_EQ(actual.semantic.layers, expected.layers);
  EXPECT_EQ(actual.semantic.sites, expected.sites);
  EXPECT_EQ(actual.semantic.masters, expected.masters);
  EXPECT_EQ(actual.semantic.terms, expected.terms);
  EXPECT_EQ(actual.semantic.ports, expected.ports);
  EXPECT_EQ(actual.semantic.port_vias, expected.port_vias);
}

double mib(int64_t kib)
{
  return static_cast<double>(kib) / 1024.0;
}

void compareRectangleStorageImports(const LefInputs& inputs)
{
  SCOPED_TRACE(inputs.name);
  ASSERT_FALSE(inputs.domains.empty());
  ASSERT_GT(inputs.sample_count, 0u);
  for (const auto& domain : inputs.domains) {
    SCOPED_TRACE(domain.name);
    ASSERT_TRUE(std::filesystem::exists(domain.technology));
    ASSERT_FALSE(domain.cells.empty());
    for (const auto& cell : domain.cells) {
      ASSERT_TRUE(std::filesystem::exists(cell));
    }
  }

  std::vector<Measurement> idb_samples;
  std::vector<Measurement> entt_samples;
  idb_samples.reserve(inputs.sample_count);
  entt_samples.reserve(inputs.sample_count);
  for (std::size_t index = 0; index < inputs.sample_count; ++index) {
    if (index % 2 == 0) {
      idb_samples.push_back(runIsolated(ImportMode::kLegacyIdb, inputs));
      entt_samples.push_back(runIsolated(ImportMode::kDirectEnttRectangularized, inputs));
    } else {
      entt_samples.push_back(runIsolated(ImportMode::kDirectEnttRectangularized, inputs));
      idb_samples.push_back(runIsolated(ImportMode::kLegacyIdb, inputs));
    }
    expectSameObservedCounts(idb_samples.front().counts, idb_samples.back().counts);
    expectSameObservedCounts(entt_samples.front().counts, entt_samples.back().counts);
    expectSameRawRectangleCounts(idb_samples.front().counts, idb_samples.back().counts);
    expectSameRawRectangleCounts(entt_samples.front().counts, entt_samples.back().counts);
  }
  expectSameSemanticCounts(idb_samples.front().counts, entt_samples.front().counts);
  expectSameRawRectangleCounts(idb_samples.front().counts, entt_samples.front().counts);
  if (inputs.expected) {
    expectExpectedSemanticCounts(idb_samples.front().counts, *inputs.expected);
    expectExpectedSemanticCounts(entt_samples.front().counts, *inputs.expected);
  }

  const auto rss_delta = [](const Measurement& value) { return difference(value.retained.rss_kib, value.baseline.rss_kib); };
  const auto peak_rss_delta = [](const Measurement& value) { return difference(value.retained.peak_rss_kib, value.baseline.peak_rss_kib); };
  const auto allocator_delta
      = [](const Measurement& value) { return difference(value.retained.allocator_in_use_kib, value.baseline.allocator_in_use_kib); };
  const auto pss_delta = [](const Measurement& value) { return difference(value.retained.pss_kib, value.baseline.pss_kib); };
  const auto private_dirty_delta
      = [](const Measurement& value) { return difference(value.retained.private_dirty_kib, value.baseline.private_dirty_kib); };
  const auto anonymous_delta
      = [](const Measurement& value) { return difference(value.retained.anonymous_kib, value.baseline.anonymous_kib); };

  const int64_t idb_rss = median(idb_samples, rss_delta);
  const int64_t idb_peak_rss = median(idb_samples, peak_rss_delta);
  const int64_t idb_allocator = median(idb_samples, allocator_delta);
  const int64_t idb_pss = median(idb_samples, pss_delta);
  const int64_t idb_private_dirty = median(idb_samples, private_dirty_delta);
  const int64_t idb_anonymous = median(idb_samples, anonymous_delta);
  const int64_t entt_rss = median(entt_samples, rss_delta);
  const int64_t entt_peak_rss = median(entt_samples, peak_rss_delta);
  const int64_t entt_allocator = median(entt_samples, allocator_delta);
  const int64_t entt_pss = median(entt_samples, pss_delta);
  const int64_t entt_private_dirty = median(entt_samples, private_dirty_delta);
  const int64_t entt_anonymous = median(entt_samples, anonymous_delta);

  EXPECT_GT(idb_rss, 0);
  EXPECT_GT(idb_allocator, 0);
  EXPECT_GT(idb_pss, 0);
  EXPECT_GT(idb_private_dirty, 0);
  EXPECT_GT(entt_rss, 0);
  EXPECT_GT(entt_allocator, 0);
  EXPECT_GT(entt_pss, 0);
  EXPECT_GT(entt_private_dirty, 0);

  std::cout << std::fixed << std::setprecision(2) << '\n'
            << inputs.name << " LEF retained-memory median, " << inputs.sample_count << " isolated samples (MiB)\n"
            << "IdbCellMasterList name index disabled before cell import\n"
            << "equal semantic rows: layers=" << idb_samples.front().counts.semantic.layers
            << ", sites=" << idb_samples.front().counts.semantic.sites << ", masters=" << idb_samples.front().counts.semantic.masters
            << ", terms=" << idb_samples.front().counts.semantic.terms << ", ports=" << idb_samples.front().counts.semantic.ports
            << ", port vias=" << idb_samples.front().counts.semantic.port_vias << '\n'
            << "raw TERM rows: iDB=" << idb_samples.front().counts.raw_term_rows
            << ", direct EnTT=" << entt_samples.front().counts.raw_term_rows << '\n'
            << "iDB geometry rows: PORT rects=" << idb_samples.front().counts.geometry.port_rects
            << ", OBS rects=" << idb_samples.front().counts.geometry.obs_rects << " (LEF POLYGON rectangularized)\n"
            << "EnTT geometry rows: groups=" << entt_samples.front().counts.geometry.groups
            << ", PORT rects=" << entt_samples.front().counts.geometry.port_rects
            << ", polygons=" << entt_samples.front().counts.geometry.port_polygons
            << ", points=" << entt_samples.front().counts.geometry.port_polygon_points
            << "; OBS rects=" << entt_samples.front().counts.geometry.obs_rects
            << ", polygons=" << entt_samples.front().counts.geometry.obs_polygons
            << ", points=" << entt_samples.front().counts.geometry.obs_polygon_points << '\n'
            << "import time median: iDB=" << median(idb_samples, [](const Measurement& value) {
                 return static_cast<int64_t>(value.import_milliseconds);
               })
            << " ms, direct EnTT=" << median(entt_samples, [](const Measurement& value) {
                 return static_cast<int64_t>(value.import_milliseconds);
               })
            << " ms\n"
            << "direct EnTT stages median: TechImporter=" << median(entt_samples, [](const Measurement& value) {
                 return static_cast<int64_t>(value.tech_import_milliseconds);
               })
            << " ms, LibraryImporter=" << median(entt_samples, [](const Measurement& value) {
                 return static_cast<int64_t>(value.library_import_milliseconds);
               })
            << " ms, GeometryPool writes=" << median(entt_samples, [](const Measurement& value) {
                 return static_cast<int64_t>(value.geometry_write_microseconds);
               })
            << " us (" << median(entt_samples, [](const Measurement& value) {
                 return static_cast<int64_t>(value.geometry_write_microseconds / 1000u);
               })
            << " ms), Geometry prepare=" << median(entt_samples, [](const Measurement& value) {
                 return static_cast<int64_t>(value.geometry_prepare_microseconds);
               })
            << " us (" << median(entt_samples, [](const Measurement& value) {
                 return static_cast<int64_t>(value.geometry_prepare_microseconds / 1000u);
               })
            << " ms)\n"
            << "direct EnTT callbacks median: parser=" << median(entt_samples, [](const Measurement& value) {
                 return static_cast<int64_t>(value.parser_microseconds / 1000u);
               })
            << " ms, SITE=" << median(entt_samples, [](const Measurement& value) {
                 return static_cast<int64_t>(value.site_callback_microseconds / 1000u);
               })
            << " ms, MACRO=" << median(entt_samples, [](const Measurement& value) {
                 return static_cast<int64_t>(value.macro_callback_microseconds / 1000u);
               })
            << " ms, PIN/TERM/PORT=" << median(entt_samples, [](const Measurement& value) {
                 return static_cast<int64_t>(value.pin_callback_microseconds / 1000u);
               })
            << " ms, OBS=" << median(entt_samples, [](const Measurement& value) {
                 return static_cast<int64_t>(value.obstruction_callback_microseconds / 1000u);
               })
            << " ms\n"
            << "metric              iDB no-index EnTT rectangles EnTT/iDB\n";
  const auto print_row = [](std::string_view name, int64_t idb, int64_t entt) {
    const double ratio = idb > 0 ? static_cast<double>(entt) / static_cast<double>(idb) : 0.0;
    std::cout << std::left << std::setw(18) << name << std::right << std::setw(14) << mib(idb) << std::setw(14) << mib(entt)
              << std::setw(11) << ratio << '\n';
  };
  print_row("GlibcInUse", idb_allocator, entt_allocator);
  print_row("PeakRSS", idb_peak_rss, entt_peak_rss);
  print_row("RSS", idb_rss, entt_rss);
  print_row("PSS", idb_pss, entt_pss);
  print_row("PrivateDirty", idb_private_dirty, entt_private_dirty);
  print_row("Anonymous", idb_anonymous, entt_anonymous);

  testing::Test::RecordProperty("idb_no_index_private_dirty_kib", idb_private_dirty);
  testing::Test::RecordProperty("idb_no_index_allocator_in_use_kib", idb_allocator);
  testing::Test::RecordProperty("entt_private_dirty_kib", entt_private_dirty);
  testing::Test::RecordProperty("entt_allocator_in_use_kib", entt_allocator);
  testing::Test::RecordProperty("entt_to_idb_allocator_in_use_ratio",
                                idb_allocator > 0 ? static_cast<double>(entt_allocator) / static_cast<double>(idb_allocator) : 0.0);
  testing::Test::RecordProperty(
      "entt_to_idb_private_dirty_ratio",
      idb_private_dirty > 0 ? static_cast<double>(entt_private_dirty) / static_cast<double>(idb_private_dirty) : 0.0);
}

TEST(LefMemoryComparisonTest, ComparesIndexFreeSky130ImportsInIsolatedProcesses)
{
  compareRectangleStorageImports(sky130Inputs());
}

TEST(LefMemoryComparisonTest, ComparesIndexFreeIhp130ImportsInIsolatedProcesses)
{
  compareRectangleStorageImports(ihp130Inputs());
}

TEST(LefMemoryComparisonTest, DISABLED_ComparesExternalIcs55ImportsInIsolatedProcesses)
{
  const auto inputs = ics55Inputs();
  if (!inputs.has_value()) {
    GTEST_SKIP() << "set ECC_TOOLS_ICS55_PDK to run the external ICsprout55 PDK test";
  }
  compareRectangleStorageImports(*inputs);
}

TEST(LefMemoryComparisonTest, DISABLED_ComparesRectangularizedFullSky130ImportsInIsolatedProcesses)
{
  compareRectangleStorageImports(fullSky130Inputs());
}

TEST(LefMemoryComparisonTest, DISABLED_ComparesRectangularizedFullIhp130ImportsInIsolatedProcesses)
{
  compareRectangleStorageImports(fullIhp130Inputs());
}

}  // namespace
}  // namespace eccdb
