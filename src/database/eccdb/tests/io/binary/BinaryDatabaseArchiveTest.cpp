// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "binary/BinaryDatabaseExporter.h"
#include "lef/LefLibraryExporter.h"
#include "lef/LefTechExporter.h"
#include "binary/BinaryDatabaseImporter.h"
#include "geometry/DomainGeometryComponents.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "LefPdkCorpus.h"
#include "binary/BinaryArchive.h"
#include "library/cell_master/storage/MasterObsInput.h"
#include "library/common/LibraryGeometry.h"
#include "library/master_port/storage/MasterPortInput.h"
#include "tech/TechStore.h"
#include "tech/via_master/storage/ViaMasterInput.h"

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
            / ("idb-refactor-binary-" + safe_stem + '-' + std::to_string(getpid()) + '-' + std::to_string(nonce) + std::string(extension));
    if (!contents.empty()) {
      std::ofstream output(_path, std::ios::binary);
      output << contents;
      if (!output) {
        throw std::runtime_error("failed to create temporary test file");
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
  uint64_t offset = 0;
  while (left) {
    left.read(left_buffer.data(), left_buffer.size());
    right.read(right_buffer.data(), right_buffer.size());
    ASSERT_EQ(left.gcount(), right.gcount()) << offset;
    ASSERT_TRUE(std::equal(left_buffer.begin(), left_buffer.begin() + left.gcount(), right_buffer.begin())) << offset;
    offset += static_cast<uint64_t>(left.gcount());
  }
}

std::string exportTech(const TechStore& technology)
{
  std::ostringstream output;
  LefTechExporter::write(output, technology);
  return output.str();
}

std::string exportLibrary(const TechStore& technology, const LibraryStore& library)
{
  std::ostringstream output;
  LefLibraryExporter::write(output, technology, library);
  return output.str();
}

void expectPdkRoundTrip(const lef_test::LefPdkDomain& domain)
{
  TechStore source_technology;
  LefTechImporter(source_technology).import(domain.technology);
  LibraryStore source_library(source_technology.techRegistry());
  std::vector<std::filesystem::path> files{domain.technology};
  files.insert(files.end(), domain.cells.begin(), domain.cells.end());
  LefLibraryImporter(source_technology, source_library).import(files);
  const auto expected_tech_lef = exportTech(source_technology);
  const auto expected_library_lef = exportLibrary(source_technology, source_library);
  const TemporaryFile tech_binary(domain.name + "-tech", ".edb");
  const TemporaryFile library_binary(domain.name + "-library", ".edb");
  const TemporaryFile tech_binary_fixed(domain.name + "-tech-fixed", ".edb");
  const TemporaryFile library_binary_fixed(domain.name + "-library-fixed", ".edb");

  BinaryDatabaseExporter::saveTech(tech_binary.path(), source_technology);
  BinaryDatabaseExporter::saveLibrary(library_binary.path(), source_library);
  auto technology = BinaryDatabaseImporter::loadTech(tech_binary.path());
  auto library = BinaryDatabaseImporter::loadLibrary(library_binary.path(), *technology);
  EXPECT_EQ(expected_tech_lef, exportTech(*technology));
  EXPECT_EQ(expected_library_lef, exportLibrary(*technology, *library));

  BinaryDatabaseExporter::saveTech(tech_binary_fixed.path(), *technology);
  BinaryDatabaseExporter::saveLibrary(library_binary_fixed.path(), *library);
  expectSameFile(tech_binary.path(), tech_binary_fixed.path());
  expectSameFile(library_binary.path(), library_binary_fixed.path());
}

void expectPdkRoundTripInChild(const lef_test::LefPdkDomain& domain)
{
  std::fflush(nullptr);
  const pid_t child = fork();
  ASSERT_GE(child, 0) << domain.name;
  if (child == 0) {
    int exit_code = 0;
    try {
      expectPdkRoundTrip(domain);
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
  ASSERT_TRUE(WIFEXITED(status)) << domain.name;
  EXPECT_EQ(WEXITSTATUS(status), 0) << domain.name;
}

TEST(BinaryDatabaseArchiveTest, RoundTripsTechLibraryIdsGeometryAndGenerations)
{
  const TemporaryFile tech_lef("synthetic-tech", ".lef", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
USEMINSPACING OBS ON ;
MANUFACTURINGGRID 0.005 ;
PROPERTYDEFINITIONS
  LAYER LEF58_TYPE STRING ;
  LAYER LEF58_BACKSIDE STRING ;
  LAYER LEF58_ENCLOSURE STRING ;
  LAYER LEF58_SPACINGTABLE STRING ;
  LAYER LEF58_CORNERSPACING STRING ;
END PROPERTYDEFINITIONS
LAYER PIMP TYPE IMPLANT ; WIDTH 0.05 ; END PIMP
LAYER NIMP TYPE IMPLANT ; WIDTH 0.06 ; SPACING 0.03 LAYER PIMP ; END NIMP
LAYER M1
  TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.20 ; WIDTH 0.10 ;
  SPACING 0.10 ;
  SPACING 0.11 ENDOFLINE 0.12 WITHIN 0.13 PARALLELEDGE 0.14 WITHIN 0.15 TWOEDGES ;
  PROPERTY LEF58_TYPE "TYPE POLYROUTING ;" ;
  PROPERTY LEF58_BACKSIDE "BACKSIDE ;" ;
  PROPERTY LEF58_CORNERSPACING "CORNERSPACING CONVEXCORNER CORNERTOCORNER EXCEPTEOL 0.16 WIDTH 0.17 SPACING 0.18 ;" ;
END M1
LAYER V0 TYPE CUT ; WIDTH 0.04 ; END V0
LAYER V1
  TYPE CUT ; WIDTH 0.10 ; SPACING 0.08 ; SPACING 0.085 SAMENET ; SPACING 0.09 ADJACENTCUTS 3 WITHIN 0.10 ;
  SPACING 0.11 CENTERTOCENTER SAMENET AREA 0.02 ;
  SPACING 0.12 SAMENET PGONLY ;
  SPACING 0.13 SAMENET LAYER V0 STACK ;
  SPACING 0.14 ADJACENTCUTS 2 WITHIN 0.15 EXCEPTSAMEPGNET ;
  SPACING 0.16 PARALLELOVERLAP ;
  ENCLOSURE 0.01 0.02 LENGTH 0.03 ;
  ENCLOSURE BELOW 0.04 0.05 WIDTH 0.06 EXCEPTEXTRACUT 0.07 ;
  ARRAYSPACING WIDTH 0.08 CUTSPACING 0.09 ARRAYCUTS 2 SPACING 0.10 ;
  SPACINGTABLE ORTHOGONAL WITHIN 0.30 SPACING 0.20 WITHIN 0.10 SPACING 0.15 ;
  PROPERTY LEF58_TYPE "TYPE SPECIALCUT ;" ;
  PROPERTY LEF58_BACKSIDE "BACKSIDE ;" ;
  PROPERTY LEF58_ENCLOSURE
    "ENCLOSURE CUTCLASS C1 ABOVE 0.11 0.12 WIDTH 0.13 INCLUDEABUTTED EXCEPTEXTRACUT 0.14 NOSHAREDEDGE ;" ;
  PROPERTY LEF58_SPACINGTABLE
    "SPACINGTABLE ORTHOGONAL WITHIN 0.21 SPACING 0.22 WITHIN 0.23 SPACING 0.24 ;
     SPACINGTABLE DEFAULT 0.12 SAMEMASK SAMENET
       LAYER V0 NOSTACK PRLFORALIGNEDCUT C1 TO C2 C3 TO C4
       PRL 0.14 MAXXY C1 TO C2 0.30
       CUTCLASS C1 SIDE C1 END C2
       C3 0.10 0.20 0.30 - 0.40 0.50
       C4 0.60 - 0.70 - 0.80 0.90 ;" ;
END V1
LAYER M2 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.30 ; WIDTH 0.12 ; END M2
VIA V12 DEFAULT
  LAYER M1 ; RECT -0.06 -0.07 0.06 0.07 ;
  LAYER V1 ; POLYGON -0.04 -0.04 0.04 -0.04 0.04 0.04 -0.04 0.04 ;
  LAYER M2 ; RECT -0.07 -0.06 0.07 0.06 ;
END V12
NONDEFAULTRULE WIDE
  LAYER M1 WIDTH 0.20 ; SPACING 0.20 ; END M1
  VIA NDR_VIA
    LAYER M1 ; RECT -0.06 -0.07 0.06 0.07 ;
    LAYER V1 ; RECT -0.04 -0.04 0.04 0.04 ;
    LAYER M2 ; RECT -0.07 -0.06 0.07 0.06 ;
  END NDR_VIA
  USEVIA NDR_VIA ;
  MINCUTS V1 2 ;
END WIDE
END LIBRARY
)LEF");
  const TemporaryFile library_lef("synthetic-library", ".lef", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
SITE CORE_SITE CLASS CORE ; SYMMETRY X Y ; SIZE 0.20 BY 0.40 ; END CORE_SITE
MACRO CELL_X1
  CLASS CORE ; ORIGIN -0.10 0.20 ; SIZE 1.20 BY 2.40 ; SYMMETRY X Y ; SITE CORE_SITE ;
  PIN A
    DIRECTION INPUT ; USE SIGNAL ;
    PORT
      CLASS CORE ;
      LAYER M1 ; RECT 0.10 0.20 0.30 0.40 ;
      POLYGON 0.40 0.20 0.60 0.20 0.60 0.40 0.40 0.40 ;
      VIA ( 0.50 0.60 ) V12 ;
    END
  END A
  OBS LAYER M2 ; RECT 0.00 0.00 0.20 0.20 ; VIA ( 0.70 0.80 ) V12 ; END
END CELL_X1
END LIBRARY
)LEF");

  TechStore source_technology;
  LefTechImporter(source_technology).import(tech_lef.path());
  source_technology.globalStorage().setUseMinSpacingPin(false);
  LibraryStore source_library(source_technology.techRegistry());
  LefLibraryImporter(source_technology, source_library).import(library_lef.path());

  auto& tech_registry = source_technology.techRegistry().registry();
  const auto stale = tech_registry.create();
  tech_registry.destroy(stale);
  const auto live = tech_registry.create();
  ASSERT_EQ(entt::to_entity(stale), entt::to_entity(live));
  ASSERT_NE(entt::to_version(stale), entt::to_version(live));

  const auto m1 = source_technology.findLayer("M1");
  const auto nimp = TechImplantLayerId{source_technology.findLayer("NIMP").entity()};
  const auto pimp = TechImplantLayerId{source_technology.findLayer("PIMP").entity()};
  const auto site = source_library.siteStorage().findSite("CORE_SITE");
  const auto master = source_library.cellMasterStorage().findCellMaster("CELL_X1");
  const auto source_via = source_technology.viaMasterStorage().findViaMaster("V12");
  const auto ndr = source_technology.nonDefaultRuleStorage().findNonDefaultRule("WIDE");
  const auto ndr_via = source_technology.nonDefaultRuleStorage().findViaDefinition("NDR_VIA");
  ASSERT_TRUE(m1);
  ASSERT_TRUE(nimp);
  ASSERT_TRUE(pimp);
  ASSERT_TRUE(site);
  ASSERT_TRUE(master);
  ASSERT_TRUE(source_via);
  ASSERT_TRUE(ndr);
  ASSERT_TRUE(ndr_via);

  const auto source_term = source_library.masterTermStorage().findMasterTerm(master, "A");
  ASSERT_TRUE(source_term);
  const auto source_ports = source_library.masterPortStorage().masterPorts(source_term);
  ASSERT_EQ(source_ports.size(), 1u);
  const auto& source_port = source_library.masterPortStorage().masterPort(source_ports.front());
  ASSERT_EQ(source_port.vias.size(), 1u);
  const auto source_port_via = source_port.vias.front();
  EXPECT_EQ(source_library.geometryRegistry().shape(source_port_via).kind, GeometryShapeKind::kLibraryVia);
  const auto& source_port_placement = source_library.geometryRegistry().get<const LibraryViaPlacement>(source_port_via);
  EXPECT_EQ(source_port_placement.via, source_via);
  EXPECT_EQ(source_port_placement.origin, (Point{500, 600}));
  const auto& source_obs = source_library.cellMasterStorage().obs(master);
  ASSERT_EQ(source_obs.vias.size(), 1u);
  const auto source_obs_via = source_obs.vias.front();
  EXPECT_EQ(source_library.geometryRegistry().shape(source_obs_via).kind, GeometryShapeKind::kLibraryVia);
  const auto& source_obs_placement = source_library.geometryRegistry().get<const LibraryViaPlacement>(source_obs_via);
  EXPECT_EQ(source_obs_placement.via, source_via);
  EXPECT_EQ(source_obs_placement.origin, (Point{700, 800}));

  const TemporaryFile tech_binary("synthetic-tech", ".edb");
  const TemporaryFile library_binary("synthetic-library", ".edb");
  const TemporaryFile tech_fixed("synthetic-tech-fixed", ".edb");
  const TemporaryFile library_fixed("synthetic-library-fixed", ".edb");
  BinaryDatabaseExporter::saveTech(tech_binary.path(), source_technology);
  BinaryDatabaseExporter::saveLibrary(library_binary.path(), source_library);

  auto technology = BinaryDatabaseImporter::loadTech(tech_binary.path());
  auto library = BinaryDatabaseImporter::loadLibrary(library_binary.path(), *technology);
  ASSERT_TRUE(technology->globalStorage().hasUseMinSpacingObs());
  ASSERT_TRUE(technology->globalStorage().hasUseMinSpacingPin());
  EXPECT_TRUE(technology->globalStorage().getUseMinSpacingObs());
  EXPECT_FALSE(technology->globalStorage().getUseMinSpacingPin());
  EXPECT_EQ(technology->findLayer("M1").packed(), m1.packed());
  const auto restored_m1 = TechRoutingLayerId{technology->findLayer("M1").entity()};
  const auto& restored_m1_info = technology->layerInfo(TechLayerId{restored_m1.entity()});
  EXPECT_EQ(restored_m1_info.lef58_type, TechLef58LayerType::kPolyRouting);
  EXPECT_NE(restored_m1_info.flags & TechLayerInfoFlag::kLef58Backside, 0u);
  const auto restored_eol = technology->routingLayerStorage().endOfLineSpacingRules(restored_m1);
  const auto restored_corner = technology->routingLayerStorage().lef58CornerSpacingRules(restored_m1);
  ASSERT_EQ(restored_eol.size(), 1u);
  ASSERT_EQ(restored_corner.size(), 1u);
  EXPECT_EQ(technology->routingLayerStorage().rule(restored_eol.front()).parallel_within, 150);
  EXPECT_EQ(technology->routingLayerStorage().rule(restored_corner.front()).width_spacings.front().spacing, 180);
  EXPECT_NE(technology->routingLayerStorage().rule(restored_corner.front()).flags
                & TechRoutingLef58CornerSpacingRuleFlag::kCornerToCorner,
            0u);
  const auto restored_v1 = TechCutLayerId{technology->findLayer("V1").entity()};
  ASSERT_EQ(technology->cutLayerStorage().spacingRules(restored_v1).size(), 8u);
  EXPECT_NE(technology->cutLayerStorage().spacingRule(technology->cutLayerStorage().spacingRules(restored_v1)[1]).flags
                & TechCutSpacingRuleFlag::kSameNet,
            0u);
  EXPECT_EQ(technology->cutLayerStorage().spacingRule(technology->cutLayerStorage().spacingRules(restored_v1)[2]).adjacent_cut_count, 3u);
  EXPECT_NE(technology->cutLayerStorage().spacingRule(technology->cutLayerStorage().spacingRules(restored_v1)[3]).flags
                & TechCutSpacingRuleFlag::kCenterToCenter,
            0u);
  EXPECT_EQ(technology->cutLayerStorage().spacingRule(technology->cutLayerStorage().spacingRules(restored_v1)[3]).cut_area, 20000);
  EXPECT_EQ(technology->cutLayerStorage().spacingRule(technology->cutLayerStorage().spacingRules(restored_v1)[5]).second_layer_name,
            "V0");
  const auto& cut_storage = technology->cutLayerStorage();
  const auto& restored_v1_info = technology->layerInfo(TechLayerId{restored_v1.entity()});
  EXPECT_EQ(restored_v1_info.lef58_type, TechLef58LayerType::kSpecialCut);
  EXPECT_NE(restored_v1_info.flags & TechLayerInfoFlag::kLef58Backside, 0u);
  const auto restored_enclosures = cut_storage.enclosureRules(restored_v1);
  ASSERT_EQ(restored_enclosures.size(), 2u);
  EXPECT_EQ(cut_storage.enclosureRule(restored_enclosures[0]).min_length, 30);
  EXPECT_EQ(cut_storage.enclosureRule(restored_enclosures[1]).min_width, 60);
  EXPECT_EQ(cut_storage.enclosureRule(restored_enclosures[1]).cut_within, 70);
  const auto restored_array = cut_storage.arraySpacingRule(restored_v1);
  ASSERT_TRUE(restored_array);
  EXPECT_NE(cut_storage.arraySpacingRule(restored_array).flags & TechCutArraySpacingRuleFlag::kHasViaWidth, 0u);
  EXPECT_EQ(cut_storage.arraySpacingRule(restored_array).via_width, 80);
  const auto restored_orthogonal = cut_storage.orthogonalSpacingTableRules(restored_v1);
  ASSERT_EQ(restored_orthogonal.size(), 2u);
  EXPECT_EQ(cut_storage.orthogonalSpacingTableRule(restored_orthogonal[0]).items[0].within, 300);
  EXPECT_NE(cut_storage.orthogonalSpacingTableRule(restored_orthogonal[1]).flags
                & TechCutOrthogonalSpacingTableRuleFlag::kLef58Property,
            0u);
  const auto restored_lef58_enclosures = cut_storage.lef58EnclosureRules(restored_v1);
  ASSERT_EQ(restored_lef58_enclosures.size(), 1u);
  const auto& restored_lef58_enclosure = cut_storage.lef58EnclosureRule(restored_lef58_enclosures.front());
  EXPECT_NE(restored_lef58_enclosure.flags & TechCutLef58EnclosureRuleFlag::kIncludeAbutted, 0u);
  EXPECT_NE(restored_lef58_enclosure.flags & TechCutLef58EnclosureRuleFlag::kNoSharedEdge, 0u);
  const auto restored_lef58_tables = cut_storage.lef58SpacingTableRules(restored_v1);
  ASSERT_EQ(restored_lef58_tables.size(), 1u);
  const auto& restored_lef58_table = cut_storage.lef58SpacingTableRule(restored_lef58_tables.front());
  EXPECT_EQ(restored_lef58_table.default_spacing, 120);
  EXPECT_NE(restored_lef58_table.flags & TechCutLef58SpacingTableRuleFlag::kNoStack, 0u);
  EXPECT_EQ(restored_lef58_table.cutclass1_edges,
            (std::vector<CutClassEdge>{CutClassEdge::kSide, CutClassEdge::kEnd, CutClassEdge::kUnspecified}));
  ASSERT_EQ(restored_lef58_table.prl_entries.size(), 1u);
  EXPECT_EQ(restored_lef58_table.prl_entries.front().prl, 300);
  const auto restored_nimp = TechImplantLayerId{technology->findLayer("NIMP").entity()};
  const auto restored_pimp = TechImplantLayerId{technology->findLayer("PIMP").entity()};
  ASSERT_TRUE(restored_nimp);
  ASSERT_TRUE(restored_pimp);
  ASSERT_EQ(technology->implantLayerStorage().spacingRules(restored_nimp).size(), 1u);
  EXPECT_EQ(technology->implantLayerStorage().spacingRules(restored_nimp).front().min_spacing, 30);
  EXPECT_EQ(technology->implantLayerStorage().spacingRules(restored_nimp).front().other_layer, restored_pimp);
  EXPECT_EQ(library->siteStorage().findSite("CORE_SITE").packed(), site.packed());
  EXPECT_EQ(library->cellMasterStorage().findCellMaster("CELL_X1").packed(), master.packed());
  const auto restored_master = library->cellMasterStorage().findCellMaster("CELL_X1");
  const auto restored_library_via = technology->viaMasterStorage().findViaMaster("V12");
  ASSERT_TRUE(restored_master);
  ASSERT_TRUE(restored_library_via);
  const auto restored_term = library->masterTermStorage().findMasterTerm(restored_master, "A");
  ASSERT_TRUE(restored_term);
  const auto restored_ports = library->masterPortStorage().masterPorts(restored_term);
  ASSERT_EQ(restored_ports.size(), 1u);
  const auto& restored_port = library->masterPortStorage().masterPort(restored_ports.front());
  ASSERT_EQ(restored_port.vias.size(), 1u);
  const auto restored_port_via = restored_port.vias.front();
  EXPECT_EQ(library->geometryRegistry().shape(restored_port_via).kind, GeometryShapeKind::kLibraryVia);
  const auto& restored_port_placement = library->geometryRegistry().get<const LibraryViaPlacement>(restored_port_via);
  EXPECT_EQ(restored_port_placement.via, restored_library_via);
  EXPECT_EQ(restored_port_placement.origin, (Point{500, 600}));
  const auto& restored_obs = library->cellMasterStorage().obs(restored_master);
  ASSERT_EQ(restored_obs.vias.size(), 1u);
  const auto restored_obs_via = restored_obs.vias.front();
  EXPECT_EQ(library->geometryRegistry().shape(restored_obs_via).kind, GeometryShapeKind::kLibraryVia);
  const auto& restored_obs_placement = library->geometryRegistry().get<const LibraryViaPlacement>(restored_obs_via);
  EXPECT_EQ(restored_obs_placement.via, restored_library_via);
  EXPECT_EQ(restored_obs_placement.origin, (Point{700, 800}));
  const auto restored_ndr = technology->nonDefaultRuleStorage().findNonDefaultRule("WIDE");
  const auto restored_ndr_via = technology->nonDefaultRuleStorage().findViaDefinition("NDR_VIA");
  ASSERT_TRUE(restored_ndr);
  ASSERT_TRUE(restored_ndr_via);
  EXPECT_EQ(restored_ndr.packed(), ndr.packed());
  EXPECT_EQ(restored_ndr_via.packed(), ndr_via.packed());
  ASSERT_EQ(technology->nonDefaultRuleStorage().routingRules(restored_ndr).size(), 1u);
  EXPECT_EQ(technology->nonDefaultRuleStorage().routingRules(restored_ndr).front().width, 200);
  ASSERT_EQ(technology->nonDefaultRuleStorage().useVias(restored_ndr).size(), 1u);
  EXPECT_EQ(technology->nonDefaultRuleStorage().useVias(restored_ndr).front(), restored_ndr_via);
  EXPECT_EQ(technology->nonDefaultRuleStorage().viaDefinitionOwner(restored_ndr_via), restored_ndr);
  EXPECT_TRUE(technology->techRegistry().registry().valid(live));
  EXPECT_EQ(technology->techRegistry().registry().current(stale), tech_registry.current(stale));
  EXPECT_EQ(exportTech(*technology), exportTech(source_technology));
  EXPECT_EQ(exportLibrary(*technology, *library), exportLibrary(source_technology, source_library));

  BinaryDatabaseExporter::saveTech(tech_fixed.path(), *technology);
  BinaryDatabaseExporter::saveLibrary(library_fixed.path(), *library);
  expectSameFile(tech_binary.path(), tech_fixed.path());
  expectSameFile(library_binary.path(), library_fixed.path());

  const auto m2 = technology->findLayer("M2");
  const auto fixed_via = technology->viaMasterStorage().findViaMaster("V12");
  ASSERT_TRUE(m2);
  ASSERT_TRUE(fixed_via);
  const auto restored_rule = technology->viaRuleStorage().createViaRule(
      TechViaRule{.name = "RESTORED_VIA_RULE"},
      TechViaRuleLowerLayer{.layer = TechRoutingLayerId{m1.entity()}, .direction = RoutingDirection::kHorizontal},
      TechViaRuleUpperLayer{.layer = TechRoutingLayerId{m2.entity()}, .direction = RoutingDirection::kVertical}, {fixed_via});
  EXPECT_TRUE(technology->viaRuleStorage().contains(restored_rule));
}

TEST(BinaryDatabaseArchiveTest, RejectsTruncatedAndTrailingPayloads)
{
  TechStore technology;
  technology.globalStorage().setUnits(
      TechGlobalUnits{.flags = TechGlobalUnitsFlag::kHasDatabaseUnitsPerMicron, .database_units_per_micron = 1000});
  LibraryStore library(technology.techRegistry());
  const TemporaryFile tech_binary("corrupt-tech", ".edb");
  const TemporaryFile library_binary("bound-library", ".edb");
  BinaryDatabaseExporter::saveTech(tech_binary.path(), technology);
  BinaryDatabaseExporter::saveLibrary(library_binary.path(), library);

  std::filesystem::resize_file(tech_binary.path(), std::filesystem::file_size(tech_binary.path()) - 1u);
  EXPECT_THROW(BinaryDatabaseImporter::loadTech(tech_binary.path()), std::runtime_error);

  {
    std::ofstream trailing(library_binary.path(), std::ios::binary | std::ios::app);
    trailing.put('x');
  }
  EXPECT_THROW(BinaryDatabaseImporter::loadLibrary(library_binary.path(), technology), std::runtime_error);
}

TEST(BinaryDatabaseArchiveTest, LibraryMasterTermDeletionReachesBinaryFixedPoint)
{
  TechStore technology;
  LibraryStore library(technology.techRegistry());
  const auto removed_master = library.cellMasterStorage().createCellMaster(LibraryCellMaster{.name = "REMOVED"});
  const auto survivor_master = library.cellMasterStorage().createCellMaster(LibraryCellMaster{.name = "SURVIVOR"});
  static_cast<void>(library.masterTermStorage().createMasterTerm(removed_master, LibraryMasterTerm{.name = "A"}));
  const auto survivor_term = library.masterTermStorage().createMasterTerm(survivor_master, LibraryMasterTerm{.name = "Y"});
  ASSERT_TRUE(library.cellMasterStorage().destroyCellMaster(removed_master));

  const TemporaryFile original("library-master-term-deletion", ".edb");
  const TemporaryFile fixed("library-master-term-deletion-fixed", ".edb");
  BinaryDatabaseExporter::saveLibrary(original.path(), library);

  auto restored = BinaryDatabaseImporter::loadLibrary(original.path(), technology);
  const auto restored_master = restored->cellMasterStorage().findCellMaster("SURVIVOR");
  ASSERT_TRUE(restored_master);
  EXPECT_EQ(restored_master, survivor_master);
  EXPECT_EQ(restored->masterTermStorage().findMasterTerm(restored_master, "Y"), survivor_term);

  BinaryDatabaseExporter::saveLibrary(fixed.path(), *restored);
  expectSameFile(original.path(), fixed.path());
}

TEST(BinaryDatabaseArchiveTest, LibraryViaPlacementsResolveAndCascadeWithTheirOwners)
{
  TechStore technology;
  const auto m1 = technology.createRoutingLayer(
      TechLayerInfo{.name = "M1"}, TechRoutingLayer{.direction = TechRoutingDirection::kHorizontal, .width = 10, .pitch_x = 20, .pitch_y = 20});
  const auto cut = technology.createCutLayer(TechLayerInfo{.name = "V1"}, TechCutLayer{.width = 6});
  const auto m2 = technology.createRoutingLayer(
      TechLayerInfo{.name = "M2"}, TechRoutingLayer{.direction = TechRoutingDirection::kVertical, .width = 12, .pitch_x = 24, .pitch_y = 24});
  const auto tech_via = technology.viaMasterStorage().createFixedViaMaster(
      TechViaMaster{.name = "VIA12"},
      TechViaMasterShapeInput{.bottom_layer = m1,
                              .bottom_geometry = {.rects = {{-2, -2, 2, 2}}},
                              .cut_layer = cut,
                              .cut_geometry = {.rects = {{-1, -1, 1, 1}}},
                              .top_layer = m2,
                              .top_geometry = {.rects = {{-3, -3, 3, 3}}}});
  LibraryStore library(technology.techRegistry());
  const auto master = library.cellMasterStorage().createCellMaster(LibraryCellMaster{.name = "CELL"});
  const auto term = library.masterTermStorage().createMasterTerm(master, LibraryMasterTerm{.name = "A"});

  const auto port = library.masterPortStorage().createMasterPort(
      term, LibraryMasterPortInput{.vias = {{.via = tech_via, .origin = {10, 20}}}});
  const auto first_port_via = library.masterPortStorage().masterPort(port).vias.front();
  ASSERT_TRUE(library.geometryRegistry().contains(first_port_via));
  EXPECT_EQ(library.geometryRegistry().shape(first_port_via).kind, GeometryShapeKind::kLibraryVia);
  EXPECT_EQ(library.geometryRegistry().get<const LibraryViaPlacement>(first_port_via).origin, (Point{10, 20}));

  library.masterPortStorage().updateMasterPort(port, LibraryMasterPortInput{.vias = {{.via = tech_via, .origin = {30, 40}}}});
  const auto updated_port_via = library.masterPortStorage().masterPort(port).vias.front();
  EXPECT_FALSE(library.geometryRegistry().contains(first_port_via));
  ASSERT_TRUE(library.geometryRegistry().contains(updated_port_via));
  EXPECT_EQ(library.geometryRegistry().get<const LibraryViaPlacement>(updated_port_via).via, tech_via);
  EXPECT_EQ(library.geometryRegistry().get<const LibraryViaPlacement>(updated_port_via).origin, (Point{30, 40}));

  const auto deleted_port = library.masterPortStorage().createMasterPort(
      term, LibraryMasterPortInput{.vias = {{.via = tech_via, .origin = {50, 60}}}});
  const auto deleted_port_via = library.masterPortStorage().masterPort(deleted_port).vias.front();
  ASSERT_TRUE(library.masterPortStorage().destroyMasterPort(deleted_port));
  EXPECT_FALSE(library.geometryRegistry().contains(deleted_port_via));

  library.cellMasterStorage().setObs(master, LibraryMasterObsInput{.vias = {{.via = tech_via, .origin = {70, 80}}}});
  const auto obs_via = library.cellMasterStorage().obs(master).vias.front();
  ASSERT_TRUE(library.geometryRegistry().contains(obs_via));
  EXPECT_EQ(library.geometryRegistry().get<const LibraryViaPlacement>(obs_via).origin, (Point{70, 80}));
  library.cellMasterStorage().clearObs(master);
  EXPECT_FALSE(library.geometryRegistry().contains(obs_via));

  ASSERT_TRUE(library.cellMasterStorage().destroyCellMaster(master));
  EXPECT_FALSE(library.geometryRegistry().contains(updated_port_via));
}

TEST(BinaryDatabaseArchiveTest, RejectsRestoredLibraryGeometryWithAmbiguousOrOrphanPayloads)
{
  TechStore technology;
  const auto m1 = technology.createRoutingLayer(
      TechLayerInfo{.name = "M1"}, TechRoutingLayer{.direction = TechRoutingDirection::kHorizontal, .width = 10, .pitch_x = 20, .pitch_y = 20});
  const auto cut = technology.createCutLayer(TechLayerInfo{.name = "V1"}, TechCutLayer{.width = 6});
  const auto m2 = technology.createRoutingLayer(
      TechLayerInfo{.name = "M2"}, TechRoutingLayer{.direction = TechRoutingDirection::kVertical, .width = 12, .pitch_x = 24, .pitch_y = 24});
  const auto tech_via = technology.viaMasterStorage().createFixedViaMaster(
      TechViaMaster{.name = "VIA12"},
      TechViaMasterShapeInput{.bottom_layer = m1,
                              .bottom_geometry = {.rects = {{-2, -2, 2, 2}}},
                              .cut_layer = cut,
                              .cut_geometry = {.rects = {{-1, -1, 1, 1}}},
                              .top_layer = m2,
                              .top_geometry = {.rects = {{-3, -3, 3, 3}}}});

  const auto add_port = [](LibraryStore& library, LibraryMasterPortInput input) {
    const auto master = library.cellMasterStorage().createCellMaster(LibraryCellMaster{.name = "CELL"});
    const auto term = library.masterTermStorage().createMasterTerm(master, LibraryMasterTerm{.name = "A"});
    return library.masterPortStorage().createMasterPort(term, input);
  };
  const auto expect_restore_rejected = [&](const LibraryStore& library, std::string_view stem) {
    const TemporaryFile archive{stem, ".edb"};
    ASSERT_NO_THROW(BinaryDatabaseExporter::saveLibrary(archive.path(), library));
    EXPECT_THROW(BinaryDatabaseImporter::loadLibrary(archive.path(), technology), std::runtime_error);
  };

  {
    LibraryStore library(technology.techRegistry());
    const auto port = add_port(
        library, LibraryMasterPortInput{.layer_clauses = {{.layer = TechLayerId{m1.entity()}, .geometry = {.rects = {{0, 0, 10, 10}}}}}});
    const auto& stored_port = library.masterPortStorage().masterPort(port);
    ASSERT_EQ(stored_port.layer_clauses.size(), 1u);
    ASSERT_EQ(stored_port.layer_clauses.front().geometry.size(), 1u);
    const auto rectangle = stored_port.layer_clauses.front().geometry.front();
    library.geometryRegistry().registry().emplace<GeometryPolygon>(rectangle.entity(), GeometryPolygon{.points = {{0, 0}, {10, 0}, {10, 10}}});
    expect_restore_rejected(library, "library-double-layer-payload");
  }

  {
    LibraryStore library(technology.techRegistry());
    const auto port = add_port(library, LibraryMasterPortInput{.vias = {{.via = tech_via, .origin = {10, 20}}}});
    const auto& stored_port = library.masterPortStorage().masterPort(port);
    ASSERT_EQ(stored_port.vias.size(), 1u);
    const auto via = stored_port.vias.front();
    library.geometryRegistry().registry().emplace<GeometryRectangle>(via.entity(), GeometryRectangle{.rectangle = {0, 0, 10, 10}});
    expect_restore_rejected(library, "library-via-with-rectangle-payload");
  }

  {
    LibraryStore library(technology.techRegistry());
    const auto port = add_port(
        library, LibraryMasterPortInput{.layer_clauses = {{.layer = TechLayerId{m1.entity()}, .geometry = {.rects = {{20, 20, 30, 30}}}}}});
    auto& stored_port = library.libraryRegistry().registry().get<LibraryMasterPort>(port.entity());
    const auto rectangle = stored_port.layer_clauses.front().geometry.front();
    stored_port.layer_clauses.front().geometry.clear();
    library.geometryRegistry().registry().remove<GeometryShape>(rectangle.entity());
    expect_restore_rejected(library, "library-payload-without-metadata");
  }
}

TEST(BinaryDatabaseArchiveTest, FullSky130AndIhp130ReachBinaryFixedPoint)
{
  auto corpus = lef_test::fullSky130Corpus(std::filesystem::path{ECC_TOOLS_SOURCE_DIR});
  auto ihp = lef_test::fullIhp130Corpus(std::filesystem::path{ECC_TOOLS_SOURCE_DIR});
  corpus.insert(corpus.end(), ihp.begin(), ihp.end());
  for (const auto& domain : corpus) {
    SCOPED_TRACE(domain.name);
    expectPdkRoundTripInChild(domain);
  }
}

}  // namespace
}  // namespace eccdb
