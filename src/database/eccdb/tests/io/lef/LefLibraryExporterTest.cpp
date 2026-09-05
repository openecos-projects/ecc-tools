// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "lef/LefLibraryExporter.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "LefPdkCorpus.h"

namespace eccdb {
namespace {

class TemporaryLef
{
 public:
  TemporaryLef(std::string_view stem, std::string_view contents)
  {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    _path = std::filesystem::temp_directory_path()
            / ("idb-refactor-library-export-" + std::string(stem) + '-' + std::to_string(nonce) + ".lef");
    std::ofstream output(_path);
    if (!output) {
      throw std::runtime_error("failed to create temporary Library LEF");
    }
    output << contents;
    if (!output) {
      throw std::runtime_error("failed to write temporary Library LEF");
    }
  }

  ~TemporaryLef()
  {
    std::error_code error;
    std::filesystem::remove(_path, error);
  }

  TemporaryLef(const TemporaryLef&) = delete;
  TemporaryLef& operator=(const TemporaryLef&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return _path; }

 private:
  std::filesystem::path _path;
};

void expectGeometry(const GeometryPool& expected_pool, GeometryHandle expected, const GeometryPool& actual_pool, GeometryHandle actual)
{
  EXPECT_EQ(expected_pool.bounds(expected), actual_pool.bounds(actual));
  const auto expected_rects = expected_pool.rectangles(expected);
  const auto actual_rects = actual_pool.rectangles(actual);
  ASSERT_EQ(expected_rects.size(), actual_rects.size());
  for (std::size_t index = 0; index < expected_rects.size(); ++index) {
    EXPECT_EQ(expected_rects[index], actual_rects[index]) << index;
  }

  const auto expected_polygon_count = expected_pool.polygonCount(expected);
  ASSERT_EQ(expected_polygon_count, actual_pool.polygonCount(actual));
  for (uint32_t polygon_index = 0; polygon_index < expected_polygon_count; ++polygon_index) {
    const auto expected_points = expected_pool.polygonPoints(expected, polygon_index);
    const auto actual_points = actual_pool.polygonPoints(actual, polygon_index);
    ASSERT_EQ(expected_points.size(), actual_points.size()) << polygon_index;
    for (std::size_t point_index = 0; point_index < expected_points.size(); ++point_index) {
      EXPECT_EQ(expected_points[point_index], actual_points[point_index]) << polygon_index << '/' << point_index;
    }
  }
}

void expectVias(std::span<const LibraryViaPlacement> expected, std::span<const LibraryViaPlacement> actual)
{
  ASSERT_EQ(expected.size(), actual.size());
  for (std::size_t index = 0; index < expected.size(); ++index) {
    EXPECT_EQ(expected[index].via, actual[index].via) << index;
    EXPECT_EQ(expected[index].origin, actual[index].origin) << index;
  }
}

void expectPort(const LibraryStore& expected_database, const LibraryMasterPort& expected, const LibraryStore& actual_database,
                const LibraryMasterPort& actual)
{
  EXPECT_EQ(expected.port_class, actual.port_class);
  ASSERT_EQ(expected.layer_clauses.size(), actual.layer_clauses.size());
  for (std::size_t index = 0; index < expected.layer_clauses.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_EQ(expected.layer_clauses[index].layer, actual.layer_clauses[index].layer);
    expectGeometry(expected_database.geometryPool(), expected.layer_clauses[index].geometry, actual_database.geometryPool(),
                   actual.layer_clauses[index].geometry);
  }
  expectVias(expected.vias, actual.vias);
}

void expectObs(const LibraryStore& expected_database, const LibraryMasterObs& expected, const LibraryStore& actual_database,
               const LibraryMasterObs& actual)
{
  ASSERT_EQ(expected.layer_clauses.size(), actual.layer_clauses.size());
  for (std::size_t index = 0; index < expected.layer_clauses.size(); ++index) {
    SCOPED_TRACE(index);
    const auto& lhs = expected.layer_clauses[index];
    const auto& rhs = actual.layer_clauses[index];
    EXPECT_EQ(lhs.layer, rhs.layer);
    EXPECT_EQ(lhs.flags, rhs.flags);
    EXPECT_EQ(lhs.spacing, rhs.spacing);
    EXPECT_EQ(lhs.design_rule_width, rhs.design_rule_width);
    EXPECT_EQ(lhs.path_width, rhs.path_width);
    expectGeometry(expected_database.geometryPool(), lhs.geometry, actual_database.geometryPool(), rhs.geometry);
  }
  expectVias(expected.vias, actual.vias);
}

void expectLibrary(const LibraryStore& expected, const LibraryStore& actual)
{
  ASSERT_EQ(expected.siteStorage().siteCount(), actual.siteStorage().siteCount());
  for (const auto expected_id : expected.siteStorage().sites()) {
    const auto& lhs = expected.siteStorage().site(expected_id);
    const auto actual_id = actual.siteStorage().findSite(lhs.name);
    ASSERT_TRUE(actual_id) << lhs.name;
    const auto& rhs = actual.siteStorage().site(actual_id);
    EXPECT_EQ(lhs.width, rhs.width) << lhs.name;
    EXPECT_EQ(lhs.height, rhs.height) << lhs.name;
    EXPECT_EQ(lhs.overlap, rhs.overlap) << lhs.name;
    EXPECT_EQ(lhs.site_class, rhs.site_class) << lhs.name;
    EXPECT_EQ(lhs.symmetry_x, rhs.symmetry_x) << lhs.name;
    EXPECT_EQ(lhs.symmetry_y, rhs.symmetry_y) << lhs.name;
    EXPECT_EQ(lhs.symmetry_r90, rhs.symmetry_r90) << lhs.name;
  }

  ASSERT_EQ(expected.cellMasterStorage().cellMasterCount(), actual.cellMasterStorage().cellMasterCount());
  ASSERT_EQ(expected.masterTermStorage().masterTermCount(), actual.masterTermStorage().masterTermCount());
  ASSERT_EQ(expected.masterPortStorage().masterPortCount(), actual.masterPortStorage().masterPortCount());
  for (const auto expected_id : expected.cellMasterStorage().cellMasters()) {
    const auto& lhs = expected.cellMasterStorage().cellMaster(expected_id);
    SCOPED_TRACE(lhs.name);
    const auto actual_id = actual.cellMasterStorage().findCellMaster(lhs.name);
    ASSERT_TRUE(actual_id);
    const auto& rhs = actual.cellMasterStorage().cellMaster(actual_id);
    EXPECT_EQ(lhs.type, rhs.type);
    EXPECT_EQ(lhs.symmetry_x, rhs.symmetry_x);
    EXPECT_EQ(lhs.symmetry_y, rhs.symmetry_y);
    EXPECT_EQ(lhs.symmetry_r90, rhs.symmetry_r90);
    EXPECT_EQ(lhs.core_filler, rhs.core_filler);
    EXPECT_EQ(lhs.pad_filler, rhs.pad_filler);
    EXPECT_EQ(lhs.origin_x, rhs.origin_x);
    EXPECT_EQ(lhs.origin_y, rhs.origin_y);
    EXPECT_EQ(lhs.width, rhs.width);
    EXPECT_EQ(lhs.height, rhs.height);
    ASSERT_EQ(lhs.site.has_value(), rhs.site.has_value());
    if (lhs.site.has_value()) {
      EXPECT_EQ(expected.siteStorage().site(*lhs.site).name, actual.siteStorage().site(*rhs.site).name);
    }

    const auto expected_terms = expected.masterTermStorage().masterTerms(expected_id);
    const auto actual_terms = actual.masterTermStorage().masterTerms(actual_id);
    ASSERT_EQ(expected_terms.size(), actual_terms.size());
    for (std::size_t term_index = 0; term_index < expected_terms.size(); ++term_index) {
      const auto& expected_term = expected.masterTermStorage().masterTerm(expected_terms[term_index]);
      const auto& actual_term = actual.masterTermStorage().masterTerm(actual_terms[term_index]);
      SCOPED_TRACE(expected_term.name);
      EXPECT_EQ(expected_term.name, actual_term.name);
      EXPECT_EQ(expected_term.direction, actual_term.direction);
      EXPECT_EQ(expected_term.use, actual_term.use);
      EXPECT_EQ(expected_term.shape, actual_term.shape);
      const auto expected_ports = expected.masterPortStorage().masterPorts(expected_terms[term_index]);
      const auto actual_ports = actual.masterPortStorage().masterPorts(actual_terms[term_index]);
      ASSERT_EQ(expected_ports.size(), actual_ports.size());
      for (std::size_t port_index = 0; port_index < expected_ports.size(); ++port_index) {
        SCOPED_TRACE(port_index);
        expectPort(expected, expected.masterPortStorage().masterPort(expected_ports[port_index]), actual,
                   actual.masterPortStorage().masterPort(actual_ports[port_index]));
      }
    }

    ASSERT_EQ(expected.cellMasterStorage().hasObs(expected_id), actual.cellMasterStorage().hasObs(actual_id));
    if (expected.cellMasterStorage().hasObs(expected_id)) {
      expectObs(expected, expected.cellMasterStorage().obs(expected_id), actual, actual.cellMasterStorage().obs(actual_id));
    }
  }

  EXPECT_EQ(expected.geometryPool().rectangleCount(), actual.geometryPool().rectangleCount());
  EXPECT_EQ(expected.geometryPool().polygonCount(), actual.geometryPool().polygonCount());
  EXPECT_EQ(expected.geometryPool().pointCount(), actual.geometryPool().pointCount());
}

void expectRoundTrip(const lef_test::LefPdkDomain& domain)
{
  TechStore technology;
  LefTechImporter(technology).import(domain.technology);

  LibraryStore source(technology.techRegistry());
  std::vector<std::filesystem::path> files;
  files.reserve(domain.cells.size() + 1u);
  files.push_back(domain.technology);
  files.insert(files.end(), domain.cells.begin(), domain.cells.end());
  LefLibraryImporter(technology, source).import(files);

  std::ostringstream first;
  LefLibraryExporter::write(first, technology, source);
  const TemporaryLef canonical(domain.name, first.str());

  LibraryStore roundtripped(technology.techRegistry());
  LefLibraryImporter(technology, roundtripped).import(canonical.path());
  expectLibrary(source, roundtripped);

  std::ostringstream second;
  LefLibraryExporter::write(second, technology, roundtripped);
  EXPECT_EQ(first.str(), second.str());
}

void expectRoundTripInIsolatedProcess(const lef_test::LefPdkDomain& domain)
{
  std::fflush(nullptr);
  const pid_t child = fork();
  ASSERT_GE(child, 0) << "fork failed for " << domain.name;
  if (child == 0) {
    int exit_code = 0;
    try {
      expectRoundTrip(domain);
      exit_code = testing::Test::HasFailure() ? 1 : 0;
    } catch (const std::exception& error) {
      std::fprintf(stderr, "%s: %s\n", domain.name.c_str(), error.what());
      exit_code = 2;
    }
    std::fflush(nullptr);
    _exit(exit_code);
  }

  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status)) << domain.name << " Library round-trip child terminated abnormally";
  EXPECT_EQ(WEXITSTATUS(status), 0) << domain.name << " Library round trip failed";
}

