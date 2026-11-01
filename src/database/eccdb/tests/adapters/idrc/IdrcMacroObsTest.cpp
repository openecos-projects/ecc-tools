// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory

#include <gtest/gtest.h>

#include <algorithm>
#include <any>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <unistd.h>

#include "DRCDesignSource.hpp"
#include "DRCInterface.hpp"
#include "DataManager.hpp"
#include "Database.hpp"
#include "Logger.hpp"
#include "MacroObs.hpp"
#include "PlanarRect.hpp"
#include "adapters/idrc/DRCInterfaceEccdb.h"
#include "def/DefDesignImporter.h"
#include "idm.h"
#include "IdrcWrappedDatabaseCompare.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"

namespace idrc {
namespace {

constexpr std::string_view kMacroObsLef = R"LEF(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;

LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.20 ;
  WIDTH 0.10 ;
  MINWIDTH 0.10 ;
  AREA 0.01 ;
  SPACING 0.10 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.10 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 0.20 ;
  WIDTH 0.10 ;
  MINWIDTH 0.10 ;
  AREA 0.01 ;
  SPACING 0.10 ;
END M2

SITE CORE
  CLASS CORE ;
  SYMMETRY X Y ;
  SIZE 0.20 BY 0.40 ;
END CORE

MACRO BLOCK_OBS
  CLASS BLOCK ;
  ORIGIN 0 0 ;
  SIZE 2.00 BY 1.00 ;
  SYMMETRY X Y ;
  PIN ACCESS
    DIRECTION INPUT ;
    PORT
      LAYER M1 ;
        RECT 0.40 0.20 0.60 0.40 ;
    END
  END ACCESS
  OBS
    LAYER M1 SPACING 0.20 ;
      RECT 0.60 0.20 1.00 0.60 ;
    LAYER V1 SPACING 0.10 ;
      RECT 1.20 0.20 1.30 0.30 ;
  END
END BLOCK_OBS
END LIBRARY
)LEF";

// A strict two-backend control fixture. Keeping the routing-only case
// separate prevents the legacy routing/cut layer-index collision below from
// hiding the intended spacing and transform coverage.
constexpr std::string_view kRoutingMacroObsLef = R"LEF(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;

LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.20 ;
  WIDTH 0.10 ;
  MINWIDTH 0.10 ;
  AREA 0.01 ;
  SPACING 0.10 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.10 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 0.20 ;
  WIDTH 0.10 ;
  MINWIDTH 0.10 ;
  AREA 0.01 ;
  SPACING 0.10 ;
END M2

SITE CORE
  CLASS CORE ;
  SYMMETRY X Y ;
  SIZE 0.20 BY 0.40 ;
END CORE

MACRO BLOCK_OBS
  CLASS BLOCK ;
  ORIGIN 0 0 ;
  SIZE 2.00 BY 1.00 ;
  SYMMETRY X Y ;
  PIN ACCESS
    DIRECTION INPUT ;
    PORT
      LAYER M1 ;
        RECT 0.40 0.20 0.60 0.40 ;
    END
  END ACCESS
  OBS
    LAYER M1 SPACING 0.20 ;
      RECT 0.60 0.20 1.00 0.60 ;
  END
END BLOCK_OBS
END LIBRARY
)LEF";

constexpr std::string_view kMacroObsDef = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN idrc_macro_obs_fixture ;
UNITS DISTANCE MICRONS 1000 ;

DIEAREA ( 0 0 ) ( 8000 5000 ) ;
ROW ROW0 CORE 0 0 N DO 40 BY 1 STEP 200 0 ;
TRACKS Y 100 DO 25 STEP 200 LAYER M1 ;
TRACKS X 100 DO 40 STEP 200 LAYER M2 ;

COMPONENTS 2 ;
- macro_n BLOCK_OBS + PLACED ( 1000 1000 ) N ;
- macro_fs BLOCK_OBS + FIXED ( 4000 2000 ) FS ;
END COMPONENTS

PINS 0 ;
END PINS
NETS 1 ;
- signal
  ( macro_n ACCESS )
  ( macro_fs ACCESS )
  + ROUTED M1 ( 100 100 ) ( 300 100 )
  ;
END NETS
END DESIGN
)DEF";

constexpr std::string_view kObliqueWireDef = R"DEF(VERSION 5.8 ;
DESIGN idrc_oblique_wire_fixture ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 1000 1000 ) ;
TRACKS Y 100 DO 5 STEP 200 LAYER M1 ;
TRACKS X 100 DO 5 STEP 200 LAYER M2 ;
NETS 1 ;
- diagonal
  + ROUTED M1 ( 100 100 ) ( 300 300 )
  ;
