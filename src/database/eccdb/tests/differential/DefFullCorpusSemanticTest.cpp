// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "DesignSemanticSnapshot.h"
#include "design/DesignStore.h"
#include "design/constraint/model/ConstraintComponents.h"
#include "design/fill/model/FillComponents.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "def/DefDesignExporter.h"
#include "def/DefDesignImporter.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

struct DefCorpusCase
{
  std::string name;
  std::filesystem::path tech_lef;
  std::vector<std::filesystem::path> library_lefs;
  std::filesystem::path def;
  std::vector<std::string> expected_diagnostics;
  std::size_t minimum_wire_count = 0;
  std::size_t minimum_path_count = 0;
};

struct ProcessMemory
{
  uint64_t resident_kib = 0;
  uint64_t peak_resident_kib = 0;
};

ProcessMemory processMemory()
{
  ProcessMemory result;
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    auto parse = [&](std::string_view prefix, uint64_t& target) {
      if (!line.starts_with(prefix)) {
        return;
      }
      std::istringstream value(line.substr(prefix.size()));
      value >> target;
    };
    parse("VmRSS:", result.resident_kib);
    parse("VmHWM:", result.peak_resident_kib);
  }
  return result;
}

void trimAllocator()
{
#if defined(__GLIBC__)
  static_cast<void>(malloc_trim(0));
#endif
}

void printMemory(std::string_view stage, ProcessMemory memory)
{
  std::cout << "[DEF memory] " << stage << ": rss=" << memory.resident_kib << " KiB, peak=" << memory.peak_resident_kib << " KiB\n";
}

std::filesystem::path openRoadSource()
{
  if (const char* value = std::getenv("OPENROAD_SOURCE_DIR"); value != nullptr && *value != '\0') {
    return value;
  }
#ifdef OPENROAD_SOURCE_DEFAULT
  return OPENROAD_SOURCE_DEFAULT;
#else
  return {};
#endif
}

std::filesystem::path requireOpenRoadSource()
{
  const auto root = openRoadSource();
  if (root.empty() || !std::filesystem::is_directory(root)) {
    return {};
  }
  return root;
}

std::vector<std::string> diagnosticStatements(const DefDesignImporter& importer)
{
  std::vector<std::string> result;
  result.reserve(importer.diagnostics().size());
  for (const auto& diagnostic : importer.diagnostics()) {
    result.push_back(diagnostic.statement);
  }
  return result;
}

void importLefs(const std::filesystem::path& root, const DefCorpusCase& test_case, TechStore& technology, LibraryStore& library)
{
  const auto tech_file = root / test_case.tech_lef;
  if (!std::filesystem::is_regular_file(tech_file)) {
    throw std::runtime_error("missing corpus Tech LEF: " + tech_file.string());
  }
  LefTechImporter(technology).import(tech_file);

  for (const auto& relative : test_case.library_lefs) {
    const auto file = root / relative;
    if (!std::filesystem::is_regular_file(file)) {
      throw std::runtime_error("missing corpus Library LEF: " + file.string());
    }
    LefLibraryImporter(technology, library).import(file);
  }
}

std::filesystem::path writeCanonicalDef(std::string_view name, const DesignStore& design)
{
  const auto directory = std::filesystem::path(testing::TempDir()) / "eccdb_def_corpus";
  std::filesystem::create_directories(directory);
  const auto file = directory / (std::string(name) + ".def");
  DefDesignExporter(design).write(file);
  return file;
}

void expectKeySection(std::string_view section, const std::vector<std::string>& expected, const std::vector<std::string>& actual)
{
  ASSERT_EQ(expected.size(), actual.size()) << section;
  for (std::size_t index = 0; index < expected.size(); ++index) {
    if (expected[index] != actual[index]) {
      ADD_FAILURE() << section << " differs at sorted item " << index << "\nexpected: " << expected[index]
                    << "\n  actual: " << actual[index];
      return;
    }
  }
}

