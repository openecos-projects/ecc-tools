// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "design/DesignStore.h"
#include "binary/BinaryDatabaseExporter.h"
#include "def/DefDesignExporter.h"
#include "binary/BinaryDatabaseImporter.h"
#include "def/DefDesignImporter.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "DesignSemanticSnapshot.h"
#include "library/LibraryStore.h"
#include "binary/BinaryFormat.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

class TemporaryFile
{
 public:
  TemporaryFile(std::string_view stem, std::string_view extension, std::string_view contents = {})
  {
    std::string safe_stem(stem);
    std::replace_if(safe_stem.begin(), safe_stem.end(), [](unsigned char character) { return !std::isalnum(character); }, '_');
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    _path = std::filesystem::temp_directory_path()
            / ("idb-refactor-design-binary-" + safe_stem + '-' + std::to_string(nonce) + std::string(extension));
    if (!contents.empty()) {
      std::ofstream output(_path, std::ios::binary);
      output << contents;
      if (!output) {
        throw std::runtime_error("failed to create temporary Design archive test input");
      }
    }
  }

  ~TemporaryFile()
  {
    std::error_code error;
    std::filesystem::remove(_path, error);
  }

  TemporaryFile(const TemporaryFile&) = delete;
  TemporaryFile& operator=(const TemporaryFile&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return _path; }

 private:
  std::filesystem::path _path;
};

void expectSameFile(const std::filesystem::path& lhs, const std::filesystem::path& rhs)
{
  ASSERT_EQ(std::filesystem::file_size(lhs), std::filesystem::file_size(rhs));
  std::ifstream left(lhs, std::ios::binary);
  std::ifstream right(rhs, std::ios::binary);
  ASSERT_TRUE(left);
  ASSERT_TRUE(right);
  std::array<char, 64 * 1024> left_buffer{};
  std::array<char, 64 * 1024> right_buffer{};
  while (left) {
    left.read(left_buffer.data(), left_buffer.size());
    right.read(right_buffer.data(), right_buffer.size());
    ASSERT_EQ(left.gcount(), right.gcount());
    ASSERT_TRUE(std::equal(left_buffer.begin(), left_buffer.begin() + left.gcount(), right_buffer.begin()));
  }
}

constexpr std::string_view kLef = R"LEF(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.20 ; WIDTH 0.10 ; SPACING 0.10 ; END M1
LAYER V1 TYPE CUT ; SPACING 0.10 ; END V1
LAYER M2 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.20 ; WIDTH 0.10 ; SPACING 0.10 ; END M2
VIA VIA12 DEFAULT
  LAYER M1 ; RECT -0.08 -0.08 0.08 0.08 ;
  LAYER V1 ; RECT -0.04 -0.04 0.04 0.04 ;
  LAYER M2 ; RECT -0.08 -0.08 0.08 0.08 ;
END VIA12
SITE CORE CLASS CORE ; SYMMETRY X Y ; SIZE 0.20 BY 0.40 ; END CORE
MACRO INVX1
  CLASS CORE ; ORIGIN 0 0 ; SIZE 0.20 BY 0.40 ; SYMMETRY X Y ; SITE CORE ;
  PIN A DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.00 0.10 0.05 0.20 ; END END A
  PIN Y DIRECTION OUTPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.15 0.10 0.20 0.20 ; END END Y
END INVX1
END LIBRARY
)LEF";

constexpr std::string_view kDef = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN binary_top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 2000 2000 ) ;
ROW ROW0 CORE 0 0 N DO 10 BY 1 STEP 200 0 ;
TRACKS X 0 DO 10 STEP 200 MASK 1 SAMEMASK LAYER M1 ;
GCELLGRID X 0 DO 4 STEP 500 ;
VIAS 1 ;
- LOCAL12
  + RECT M1 ( -80 -80 ) ( 80 80 )
  + RECT V1 ( -40 -40 ) ( 40 40 )
  + RECT M2 ( -80 -80 ) ( 80 80 ) ;
END VIAS
COMPONENTS 2 ;
- u1 INVX1 + SOURCE NETLIST + WEIGHT 2 + PLACED ( 200 200 ) N ;
- u2 INVX1 + FIXED ( 800 200 ) FN ;
END COMPONENTS
PINS 1 ;
- IN + NET n1 + DIRECTION INPUT + USE SIGNAL
  + LAYER M1 MASK 2 ( -25 -25 ) ( 25 25 ) + FIXED ( 0 200 ) N ;