END NETS
END DESIGN
)DEF";

class TemporaryWorkspace
{
 public:
  TemporaryWorkspace()
  {
    _directory = std::filesystem::path(testing::TempDir()) / ("idrc-macro-obs-" + std::to_string(::getpid()));
    std::filesystem::remove_all(_directory);
    std::filesystem::create_directories(_directory);
  }

  ~TemporaryWorkspace()
  {
    std::error_code error;
    std::filesystem::remove_all(_directory, error);
  }

  [[nodiscard]] std::filesystem::path path(std::string_view name) const { return _directory / name; }

 private:
  std::filesystem::path _directory;
};

class ScopedEnvironmentValue
{
 public:
  explicit ScopedEnvironmentValue(std::string name) : _name(std::move(name))
  {
    if (const char* value = std::getenv(_name.c_str()); value != nullptr) {
      _previous = value;
    }
  }

  ~ScopedEnvironmentValue()
  {
    if (_previous.has_value()) {
      static_cast<void>(setenv(_name.c_str(), _previous->c_str(), 1));
    } else {
      static_cast<void>(unsetenv(_name.c_str()));
    }
  }

  ScopedEnvironmentValue(const ScopedEnvironmentValue&) = delete;
  ScopedEnvironmentValue& operator=(const ScopedEnvironmentValue&) = delete;

 private:
  std::string _name;
  std::optional<std::string> _previous;
};

void writeText(const std::filesystem::path& path, std::string_view contents)
{
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("cannot write file: " + path.string());
  }
  output << contents;
  if (!output) {
    throw std::runtime_error("cannot flush file: " + path.string());
  }
}

void loadIdb(const std::filesystem::path& lef, const std::filesystem::path& def)
{
  dmInst->reset();
  if (!dmInst->readLef(std::vector<std::string>{lef.string()}, true)) {
    throw std::runtime_error("readLef failed: " + lef.string());
  }
  if (!dmInst->readDef(def.string())) {
    throw std::runtime_error("readDef failed: " + def.string());
  }
}

struct ImportedCase
{
  TemporaryWorkspace workspace;
  std::filesystem::path lef = workspace.path("macro-obs.lef");
  std::filesystem::path def = workspace.path("macro-obs.def");
  eccdb::TechStore technology;
  std::unique_ptr<eccdb::LibraryStore> library;
  std::unique_ptr<eccdb::DesignStore> design;

  explicit ImportedCase(std::string_view lef_contents = kMacroObsLef, std::string_view def_contents = kMacroObsDef)
  {
    writeText(lef, lef_contents);
    writeText(def, def_contents);
    static_cast<void>(LefDefParser::lefrClear());
    static_cast<void>(LefDefParser::defrClear());
    eccdb::LefTechImporter(technology).import(lef);
    library = std::make_unique<eccdb::LibraryStore>(technology.techRegistry());
    eccdb::LefLibraryImporter(technology, *library).import(lef);
    design = std::make_unique<eccdb::DesignStore>(technology.techRegistry(), library->libraryRegistry());
    eccdb::DefDesignImporter(*design).import(def);
    loadIdb(lef, def);
  }

  ~ImportedCase()
  {
    DRCI.abortDRC();
    DRCI.setDesignSource(nullptr);
    dmInst->reset();
  }

  [[nodiscard]] std::shared_ptr<DRCDesignSource> nativeSource()
  {
    return eccdb::adapters::makeDrcSource(*design, technology, *library);
  }
};

struct ActiveDrcRun
{
  explicit ActiveDrcRun(std::shared_ptr<DRCDesignSource> source, const std::filesystem::path& temp_directory)
  {
    DRCI.setDesignSource(std::move(source));
    std::map<std::string, std::any> config;
    config.emplace("-temp_directory_path", temp_directory.string());
    config.emplace("-thread_number", 1);
    DRCI.initDRC(config, true);
  }

  ~ActiveDrcRun()
  {
    DRCI.abortDRC();
    DRCI.setDesignSource(nullptr);
  }

  ActiveDrcRun(const ActiveDrcRun&) = delete;
  ActiveDrcRun& operator=(const ActiveDrcRun&) = delete;
};

Database captureWrappedDatabase(std::shared_ptr<DRCDesignSource> source, const std::filesystem::path& temp_directory)
{
  ActiveDrcRun run(std::move(source), temp_directory);
  return std::move(DRCDM.getDatabase());
}

ids::Shape shape(int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y, int32_t layer_idx, bool is_routing)
{
  return ids::Shape{.net_idx = 0,
                    .ll_x = ll_x,
                    .ll_y = ll_y,
                    .ur_x = ur_x,
                    .ur_y = ur_y,
                    .layer_idx = layer_idx,
                    .is_routing = is_routing,
                    .source_type = ids::Shape::SourceType::kRegularWire};
}

