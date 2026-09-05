// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "DesignSemanticSnapshot.h"
#include "StructuredDesignSemanticDiffer.h"
#include "design/DesignStore.h"
#include "def/DefDesignExporter.h"
#include "def/DefDesignImporter.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

std::filesystem::path openDbTclExecutable()
{
  if (const char* value = std::getenv("OPENDB_TCL"); value != nullptr && value[0] != '\0') {
    return value;
  }
#ifdef OPENDB_TCL_DEFAULT
  return OPENDB_TCL_DEFAULT;
#else
  return {};
#endif
}

std::filesystem::path openDbPythonExecutable()
{
  if (const char* value = std::getenv("OPENDB_PYTHON"); value != nullptr && value[0] != '\0') {
    return value;
  }
#ifdef OPENDB_PYTHON_DEFAULT
  return OPENDB_PYTHON_DEFAULT;
#else
  return {};
#endif
}

std::filesystem::path openDbPythonPath()
{
  if (const char* value = std::getenv("OPENDB_PYTHONPATH"); value != nullptr && value[0] != '\0') {
    return value;
  }
#ifdef OPENDB_PYTHONPATH_DEFAULT
  return OPENDB_PYTHONPATH_DEFAULT;
#else
  return {};
#endif
}

std::filesystem::path openRoadSource()
{
  if (const char* value = std::getenv("OPENROAD_SOURCE_DIR"); value != nullptr && value[0] != '\0') {
    return value;
  }
#ifdef OPENROAD_SOURCE_DEFAULT
  return OPENROAD_SOURCE_DEFAULT;
#else
  return {};
#endif
}

std::filesystem::path ispd18Source()
{
  if (const char* value = std::getenv("ECCDB_ISPD18_ROOT"); value != nullptr && value[0] != '\0') {
    return value;
  }
  return {};
}

std::filesystem::path ispd19Source()
{
  if (const char* value = std::getenv("ECCDB_ISPD19_ROOT"); value != nullptr && value[0] != '\0') {
    return value;
  }
  return {};
}

class TemporaryWorkspace
{
 public:
  TemporaryWorkspace()
  {
    _directory = std::filesystem::path(testing::TempDir()) / ("idb-refactor-opendb-diff-" + std::to_string(getpid()));
    std::filesystem::remove_all(_directory);
    std::filesystem::create_directories(_directory);
  }

  ~TemporaryWorkspace()
  {
    std::error_code error;
    std::filesystem::remove_all(_directory, error);
  }

  std::filesystem::path write(std::string_view name, std::string_view contents) const
  {
    const auto path = _directory / name;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
      throw std::runtime_error("cannot create differential-test input: " + path.string());
    }
    output << contents;
    if (!output) {
      throw std::runtime_error("cannot write differential-test input: " + path.string());
    }
    return path;
  }

  [[nodiscard]] std::filesystem::path path(std::string_view name) const { return _directory / name; }

 private:
  std::filesystem::path _directory;
};

std::string readText(const std::filesystem::path& path)
{
  std::ifstream input(path, std::ios::binary);
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

int runOpenDb(const std::filesystem::path& executable, const std::filesystem::path& lef, const std::filesystem::path& input_def,
              const std::filesystem::path& output_def, const std::filesystem::path& log)
{
  const pid_t child = fork();
  if (child < 0) {
    throw std::runtime_error("fork failed while starting OpenDB");
  }
  if (child == 0) {
    const auto log_file = std::fopen(log.c_str(), "w");
    if (log_file != nullptr) {
      dup2(fileno(log_file), STDOUT_FILENO);
      dup2(fileno(log_file), STDERR_FILENO);
    }
    execl(executable.c_str(), executable.c_str(), OPENDB_NORMALIZE_DEF_SCRIPT, lef.c_str(), input_def.c_str(), output_def.c_str(),
          static_cast<char*>(nullptr));
    _exit(127);
  }

  int status = 0;
  if (waitpid(child, &status, 0) != child) {
    throw std::runtime_error("waitpid failed while running OpenDB");
  }
  if (!WIFEXITED(status)) {
    return 128;
  }
  return WEXITSTATUS(status);
}

int runOpenDbPython(const std::filesystem::path& executable, const std::filesystem::path& module_directory,
                    const std::vector<std::filesystem::path>& lefs, const std::filesystem::path& input_def,
                    const std::filesystem::path& output_def, const std::filesystem::path& log)
{
  const pid_t child = fork();
  if (child < 0) {
    throw std::runtime_error("fork failed while starting OpenDB Python");
  }
  if (child == 0) {
    const auto log_file = std::fopen(log.c_str(), "w");
    if (log_file != nullptr) {
      dup2(fileno(log_file), STDOUT_FILENO);
      dup2(fileno(log_file), STDERR_FILENO);
    }
    setenv("PYTHONPATH", module_directory.c_str(), 1);

    std::vector<std::string> arguments{executable.string(), OPENDB_NORMALIZE_DEF_PYTHON_SCRIPT, input_def.string(), output_def.string()};
    for (const auto& lef : lefs) {
      arguments.push_back(lef.string());
    }
    std::vector<char*> argument_pointers;
    argument_pointers.reserve(arguments.size() + 1u);
    for (auto& argument : arguments) {
      argument_pointers.push_back(argument.data());
    }
    argument_pointers.push_back(nullptr);
    execv(executable.c_str(), argument_pointers.data());
    _exit(127);
  }

  int status = 0;
  if (waitpid(child, &status, 0) != child) {
    throw std::runtime_error("waitpid failed while running OpenDB Python");
  }
  if (!WIFEXITED(status)) {
    return 128;
  }
  return WEXITSTATUS(status);
}

bool hasOpenDbRuntime()
{
  const auto python = openDbPythonExecutable();
  const auto module_directory = openDbPythonPath();
  if (!python.empty() && std::filesystem::is_regular_file(python) && std::filesystem::is_regular_file(module_directory / "odb.py")
      && std::filesystem::is_regular_file(module_directory / "_odb.so")) {
    return true;
  }
  const auto odbtcl = openDbTclExecutable();
  return !odbtcl.empty() && std::filesystem::is_regular_file(odbtcl);
}

int normalizeWithOpenDb(const std::vector<std::filesystem::path>& lefs, const std::filesystem::path& input_def,
                        const std::filesystem::path& output_def, const std::filesystem::path& log)
{
  const auto python = openDbPythonExecutable();
  const auto module_directory = openDbPythonPath();
  if (!python.empty() && std::filesystem::is_regular_file(python) && std::filesystem::is_regular_file(module_directory / "odb.py")
      && std::filesystem::is_regular_file(module_directory / "_odb.so")) {
    return runOpenDbPython(python, module_directory, lefs, input_def, output_def, log);
  }

  const auto odbtcl = openDbTclExecutable();
  if (!odbtcl.empty() && std::filesystem::is_regular_file(odbtcl) && lefs.size() == 1u) {
    return runOpenDb(odbtcl, lefs.front(), input_def, output_def, log);
  }
  return 127;
}

int runSi2Reader(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
                 const std::filesystem::path& output, const std::filesystem::path& log)
{
  const pid_t child = fork();
  if (child < 0) {
    throw std::runtime_error("fork failed while starting Si2 reader/writer");
  }
  if (child == 0) {
    const auto output_file = std::fopen(output.c_str(), "w");
    const auto log_file = std::fopen(log.c_str(), "w");
    if (output_file == nullptr || log_file == nullptr) {
      _exit(126);
    }
    dup2(fileno(output_file), STDOUT_FILENO);
    dup2(fileno(log_file), STDERR_FILENO);

    std::vector<std::string> command{executable.string()};
    command.insert(command.end(), arguments.begin(), arguments.end());
    std::vector<char*> pointers;
    pointers.reserve(command.size() + 1u);
    for (auto& argument : command) {
      pointers.push_back(argument.data());
    }
    pointers.push_back(nullptr);
    execv(executable.c_str(), pointers.data());
    _exit(127);
  }

  int status = 0;
  if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) {
    return 128;
  }
  return WEXITSTATUS(status);
}

