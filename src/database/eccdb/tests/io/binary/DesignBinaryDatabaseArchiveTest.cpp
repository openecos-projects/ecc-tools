// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

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
  auto& source_netlist = fixture.design.netlistStorage();
  const auto source_regular = source_netlist.findRegularNet("n1");
  const std::vector<std::string> source_aliases{R"(\rtl_n1[0])", "n1", R"(\rtl_n1[0])"};
  ASSERT_TRUE(source_regular);
  source_netlist.setNetAliases(source_regular, DesignNetAliases{.names = source_aliases});
  const auto expected = test::makeDesignSemanticSnapshot(fixture.design);
  const auto expected_def = DefDesignExporter(fixture.design).exportText();
  const auto expected_statistics = fixture.design.routingStorage().routingStatistics();

  BinaryDatabaseExporter::saveDesign(binary.path(), fixture.design);
  const auto source_instance = source_netlist.findInstance("u1");
  const auto source_term_id = source_netlist.findInstanceTerm(source_instance, "A");
  ASSERT_TRUE(source_term_id);
  auto& source_registry = fixture.design.designRegistry().registry();
  auto& source_terms = source_registry.get<DesignInstanceTerms>(source_instance.entity());
  ASSERT_EQ(source_terms.values.size(), source_terms.names.size());
  const auto source_name = source_terms.names.find("A");
  ASSERT_NE(source_name, source_terms.names.end());
  EXPECT_EQ(source_name->second, source_term_id);
  auto& source_term_value = source_registry.get<DesignInstanceTerm>(source_term_id.entity());
  const auto source_master_term = source_term_value.master_term;
  const auto* cached_instance = source_term_value.instance_ptr;
  const auto* cached_term = source_term_value.master_term_ptr;
  EXPECT_EQ(source_term_value.instance, source_instance);
  ASSERT_EQ(cached_instance, &source_netlist.instance(source_instance));
  ASSERT_EQ(cached_term, &fixture.library.masterTermStorage().masterTerm(source_master_term));
  EXPECT_EQ(source_netlist.instanceTerm(source_term_id).master_term, source_master_term);
  auto source_names = std::move(source_terms.names);
  source_terms.names.clear();
  source_term_value.instance_ptr = nullptr;
  source_term_value.master_term_ptr = nullptr;
  BinaryDatabaseExporter::saveDesign(transient.path(), fixture.design);
  source_terms.names = std::move(source_names);
  source_term_value.instance_ptr = cached_instance;
  source_term_value.master_term_ptr = cached_term;
  expectSameFile(binary.path(), transient.path());

  auto restored = BinaryDatabaseImporter::loadDesign(binary.path(), fixture.technology, fixture.library);

  EXPECT_EQ(test::makeDesignSemanticSnapshot(*restored), expected);
  EXPECT_EQ(DefDesignExporter(*restored).exportText(), expected_def);
  EXPECT_TRUE(restored->netlistStorage().findInstance("u1"));
  const auto restored_regular = restored->netlistStorage().findRegularNet("n1");
  EXPECT_EQ(restored_regular, source_regular);
  const auto* restored_aliases = restored->netlistStorage().netAliases(restored_regular);
  ASSERT_NE(restored_aliases, nullptr);
  EXPECT_EQ(restored_aliases->names, source_aliases);
  EXPECT_TRUE(restored->netlistStorage().findSpecialNet("VDD"));
  EXPECT_EQ(restored->routingStorage().wires(restored_regular).size(), 1u);

  const auto restored_instance = restored->netlistStorage().findInstance("u1");
  const auto& restored_registry = std::as_const(restored->designRegistry().registry());
  const auto& restored_terms = restored_registry.get<const DesignInstanceTerms>(restored_instance.entity());
  EXPECT_EQ(restored_terms.values.size(), restored_terms.names.size());
  EXPECT_EQ(restored_terms.values, source_terms.values);
  EXPECT_EQ(restored_terms.names.size(), source_terms.names.size());
  for (const auto& [name, source_term_id] : source_terms.names) {
    const auto restored_name = restored_terms.names.find(name);
    ASSERT_NE(restored_name, restored_terms.names.end());
    EXPECT_EQ(restored_name->second, source_term_id);
    const auto& source_term = source_registry.get<const DesignInstanceTerm>(source_term_id.entity());
    const auto& restored_term = restored_registry.get<const DesignInstanceTerm>(restored_name->second.entity());
    EXPECT_EQ(restored_term.master_term, source_term.master_term);
  }
  ASSERT_NE(restored_terms.names.find("A"), restored_terms.names.end());
  const auto restored_term_id = restored->netlistStorage().findInstanceTerm(restored_instance, "A");
  ASSERT_TRUE(restored_term_id);
  const auto& restored_term = restored->netlistStorage().instanceTerm(restored_term_id);
  EXPECT_EQ(restored_term.instance, restored_instance);
  EXPECT_EQ(restored_term.master_term, source_master_term);
  EXPECT_EQ(restored_term.instance_ptr, &restored->netlistStorage().instance(restored_instance));
  EXPECT_EQ(restored_term.master_term_ptr, &fixture.library.masterTermStorage().masterTerm(restored_term.master_term));
  EXPECT_NE(restored_registry.storage<DesignInstanceTerm>(), nullptr);
  EXPECT_TRUE(restored_registry.all_of<DesignInstanceTerm>(restored_term_id.entity()));

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

