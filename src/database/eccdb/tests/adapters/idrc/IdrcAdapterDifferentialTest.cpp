// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "DRCInterface.hpp"
#include "DRCDesignSource.hpp"
#include "IdrcWrappedDatabaseCompare.h"
#include "adapters/idrc/DRCInterfaceEccdb.h"
#include "DataManager.hpp"
#include "Database.hpp"
#include "Logger.hpp"
#include "context/DatabaseContext.h"
#include "design/DesignStore.h"
#include "design/geometry/DesignGeometryStorage.h"
#include "design/routing/component/RoutingComponents.h"
#include "idm.h"
#include "def/DefDesignImporter.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"

namespace idrc {
namespace {

std::filesystem::path envPath(const char* name)
{
  if (const char* value = std::getenv(name); value != nullptr && value[0] != '\0') {
    return value;
  }
  return {};
}

std::filesystem::path ispd18Source()
{
  return envPath("ECCDB_ISPD18_ROOT");
}

std::filesystem::path ispd19Source()
{
  return envPath("ECCDB_ISPD19_ROOT");
}

struct Ispd18Case
{
  std::string name;
};

constexpr std::string_view kShapeFixtureLef = R"LEF(VERSION 5.8 ;
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

VIA VIA12 DEFAULT
  LAYER M1 ;
    RECT -0.08 -0.08 0.08 0.08 ;
  LAYER V1 ;
    RECT -0.04 -0.04 0.04 0.04 ;
  LAYER M2 ;
    RECT -0.08 -0.08 0.08 0.08 ;
END VIA12

VIARULE GEN12 GENERATE
  LAYER M1 ;
    ENCLOSURE 0.03 0.04 ;
  LAYER V1 ;
    RECT -0.05 -0.06 0.05 0.06 ;
    SPACING 0.02 BY 0.03 ;
  LAYER M2 ;
    ENCLOSURE 0.05 0.06 ;
END GEN12

SITE CORE
  CLASS CORE ;
  SYMMETRY X Y ;
  SIZE 0.20 BY 0.40 ;
END CORE

MACRO OBS_CELL
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.20 BY 0.20 ;
  SYMMETRY X Y ;
  OBS
    LAYER M1 ;
      RECT 0 0 0.20 0.20 ;
  END
END OBS_CELL
END LIBRARY
)LEF";

constexpr std::string_view kShapeFixtureDef = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN idrc_shape_fixture ;
UNITS DISTANCE MICRONS 1000 ;

DIEAREA ( 0 0 ) ( 2000 2000 ) ;
ROW ROW0 CORE 0 0 N DO 10 BY 1 STEP 200 0 ;
TRACKS Y 100 DO 10 STEP 200 LAYER M1 ;
TRACKS X 100 DO 10 STEP 200 LAYER M2 ;

VIAS 1 ;
- GENERATED12
  + VIARULE GEN12
  + CUTSIZE 100 120
  + LAYERS M1 V1 M2
  + CUTSPACING 20 30
  + ENCLOSURE 30 40 50 60
  + ROWCOL 2 3
  ;
END VIAS

COMPONENTS 2 ;
- placed_obs OBS_CELL + PLACED ( 200 400 ) N ;
- unplaced_obs OBS_CELL + UNPLACED ;
END COMPONENTS
PINS 0 ;
END PINS
SPECIALNETS 1 ;
- VDD
  + USE POWER
  + ROUTED M1 0 + SHAPE STRIPE ( 1000 1000 ) GENERATED12
  ;
END SPECIALNETS
BLOCKAGES 1 ;
- LAYER M2
  RECT ( 1400 1400 ) ( 1600 1600 ) ;
END BLOCKAGES
NETS 2 ;
- z_sig
  + ROUTED M1 ( 100 100 ) ( 500 100 ) VIA12
  ;
- a_sig
  + ROUTED M1 ( 100 300 ) ( 500 300 )
  ;
END NETS
END DESIGN
)DEF";

// A boundary route is intentionally centered on y=0.  DEF describes the
// centerline, so its physical M1 geometry extends to y=-50 and must not be
// clipped by the iDRC input projection.  The VIA12 at the second endpoint
// also verifies that an overlapping negative-coordinate via is retained.
constexpr std::string_view kBoundaryWireFixtureDef = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN idrc_boundary_wire_fixture ;
UNITS DISTANCE MICRONS 1000 ;

DIEAREA ( 0 0 ) ( 1000 1000 ) ;
PINS 2 ;
- boundary_io
  + NET boundary
  + DIRECTION INPUT
  + USE SIGNAL
  + PORT
    + LAYER M1 ( -50 -50 ) ( 50 50 )
    + PLACED ( 250 50 ) N
  ;
- boundary_peer
  + NET boundary
  + DIRECTION OUTPUT
  + USE SIGNAL
  + PORT
    + LAYER M1 ( -25 -25 ) ( 25 25 )
    + PLACED ( 750 750 ) N
  ;
END PINS
NETS 1 ;
- boundary
  ( PIN boundary_io )
  ( PIN boundary_peer )
  + ROUTED M1 ( 250 0 ) ( 500 0 ) VIA12
  ;
END NETS
END DESIGN
)DEF";

constexpr std::string_view kFlushProjectionFixtureDef = R"DEF(VERSION 5.8 ;
DESIGN idrc_flush_projection_fixture ;
UNITS DISTANCE MICRONS 1000 ;

DIEAREA ( 0 0 ) ( 1000 1000 ) ;
NETS 3 ;
- boundary_plain
  + ROUTED M1 ( 250 0 ) ( 500 0 )
  ;
- boundary_reversed_flush
  + ROUTED M1 ( 500 100 0 ) ( 250 100 )
  ;
- boundary_reversed_vertical_flush
  + ROUTED M1 ( 100 500 0 ) ( 100 250 )
  ;
END NETS
END DESIGN
)DEF";