TEST(LefLibraryExporterTest, RoundTripsEveryModeledLibraryFieldAndGeometryKind)
{
  const TemporaryLef technology("synthetic-tech", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.20 ; WIDTH 0.10 ; END M1
LAYER V1 TYPE CUT ; WIDTH 0.10 ; END V1
LAYER M2 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.30 ; WIDTH 0.10 ; END M2
LAYER OVERLAP TYPE OVERLAP ; END OVERLAP
VIA V12
  LAYER M1 ; RECT -0.06 -0.07 0.06 0.07 ;
  LAYER V1 ; RECT -0.04 -0.04 0.04 0.04 ;
  LAYER M2 ; RECT -0.07 -0.06 0.07 0.06 ;
END V12
END LIBRARY
)LEF");
  const TemporaryLef library_lef("synthetic-library", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
SITE CORE_SITE
  CLASS CORE ; SYMMETRY X Y R90 ; SIZE 0.20 BY 0.40 ;
END CORE_SITE
MACRO FILL_X1
  CLASS CORE SPACER ;
  ORIGIN -0.10 0.20 ;
  SIZE 1.20 BY 2.40 ;
  SYMMETRY X Y ;
  SITE CORE_SITE ;
  PIN A
    DIRECTION INPUT ; USE SIGNAL ; SHAPE ABUTMENT ;
    PORT
      CLASS CORE ;
      LAYER M1 ;
        RECT 0.10 0.20 0.30 0.40 ;
        POLYGON 0.40 0.20 0.60 0.20 0.60 0.40 0.40 0.40 ;
      VIA ( 0.50 0.60 ) V12 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT TRISTATE ; USE CLOCK ; SHAPE RING ;
    PORT
      LAYER M2 ;
        PATH ( 0.70 0.80 ) ( 0.70 1.20 ) ;
    END
  END Y
  OBS
    LAYER M1 EXCEPTPGNET SPACING 0.05 ;
      RECT 0.00 0.00 0.20 0.20 ;
      POLYGON 0.30 0.00 0.50 0.00 0.50 0.20 0.30 0.20 ;
    LAYER M2 DESIGNRULEWIDTH 0.08 ;
      RECT 0.60 0.00 0.80 0.20 ;
    VIA ( 0.90 1.00 ) V12 ;
  END
END FILL_X1
END LIBRARY
)LEF");

  TechStore tech;
  LefTechImporter(tech).import(technology.path());
  LibraryStore source(tech.techRegistry());
  LefLibraryImporter(tech, source).import(library_lef.path());

  std::ostringstream first;
  ASSERT_NO_THROW(LefLibraryExporter::write(first, tech, source));
  EXPECT_NE(first.str().find("SITE CORE_SITE"), std::string::npos);
  EXPECT_NE(first.str().find("MACRO FILL_X1"), std::string::npos);
  EXPECT_NE(first.str().find("POLYGON"), std::string::npos);
  EXPECT_NE(first.str().find("VIA ( 0.5 0.6 ) V12"), std::string::npos);

  const TemporaryLef canonical("synthetic-canonical", first.str());
  LibraryStore roundtripped(tech.techRegistry());
  ASSERT_NO_THROW(LefLibraryImporter(tech, roundtripped).import(canonical.path()));
  expectLibrary(source, roundtripped);

  std::ostringstream second;
  ASSERT_NO_THROW(LefLibraryExporter::write(second, tech, roundtripped));
  EXPECT_EQ(first.str(), second.str());
}

TEST(LefLibraryExporterTest, FullSky130AndIhp130LibrariesReachCanonicalFixedPoint)
{
  auto corpus = lef_test::fullSky130Corpus(std::filesystem::path{ECC_TOOLS_SOURCE_DIR});
  auto ihp = lef_test::fullIhp130Corpus(std::filesystem::path{ECC_TOOLS_SOURCE_DIR});
  corpus.insert(corpus.end(), ihp.begin(), ihp.end());
  for (const auto& domain : corpus) {
    SCOPED_TRACE(domain.name);
    expectRoundTripInIsolatedProcess(domain);
  }
}

}  // namespace
}  // namespace eccdb
