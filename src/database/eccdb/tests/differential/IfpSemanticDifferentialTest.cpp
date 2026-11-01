// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <gtest/gtest.h>
#include <json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "IfpSemanticSnapshot.h"
#include "def/DefDesignImporter.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

using Json = nlohmann::json;

struct ModelOptions
{
  bool reverse_entity_creation = false;
  bool reverse_segment_direction = false;
  bool split_route_containers = false;
  bool reverse_port_order = false;
  bool world_port_geometry = false;
  bool polygon_die_boundary = false;
  bool generated_explicit_defaults = false;
  bool generated_explicit_zero_origin_and_offset = false;
  bool move_segment_endpoint_one_dbu = false;
  bool move_route_via_one_dbu = false;
  bool move_connection_to_other_net = false;
  bool clock_use = false;
  bool split_port = false;
  bool merge_ports = false;
  bool duplicate_port = false;
  bool generated_cut_or_enclosure_change = false;
  bool rotate_non_symmetric_pin_via = false;
  std::string generated_via_name = "GENERATED_A";
  std::string signal_name = "signal";
};

std::string unsupportedText(const test::IfpSemanticSnapshot& snapshot)
{
  std::ostringstream stream;
  for (std::size_t index = 0; index < snapshot.unsupported.size(); ++index) {
    if (index != 0u) stream << "; ";
    stream << snapshot.unsupported[index];
  }
  return stream.str();
}

bool semanticallyEqual(const test::IfpSemanticSnapshot& lhs, const test::IfpSemanticSnapshot& rhs)
{
  return lhs.complete && rhs.complete && lhs == rhs;
}

void expectSemanticEqual(const test::IfpSemanticSnapshot& lhs, const test::IfpSemanticSnapshot& rhs)
{
  ASSERT_TRUE(lhs.complete) << unsupportedText(lhs);
  ASSERT_TRUE(rhs.complete) << unsupportedText(rhs);
  const auto expect_section = [](std::string_view section, const auto& left, const auto& right) {
    EXPECT_TRUE(left == right) << "section=" << section << ", left_size=" << left.size() << ", right_size=" << right.size();
  };
  expect_section("global", lhs.global, rhs.global);
  expect_section("core", lhs.core, rhs.core);
  expect_section("rows", lhs.rows, rhs.rows);
  expect_section("track_grids", lhs.track_grids, rhs.track_grids);
  expect_section("gcell_grids", lhs.gcell_grids, rhs.gcell_grids);
  expect_section("instances", lhs.instances, rhs.instances);
  expect_section("instance_pins", lhs.instance_pins, rhs.instance_pins);
  expect_section("io_pins", lhs.io_pins, rhs.io_pins);
  expect_section("vias", lhs.vias, rhs.vias);
  expect_section("nets", lhs.nets, rhs.nets);
}

void expectSemanticDifferent(const test::IfpSemanticSnapshot& lhs, const test::IfpSemanticSnapshot& rhs)
{
  ASSERT_TRUE(lhs.complete) << unsupportedText(lhs);
  ASSERT_TRUE(rhs.complete) << unsupportedText(rhs);
  EXPECT_FALSE(semanticallyEqual(lhs, rhs));
}

class SemanticModel
{
 public:
  explicit SemanticModel(const ModelOptions& options)
      : library(technology.techRegistry()), design(technology.techRegistry(), library.libraryRegistry(), &technology.geometryRegistry())
  {
    build(options);
  }

  [[nodiscard]] test::IfpSemanticSnapshot snapshot() const
  {
    return test::makeIfpSemanticSnapshot(technology, library, design);
  }

  void makeGeneratedViaUnexpandable()
  {
    auto replacement = design.routingStorage().via(generated_via);
    replacement.generated.via_rule = {};
    replacement.generated.bottom_layer = {};
    design.designRegistry().registry().replace<DesignVia>(generated_via.entity(), std::move(replacement));
  }