class TemporaryWorkspace
{
 public:
  TemporaryWorkspace()
  {
    _directory = std::filesystem::path(testing::TempDir()) / ("idrc-adapter-diff-" + std::to_string(getpid()));
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

std::pair<std::filesystem::path, std::filesystem::path> ispd18LefDef(const std::filesystem::path& root, const Ispd18Case& test_case)
{
  const auto lef = root / test_case.name / (test_case.name + ".input.lef");
  const auto def = root / test_case.name / (test_case.name + ".input.def");
  if (!std::filesystem::exists(lef) || !std::filesystem::exists(def)) {
    throw std::runtime_error("missing ISPD18 lef/def for " + test_case.name);
  }
  return {lef, def};
}

void loadIspd18IntoIdb(const std::filesystem::path& lef, const std::filesystem::path& def)
{
  dmInst->reset();
  if (!dmInst->readLef(std::vector<std::string>{lef.string()}, true)) {
    throw std::runtime_error("readLef failed: " + lef.string());
  }
  if (!dmInst->readDef(def.string())) {
    throw std::runtime_error("readDef failed: " + def.string());
  }
}

Database wrapCurrentSource()
{
  DataManager::initInst();
  DRCI.wrapDatabase();
  Database database = std::move(DRCDM.getDatabase());
  DataManager::destroyInst();
  return database;
}

void expectWrappedEqual(Database& left, Database& right)
{
  const std::string mismatch = eccdb::test::compareIdrcWrappedDatabase(left, right);
  ASSERT_TRUE(mismatch.empty()) << mismatch;
}

std::vector<ids::Shape> legacyEnvironmentShapes()
{
  return eccdb::test::legacyIdrcEnvironmentShapes(DRCI);
}

std::vector<ids::Shape> legacyResultShapes()
{
  return eccdb::test::legacyIdrcResultShapes(DRCI);
}

struct NativeEnvironmentShapes
{
  std::vector<ids::Shape> shapes;
  std::set<size_t> obstacle_indices;
};

NativeEnvironmentShapes nativeEnvironmentShapeBatch(eccdb::DesignStore& design, eccdb::TechStore& technology,
                                                     eccdb::LibraryStore& library)
{
  const auto source = eccdb::adapters::makeDrcSource(design, technology, library);
  source->beginRun();
  NativeEnvironmentShapes batch;
  batch.shapes = source->environmentShapes(&batch.obstacle_indices);
  source->endRun();
  return batch;
}

std::vector<ids::Shape> nativeEnvironmentShapes(eccdb::DesignStore& design, eccdb::TechStore& technology,
                                                 eccdb::LibraryStore& library)
{
  return nativeEnvironmentShapeBatch(design, technology, library).shapes;
}

std::vector<ids::Shape> nativeResultShapes(eccdb::DesignStore& design, eccdb::TechStore& technology,
                                            eccdb::LibraryStore& library)
{
  const auto source = eccdb::adapters::makeDrcSource(design, technology, library);
  source->beginRun();
  auto shapes = source->resultShapes();
  source->endRun();
  return shapes;
}

using NetNames = std::vector<std::string>;

NetNames idbNetNames()
{
  auto* design = dmInst->get_idb_def_service()->get_design();
  NetNames names;
  const auto& regular = design->get_net_list()->get_net_list();
  names.reserve(regular.size() + design->get_special_net_list()->get_net_list().size());
  for (auto* net : regular) {
    names.push_back(net->get_net_name());
  }
  for (auto* net : design->get_special_net_list()->get_net_list()) {
    names.push_back(net->get_net_name());
  }
  return names;
}

NetNames eccdbNetNames(const eccdb::DesignStore& design)
{
  NetNames names;
  for (const auto net : design.netlistStorage().regularNets()) {
    names.push_back(design.netlistStorage().net(net).name);
  }
  for (const auto net : design.netlistStorage().specialNets()) {
    names.push_back(design.netlistStorage().net(net).name);
  }
  return names;
}

struct ShapeKey
{
  std::string net_name;
  int32_t layer_idx = -1;
  bool is_routing = false;
  int32_t ll_x = 0;
  int32_t ll_y = 0;
  int32_t ur_x = 0;
  int32_t ur_y = 0;

  [[nodiscard]] auto tuple() const
  {
    return std::make_tuple(net_name, layer_idx, is_routing, ll_x, ll_y, ur_x, ur_y);
  }

  [[nodiscard]] bool operator<(const ShapeKey& other) const { return tuple() < other.tuple(); }
  [[nodiscard]] bool operator==(const ShapeKey& other) const { return tuple() == other.tuple(); }
};

std::vector<ShapeKey> canonicalShapes(const std::vector<ids::Shape>& shapes, const NetNames& net_names)
{
  std::vector<ShapeKey> keys;
  keys.reserve(shapes.size());
  for (const auto& shape : shapes) {
    const auto net_index = static_cast<size_t>(shape.net_idx);
    keys.push_back(ShapeKey{.net_name = shape.net_idx >= 0 && net_index < net_names.size() ? net_names[net_index] : "<invalid>",
                            .layer_idx = shape.layer_idx,
                            .is_routing = shape.is_routing,
                            .ll_x = shape.ll_x,
                            .ll_y = shape.ll_y,
                            .ur_x = shape.ur_x,
                            .ur_y = shape.ur_y});
  }
  std::sort(keys.begin(), keys.end());
  return keys;
}

std::string shapeToString(const ShapeKey& shape)
{
  std::ostringstream out;
  out << "net=" << shape.net_name << " layer=" << shape.layer_idx << " routing=" << (shape.is_routing ? "true" : "false") << " rect=("
      << shape.ll_x << ',' << shape.ll_y << ',' << shape.ur_x << ',' << shape.ur_y << ')';
  return out.str();
}

void expectShapeListsEqual(std::string_view tag, const std::vector<ids::Shape>& left, const NetNames& left_names,
                           const std::vector<ids::Shape>& right, const NetNames& right_names)
{
  const auto left_keys = canonicalShapes(left, left_names);
  const auto right_keys = canonicalShapes(right, right_names);
  ASSERT_EQ(left_keys.size(), right_keys.size()) << tag;
  for (size_t index = 0; index < left_keys.size(); ++index) {
    ASSERT_EQ(left_keys[index], right_keys[index]) << tag << " first_diff=" << index << " left=" << shapeToString(left_keys[index])
                                                  << " right=" << shapeToString(right_keys[index]);
  }
}

struct ViolationKey
{
  std::string violation_type;
  int32_t layer_idx = -1;
  bool is_routing = false;
  int32_t ll_x = 0;
  int32_t ll_y = 0;
  int32_t ur_x = 0;
  int32_t ur_y = 0;
  std::vector<std::string> net_names;
  int32_t required_size = 0;

  [[nodiscard]] auto tuple() const
  {
    return std::make_tuple(violation_type, layer_idx, is_routing, ll_x, ll_y, ur_x, ur_y, net_names, required_size);
  }

  [[nodiscard]] bool operator<(const ViolationKey& other) const { return tuple() < other.tuple(); }
  [[nodiscard]] bool operator==(const ViolationKey& other) const { return tuple() == other.tuple(); }
};

std::vector<ViolationKey> canonicalViolations(const std::vector<ids::Violation>& violations, const NetNames& net_names)
{
  std::vector<ViolationKey> keys;
  keys.reserve(violations.size());
  for (const auto& violation : violations) {
    std::vector<std::string> violation_net_names;
    violation_net_names.reserve(violation.violation_net_set.size());
    for (const auto net_idx : violation.violation_net_set) {
      if (net_idx < 0) {
        violation_net_names.emplace_back("<invalid>");
      } else {
        const auto index = static_cast<size_t>(net_idx);
        violation_net_names.emplace_back(index < net_names.size() ? net_names[index] : "<invalid>");
      }
    }
    std::sort(violation_net_names.begin(), violation_net_names.end());
    keys.push_back(ViolationKey{.violation_type = violation.violation_type,
                                .layer_idx = violation.layer_idx,
                                .is_routing = violation.is_routing,
                                .ll_x = violation.ll_x,
                                .ll_y = violation.ll_y,
                                .ur_x = violation.ur_x,
                                .ur_y = violation.ur_y,
                                .net_names = std::move(violation_net_names),
                                .required_size = violation.required_size});
  }
  std::sort(keys.begin(), keys.end());
  return keys;
}

std::string violationToString(const ViolationKey& violation)
{
  std::ostringstream out;
  out << "type=" << violation.violation_type << " layer=" << violation.layer_idx << " routing=" << (violation.is_routing ? "true" : "false")
      << " rect=(" << violation.ll_x << ',' << violation.ll_y << ',' << violation.ur_x << ',' << violation.ur_y << ") nets={";
  for (size_t index = 0; index < violation.net_names.size(); ++index) {
    if (index != 0) {
      out << ',';
    }
    out << violation.net_names[index];
  }
  out << "} required=" << violation.required_size;
  return out.str();
}

void expectViolationListsEqual(std::string_view tag, const std::vector<ids::Violation>& left, const NetNames& left_names,
                               const std::vector<ids::Violation>& right, const NetNames& right_names)
{
  const auto left_keys = canonicalViolations(left, left_names);
  const auto right_keys = canonicalViolations(right, right_names);
  std::ostringstream details;
  if (left_keys.size() != right_keys.size()) {
    details << tag << "\nleft:";
    for (const auto& violation : left_keys) {
      details << "\n  " << violationToString(violation);
    }
    details << "\nright:";
    for (const auto& violation : right_keys) {
      details << "\n  " << violationToString(violation);
    }
  }
  ASSERT_EQ(left_keys.size(), right_keys.size()) << details.str();
  for (size_t index = 0; index < left_keys.size(); ++index) {
    ASSERT_EQ(left_keys[index], right_keys[index]) << tag << " first_diff=" << index << " left=" << violationToString(left_keys[index])
                                                  << " right=" << violationToString(right_keys[index]);
  }
}

}  // namespace

void compareIdbAndEccdb(const Ispd18Case& test_case, const std::filesystem::path& root, const char* skip_message)
{
  if (root.empty()) {
    GTEST_SKIP() << skip_message;
  }

  const auto [lef, def] = ispd18LefDef(root, test_case);

  eccdb::TechStore tech;
  std::unique_ptr<eccdb::LibraryStore> library;
  std::unique_ptr<eccdb::DesignStore> design;

  // SI2 LEF/DEF parsers are process-global. Clear leftover callback and
  // parser state before starting the EccDB import sequence.
  static_cast<void>(LefDefParser::lefrClear());
  static_cast<void>(LefDefParser::defrClear());
  eccdb::LefTechImporter(tech).import(lef);
  library = std::make_unique<eccdb::LibraryStore>(tech.techRegistry());
  eccdb::LefLibraryImporter(tech, *library).import(lef);
  design = std::make_unique<eccdb::DesignStore>(tech.techRegistry(), library->libraryRegistry());
  eccdb::DefDesignImporter(*design).import(def);
  loadIspd18IntoIdb(lef, def);

  Logger::initInst();
  eccdb::adapters::bindDrcSource(DRCI, nullptr, nullptr, nullptr);
  Database idb_database = wrapCurrentSource();
  eccdb::adapters::bindDrcSource(DRCI, design.get(), &tech, library.get());
  Database eccdb_database = wrapCurrentSource();
  eccdb::adapters::bindDrcSource(DRCI, nullptr, nullptr, nullptr);
  expectWrappedEqual(idb_database, eccdb_database);
  dmInst->reset();
}

TEST(SelfCheck, TwoIdbWrapsMatch)
{
  const std::filesystem::path root = ispd18Source();
  if (root.empty()) {
    GTEST_SKIP() << "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus";
  }

  const Ispd18Case test_case{"ispd18_sample"};
  const auto [lef, def] = ispd18LefDef(root, test_case);
  loadIspd18IntoIdb(lef, def);

  Logger::initInst();
  eccdb::adapters::bindDrcSource(DRCI, nullptr, nullptr, nullptr);
  Database left = wrapCurrentSource();
  Database right = wrapCurrentSource();
  expectWrappedEqual(left, right);
  dmInst->reset();
}

TEST(Shapes, GeneratedViaAndRegularWireMatch)
{
  TemporaryWorkspace workspace;
  const auto lef = workspace.path("shape-fixture.lef");
  const auto def = workspace.path("shape-fixture.def");
  writeText(lef, kShapeFixtureLef);
  writeText(def, kShapeFixtureDef);

  eccdb::TechStore tech;
  std::unique_ptr<eccdb::LibraryStore> library;
  std::unique_ptr<eccdb::DesignStore> design;

  static_cast<void>(LefDefParser::lefrClear());
  static_cast<void>(LefDefParser::defrClear());
  eccdb::LefTechImporter(tech).import(lef);
  library = std::make_unique<eccdb::LibraryStore>(tech.techRegistry());
  eccdb::LefLibraryImporter(tech, *library).import(lef);
  design = std::make_unique<eccdb::DesignStore>(tech.techRegistry(), library->libraryRegistry());
  eccdb::DefDesignImporter(*design).import(def);
  loadIspd18IntoIdb(lef, def);
  const auto idb_names = idbNetNames();
  const auto eccdb_names = eccdbNetNames(*design);

  Logger::initInst();
  eccdb::adapters::bindDrcSource(DRCI, nullptr, nullptr, nullptr);
  DataManager::initInst();
  DRCI.wrapDatabase();
  const auto idb_env = legacyEnvironmentShapes();
  const auto idb_result = legacyResultShapes();
  DataManager::destroyInst();

  const auto eccdb_environment = nativeEnvironmentShapeBatch(*design, tech, *library);
  const auto& eccdb_env = eccdb_environment.shapes;
  const auto eccdb_result = nativeResultShapes(*design, tech, *library);

  expectShapeListsEqual("env", idb_env, idb_names, eccdb_env, eccdb_names);
  expectShapeListsEqual("result", idb_result, idb_names, eccdb_result, eccdb_names);
  const auto env_keys = canonicalShapes(eccdb_env, eccdb_names);
  const auto has_env_shape = [&](const ShapeKey& expected) {
    return std::find(env_keys.begin(), env_keys.end(), expected) != env_keys.end();
  };
  EXPECT_TRUE(has_env_shape(ShapeKey{.net_name = "<invalid>", .layer_idx = 0, .is_routing = true,
                                     .ll_x = 200, .ll_y = 400, .ur_x = 400, .ur_y = 600}))
      << "placed instance obstruction must be part of the environment";
  EXPECT_FALSE(has_env_shape(ShapeKey{.net_name = "<invalid>", .layer_idx = 0, .is_routing = true,
                                      .ll_x = 0, .ll_y = 0, .ur_x = 200, .ur_y = 200}))
      << "unplaced instance obstruction must be excluded from the environment";
  EXPECT_TRUE(has_env_shape(ShapeKey{.net_name = "<invalid>", .layer_idx = 1, .is_routing = true,
                                     .ll_x = 1400, .ll_y = 1400, .ur_x = 1600, .ur_y = 1600}))
      << "routing blockage must be part of the environment";
  EXPECT_FALSE(eccdb_environment.obstacle_indices.empty()) << "placed macro OBS must retain obstacle provenance";
  for (const auto index : eccdb_environment.obstacle_indices) {
    ASSERT_LT(index, eccdb_env.size());
    EXPECT_TRUE(eccdb_env[index].is_routing) << "only routing obstructions are DRC obstacle inputs";
  }
  const auto result_keys = canonicalShapes(idb_result, idb_names);
  EXPECT_NE(std::find(result_keys.begin(), result_keys.end(),
                      ShapeKey{.net_name = "a_sig", .layer_idx = 0, .is_routing = true, .ll_x = 50, .ll_y = 250, .ur_x = 550, .ur_y = 350}),
            result_keys.end())
      << "a_sig should retain its semantic net name";
  EXPECT_NE(std::find(result_keys.begin(), result_keys.end(),
                      ShapeKey{.net_name = "z_sig", .layer_idx = 0, .is_routing = true, .ll_x = 50, .ll_y = 50, .ur_x = 550, .ur_y = 150}),
            result_keys.end())
      << "z_sig should retain its semantic net name";

  auto run_violations = [&](bool use_eccdb_source, std::string_view temp_name) {
    std::shared_ptr<DRCDesignSource> source;
    if (use_eccdb_source) {
      source = eccdb::adapters::makeDrcSource(*design, tech, *library);
      DRCI.setDesignSource(source);
    } else {
      DRCI.setDesignSource(nullptr);
    }
    std::map<std::string, std::any> config_map;
    config_map.insert({"-temp_directory_path", workspace.path(temp_name).string()});
    config_map.insert({"-thread_number", 1});
    DRCI.initDRC(config_map, true);
    const auto environment = source ? source->environmentShapes(nullptr) : legacyEnvironmentShapes();
    const auto result = source ? source->resultShapes() : legacyResultShapes();
    const auto violations = DRCI.getViolationList(environment, result, {}, {});
    DRCI.destroyDRC();
    DRCI.setDesignSource(nullptr);
    return violations;
  };

  const auto idb_violations = run_violations(false, "idb-drc");
  const auto eccdb_violations = run_violations(true, "eccdb-drc");
  eccdb::adapters::bindDrcSource(DRCI, nullptr, nullptr, nullptr);
  expectViolationListsEqual("violations", idb_violations, idb_names, eccdb_violations, eccdb_names);

  const auto count_source = eccdb::adapters::makeDrcSource(*design, tech, *library);
  DRCI.setDesignSource(count_source);
  std::map<std::string, std::any> count_config;
  const auto count_directory = workspace.path("eccdb-counts");
  count_config.insert({"-temp_directory_path", count_directory.string()});
  count_config.insert({"-thread_number", 1});
  DRCI.initDRC(count_config, true);
  DRCI.runDRC();
  EXPECT_EQ(DRCDM.getDatabase().get_design_name(), "idrc_shape_fixture")
      << "runDRC must retain the working model until explicit destroyDRC";
  EXPECT_FALSE(DRCDM.getDatabase().get_routing_layer_list().empty())
      << "runDRC must retain wrapped technology until explicit destroyDRC";
  const auto report_path = count_directory / "violation_map.json";
  std::ifstream report(report_path, std::ios::binary);
  if (!report) {
    ADD_FAILURE() << "runDRC must write " << report_path;
  } else {
    const std::string report_text((std::istreambuf_iterator<char>(report)), std::istreambuf_iterator<char>());
    EXPECT_FALSE(report_text.empty());
    if (!report_text.empty()) {
      EXPECT_EQ(report_text.front(), '[');
    }
    EXPECT_NE(report_text.find("\"type\""), std::string::npos);
  }
  DRCI.destroyDRC();
  DRCI.setDesignSource(nullptr);
  dmInst->reset();

  const auto special = design->netlistStorage().findSpecialNet("VDD");
  ASSERT_TRUE(special);
  const auto m2 = eccdb::TechRoutingLayerId{tech.findLayer("M2").entity()};
  static_cast<void>(design->geometryStorage().appendNetShape(
      special, eccdb::DesignNetRectangle{.layer = m2, .rectangle = {1500, 300, 1600, 400}}));
  const auto standalone_result = nativeResultShapes(*design, tech, *library);
  const auto standalone_keys = canonicalShapes(standalone_result, eccdbNetNames(*design));
  EXPECT_NE(std::find(standalone_keys.begin(), standalone_keys.end(),
                      ShapeKey{.net_name = "VDD", .layer_idx = 1, .is_routing = true,
                               .ll_x = 1500, .ll_y = 300, .ur_x = 1600, .ur_y = 400}),
            standalone_keys.end())
      << "standalone SPECIALNET geometry must reach iDRC result input";
}

TEST(Shapes, GeneratedViaOriginOffsetMatchesAndShiftsOnlyMetal)
{
  TemporaryWorkspace workspace;
  const auto lef = workspace.path("generated-via-origin-offset.lef");
  const auto def = workspace.path("generated-via-origin-offset.def");
  writeText(lef, kShapeFixtureLef);
  auto def_text = std::string{kShapeFixtureDef};
  const auto row_col = def_text.find("  + ROWCOL 2 3\n");
  ASSERT_NE(row_col, std::string::npos);
  def_text.insert(row_col + std::string_view{"  + ROWCOL 2 3\n"}.size(), "  + ORIGIN 10 -20\n  + OFFSET 3 4 -5 -6\n");
  writeText(def, def_text);

  eccdb::TechStore tech;
  static_cast<void>(LefDefParser::lefrClear());
  static_cast<void>(LefDefParser::defrClear());
  eccdb::LefTechImporter(tech).import(lef);
  eccdb::LibraryStore library{tech.techRegistry()};
  eccdb::LefLibraryImporter(tech, library).import(lef);
  eccdb::DesignStore design{tech.techRegistry(), library.libraryRegistry()};
  eccdb::DefDesignImporter(design).import(def);
  loadIspd18IntoIdb(lef, def);

  Logger::initInst();
  eccdb::adapters::bindDrcSource(DRCI, nullptr, nullptr, nullptr);
  DataManager::initInst();
  DRCI.wrapDatabase();
  const auto idb_result = legacyResultShapes();
  DataManager::destroyInst();

  const auto eccdb_result = nativeResultShapes(design, tech, library);

  const auto idb_names = idbNetNames();
  const auto eccdb_names = eccdbNetNames(design);
  expectShapeListsEqual("generated via origin/offset", idb_result, idb_names, eccdb_result, eccdb_names);

  const auto result_keys = canonicalShapes(idb_result, idb_names);
  const auto has_result = [&](const ShapeKey& expected) {
    return std::find(result_keys.begin(), result_keys.end(), expected) != result_keys.end();
  };
  EXPECT_TRUE(has_result(ShapeKey{.net_name = "VDD", .layer_idx = 0, .is_routing = true,
                                   .ll_x = 813, .ll_y = 809, .ur_x = 1213, .ur_y = 1159}));
  EXPECT_TRUE(has_result(ShapeKey{.net_name = "VDD", .layer_idx = 1, .is_routing = true,
                                   .ll_x = 785, .ll_y = 779, .ur_x = 1225, .ur_y = 1169}));
  EXPECT_TRUE(has_result(ShapeKey{.net_name = "VDD", .layer_idx = 0, .is_routing = false,
                                   .ll_x = 840, .ll_y = 845, .ur_x = 940, .ur_y = 965}));
  dmInst->reset();
}

TEST(Shapes, BoundaryWireKeepsNegativeEnvelopeAndViaFootprint)
{
  TemporaryWorkspace workspace;
  const auto lef = workspace.path("boundary-wire.lef");
  const auto def = workspace.path("boundary-wire.def");
  writeText(lef, kShapeFixtureLef);
  writeText(def, kBoundaryWireFixtureDef);

  eccdb::TechStore tech;
  static_cast<void>(LefDefParser::lefrClear());
  static_cast<void>(LefDefParser::defrClear());
  eccdb::LefTechImporter(tech).import(lef);
  eccdb::LibraryStore library{tech.techRegistry()};
  eccdb::LefLibraryImporter(tech, library).import(lef);
  eccdb::DesignStore design{tech.techRegistry(), library.libraryRegistry()};
  eccdb::DefDesignImporter(design).import(def);
  loadIspd18IntoIdb(lef, def);

  Logger::initInst();
  eccdb::adapters::bindDrcSource(DRCI, nullptr, nullptr, nullptr);
  DataManager::initInst();
  DRCI.wrapDatabase();
  const auto idb_env = legacyEnvironmentShapes();
  const auto idb_result = legacyResultShapes();
  DataManager::destroyInst();

  const auto eccdb_env = nativeEnvironmentShapes(design, tech, library);
  const auto eccdb_result = nativeResultShapes(design, tech, library);

  const auto idb_names = idbNetNames();
  const auto eccdb_names = eccdbNetNames(design);
  expectShapeListsEqual("boundary env", idb_env, idb_names, eccdb_env, eccdb_names);
  expectShapeListsEqual("boundary result", idb_result, idb_names, eccdb_result, eccdb_names);

  const auto env_keys = canonicalShapes(idb_env, idb_names);
  const auto result_keys = canonicalShapes(idb_result, idb_names);
  const auto has_env = [&](const ShapeKey& expected) {
    return std::find(env_keys.begin(), env_keys.end(), expected) != env_keys.end();
  };
  const auto has_result = [&](const ShapeKey& expected) {
    return std::find(result_keys.begin(), result_keys.end(), expected) != result_keys.end();
  };

  EXPECT_TRUE(has_env(ShapeKey{.net_name = "boundary", .layer_idx = 0, .is_routing = true,
                                .ll_x = 200, .ll_y = 0, .ur_x = 300, .ur_y = 100}))
      << "the DEF IO-port local rectangle must be translated by its PORT placement";
  EXPECT_TRUE(has_result(ShapeKey{.net_name = "boundary", .layer_idx = 0, .is_routing = true,
                                   .ll_x = 200, .ll_y = -50, .ur_x = 550, .ur_y = 50}))
      << "a y=0 centerline must retain its negative half-width envelope";
  EXPECT_TRUE(has_result(ShapeKey{.net_name = "boundary", .layer_idx = 0, .is_routing = true,
                                   .ll_x = 420, .ll_y = -80, .ur_x = 580, .ur_y = 80}))
      << "the lower routing footprint of VIA12 must retain negative coordinates";
  EXPECT_TRUE(has_result(ShapeKey{.net_name = "boundary", .layer_idx = 1, .is_routing = true,
                                   .ll_x = 420, .ll_y = -80, .ur_x = 580, .ur_y = 80}))
      << "the upper routing footprint of VIA12 must retain negative coordinates";

  dmInst->reset();
}

TEST(Shapes, IdbProjectionPreservesNegativeEnvelopeAndFlushExtension)
{
  TemporaryWorkspace workspace;
  const auto lef = workspace.path("flush-projection.lef");
  const auto def = workspace.path("flush-projection.def");
  writeText(lef, kShapeFixtureLef);
  writeText(def, kFlushProjectionFixtureDef);

  eccdb::TechStore tech;
  static_cast<void>(LefDefParser::lefrClear());
  static_cast<void>(LefDefParser::defrClear());
  eccdb::LefTechImporter(tech).import(lef);
  eccdb::LibraryStore library{tech.techRegistry()};
  eccdb::LefLibraryImporter(tech, library).import(lef);
  eccdb::DesignStore design{tech.techRegistry(), library.libraryRegistry()};
  eccdb::DefDesignImporter(design).import(def);
  loadIspd18IntoIdb(lef, def);
  const auto idb_names = idbNetNames();
  const auto eccdb_names = eccdbNetNames(design);

  Logger::initInst();
  eccdb::adapters::bindDrcSource(DRCI, nullptr, nullptr, nullptr);
  DataManager::initInst();
  DRCI.wrapDatabase();
  const auto idb_result = legacyResultShapes();
  DataManager::destroyInst();

  const auto eccdb_result = nativeResultShapes(design, tech, library);

  expectShapeListsEqual("flush extension result", idb_result, idb_names, eccdb_result, eccdb_names);
  const auto idb_keys = canonicalShapes(idb_result, idb_names);
  const auto eccdb_keys = canonicalShapes(eccdb_result, eccdb_names);
  const auto has_shape = [](const std::vector<ShapeKey>& keys, const ShapeKey& expected) {
    return std::find(keys.begin(), keys.end(), expected) != keys.end();
  };
  const auto expect_for_both = [&](const ShapeKey& expected) {
    EXPECT_TRUE(has_shape(idb_keys, expected)) << "iDB projection misses " << shapeToString(expected);
    EXPECT_TRUE(has_shape(eccdb_keys, expected)) << "EccDB projection misses " << shapeToString(expected);
  };

  expect_for_both(ShapeKey{.net_name = "boundary_plain", .layer_idx = 0, .is_routing = true,
                            .ll_x = 200, .ll_y = -50, .ur_x = 550, .ur_y = 50});
  // The input starts at x=500 and marks that endpoint flush with ext=0. Its
  // metal must end at x=500, while the reversed plain endpoint expands by
  // half the M1 width to x=200.
  expect_for_both(ShapeKey{.net_name = "boundary_reversed_flush", .layer_idx = 0, .is_routing = true,
                            .ll_x = 200, .ll_y = 50, .ur_x = 500, .ur_y = 150});
  // Cover the matching vertical branch: the first point is the geometrically
  // upper endpoint despite appearing first in the DEF path.
  expect_for_both(ShapeKey{.net_name = "boundary_reversed_vertical_flush", .layer_idx = 0, .is_routing = true,
                            .ll_x = 50, .ll_y = 200, .ur_x = 150, .ur_y = 500});
  dmInst->reset();
}

TEST(NetIndex, EccdbRunSnapshotTracksShapeOrdinalsAndRefreshesAfterStructuralChanges)
{
  TemporaryWorkspace workspace;
  const auto lef = workspace.path("net-index-fixture.lef");
  const auto def = workspace.path("net-index-fixture.def");
  writeText(lef, kShapeFixtureLef);
  writeText(def, kShapeFixtureDef);

  eccdb::TechStore tech;
  static_cast<void>(LefDefParser::lefrClear());
  static_cast<void>(LefDefParser::defrClear());
  eccdb::LefTechImporter(tech).import(lef);
  eccdb::LibraryStore library{tech.techRegistry()};
  eccdb::LefLibraryImporter(tech, library).import(lef);
  eccdb::DesignStore design{tech.techRegistry(), library.libraryRegistry()};
  eccdb::DefDesignImporter(design).import(def);

  auto& netlist = design.netlistStorage();
  const auto retired_regular = netlist.createNet(eccdb::DesignNet{.name = "retired_regular"});
  const auto retired_special = netlist.createSpecialNet(eccdb::DesignNet{.name = "retired_special"});

  const auto init = [&](std::string_view directory) {
    std::map<std::string, std::any> config;
    config.emplace("-temp_directory_path", workspace.path(directory).string());
    config.emplace("-thread_number", 1);
    DRCI.initDRC(std::move(config), true);
  };
  const auto expect_snapshot = [&]() {
    const auto regular = netlist.regularNets();
    const auto special = netlist.specialNets();
    const auto names = eccdbNetNames(design);
    EXPECT_EQ(names.size(), regular.size() + special.size());
    for (size_t index = 0; index < regular.size(); ++index) {
      EXPECT_EQ(DRCI.getNetName(static_cast<int32_t>(index)), netlist.net(regular[index]).name);
    }
    for (size_t index = 0; index < special.size(); ++index) {
      EXPECT_EQ(DRCI.getNetName(static_cast<int32_t>(regular.size() + index)), netlist.net(special[index]).name);
    }
    EXPECT_EQ(DRCI.getNetName(-1), "obs");
    EXPECT_EQ(DRCI.getNetName(-2), "<invalid_eccdb_net:-2>");
    EXPECT_EQ(DRCI.getNetName(static_cast<int32_t>(names.size())),
              "<invalid_eccdb_net:" + std::to_string(names.size()) + ">");
    return names;
  };

  Logger::initInst();
  const auto source = eccdb::adapters::makeDrcSource(design, tech, library);
  DRCI.setDesignSource(source);
  init("net-index-initial");
  const auto initial_names = expect_snapshot();
  const auto initial_shapes = source->resultShapes();
  ASSERT_FALSE(initial_shapes.empty());
  const auto shape_name = [&](int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y) {
    const auto shape = std::find_if(initial_shapes.begin(), initial_shapes.end(), [&](const ids::Shape& candidate) {
      return candidate.ll_x == ll_x && candidate.ll_y == ll_y && candidate.ur_x == ur_x && candidate.ur_y == ur_y;
    });
    EXPECT_NE(shape, initial_shapes.end());
    return shape == initial_shapes.end() ? std::string{} : DRCI.getNetName(shape->net_idx);
  };
  EXPECT_EQ(shape_name(50, 50, 550, 150), "z_sig");
  EXPECT_EQ(shape_name(50, 250, 550, 350), "a_sig");
  DRCI.destroyDRC();

  // Change the object set, rather than merely renaming an old entry.  The
  // second run has one additional regular net and one additional special net.
  ASSERT_TRUE(netlist.destroyNet(retired_regular));
  ASSERT_TRUE(netlist.destroyNet(retired_special));
  static_cast<void>(netlist.createNet(eccdb::DesignNet{.name = "late_regular_0"}));
  static_cast<void>(netlist.createNet(eccdb::DesignNet{.name = "late_regular_1"}));
  const auto late_special_0 = netlist.createSpecialNet(eccdb::DesignNet{.name = "late_special_0"});
  static_cast<void>(netlist.createSpecialNet(eccdb::DesignNet{.name = "late_special_1"}));
  const auto m2 = eccdb::TechRoutingLayerId{tech.findLayer("M2").entity()};
  static_cast<void>(design.geometryStorage().appendNetShape(
      late_special_0, eccdb::DesignNetRectangle{.layer = m2, .rectangle = {1700, 300, 1800, 400}}));

  init("net-index-refreshed");
  const auto refreshed_names = expect_snapshot();
  EXPECT_GT(refreshed_names.size(), initial_names.size());
  EXPECT_EQ(std::find(refreshed_names.begin(), refreshed_names.end(), "retired_regular"), refreshed_names.end());
  EXPECT_EQ(std::find(refreshed_names.begin(), refreshed_names.end(), "retired_special"), refreshed_names.end());
  EXPECT_NE(std::find(refreshed_names.begin(), refreshed_names.end(), "late_regular_0"), refreshed_names.end());
  EXPECT_NE(std::find(refreshed_names.begin(), refreshed_names.end(), "late_regular_1"), refreshed_names.end());
  EXPECT_NE(std::find(refreshed_names.begin(), refreshed_names.end(), "late_special_0"), refreshed_names.end());
  EXPECT_NE(std::find(refreshed_names.begin(), refreshed_names.end(), "late_special_1"), refreshed_names.end());

  const auto refreshed_shapes = source->resultShapes();
  const auto late_shape = std::find_if(refreshed_shapes.begin(), refreshed_shapes.end(), [](const ids::Shape& candidate) {
    return candidate.ll_x == 1700 && candidate.ll_y == 300 && candidate.ur_x == 1800 && candidate.ur_y == 400;
  });
  ASSERT_NE(late_shape, refreshed_shapes.end());
  EXPECT_EQ(DRCI.getNetName(late_shape->net_idx), "late_special_0");
  DRCI.destroyDRC();

  // Once the run ends, the public lookup intentionally returns to the live
  // source; this also proves destroy cleared the stale run table.
  const auto live_names = eccdbNetNames(design);
  ASSERT_FALSE(live_names.empty());
  EXPECT_EQ(DRCI.getNetName(static_cast<int32_t>(live_names.size() - 1)), live_names.back());
  DRCI.setDesignSource(nullptr);
}

TEST(Source, DatabaseContextCanAttachAndDetachNativeSource)
{
  TemporaryWorkspace workspace;
  const auto lef = workspace.path("context-fixture.lef");
  const auto def = workspace.path("context-fixture.def");
  writeText(lef, kShapeFixtureLef);
  writeText(def, kShapeFixtureDef);

  eccdb::TechStore tech;
  static_cast<void>(LefDefParser::lefrClear());
  static_cast<void>(LefDefParser::defrClear());
  eccdb::LefTechImporter(tech).import(lef);
  eccdb::LibraryStore library{tech.techRegistry()};
  eccdb::LefLibraryImporter(tech, library).import(lef);
  eccdb::DesignStore design{tech.techRegistry(), library.libraryRegistry()};
  eccdb::DefDesignImporter(design).import(def);
  loadIspd18IntoIdb(lef, def);

  Logger::initInst();
  eccdb::DatabaseContext context(tech, library, design);
  const auto run_bound_source = [&](std::string_view directory, std::string_view expected_design_name) {
    std::map<std::string, std::any> config;
    config.emplace("-temp_directory_path", workspace.path(directory).string());
    config.emplace("-thread_number", 1);
    DRCI.initDRC(std::move(config), true);
    EXPECT_EQ(DRCDM.getDatabase().get_design_name(), expected_design_name);
    DRCI.destroyDRC();
  };

  eccdb::adapters::bindDrcSource(DRCI, context);
  run_bound_source("context-reference", "idrc_shape_fixture");
  const auto from_reference = nativeResultShapes(design, tech, library);
  eccdb::adapters::bindDrcSource(DRCI, &context);
  run_bound_source("context-pointer", "idrc_shape_fixture");
  const auto from_pointer = nativeResultShapes(design, tech, library);
  eccdb::adapters::bindDrcSource(DRCI, nullptr);
  run_bound_source("context-detach", "idrc_shape_fixture");
  const auto after_detach = legacyResultShapes();
  eccdb::adapters::bindDrcSource(DRCI, &context);
  run_bound_source("context-reattach", "idrc_shape_fixture");
  const auto after_reattach = nativeResultShapes(design, tech, library);
  eccdb::adapters::bindDrcSource(DRCI, nullptr);

  expectShapeListsEqual("context pointer", from_reference, eccdbNetNames(design), from_pointer, eccdbNetNames(design));
  expectShapeListsEqual("context detach", from_reference, eccdbNetNames(design), after_detach, idbNetNames());
  expectShapeListsEqual("context reattach", from_reference, eccdbNetNames(design), after_reattach, eccdbNetNames(design));
  dmInst->reset();
}

TEST(Source, PublicInitUsesNativeDatabaseWrapperWithoutIdb)
{
  TemporaryWorkspace workspace;
  const auto lef = workspace.path("native-init.lef");
  const auto def = workspace.path("native-init.def");
  writeText(lef, kShapeFixtureLef);
  writeText(def, kShapeFixtureDef);

  eccdb::TechStore tech;
  static_cast<void>(LefDefParser::lefrClear());
  static_cast<void>(LefDefParser::defrClear());
  eccdb::LefTechImporter(tech).import(lef);
  eccdb::LibraryStore library{tech.techRegistry()};
  eccdb::LefLibraryImporter(tech, library).import(lef);
  eccdb::DesignStore design{tech.techRegistry(), library.libraryRegistry()};
  eccdb::DefDesignImporter(design).import(def);

  // Do not populate iDB.  This exercises initDRC's public native path, which
  // previously re-entered DataManager's unconditional iDB wrapper.
  dmInst->reset();
  Logger::initInst();
  const auto source = eccdb::adapters::makeDrcSource(design, tech, library);
  DRCI.setDesignSource(source);
  std::map<std::string, std::any> config;
  config.emplace("-temp_directory_path", workspace.path("native-init-drc").string());
  config.emplace("-thread_number", 1);
  DRCI.initDRC(std::move(config), true);

  auto& database = DRCDM.getDatabase();
  EXPECT_EQ(database.get_design_name(), "idrc_shape_fixture");
  EXPECT_EQ(database.get_micron_dbu(), 1000);
  EXPECT_EQ(database.get_routing_layer_list().size(), 2U);
  EXPECT_EQ(database.get_cut_layer_list().size(), 1U);
  EXPECT_FALSE(source->resultShapes().empty());

  DRCI.destroyDRC();
  DRCI.setDesignSource(nullptr);
  dmInst->reset();
}

TEST(Shapes, DISABLED_UsesViaExitLayerAndAllSegmentsForRegularWire)
{
  constexpr std::string_view def_text = R"DEF(VERSION 5.8 ;
DESIGN routed_shape ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 1000 1000 ) ;
NETS 1 ;
- signal + ROUTED M1 ( 100 100 ) ( 500 100 ) VIA12 RECT ( -20 -20 20 20 ) ( 500 700 ) ;
END NETS
END DESIGN
)DEF";

  TemporaryWorkspace workspace;
  const auto lef = workspace.path("layer-transition.lef");
  const auto def = workspace.path("layer-transition.def");
  writeText(lef, kShapeFixtureLef);
  writeText(def, def_text);

  eccdb::TechStore tech;
  static_cast<void>(LefDefParser::lefrClear());
  static_cast<void>(LefDefParser::defrClear());
  eccdb::LefTechImporter(tech).import(lef);
  eccdb::LibraryStore library{tech.techRegistry()};
  eccdb::LefLibraryImporter(tech, library).import(lef);
  eccdb::DesignStore design{tech.techRegistry(), library.libraryRegistry()};
  eccdb::DefDesignImporter(design).import(def);

  Logger::initInst();
  const auto shapes = nativeResultShapes(design, tech, library);
  const auto keys = canonicalShapes(shapes, eccdbNetNames(design));
  const auto has_shape = [&](const ShapeKey& expected) {
    return std::find(keys.begin(), keys.end(), expected) != keys.end();
  };

  EXPECT_TRUE(has_shape(ShapeKey{.net_name = "signal", .layer_idx = 0, .is_routing = true,
                                 .ll_x = 50, .ll_y = 50, .ur_x = 550, .ur_y = 150}));
  EXPECT_TRUE(has_shape(ShapeKey{.net_name = "signal", .layer_idx = 1, .is_routing = true,
                                 .ll_x = 450, .ll_y = 50, .ur_x = 550, .ur_y = 750}))
      << "the segment after VIA12 belongs to M2";
  EXPECT_TRUE(has_shape(ShapeKey{.net_name = "signal", .layer_idx = 1, .is_routing = true,
                                 .ll_x = 480, .ll_y = 80, .ur_x = 520, .ur_y = 120}))
      << "the RECT after VIA12 belongs to M2";
}