std::vector<ids::Violation> macroViolations(std::shared_ptr<DRCDesignSource> source, const std::filesystem::path& temp_directory,
                                            std::vector<ids::Shape> result)
{
  ActiveDrcRun run(std::move(source), temp_directory);
  const auto violations = DRCI.getViolationList({}, result, {"macro_obs_spacing"}, {});
  std::vector<ids::Violation> macro;
  for (const auto& violation : violations) {
    if (violation.violation_type == "macro_obs_spacing") {
      macro.push_back(violation);
    }
  }
  return macro;
}

const MacroObsLayer* findLayer(Database& database, const PlanarRect& expected, bool is_routing)
{
  for (const auto& macro : database.get_macro_obs_list()) {
    for (const auto& layer : macro.obs_layer_list) {
      if (layer.is_routing == is_routing && layer.bounding_box == expected) {
        return &layer;
      }
    }
  }
  return nullptr;
}

void expectLayer(Database& database, const PlanarRect& bbox, int32_t layer_idx, bool is_routing, int32_t spacing,
                 std::vector<PlanarRect> expected_obs, std::vector<PlanarRect> expected_pins)
{
  const auto* layer = findLayer(database, bbox, is_routing);
  ASSERT_NE(layer, nullptr) << "missing macro OBS layer bbox=(" << bbox.get_ll_x() << ',' << bbox.get_ll_y() << ',' << bbox.get_ur_x()
                            << ',' << bbox.get_ur_y() << ") routing=" << is_routing;
  EXPECT_EQ(layer->layer_idx, layer_idx);
  EXPECT_EQ(layer->min_spacing, spacing);
  EXPECT_EQ(layer->obs_rect_list, expected_obs);
  EXPECT_EQ(layer->pin_rect_list, expected_pins);
}

void expectOneMacroViolation(const std::vector<ids::Violation>& violations, int32_t spacing, int32_t layer_idx)
{
  ASSERT_EQ(violations.size(), 1U);
  EXPECT_EQ(violations.front().required_size, spacing);
  EXPECT_EQ(violations.front().layer_idx, layer_idx);
}

TEST(MacroObs, ProjectsRoutingSpacingPinExemptionsAndTransformsForBothSources)
{
  ImportedCase fixture{kRoutingMacroObsLef};

  Database idb = captureWrappedDatabase(nullptr, fixture.workspace.path("idb-wrap"));
  Database native = captureWrappedDatabase(fixture.nativeSource(), fixture.workspace.path("native-wrap"));

  ASSERT_EQ(idb.get_macro_obs_list().size(), 2U) << "legacy iDB wrapper must materialize both BLOCK instances";
  ASSERT_EQ(native.get_macro_obs_list().size(), 2U) << "native adaptor must materialize both BLOCK instances";
  const std::string wrapped_mismatch = eccdb::test::compareIdrcWrappedDatabase(idb, native);
  EXPECT_TRUE(wrapped_mismatch.empty()) << wrapped_mismatch;

  const PlanarRect n_m1_obs{1600, 1200, 2000, 1600};
  const PlanarRect fs_m1_obs{4600, 2400, 5000, 2800};
  expectLayer(idb, n_m1_obs, 0, true, 200, {n_m1_obs}, {{1400, 1200, 1600, 1400}});
  expectLayer(idb, fs_m1_obs, 0, true, 200, {fs_m1_obs}, {{4400, 2600, 4600, 2800}});
  expectLayer(native, n_m1_obs, 0, true, 200, {n_m1_obs}, {{1400, 1200, 1600, 1400}});
  expectLayer(native, fs_m1_obs, 0, true, 200, {fs_m1_obs}, {{4400, 2600, 4600, 2800}});

  // N instance: near the OBS but outside its ACCESS port is a violation.
  const auto near_n = shape(1450, 1450, 1550, 1550, 0, true);
  // The right edge is exactly the requested 200 DBU away, which is legal.
  const auto at_spacing_n = shape(1200, 1450, 1400, 1550, 0, true);
  // This is only 50 DBU from the OBS but overlaps the macro ACCESS pin.
  const auto on_pin_n = shape(1450, 1250, 1550, 1350, 0, true);
  // FS instance proves the physical transform is not N-only.
  const auto near_fs = shape(5050, 2450, 5150, 2550, 0, true);
  const auto on_pin_fs = shape(4450, 2650, 4550, 2750, 0, true);
  for (const auto use_native : {false, true}) {
    const auto source = use_native ? fixture.nativeSource() : std::shared_ptr<DRCDesignSource>{};
    const auto name = use_native ? "native" : "idb";
    expectOneMacroViolation(macroViolations(source, fixture.workspace.path(std::string(name) + "-near-n"), {near_n}), 200, 0);
    EXPECT_TRUE(macroViolations(source, fixture.workspace.path(std::string(name) + "-at-spacing-n"), {at_spacing_n}).empty());
    EXPECT_TRUE(macroViolations(source, fixture.workspace.path(std::string(name) + "-pin-n"), {on_pin_n}).empty());
    expectOneMacroViolation(macroViolations(source, fixture.workspace.path(std::string(name) + "-near-fs"), {near_fs}), 200, 0);
    EXPECT_TRUE(macroViolations(source, fixture.workspace.path(std::string(name) + "-pin-fs"), {on_pin_fs}).empty());
  }

}

