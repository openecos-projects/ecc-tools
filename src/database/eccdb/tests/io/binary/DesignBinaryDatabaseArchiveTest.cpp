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
#include <utility>

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

TEST(DesignBinaryDatabaseArchiveTest, RoundTripsDefSemanticsShapeRoutingAndIndexes)
{
  DesignFixture fixture;
  const TemporaryFile binary{"design", ".edb"};
  const TemporaryFile fixed{"design-fixed", ".edb"};
  const TemporaryFile transient{"design-transient", ".edb"};
  const auto expected = test::makeDesignSemanticSnapshot(fixture.design);
  const auto expected_def = DefDesignExporter(fixture.design).exportText();
  const auto expected_statistics = fixture.design.routingStorage().routingStatistics();

  BinaryDatabaseExporter::saveDesign(binary.path(), fixture.design);
  const auto source_instance = fixture.design.netlistStorage().findInstance("u1");
  const auto source_pin = fixture.design.netlistStorage().findInstancePin(source_instance, "A");
  ASSERT_TRUE(source_pin);
  auto& source_pins = fixture.design.designRegistry().registry().get<DesignInstancePins>(source_instance.entity());
  ASSERT_EQ(source_pins.values.size(), source_pins.records.size());
  const auto source_record = source_pins.records.find("A");
  ASSERT_NE(source_record, source_pins.records.end());
  auto& source_pin_value = source_record->second;
  const auto source_term = source_pin_value.master_term;
  const auto* cached_instance = source_pin_value.instance_ptr;
  const auto* cached_term = source_pin_value.master_term_ptr;
  EXPECT_EQ(source_pin_value.instance, source_instance);
  EXPECT_EQ(source_pin_value.id, source_pin);
  ASSERT_EQ(cached_instance, &fixture.design.netlistStorage().instance(source_instance));
  ASSERT_EQ(cached_term, &fixture.library.masterTermStorage().masterTerm(source_term));
  EXPECT_EQ(fixture.design.netlistStorage().instancePin(source_pin).master_term, source_term);
  source_pin_value.instance_ptr = nullptr;
  source_pin_value.master_term_ptr = nullptr;
  BinaryDatabaseExporter::saveDesign(transient.path(), fixture.design);
  source_pin_value.instance_ptr = cached_instance;
  source_pin_value.master_term_ptr = cached_term;
  expectSameFile(binary.path(), transient.path());

  auto restored = BinaryDatabaseImporter::loadDesign(binary.path(), fixture.technology, fixture.library);

  EXPECT_EQ(test::makeDesignSemanticSnapshot(*restored), expected);
  EXPECT_EQ(DefDesignExporter(*restored).exportText(), expected_def);
  EXPECT_TRUE(restored->netlistStorage().findInstance("u1"));
  EXPECT_TRUE(restored->netlistStorage().findRegularNet("n1"));
  EXPECT_TRUE(restored->netlistStorage().findSpecialNet("VDD"));
  EXPECT_EQ(restored->routingStorage().wires(restored->netlistStorage().findRegularNet("n1")).size(), 1u);

  const auto restored_instance = restored->netlistStorage().findInstance("u1");
  const auto& restored_pins = restored->designRegistry().registry().get<const DesignInstancePins>(restored_instance.entity());
  EXPECT_EQ(restored_pins.values.size(), restored_pins.records.size());
  EXPECT_EQ(restored_pins.values, source_pins.values);
  EXPECT_EQ(restored_pins.records.size(), source_pins.records.size());
  for (const auto& [name, source_record] : source_pins.records) {
    const auto restored_record = restored_pins.records.find(name);
    ASSERT_NE(restored_record, restored_pins.records.end());
    EXPECT_EQ(restored_record->second.id, source_record.id);
    EXPECT_EQ(restored_record->second.master_term, source_record.master_term);
  }
  ASSERT_NE(restored_pins.records.find("A"), restored_pins.records.end());
  const auto restored_pin = restored->netlistStorage().findInstancePin(restored_instance, "A");
  ASSERT_TRUE(restored_pin);
  const auto& restored_instance_pin = restored->netlistStorage().instancePin(restored_pin);
  EXPECT_EQ(restored_instance_pin.instance, restored_instance);
  EXPECT_EQ(restored_instance_pin.id, restored_pin);
  EXPECT_EQ(restored_instance_pin.master_term, source_term);
  EXPECT_EQ(restored_instance_pin.instance_ptr, &restored->netlistStorage().instance(restored_instance));
  EXPECT_EQ(restored_instance_pin.master_term_ptr,
            &fixture.library.masterTermStorage().masterTerm(restored_instance_pin.master_term));
  const auto& restored_registry = std::as_const(restored->designRegistry().registry());
  EXPECT_EQ(restored_registry.storage<DesignInstanceTerm>(), nullptr);

  const auto actual_statistics = restored->routingStorage().routingStatistics();
  EXPECT_EQ(actual_statistics.shapes.count, expected_statistics.shapes.count);
  EXPECT_EQ(actual_statistics.paths.count, expected_statistics.paths.count);
  EXPECT_EQ(actual_statistics.path_extras.count, expected_statistics.path_extras.count);
  EXPECT_EQ(actual_statistics.points.count, expected_statistics.points.count);
  EXPECT_EQ(actual_statistics.vias.count, expected_statistics.vias.count);
  EXPECT_EQ(actual_statistics.rectangles.count, expected_statistics.rectangles.count);

  BinaryDatabaseExporter::saveDesign(fixed.path(), *restored);
  expectSameFile(binary.path(), fixed.path());
}

