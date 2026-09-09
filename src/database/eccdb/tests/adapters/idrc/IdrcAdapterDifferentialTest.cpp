// SPDX-License-Identifier: MulanPSL-2.0

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
#include "DataManager.hpp"
#include "Database.hpp"
#include "Logger.hpp"
#include "design/DesignStore.h"
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
  bool repair_metal9_prl = false;
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

COMPONENTS 0 ;
END COMPONENTS
PINS 0 ;
END PINS
SPECIALNETS 1 ;
- VDD
  + USE POWER
  + ROUTED M1 0 + SHAPE STRIPE ( 1000 1000 ) GENERATED12
  ;
END SPECIALNETS
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

std::string readText(const std::filesystem::path& path)
{
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot read file: " + path.string());
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

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

std::filesystem::path repairInvalidIspd18Metal9Prl(const std::filesystem::path& source, const std::filesystem::path& output)
{
  constexpr std::string_view malformed = "    WIDTH 4.5   1.50\n    WIDTH 1.5   0.45 ;";
  constexpr std::string_view repaired = "    WIDTH 4.5   1.50 ;";
  auto contents = readText(source);
  const auto position = contents.find(malformed);
  if (position == std::string::npos || contents.find(malformed, position + malformed.size()) != std::string::npos) {
    throw std::runtime_error("expected exactly one malformed ISPD18 Metal9 PRL row in " + source.string());
  }
  contents.replace(position, malformed.size(), repaired);
  writeText(output, contents);
  return output;
}

std::pair<std::filesystem::path, std::filesystem::path> ispd18LefDef(const std::filesystem::path& root, const Ispd18Case& test_case,
                                                                    const std::filesystem::path& workspace)
{
  auto lef = root / test_case.name / (test_case.name + ".input.lef");
  const auto def = root / test_case.name / (test_case.name + ".input.def");
  if (!std::filesystem::exists(lef) || !std::filesystem::exists(def)) {
    throw std::runtime_error("missing ISPD18 lef/def for " + test_case.name);
  }
  if (test_case.repair_metal9_prl) {
    lef = repairInvalidIspd18Metal9Prl(lef, workspace / (test_case.name + "-repaired.lef"));
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
  const std::string mismatch = DRCI.compareWrappedDatabase(left, right);
  ASSERT_TRUE(mismatch.empty()) << mismatch;
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

NetNames enttNetNames(const eccdb::DesignStore& design)
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
  std::vector<int32_t> net_indices;
  int32_t required_size = 0;

  [[nodiscard]] auto tuple() const
  {
    return std::make_tuple(violation_type, layer_idx, is_routing, ll_x, ll_y, ur_x, ur_y, net_indices, required_size);
  }

  [[nodiscard]] bool operator<(const ViolationKey& other) const { return tuple() < other.tuple(); }
  [[nodiscard]] bool operator==(const ViolationKey& other) const { return tuple() == other.tuple(); }
};

std::vector<ViolationKey> canonicalViolations(const std::vector<ids::Violation>& violations)
{
  std::vector<ViolationKey> keys;
  keys.reserve(violations.size());
  for (const auto& violation : violations) {
    keys.push_back(ViolationKey{.violation_type = violation.violation_type,
                                .layer_idx = violation.layer_idx,
                                .is_routing = violation.is_routing,
                                .ll_x = violation.ll_x,
                                .ll_y = violation.ll_y,
                                .ur_x = violation.ur_x,
                                .ur_y = violation.ur_y,
                                .net_indices = {violation.violation_net_set.begin(), violation.violation_net_set.end()},
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
  for (size_t index = 0; index < violation.net_indices.size(); ++index) {
    if (index != 0) {
      out << ',';
    }
    out << violation.net_indices[index];
  }
  out << "} required=" << violation.required_size;
  return out.str();
}

void expectViolationListsEqual(std::string_view tag, const std::vector<ids::Violation>& left, const std::vector<ids::Violation>& right)
{
  const auto left_keys = canonicalViolations(left);
  const auto right_keys = canonicalViolations(right);
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

void compareIdbAndEntt(const Ispd18Case& test_case, const std::filesystem::path& root, const char* skip_message)
{
  if (root.empty()) {
    GTEST_SKIP() << skip_message;
  }

  TemporaryWorkspace workspace;
  const auto repair_workspace = workspace.path("ws");
  std::filesystem::create_directories(repair_workspace);
  const auto [lef, def] = ispd18LefDef(root, test_case, repair_workspace);

  eccdb::TechStore tech;
  std::unique_ptr<eccdb::LibraryStore> library;
  std::unique_ptr<eccdb::DesignStore> design;

  // SI2 LEF/DEF parsers are process-global. Clear leftover callback and
  // parser state before starting the EnTT import sequence.
  static_cast<void>(LefDefParser::lefrClear());
  static_cast<void>(LefDefParser::defrClear());
  eccdb::LefTechImporter(tech).import(lef);
  library = std::make_unique<eccdb::LibraryStore>(tech.techRegistry());
  eccdb::LefLibraryImporter(tech, *library).import(lef);
  design = std::make_unique<eccdb::DesignStore>(tech.techRegistry(), library->libraryRegistry());
  eccdb::DefDesignImporter(*design).import(def);
  loadIspd18IntoIdb(lef, def);

  Logger::initInst();
  DRCI.setDesignSource(nullptr, nullptr, nullptr);
  Database idb_database = wrapCurrentSource();
  DRCI.setDesignSource(design.get(), &tech, library.get());
  Database entt_database = wrapCurrentSource();
  DRCI.setDesignSource(nullptr, nullptr, nullptr);
  expectWrappedEqual(idb_database, entt_database);
  dmInst->reset();
}

TEST(SelfCheck, TwoIdbWrapsMatch)
{
  const std::filesystem::path root = ispd18Source();
  if (root.empty()) {
    GTEST_SKIP() << "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus";
  }

  TemporaryWorkspace workspace;
  const Ispd18Case test_case{"ispd18_sample"};
  const auto repair_workspace = workspace.path("ws");
  std::filesystem::create_directories(repair_workspace);
  const auto [lef, def] = ispd18LefDef(root, test_case, repair_workspace);
  loadIspd18IntoIdb(lef, def);

  Logger::initInst();
  DRCI.setDesignSource(nullptr, nullptr, nullptr);
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
  const auto entt_names = enttNetNames(*design);

  Logger::initInst();
  DRCI.setDesignSource(nullptr, nullptr, nullptr);
  DataManager::initInst();
  DRCI.wrapDatabase();
  const auto idb_env = DRCI.buildEnvShapeList();
  const auto idb_result = DRCI.buildResultShapeList();
  DataManager::destroyInst();

  DRCI.setDesignSource(design.get(), &tech, library.get());
  DataManager::initInst();
  DRCI.wrapDatabase();
  const auto entt_env = DRCI.buildEnvShapeList();
  const auto entt_result = DRCI.buildResultShapeList();
  DataManager::destroyInst();
  DRCI.setDesignSource(nullptr, nullptr, nullptr);

  expectShapeListsEqual("env", idb_env, idb_names, entt_env, entt_names);
  expectShapeListsEqual("result", idb_result, idb_names, entt_result, entt_names);
  const auto result_keys = canonicalShapes(idb_result, idb_names);
  EXPECT_NE(std::find(result_keys.begin(), result_keys.end(),
                      ShapeKey{.net_name = "a_sig", .layer_idx = 0, .is_routing = true, .ll_x = 50, .ll_y = 250, .ur_x = 550, .ur_y = 350}),
            result_keys.end())
      << "a_sig should retain its semantic net name";
  EXPECT_NE(std::find(result_keys.begin(), result_keys.end(),
                      ShapeKey{.net_name = "z_sig", .layer_idx = 0, .is_routing = true, .ll_x = 50, .ll_y = 50, .ur_x = 550, .ur_y = 150}),
            result_keys.end())
      << "z_sig should retain its semantic net name";

  auto run_violations = [&](bool use_entt_source, std::string_view temp_name) {
    if (use_entt_source) {
      DRCI.setDesignSource(design.get(), &tech, library.get());
    } else {
      DRCI.setDesignSource(nullptr, nullptr, nullptr);
    }
    std::map<std::string, std::any> config_map;
    config_map.insert({"-temp_directory_path", workspace.path(temp_name).string()});
    config_map.insert({"-thread_number", 1});
    DRCI.initDRC(config_map, true);
    const auto violations = DRCI.getViolationList(DRCI.buildEnvShapeList(), DRCI.buildResultShapeList(), {}, {});
    DRCI.destroyDRC();
    return violations;
  };

  const auto idb_violations = run_violations(false, "idb-drc");
  const auto entt_violations = run_violations(true, "entt-drc");
  DRCI.setDesignSource(nullptr, nullptr, nullptr);
  dmInst->reset();
  expectViolationListsEqual("violations", idb_violations, entt_violations);
}

TEST(Wrap, Ispd18Sample) { compareIdbAndEntt({"ispd18_sample"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Sample2) { compareIdbAndEntt({"ispd18_sample2"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Sample3) { compareIdbAndEntt({"ispd18_sample3"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test1) { compareIdbAndEntt({"ispd18_test1"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test2) { compareIdbAndEntt({"ispd18_test2"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test3) { compareIdbAndEntt({"ispd18_test3"}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test4) { compareIdbAndEntt({"ispd18_test4", true}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test5) { compareIdbAndEntt({"ispd18_test5", true}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test6) { compareIdbAndEntt({"ispd18_test6", true}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test7) { compareIdbAndEntt({"ispd18_test7", true}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test8) { compareIdbAndEntt({"ispd18_test8", true}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test9) { compareIdbAndEntt({"ispd18_test9", true}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd18Test10) { compareIdbAndEntt({"ispd18_test10", true}, ispd18Source(), "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus"); }
TEST(Wrap, Ispd19Sample)
{
  compareIdbAndEntt({"ispd19_sample"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Sample2)
{
  compareIdbAndEntt({"ispd19_sample2"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Sample3)
{
  compareIdbAndEntt({"ispd19_sample3"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Sample4)
{
  compareIdbAndEntt({"ispd19_sample4"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test1)
{
  compareIdbAndEntt({"ispd19_test1"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test2)
{
  compareIdbAndEntt({"ispd19_test2"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test3)
{
  compareIdbAndEntt({"ispd19_test3"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test4)
{
  compareIdbAndEntt({"ispd19_test4"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test5)
{
  compareIdbAndEntt({"ispd19_test5"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test6)
{
  compareIdbAndEntt({"ispd19_test6"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test7)
{
  compareIdbAndEntt({"ispd19_test7"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test8)
{
  compareIdbAndEntt({"ispd19_test8"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test9)
{
  compareIdbAndEntt({"ispd19_test9"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}
TEST(Wrap, Ispd19Test10)
{
  compareIdbAndEntt({"ispd19_test10"}, ispd19Source(), "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus");
}

}  // namespace idrc