  void addUnusedMalformedGeneratedVia()
  {
    const auto unused = design.routingStorage().createVia(
        DesignViaInput{.name = "UNUSED_MALFORMED",
                       .flags = DesignViaFlag::kGenerated,
                       .generated = {.via_rule = generated_rule,
                                     .bottom_layer = m1,
                                     .cut_layer = v1,
                                     .top_layer = m2,
                                     .cut_size_x = 4,
                                     .cut_size_y = 6,
                                     .cut_spacing_x = 4,
                                     .cut_spacing_y = 6,
                                     .bottom_enclosure_x = 2,
                                     .bottom_enclosure_y = 3,
                                     .top_enclosure_x = 4,
                                     .top_enclosure_y = 5}});
    auto malformed = design.routingStorage().via(unused);
    malformed.generated.via_rule = {};
    malformed.generated.row_count = 0;
    design.designRegistry().registry().replace<DesignVia>(unused.entity(), std::move(malformed));
  }

  void makeRouteViaReferenceMissing()
  {
    const auto missing = DesignViaId{design.designRegistry().registry().create()};
    auto replacement = design.routingStorage().shape(route_via_shape);
    auto& route_via = std::get<DesignRouteVia>(replacement.geometry);
    route_via.via.design_via = missing;
    design.geometryRegistry().registry().replace<DesignRouteShape>(route_via_shape.entity(), std::move(replacement));
  }

  TechStore technology;
  LibraryStore library;
  DesignStore design;
  TechRoutingLayerId m1;
  TechCutLayerId v1;
  TechRoutingLayerId m2;
  TechViaRuleGenerateId generated_rule;
  DesignViaId generated_via;
  GeometryShapeId route_segment_shape;
  GeometryShapeId route_via_shape;