void expectSemanticSnapshot(const test::DesignSemanticSnapshot& expected, const test::DesignSemanticSnapshot& actual)
{
  EXPECT_EQ(expected.component_counts, actual.component_counts);
  EXPECT_EQ(expected.global, actual.global) << "global";
  expectKeySection("rows", expected.rows, actual.rows);
  expectKeySection("track_grids", expected.track_grids, actual.track_grids);
  expectKeySection("gcell_grids", expected.gcell_grids, actual.gcell_grids);
  expectKeySection("instances", expected.instances, actual.instances);
  expectKeySection("instance_pins", expected.instance_pins, actual.instance_pins);
  expectKeySection("io_pins", expected.io_pins, actual.io_pins);
  expectKeySection("vias", expected.vias, actual.vias);
  expectKeySection("non_default_rules", expected.non_default_rules, actual.non_default_rules);
  expectKeySection("nets", expected.nets, actual.nets);
  expectKeySection("regions", expected.regions, actual.regions);
  expectKeySection("groups", expected.groups, actual.groups);
  expectKeySection("blockages", expected.blockages, actual.blockages);
  expectKeySection("fills", expected.fills, actual.fills);
}

void expectAllNetRoutingSemantics(const DesignStore& expected, const DesignStore& actual)
{
  for (const auto expected_id : expected.netlistStorage().nets()) {
    if (test::detail::isEmptyRegularAliasOfSpecialNet(expected, expected_id)) {
      continue;
    }
    const auto& expected_net = expected.netlistStorage().net(expected_id);
    const bool special = expected.netlistStorage().isSpecialNet(expected_id);
    const auto actual_id
        = special ? actual.netlistStorage().findSpecialNet(expected_net.name) : actual.netlistStorage().findRegularNet(expected_net.name);
    ASSERT_TRUE(actual_id) << (special ? "special:" : "regular:") << expected_net.name;

    const auto net_name = std::string(special ? "special:" : "regular:") + expected_net.name;
    const auto expected_wires = test::detail::wirePrimitiveKeys(expected, expected_id);
    const auto actual_wires = test::detail::wirePrimitiveKeys(actual, actual_id);
    if (expected_wires != actual_wires) {
      expectKeySection("wire primitives for " + net_name, expected_wires, actual_wires);
      return;
    }

    const auto expected_geometry = test::detail::netGeometryKeys(expected, expected_id);
    const auto actual_geometry = test::detail::netGeometryKeys(actual, actual_id);
    if (expected_geometry != actual_geometry) {
      expectKeySection("standalone geometry for " + net_name, expected_geometry, actual_geometry);
      return;
    }
  }
}

std::size_t wirePathCount(const DesignStore& design)
{
  std::size_t result = 0;
  for (const auto net : design.netlistStorage().nets()) {
    for (const auto wire : design.routingStorage().wires(net)) {
      result += design.routingStorage().pathCount(wire);
    }
  }
  return result;
}

void expectRoutingCoverage(const DefCorpusCase& test_case, const DesignStore& design)
{
  const auto wire_count = test::detail::componentCount<DesignWire>(design);
  const auto path_count = wirePathCount(design);
  EXPECT_GE(wire_count, test_case.minimum_wire_count) << test_case.def;
  EXPECT_GE(path_count, test_case.minimum_path_count) << test_case.def;
  std::cout << "[DEF routing] " << test_case.name << ": wires=" << wire_count << ", paths=" << path_count << '\n';
}

void expectCanonicalSemanticFixedPoint(const std::filesystem::path& root, const DefCorpusCase& test_case)
{
  TechStore technology;
  LibraryStore library(technology.techRegistry());
  ASSERT_NO_THROW(importLefs(root, test_case, technology, library));

  const auto source_def = root / test_case.def;
  ASSERT_TRUE(std::filesystem::is_regular_file(source_def)) << source_def;

  DesignStore imported(technology.techRegistry(), library.libraryRegistry());
  DefDesignImporter importer(imported);
  ASSERT_NO_THROW(importer.import(source_def)) << source_def;
  EXPECT_EQ(diagnosticStatements(importer), test_case.expected_diagnostics) << source_def;
  expectRoutingCoverage(test_case, imported);
  const auto first = test::makeDesignSemanticSnapshot(imported);

  const auto canonical_file = writeCanonicalDef(test_case.name, imported);
  DesignStore reimported(technology.techRegistry(), library.libraryRegistry());
  DefDesignImporter reimporter(reimported);
  ASSERT_NO_THROW(reimporter.import(canonical_file)) << canonical_file;
  EXPECT_TRUE(reimporter.diagnostics().empty()) << canonical_file;
  expectRoutingCoverage(test_case, reimported);
  expectSemanticSnapshot(first, test::makeDesignSemanticSnapshot(reimported));
  expectAllNetRoutingSemantics(imported, reimported);
}