END PINS
BLOCKAGES 1 ;
- LAYER M1 + SPACING 10 + MASK 1 RECT ( 900 900 ) ( 1100 1100 ) ;
END BLOCKAGES
REGIONS 1 ;
- FENCE ( 0 0 ) ( 1000 1000 ) + TYPE FENCE ;
END REGIONS
GROUPS 1 ;
- logic u1 + REGION FENCE ;
END GROUPS
FILLS 1 ;
- LAYER M1 + MASK 1 + OPC RECT ( 1200 1200 ) ( 1300 1300 ) ;
END FILLS
SPECIALNETS 1 ;
- VDD ( u1 Y ) ( u2 Y ) + USE POWER
  + ROUTED M2 40 + SHAPE STRIPE ( 200 200 ) ( 200 1200 ) VIA12 ;
END SPECIALNETS
NETS 1 ;
- n1 ( PIN IN ) ( u1 A ) ( u2 A ) + USE SIGNAL + SOURCE NETLIST + WEIGHT 3
  + ORIGINAL source_n1 + PATTERN STEINER + ESTCAP 1.25 + FREQUENCY 2.5 + XTALK 4 + STYLE 7
  + ROUTED M1 STYLE 3 ( 0 200 ) VIRTUAL ( 100 200 ) ( 200 200 5 ) MASK 123 LOCAL12 FN RECT ( -2 -2 2 2 )
    NEW M1 TAPER ( 200 200 ) ( 800 200 ) VIA12 S ;
END NETS
END DESIGN
)DEF";

struct DesignFixture
{
  TemporaryFile lef{"input", ".lef", kLef};
  TemporaryFile def{"input", ".def", kDef};
  TechStore technology;
  LibraryStore library{technology.techRegistry()};
  DesignStore design{technology.techRegistry(), library.libraryRegistry()};

  DesignFixture()
  {
    LefTechImporter(technology).import(lef.path());
    LefLibraryImporter(technology, library).import(lef.path());
    DefDesignImporter importer(design);
    importer.import(def.path());
    if (!importer.diagnostics().empty()) {
      throw std::runtime_error("unexpected diagnostic while creating Design binary fixture");
    }
  }
};

TEST(DesignBinaryDatabaseArchiveTest, RoundTripsDefSemanticsCompactRoutingAndIndexes)
{
  DesignFixture fixture;
  const TemporaryFile binary{"design", ".edb"};
  const TemporaryFile fixed{"design-fixed", ".edb"};
  const auto expected = test::makeDesignSemanticSnapshot(fixture.design);
  const auto expected_def = DefDesignExporter(fixture.design).exportText();
  const auto expected_pool = fixture.design.routingStorage().routingPoolStatistics();

  BinaryDatabaseExporter::saveDesign(binary.path(), fixture.design);
  auto restored = BinaryDatabaseImporter::loadDesign(binary.path(), fixture.technology, fixture.library);

  EXPECT_EQ(test::makeDesignSemanticSnapshot(*restored), expected);
  EXPECT_EQ(DefDesignExporter(*restored).exportText(), expected_def);
  EXPECT_TRUE(restored->netlistStorage().findInstance("u1"));
  EXPECT_TRUE(restored->netlistStorage().findRegularNet("n1"));
  EXPECT_TRUE(restored->netlistStorage().findSpecialNet("VDD"));
  EXPECT_EQ(restored->routingStorage().wires(restored->netlistStorage().findRegularNet("n1")).size(), 1u);

  const auto actual_pool = restored->routingStorage().routingPoolStatistics();
  EXPECT_EQ(actual_pool.routing_layers.count, expected_pool.routing_layers.count);
  EXPECT_EQ(actual_pool.paths.count, expected_pool.paths.count);
  EXPECT_EQ(actual_pool.path_extras.count, expected_pool.path_extras.count);
  EXPECT_EQ(actual_pool.points.count, expected_pool.points.count);
  EXPECT_EQ(actual_pool.point_extras.count, expected_pool.point_extras.count);
  EXPECT_EQ(actual_pool.vias.count, expected_pool.vias.count);
  EXPECT_EQ(actual_pool.via_extras.count, expected_pool.via_extras.count);
  EXPECT_EQ(actual_pool.rectangles.count, expected_pool.rectangles.count);

  BinaryDatabaseExporter::saveDesign(fixed.path(), *restored);
  expectSameFile(binary.path(), fixed.path());
}

TEST(DesignBinaryDatabaseArchiveTest, RejectsTruncatedPayload)
{
  DesignFixture fixture;
  const TemporaryFile binary{"bound-design", ".edb"};
  BinaryDatabaseExporter::saveDesign(binary.path(), fixture.design);
  std::filesystem::resize_file(binary.path(), std::filesystem::file_size(binary.path()) - 1u);
  EXPECT_THROW(BinaryDatabaseImporter::loadDesign(binary.path(), fixture.technology, fixture.library), std::runtime_error);
}