 private:
  void build(const ModelOptions& options)
  {
    m1 = technology.createRoutingLayer(
        TechLayerInfo{.name = "M1"},
        TechRoutingLayer{.direction = TechRoutingDirection::kHorizontal, .width = 10, .min_width = 10, .pitch_x = 40, .pitch_y = 40});
    v1 = technology.createCutLayer(TechLayerInfo{.name = "V1"}, TechCutLayer{.width = 4});
    m2 = technology.createRoutingLayer(
        TechLayerInfo{.name = "M2"},
        TechRoutingLayer{.direction = TechRoutingDirection::kVertical, .width = 12, .min_width = 12, .pitch_x = 50, .pitch_y = 50});

    generated_rule = technology.viaRuleGenerateStorage().createViaRuleGenerate(
        TechViaRuleGenerate{.name = "GEN12", .flags = TechViaRuleGenerateFlag::kDefault},
        TechViaRuleGenerateBottomLayer{.layer = m1,
                                       .flags = TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure,
                                       .enclosure_overhang1 = 2,
                                       .enclosure_overhang2 = 3},
        TechViaRuleGenerateCutLayer{.layer = v1,
                                    .flags = TechViaRuleGenerateCutLayerFlag::kHasRect | TechViaRuleGenerateCutLayerFlag::kHasSpacing,
                                    .cut_rect = {-2, -3, 2, 3},
                                    .spacing_x = 4,
                                    .spacing_y = 6},
        TechViaRuleGenerateTopLayer{.layer = m2,
                                    .flags = TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure,
                                    .enclosure_overhang1 = 4,
                                    .enclosure_overhang2 = 5});

    const auto site = library.siteStorage().createSite(LibrarySite{.name = "CORE", .width = 20, .height = 40,
                                                                    .site_class = LibrarySiteClass::kCore});
    const auto master = library.cellMasterStorage().createCellMaster(
        LibraryCellMaster{.name = "CELL", .site = site, .width = 20, .height = 40});
    const auto term_a = library.masterTermStorage().createMasterTerm(
        master, LibraryMasterTerm{.name = "A", .direction = LibraryMasterTermDirection::kInput});
    const auto term_b = library.masterTermStorage().createMasterTerm(
        master, LibraryMasterTerm{.name = "B", .direction = LibraryMasterTermDirection::kOutput});

    design.globalStorage().setInfo(DesignInfo{.name = "ifp-semantic", .database_units_per_micron = 1000});
    if (options.polygon_die_boundary) {
      design.globalStorage().setDieArea(DesignDieAreaInput{.boundary = {{1000, 1000}, {1000, 0}, {0, 0}, {0, 1000}}});
      design.floorplanStorage().setCoreArea(DesignCoreAreaInput{.boundary = {{900, 900}, {100, 900}, {100, 100}, {900, 100}}});
    } else {
      design.globalStorage().setDieArea(DesignDieAreaInput{.boundary = {{0, 0}, {1000, 1000}}});
      design.floorplanStorage().setCoreArea(DesignCoreAreaInput{.boundary = {{100, 100}, {900, 900}}});
    }
    static_cast<void>(design.floorplanStorage().createTrackGrid(
        DesignTrackGrid{.axis = DesignAxis::kX, .start = 20, .track_count = 10, .step = 40, .layers = {m1}}));
    static_cast<void>(design.floorplanStorage().createGCellGrid(DesignGCellGrid{.axis = DesignAxis::kY, .start = 0, .line_count = 6, .step = 200}));

    const auto signal = design.netlistStorage().createNet(
        DesignNet{.name = options.signal_name, .use = options.clock_use ? DesignSignalUse::kClock : DesignSignalUse::kSignal});
    const auto alternate = design.netlistStorage().createNet(DesignNet{.name = "alternate", .use = DesignSignalUse::kSignal});
    const auto fixed_via = design.routingStorage().createVia(
        DesignViaInput{.name = "FIXED", .rectangles = {{.layer = TechLayerId{m1.entity()}, .rectangle = {0, 0, 4, 8}},
                                                            {.layer = TechLayerId{v1.entity()}, .rectangle = {-1, -2, 2, 3}},
                                                            {.layer = TechLayerId{m2.entity()}, .rectangle = {-3, -2, 5, 3}}}});

    uint32_t generated_flags = options.generated_explicit_defaults ? DesignGeneratedViaFlag::kHasRowCol : 0u;
    if (options.generated_explicit_zero_origin_and_offset) {
      generated_flags |= DesignGeneratedViaFlag::kHasOrigin | DesignGeneratedViaFlag::kHasOffset;
    }
    generated_via = design.routingStorage().createVia(
        DesignViaInput{.name = options.generated_via_name,
                       .flags = DesignViaFlag::kGenerated,
                       .generated = {.via_rule = generated_rule,
                                     .bottom_layer = m1,
                                     .cut_layer = v1,
                                     .top_layer = m2,
                                     .flags = generated_flags,
                                     .cut_size_x = options.generated_cut_or_enclosure_change ? 6 : 4,
                                     .cut_size_y = 6,
                                     .cut_spacing_x = 4,
                                     .cut_spacing_y = 6,
                                     .bottom_enclosure_x = options.generated_cut_or_enclosure_change ? 3 : 2,
                                     .bottom_enclosure_y = 3,
                                     .top_enclosure_x = 4,
                                     .top_enclosure_y = 5,
                                     .row_count = 1,
                                     .column_count = 1}});

    const auto add_instance = [&](std::string name, Point origin, LibraryMasterTermId term, bool use_alternate) {
      const auto instance = design.netlistStorage().createInstance(
          DesignInstance{.name = std::move(name), .master = master, .origin = origin, .placement_status = DesignPlacementStatus::kPlaced});
      design.netlistStorage().connect(design.netlistStorage().findInstanceTerm(instance, term), use_alternate ? alternate : signal);
    };
    if (options.reverse_entity_creation) {
      add_instance("U1", {300, 200}, term_b, options.move_connection_to_other_net);
      add_instance("U0", {100, 200}, term_a, false);
    } else {
      add_instance("U0", {100, 200}, term_a, false);
      add_instance("U1", {300, 200}, term_b, options.move_connection_to_other_net);
    }

    const auto first_port = [&](bool transformed) {
      const bool rotated = options.rotate_non_symmetric_pin_via && transformed;
      return DesignBlockPinInput{.flags = DesignIoPinPortFlag::kExplicit | DesignIoPinPortFlag::kHasPlacement,
                                 .placement_status = DesignPlacementStatus::kPlaced,
                                 .origin = {100, 100},
                                 .orientation = rotated ? DesignOrientation::kE : DesignOrientation::kN,
                                 .rectangles = {{.layer = TechLayerId{m1.entity()},
                                                 .rectangle = transformed ? Rect{-20, -10, 20, 10} : Rect{-10, -20, 10, 20}}},
                                 .vias = {{.design_via = fixed_via, .origin = {0, 0}}}};
    };
    const auto second_port = [&](bool world_coordinates) {
      return DesignBlockPinInput{.flags = DesignIoPinPortFlag::kExplicit | DesignIoPinPortFlag::kHasPlacement,
                                 .placement_status = DesignPlacementStatus::kPlaced,
                                 .origin = world_coordinates ? Point{0, 0} : Point{300, 100},
                                 .orientation = DesignOrientation::kN,
                                 .rectangles = {{.layer = TechLayerId{m2.entity()},
                                                 .rectangle = world_coordinates ? Rect{280, 90, 320, 110} : Rect{-20, -10, 20, 10}}}};
    };

    const auto append_first_port_rectangles = [&](DesignBlockPinInput& port) {
      port.rectangles.push_back({.layer = TechLayerId{m1.entity()}, .rectangle = {-4, -4, 4, 4}});
    };

    std::vector<DesignBlockPinInput> ports;
    if (options.split_port) {
      auto first = first_port(false);
      append_first_port_rectangles(first);
      auto first_rectangle_and_via = first;
      first_rectangle_and_via.rectangles = {first.rectangles.front()};
      auto second = first;
      second.rectangles = {first.rectangles.back()};
      second.vias.clear();
      ports = {std::move(first_rectangle_and_via), std::move(second), second_port(false)};
    } else if (options.merge_ports) {
      auto merged = first_port(false);
      append_first_port_rectangles(merged);
      // Preserve the M2 rectangle's world coordinates while making it part of
      // the first PORT's local coordinate system.
      merged.rectangles.push_back({.layer = TechLayerId{m2.entity()}, .rectangle = {180, -10, 220, 10}});
      ports = {std::move(merged)};
    } else {
      auto first = first_port(options.rotate_non_symmetric_pin_via);
      append_first_port_rectangles(first);
      if (options.world_port_geometry && !options.rotate_non_symmetric_pin_via) {
        first.origin = {0, 0};
        first.rectangles.front().rectangle = {90, 80, 110, 120};
        first.rectangles.back().rectangle = {96, 96, 104, 104};
        first.vias.front().origin = {100, 100};
      }
      auto second = second_port(options.world_port_geometry);
      ports = {std::move(first), std::move(second)};
      if (options.duplicate_port) ports.push_back(ports.front());
      if (options.reverse_port_order) std::reverse(ports.begin(), ports.end());
    }
    const auto block_term = design.netlistStorage().createBlockTerm(
        DesignBlockTerm{.name = "IN", .direction = DesignIoPinDirection::kInput, .use = DesignSignalUse::kSignal}, std::move(ports));
    design.netlistStorage().connect(block_term, signal);

    const auto add_segment = [&](DesignWireId wire, Point begin, Point end, uint32_t source_path) {
      static_cast<void>(design.geometryStorage().createRouteShape(
          wire, DesignRouteShape{.layer = m1,
                                 .source_path = source_path,
                                 .geometry = DesignRouteSegment{.begin = {.position = begin}, .end = {.position = end}}}));
    };
    const auto first_wire = design.routingStorage().createWire(DesignWire{.net = signal});
    const Point first_begin = options.reverse_segment_direction ? Point{100, 0} : Point{0, 0};
    const Point first_end = options.reverse_segment_direction ? Point{0, 0} : Point{100, 0};
    route_segment_shape = design.geometryStorage().createRouteShape(
        first_wire, DesignRouteShape{.layer = m1,
                                     .source_path = 0,
                                     .geometry = DesignRouteSegment{.begin = {.position = first_begin}, .end = {.position = first_end}}});
    const Point second_begin = options.reverse_segment_direction ? Point{100, 100} : Point{100, 0};
    const Point second_end = options.reverse_segment_direction ? Point{100, 0} : Point{100, 100};
    if (options.split_route_containers) {
      const auto second_wire = design.routingStorage().createWire(DesignWire{.net = signal});
      add_segment(second_wire, second_begin, second_end, 1);
      route_via_shape = design.geometryStorage().createRouteShape(
          second_wire,
          DesignRouteShape{.layer = m1,
                           .source_path = 1,
                           .geometry = DesignRouteVia{.origin = {.position = {100, 100}},
                                                      .via = {.design_via = generated_via},
                                                      .exit_layer = m2}});
    } else {
      add_segment(first_wire, second_begin, second_end, 1);
      route_via_shape = design.geometryStorage().createRouteShape(
          first_wire,
          DesignRouteShape{.layer = m1,
                           .source_path = 1,
                           .geometry = DesignRouteVia{.origin = {.position = {100, 100}},
                                                      .via = {.design_via = generated_via},
                                                      .exit_layer = m2}});
    }
    if (options.move_segment_endpoint_one_dbu) {
      auto moved = design.routingStorage().shape(route_segment_shape);
      auto& segment = std::get<DesignRouteSegment>(moved.geometry);
      segment.end.position.y += 1;
      design.geometryStorage().updateRouteShape(route_segment_shape, std::move(moved));
    }
    if (options.move_route_via_one_dbu) {
      auto moved = design.routingStorage().shape(route_via_shape);
      auto& via = std::get<DesignRouteVia>(moved.geometry);
      via.origin.position.x += 1;
      design.geometryStorage().updateRouteShape(route_via_shape, std::move(moved));
    }
  }
};