class DefFullCorpusSemanticTest : public testing::TestWithParam<DefCorpusCase>
{
};

TEST_P(DefFullCorpusSemanticTest, ReachesCanonicalSemanticFixedPoint)
{
  const auto root = requireOpenRoadSource();
  if (root.empty()) {
    GTEST_SKIP() << "set OPENROAD_SOURCE_DIR to an OpenROAD source checkout";
  }
  expectCanonicalSemanticFixedPoint(root, GetParam());
}

INSTANTIATE_TEST_SUITE_P(OpenRoadOdbData, DefFullCorpusSemanticTest,
                         testing::Values(DefCorpusCase{.name = "design",
                                                       .tech_lef = "src/odb/test/data/gscl45nm.lef",
                                                       .library_lefs = {"src/odb/test/data/gscl45nm.lef"},
                                                       .def = "src/odb/test/data/design.def",
                                                       .expected_diagnostics = {"DUPLICATE NET CONNECTION"}},
                                         DefCorpusCase{.name = "design58",
                                                       .tech_lef = "src/odb/test/data/gscl45nm.lef",
                                                       .library_lefs = {"src/odb/test/data/gscl45nm.lef"},
                                                       .def = "src/odb/test/data/design58.def",
                                                       .expected_diagnostics = {"DUPLICATE NET CONNECTION"}},
                                         DefCorpusCase{.name = "ndr_named_fixture",
                                                       .tech_lef = "src/odb/test/data/gscl45nm.lef",
                                                       .library_lefs = {"src/odb/test/data/gscl45nm.lef"},
                                                       .def = "src/odb/test/data/ndr.def",
                                                       .expected_diagnostics = {"DUPLICATE NET CONNECTION"}},
                                         DefCorpusCase{.name = "virtual_route",
                                                       .tech_lef = "src/odb/test/data/gscl45nm.lef",
                                                       .library_lefs = {"src/odb/test/data/gscl45nm.lef"},
                                                       .def = "src/odb/test/data/virtual_route.def"},
                                         DefCorpusCase{
                                             .name = "parser_test",
                                             .tech_lef = "src/odb/test/data/gscl45nm.lef",
                                             .library_lefs = {"src/odb/test/data/gscl45nm.lef"},
                                             .def = "src/odb/test/data/parser_test.def",
                                             .expected_diagnostics = {"COMPONENT PROPERTY", "DUPLICATE NET CONNECTION",
                                                                      "GROUP SOFT/property", "PIN NETEXPR/SENSITIVITY", "REGION PROPERTY"},
                                             .minimum_wire_count = 3,
                                             .minimum_path_count = 6},
                                         DefCorpusCase{.name = "gcd_nangate45_route",
                                                       .tech_lef = "src/gpl/test/nangate45.lef",
                                                       .library_lefs = {"src/gpl/test/nangate45.lef"},
                                                       .def = "src/odb/test/data/gcd/gcd_nangate45_route.def",
                                                       .minimum_wire_count = 400,
                                                       .minimum_path_count = 4000}),
                         [](const testing::TestParamInfo<DefCorpusCase>& info) { return info.param.name; });

TEST(DefLargeCorpusTest, AesNangate45PrerouteReachesSemanticFixedPoint)
{
  if (std::getenv("ECCDB_RUN_LARGE_DEF_TESTS") == nullptr) {
    GTEST_SKIP() << "set ECCDB_RUN_LARGE_DEF_TESTS=1 to run the large DEF stress test";
  }
  const auto root = requireOpenRoadSource();
  if (root.empty()) {
    GTEST_SKIP() << "set OPENROAD_SOURCE_DIR to an OpenROAD source checkout";
  }

  expectCanonicalSemanticFixedPoint(root, DefCorpusCase{.name = "aes_nangate45_preroute",
                                                        .tech_lef = "src/gpl/test/nangate45.lef",
                                                        .library_lefs = {"src/gpl/test/nangate45.lef"},
                                                        .def = "src/drt/test/aes_nangate45_preroute.def",
                                                        .minimum_wire_count = 2,
                                                        .minimum_path_count = 37000});
}