TEST(DesignBinaryDatabaseArchiveTest, RestoresInstancePinsAfterOtherInstanceDeletion)
{
  DesignFixture fixture;
  auto& netlist = fixture.design.netlistStorage();
  const auto instance = netlist.findInstance("u1");
  const auto master = netlist.instance(instance).master;
  const auto regular = netlist.findRegularNet("n1");
  const auto temporary = netlist.createInstance(DesignInstance{.name = "temporary", .master = master});
  const auto survivor = netlist.createInstance(DesignInstance{.name = "survivor", .master = master});
  const auto survivor_pin = netlist.findInstancePin(survivor, "A");
  ASSERT_TRUE(survivor_pin);
  const auto* cached_survivor_pin = &netlist.instancePin(survivor_pin);
  const auto* cached_survivor_instance = cached_survivor_pin->instance_ptr;
  const auto* cached_survivor_term = cached_survivor_pin->master_term_ptr;
  ASSERT_EQ(cached_survivor_instance, &netlist.instance(survivor));
  ASSERT_EQ(cached_survivor_term, &fixture.library.masterTermStorage().masterTerm(cached_survivor_pin->master_term));
  netlist.connect(survivor_pin, regular);
  ASSERT_TRUE(netlist.destroyInstance(temporary));
  ASSERT_EQ(netlist.instanceCount(), 3u);
  ASSERT_EQ(netlist.findInstancePin(survivor, "A"), survivor_pin);
  EXPECT_EQ(&netlist.instancePin(survivor_pin), cached_survivor_pin);
  EXPECT_EQ(netlist.instancePin(survivor_pin).instance_ptr, cached_survivor_instance);
  EXPECT_EQ(netlist.instancePin(survivor_pin).master_term_ptr, cached_survivor_term);
  ASSERT_EQ(netlist.instancePin(survivor_pin).instance, survivor);
  ASSERT_EQ(netlist.instancePin(survivor_pin).net, regular);
  const auto regular_pins = netlist.instancePins(regular);
  ASSERT_NE(std::find(regular_pins.begin(), regular_pins.end(), survivor_pin), regular_pins.end());

  const TemporaryFile binary{"design-deleted-instance", ".edb"};
  const TemporaryFile fixed{"design-deleted-instance-fixed", ".edb"};
  BinaryDatabaseExporter::saveDesign(binary.path(), fixture.design);
  auto restored = BinaryDatabaseImporter::loadDesign(binary.path(), fixture.technology, fixture.library);
  EXPECT_EQ(restored->netlistStorage().instanceCount(), 3u);
  EXPECT_FALSE(restored->netlistStorage().findInstance("temporary"));
  const auto restored_survivor = restored->netlistStorage().findInstance("survivor");
  ASSERT_TRUE(restored_survivor);
  const auto restored_survivor_pin = restored->netlistStorage().findInstancePin(restored_survivor, "A");
  ASSERT_TRUE(restored_survivor_pin);
  const auto restored_regular = restored->netlistStorage().findRegularNet("n1");
  EXPECT_EQ(restored->netlistStorage().instancePin(restored_survivor_pin).instance, restored_survivor);
  EXPECT_EQ(restored->netlistStorage().instancePin(restored_survivor_pin).net, restored_regular);
  EXPECT_EQ(restored->netlistStorage().instancePin(restored_survivor_pin).instance_ptr,
            &restored->netlistStorage().instance(restored_survivor));
  EXPECT_EQ(restored->netlistStorage().instancePin(restored_survivor_pin).master_term_ptr,
            &fixture.library.masterTermStorage().masterTerm(
                restored->netlistStorage().instancePin(restored_survivor_pin).master_term));
  const auto restored_regular_pins = restored->netlistStorage().instancePins(restored_regular);
  EXPECT_NE(std::find(restored_regular_pins.begin(), restored_regular_pins.end(), restored_survivor_pin), restored_regular_pins.end());

  const auto restored_instance = restored->netlistStorage().findInstance("u1");
  const auto pin = restored->netlistStorage().findInstancePin(restored_instance, "A");
  ASSERT_TRUE(pin);
  EXPECT_EQ(restored->netlistStorage().instancePin(pin).instance, restored_instance);

  BinaryDatabaseExporter::saveDesign(fixed.path(), *restored);
  expectSameFile(binary.path(), fixed.path());
}