TEST(IfpSemanticSnapshotTest, IgnoresStorageAndSyntaxOnlyDifferences)
{
  const SemanticModel baseline(ModelOptions{});
  const auto expected = baseline.snapshot();
  ASSERT_TRUE(expected.complete) << unsupportedText(expected);

  const auto expect_equivalent = [&](std::string_view label, const ModelOptions& options) {
    SCOPED_TRACE(label);
    const SemanticModel equivalent(options);
    expectSemanticEqual(expected, equivalent.snapshot());
  };

  expect_equivalent("entity creation order", ModelOptions{.reverse_entity_creation = true});
  expect_equivalent("segment endpoint direction", ModelOptions{.reverse_segment_direction = true});
  expect_equivalent("wire and NEW grouping", ModelOptions{.split_route_containers = true});
  expect_equivalent("PORT order", ModelOptions{.reverse_port_order = true});
  expect_equivalent("same world PORT geometry with different local placement", ModelOptions{.world_port_geometry = true});
  expect_equivalent("DIEAREA rectangle or polygon", ModelOptions{.polygon_die_boundary = true});
  expect_equivalent("generated via name", ModelOptions{.generated_via_name = "GENERATED_B"});
  expect_equivalent("generated via explicit default ROWCOL", ModelOptions{.generated_explicit_defaults = true});
  expect_equivalent("generated via explicit zero ORIGIN and OFFSET", ModelOptions{.generated_explicit_zero_origin_and_offset = true});
  expect_equivalent("combined representation differences",
                    ModelOptions{.reverse_entity_creation = true,
                                 .reverse_segment_direction = true,
                                 .split_route_containers = true,
                                 .reverse_port_order = true,
                                 .world_port_geometry = true,
                                 .polygon_die_boundary = true,
                                 .generated_explicit_defaults = true,
                                 .generated_explicit_zero_origin_and_offset = true,
                                 .generated_via_name = "GENERATED_B"});
}