std::size_t lineCount(std::string_view text)
{
  return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + (!text.empty() && text.back() != '\n');
}

std::size_t linePrefixCount(std::string_view text, std::string_view prefix)
{
  std::size_t result = 0;
  std::size_t position = 0;
  while (position < text.size()) {
    const auto end = text.find('\n', position);
    const auto line = text.substr(position, end == std::string_view::npos ? text.size() - position : end - position);
    result += line.starts_with(prefix);
    if (end == std::string_view::npos) {
      break;
    }
    position = end + 1u;
  }
  return result;
}

void expectByteEqual(std::string_view expected, std::string_view actual, const std::filesystem::path& log)
{
  if (expected == actual) {
    return;
  }
  const auto common = std::mismatch(expected.begin(), expected.end(), actual.begin(), actual.end());
  const auto offset = static_cast<std::size_t>(std::distance(expected.begin(), common.first));
  ADD_FAILURE() << "Si2 normalized output differs from OpenROAD golden at byte " << offset << "; expected bytes=" << expected.size()
                << ", actual bytes=" << actual.size() << "\nreader diagnostics:\n" << readText(log);
}

struct CompleteSyntaxCorpus
{
  std::filesystem::path input;
  std::filesystem::path golden;
};

CompleteSyntaxCorpus completeSyntaxCorpus(std::string_view domain)
{
  const auto root = openRoadSource();
  if (root.empty() || !std::filesystem::is_directory(root)) {
    return {};
  }
  const auto directory = root / "src/odb/src" / domain / "TEST";
  return {.input = directory / ("complete.5.8." + std::string{domain}),
          .golden = directory / ("complete.5.8." + std::string{domain} + ".au")};
}

void expectKeySection(std::string_view section, const std::vector<std::string>& expected, const std::vector<std::string>& actual)
{
  ASSERT_EQ(expected.size(), actual.size()) << section;
  for (std::size_t index = 0; index < expected.size(); ++index) {
    if (expected[index] != actual[index]) {
      ADD_FAILURE() << section << " differs at sorted item " << index << "\nexpected: " << expected[index]
                    << "\n  actual: " << actual[index];
      return;
    }
  }
}

void expectSemanticSnapshot(const test::DesignSemanticSnapshot& expected, const test::DesignSemanticSnapshot& actual)
{
  EXPECT_EQ(expected.component_counts, actual.component_counts);
  EXPECT_EQ(expected.global, actual.global) << "global";
  expectKeySection("rows", expected.rows, actual.rows);
  expectKeySection("track_grids", expected.track_grids, actual.track_grids);
  expectKeySection("gcell_grids", expected.gcell_grids, actual.gcell_grids);
  expectKeySection("instances", expected.instances, actual.instances);
  expectKeySection("instance_pins", expected.instance_pins, actual.instance_pins);
  expectKeySection("io_pins", expected.io_pins, actual.io_pins);
  expectKeySection("vias", expected.vias, actual.vias);
  expectKeySection("non_default_rules", expected.non_default_rules, actual.non_default_rules);
  expectKeySection("nets", expected.nets, actual.nets);
  expectKeySection("regions", expected.regions, actual.regions);
  expectKeySection("groups", expected.groups, actual.groups);
  expectKeySection("blockages", expected.blockages, actual.blockages);
  expectKeySection("fills", expected.fills, actual.fills);
}

void expectAllNetRoutingSemantics(const DesignStore& expected, const DesignStore& actual, bool ignore_unrepresented_wire_mask = false)
{
  for (const auto expected_id : expected.netlistStorage().nets()) {
    if (test::detail::isEmptyRegularAliasOfSpecialNet(expected, expected_id)) {
      continue;
    }
    const auto& expected_net = expected.netlistStorage().net(expected_id);
    const bool special = expected.netlistStorage().isSpecialNet(expected_id);
    const auto actual_id
        = special ? actual.netlistStorage().findSpecialNet(expected_net.name) : actual.netlistStorage().findRegularNet(expected_net.name);
    ASSERT_TRUE(actual_id) << (special ? "special:" : "regular:") << expected_net.name;

    const auto net_name = std::string(special ? "special:" : "regular:") + expected_net.name;
    const auto expected_wires = test::detail::wirePrimitiveKeys(expected, expected_id, ignore_unrepresented_wire_mask);
    const auto actual_wires = test::detail::wirePrimitiveKeys(actual, actual_id, ignore_unrepresented_wire_mask);
    if (expected_wires != actual_wires) {
      expectKeySection("wire primitives for " + net_name, expected_wires, actual_wires);
      return;
    }

    const auto expected_geometry = test::detail::netGeometryKeys(expected, expected_id);
    const auto actual_geometry = test::detail::netGeometryKeys(actual, actual_id);
    if (expected_geometry != actual_geometry) {
      expectKeySection("standalone geometry for " + net_name, expected_geometry, actual_geometry);
      return;
    }
  }
}

std::string netKey(const DesignStore& design, DesignNetId id)
{
  if (!id) {
    return {};
  }
  const auto& storage = design.netlistStorage();
  return std::string(storage.isSpecialNet(id) ? "special:" : "regular:") + storage.net(id).name;
}

void expectShapeSet(const DesignShapeSet& expected, const DesignShapeSet& actual)
{
  EXPECT_EQ(expected.rectangles, actual.rectangles);
  EXPECT_EQ(expected.polygons, actual.polygons);
}

void expectGeometry(const std::vector<DesignLayerGeometry>& expected, const std::vector<DesignLayerGeometry>& actual)
{
  ASSERT_EQ(expected.size(), actual.size());
  for (std::size_t index = 0; index < expected.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_EQ(expected[index].layer, actual[index].layer);
    expectShapeSet(expected[index].shapes, actual[index].shapes);
  }
}