TEST(DesignBinaryDatabaseArchiveTest, LargeIspd19RoutingPoolReachesByteExactFixedPoint)
{
  using Clock = std::chrono::steady_clock;
  const auto elapsedSeconds = [](Clock::time_point begin) {
    return std::chrono::duration<double>(Clock::now() - begin).count();
  };
  if (std::getenv("ECCDB_RUN_LARGE_DESIGN_BINARY_TESTS") == nullptr) {
    GTEST_SKIP() << "set ECCDB_RUN_LARGE_DESIGN_BINARY_TESTS=1 to run the large Design binary archive test";
  }
  const char* root_value = std::getenv("ECCDB_ISPD19_ROOT");
  if (root_value == nullptr || *root_value == '\0') {
    GTEST_SKIP() << "set ECCDB_ISPD19_ROOT to the extracted ISPD 2019 benchmark root";
  }
  const std::filesystem::path root(root_value);
  const auto lef = root / "input/ispd19_test10/ispd19_test10.input.lef";
  const auto def = root / "solutions/extracted/test10/def/12.t10.def";
  ASSERT_TRUE(std::filesystem::is_regular_file(lef)) << lef;
  ASSERT_TRUE(std::filesystem::is_regular_file(def)) << def;

  TechStore technology;
  LibraryStore library(technology.techRegistry());
  ASSERT_NO_THROW(LefTechImporter(technology).import(lef));
  ASSERT_NO_THROW(LefLibraryImporter(technology, library).import(lef));
  auto source = std::make_unique<DesignStore>(technology.techRegistry(), library.libraryRegistry());
  ASSERT_NO_THROW(DefDesignImporter(*source).import(def));

  const auto expected_pool = source->routingStorage().routingPoolStatistics();
  EXPECT_EQ(source->routingStorage().wireCount(), 895077u);
  EXPECT_EQ(expected_pool.paths.count, 46978419u);
  EXPECT_EQ(expected_pool.points.count, 74768813u);
  EXPECT_EQ(expected_pool.vias.count, 14697732u);
  EXPECT_EQ(expected_pool.rectangles.count, 4490293u);

  const TemporaryFile binary{"ispd19-test10-design", ".edb"};
  const TemporaryFile fixed{"ispd19-test10-design-fixed", ".edb"};
  const auto first_write_begin = Clock::now();
  BinaryDatabaseExporter::saveDesign(binary.path(), *source);
  const auto first_write_seconds = elapsedSeconds(first_write_begin);
  const auto archive_bytes = std::filesystem::file_size(binary.path());

  const auto read_begin = Clock::now();
  auto restored = BinaryDatabaseImporter::loadDesign(binary.path(), technology, library);
  const auto read_seconds = elapsedSeconds(read_begin);
  const auto actual_pool = restored->routingStorage().routingPoolStatistics();
  EXPECT_EQ(restored->routingStorage().wireCount(), source->routingStorage().wireCount());
  EXPECT_EQ(restored->netlistStorage().instanceCount(), source->netlistStorage().instanceCount());
  EXPECT_EQ(restored->netlistStorage().instancePinCount(), source->netlistStorage().instancePinCount());
  EXPECT_EQ(restored->netlistStorage().netCount(), source->netlistStorage().netCount());
  EXPECT_EQ(actual_pool.routing_layers.count, expected_pool.routing_layers.count);
  EXPECT_EQ(actual_pool.paths.count, expected_pool.paths.count);
  EXPECT_EQ(actual_pool.path_extras.count, expected_pool.path_extras.count);
  EXPECT_EQ(actual_pool.points.count, expected_pool.points.count);
  EXPECT_EQ(actual_pool.point_extras.count, expected_pool.point_extras.count);
  EXPECT_EQ(actual_pool.vias.count, expected_pool.vias.count);
  EXPECT_EQ(actual_pool.via_extras.count, expected_pool.via_extras.count);
  EXPECT_EQ(actual_pool.rectangles.count, expected_pool.rectangles.count);

  const auto fixed_write_begin = Clock::now();
  BinaryDatabaseExporter::saveDesign(fixed.path(), *restored);
  const auto fixed_write_seconds = elapsedSeconds(fixed_write_begin);
  const auto compare_begin = Clock::now();
  expectSameFile(binary.path(), fixed.path());
  const auto compare_seconds = elapsedSeconds(compare_begin);
  std::cout << "[Design binary] archive_bytes=" << archive_bytes << " first_write_seconds=" << first_write_seconds
            << " read_seconds=" << read_seconds << " fixed_write_seconds=" << fixed_write_seconds
            << " compare_seconds=" << compare_seconds << '\n';
}

}  // namespace
}  // namespace eccdb