TEST(IfpSemanticSnapshotTest, DetectsPhysicalAndConnectivityMutations)
{
  const SemanticModel baseline(ModelOptions{});
  const auto expected = baseline.snapshot();
  ASSERT_TRUE(expected.complete) << unsupportedText(expected);

  const auto expect_different = [&](std::string_view label, const ModelOptions& options) {
    SCOPED_TRACE(label);
    const SemanticModel mutated(options);
    expectSemanticDifferent(expected, mutated.snapshot());
  };

  expect_different("one DBU route segment endpoint", ModelOptions{.move_segment_endpoint_one_dbu = true});
  expect_different("one DBU route-via coordinate", ModelOptions{.move_route_via_one_dbu = true});
  expect_different("instance terminal net connection", ModelOptions{.move_connection_to_other_net = true});
  expect_different("net USE", ModelOptions{.clock_use = true});
  expect_different("PORT split", ModelOptions{.split_port = true});
  expect_different("PORT merge", ModelOptions{.merge_ports = true});
  expect_different("PORT duplicate", ModelOptions{.duplicate_port = true});
  expect_different("generated via cut/enclosure", ModelOptions{.generated_cut_or_enclosure_change = true});
  expect_different("non-symmetric PORT via orientation", ModelOptions{.rotate_non_symmetric_pin_via = true});
  expect_different("literal escaped identifier", ModelOptions{.signal_name = "\\signal "});
}