TEST(DesignBinaryDatabaseArchiveTest, RoundTripsRouterGuidesTracksAndPinAccess)
{
  DesignFixture fixture;
  const auto layer = TechRoutingLayerId{fixture.technology.findLayer("M1").entity()};
  const auto net = fixture.design.netlistStorage().findRegularNet("n1");
  const auto instance = fixture.design.netlistStorage().findInstance("u1");
  const auto master = fixture.design.netlistStorage().instance(instance).master;
  const auto term = fixture.library.masterTermStorage().findMasterTerm(master, "A");
  const auto ports = fixture.library.masterPortStorage().masterPorts(term);
  ASSERT_FALSE(ports.empty());
  const auto pin = fixture.design.netlistStorage().findInstancePin(instance, term);
  const auto io_pin = fixture.design.netlistStorage().findIoPin("IN");
  ASSERT_TRUE(net);
  ASSERT_TRUE(instance);
  ASSERT_TRUE(pin);
  ASSERT_TRUE(io_pin);

  fixture.design.guideStorage().replaceGuides(
      net, {{.box = {0, 100, 900, 300}, .layer = layer, .flags = DesignRouteGuideFlag::kJumper}});
  fixture.design.guideStorage().replaceTrackAssignments(
      net, {{.layer = layer, .box = {0, 200, 900, 200}}});
  const auto candidate = DesignPinAccessPoint{
      .point = {10, 20},
      .layer = layer,
      .access_directions = DesignPinAccessDirection::kEast | DesignPinAccessDirection::kWest};
  fixture.design.pinAccessStorage().setMasterPortCandidateGroup(ports.front(), 0u, {candidate});
  fixture.design.pinAccessStorage().setInstancePinAccessGroup(instance, 0u);
  fixture.design.pinAccessStorage().setPreferredInstancePinAccess(
      pin, {{.master_port = ports.front(), .group_index = 0u, .point_index = 0u}});
  fixture.design.pinAccessStorage().setIoPinPortCandidateGroup(io_pin, 0u, 0u, {candidate});
  const auto bpin = fixture.design.netlistStorage().blockPins(io_pin).front();
  ASSERT_TRUE(fixture.design.designRegistry().registry().all_of<DesignBlockPin>(bpin.entity()));

  const TemporaryFile binary{"router-context", ".edb"};
  const TemporaryFile fixed{"router-context-fixed", ".edb"};
  BinaryDatabaseExporter::saveDesign(binary.path(), fixture.design);
  auto restored = BinaryDatabaseImporter::loadDesign(binary.path(), fixture.technology, fixture.library);

  ASSERT_EQ(restored->guideStorage().guides(net).size(), 1u);
  EXPECT_EQ(restored->guideStorage().guides(net).front().box, (Rect{0, 100, 900, 300}));
  ASSERT_EQ(restored->guideStorage().trackAssignments(net).size(), 1u);
  EXPECT_EQ(restored->guideStorage().trackAssignments(net).front().box, (Rect{0, 200, 900, 200}));
  ASSERT_EQ(restored->pinAccessStorage().masterPortCandidateGroup(ports.front(), 0u).points.size(), 1u);
  EXPECT_EQ(restored->pinAccessStorage().masterPortCandidateGroup(ports.front(), 0u).points.front().point,
            (Point{10, 20}));
  EXPECT_EQ(restored->pinAccessStorage().instancePinAccessGroup(instance), 0u);
  ASSERT_EQ(restored->pinAccessStorage().preferredInstancePinAccess(pin).size(), 1u);
  EXPECT_EQ(restored->pinAccessStorage().preferredInstancePinAccess(pin).front().master_port, ports.front());
  ASSERT_EQ(restored->pinAccessStorage().ioPinPortCandidateGroup(io_pin, 0u, 0u).points.size(), 1u);
  ASSERT_EQ(restored->netlistStorage().blockPins(io_pin).size(), 1u);
  EXPECT_EQ(restored->netlistStorage().blockPins(io_pin).front(), bpin);
  EXPECT_EQ(restored->netlistStorage().blockPin(bpin).term, io_pin);
  ASSERT_TRUE(restored->designRegistry().registry().all_of<DesignBlockPinAccessGroups>(bpin.entity()));
  EXPECT_EQ(restored->pinAccessStorage().blockPinCandidateGroup(bpin, 0u).points.front().point, candidate.point);

  BinaryDatabaseExporter::saveDesign(fixed.path(), *restored);
  expectSameFile(binary.path(), fixed.path());
}

