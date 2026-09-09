// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"

namespace eccdb {
namespace {

class LefLibraryImporterTest : public testing::Test
{
 protected:
  std::filesystem::path writeLef(std::string_view stem, std::string_view contents)
  {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path
        = std::filesystem::temp_directory_path()
          / ("idb-refactor-library-" + std::string(stem) + "-" + std::to_string(nonce) + "-" + std::to_string(_paths.size()) + ".lef");
    std::ofstream output(path);
    if (!output) {
      throw std::runtime_error("failed to create temporary LEF test input");
    }
    output << contents;
    output.close();
    _paths.push_back(path);
    return path;
  }

  void TearDown() override
  {
    for (const auto& path : _paths) {
      std::error_code error;
      std::filesystem::remove(path, error);
    }
  }

  static std::filesystem::path sourcePath(std::string_view relative) { return std::filesystem::path{ECC_TOOLS_SOURCE_DIR} / relative; }

  static std::size_t diagnosticCount(const LefLibraryImporter& importer, std::string_view statement)
  {
    for (const auto& diagnostic : importer.diagnostics()) {
      if (diagnostic.statement == statement) {
        return diagnostic.occurrence_count;
      }
    }
    return 0;
  }

  std::vector<std::filesystem::path> _paths;
};

TEST_F(LefLibraryImporterTest, ImportsSiteMacroPinPortsObsAndTechReferences)
{
  const auto technology_lef = writeLef("technology", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.20 ; WIDTH 0.10 ; END M1
LAYER V1 TYPE CUT ; WIDTH 0.10 ; END V1
LAYER M2 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.30 ; WIDTH 0.101 ; END M2
LAYER OVERLAP TYPE OVERLAP ; END OVERLAP
VIA V12
  LAYER M1 ; RECT -0.06 -0.07 0.06 0.07 ;
  LAYER V1 ; RECT -0.04 -0.04 0.04 0.04 ;
  LAYER M2 ; RECT -0.07 -0.06 0.07 0.06 ;
END V12
END LIBRARY
)LEF");
  const auto site_lef = writeLef("site", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
SITE CORE_SITE
  CLASS CORE ;
  SYMMETRY X Y R90 ;
  SIZE 0.20 BY 0.40 ;
END CORE_SITE
SITE VIRTUAL_SITE
  CLASS VIRTUAL ;
  SIZE 0.10 BY 0.10 ;
END VIRTUAL_SITE
END LIBRARY
)LEF");
  const auto macro_lef = writeLef("macro", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
MACRO FILL_X1
  CLASS CORE SPACER ;
  ORIGIN -0.10 0.20 ;
  SIZE 1.20 BY 2.40 ;
  SYMMETRY X Y ;
  SITE CORE_SITE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    SHAPE ABUTMENT ;
    PORT
      CLASS CORE ;
      LAYER M1 ;
        RECT 0.10 0.20 0.30 0.40 ;
      VIA ( 0.50 0.60 ) V12 ;
    END
    PORT
      LAYER M2 ;
        PATH ( 0.20 0.20 ) ( 0.20 0.80 ) ;
        PATH ( 0.40 0.40 ) ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT TRISTATE ;
    USE CLOCK ;
    PORT
      LAYER M2 ;
        RECT 0.70 0.80 1.00 1.20 ;
    END
  END Y
  OBS
    LAYER M1 EXCEPTPGNET SPACING 0.05 ;
      RECT 0.00 0.00 0.20 0.20 ;
    LAYER OVERLAP ;
      RECT 0.00 0.00 1.20 2.40 ;
    VIA ( 0.90 1.00 ) V12 ;
  END
  DENSITY
    LAYER M1 ; RECT 0.00 0.00 1.20 2.40 0.50 ;
  END
END FILL_X1
MACRO LARGE_ORIGIN
  CLASS BLOCK ;
  ORIGIN 3000000 -3000000 ;
  SIZE 1.0 BY 1.0 ;
END LARGE_ORIGIN
END LIBRARY
)LEF");

  TechStore technology;
  LefTechImporter tech_importer(technology);
  ASSERT_NO_THROW(tech_importer.import(technology_lef));
  LibraryStore library(technology.techRegistry());
  LefLibraryImporter library_importer(technology, library);
  const std::array files{site_lef, macro_lef};
  ASSERT_NO_THROW(library_importer.import(std::span<const std::filesystem::path>(files)));

  const auto site = library.siteStorage().findSite("CORE_SITE");
  ASSERT_TRUE(site);
  const auto& stored_site = library.siteStorage().site(site);
  EXPECT_EQ(stored_site.width, 200);
  EXPECT_EQ(stored_site.height, 400);
  EXPECT_EQ(stored_site.site_class, LibrarySiteClass::kCore);
  EXPECT_TRUE(stored_site.symmetry_x);
  EXPECT_TRUE(stored_site.symmetry_y);
  EXPECT_TRUE(stored_site.symmetry_r90);
  const auto virtual_site = library.siteStorage().findSite("VIRTUAL_SITE");
  ASSERT_TRUE(virtual_site);
  EXPECT_EQ(library.siteStorage().site(virtual_site).site_class, LibrarySiteClass::kVirtual);

  const auto master = library.cellMasterStorage().findCellMaster("FILL_X1");
  ASSERT_TRUE(master);
  const auto& stored_master = library.cellMasterStorage().cellMaster(master);
  EXPECT_EQ(stored_master.type, LibraryCellMasterType::kCoreSpacer);
  EXPECT_TRUE(stored_master.core_filler);
  EXPECT_EQ(stored_master.site, site);
  EXPECT_EQ(stored_master.origin_x, -100);
  EXPECT_EQ(stored_master.origin_y, 200);
  EXPECT_EQ(stored_master.width, 1200u);
  EXPECT_EQ(stored_master.height, 2400u);

  const auto terms = library.masterTermStorage().masterTerms(master);
  ASSERT_EQ(terms.size(), 2u);
  const auto a = library.masterTermStorage().findMasterTerm(master, "A");
  ASSERT_TRUE(a);
  EXPECT_EQ(library.masterTermStorage().masterTerm(a).direction, LibraryMasterTermDirection::kInput);
  EXPECT_EQ(library.masterTermStorage().masterTerm(a).use, LibraryMasterTermUse::kSignal);
  EXPECT_EQ(library.masterTermStorage().masterTerm(a).shape, LibraryMasterTermShape::kAbutment);
  const auto ports = library.masterPortStorage().masterPorts(a);
  ASSERT_EQ(ports.size(), 2u);
  const auto& first_port = library.masterPortStorage().masterPort(ports[0]);
  EXPECT_EQ(first_port.port_class, LibraryMasterPortClass::kCore);
  ASSERT_EQ(first_port.layer_clauses.size(), 1u);
  EXPECT_EQ(first_port.layer_clauses.front().layer, technology.findLayer("M1"));
  const auto first_port_rects = library.geometryPool().rectangles(first_port.layer_clauses.front().geometry);
  ASSERT_EQ(first_port_rects.size(), 1u);
  EXPECT_EQ(first_port_rects.front(), (Rect{100, 200, 300, 400}));
  ASSERT_EQ(first_port.vias.size(), 1u);
  EXPECT_EQ(first_port.vias.front().via, technology.viaMasterStorage().findViaMaster("V12"));
  EXPECT_EQ(first_port.vias.front().origin, (Point{500, 600}));
  const auto& path_port = library.masterPortStorage().masterPort(ports[1]);
  ASSERT_EQ(path_port.layer_clauses.size(), 1u);
  const auto path_rects = library.geometryPool().rectangles(path_port.layer_clauses.front().geometry);
  ASSERT_EQ(path_rects.size(), 2u);
  // OpenDB uses dbdist(width) >> 1 for both sides of a PATH box.
  EXPECT_EQ(path_rects.front(), (Rect{150, 150, 250, 850}));
  EXPECT_EQ(path_rects.back(), (Rect{350, 350, 450, 450}));

  ASSERT_TRUE(library.cellMasterStorage().hasObs(master));
  const auto& obs = library.cellMasterStorage().obs(master);
  ASSERT_EQ(obs.layer_clauses.size(), 2u);
  EXPECT_EQ(obs.layer_clauses[0].layer, technology.findLayer("M1"));
  EXPECT_NE(obs.layer_clauses[0].flags & LibraryObsLayerFlag::kExceptPgNet, 0u);
  EXPECT_NE(obs.layer_clauses[0].flags & LibraryObsLayerFlag::kHasSpacing, 0u);
  EXPECT_EQ(obs.layer_clauses[0].spacing, 50);
  EXPECT_EQ(obs.layer_clauses[1].layer, technology.findLayer("OVERLAP"));
  ASSERT_EQ(obs.vias.size(), 1u);
  EXPECT_EQ(obs.vias.front().origin, (Point{900, 1000}));
  const auto large_origin = library.cellMasterStorage().findCellMaster("LARGE_ORIGIN");
  ASSERT_TRUE(large_origin);
  EXPECT_EQ(library.cellMasterStorage().cellMaster(large_origin).origin_x, 3'000'000'000LL);
  EXPECT_EQ(library.cellMasterStorage().cellMaster(large_origin).origin_y, -3'000'000'000LL);
  EXPECT_EQ(diagnosticCount(library_importer, "MACRO DENSITY"), 1u);
}

TEST_F(LefLibraryImporterTest, RejectsUnknownReferencesWithoutPartialEntities)
{
  const auto technology_lef = writeLef("rollback-tech", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.20 ; WIDTH 0.10 ; END M1
END LIBRARY
)LEF");
  const auto bad_library_lef = writeLef("rollback-library", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
SITE CORE_SITE CLASS CORE ; SIZE 0.20 BY 0.40 ; END CORE_SITE
MACRO BROKEN
  CLASS CORE ; SIZE 1.0 BY 1.0 ; SITE CORE_SITE ;
  PIN A
    DIRECTION INPUT ; USE SIGNAL ;
    PORT LAYER UNKNOWN ; RECT 0 0 1 1 ; END
  END A
END BROKEN
END LIBRARY
)LEF");

  TechStore technology;
  LefTechImporter tech_importer(technology);
  ASSERT_NO_THROW(tech_importer.import(technology_lef));
  LibraryStore library(technology.techRegistry());
  LefLibraryImporter importer(technology, library);
  EXPECT_THROW(importer.import(bad_library_lef), std::runtime_error);
  EXPECT_EQ(library.siteStorage().siteCount(), 0u);
  EXPECT_EQ(library.cellMasterStorage().cellMasterCount(), 0u);
  EXPECT_EQ(library.masterTermStorage().masterTermCount(), 0u);
  EXPECT_EQ(library.masterPortStorage().masterPortCount(), 0u);
  EXPECT_EQ(library.geometryPool().rectangleCount(), 0u);
  EXPECT_EQ(library.geometryPool().polygonCount(), 0u);
  EXPECT_EQ(library.geometryPool().pointCount(), 0u);
}

TEST_F(LefLibraryImporterTest, StoresPolygonsNativelyAndRectangularizesThemWhenConfigured)
{
  const auto technology_lef = writeLef("polygon-tech", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.20 ; WIDTH 0.10 ; END M1
END LIBRARY
)LEF");
  const auto polygon_lef = writeLef("polygon-library", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
MACRO POLYGON_CELL
  CLASS CORE ; SIZE 1.0 BY 1.0 ;
  PIN A
    DIRECTION INPUT ; USE SIGNAL ;
    PORT
      LAYER M1 ;
      POLYGON ( 0 0 ) ( 1 0 ) ( 1 0.25 ) ( 0.25 0.25 ) ( 0.25 1 ) ( 0 1 ) ;
    END
  END A
  OBS
    LAYER M1 ;
    POLYGON ( 0 0 ) ( 1 0 ) ( 1 0.25 ) ( 0.25 0.25 ) ( 0.25 1 ) ( 0 1 ) ;
  END
END POLYGON_CELL
END LIBRARY
)LEF");

  TechStore technology;
  LefTechImporter tech_importer(technology);
  ASSERT_NO_THROW(tech_importer.import(technology_lef));
  LibraryStore library(technology.techRegistry());
  LefLibraryImporter importer(technology, library);
  ASSERT_NO_THROW(importer.import(polygon_lef));

  const auto master = library.cellMasterStorage().findCellMaster("POLYGON_CELL");
  ASSERT_TRUE(master);
  const auto term = library.masterTermStorage().findMasterTerm(master, "A");
  ASSERT_TRUE(term);
  const auto ports = library.masterPortStorage().masterPorts(term);
  ASSERT_EQ(ports.size(), 1u);
  const auto& port = library.masterPortStorage().masterPort(ports.front());
  ASSERT_EQ(port.layer_clauses.size(), 1u);
  ASSERT_EQ(library.geometryPool().polygonCount(port.layer_clauses.front().geometry), 1u);
  const auto port_points = library.geometryPool().polygonPoints(port.layer_clauses.front().geometry, 0);
  ASSERT_EQ(port_points.size(), 6u);
  EXPECT_EQ(port_points[0], (Point{0, 0}));
  EXPECT_EQ(port_points[1], (Point{1000, 0}));
  EXPECT_EQ(port_points[2], (Point{1000, 250}));
  EXPECT_EQ(port_points[3], (Point{250, 250}));
  EXPECT_EQ(port_points[4], (Point{250, 1000}));
  EXPECT_EQ(port_points[5], (Point{0, 1000}));

  const auto& obs = library.cellMasterStorage().obs(master);
  ASSERT_EQ(obs.layer_clauses.size(), 1u);
  ASSERT_EQ(library.geometryPool().polygonCount(obs.layer_clauses.front().geometry), 1u);
  EXPECT_EQ(library.geometryPool().polygonPoints(obs.layer_clauses.front().geometry, 0).size(), 6u);
  EXPECT_EQ(library.geometryPool().polygonCount(), 2u);
  EXPECT_EQ(library.geometryPool().pointCount(), 12u);

  const LibraryStoreOptions options{.geometry = GeometryPoolOptions{.polygon_mode = PolygonStorageMode::kRectangularized}};
  LibraryStore rectangularized_library(technology.techRegistry(), options);
  LefLibraryImporter rectangularized_importer(technology, rectangularized_library);
  ASSERT_NO_THROW(rectangularized_importer.import(polygon_lef));

  const auto rectangularized_master = rectangularized_library.cellMasterStorage().findCellMaster("POLYGON_CELL");
  ASSERT_TRUE(rectangularized_master);
  const auto rectangularized_term = rectangularized_library.masterTermStorage().findMasterTerm(rectangularized_master, "A");
  ASSERT_TRUE(rectangularized_term);
  const auto rectangularized_ports = rectangularized_library.masterPortStorage().masterPorts(rectangularized_term);
  ASSERT_EQ(rectangularized_ports.size(), 1u);
  const auto& rectangularized_port = rectangularized_library.masterPortStorage().masterPort(rectangularized_ports.front());
  ASSERT_EQ(rectangularized_port.layer_clauses.size(), 1u);

  const std::array expected_rectangles{Rect{.ll_x = 0, .ll_y = 0, .ur_x = 250, .ur_y = 1000},
                                       Rect{.ll_x = 250, .ll_y = 0, .ur_x = 1000, .ur_y = 250}};
  const auto port_rectangles = rectangularized_library.geometryPool().rectangles(rectangularized_port.layer_clauses.front().geometry);
  ASSERT_EQ(port_rectangles.size(), expected_rectangles.size());
  EXPECT_EQ(port_rectangles[0], expected_rectangles[0]);
  EXPECT_EQ(port_rectangles[1], expected_rectangles[1]);
  EXPECT_EQ(rectangularized_library.geometryPool().polygonCount(rectangularized_port.layer_clauses.front().geometry), 0u);

  const auto& rectangularized_obs = rectangularized_library.cellMasterStorage().obs(rectangularized_master);
  ASSERT_EQ(rectangularized_obs.layer_clauses.size(), 1u);
  const auto obs_rectangles = rectangularized_library.geometryPool().rectangles(rectangularized_obs.layer_clauses.front().geometry);
  ASSERT_EQ(obs_rectangles.size(), expected_rectangles.size());
  EXPECT_EQ(obs_rectangles[0], expected_rectangles[0]);
  EXPECT_EQ(obs_rectangles[1], expected_rectangles[1]);
  EXPECT_EQ(rectangularized_library.geometryPool().polygonCount(rectangularized_obs.layer_clauses.front().geometry), 0u);
  EXPECT_EQ(rectangularized_library.geometryPool().rectangleCount(), 4u);
  EXPECT_EQ(rectangularized_library.geometryPool().polygonCount(), 0u);
  EXPECT_EQ(rectangularized_library.geometryPool().pointCount(), 0u);
}

TEST_F(LefLibraryImporterTest, NormalizesCompatibleDuplicateMacroPins)
{
  const auto technology_lef = writeLef("duplicate-pin-tech", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.20 ; WIDTH 0.10 ; END M1
END LIBRARY
)LEF");
  const auto library_lef = writeLef("duplicate-pin-library", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
MACRO DUPLICATE_PIN
  CLASS PAD ;
  SIZE 1.0 BY 1.0 ;
  PIN VSSIO
    DIRECTION INOUT ;
    USE GROUND ;
    PORT
      LAYER M1 ;
      RECT 0.00 0.00 0.10 0.10 ;
    END
  END VSSIO
  PIN VSSIO
    DIRECTION INOUT ;
    USE GROUND ;
    PORT
      LAYER M1 ;
      RECT 0.90 0.90 1.00 1.00 ;
    END
  END VSSIO
END DUPLICATE_PIN
END LIBRARY
)LEF");

  TechStore technology;
  LefTechImporter tech_importer(technology);
  ASSERT_NO_THROW(tech_importer.import(technology_lef));
  LibraryStore library(technology.techRegistry());
  LefLibraryImporter importer(technology, library);
  ASSERT_NO_THROW(importer.import(library_lef));

  const auto master = library.cellMasterStorage().findCellMaster("DUPLICATE_PIN");
  ASSERT_TRUE(master);
  const auto terms = library.masterTermStorage().masterTerms(master);
  ASSERT_EQ(terms.size(), 1u);
  const auto vssio = library.masterTermStorage().findMasterTerm(master, "VSSIO");
  ASSERT_TRUE(vssio);
  EXPECT_EQ(vssio, terms.front());
  EXPECT_EQ(library.masterPortStorage().masterPorts(vssio).size(), 2u);
  EXPECT_EQ(diagnosticCount(importer, "MACRO duplicate PIN normalized"), 1u);
}

TEST_F(LefLibraryImporterTest, NormalizesInvertedLibraryRectangleCorners)
{
  const auto technology_lef = writeLef("inverted-rect-tech", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.20 ; WIDTH 0.10 ; END M1
END LIBRARY
)LEF");
  const auto library_lef = writeLef("inverted-rect-library", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
MACRO INVERTED_RECT
  CLASS BLOCK ;
  SIZE 1.0 BY 1.0 ;
  PIN A
    DIRECTION INPUT ;
    PORT
      LAYER M1 ;
      RECT 0.80 0.70 0.20 0.10 ;
    END
  END A
  OBS
    LAYER M1 ;
    RECT 0.90 0.60 0.30 0.40 ;
  END
END INVERTED_RECT
END LIBRARY
)LEF");

  TechStore technology;
  LefTechImporter tech_importer(technology);
  ASSERT_NO_THROW(tech_importer.import(technology_lef));
  LibraryStore library(technology.techRegistry());
  LefLibraryImporter importer(technology, library);
  ASSERT_NO_THROW(importer.import(library_lef));

  const auto master = library.cellMasterStorage().findCellMaster("INVERTED_RECT");
  ASSERT_TRUE(master);
  const auto term = library.masterTermStorage().findMasterTerm(master, "A");
  ASSERT_TRUE(term);
  const auto ports = library.masterPortStorage().masterPorts(term);
  ASSERT_EQ(ports.size(), 1u);
  const auto& port = library.masterPortStorage().masterPort(ports.front());
  EXPECT_EQ(library.geometryPool().rectangles(port.layer_clauses.front().geometry).front(), (Rect{200, 100, 800, 700}));
  const auto& obs = library.cellMasterStorage().obs(master);
  EXPECT_EQ(library.geometryPool().rectangles(obs.layer_clauses.front().geometry).front(), (Rect{300, 400, 900, 600}));
}

TEST_F(LefLibraryImporterTest, DirectlyImportsSky130CellLibrary)
{
  const auto technology_lef = sourcePath("scripts/foundry/sky130/lef/sky130_fd_sc_hd.tlef");
  const auto cells_lef = sourcePath("scripts/foundry/sky130/lef/sky130_fd_sc_hd_merged.lef");
  ASSERT_TRUE(std::filesystem::exists(technology_lef));
  ASSERT_TRUE(std::filesystem::exists(cells_lef));

  TechStore technology;
  LefTechImporter tech_importer(technology);
  ASSERT_NO_THROW(tech_importer.import(technology_lef));
  LibraryStore library(technology.techRegistry());
  LefLibraryImporter library_importer(technology, library);
  const std::array files{technology_lef, cells_lef};
  ASSERT_NO_THROW(library_importer.import(std::span<const std::filesystem::path>(files)));

  EXPECT_EQ(library.siteStorage().siteCount(), 2u);
  EXPECT_EQ(library.cellMasterStorage().cellMasterCount(), 437u);
  EXPECT_EQ(library.masterTermStorage().masterTermCount(), 2662u);
  EXPECT_EQ(library.masterPortStorage().masterPortCount(), 3775u);
  const auto master = library.cellMasterStorage().findCellMaster("sky130_fd_sc_hd__a2111o_1");
  ASSERT_TRUE(master);
  EXPECT_EQ(library.cellMasterStorage().cellMaster(master).site, library.siteStorage().findSite("unithd"));
  EXPECT_TRUE(library.cellMasterStorage().hasObs(master));
  EXPECT_GT(diagnosticCount(library_importer, "PIN ANTENNAMODEL"), 0u);
  EXPECT_GT(diagnosticCount(library_importer, "PIN ANTENNADIFFAREA"), 0u);
}

TEST_F(LefLibraryImporterTest, DirectlyImportsIhpCellLibrary)
{
  const auto technology_lef = sourcePath("scripts/foundry/ihp130/ihp-sg13g2/libs.ref/sg13g2_stdcell/lef/sg13g2_tech.lef");
  const auto cells_lef = sourcePath("scripts/foundry/ihp130/ihp-sg13g2/libs.ref/sg13g2_stdcell/lef/sg13g2_stdcell.lef");
  ASSERT_TRUE(std::filesystem::exists(technology_lef));
  ASSERT_TRUE(std::filesystem::exists(cells_lef));

  TechStore technology;
  LefTechImporter tech_importer(technology);
  ASSERT_NO_THROW(tech_importer.import(technology_lef));
  LibraryStore library(technology.techRegistry());
  LefLibraryImporter library_importer(technology, library);
  const std::array files{technology_lef, cells_lef};
  ASSERT_NO_THROW(library_importer.import(std::span<const std::filesystem::path>(files)));

  EXPECT_EQ(library.siteStorage().siteCount(), 1u);
  EXPECT_EQ(library.cellMasterStorage().cellMasterCount(), 78u);
  EXPECT_EQ(library.masterTermStorage().masterTermCount(), 405u);
  EXPECT_EQ(library.masterPortStorage().masterPortCount(), 405u);
  const auto master = library.cellMasterStorage().findCellMaster("sg13g2_a21o_1");
  ASSERT_TRUE(master);
  EXPECT_EQ(library.cellMasterStorage().cellMaster(master).site, library.siteStorage().findSite("CoreSite"));
  EXPECT_TRUE(library.cellMasterStorage().hasObs(master));
  EXPECT_GT(diagnosticCount(library_importer, "MACRO FOREIGN"), 0u);
  EXPECT_GT(diagnosticCount(library_importer, "MACRO PROPERTY"), 0u);
  EXPECT_GT(diagnosticCount(library_importer, "PIN ANTENNAMODEL"), 0u);
  EXPECT_GT(diagnosticCount(library_importer, "PIN NETEXPR"), 0u);
}

}  // namespace
}  // namespace eccdb