TEST(Wrap, Ispd18Sample) { compareIdbAndEccdb({"ispd18_sample"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Sample2) { compareIdbAndEccdb({"ispd18_sample2"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Sample3) { compareIdbAndEccdb({"ispd18_sample3"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test1) { compareIdbAndEccdb({"ispd18_test1"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test2) { compareIdbAndEccdb({"ispd18_test2"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test3) { compareIdbAndEccdb({"ispd18_test3"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test4) { compareIdbAndEccdb({"ispd18_test4"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test5) { compareIdbAndEccdb({"ispd18_test5"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test6) { compareIdbAndEccdb({"ispd18_test6"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test7) { compareIdbAndEccdb({"ispd18_test7"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test8) { compareIdbAndEccdb({"ispd18_test8"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test9) { compareIdbAndEccdb({"ispd18_test9"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test10) { compareIdbAndEccdb({"ispd18_test10"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd19Sample)
{
  compareIdbAndEccdb({"ispd19_sample"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Sample2)
{
  compareIdbAndEccdb({"ispd19_sample2"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Sample3)
{
  compareIdbAndEccdb({"ispd19_sample3"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Sample4)
{
  compareIdbAndEccdb({"ispd19_sample4"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test1)
{
  compareIdbAndEccdb({"ispd19_test1"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test2)
{
  compareIdbAndEccdb({"ispd19_test2"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test3)
{
  compareIdbAndEccdb({"ispd19_test3"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test4)
{
  compareIdbAndEccdb({"ispd19_test4"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test5)
{
  compareIdbAndEccdb({"ispd19_test5"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test6)
{
  compareIdbAndEccdb({"ispd19_test6"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test7)
{
  compareIdbAndEccdb({"ispd19_test7"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test8)
{
  compareIdbAndEccdb({"ispd19_test8"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test9)
{
  compareIdbAndEccdb({"ispd19_test9"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test10)
{
  compareIdbAndEccdb({"ispd19_test10"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}

}  // namespace idrc