TEST(DesignBinaryDatabaseArchiveTest, DoesNotReuseDeletedInstancePinIdsAfterRestore)
{
  DesignFixture fixture;
  auto& netlist = fixture.design.netlistStorage();
  const auto master = netlist.instance(netlist.findInstance("u1")).master;
  const auto temporary = netlist.createInstance(DesignInstance{.name = "temporary", .master = master});
  const auto deleted_pin = netlist.findInstancePin(temporary, "A");
  ASSERT_TRUE(deleted_pin);
  ASSERT_TRUE(netlist.destroyInstance(temporary));
  const TemporaryFile binary{"deleted-pin-id", ".edb"};
  BinaryDatabaseExporter::saveDesign(binary.path(), fixture.design);
  auto restored = BinaryDatabaseImporter::loadDesign(binary.path(), fixture.technology, fixture.library);
  auto& restored_netlist = restored->netlistStorage();
  EXPECT_FALSE(restored_netlist.contains(deleted_pin));
  const auto replacement = restored_netlist.createInstance(DesignInstance{.name = "replacement", .master = master});
  EXPECT_NE(restored_netlist.findInstancePin(replacement, "A"), deleted_pin);
  EXPECT_FALSE(restored_netlist.contains(deleted_pin));
}

TEST(DesignBinaryDatabaseArchiveTest, RejectsTruncatedPayload)
{
  DesignFixture fixture;
  const TemporaryFile binary{"bound-design", ".edb"};
  BinaryDatabaseExporter::saveDesign(binary.path(), fixture.design);
  std::filesystem::resize_file(binary.path(), std::filesystem::file_size(binary.path()) - 1u);
  EXPECT_THROW(BinaryDatabaseImporter::loadDesign(binary.path(), fixture.technology, fixture.library), std::runtime_error);
}

