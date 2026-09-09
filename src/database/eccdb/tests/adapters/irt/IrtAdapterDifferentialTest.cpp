// SPDX-License-Identifier: MulanPSL-2.0

#include <gtest/gtest.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <any>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "DataManager.hpp"
#include "Database.hpp"
#include "DRCInterface.hpp"
#include "Logger.hpp"
#include "RTInterface.hpp"
#include "design/DesignStore.h"
#include "def/DefDesignExporter.h"
#include "idm.h"
#include "def/DefDesignImporter.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "library/LibraryStore.h"
#include "StructuredDesignSemanticDiffer.h"
#include "tech/TechStore.h"

namespace irt {
namespace {

std::filesystem::path envPath(const char* name)
{
  if (const char* value = std::getenv(name); value != nullptr && value[0] != '\0') {
    return value;
  }
  return {};
}

int32_t routingThreadNumber()
{
  constexpr int32_t kDefaultThreadNumber = 1;
  const char* value = std::getenv("ECCDB_RT_THREAD_NUMBER");
  if (value == nullptr || value[0] == '\0') {
    return kDefaultThreadNumber;
  }

  const std::string text(value);
  std::size_t consumed = 0;
  const int parsed = std::stoi(text, &consumed);
  if (consumed != text.size() || parsed <= 0) {
    throw std::invalid_argument("ECCDB_RT_THREAD_NUMBER must be a positive integer");
  }
  return parsed;
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

enum class IspdCorpus
{
  k2018,
  k2019
};

struct IspdRoutedWritebackCase
{
  std::string parameter_name;
  std::string design_name;
  IspdCorpus corpus = IspdCorpus::k2018;
  uint32_t component_count = 0;
  bool repair_metal9_prl = false;
  uint32_t component_limit = 100000;
};

void PrintTo(const IspdRoutedWritebackCase& test_case, std::ostream* output)
{
  *output << test_case.parameter_name << " (" << test_case.design_name << ", components=" << test_case.component_count << ')';
}

class TemporaryWorkspace
{
 public:
  TemporaryWorkspace()
  {
    _directory = std::filesystem::path(testing::TempDir()) / ("irt-adapter-diff-" + std::to_string(getpid()));
    std::filesystem::remove_all(_directory);
    std::filesystem::create_directories(_directory);
  }

  ~TemporaryWorkspace()
  {
    if (std::getenv("ECCDB_KEEP_TEMP") != nullptr) {
      std::cerr << "preserving temporary workspace: " << _directory << '\n';
      return;
    }
    std::error_code error;
    std::filesystem::remove_all(_directory, error);
  }

  [[nodiscard]] std::filesystem::path path(std::string_view name) const { return _directory / name; }

 private:
  std::filesystem::path _directory;
};

constexpr std::string_view kGeneratedViaLef = R"LEF(VERSION 5.8 ;
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

constexpr std::string_view kGeneratedViaDef = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN generated_via_wrap ;
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
NETS 0 ;
END NETS
END DESIGN
)DEF";

constexpr std::string_view kWritebackDef = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN irt_writeback ;
UNITS DISTANCE MICRONS 1000 ;

DIEAREA ( 0 0 ) ( 2000 2000 ) ;
ROW ROW0 CORE 0 0 N DO 10 BY 1 STEP 200 0 ;
TRACKS Y 100 DO 10 STEP 200 LAYER M1 ;
TRACKS X 100 DO 10 STEP 200 LAYER M2 ;
GCELLGRID X 0 DO 11 STEP 200 ;
GCELLGRID Y 0 DO 11 STEP 200 ;

COMPONENTS 0 ;
END COMPONENTS
PINS 4 ;
- A + NET a_net + DIRECTION INPUT + USE SIGNAL
  + PORT
    + LAYER M1 ( -50 -50 ) ( 50 50 )
    + PLACED ( 200 200 ) N
  ;
- B + NET a_net + DIRECTION OUTPUT + USE SIGNAL
  + PORT
    + LAYER M2 ( -50 -50 ) ( 50 50 )
    + PLACED ( 800 800 ) N
  ;
- Y + NET z_net + DIRECTION INPUT + USE SIGNAL
  + PORT
    + LAYER M2 ( -50 -50 ) ( 50 50 )
    + PLACED ( 1200 400 ) N
  ;
- Z + NET z_net + DIRECTION OUTPUT + USE SIGNAL
  + PORT
    + LAYER M2 ( -50 -50 ) ( 50 50 )
    + PLACED ( 1200 1400 ) N
  ;
END PINS
SPECIALNETS 1 ;
- VDD
  + USE POWER
  + ROUTED M1 100 ( 1000 1000 ) VIA12
  ;
END SPECIALNETS
NETS 2 ;
- z_net ( PIN Y ) ( PIN Z )
  + USE SIGNAL
  + ROUTED M1 ( 1200 400 ) ( 1400 400 )
  ;
- a_net ( PIN A ) ( PIN B )
  + USE SIGNAL
  + ROUTED M1 ( 200 200 ) ( 400 200 )
  ;
END NETS
END DESIGN
)DEF";

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

template <typename Callback>
std::string replaceAll(const std::string& text, const std::regex& pattern, Callback callback)
{
  std::string result;
  std::sregex_iterator begin(text.begin(), text.end(), pattern);
  std::sregex_iterator end;
  std::size_t last = 0;
  for (auto it = begin; it != end; ++it) {
    result.append(text, last, static_cast<std::size_t>(it->position()) - last);
    result += callback(*it);
    last = static_cast<std::size_t>(it->position() + it->length());
  }
  result.append(text, last, std::string::npos);
  return result;
}

void writeOriginShiftedDef(const std::filesystem::path& input, const std::filesystem::path& output)
{
  const std::string text = readText(input);
  static const std::regex diearea_re(R"(DIEAREA\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\))");
  std::smatch match;
  if (!std::regex_search(text, match, diearea_re)) {
    writeText(output, text);
    return;
  }

  const int32_t origin_x = std::stoi(match[1].str());
  const int32_t origin_y = std::stoi(match[2].str());
  if (origin_x == 0 && origin_y == 0) {
    writeText(output, text);
    return;
  }

  static const std::regex pair_re(R"(\(\s*(-?\d+)\s+(-?\d+)\s*\))");
  std::string shifted = replaceAll(text, pair_re, [origin_x, origin_y](const std::smatch& found) {
    const int32_t x = std::stoi(found[1].str()) - origin_x;
    const int32_t y = std::stoi(found[2].str()) - origin_y;
    return "( " + std::to_string(x) + " " + std::to_string(y) + " )";
  });

  static const std::regex row_re(R"((ROW\s+\S+\s+\S+\s+)(-?\d+)(\s+)(-?\d+))");
  shifted = replaceAll(shifted, row_re, [origin_x, origin_y](const std::smatch& found) {
    return found[1].str() + std::to_string(std::stoi(found[2].str()) - origin_x) + found[3].str()
           + std::to_string(std::stoi(found[4].str()) - origin_y);
  });

  static const std::regex track_re(R"((TRACKS\s+)([XY])(\s+)(-?\d+))");
  shifted = replaceAll(shifted, track_re, [origin_x, origin_y](const std::smatch& found) {
    const int32_t origin = found[2] == "X" ? origin_x : origin_y;
    return found[1].str() + found[2].str() + found[3].str() + std::to_string(std::stoi(found[4].str()) - origin);
  });

  static const std::regex gcell_re(R"((GCELLGRID\s+)([XY])(\s+)(-?\d+))");
  shifted = replaceAll(shifted, gcell_re, [origin_x, origin_y](const std::smatch& found) {
    const int32_t origin = found[2] == "X" ? origin_x : origin_y;
    return found[1].str() + found[2].str() + found[3].str() + std::to_string(std::stoi(found[4].str()) - origin);
  });

  writeText(output, shifted);
}

void writeUniformRoutingGridLef(const std::filesystem::path& input, const std::filesystem::path& output, std::string_view pitch,
                                std::string_view offset)
{
  const std::string text = readText(input);
  const std::string pitch_text{pitch};
  const std::string offset_text{offset};
  static const std::regex pitch_pair_re(R"((\bPITCH\s+)([0-9.]+)(\s+)([0-9.]+)(\s*;))");
  static const std::regex pitch_scalar_re(R"((\bPITCH\s+)([0-9.]+)(\s*;))");
  static const std::regex offset_re(R"(\n[ \t]*OFFSET\s+[0-9.]+(?:\s+[0-9.]+)?\s*;)");
  if (!std::regex_search(text, pitch_pair_re) && !std::regex_search(text, pitch_scalar_re)) {
    throw std::runtime_error("missing routing PITCH in " + input.string());
  }
  auto normalized = std::regex_replace(text, offset_re, "");
  normalized = replaceAll(normalized, pitch_pair_re, [&pitch_text, &offset_text](const std::smatch& found) {
    return found[1].str() + pitch_text + found[3].str() + pitch_text + found[5].str() + "\n  OFFSET " + offset_text + ' '
           + offset_text + " ;";
  });
  normalized = replaceAll(normalized, pitch_scalar_re, [&pitch_text, &offset_text](const std::smatch& found) {
    return found[1].str() + pitch_text + found[3].str() + "\n  OFFSET " + offset_text + " ;";
  });
  writeText(output, normalized);
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

std::pair<std::filesystem::path, std::filesystem::path> ispd19LefDef(const std::filesystem::path& root, std::string_view case_name)
{
  const auto directory = root / case_name;
  const auto lef = directory / (std::string(case_name) + ".input.lef");
  const auto def = directory / (std::string(case_name) + ".input.def");
  if (!std::filesystem::exists(lef) || !std::filesystem::exists(def)) {
    throw std::runtime_error("missing ISPD19 lef/def for " + std::string(case_name));
  }
  return {lef, def};
}

void loadIntoIdb(const std::filesystem::path& lef, const std::filesystem::path& def)
{
  dmInst->reset();
  if (!dmInst->readLef(std::vector<std::string>{lef.string()}, true)) {
    throw std::runtime_error("readLef failed: " + lef.string());
  }
  if (!dmInst->readDef(def.string())) {
    throw std::runtime_error("readDef failed: " + def.string());
  }
  auto& nets = dmInst->get_idb_def_service()->get_design()->get_net_list()->get_net_list();
  std::sort(nets.begin(), nets.end(), [](idb::IdbNet* lhs, idb::IdbNet* rhs) {
    if (lhs->get_net_name() != rhs->get_net_name()) {
      return lhs->get_net_name() < rhs->get_net_name();
    }
    return lhs->get_id() < rhs->get_id();
  });
}

class EnttDesignFixture
{
 public:
  EnttDesignFixture(const std::filesystem::path& lef, const std::filesystem::path& def)
      : library(technology.techRegistry()), design(technology.techRegistry(), library.libraryRegistry())
  {
    static_cast<void>(LefDefParser::lefrClear());
    static_cast<void>(LefDefParser::defrClear());
    eccdb::LefTechImporter(technology).import(lef);
    eccdb::LefLibraryImporter(technology, library).import(lef);
    eccdb::DefDesignImporter(design).import(def);
  }

  eccdb::TechStore technology;
  eccdb::LibraryStore library;
  eccdb::DesignStore design;
};

int32_t rtNetIndex(std::string_view name)
{
  auto& nets = RTDM.getDatabase().get_net_list();
  for (int32_t index = 0; index < static_cast<int32_t>(nets.size()); ++index) {
    if (nets[static_cast<std::size_t>(index)].get_net_name() == name) {
      return index;
    }
  }
  throw std::runtime_error("missing RT net: " + std::string{name});
}

int32_t rtRoutingLayerIndex(std::string_view name)
{
  auto& layers = RTDM.getDatabase().get_routing_layer_list();
  for (int32_t index = 0; index < static_cast<int32_t>(layers.size()); ++index) {
    if (layers[static_cast<std::size_t>(index)].get_layer_name() == name) {
      return index;
    }
  }
  throw std::runtime_error("missing RT routing layer: " + std::string{name});
}

void injectWritebackResults()
{
  const int32_t a_net = rtNetIndex("a_net");
  const int32_t z_net = rtNetIndex("z_net");
  const int32_t m1 = rtRoutingLayerIndex("M1");
  const int32_t m2 = rtRoutingLayerIndex("M2");

  auto& detailed_results = RTDM.getDatabase().get_net_detailed_result_map();
  detailed_results[z_net].emplace_back(LayerCoord{1200, 400, m2}, LayerCoord{1200, 1400, m2});
  detailed_results[a_net].emplace_back(LayerCoord{800, 200, m1}, LayerCoord{800, 200, m2});
  detailed_results[a_net].emplace_back(LayerCoord{200, 200, m1}, LayerCoord{800, 200, m1});

  auto& patch = RTDM.getDatabase().get_net_detailed_patch_map()[a_net].emplace_back();
  patch.set_layer_idx(m2);
  patch.set_real_rect(PlanarRect{750, 150, 850, 250});
}

void runInjectedWriteback(const std::filesystem::path& temp_directory, eccdb::DesignStore* design = nullptr,
                          eccdb::TechStore* technology = nullptr, eccdb::LibraryStore* library = nullptr)
{
  Logger::initInst();
  RTI.setDesignSource(design, technology, library);
  std::map<std::string, std::any> config{{"-temp_directory_path", temp_directory.string()},
                                         {"-thread_number", 1},
                                         {"-output_inter_result", 0},
                                         {"-enable_notification", 0},
                                         {"-enable_timing", 0}};
  RTI.initRT(config);
  injectWritebackResults();
  RTI.destroyRT();
  RTI.setDesignSource(nullptr, nullptr, nullptr);
}

void expectWritebackSemanticsEqual(const eccdb::DesignStore& expected, const eccdb::DesignStore& actual,
                                   bool compare_vias = true)
{
  const auto expected_snapshot = eccdb::test::makeDesignSemanticSnapshot(expected);
  const auto actual_snapshot = eccdb::test::makeDesignSemanticSnapshot(actual);
  EXPECT_EQ(expected_snapshot.component_counts, actual_snapshot.component_counts);
  EXPECT_EQ(expected_snapshot.global, actual_snapshot.global);
  EXPECT_EQ(expected_snapshot.track_grids, actual_snapshot.track_grids);
  EXPECT_EQ(expected_snapshot.gcell_grids, actual_snapshot.gcell_grids);
  EXPECT_EQ(expected_snapshot.instances, actual_snapshot.instances);
  EXPECT_EQ(expected_snapshot.instance_pins, actual_snapshot.instance_pins);
  EXPECT_EQ(expected_snapshot.io_pins, actual_snapshot.io_pins);
  if (compare_vias) {
    EXPECT_EQ(expected_snapshot.vias, actual_snapshot.vias);
  }
  EXPECT_EQ(expected_snapshot.non_default_rules, actual_snapshot.non_default_rules);
  EXPECT_EQ(expected_snapshot.nets, actual_snapshot.nets);
  EXPECT_EQ(expected_snapshot.regions, actual_snapshot.regions);
  EXPECT_EQ(expected_snapshot.groups, actual_snapshot.groups);
  EXPECT_EQ(expected_snapshot.blockages, actual_snapshot.blockages);
  EXPECT_EQ(expected_snapshot.fills, actual_snapshot.fills);
  eccdb::test::expectStructuredNetSemantics(expected, actual);
}

using DrcShapeKey = std::tuple<std::string, int32_t, bool, int32_t, int32_t, int32_t, int32_t>;

std::vector<std::string> enttNetNames(const eccdb::DesignStore& design)
{
  std::vector<std::string> names;
  for (const auto net : design.netlistStorage().regularNets()) {
    names.push_back(design.netlistStorage().net(net).name);
  }
  for (const auto net : design.netlistStorage().specialNets()) {
    names.push_back(design.netlistStorage().net(net).name);
  }
  return names;
}

std::vector<DrcShapeKey> canonicalDrcShapes(const std::vector<ids::Shape>& shapes, const std::vector<std::string>& net_names)
{
  std::vector<DrcShapeKey> result;
  result.reserve(shapes.size());
  for (const auto& shape : shapes) {
    const auto net_index = static_cast<size_t>(shape.net_idx);
    result.emplace_back(shape.net_idx >= 0 && net_index < net_names.size() ? net_names[net_index] : "<invalid>", shape.layer_idx,
                        shape.is_routing, shape.ll_x, shape.ll_y, shape.ur_x, shape.ur_y);
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::vector<eccdb::test::structured::CanonicalNet> canonicalSpecialNets(const eccdb::DesignStore& design)
{
  std::vector<eccdb::test::structured::CanonicalNet> result;
  for (const auto net : design.netlistStorage().specialNets()) {
    result.push_back(eccdb::test::structured::canonicalNet(design, net));
  }
  std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });
  return result;
}

Database wrapCurrentSource()
{
  DataManager::initInst();
  RTI.wrapDatabase();
  Database database = std::move(RTDM.getDatabase());
  DataManager::destroyInst();
  return database;
}

void expectWrappedEqual(Database& left, Database& right)
{
  const std::string mismatch = RTI.compareWrappedDatabase(left, right);
  ASSERT_TRUE(mismatch.empty()) << mismatch;
}

std::pair<Database, Database> wrapIdbAndEntt(const std::filesystem::path& lef, const std::filesystem::path& def)
{
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
  loadIntoIdb(lef, def);

  Logger::initInst();
  RTI.setDesignSource(nullptr, nullptr, nullptr);
  Database idb_database = wrapCurrentSource();
  RTI.setDesignSource(design.get(), &tech, library.get());
  Database entt_database = wrapCurrentSource();
  RTI.setDesignSource(nullptr, nullptr, nullptr);
  dmInst->reset();
  return {std::move(idb_database), std::move(entt_database)};
}

using RoutedSegmentKey = std::tuple<std::string, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t>;
using RoutedPatchKey = std::tuple<std::string, int32_t, int32_t, int32_t, int32_t, int32_t>;
using RoutedViolationKey = std::tuple<int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, bool, int32_t, std::vector<std::string>>;

struct RoutedSnapshot
{
  std::vector<RoutedSegmentKey> segments;
  std::vector<RoutedPatchKey> patches;
  std::vector<RoutedViolationKey> violations;
  std::map<int32_t, int64_t> routing_wire_length_dbu;
  std::map<int32_t, int32_t> cut_via_count;
  std::map<int32_t, int32_t> routing_patch_count;
  int64_t total_wire_length_dbu = 0;
  int32_t total_via_count = 0;
  int32_t total_patch_count = 0;
  int32_t within_net_violation_count = 0;
  int32_t among_net_violation_count = 0;
};

std::map<std::string, std::any> buildOnlyConfig(const std::filesystem::path& temp_directory);

RoutedSnapshot captureRoutedSnapshot()
{
  RoutedSnapshot snapshot;
  auto& database = RTDM.getDatabase();
  auto& die = database.get_die();
  auto& nets = database.get_net_list();
  auto netName = [&nets](int32_t net_idx) {
    if (net_idx >= 0 && net_idx < static_cast<int32_t>(nets.size())) {
      return nets[net_idx].get_net_name();
    }
    return std::string{"#"} + std::to_string(net_idx);
  };

  for (const auto& [net_idx, segment_set] : RTDM.getNetDetailedResultMap(die)) {
    for (const auto* segment : segment_set) {
      auto first = std::tuple{segment->get_first().get_x(), segment->get_first().get_y(), segment->get_first().get_layer_idx()};
      auto second = std::tuple{segment->get_second().get_x(), segment->get_second().get_y(), segment->get_second().get_layer_idx()};
      if (second < first) {
        std::swap(first, second);
      }
      snapshot.segments.emplace_back(netName(net_idx), std::get<0>(first), std::get<1>(first), std::get<2>(first), std::get<0>(second),
                                     std::get<1>(second), std::get<2>(second));
    }
  }
  for (const auto& [net_idx, patch_set] : RTDM.getNetDetailedPatchMap(die)) {
    for (const auto* patch : patch_set) {
      snapshot.patches.emplace_back(netName(net_idx), patch->get_layer_idx(), patch->get_real_ll_x(), patch->get_real_ll_y(),
                                    patch->get_real_ur_x(), patch->get_real_ur_y());
    }
  }
  for (const auto& violation : RTDM.getViolationList(die)) {
    const auto& shape = violation.get_violation_shape();
    std::vector<std::string> violation_nets;
    for (const int32_t net_idx : violation.get_violation_net_set()) {
      violation_nets.push_back(netName(net_idx));
    }
    std::sort(violation_nets.begin(), violation_nets.end());
    snapshot.violations.emplace_back(static_cast<int32_t>(violation.get_violation_type()), shape.get_layer_idx(), shape.get_real_ll_x(),
                                     shape.get_real_ll_y(), shape.get_real_ur_x(), shape.get_real_ur_y(), violation.get_is_routing(),
                                     violation.get_required_size(), std::move(violation_nets));
  }
  std::sort(snapshot.segments.begin(), snapshot.segments.end());
  std::sort(snapshot.patches.begin(), snapshot.patches.end());
  std::sort(snapshot.violations.begin(), snapshot.violations.end());

  // Summary wire lengths are accumulated as floating-point microns, so equal
  // geometry visited in a different net order can differ in the last bits.
  // Recompute the canonical comparison value directly from integer DBU
  // segment coordinates instead.
  for (const auto& routing_layer : database.get_routing_layer_list()) {
    snapshot.routing_wire_length_dbu[routing_layer.get_layer_idx()] = 0;
  }
  for (const auto& [net, x1, y1, layer1, x2, y2, layer2] : snapshot.segments) {
    if (layer1 != layer2) {
      continue;
    }
    const int64_t length = std::abs(static_cast<int64_t>(x2) - x1) + std::abs(static_cast<int64_t>(y2) - y1);
    snapshot.routing_wire_length_dbu[layer1] += length;
    snapshot.total_wire_length_dbu += length;
  }

  const auto& summary = database.get_summary().vr_summary;
  snapshot.cut_via_count = summary.cut_via_num_map;
  snapshot.routing_patch_count = summary.routing_patch_num_map;
  snapshot.total_via_count = summary.total_via_num;
  snapshot.total_patch_count = summary.total_patch_num;
  snapshot.within_net_violation_count = summary.within_net_total_violation_num;
  snapshot.among_net_violation_count = summary.among_net_total_violation_num;
  return snapshot;
}

RoutedSnapshot runIdbRoutingOnce(const std::filesystem::path& lef, const std::filesystem::path& def,
                                 const std::filesystem::path& temp_directory)
{
  loadIntoIdb(lef, def);
  RTI.setDesignSource(nullptr, nullptr, nullptr);
  std::map<std::string, std::any> config{{"-temp_directory_path", temp_directory.string()},
                                         {"-thread_number", routingThreadNumber()},
                                         {"-output_inter_result", 0},
                                         {"-enable_notification", 0},
                                         {"-enable_timing", 0}};
  RTI.initRT(config);
  RTI.runRT();
  RoutedSnapshot snapshot = captureRoutedSnapshot();
  RTI.destroyRT();
  RTI.setDesignSource(nullptr, nullptr, nullptr);
  dmInst->reset();
  return snapshot;
}

RoutedSnapshot runEnttRoutingOnce(const std::filesystem::path& lef, const std::filesystem::path& def,
                                  const std::filesystem::path& temp_directory)
{
  eccdb::TechStore tech;
  static_cast<void>(LefDefParser::lefrClear());
  static_cast<void>(LefDefParser::defrClear());
  eccdb::LefTechImporter(tech).import(lef);
  auto library = std::make_unique<eccdb::LibraryStore>(tech.techRegistry());
  eccdb::LefLibraryImporter(tech, *library).import(lef);
  auto design = std::make_unique<eccdb::DesignStore>(tech.techRegistry(), library->libraryRegistry());
  eccdb::DefDesignImporter(*design).import(def);

  RTI.setDesignSource(design.get(), &tech, library.get());
  std::map<std::string, std::any> config{{"-temp_directory_path", temp_directory.string()},
                                         {"-thread_number", routingThreadNumber()},
                                         {"-output_inter_result", 0},
                                         {"-enable_notification", 0},
                                         {"-enable_timing", 0}};
  RTI.initRT(config);
  RTI.runRT();
  RoutedSnapshot snapshot = captureRoutedSnapshot();
  RTI.destroyRT();
  RTI.setDesignSource(nullptr, nullptr, nullptr);
  dmInst->reset();
  return snapshot;
}

void expectRoutedSnapshotsEqual(const RoutedSnapshot& expected, const RoutedSnapshot& actual)
{
  EXPECT_EQ(expected.segments, actual.segments);
  EXPECT_EQ(expected.patches, actual.patches);
  EXPECT_EQ(expected.violations, actual.violations);
  EXPECT_EQ(expected.routing_wire_length_dbu, actual.routing_wire_length_dbu);
  EXPECT_EQ(expected.cut_via_count, actual.cut_via_count);
  EXPECT_EQ(expected.routing_patch_count, actual.routing_patch_count);
  EXPECT_EQ(expected.total_wire_length_dbu, actual.total_wire_length_dbu);
  EXPECT_EQ(expected.total_via_count, actual.total_via_count);
  EXPECT_EQ(expected.total_patch_count, actual.total_patch_count);
  EXPECT_EQ(expected.within_net_violation_count, actual.within_net_violation_count);
  EXPECT_EQ(expected.among_net_violation_count, actual.among_net_violation_count);
}

RoutedSnapshot runIdbRoutingAndWriteDef(const std::filesystem::path& lef, const std::filesystem::path& def,
                                        const std::filesystem::path& temp_directory, const std::filesystem::path& output_def)
{
  loadIntoIdb(lef, def);
  RTI.setDesignSource(nullptr, nullptr, nullptr);
  RTI.initRT(buildOnlyConfig(temp_directory));
  RTI.runRT();
  RoutedSnapshot snapshot = captureRoutedSnapshot();
  RTI.destroyRT();
  static_cast<void>(dmInst->saveDef(output_def.string()));
  if (!std::filesystem::is_regular_file(output_def) || std::filesystem::file_size(output_def) == 0u) {
    throw std::runtime_error("iDB did not write routed DEF: " + output_def.string());
  }
  RTI.setDesignSource(nullptr, nullptr, nullptr);
  dmInst->reset();
  return snapshot;
}

RoutedSnapshot runEnttRoutingAndWriteback(EnttDesignFixture& fixture, const std::filesystem::path& temp_directory)
{
  RTI.setDesignSource(&fixture.design, &fixture.technology, &fixture.library);
  RTI.initRT(buildOnlyConfig(temp_directory));
  RTI.runRT();
  RoutedSnapshot snapshot = captureRoutedSnapshot();
  RTI.destroyRT();
  RTI.setDesignSource(nullptr, nullptr, nullptr);
  dmInst->reset();
  return snapshot;
}

using BuiltRectKey = std::tuple<int32_t, int32_t, int32_t, int32_t, int32_t>;
using BuiltViaKey = std::pair<std::string, int32_t>;

struct BuiltOrderSnapshot
{
  std::vector<std::vector<BuiltViaKey>> vias;
  std::vector<std::string> nets;
  std::vector<std::vector<std::string>> pins;
  std::vector<BuiltRectKey> routing_obstacles;
  std::vector<BuiltRectKey> cut_obstacles;
};

BuiltRectKey builtRectKey(const EXTLayerRect& rect)
{
  return {rect.get_layer_idx(), rect.get_real_ll_x(), rect.get_real_ll_y(), rect.get_real_ur_x(), rect.get_real_ur_y()};
}

BuiltOrderSnapshot captureBuiltOrder()
{
  BuiltOrderSnapshot snapshot;
  auto& database = RTDM.getDatabase();

  for (auto& layer_vias : database.get_layer_via_master_list()) {
    auto& vias = snapshot.vias.emplace_back();
    for (auto& via : layer_vias) {
      vias.emplace_back(via.get_via_name(), via.get_via_master_idx().get_via_idx());
    }
  }
  for (auto& net : database.get_net_list()) {
    snapshot.nets.push_back(net.get_net_name());
    auto& pins = snapshot.pins.emplace_back();
    for (auto& pin : net.get_pin_list()) {
      pins.push_back(pin.get_pin_name());
    }
  }
  for (auto& obstacle : database.get_routing_obstacle_list()) {
    snapshot.routing_obstacles.push_back(builtRectKey(obstacle));
  }
  for (auto& obstacle : database.get_cut_obstacle_list()) {
    snapshot.cut_obstacles.push_back(builtRectKey(obstacle));
  }
  return snapshot;
}

std::string compareBuiltOrder(const BuiltOrderSnapshot& left, const BuiltOrderSnapshot& right)
{
  std::ostringstream out;
  auto compareSize = [&out](std::string_view tag, size_t left_size, size_t right_size) {
    if (left_size != right_size) {
      out << tag << " size " << left_size << " vs " << right_size << '\n';
    }
  };
  auto compareRectList = [&out, &compareSize](std::string_view tag, const std::vector<BuiltRectKey>& left_list,
                                               const std::vector<BuiltRectKey>& right_list) {
    compareSize(tag, left_list.size(), right_list.size());
    const size_t count = std::min(left_list.size(), right_list.size());
    for (size_t i = 0; i < count; ++i) {
      if (left_list[i] != right_list[i]) {
        out << tag << " first_diff=" << i << " left=" << std::get<0>(left_list[i]) << ',' << std::get<1>(left_list[i]) << ','
            << std::get<2>(left_list[i]) << ',' << std::get<3>(left_list[i]) << ',' << std::get<4>(left_list[i]) << " right="
            << std::get<0>(right_list[i]) << ',' << std::get<1>(right_list[i]) << ',' << std::get<2>(right_list[i]) << ','
            << std::get<3>(right_list[i]) << ',' << std::get<4>(right_list[i]) << '\n';
        break;
      }
    }
  };

  compareSize("via_layer_count", left.vias.size(), right.vias.size());
  for (size_t layer = 0; layer < std::min(left.vias.size(), right.vias.size()); ++layer) {
    compareSize("via[" + std::to_string(layer) + "]", left.vias[layer].size(), right.vias[layer].size());
    const size_t count = std::min(left.vias[layer].size(), right.vias[layer].size());
    for (size_t index = 0; index < count; ++index) {
      if (left.vias[layer][index] != right.vias[layer][index]) {
        out << "via[" << layer << "][" << index << "] left=" << left.vias[layer][index].first << "#" << left.vias[layer][index].second
            << " right=" << right.vias[layer][index].first << "#" << right.vias[layer][index].second << '\n';
        break;
      }
    }
  }

  compareSize("net", left.nets.size(), right.nets.size());
  size_t net_order_mismatch_count = 0;
  for (size_t index = 0; index < std::min(left.nets.size(), right.nets.size()); ++index) {
    if (left.nets[index] != right.nets[index]) {
      if (net_order_mismatch_count == 0) {
        out << "net[" << index << "] left=" << left.nets[index] << " right=" << right.nets[index] << '\n';
      }
      ++net_order_mismatch_count;
    }
  }
  out << "net_order_mismatch_count=" << net_order_mismatch_count << '\n';

  std::map<std::string, const std::vector<std::string>*> left_pins_by_net;
  std::map<std::string, const std::vector<std::string>*> right_pins_by_net;
  for (size_t index = 0; index < left.nets.size() && index < left.pins.size(); ++index) {
    left_pins_by_net[left.nets[index]] = &left.pins[index];
  }
  for (size_t index = 0; index < right.nets.size() && index < right.pins.size(); ++index) {
    right_pins_by_net[right.nets[index]] = &right.pins[index];
  }
  size_t pin_order_mismatch_count = 0;
  for (const auto& [net_name, left_pins] : left_pins_by_net) {
    const auto right_it = right_pins_by_net.find(net_name);
    if (right_it == right_pins_by_net.end()) {
      continue;
    }
    const auto& right_pins = *right_it->second;
    if (*left_pins == right_pins) {
      continue;
    }
    if (pin_order_mismatch_count == 0) {
      out << "pin_order net=" << net_name << " left_size=" << left_pins->size() << " right_size=" << right_pins.size();
      const size_t count = std::min(left_pins->size(), right_pins.size());
      for (size_t index = 0; index < count; ++index) {
        if ((*left_pins)[index] != right_pins[index]) {
          out << " first_diff=" << index << " left=" << (*left_pins)[index] << " right=" << right_pins[index];
          break;
        }
      }
      out << '\n';
    }
    ++pin_order_mismatch_count;
  }
  out << "pin_order_mismatch_count=" << pin_order_mismatch_count << '\n';

  compareRectList("routing_obstacle", left.routing_obstacles, right.routing_obstacles);
  compareRectList("cut_obstacle", left.cut_obstacles, right.cut_obstacles);
  auto sorted_routing_left = left.routing_obstacles;
  auto sorted_routing_right = right.routing_obstacles;
  auto sorted_cut_left = left.cut_obstacles;
  auto sorted_cut_right = right.cut_obstacles;
  std::sort(sorted_routing_left.begin(), sorted_routing_left.end());
  std::sort(sorted_routing_right.begin(), sorted_routing_right.end());
  std::sort(sorted_cut_left.begin(), sorted_cut_left.end());
  std::sort(sorted_cut_right.begin(), sorted_cut_right.end());
  out << "routing_obstacle_set_equal=" << (sorted_routing_left == sorted_routing_right ? "true" : "false") << '\n';
  out << "cut_obstacle_set_equal=" << (sorted_cut_left == sorted_cut_right ? "true" : "false") << '\n';
  return out.str();
}

std::map<std::string, std::any> buildOnlyConfig(const std::filesystem::path& temp_directory)
{
  return {{"-temp_directory_path", temp_directory.string()},
          {"-thread_number", routingThreadNumber()},
          {"-output_inter_result", 0},
          {"-enable_notification", 0},
          {"-enable_timing", 0}};
}

BuiltOrderSnapshot runIdbBuildOnce(const std::filesystem::path& lef, const std::filesystem::path& def,
                                   const std::filesystem::path& temp_directory)
{
  loadIntoIdb(lef, def);
  RTI.setDesignSource(nullptr, nullptr, nullptr);
  RTI.initRT(buildOnlyConfig(temp_directory));
  BuiltOrderSnapshot snapshot = captureBuiltOrder();
  RTI.destroyRT();
  RTI.setDesignSource(nullptr, nullptr, nullptr);
  dmInst->reset();
  return snapshot;
}

BuiltOrderSnapshot runEnttBuildOnce(const std::filesystem::path& lef, const std::filesystem::path& def,
                                    const std::filesystem::path& temp_directory)
{
  eccdb::TechStore tech;
  static_cast<void>(LefDefParser::lefrClear());
  static_cast<void>(LefDefParser::defrClear());
  eccdb::LefTechImporter(tech).import(lef);
  auto library = std::make_unique<eccdb::LibraryStore>(tech.techRegistry());
  eccdb::LefLibraryImporter(tech, *library).import(lef);
  auto design = std::make_unique<eccdb::DesignStore>(tech.techRegistry(), library->libraryRegistry());
  eccdb::DefDesignImporter(*design).import(def);

  RTI.setDesignSource(design.get(), &tech, library.get());
  RTI.initRT(buildOnlyConfig(temp_directory));
  BuiltOrderSnapshot snapshot = captureBuiltOrder();
  RTI.destroyRT();
  RTI.setDesignSource(nullptr, nullptr, nullptr);
  dmInst->reset();
  return snapshot;
}

void writeRoutedSnapshot(const std::filesystem::path& path, const RoutedSnapshot& snapshot)
{
  std::ostringstream output;
  for (const auto& [net, x1, y1, layer1, x2, y2, layer2] : snapshot.segments) {
    output << "segment\t" << net << '\t' << x1 << '\t' << y1 << '\t' << layer1 << '\t' << x2 << '\t' << y2 << '\t' << layer2 << '\n';
  }
  for (const auto& [net, layer, ll_x, ll_y, ur_x, ur_y] : snapshot.patches) {
    output << "patch\t" << net << '\t' << layer << '\t' << ll_x << '\t' << ll_y << '\t' << ur_x << '\t' << ur_y << '\n';
  }
  for (const auto& [type, layer, ll_x, ll_y, ur_x, ur_y, routing, required, nets] : snapshot.violations) {
    output << "violation\t" << type << '\t' << layer << '\t' << ll_x << '\t' << ll_y << '\t' << ur_x << '\t' << ur_y << '\t'
           << routing << '\t' << required;
    for (const std::string& net : nets) {
      output << '\t' << net;
    }
    output << '\n';
  }
  for (const auto& [layer, length] : snapshot.routing_wire_length_dbu) {
    output << "wire_length_dbu\t" << layer << '\t' << length << '\n';
  }
  for (const auto& [layer, count] : snapshot.cut_via_count) {
    output << "via_count\t" << layer << '\t' << count << '\n';
  }
  for (const auto& [layer, count] : snapshot.routing_patch_count) {
    output << "patch_count\t" << layer << '\t' << count << '\n';
  }
  output << "total_dbu\t" << snapshot.total_wire_length_dbu << '\t' << snapshot.total_via_count << '\t' << snapshot.total_patch_count << '\t'
         << snapshot.within_net_violation_count << '\t' << snapshot.among_net_violation_count << '\n';
  writeText(path, output.str());
}

std::vector<std::string> splitTabFields(const std::string& line)
{
  std::vector<std::string> fields;
  std::istringstream input(line);
  for (std::string field; std::getline(input, field, '\t');) {
    fields.push_back(std::move(field));
  }
  return fields;
}

RoutedSnapshot readRoutedSnapshot(const std::filesystem::path& path)
{
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to read routed snapshot: " + path.string());
  }

  RoutedSnapshot snapshot;
  for (std::string line; std::getline(input, line);) {
    const auto fields = splitTabFields(line);
    if (fields.empty()) {
      continue;
    }
    auto requireSize = [&fields, &path](std::size_t minimum) {
      if (fields.size() < minimum) {
        throw std::runtime_error("malformed routed snapshot record in " + path.string());
      }
    };
    if (fields[0] == "segment") {
      requireSize(8);
      snapshot.segments.emplace_back(fields[1], std::stoi(fields[2]), std::stoi(fields[3]), std::stoi(fields[4]), std::stoi(fields[5]),
                                     std::stoi(fields[6]), std::stoi(fields[7]));
    } else if (fields[0] == "patch") {
      requireSize(7);
      snapshot.patches.emplace_back(fields[1], std::stoi(fields[2]), std::stoi(fields[3]), std::stoi(fields[4]), std::stoi(fields[5]),
                                    std::stoi(fields[6]));
    } else if (fields[0] == "violation") {
      requireSize(9);
      std::vector<std::string> nets(fields.begin() + 9, fields.end());
      snapshot.violations.emplace_back(std::stoi(fields[1]), std::stoi(fields[2]), std::stoi(fields[3]), std::stoi(fields[4]),
                                       std::stoi(fields[5]), std::stoi(fields[6]), std::stoi(fields[7]) != 0, std::stoi(fields[8]),
                                       std::move(nets));
    } else if (fields[0] == "wire_length_dbu") {
      requireSize(3);
      snapshot.routing_wire_length_dbu[std::stoi(fields[1])] = std::stoll(fields[2]);
    } else if (fields[0] == "via_count") {
      requireSize(3);
      snapshot.cut_via_count[std::stoi(fields[1])] = std::stoi(fields[2]);
    } else if (fields[0] == "patch_count") {
      requireSize(3);
      snapshot.routing_patch_count[std::stoi(fields[1])] = std::stoi(fields[2]);
    } else if (fields[0] == "total_dbu") {
      requireSize(6);
      snapshot.total_wire_length_dbu = std::stoll(fields[1]);
      snapshot.total_via_count = std::stoi(fields[2]);
      snapshot.total_patch_count = std::stoi(fields[3]);
      snapshot.within_net_violation_count = std::stoi(fields[4]);
      snapshot.among_net_violation_count = std::stoi(fields[5]);
    } else {
      throw std::runtime_error("unknown routed snapshot record '" + fields[0] + "' in " + path.string());
    }
  }
  return snapshot;
}

template <typename Worker>
pid_t launchRoutingWorker(const std::filesystem::path& log_path, Worker&& worker)
{
  const pid_t pid = fork();
  if (pid < 0) {
    throw std::runtime_error("failed to fork routing worker");
  }
  if (pid != 0) {
    return pid;
  }

  const int log_fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (log_fd < 0 || dup2(log_fd, STDOUT_FILENO) < 0 || dup2(log_fd, STDERR_FILENO) < 0) {
    _exit(125);
  }
  close(log_fd);
  try {
    worker();
    std::cout.flush();
    std::cerr.flush();
    _exit(0);
  } catch (const std::exception& error) {
    dprintf(STDERR_FILENO, "routing worker failed: %s\n", error.what());
  } catch (...) {
    dprintf(STDERR_FILENO, "routing worker failed with an unknown exception\n");
  }
  _exit(1);
}

int waitRoutingWorker(pid_t pid)
{
  int status = 0;
  pid_t waited = -1;
  do {
    waited = waitpid(pid, &status, 0);
  } while (waited < 0 && errno == EINTR);
  if (waited != pid) {
    throw std::runtime_error("failed to wait for routing worker");
  }
  return status;
}

bool workerSucceeded(int status)
{
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::pair<std::filesystem::path, std::filesystem::path> routedCaseLefDef(const IspdRoutedWritebackCase& test_case,
                                                                        const std::filesystem::path& root,
                                                                        const std::filesystem::path& repair_workspace)
{
  if (test_case.corpus == IspdCorpus::k2018) {
    return ispd18LefDef(root, Ispd18Case{test_case.design_name, test_case.repair_metal9_prl}, repair_workspace);
  }
  return ispd19LefDef(root, test_case.design_name);
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
  const auto [lef, original_def] = ispd18LefDef(root, test_case, repair_workspace);
  const auto shifted_def = workspace.path(test_case.name + ".shifted.def");
  writeOriginShiftedDef(original_def, shifted_def);

  auto [idb_database, entt_database] = wrapIdbAndEntt(lef, shifted_def);
  expectWrappedEqual(idb_database, entt_database);
}

class IspdRoutedWritebackDifferentialTest : public testing::TestWithParam<IspdRoutedWritebackCase>
{
};

TEST_P(IspdRoutedWritebackDifferentialTest, NativeEnttMatchesLegacyIdbAfterRouting)
{
  const auto& test_case = GetParam();
  const auto root = test_case.corpus == IspdCorpus::k2018 ? ispd18Source() : ispd19Source();
  if (root.empty()) {
    GTEST_SKIP() << "set " << (test_case.corpus == IspdCorpus::k2018 ? "ECCDB_ISPD18_ROOT" : "ECCDB_ISPD19_ROOT")
                 << " to the extracted ISPD corpus";
  }

  TemporaryWorkspace workspace;
  const auto repair_workspace = workspace.path("repair");
  std::filesystem::create_directories(repair_workspace);
  const auto [original_lef, original_def] = routedCaseLefDef(test_case, root, repair_workspace);
  const auto compatible_lef = workspace.path(test_case.parameter_name + "-uniform-pitch.lef");
  const auto input_def = workspace.path(test_case.parameter_name + "-origin-zero.def");
  const auto idb_output_def = workspace.path(test_case.parameter_name + "-idb-routed.def");
  const auto entt_output_def = workspace.path(test_case.parameter_name + "-entt-routed.def");
  const auto idb_snapshot_path = workspace.path(test_case.parameter_name + "-idb.snapshot");
  const auto entt_snapshot_path = workspace.path(test_case.parameter_name + "-entt.snapshot");
  const auto idb_log = workspace.path(test_case.parameter_name + "-idb.log");
  const auto entt_log = workspace.path(test_case.parameter_name + "-entt.log");
  writeUniformRoutingGridLef(original_lef, compatible_lef, "0.100000", "0.050000");
  writeOriginShiftedDef(original_def, input_def);

  const pid_t idb_pid = launchRoutingWorker(idb_log, [&] {
    const auto snapshot = runIdbRoutingAndWriteDef(compatible_lef, input_def, workspace.path("idb-run"), idb_output_def);
    writeRoutedSnapshot(idb_snapshot_path, snapshot);
  });
  const pid_t entt_pid = launchRoutingWorker(entt_log, [&] {
    EnttDesignFixture fixture(compatible_lef, input_def);
    const auto snapshot = runEnttRoutingAndWriteback(fixture, workspace.path("entt-run"));
    eccdb::DefDesignExporter(fixture.design).write(entt_output_def);
    writeRoutedSnapshot(entt_snapshot_path, snapshot);
  });

  const int idb_status = waitRoutingWorker(idb_pid);
  const int entt_status = waitRoutingWorker(entt_pid);
  ASSERT_TRUE(workerSucceeded(idb_status)) << "iDB routing worker failed; inspect " << idb_log;
  ASSERT_TRUE(workerSucceeded(entt_status)) << "EnTT routing worker failed; inspect " << entt_log;

  const auto idb_snapshot = readRoutedSnapshot(idb_snapshot_path);
  const auto entt_snapshot = readRoutedSnapshot(entt_snapshot_path);
  SCOPED_TRACE(test_case.parameter_name);
  expectRoutedSnapshotsEqual(idb_snapshot, entt_snapshot);

  EnttDesignFixture input(compatible_lef, input_def);
  EnttDesignFixture actual(compatible_lef, entt_output_def);
  ASSERT_EQ(actual.design.netlistStorage().instanceCount(), test_case.component_count);
  ASSERT_LE(actual.design.netlistStorage().instanceCount(), test_case.component_limit);
  EXPECT_EQ(canonicalSpecialNets(actual.design), canonicalSpecialNets(input.design));
  EXPECT_GT(actual.design.routingStorage().wireCount(), 0u);

  EnttDesignFixture expected(compatible_lef, idb_output_def);
  // iRT does not modify design VIA definitions. The legacy iDB DEF writer does
  // not preserve their ordering and all generated-VIA metadata, so compare the
  // routed NET references here and leave full VIA round-trip coverage to EnTT.
  expectWritebackSemanticsEqual(expected.design, actual.design, false);

  DRCI.setDesignSource(&expected.design, &expected.technology, &expected.library);
  const auto idb_shapes = canonicalDrcShapes(DRCI.buildResultShapeList(), enttNetNames(expected.design));
  DRCI.setDesignSource(&actual.design, &actual.technology, &actual.library);
  const auto entt_shapes = canonicalDrcShapes(DRCI.buildResultShapeList(), enttNetNames(actual.design));
  DRCI.setDesignSource(nullptr, nullptr, nullptr);
  EXPECT_EQ(entt_shapes, idb_shapes);
}

INSTANTIATE_TEST_SUITE_P(
    IspdUnder100k, IspdRoutedWritebackDifferentialTest,
    testing::Values(IspdRoutedWritebackCase{"Ispd18Sample", "ispd18_sample", IspdCorpus::k2018, 22},
                    IspdRoutedWritebackCase{"Ispd18Sample2", "ispd18_sample2", IspdCorpus::k2018, 23},
                    IspdRoutedWritebackCase{"Ispd18Sample3", "ispd18_sample3", IspdCorpus::k2018, 6},
                    IspdRoutedWritebackCase{"Ispd18Test1", "ispd18_test1", IspdCorpus::k2018, 8879},
                    IspdRoutedWritebackCase{"Ispd18Test2", "ispd18_test2", IspdCorpus::k2018, 35913},
                    IspdRoutedWritebackCase{"Ispd18Test3", "ispd18_test3", IspdCorpus::k2018, 35977},
                    IspdRoutedWritebackCase{"Ispd18Test4", "ispd18_test4", IspdCorpus::k2018, 72094, true},
                    IspdRoutedWritebackCase{"Ispd18Test5", "ispd18_test5", IspdCorpus::k2018, 71954, true},
                    IspdRoutedWritebackCase{"Ispd19Sample", "ispd19_sample", IspdCorpus::k2019, 22},
                    IspdRoutedWritebackCase{"Ispd19Sample2", "ispd19_sample2", IspdCorpus::k2019, 23},
                    IspdRoutedWritebackCase{"Ispd19Sample3", "ispd19_sample3", IspdCorpus::k2019, 6},
                    IspdRoutedWritebackCase{"Ispd19Sample4", "ispd19_sample4", IspdCorpus::k2019, 67},
                    IspdRoutedWritebackCase{"Ispd19Test1", "ispd19_test1", IspdCorpus::k2019, 8879},
                    IspdRoutedWritebackCase{"Ispd19Test2", "ispd19_test2", IspdCorpus::k2019, 72094},
                    IspdRoutedWritebackCase{"Ispd19Test3", "ispd19_test3", IspdCorpus::k2019, 8283},
                    IspdRoutedWritebackCase{"Ispd19Test5", "ispd19_test5", IspdCorpus::k2019, 28920}),
    [](const testing::TestParamInfo<IspdRoutedWritebackCase>& info) { return info.param.parameter_name; });

INSTANTIATE_TEST_SUITE_P(
    IspdAround100k, IspdRoutedWritebackDifferentialTest,
    testing::Values(IspdRoutedWritebackCase{"Ispd18Test6", "ispd18_test6", IspdCorpus::k2018, 107919, true, 120000}),
    [](const testing::TestParamInfo<IspdRoutedWritebackCase>& info) { return info.param.parameter_name; });

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
  const auto [lef, original_def] = ispd18LefDef(root, test_case, repair_workspace);
  const auto shifted_def = workspace.path(test_case.name + ".shifted.def");
  writeOriginShiftedDef(original_def, shifted_def);
  loadIntoIdb(lef, shifted_def);

  Logger::initInst();
  RTI.setDesignSource(nullptr, nullptr, nullptr);
  Database left = wrapCurrentSource();
  Database right = wrapCurrentSource();
  expectWrappedEqual(left, right);
  dmInst->reset();
}

TEST(Writeback, NativeEnttMatchesLegacyIdbDefAndRoundTrips)
{
  TemporaryWorkspace workspace;
  const auto lef = workspace.path("writeback.lef");
  const auto input_def = workspace.path("writeback.def");
  const auto idb_output_def = workspace.path("idb-output.def");
  const auto entt_output_def = workspace.path("entt-output.def");
  writeText(lef, kGeneratedViaLef);
  writeText(input_def, kWritebackDef);

  loadIntoIdb(lef, input_def);
  runInjectedWriteback(workspace.path("idb-run"));
  static_cast<void>(dmInst->saveDef(idb_output_def.string()));
  ASSERT_TRUE(std::filesystem::is_regular_file(idb_output_def));
  ASSERT_GT(std::filesystem::file_size(idb_output_def), 0u);
  dmInst->reset();

  EnttDesignFixture actual(lef, input_def);
  const auto special_id = actual.design.netlistStorage().findSpecialNet("VDD");
  ASSERT_TRUE(special_id);
  const auto special_before = eccdb::test::structured::canonicalNet(actual.design, special_id);
  runInjectedWriteback(workspace.path("entt-run"), &actual.design, &actual.technology, &actual.library);

  EnttDesignFixture expected(lef, idb_output_def);
  expectWritebackSemanticsEqual(expected.design, actual.design);
  EXPECT_EQ(special_before, eccdb::test::structured::canonicalNet(actual.design, special_id));

  eccdb::DefDesignExporter(actual.design).write(entt_output_def);
  eccdb::DesignStore roundtripped(actual.technology.techRegistry(), actual.library.libraryRegistry());
  eccdb::DefDesignImporter(roundtripped).import(entt_output_def);
  expectWritebackSemanticsEqual(actual.design, roundtripped);

  const auto first_export = eccdb::DefDesignExporter(actual.design).exportText();
  const auto first_wire_count = actual.design.routingStorage().wireCount();
  const auto first_track_count = actual.design.floorplanStorage().trackGridCount();
  const auto first_gcell_count = actual.design.floorplanStorage().gcellGridCount();
  const auto first_pool = actual.design.routingStorage().routingPoolStatistics();
  runInjectedWriteback(workspace.path("entt-repeat-run"), &actual.design, &actual.technology, &actual.library);
  const auto second_pool = actual.design.routingStorage().routingPoolStatistics();
  EXPECT_EQ(actual.design.routingStorage().wireCount(), first_wire_count);
  EXPECT_EQ(actual.design.floorplanStorage().trackGridCount(), first_track_count);
  EXPECT_EQ(actual.design.floorplanStorage().gcellGridCount(), first_gcell_count);
  EXPECT_EQ(eccdb::DefDesignExporter(actual.design).exportText(), first_export);
  EXPECT_EQ(special_before, eccdb::test::structured::canonicalNet(actual.design, special_id));
  expectWritebackSemanticsEqual(expected.design, actual.design);
  std::cout << "[EnTT writeback pool] paths=" << first_pool.paths.count << "->" << second_pool.paths.count
            << " points=" << first_pool.points.count << "->" << second_pool.points.count << " vias=" << first_pool.vias.count << "->"
            << second_pool.vias.count << " rectangles=" << first_pool.rectangles.count << "->" << second_pool.rectangles.count << '\n';

  loadIntoIdb(lef, idb_output_def);
  DRCI.setDesignSource(nullptr, nullptr, nullptr);
  const auto idb_names = [&]() {
    std::vector<std::string> names;
    auto* idb_design = dmInst->get_idb_def_service()->get_design();
    for (auto* net : idb_design->get_net_list()->get_net_list()) {
      names.push_back(net->get_net_name());
    }
    for (auto* net : idb_design->get_special_net_list()->get_net_list()) {
      names.push_back(net->get_net_name());
    }
    return names;
  }();
  const auto idb_shapes = canonicalDrcShapes(DRCI.buildResultShapeList(), idb_names);
  DRCI.setDesignSource(&actual.design, &actual.technology, &actual.library);
  const auto entt_shapes = canonicalDrcShapes(DRCI.buildResultShapeList(), enttNetNames(actual.design));
  DRCI.setDesignSource(nullptr, nullptr, nullptr);
  dmInst->reset();
  EXPECT_EQ(entt_shapes, idb_shapes);
}

TEST(Diagnostic, DISABLED_Ispd19Test2BuildOrder)
{
  const auto root = ispd19Source();
  if (root.empty()) {
    GTEST_SKIP() << "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus";
  }

  TemporaryWorkspace workspace;
  const auto [original_lef, def] = ispd19LefDef(root, "ispd19_test2");
  const auto compatible_lef = workspace.path("test2-uniform-pitch.lef");
  writeUniformRoutingGridLef(original_lef, compatible_lef, "0.100000", "0.050000");

  const auto idb = runIdbBuildOnce(compatible_lef, def, workspace.path("idb-build"));
  const auto entt = runEnttBuildOnce(compatible_lef, def, workspace.path("entt-build"));
  const std::string mismatch = compareBuiltOrder(idb, entt);
  std::cout << "[BUILD_ORDER_DIAGNOSTIC]\n" << (mismatch.empty() ? "equal\n" : mismatch);
  SUCCEED();
}

TEST(RunSnapshot, Ispd18Sample3IdbOnce)
{
  const auto root = ispd18Source();
  if (root.empty()) {
    GTEST_SKIP() << "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus";
  }
  TemporaryWorkspace workspace;
  const auto [original_lef, def] = ispd18LefDef(root, {"ispd18_sample3"}, workspace.path("repair"));
  const auto compatible_lef = workspace.path("sample3-uniform-pitch.lef");
  writeUniformRoutingGridLef(original_lef, compatible_lef, "0.100000", "0.050000");
  const auto snapshot = runIdbRoutingOnce(compatible_lef, def, workspace.path("run"));
  const auto output = envPath("ECCDB_RT_SNAPSHOT_OUT");
  if (output.empty()) {
    GTEST_FAIL() << "set ECCDB_RT_SNAPSHOT_OUT to the snapshot output path";
  }
  writeRoutedSnapshot(output, snapshot);
}

TEST(RunSnapshot, Ispd18Sample3EnttOnce)
{
  const auto root = ispd18Source();
  if (root.empty()) {
    GTEST_SKIP() << "set ECCDB_ISPD18_ROOT to the extracted ISPD18 corpus";
  }
  TemporaryWorkspace workspace;
  const auto [original_lef, def] = ispd18LefDef(root, {"ispd18_sample3"}, workspace.path("repair"));
  const auto compatible_lef = workspace.path("sample3-uniform-pitch.lef");
  writeUniformRoutingGridLef(original_lef, compatible_lef, "0.100000", "0.050000");
  const auto snapshot = runEnttRoutingOnce(compatible_lef, def, workspace.path("run"));
  const auto output = envPath("ECCDB_RT_SNAPSHOT_OUT");
  if (output.empty()) {
    GTEST_FAIL() << "set ECCDB_RT_SNAPSHOT_OUT to the snapshot output path";
  }
  writeRoutedSnapshot(output, snapshot);
}

TEST(RunSnapshot, Ispd19Test9IdbOnce)
{
  const auto root = ispd19Source();
  if (root.empty()) {
    GTEST_SKIP() << "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus";
  }
  TemporaryWorkspace workspace;
  const auto [original_lef, def] = ispd18LefDef(root, {"ispd19_test9"}, workspace.path("repair"));
  const auto compatible_lef = workspace.path("test9-uniform-pitch.lef");
  writeUniformRoutingGridLef(original_lef, compatible_lef, "0.100000", "0.050000");
  const auto snapshot = runIdbRoutingOnce(compatible_lef, def, workspace.path("run"));
  const auto output = envPath("ECCDB_RT_SNAPSHOT_OUT");
  if (output.empty()) {
    GTEST_FAIL() << "set ECCDB_RT_SNAPSHOT_OUT to the snapshot output path";
  }
  writeRoutedSnapshot(output, snapshot);
}

TEST(RunSnapshot, Ispd19Test2IdbOnce)
{
  const auto root = ispd19Source();
  if (root.empty()) {
    GTEST_SKIP() << "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus";
  }
  TemporaryWorkspace workspace;
  const auto [original_lef, def] = ispd19LefDef(root, "ispd19_test2");
  const auto compatible_lef = workspace.path("test2-uniform-pitch.lef");
  writeUniformRoutingGridLef(original_lef, compatible_lef, "0.100000", "0.050000");
  const auto snapshot = runIdbRoutingOnce(compatible_lef, def, workspace.path("run"));
  const auto output = envPath("ECCDB_RT_SNAPSHOT_OUT");
  if (output.empty()) {
    GTEST_FAIL() << "set ECCDB_RT_SNAPSHOT_OUT to the snapshot output path";
  }
  writeRoutedSnapshot(output, snapshot);
}

TEST(RunSnapshot, Ispd19Test2EnttOnce)
{
  const auto root = ispd19Source();
  if (root.empty()) {
    GTEST_SKIP() << "set ECCDB_ISPD19_ROOT to the extracted ISPD19 corpus";
  }
  TemporaryWorkspace workspace;
  const auto [original_lef, def] = ispd19LefDef(root, "ispd19_test2");
  const auto compatible_lef = workspace.path("test2-uniform-pitch.lef");
  writeUniformRoutingGridLef(original_lef, compatible_lef, "0.100000", "0.050000");
  const auto snapshot = runEnttRoutingOnce(compatible_lef, def, workspace.path("run"));
  const auto output = envPath("ECCDB_RT_SNAPSHOT_OUT");
  if (output.empty()) {
    GTEST_FAIL() << "set ECCDB_RT_SNAPSHOT_OUT to the snapshot output path";
  }
  writeRoutedSnapshot(output, snapshot);
}

TEST(Wrap, GeneratedDefViaMaterializesAtSpecialNetPoint)
{
  TemporaryWorkspace workspace;
  const auto lef = workspace.path("generated-via.lef");
  const auto def = workspace.path("generated-via.def");
  writeText(lef, kGeneratedViaLef);
  writeText(def, kGeneratedViaDef);

  auto [idb_database, entt_database] = wrapIdbAndEntt(lef, def);
  EXPECT_EQ(idb_database.get_routing_obstacle_list().size(), 2u);
  EXPECT_EQ(entt_database.get_routing_obstacle_list().size(), 2u);
  EXPECT_EQ(idb_database.get_cut_obstacle_list().size(), 6u);
  EXPECT_EQ(entt_database.get_cut_obstacle_list().size(), 6u);
  expectWrappedEqual(idb_database, entt_database);
}

TEST(Writeback, IdbGeneratedViaPreservesOriginAndOffset)
{
  TemporaryWorkspace workspace;
  const auto lef = workspace.path("generated-via.lef");
  const auto def = workspace.path("generated-via-with-offset.def");
  const auto output = workspace.path("generated-via-with-offset-output.def");
  writeText(lef, kGeneratedViaLef);
  auto def_text = std::string{kGeneratedViaDef};
  const auto row_col = def_text.find("  + ROWCOL 2 3\n");
  ASSERT_NE(row_col, std::string::npos);
  def_text.insert(row_col + std::string_view{"  + ROWCOL 2 3\n"}.size(), "  + ORIGIN 10 -20\n  + OFFSET 3 4 -5 -6\n");
  writeText(def, def_text);

  loadIntoIdb(lef, def);
  ASSERT_TRUE(dmInst->saveDef(output.string()));
  const auto output_text = readText(output);
  EXPECT_NE(output_text.find("+ ORIGIN 10 -20"), std::string::npos);
  EXPECT_NE(output_text.find("+ OFFSET 3 4 -5 -6"), std::string::npos);
  dmInst->reset();
}

TEST(Wrap, ExternalLefDef)
{
  const auto lef = envPath("ECCDB_IRT_WRAP_LEF");
  const auto def = envPath("ECCDB_IRT_WRAP_DEF");
  if (lef.empty() || def.empty()) {
    GTEST_SKIP() << "set ECCDB_IRT_WRAP_LEF and ECCDB_IRT_WRAP_DEF";
  }
  if (!std::filesystem::is_regular_file(lef) || !std::filesystem::is_regular_file(def)) {
    throw std::runtime_error("missing external wrapper differential LEF/DEF");
  }

  auto [idb_database, entt_database] = wrapIdbAndEntt(lef, def);
  expectWrappedEqual(idb_database, entt_database);
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

}  // namespace irt