TEST(DesignBinaryDatabaseArchiveTest, RoundTripsDeletedInstanceTermsAndReusedSlots)
{
  DesignFixture fixture;
  auto& netlist = fixture.design.netlistStorage();
  const auto master = netlist.instance(netlist.findInstance("u1")).master;
  const auto temporary = netlist.createInstance(DesignInstance{.name = "temporary", .master = master});
  const std::array deleted_terms{netlist.findInstanceTerm(temporary, "A"), netlist.findInstanceTerm(temporary, "Y")};
  ASSERT_TRUE(deleted_terms.front());
  ASSERT_TRUE(deleted_terms.back());
  ASSERT_TRUE(netlist.destroyInstance(temporary));
  for (const auto term : deleted_terms) {
    EXPECT_FALSE(netlist.contains(term));
  }

  const TemporaryFile binary{"design-deleted-terms", ".edb"};
  const TemporaryFile deleted_fixed{"design-deleted-terms-fixed", ".edb"};
  BinaryDatabaseExporter::saveDesign(binary.path(), fixture.design);
  auto restored = BinaryDatabaseImporter::loadDesign(binary.path(), fixture.technology, fixture.library);
  EXPECT_FALSE(restored->netlistStorage().findInstance("temporary"));
  for (const auto term : deleted_terms) {
    EXPECT_FALSE(restored->netlistStorage().contains(term));
  }
  BinaryDatabaseExporter::saveDesign(deleted_fixed.path(), *restored);
  expectSameFile(binary.path(), deleted_fixed.path());

  auto& restored_netlist = restored->netlistStorage();
  const auto restored_regular = restored_netlist.findRegularNet("n1");
  const auto replacement = restored_netlist.createInstance(DesignInstance{.name = "replacement", .master = master});
  const auto replacement_view = restored_netlist.instanceTerms(replacement);
  const std::vector<DesignInstanceTermId> replacement_terms{replacement_view.begin(), replacement_view.end()};
  ASSERT_EQ(replacement_terms.size(), deleted_terms.size());
  const auto replacement_term = restored_netlist.findInstanceTerm(replacement, "A");
  ASSERT_TRUE(replacement_term);

  DesignInstanceTermId reused_term;
  DesignInstanceTermId replaced_term;
  for (const auto term : replacement_terms) {
    for (const auto deleted : deleted_terms) {
      if (entt::to_entity(term.entity()) == entt::to_entity(deleted.entity())) {
        reused_term = term;
        replaced_term = deleted;
        break;
      }
    }
    if (reused_term) {
      break;
    }
  }
  ASSERT_TRUE(reused_term);
  EXPECT_NE(reused_term, replaced_term);
  EXPECT_NE(entt::to_version(reused_term.entity()), entt::to_version(replaced_term.entity()));
  for (const auto term : deleted_terms) {
    EXPECT_FALSE(restored_netlist.contains(term));
  }

  const auto& replacement_relation
      = restored->designRegistry().registry().get<const DesignInstanceTerms>(replacement.entity());
  const auto replacement_name = replacement_relation.names.find("A");
  ASSERT_NE(replacement_name, replacement_relation.names.end());
  EXPECT_EQ(replacement_name->second, replacement_term);
  restored_netlist.connect(replacement_term, restored_regular);
  EXPECT_EQ(restored_netlist.instanceTerm(replacement_term).net, restored_regular);

  const TemporaryFile reused_binary{"design-reused-term-slots", ".edb"};
  const TemporaryFile reused_fixed{"design-reused-term-slots-fixed", ".edb"};
  BinaryDatabaseExporter::saveDesign(reused_binary.path(), *restored);
  auto round_tripped = BinaryDatabaseImporter::loadDesign(reused_binary.path(), fixture.technology, fixture.library);
  auto& round_tripped_netlist = round_tripped->netlistStorage();
  for (const auto term : deleted_terms) {
    EXPECT_FALSE(round_tripped_netlist.contains(term));
  }
  const auto round_tripped_replacement = round_tripped_netlist.findInstance("replacement");
  ASSERT_TRUE(round_tripped_replacement);
  const auto round_tripped_term = round_tripped_netlist.findInstanceTerm(round_tripped_replacement, "A");
  ASSERT_TRUE(round_tripped_term);
  EXPECT_EQ(round_tripped_term, replacement_term);
  EXPECT_EQ(entt::to_entity(round_tripped_term.entity()), entt::to_entity(replacement_term.entity()));
  EXPECT_EQ(entt::to_version(round_tripped_term.entity()), entt::to_version(replacement_term.entity()));
  EXPECT_EQ(round_tripped_netlist.instanceTerm(round_tripped_term).net, round_tripped_netlist.findRegularNet("n1"));
  const auto& round_tripped_relation
      = round_tripped->designRegistry().registry().get<const DesignInstanceTerms>(round_tripped_replacement.entity());
  const auto round_tripped_name = round_tripped_relation.names.find("A");
  ASSERT_NE(round_tripped_name, round_tripped_relation.names.end());
  EXPECT_EQ(round_tripped_name->second, round_tripped_term);
  for (const auto term : replacement_terms) {
    EXPECT_TRUE(round_tripped_netlist.contains(term));
    EXPECT_EQ(round_tripped_netlist.instanceTerm(term).instance, round_tripped_replacement);
  }

  BinaryDatabaseExporter::saveDesign(reused_fixed.path(), *round_tripped);
  expectSameFile(reused_binary.path(), reused_fixed.path());
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

  const auto restored_guides = restored->guideStorage().guides(net);
  ASSERT_EQ(restored_guides.size(), 1u);
  EXPECT_EQ(restored->guideStorage().guide(restored_guides.front()).box, (Rect{0, 100, 900, 300}));
  const auto restored_assignments = restored->guideStorage().trackAssignments(net);
  ASSERT_EQ(restored_assignments.size(), 1u);
  EXPECT_EQ(restored->guideStorage().trackAssignment(restored_assignments.front()).box, (Rect{0, 200, 900, 200}));
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

TEST(DesignBinaryDatabaseArchiveTest, DoesNotReviveDeletedInstanceTermIdsAfterRestore)
{
  DesignFixture fixture;
  auto& netlist = fixture.design.netlistStorage();
  const auto master = netlist.instance(netlist.findInstance("u1")).master;
  const auto temporary = netlist.createInstance(DesignInstance{.name = "temporary", .master = master});
  const auto deleted_term = netlist.findInstanceTerm(temporary, "A");
  ASSERT_TRUE(deleted_term);
  ASSERT_TRUE(netlist.destroyInstance(temporary));
  const TemporaryFile binary{"deleted-term-id", ".edb"};
  BinaryDatabaseExporter::saveDesign(binary.path(), fixture.design);
  auto restored = BinaryDatabaseImporter::loadDesign(binary.path(), fixture.technology, fixture.library);
  auto& restored_netlist = restored->netlistStorage();
  EXPECT_FALSE(restored_netlist.contains(deleted_term));
  const auto replacement = restored_netlist.createInstance(DesignInstance{.name = "replacement", .master = master});
  EXPECT_NE(restored_netlist.findInstanceTerm(replacement, "A"), deleted_term);
  EXPECT_FALSE(restored_netlist.contains(deleted_term));
}

TEST(DesignBinaryDatabaseArchiveTest, RejectsTruncatedPayload)
{
  DesignFixture fixture;
  const TemporaryFile binary{"bound-design", ".edb"};
  BinaryDatabaseExporter::saveDesign(binary.path(), fixture.design);
  std::filesystem::resize_file(binary.path(), std::filesystem::file_size(binary.path()) - 1u);
  EXPECT_THROW(BinaryDatabaseImporter::loadDesign(binary.path(), fixture.technology, fixture.library), std::runtime_error);
}

TEST(DesignBinaryDatabaseArchiveTest, RejectsLegacyDesignSchemas)
{
  TechStore technology;
  LibraryStore library{technology.techRegistry()};
  const TemporaryFile binary{"legacy-design-schema", ".edb"};
  constexpr uint32_t kEmbeddedInstanceTermSchemaVersion = 12u;
  constexpr uint32_t kBeforeNetAliasesSchemaVersion = 14u;
  for (const uint32_t schema_version : {6u, 7u, 8u, kEmbeddedInstanceTermSchemaVersion, kBeforeNetAliasesSchemaVersion}) {
    binary_detail::BinaryFileHeader header;
    header.kind = binary_detail::BinaryDatabaseKind::kDesign;
    header.schema_version = schema_version;
    binary_detail::writeBinaryFile(binary.path(), header, [](std::ostream&) {});
    try {
      static_cast<void>(BinaryDatabaseImporter::loadDesign(binary.path(), technology, library));
      ADD_FAILURE() << "accepted legacy design schema " << schema_version;
    } catch (const std::runtime_error& error) {
      EXPECT_EQ(std::string_view{error.what()}, "unsupported binary database schema version");
    }
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