std::vector<std::string> viaGeometryKeys(const DesignStore& design, const DesignVia& via)
{
  std::vector<std::string> result;
  for (const auto& rectangle : via.rectangles) {
    const auto& layer = design.techRegistry().registry().get<const TechLayerInfo>(rectangle.layer.entity());
    std::ostringstream key;
    key << "rect|" << layer.name << '|' << rectangle.mask << '|' << rectangle.rectangle.ll_x << ',' << rectangle.rectangle.ll_y << ','
        << rectangle.rectangle.ur_x << ',' << rectangle.rectangle.ur_y;
    result.push_back(key.str());
  }
  for (const auto& polygon : via.polygons) {
    const auto& layer = design.techRegistry().registry().get<const TechLayerInfo>(polygon.layer.entity());
    std::ostringstream key;
    key << "polygon|" << layer.name << '|' << polygon.mask;
    for (const auto point : polygon.points) {
      key << '|' << point.x << ',' << point.y;
    }
    result.push_back(key.str());
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::string layerName(const DesignStore& design, TechRoutingLayerId layer)
{
  return design.techRegistry().registry().get<const TechLayerInfo>(layer.entity()).name;
}

std::string viaName(const DesignStore& design, const DesignWireVia& via)
{
  if (via.tech_via) {
    return design.techRegistry().registry().get<const TechViaMaster>(via.tech_via.entity()).name;
  }
  return design.routingStorage().via(via.design_via).name;
}

std::string pinViaName(const DesignStore& design, const DesignPinVia& via)
{
  if (via.tech_via) {
    return design.techRegistry().registry().get<const TechViaMaster>(via.tech_via.entity()).name;
  }
  return design.routingStorage().via(via.design_via).name;
}

std::vector<std::string> pinPortKeys(const DesignStore& design, const DesignIoPin& pin)
{
  std::vector<std::string> result;
  result.reserve(pin.ports.size());
  for (const auto& port : pin.ports) {
    std::vector<std::string> primitives;
    for (const auto& rectangle : port.rectangles) {
      const auto& layer = design.techRegistry().registry().get<const TechLayerInfo>(rectangle.layer.entity());
      std::ostringstream key;
      key << "rect|" << layer.name << '|' << rectangle.rectangle.ll_x << ',' << rectangle.rectangle.ll_y << ',' << rectangle.rectangle.ur_x
          << ',' << rectangle.rectangle.ur_y << '|' << rectangle.flags << '|' << rectangle.mask << '|' << rectangle.spacing << '|'
          << rectangle.design_rule_width;
      primitives.push_back(key.str());
    }
    for (const auto& polygon : port.polygons) {
      const auto& layer = design.techRegistry().registry().get<const TechLayerInfo>(polygon.layer.entity());
      std::ostringstream key;
      key << "polygon|" << layer.name << '|' << polygon.flags << '|' << polygon.mask << '|' << polygon.spacing << '|'
          << polygon.design_rule_width;
      for (const auto point : polygon.points) {
        key << '|' << point.x << ',' << point.y;
      }
      primitives.push_back(key.str());
    }
    for (const auto& via : port.vias) {
      std::ostringstream key;
      key << "via|" << pinViaName(design, via) << '|' << via.origin.x << ',' << via.origin.y << '|' << via.flags << '|' << via.top_mask
          << ',' << via.cut_mask << ',' << via.bottom_mask;
      primitives.push_back(key.str());
    }
    std::sort(primitives.begin(), primitives.end());
    std::ostringstream port_key;
    const bool has_placement = (port.flags & DesignIoPinPortFlag::kHasPlacement) != 0u;
    port_key << "placement|" << has_placement << '|' << static_cast<unsigned>(port.placement_status) << '|' << port.origin.x << ','
             << port.origin.y << '|' << static_cast<unsigned>(port.orientation);
    for (const auto& primitive : primitives) {
      port_key << "||" << primitive;
    }
    result.push_back(port_key.str());
  }
  std::sort(result.begin(), result.end());
  return result;
}

bool pointLess(Point lhs, Point rhs)
{
  return lhs.x < rhs.x || (lhs.x == rhs.x && lhs.y < rhs.y);
}

std::vector<std::string> wirePrimitives(const DesignStore& design, DesignNetId net)
{
  std::vector<std::string> result;
  for (const auto wire_id : design.routingStorage().wires(net)) {
    const auto& wire = design.routingStorage().wire(wire_id);
    for (std::size_t path_index = 0; path_index < design.routingStorage().pathCount(wire_id); ++path_index) {
      const auto path = design.routingStorage().path(wire_id, path_index);
      const auto layer = layerName(design, path.layer());
      for (std::size_t point_index = 1; point_index < path.points().size(); ++point_index) {
        auto first = path.points()[point_index - 1u];
        auto second = path.points()[point_index];
        if (pointLess(second.position, first.position)) {
          std::swap(first, second);
        }
        std::ostringstream key;
        key << "segment|" << static_cast<unsigned>(wire.status) << '|' << wire.shield_net << '|' << layer << '|' << path.width() << '|'
            << path.mask() << '|' << path.shape() << '|' << path.style() << '|' << first.position.x << ',' << first.position.y << ','
            << first.flags << ',' << first.extension << '|' << second.position.x << ',' << second.position.y << ',' << second.flags << ','
            << second.extension;
        result.push_back(key.str());
      }
      for (const auto& via : path.vias()) {
        const auto& anchor = path.points()[via.point_index];
        std::ostringstream key;
        key << "via|" << static_cast<unsigned>(wire.status) << '|' << wire.shield_net << '|' << anchor.position.x << ','
            << anchor.position.y << '|' << viaName(design, via) << '|' << static_cast<unsigned>(via.orientation) << '|' << via.flags << '|'
            << via.top_mask << ',' << via.cut_mask << ',' << via.bottom_mask << '|' << via.rows << ',' << via.columns << ',' << via.step_x
            << ',' << via.step_y;
        result.push_back(key.str());
      }
      for (const auto& rectangle : path.rectangles()) {
        const auto anchor = path.points()[rectangle.point_index].position;
        const auto& delta = rectangle.delta;
        std::ostringstream key;
        key << "rect|" << static_cast<unsigned>(wire.status) << '|' << wire.shield_net << '|' << layer << '|' << anchor.x + delta.ll_x
            << ',' << anchor.y + delta.ll_y << ',' << anchor.x + delta.ur_x << ',' << anchor.y + delta.ur_y;
        result.push_back(key.str());
      }
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::vector<std::string> netGeometryPrimitives(const DesignStore& design, DesignNetId net)
{
  const auto* geometry = design.routingStorage().netGeometry(net);
  if (geometry == nullptr) {
    return {};
  }
  std::vector<std::string> result;
  for (const auto& rectangle : geometry->rectangles) {
    std::ostringstream key;
    key << "rect|" << static_cast<unsigned>(rectangle.route_status) << '|' << rectangle.shield_net << '|'
        << layerName(design, rectangle.layer) << '|' << rectangle.flags << '|' << rectangle.mask << '|' << rectangle.shape << '|'
        << rectangle.rectangle.ll_x << ',' << rectangle.rectangle.ll_y << ',' << rectangle.rectangle.ur_x << ','
        << rectangle.rectangle.ur_y;
    result.push_back(key.str());
  }
  for (const auto& polygon : geometry->polygons) {
    std::ostringstream key;
    key << "polygon|" << static_cast<unsigned>(polygon.route_status) << '|' << polygon.shield_net << '|' << layerName(design, polygon.layer)
        << '|' << polygon.flags << '|' << polygon.mask << '|' << polygon.shape;
    for (const auto point : polygon.points) {
      key << '|' << point.x << ',' << point.y;
    }
    result.push_back(key.str());
  }
  for (const auto& via : geometry->vias) {
    const auto name = via.tech_via ? design.techRegistry().registry().get<const TechViaMaster>(via.tech_via.entity()).name
                                   : design.routingStorage().via(via.design_via).name;
    for (const auto origin : via.origins) {
      std::ostringstream key;
      key << "via|" << static_cast<unsigned>(via.route_status) << '|' << via.shield_net << '|' << name << '|'
          << static_cast<unsigned>(via.orientation) << '|' << via.flags << '|' << via.top_mask << ',' << via.cut_mask << ','
          << via.bottom_mask << '|' << via.shape << '|' << origin.x << ',' << origin.y;
      result.push_back(key.str());
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::vector<std::string> netOptionKeys(const DesignStore& design, DesignNetId net)
{
  const auto* options = design.netlistStorage().netOptions(net);
  if (options == nullptr) {
    return {};
  }
  std::vector<std::string> result;
  std::ostringstream values;
  values << options->flags << '|' << options->original << '|' << static_cast<unsigned>(options->pattern) << '|'
         << options->estimated_capacitance << '|' << options->frequency << '|' << options->xtalk << '|' << options->style << '|'
         << options->voltage;
  result.push_back(values.str());
  for (const auto& spacing : options->spacing_rules) {
    std::ostringstream key;
    key << "spacing|" << layerName(design, spacing.layer) << '|' << spacing.spacing << '|' << spacing.flags << '|' << spacing.range_left
        << '|' << spacing.range_right;
    result.push_back(key.str());
  }
  std::sort(result.begin(), result.end());
  return result;
}

void expectNet(const DesignStore& expected, const DesignStore& actual, std::string_view name, bool special)
{
  const auto expected_id = special ? expected.netlistStorage().findSpecialNet(name) : expected.netlistStorage().findRegularNet(name);
  const auto actual_id = special ? actual.netlistStorage().findSpecialNet(name) : actual.netlistStorage().findRegularNet(name);
  ASSERT_TRUE(expected_id) << name;
  ASSERT_TRUE(actual_id) << name;

  const auto& lhs = expected.netlistStorage().net(expected_id);
  const auto& rhs = actual.netlistStorage().net(actual_id);
  EXPECT_EQ(lhs.name, rhs.name);
  EXPECT_EQ(lhs.use, rhs.use);
  EXPECT_EQ(lhs.source, rhs.source);
  EXPECT_EQ(lhs.flags, rhs.flags);
  EXPECT_EQ(lhs.weight, rhs.weight);

  auto pin_keys = [](const DesignStore& design, DesignNetId net) {
    std::vector<std::string> keys;
    for (const auto pin_id : design.netlistStorage().instancePins(net)) {
      const auto& pin = design.netlistStorage().instancePin(pin_id);
      const auto& instance = design.netlistStorage().instance(pin.instance);
      const auto& term = design.libraryRegistry().registry().get<const LibraryMasterTerm>(pin.master_term.entity());
      keys.push_back(instance.name + '/' + term.name);
    }
    for (const auto pin_id : design.netlistStorage().ioPins(net)) {
      keys.push_back("PIN/" + design.netlistStorage().ioPin(pin_id).name);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
  };
  EXPECT_EQ(pin_keys(expected, expected_id), pin_keys(actual, actual_id));
  EXPECT_EQ(netOptionKeys(expected, expected_id), netOptionKeys(actual, actual_id));

  const auto expected_wires = expected.routingStorage().wires(expected_id);
  const auto actual_wires = actual.routingStorage().wires(actual_id);
  ASSERT_EQ(expected_wires.size(), actual_wires.size());
  EXPECT_EQ(wirePrimitives(expected, expected_id), wirePrimitives(actual, actual_id));
  EXPECT_EQ(netGeometryPrimitives(expected, expected_id), netGeometryPrimitives(actual, actual_id));
}

void expectEquivalent(const DesignStore& expected, const DesignStore& actual)
{
  EXPECT_EQ(expected.globalStorage().info().name, actual.globalStorage().info().name);
  EXPECT_EQ(expected.globalStorage().info().database_units_per_micron, actual.globalStorage().info().database_units_per_micron);
  EXPECT_EQ(expected.globalStorage().dieArea().boundary, actual.globalStorage().dieArea().boundary);

  ASSERT_EQ(expected.floorplanStorage().rowCount(), 1u);
  ASSERT_EQ(actual.floorplanStorage().rowCount(), 1u);
  const auto expected_row_id = expected.floorplanStorage().findRow("ROW0");
  const auto actual_row_id = actual.floorplanStorage().findRow("ROW0");
  ASSERT_TRUE(expected_row_id);
  ASSERT_TRUE(actual_row_id);
  const auto& expected_row = expected.floorplanStorage().row(expected_row_id);
  const auto& actual_row = actual.floorplanStorage().row(actual_row_id);
  EXPECT_EQ(expected_row.site, actual_row.site);
  EXPECT_EQ(expected_row.origin, actual_row.origin);
  EXPECT_EQ(expected_row.orientation, actual_row.orientation);
  EXPECT_EQ(expected_row.repeat_count_x, actual_row.repeat_count_x);
  EXPECT_EQ(expected_row.repeat_count_y, actual_row.repeat_count_y);
  EXPECT_EQ(expected_row.step_x, actual_row.step_x);
  EXPECT_EQ(expected_row.step_y, actual_row.step_y);

  ASSERT_EQ(expected.floorplanStorage().trackGridCount(), actual.floorplanStorage().trackGridCount());
  ASSERT_EQ(expected.floorplanStorage().trackGridCount(), 1u);
  const auto& expected_track = expected.floorplanStorage().trackGrid(expected.floorplanStorage().trackGrids().front());
  const auto& actual_track = actual.floorplanStorage().trackGrid(actual.floorplanStorage().trackGrids().front());
  EXPECT_EQ(expected_track.axis, actual_track.axis);
  EXPECT_EQ(expected_track.start, actual_track.start);
  EXPECT_EQ(expected_track.track_count, actual_track.track_count);
  EXPECT_EQ(expected_track.step, actual_track.step);
  EXPECT_EQ(expected_track.layers, actual_track.layers);

  ASSERT_EQ(expected.floorplanStorage().gcellGridCount(), actual.floorplanStorage().gcellGridCount());
  ASSERT_EQ(expected.floorplanStorage().gcellGridCount(), 1u);
  const auto& expected_gcell = expected.floorplanStorage().gcellGrid(expected.floorplanStorage().gcellGrids().front());
  const auto& actual_gcell = actual.floorplanStorage().gcellGrid(actual.floorplanStorage().gcellGrids().front());
  EXPECT_EQ(expected_gcell.axis, actual_gcell.axis);
  EXPECT_EQ(expected_gcell.start, actual_gcell.start);
  EXPECT_EQ(expected_gcell.line_count, actual_gcell.line_count);
  EXPECT_EQ(expected_gcell.step, actual_gcell.step);

  ASSERT_EQ(expected.netlistStorage().instanceCount(), actual.netlistStorage().instanceCount());
  for (const std::string_view name : {"u1", "u2"}) {
    const auto expected_id = expected.netlistStorage().findInstance(name);
    const auto actual_id = actual.netlistStorage().findInstance(name);
    ASSERT_TRUE(expected_id) << name;
    ASSERT_TRUE(actual_id) << name;
    const auto& lhs = expected.netlistStorage().instance(expected_id);
    const auto& rhs = actual.netlistStorage().instance(actual_id);
    EXPECT_EQ(lhs.master, rhs.master);
    EXPECT_EQ(lhs.origin, rhs.origin);
    EXPECT_EQ(lhs.orientation, rhs.orientation);
    EXPECT_EQ(lhs.placement_status, rhs.placement_status);
    EXPECT_EQ(lhs.source, rhs.source);
    EXPECT_EQ(lhs.flags, rhs.flags);
    EXPECT_EQ(lhs.weight, rhs.weight);
  }

  ASSERT_EQ(expected.netlistStorage().ioPinCount(), actual.netlistStorage().ioPinCount());
  const auto expected_pin_id = expected.netlistStorage().findIoPin("IN");
  const auto actual_pin_id = actual.netlistStorage().findIoPin("IN");
  ASSERT_TRUE(expected_pin_id);
  ASSERT_TRUE(actual_pin_id);
  const auto& expected_pin = expected.netlistStorage().ioPin(expected_pin_id);
  const auto& actual_pin = actual.netlistStorage().ioPin(actual_pin_id);
  EXPECT_EQ(netKey(expected, expected_pin.net), netKey(actual, actual_pin.net));
  EXPECT_EQ(expected_pin.direction, actual_pin.direction);
  EXPECT_EQ(expected_pin.use, actual_pin.use);
  EXPECT_EQ(pinPortKeys(expected, expected_pin), pinPortKeys(actual, actual_pin));

  EXPECT_EQ(expected.netlistStorage().netCount(), actual.netlistStorage().netCount());
  expectNet(expected, actual, "n1", false);
  expectNet(expected, actual, "VDD", true);

  ASSERT_EQ(expected.routingStorage().viaCount(), actual.routingStorage().viaCount());
  const auto expected_via_id = expected.routingStorage().findVia("LOCAL12");
  const auto actual_via_id = actual.routingStorage().findVia("LOCAL12");
  ASSERT_TRUE(expected_via_id);
  ASSERT_TRUE(actual_via_id);
  EXPECT_EQ(viaGeometryKeys(expected, expected.routingStorage().via(expected_via_id)),
            viaGeometryKeys(actual, actual.routingStorage().via(actual_via_id)));

  ASSERT_EQ(expected.constraintStorage().regionCount(), actual.constraintStorage().regionCount());
  const auto expected_region_id = expected.constraintStorage().findRegion("FENCE");
  const auto actual_region_id = actual.constraintStorage().findRegion("FENCE");
  ASSERT_TRUE(expected_region_id);
  ASSERT_TRUE(actual_region_id);
  EXPECT_EQ(expected.constraintStorage().region(expected_region_id).type, actual.constraintStorage().region(actual_region_id).type);
  EXPECT_EQ(expected.constraintStorage().region(expected_region_id).rectangles,
            actual.constraintStorage().region(actual_region_id).rectangles);

  ASSERT_EQ(expected.constraintStorage().groupCount(), actual.constraintStorage().groupCount());
  const auto expected_group_id = expected.constraintStorage().findGroup("logic");
  const auto actual_group_id = actual.constraintStorage().findGroup("logic");
  ASSERT_TRUE(expected_group_id);
  ASSERT_TRUE(actual_group_id);
  const auto& expected_group = expected.constraintStorage().group(expected_group_id);
  const auto& actual_group = actual.constraintStorage().group(actual_group_id);
  EXPECT_EQ(expected_group.flags, actual_group.flags);
  ASSERT_EQ(expected_group.instances.size(), actual_group.instances.size());
  EXPECT_EQ(expected.netlistStorage().instance(expected_group.instances.front()).name,
            actual.netlistStorage().instance(actual_group.instances.front()).name);

  ASSERT_EQ(expected.constraintStorage().blockageCount(), actual.constraintStorage().blockageCount());
  ASSERT_EQ(expected.constraintStorage().blockageCount(), 1u);
  const auto& expected_blockage = expected.constraintStorage().blockage(expected.constraintStorage().blockages().front());
  const auto& actual_blockage = actual.constraintStorage().blockage(actual.constraintStorage().blockages().front());
  EXPECT_EQ(expected_blockage.kind, actual_blockage.kind);
  EXPECT_EQ(expected_blockage.flags, actual_blockage.flags);
  EXPECT_EQ(expected_blockage.layer, actual_blockage.layer);
  EXPECT_EQ(expected_blockage.spacing, actual_blockage.spacing);
  EXPECT_EQ(expected_blockage.design_rule_width, actual_blockage.design_rule_width);
  EXPECT_EQ(expected_blockage.mask, actual_blockage.mask);
  EXPECT_EQ(expected_blockage.rectangles, actual_blockage.rectangles);
  EXPECT_EQ(expected_blockage.polygons, actual_blockage.polygons);
}

std::string_view testLef()
{
  return R"LEF(VERSION 5.8 ;
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

SITE CORE
  CLASS CORE ;
  SYMMETRY X Y ;
  SIZE 0.20 BY 0.40 ;
END CORE

MACRO INVX1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.20 BY 0.40 ;
  SYMMETRY X Y ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER M1 ;
        RECT 0.00 0.10 0.05 0.20 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER M1 ;
        RECT 0.15 0.10 0.20 0.20 ;
    END
  END Y
END INVX1
END LIBRARY
)LEF";
}

std::string_view testDef()
{
  return R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN diff_top ;
UNITS DISTANCE MICRONS 1000 ;

DIEAREA ( 0 0 ) ( 2000 2000 ) ;
ROW ROW0 CORE 0 0 N DO 10 BY 1 STEP 200 0 ;
TRACKS X 0 DO 10 STEP 200 LAYER M1 ;
GCELLGRID X 0 DO 4 STEP 500 ;

VIAS 1 ;
- LOCAL12
  + RECT M1 ( -80 -80 ) ( 80 80 )
  + RECT V1 ( -40 -40 ) ( 40 40 )
  + RECT M2 ( -80 -80 ) ( 80 80 )
  ;
END VIAS

COMPONENTS 2 ;
- u1 INVX1 + SOURCE NETLIST + WEIGHT 2 + PLACED ( 200 200 ) N ;
- u2 INVX1 + FIXED ( 800 200 ) FN ;
END COMPONENTS

PINS 1 ;
- IN
  + NET n1
  + DIRECTION INPUT
  + USE SIGNAL
  + LAYER M1 ( -25 -25 ) ( 25 25 )
  + FIXED ( 0 200 ) N
  ;
END PINS

BLOCKAGES 1 ;
- LAYER M1 RECT ( 900 900 ) ( 1100 1100 ) ;
END BLOCKAGES

REGIONS 1 ;
- FENCE ( 0 0 ) ( 1000 1000 ) + TYPE FENCE ;
END REGIONS

GROUPS 1 ;
- logic u1 + REGION FENCE ;
END GROUPS

SPECIALNETS 1 ;
- VDD
  ( u1 Y )
  ( u2 Y )
  + USE POWER
  + ROUTED M2 40 + SHAPE STRIPE ( 200 200 ) ( 200 1200 ) VIA12
  ;
END SPECIALNETS

NETS 1 ;
- n1
  ( PIN IN )
  ( u1 A )
  ( u2 A )
  + USE SIGNAL
  + SOURCE NETLIST
  + WEIGHT 3
  + ROUTED M1 ( 0 200 ) ( 200 200 ) LOCAL12 N ( 800 200 )
  ;
END NETS

END DESIGN
)DEF";
}

TEST(StructuredDesignSemanticDifferTest, DetectsRoutedWireCoordinateChange)
{
  TemporaryWorkspace workspace;
  const auto lef = workspace.write("input.lef", testLef());
  const auto expected_def = workspace.write("expected.def", testDef());

  std::string actual_text{testDef()};
  const std::string original_endpoint = "LOCAL12 N ( 800 200 )";
  const std::string changed_endpoint = "LOCAL12 N ( 801 200 )";
  const auto endpoint = actual_text.find(original_endpoint);
  ASSERT_NE(endpoint, std::string::npos);
  actual_text.replace(endpoint, original_endpoint.size(), changed_endpoint);
  const auto actual_def = workspace.write("actual.def", actual_text);

  TechStore technology;
  ASSERT_NO_THROW(LefTechImporter(technology).import(lef));
  LibraryStore library(technology.techRegistry());
  ASSERT_NO_THROW(LefLibraryImporter(technology, library).import(lef));

  DesignStore expected(technology.techRegistry(), library.libraryRegistry());
  DesignStore actual(technology.techRegistry(), library.libraryRegistry());
  ASSERT_NO_THROW(DefDesignImporter(expected).import(expected_def));
  ASSERT_NO_THROW(DefDesignImporter(actual).import(actual_def));

  const auto expected_id = expected.netlistStorage().findRegularNet("n1");
  const auto actual_id = actual.netlistStorage().findRegularNet("n1");
  ASSERT_TRUE(expected_id);
  ASSERT_TRUE(actual_id);

  const auto mismatch = test::compareNetPairs(expected, actual, {{.expected = expected_id, .actual = actual_id, .name = "n1"}});
  ASSERT_TRUE(mismatch.has_value());
  EXPECT_EQ(mismatch->name, "n1");
  EXPECT_NE(mismatch->section.find("wire.segments"), std::string::npos);
}

// OpenROAD's Si2 "complete" files are grammar corpora, not a consistent
// library/design pair: they intentionally contain dangling names, invalid
// table dimensions, and section-count mismatches. Compare the raw parser
// normalization with OpenROAD's golden output before any DB validation.
TEST(OpenRoadCompleteSyntaxDifferentialTest, Lef58MatchesOpenRoadGolden)
{
  const auto corpus = completeSyntaxCorpus("lef");
  if (corpus.input.empty()) {
    GTEST_SKIP() << "set OPENROAD_SOURCE_DIR to an OpenROAD source checkout";
  }
  ASSERT_TRUE(std::filesystem::is_regular_file(corpus.input)) << corpus.input;
  ASSERT_TRUE(std::filesystem::is_regular_file(corpus.golden)) << corpus.golden;

  const auto source = readText(corpus.input);
  EXPECT_EQ(source.size(), 51774u);
  EXPECT_EQ(lineCount(source), 1962u);
  EXPECT_EQ(linePrefixCount(source, "LAYER "), 25u);
  EXPECT_EQ(linePrefixCount(source, "MACRO "), 14u);
  EXPECT_EQ(linePrefixCount(source, "VIA "), 10u);
  EXPECT_EQ(linePrefixCount(source, "VIARULE "), 11u);
  EXPECT_EQ(linePrefixCount(source, "NONDEFAULTRULE "), 7u);
  EXPECT_EQ(linePrefixCount(source, "SITE "), 10u);
  EXPECT_EQ(linePrefixCount(source, "ARRAY "), 1u);
  EXPECT_EQ(linePrefixCount(source, "BEGINEXT "), 1u);

  TemporaryWorkspace workspace;
  const auto output = workspace.path("complete-lef.actual.au");
  const auto log = workspace.path("complete-lef.reader.log");
  const auto exit_code = runSi2Reader(SI2_LEFRW_EXECUTABLE, {"-65nm", "-lef58", corpus.input.string()}, output, log);
  ASSERT_EQ(exit_code, 0) << readText(log);
  expectByteEqual(readText(corpus.golden), readText(output), log);
}

TEST(OpenRoadCompleteSyntaxDifferentialTest, Def58MatchesOpenRoadGolden)
{
  const auto corpus = completeSyntaxCorpus("def");
  if (corpus.input.empty()) {
    GTEST_SKIP() << "set OPENROAD_SOURCE_DIR to an OpenROAD source checkout";
  }
  ASSERT_TRUE(std::filesystem::is_regular_file(corpus.input)) << corpus.input;
  ASSERT_TRUE(std::filesystem::is_regular_file(corpus.golden)) << corpus.golden;

  const auto source = readText(corpus.input);
  EXPECT_EQ(source.size(), 34796u);
  EXPECT_EQ(lineCount(source), 1007u);
  for (const auto section : {"COMPONENTS ", "PINS ", "PINPROPERTIES ", "NETS ", "SPECIALNETS ", "VIAS ", "REGIONS ",
                             "GROUPS ", "BLOCKAGES ", "SLOTS ", "FILLS ", "SCANCHAINS ", "STYLES ", "NONDEFAULTRULES "}) {
    EXPECT_EQ(linePrefixCount(source, section), 1u) << section;
  }

  TemporaryWorkspace workspace;
  const auto output = workspace.path("complete-def.actual.au");
  const auto log = workspace.path("complete-def.reader.log");
  const auto exit_code = runSi2Reader(SI2_DEFRW_EXECUTABLE, {corpus.input.string()}, output, log);
  ASSERT_EQ(exit_code, 0) << readText(log);
  expectByteEqual(readText(corpus.golden), readText(output), log);
}

TEST(OpenDbDesignDifferentialTest, OpenDbNormalizationPreservesRepresentedDesignSemantics)
{
  if (!hasOpenDbRuntime()) {
    GTEST_SKIP() << "configure the OpenDB Python module or set OPENDB_TCL";
  }

  TemporaryWorkspace workspace;
  const auto lef = workspace.write("input.lef", testLef());
  const auto input_def = workspace.write("input.def", testDef());
  const auto open_db_def = workspace.path("opendb.def");
  const auto open_db_log = workspace.path("opendb.log");

  TechStore technology;
  ASSERT_NO_THROW(LefTechImporter(technology).import(lef));
  LibraryStore library(technology.techRegistry());
  ASSERT_NO_THROW(LefLibraryImporter(technology, library).import(lef));

  DesignStore reference(technology.techRegistry(), library.libraryRegistry());
  DefDesignImporter reference_importer(reference);
  ASSERT_NO_THROW(reference_importer.import(input_def));
  EXPECT_TRUE(reference_importer.diagnostics().empty());

  const auto exit_code = normalizeWithOpenDb({lef}, input_def, open_db_def, open_db_log);
  ASSERT_EQ(exit_code, 0) << readText(open_db_log);
  ASSERT_TRUE(std::filesystem::is_regular_file(open_db_def)) << readText(open_db_log);

  DesignStore normalized(technology.techRegistry(), library.libraryRegistry());
  DefDesignImporter normalized_importer(normalized);
  ASSERT_NO_THROW(normalized_importer.import(open_db_def)) << readText(open_db_log) << '\n' << readText(open_db_def);

  expectEquivalent(reference, normalized);

  const auto canonical = workspace.path("canonical.def");
  DefDesignExporter(normalized).write(canonical);
  DesignStore fixed_point(technology.techRegistry(), library.libraryRegistry());
  ASSERT_NO_THROW(DefDesignImporter(fixed_point).import(canonical));
  expectEquivalent(normalized, fixed_point);
  expectSemanticSnapshot(test::makeDesignSemanticSnapshot(normalized), test::makeDesignSemanticSnapshot(fixed_point));

  const auto canonical_open_db_def = workspace.path("canonical-opendb.def");
  const auto canonical_open_db_log = workspace.path("canonical-opendb.log");
  const auto canonical_exit_code = normalizeWithOpenDb({lef}, canonical, canonical_open_db_def, canonical_open_db_log);
  ASSERT_EQ(canonical_exit_code, 0) << readText(canonical_open_db_log);
  ASSERT_TRUE(std::filesystem::is_regular_file(canonical_open_db_def)) << readText(canonical_open_db_log);

  DesignStore open_db_fixed_point(technology.techRegistry(), library.libraryRegistry());
  DefDesignImporter open_db_fixed_point_importer(open_db_fixed_point);
  ASSERT_NO_THROW(open_db_fixed_point_importer.import(canonical_open_db_def))
      << readText(canonical_open_db_log) << '\n'
      << readText(canonical_open_db_def);
  EXPECT_TRUE(open_db_fixed_point_importer.diagnostics().empty());
  expectEquivalent(normalized, open_db_fixed_point);
  expectSemanticSnapshot(test::makeDesignSemanticSnapshot(normalized), test::makeDesignSemanticSnapshot(open_db_fixed_point));
}

enum class OpenDbCorpusRoot
{
  kOpenRoad,
  kIspd18,
  kIspd19
};

struct OpenDbCorpusCase
{
  std::string name;
  std::filesystem::path lef;
  std::filesystem::path def;
  std::size_t minimum_wire_count = 0;
  std::size_t minimum_path_count = 0;
  std::size_t minimum_path_via_count = 0;
  std::size_t minimum_virtual_point_count = 0;
  bool large = false;
  OpenDbCorpusRoot root = OpenDbCorpusRoot::kOpenRoad;
  std::filesystem::path additional_lef;
  bool repair_invalid_ispd18_metal9_prl = false;
  bool ignore_unrepresented_wire_mask = false;
  bool repair_zero_diearea = false;
  bool default_unspecified_pin_use_to_signal = false;
};

struct RoutingCoverage
{
  std::size_t wire_count = 0;
  std::size_t path_count = 0;
  std::size_t point_count = 0;
  std::size_t path_via_count = 0;
  std::size_t path_rectangle_count = 0;
  std::size_t virtual_point_count = 0;
  std::size_t geometry_primitive_count = 0;

  bool operator==(const RoutingCoverage&) const = default;
};

RoutingCoverage routingCoverage(const DesignStore& design)
{
  RoutingCoverage result;
  for (const auto net : design.netlistStorage().nets()) {
    for (const auto wire_id : design.routingStorage().wires(net)) {
      ++result.wire_count;
      result.path_count += design.routingStorage().pathCount(wire_id);
      for (std::size_t path_index = 0; path_index < design.routingStorage().pathCount(wire_id); ++path_index) {
        const auto path = design.routingStorage().path(wire_id, path_index);
        result.point_count += path.points().size();
        result.path_via_count += path.vias().size();
        result.path_rectangle_count += path.rectangles().size();
        result.virtual_point_count += std::ranges::count_if(
            path.points(), [](const DesignWirePoint& point) { return (point.flags & DesignWirePointFlag::kVirtual) != 0u; });
      }
    }
    if (const auto* geometry = design.routingStorage().netGeometry(net); geometry != nullptr) {
      result.geometry_primitive_count += geometry->rectangles.size() + geometry->polygons.size() + geometry->vias.size();
    }
  }
  return result;
}

void expectRoutingCoverage(const OpenDbCorpusCase& test_case, const DesignStore& source, const DesignStore& normalized)
{
  const auto expected = routingCoverage(source);
  const auto actual = routingCoverage(normalized);
  EXPECT_GE(expected.wire_count, test_case.minimum_wire_count) << test_case.name;
  EXPECT_GE(expected.path_count, test_case.minimum_path_count) << test_case.name;
  EXPECT_GE(expected.path_via_count, test_case.minimum_path_via_count) << test_case.name;
  EXPECT_GE(expected.virtual_point_count, test_case.minimum_virtual_point_count) << test_case.name;
  EXPECT_EQ(expected.wire_count, actual.wire_count) << test_case.name;
  EXPECT_EQ(expected.path_count, actual.path_count) << test_case.name;
  EXPECT_EQ(expected.point_count, actual.point_count) << test_case.name;
  EXPECT_EQ(expected.path_via_count, actual.path_via_count) << test_case.name;
  EXPECT_EQ(expected.path_rectangle_count, actual.path_rectangle_count) << test_case.name;
  EXPECT_EQ(expected.virtual_point_count, actual.virtual_point_count) << test_case.name;
  EXPECT_EQ(expected.geometry_primitive_count, actual.geometry_primitive_count) << test_case.name;
  std::cout << "[OpenDB routing diff] " << test_case.name << ": wires=" << expected.wire_count << ", paths=" << expected.path_count
            << ", points=" << expected.point_count << ", vias=" << expected.path_via_count
            << ", path_rectangles=" << expected.path_rectangle_count << ", virtual_points=" << expected.virtual_point_count
            << ", geometry_primitives=" << expected.geometry_primitive_count << '\n';
}

void expectRepresentedSemantics(const OpenDbCorpusCase& test_case, const DesignStore& expected, const DesignStore& actual)
{
  expectRoutingCoverage(test_case, expected, actual);
  if (test_case.large) {
    expectSemanticSnapshot(test::makeDesignSemanticSnapshot(expected, false, test_case.default_unspecified_pin_use_to_signal),
                           test::makeDesignSemanticSnapshot(actual, false, test_case.default_unspecified_pin_use_to_signal));
    test::expectStructuredNetSemantics(expected, actual);
  } else {
    expectSemanticSnapshot(test::makeDesignSemanticSnapshot(expected, true, test_case.default_unspecified_pin_use_to_signal),
                           test::makeDesignSemanticSnapshot(actual, true, test_case.default_unspecified_pin_use_to_signal));
    expectAllNetRoutingSemantics(expected, actual, test_case.ignore_unrepresented_wire_mask);
  }
}

void PrintTo(const OpenDbCorpusCase& test_case, std::ostream* output)
{
  *output << test_case.name;
}

std::filesystem::path repairInvalidIspd18Metal9Prl(const OpenDbCorpusCase& test_case, const std::filesystem::path& source,
                                                   const TemporaryWorkspace& workspace)
{
  constexpr std::string_view malformed = "    WIDTH 4.5   1.50\n    WIDTH 1.5   0.45 ;";
  constexpr std::string_view repaired = "    WIDTH 4.5   1.50 ;";

  auto contents = readText(source);
  const auto position = contents.find(malformed);
  if (position == std::string::npos || contents.find(malformed, position + malformed.size()) != std::string::npos) {
    throw std::runtime_error("expected exactly one malformed ISPD18 Metal9 PRL row in " + source.string());
  }
  contents.replace(position, malformed.size(), repaired);
  return workspace.write(test_case.name + "-repaired.lef", contents);
}

std::filesystem::path repairZeroDieArea(const OpenDbCorpusCase& test_case, const std::filesystem::path& source,
                                        const TemporaryWorkspace& workspace)
{
  constexpr std::string_view degenerate = "DIEAREA ( 0 0 ) ( 0 0 ) ;";
  constexpr std::string_view repaired = "DIEAREA ( 0 0 ) ( 1 1 ) ;";

  auto contents = readText(source);
  const auto position = contents.find(degenerate);
  if (position == std::string::npos || contents.find(degenerate, position + degenerate.size()) != std::string::npos) {
    throw std::runtime_error("expected exactly one degenerate DIEAREA in " + source.string());
  }
  contents.replace(position, degenerate.size(), repaired);
  return workspace.write(test_case.name + "-repaired.def", contents);
}

class OpenDbCorpusDifferentialTest : public testing::TestWithParam<OpenDbCorpusCase>
{
};

TEST_P(OpenDbCorpusDifferentialTest, MatchesOpenDbNormalizedImportAndExportSemantics)
{
  if (!hasOpenDbRuntime()) {
    GTEST_SKIP() << "configure the OpenDB Python module or set OPENDB_TCL";
  }
  const auto& test_case = GetParam();
  if (test_case.large && test_case.root == OpenDbCorpusRoot::kOpenRoad && std::getenv("ECCDB_RUN_LARGE_DEF_TESTS") == nullptr) {
    GTEST_SKIP() << "set ECCDB_RUN_LARGE_DEF_TESTS=1 to run the large OpenDB differential test";
  }
  const auto root = [&test_case] {
    switch (test_case.root) {
      case OpenDbCorpusRoot::kOpenRoad:
        return openRoadSource();
      case OpenDbCorpusRoot::kIspd18:
        return ispd18Source();
      case OpenDbCorpusRoot::kIspd19:
        return ispd19Source();
    }
    return std::filesystem::path{};
  }();
  if (root.empty() || !std::filesystem::is_directory(root)) {
    if (test_case.root == OpenDbCorpusRoot::kIspd18) {
      GTEST_SKIP() << "set ECCDB_ISPD18_ROOT to the extracted ISPD 2018 benchmark root";
    }
    if (test_case.root == OpenDbCorpusRoot::kIspd19) {
      GTEST_SKIP() << "set ECCDB_ISPD19_ROOT to the extracted ISPD 2019 benchmark root";
    }
    GTEST_SKIP() << "set OPENROAD_SOURCE_DIR to an OpenROAD source checkout";
  }

  const auto lef = root / test_case.lef;
  const auto input_def = root / test_case.def;
  ASSERT_TRUE(std::filesystem::is_regular_file(lef)) << lef;
  ASSERT_TRUE(std::filesystem::is_regular_file(input_def)) << input_def;

  TemporaryWorkspace workspace;
  const auto effective_input_def = test_case.repair_zero_diearea ? repairZeroDieArea(test_case, input_def, workspace) : input_def;
  std::vector<std::filesystem::path> lefs;
  lefs.push_back(test_case.repair_invalid_ispd18_metal9_prl ? repairInvalidIspd18Metal9Prl(test_case, lef, workspace) : lef);
  if (!test_case.additional_lef.empty()) {
    const auto additional_lef = root / test_case.additional_lef;
    ASSERT_TRUE(std::filesystem::is_regular_file(additional_lef)) << additional_lef;
    lefs.push_back(additional_lef);
  }
  const auto open_db_def = workspace.path(test_case.name + "-opendb.def");
  const auto open_db_log = workspace.path(test_case.name + "-opendb.log");

  TechStore technology;
  ASSERT_NO_THROW(LefTechImporter(technology).import(lefs));
  LibraryStore library(technology.techRegistry());
  ASSERT_NO_THROW(LefLibraryImporter(technology, library).import(lefs));

  DesignStore source(technology.techRegistry(), library.libraryRegistry());
  DefDesignImporter source_importer(source);
  ASSERT_NO_THROW(source_importer.import(effective_input_def)) << effective_input_def;

  const auto exit_code = normalizeWithOpenDb(lefs, effective_input_def, open_db_def, open_db_log);
  ASSERT_EQ(exit_code, 0) << readText(open_db_log);
  ASSERT_TRUE(std::filesystem::is_regular_file(open_db_def)) << readText(open_db_log);

  DesignStore open_db_normalized(technology.techRegistry(), library.libraryRegistry());
  DefDesignImporter normalized_importer(open_db_normalized);
  ASSERT_NO_THROW(normalized_importer.import(open_db_def)) << readText(open_db_log) << '\n' << readText(open_db_def);

  {
    SCOPED_TRACE("OpenDB normalization of the source DEF");
    expectRepresentedSemantics(test_case, source, open_db_normalized);
  }

  const auto exported_def = workspace.path(test_case.name + "-idb-export.def");
  ASSERT_NO_THROW(DefDesignExporter(source).write(exported_def));
  const auto exported_open_db_def = workspace.path(test_case.name + "-idb-export-opendb.def");
  const auto exported_open_db_log = workspace.path(test_case.name + "-idb-export-opendb.log");
  const auto exported_exit_code = normalizeWithOpenDb(lefs, exported_def, exported_open_db_def, exported_open_db_log);
  ASSERT_EQ(exported_exit_code, 0) << readText(exported_open_db_log);
  ASSERT_TRUE(std::filesystem::is_regular_file(exported_open_db_def)) << readText(exported_open_db_log);

  DesignStore exported_open_db_normalized(technology.techRegistry(), library.libraryRegistry());
  DefDesignImporter exported_normalized_importer(exported_open_db_normalized);
  ASSERT_NO_THROW(exported_normalized_importer.import(exported_open_db_def))
      << readText(exported_open_db_log) << '\n'
      << readText(exported_open_db_def);
  EXPECT_TRUE(exported_normalized_importer.diagnostics().empty());
  {
    SCOPED_TRACE("OpenDB normalization of the IDB canonical exporter output");
    expectRepresentedSemantics(test_case, source, exported_open_db_normalized);
  }
}

INSTANTIATE_TEST_SUITE_P(
    OpenRoadOdbData, OpenDbCorpusDifferentialTest,
    testing::Values(
        OpenDbCorpusCase{.name = "virtual_route",
                         .lef = "src/odb/test/data/gscl45nm.lef",
                         .def = "src/odb/test/data/virtual_route.def",
                         .minimum_wire_count = 1,
                         .minimum_path_count = 1,
                         .minimum_virtual_point_count = 1},
        OpenDbCorpusCase{.name = "ndr",
                         .lef = "src/odb/test/data/gscl45nm.lef",
                         .def = "src/odb/test/data/ndr.def",
                         .repair_zero_diearea = true,
                         .default_unspecified_pin_use_to_signal = true},
        OpenDbCorpusCase{.name = "sky130hd_multi_patterned",
                         .lef = "src/odb/test/data/sky130hd/sky130hd_multi_patterned.tlef",
                         .def = "src/odb/test/data/sky130hd_multi_patterned.def",
                         .minimum_wire_count = 1,
                         .minimum_path_count = 1,
                         .ignore_unrepresented_wire_mask = true},
        OpenDbCorpusCase{.name = "gcd_floorplan", .lef = "src/gpl/test/nangate45.lef", .def = "src/odb/test/data/gcd/floorplan.def"},
        OpenDbCorpusCase{.name = "gcd_placed",
                         .lef = "src/gpl/test/nangate45.lef",
                         .def = "src/odb/test/data/gcd/gcd.def",
                         .minimum_wire_count = 2,
                         .minimum_path_count = 300,
                         .minimum_path_via_count = 1},
        OpenDbCorpusCase{.name = "gcd_pdn",
                         .lef = "src/gpl/test/nangate45.lef",
                         .def = "src/odb/test/data/gcd/gcd_pdn.def",
                         .minimum_wire_count = 2,
                         .minimum_path_count = 200,
                         .minimum_path_via_count = 1},
        OpenDbCorpusCase{.name = "gcd_nangate45_route",
                         .lef = "src/gpl/test/nangate45.lef",
                         .def = "src/odb/test/data/gcd/gcd_nangate45_route.def",
                         .minimum_wire_count = 400,
                         .minimum_path_count = 4000,
                         .minimum_path_via_count = 100},
        OpenDbCorpusCase{.name = "gcd_route_via_only_layer",
                         .lef = "src/gpl/test/nangate45.lef",
                         .def = "src/odb/test/data/gcd/gcd_nangate45_route_via_only_layer.def",
                         .minimum_wire_count = 400,
                         .minimum_path_count = 4000,
                         .minimum_path_via_count = 100},
        OpenDbCorpusCase{.name = "gcd_route_with_power_pins",
                         .lef = "src/gpl/test/nangate45.lef",
                         .def = "src/odb/test/data/gcd/gcd_nangate45_route_with_power_pins.def",
                         .minimum_wire_count = 400,
                         .minimum_path_count = 4000,
                         .minimum_path_via_count = 100},
        OpenDbCorpusCase{.name = "large01", .lef = "src/gpl/test/nangate45.lef", .def = "src/gpl/test/large01.def", .large = true},
        OpenDbCorpusCase{.name = "aes_nangate45_preroute",
                         .lef = "test/Nangate45/Nangate45_tech.lef",
                         .def = "src/drt/test/aes_nangate45_preroute.def",
                         .minimum_wire_count = 2,
                         .minimum_path_count = 37000,
                         .minimum_path_via_count = 37000,
                         .large = true,
                         .additional_lef = "test/Nangate45/Nangate45_stdcell.lef"}),
    [](const testing::TestParamInfo<OpenDbCorpusCase>& info) { return info.param.name; });

INSTANTIATE_TEST_SUITE_P(Ispd18, OpenDbCorpusDifferentialTest,
                         testing::Values(OpenDbCorpusCase{.name = "test1",
                                                          .lef = "ispd18_test1/ispd18_test1.input.lef",
                                                          .def = "ispd18_test1/ispd18_test1.input.def",
                                                          .root = OpenDbCorpusRoot::kIspd18},
                                         OpenDbCorpusCase{.name = "test2",
                                                          .lef = "ispd18_test2/ispd18_test2.input.lef",
                                                          .def = "ispd18_test2/ispd18_test2.input.def",
                                                          .root = OpenDbCorpusRoot::kIspd18},
                                         OpenDbCorpusCase{.name = "test3",
                                                          .lef = "ispd18_test3/ispd18_test3.input.lef",
                                                          .def = "ispd18_test3/ispd18_test3.input.def",
                                                          .root = OpenDbCorpusRoot::kIspd18},
                                         OpenDbCorpusCase{.name = "test4",
                                                          .lef = "ispd18_test4/ispd18_test4.input.lef",
                                                          .def = "ispd18_test4/ispd18_test4.input.def",
                                                          .root = OpenDbCorpusRoot::kIspd18,
                                                          .repair_invalid_ispd18_metal9_prl = true},
                                         OpenDbCorpusCase{.name = "test5",
                                                          .lef = "ispd18_test5/ispd18_test5.input.lef",
                                                          .def = "ispd18_test5/ispd18_test5.input.def",
                                                          .root = OpenDbCorpusRoot::kIspd18,
                                                          .repair_invalid_ispd18_metal9_prl = true},
                                         OpenDbCorpusCase{.name = "test6",
                                                          .lef = "ispd18_test6/ispd18_test6.input.lef",
                                                          .def = "ispd18_test6/ispd18_test6.input.def",
                                                          .root = OpenDbCorpusRoot::kIspd18,
                                                          .repair_invalid_ispd18_metal9_prl = true},
                                         OpenDbCorpusCase{.name = "test7",
                                                          .lef = "ispd18_test7/ispd18_test7.input.lef",
                                                          .def = "ispd18_test7/ispd18_test7.input.def",
                                                          .root = OpenDbCorpusRoot::kIspd18,
                                                          .repair_invalid_ispd18_metal9_prl = true},
                                         OpenDbCorpusCase{.name = "test8",
                                                          .lef = "ispd18_test8/ispd18_test8.input.lef",
                                                          .def = "ispd18_test8/ispd18_test8.input.def",
                                                          .root = OpenDbCorpusRoot::kIspd18,
                                                          .repair_invalid_ispd18_metal9_prl = true},
                                         OpenDbCorpusCase{.name = "test9",
                                                          .lef = "ispd18_test9/ispd18_test9.input.lef",
                                                          .def = "ispd18_test9/ispd18_test9.input.def",
                                                          .root = OpenDbCorpusRoot::kIspd18,
                                                          .repair_invalid_ispd18_metal9_prl = true},
                                         OpenDbCorpusCase{.name = "test10",
                                                          .lef = "ispd18_test10/ispd18_test10.input.lef",
                                                          .def = "ispd18_test10/ispd18_test10.input.def",
                                                          .root = OpenDbCorpusRoot::kIspd18,
                                                          .repair_invalid_ispd18_metal9_prl = true}),
                         [](const testing::TestParamInfo<OpenDbCorpusCase>& info) { return info.param.name; });

INSTANTIATE_TEST_SUITE_P(
    Ispd19LargestTest10, OpenDbCorpusDifferentialTest,
    testing::Values(OpenDbCorpusCase{.name = "test10_team12",
                                     .lef = "input/ispd19_test10/ispd19_test10.input.lef",
                                     .def = "solutions/extracted/test10/def/12.t10.def",
                                     .minimum_wire_count = 800000,
                                     .minimum_path_count = 800000,
                                     .minimum_path_via_count = 1000000,
                                     .large = true,
                                     .root = OpenDbCorpusRoot::kIspd19}),
    [](const testing::TestParamInfo<OpenDbCorpusCase>& info) { return info.param.name; });

}  // namespace
}  // namespace eccdb