TEST(DesignBinaryDatabaseArchiveTest, RejectsSchemasPrecedingShapeRouting)
{
  DesignFixture fixture;
  const TemporaryFile binary{"prior-shape-routing-schema", ".edb"};
  for (const uint32_t schema_version : {6u, 7u, 8u}) {
    BinaryDatabaseExporter::saveDesign(binary.path(), fixture.design);
    std::fstream output(binary.path(), std::ios::in | std::ios::out | std::ios::binary);
    ASSERT_TRUE(output);
    // Header: magic[8], format version, header size, kind, schema version.
    output.seekp(20);
    const std::array<char, 4> encoded_schema{static_cast<char>(schema_version), 0, 0, 0};
    output.write(encoded_schema.data(), static_cast<std::streamsize>(encoded_schema.size()));
    output.close();

    EXPECT_THROW(BinaryDatabaseImporter::loadDesign(binary.path(), fixture.technology, fixture.library), std::runtime_error);
  }
}

TEST(DesignBinaryDatabaseArchiveTest, LargeIspd19ShapeRoutingReachesByteExactFixedPoint)
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

  const auto expected_statistics = source->routingStorage().routingStatistics();
  EXPECT_EQ(source->routingStorage().wireCount(), 895077u);
  EXPECT_GT(expected_statistics.shapes.count, 0u);
  EXPECT_EQ(expected_statistics.paths.count, 46978419u);
  EXPECT_EQ(expected_statistics.points.count, 74768813u);
  EXPECT_EQ(expected_statistics.vias.count, 14697732u);
  EXPECT_EQ(expected_statistics.rectangles.count, 4490293u);

  const TemporaryFile binary{"ispd19-test10-design", ".edb"};
  const TemporaryFile fixed{"ispd19-test10-design-fixed", ".edb"};
  const auto first_write_begin = Clock::now();
  BinaryDatabaseExporter::saveDesign(binary.path(), *source);
  const auto first_write_seconds = elapsedSeconds(first_write_begin);
  const auto archive_bytes = std::filesystem::file_size(binary.path());

  const auto read_begin = Clock::now();
  auto restored = BinaryDatabaseImporter::loadDesign(binary.path(), technology, library);
  const auto read_seconds = elapsedSeconds(read_begin);
  const auto actual_statistics = restored->routingStorage().routingStatistics();
  EXPECT_EQ(restored->routingStorage().wireCount(), source->routingStorage().wireCount());
  EXPECT_EQ(restored->netlistStorage().instanceCount(), source->netlistStorage().instanceCount());
  EXPECT_EQ(restored->netlistStorage().instancePinCount(), source->netlistStorage().instancePinCount());
  EXPECT_EQ(restored->netlistStorage().netCount(), source->netlistStorage().netCount());
  EXPECT_EQ(actual_statistics.shapes.count, expected_statistics.shapes.count);
  EXPECT_EQ(actual_statistics.paths.count, expected_statistics.paths.count);
  EXPECT_EQ(actual_statistics.path_extras.count, expected_statistics.path_extras.count);
  EXPECT_EQ(actual_statistics.points.count, expected_statistics.points.count);
  EXPECT_EQ(actual_statistics.vias.count, expected_statistics.vias.count);
  EXPECT_EQ(actual_statistics.rectangles.count, expected_statistics.rectangles.count);

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