TEST(IfpSemanticSnapshotTest, FailsClosedForUnexpandableAndMissingViaReferences)
{
  SemanticModel baseline(ModelOptions{});
  const auto valid = baseline.snapshot();
  ASSERT_TRUE(valid.complete) << unsupportedText(valid);

  SemanticModel unexpandable(ModelOptions{});
  unexpandable.makeGeneratedViaUnexpandable();
  const auto bad_geometry = unexpandable.snapshot();
  EXPECT_FALSE(bad_geometry.complete);
  EXPECT_FALSE(bad_geometry.unsupported.empty());
  EXPECT_FALSE(semanticallyEqual(bad_geometry, bad_geometry));

  SemanticModel unused_malformed(ModelOptions{});
  unused_malformed.addUnusedMalformedGeneratedVia();
  expectSemanticEqual(valid, unused_malformed.snapshot());

  SemanticModel missing_reference(ModelOptions{});
  EXPECT_NO_THROW(missing_reference.makeRouteViaReferenceMissing());
  test::IfpSemanticSnapshot missing_snapshot;
  EXPECT_NO_THROW(missing_snapshot = missing_reference.snapshot());
  EXPECT_FALSE(missing_snapshot.complete);
  EXPECT_FALSE(missing_snapshot.unsupported.empty());
  EXPECT_FALSE(semanticallyEqual(valid, missing_snapshot));
}

struct ImportedPair
{
  ImportedPair() : library(technology.techRegistry()), lhs(technology.techRegistry(), library.libraryRegistry(), &technology.geometryRegistry()),
                   rhs(technology.techRegistry(), library.libraryRegistry(), &technology.geometryRegistry()) {}

  TechStore technology;
  LibraryStore library;
  DesignStore lhs;
  DesignStore rhs;
};

std::optional<std::filesystem::path> environmentPath(const char* name)
{
  const auto* value = std::getenv(name);
  if (value == nullptr || *value == '\0') return std::nullopt;
  return std::filesystem::path{value};
}

Json readJson(const std::filesystem::path& path)
{
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open " + path.string());
  return Json::parse(input);
}

std::vector<std::filesystem::path> manifestLefs(const Json& manifest, const std::filesystem::path& manifest_path)
{
  const auto base = std::filesystem::absolute(manifest_path).parent_path();
  std::vector<std::filesystem::path> result;
  for (const auto& item : manifest.at("lef_files")) {
    auto path = std::filesystem::path{item.get<std::string>()};
    result.push_back(path.is_absolute() ? std::move(path) : base / path);
  }
  return result;
}

void expectSavedPairSemanticEquality(const std::filesystem::path& manifest_path, const std::filesystem::path& root)
{
  const auto manifest = readJson(manifest_path);
  const auto lefs = manifestLefs(manifest, manifest_path);

  for (const auto phase : {"import", "output"}) {
    SCOPED_TRACE(manifest_path.filename().string() + "/" + phase);
    const auto idb_def = root / "idb" / (std::string(phase) + ".def");
    const auto eccdb_def = root / "eccdb" / (std::string(phase) + ".def");
    ASSERT_TRUE(std::filesystem::is_regular_file(idb_def)) << idb_def;
    ASSERT_TRUE(std::filesystem::is_regular_file(eccdb_def)) << eccdb_def;
    ImportedPair pair;
    LefTechImporter(pair.technology).import(lefs);
    LefLibraryImporter(pair.technology, pair.library).import(lefs);
    DefDesignImporter(pair.lhs).import(idb_def);
    DefDesignImporter(pair.rhs).import(eccdb_def);
    const auto lhs = test::makeIfpSemanticSnapshot(pair.technology, pair.library, pair.lhs);
    const auto rhs = test::makeIfpSemanticSnapshot(pair.technology, pair.library, pair.rhs);
    ASSERT_TRUE(lhs.complete) << unsupportedText(lhs);
    ASSERT_TRUE(rhs.complete) << unsupportedText(rhs);
    expectSemanticEqual(lhs, rhs);
  }
}

TEST(IfpSemanticSavedPairs, OptionalSavedIdbAndEccdbPairs)
{
  const auto manifest_path = environmentPath("ECCDB_IFP_MANIFEST");
  const auto root = environmentPath("ECCDB_IFP_PAIR_ROOT");
  if (!manifest_path.has_value() || !root.has_value()) {
    GTEST_SKIP() << "set ECCDB_IFP_MANIFEST to one case manifest.json and ECCDB_IFP_PAIR_ROOT to its run directory";
  }
  ASSERT_TRUE(std::filesystem::is_regular_file(*manifest_path)) << *manifest_path;
  ASSERT_TRUE(std::filesystem::is_directory(*root)) << *root;
  ASSERT_TRUE(readJson(*manifest_path).is_object()) << *manifest_path;
  expectSavedPairSemanticEquality(*manifest_path, *root);
}

}  // namespace
}  // namespace eccdb