TEST(DefLargeCorpusTest, ImportsBeyondLegacyLimitAndReachesSemanticFixedPoint)
{
  if (std::getenv("ECCDB_RUN_LARGE_DEF_TESTS") == nullptr) {
    GTEST_SKIP() << "set ECCDB_RUN_LARGE_DEF_TESTS=1 to run the large DEF stress test";
  }
  const auto root = requireOpenRoadSource();
  if (root.empty()) {
    GTEST_SKIP() << "set OPENROAD_SOURCE_DIR to an OpenROAD source checkout";
  }

  const DefCorpusCase test_case{.name = "large01",
                                .tech_lef = "src/gpl/test/nangate45.lef",
                                .library_lefs = {"src/gpl/test/nangate45.lef"},
                                .def = "src/gpl/test/large01.def"};
  trimAllocator();
  const auto initial_memory = processMemory();
  printMemory("initial", initial_memory);

  TechStore technology;
  LibraryStore library(technology.techRegistry());
  ASSERT_NO_THROW(importLefs(root, test_case, technology, library));
  trimAllocator();
  const auto library_memory = processMemory();
  printMemory("after LEF Tech+Library", library_memory);

  auto design = std::make_unique<DesignStore>(technology.techRegistry(), library.libraryRegistry());
  {
    DefDesignImporter importer(*design);
    ASSERT_NO_THROW(importer.import(root / test_case.def));
    EXPECT_TRUE(importer.diagnostics().empty());
  }
  trimAllocator();
  const auto design_memory = processMemory();
  printMemory("after DEF Design", design_memory);
  EXPECT_EQ(test::detail::componentCount<DesignInstance>(*design), 274700u);
  EXPECT_EQ(test::detail::componentCount<DesignIoPin>(*design), 1849u);
  EXPECT_EQ(test::detail::componentCount<DesignNet>(*design), 290356u);

  const auto modeled_entity_count
      = 1u + test::detail::componentCount<DesignRow>(*design) + test::detail::componentCount<DesignTrackGrid>(*design)
        + test::detail::componentCount<DesignGCellGrid>(*design) + test::detail::componentCount<DesignInstance>(*design)
        + test::detail::componentCount<DesignInstancePin>(*design) + test::detail::componentCount<DesignIoPin>(*design)
        + test::detail::componentCount<DesignNet>(*design) + test::detail::componentCount<DesignWire>(*design)
        + test::detail::componentCount<DesignVia>(*design) + test::detail::componentCount<DesignRegion>(*design)
        + test::detail::componentCount<DesignGroup>(*design) + test::detail::componentCount<DesignBlockage>(*design)
        + test::detail::componentCount<DesignFill>(*design);
  EXPECT_GT(modeled_entity_count, 0xFFFFFu);
  std::cout << "[DEF memory] modeled entities=" << modeled_entity_count
            << ", instance pins=" << test::detail::componentCount<DesignInstancePin>(*design)
            << ", wires=" << test::detail::componentCount<DesignWire>(*design)
            << ", stable Design delta=" << (design_memory.resident_kib - library_memory.resident_kib) << " KiB\n";

  const auto expected = test::makeDesignSemanticSnapshot(*design);
  const auto canonical_file = writeCanonicalDef(test_case.name, *design);
  design.reset();
  trimAllocator();
  printMemory("after releasing source Design", processMemory());

  DesignStore reimported(technology.techRegistry(), library.libraryRegistry());
  {
    DefDesignImporter importer(reimported);
    ASSERT_NO_THROW(importer.import(canonical_file));
    EXPECT_TRUE(importer.diagnostics().empty());
  }
  expectSemanticSnapshot(expected, test::makeDesignSemanticSnapshot(reimported));
}

}  // namespace
}  // namespace eccdb