TEST(MacroObs, DiagnosesLegacyCutLayerPinIndexCollision)
{
  ImportedCase fixture;
  Database idb = captureWrappedDatabase(nullptr, fixture.workspace.path("collision-idb"));
  Database native = captureWrappedDatabase(fixture.nativeSource(), fixture.workspace.path("collision-native"));

  const PlanarRect n_v1_obs{2200, 1200, 2300, 1300};
  const std::vector<PlanarRect> n_m1_pin{{1400, 1200, 1600, 1400}};
  // M1 and V1 both use local index zero. The legacy iDB helper keys only by
  // that integer, so it leaks M1's pin rectangle into V1. EccDB keys by
  // (index, is_routing), which is the actual iDRC layer identity.
  expectLayer(idb, n_v1_obs, 0, false, 100, {n_v1_obs}, n_m1_pin);
  expectLayer(native, n_v1_obs, 0, false, 100, {n_v1_obs}, {});
  const std::string mismatch = eccdb::test::compareIdrcWrappedDatabase(idb, native);
  EXPECT_FALSE(mismatch.empty());
  EXPECT_NE(mismatch.find("macro_obs"), std::string::npos) << mismatch;
}

TEST(MacroObsComparer, CoversContentsAndIgnoresContainerOrdering)
{
  ImportedCase fixture{kRoutingMacroObsLef};
  Database native = captureWrappedDatabase(fixture.nativeSource(), fixture.workspace.path("comparer-native"));
  ASSERT_EQ(native.get_macro_obs_list().size(), 2U);
  ASSERT_FALSE(native.get_macro_obs_list().front().obs_layer_list.empty());
  ASSERT_FALSE(native.get_macro_obs_list().front().obs_layer_list.front().pin_rect_list.empty());

  Database reordered = native;
  for (auto& macro : reordered.get_macro_obs_list()) {
    std::reverse(macro.obs_layer_list.begin(), macro.obs_layer_list.end());
  }
  std::reverse(reordered.get_macro_obs_list().begin(), reordered.get_macro_obs_list().end());
  EXPECT_TRUE(eccdb::test::compareIdrcWrappedDatabase(native, reordered).empty());

  Database missing = native;
  missing.get_macro_obs_list().clear();
  EXPECT_FALSE(eccdb::test::compareIdrcWrappedDatabase(native, missing).empty());

  Database changed_spacing = native;
  ++changed_spacing.get_macro_obs_list().front().obs_layer_list.front().min_spacing;
  EXPECT_FALSE(eccdb::test::compareIdrcWrappedDatabase(native, changed_spacing).empty());

  Database missing_pin = native;
  missing_pin.get_macro_obs_list().front().obs_layer_list.front().pin_rect_list.clear();
  EXPECT_FALSE(eccdb::test::compareIdrcWrappedDatabase(native, missing_pin).empty());
}

TEST(WireProjection, RejectsObliqueLegacyDefSegments)
{
  TemporaryWorkspace workspace;
  const auto lef = workspace.path("oblique-wire.lef");
  const auto def = workspace.path("oblique-wire.def");
  writeText(lef, kMacroObsLef);
  writeText(def, kObliqueWireDef);
  loadIdb(lef, def);

  ScopedEnvironmentValue logger_error_mode{"ECC_LOGGER_THROW_ON_ERROR"};
  ASSERT_EQ(setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1), 0);
  {
    ActiveDrcRun run(std::shared_ptr<DRCDesignSource>{}, workspace.path("oblique-run"));
    bool threw_oblique = false;
    try {
      DRCI.runDRC();
    } catch (const std::runtime_error& error) {
      threw_oblique = true;
      EXPECT_NE(std::string(error.what()).find("oblique"), std::string::npos) << error.what();
    }
    EXPECT_TRUE(threw_oblique) << "legacy oblique DEF segments must remain rejected";
  }
  dmInst->reset();
}

}  // namespace
}  // namespace idrc
