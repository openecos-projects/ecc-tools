// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "DesignTestFixture.h"
#include "design/pin_access/storage/DesignPinAccessStorage.h"
#include "design/via/component/ViaComponents.h"

namespace eccdb {
namespace {

TEST_F(DesignStorageTest, StoresSharedMasterCandidatesBlockPinCandidatesAndInstancePreferences)
{
  const auto input_port = library.masterPortStorage().createMasterPort(input_term);
  const auto output_port = library.masterPortStorage().createMasterPort(output_term);
  const auto design_via = design.routingStorage().createVia(
      DesignViaInput{.name = "DVIA", .rectangles = {{.layer = layer, .rectangle = {-2, -2, 2, 2}}}});
  const auto io_design_via = design.routingStorage().createVia(
      DesignViaInput{.name = "IO_DVIA", .rectangles = {{.layer = layer, .rectangle = {-2, -2, 2, 2}}}});
  const auto instance = design.netlistStorage().createInstance(createInstance("u1", {100, 200}));
  const auto input_pin = design.netlistStorage().findInstancePin(instance, input_term);
  const auto output_pin = design.netlistStorage().findInstancePin(instance, output_term);
  const auto io = design.netlistStorage().createIoPin(
      DesignIoPin{.name = "OUT"},
      {{.flags = DesignIoPinPortFlag::kExplicit}, {.flags = DesignIoPinPortFlag::kExplicit}});
  const auto block_pins = design.netlistStorage().blockPins(io);
  ASSERT_EQ(block_pins.size(), 2u);
  const auto first_block_pin = block_pins[0];
  const auto second_block_pin = block_pins[1];

  DesignPinAccessStorage pin_access{design.designRegistry(), tech, library.libraryRegistry(), design.netlistStorage(), design.rootId()};
  const auto candidate = [&](Point point, bool include_design_via = true) {
    auto result = DesignPinAccessPoint{.point = point,
                                       .layer = routing_layer,
                                       .access_directions = DesignPinAccessDirection::kNorth | DesignPinAccessDirection::kEast,
                                       .low_type = DesignPinAccessType::kOnGrid,
                                       .high_type = DesignPinAccessType::kEncOpt,
                                       .vias = {{.cut_count = 1u, .tech_via = tech_via}},
                                       .path_segments = {{.rectangle = {10, 20, 30, 20},
                                                          .flags = DesignPinAccessPathSegmentFlag::kBeginTruncated}}};
    if (include_design_via) {
      result.vias.push_back(DesignPinAccessVia{.cut_count = 2u, .design_via = design_via});
    }
    return result;
  };

  // Catalog candidates are master-local.  They are shared by every instance
  // of INVX1 and are not translated by the instance origin here.
  pin_access.setMasterPortCandidateGroup(input_port, 3u, {candidate({10, 20}), candidate({30, 20})});
  pin_access.setMasterPortCandidateGroup(output_port, 3u, {candidate({40, 20}, false)});
  ASSERT_NE(pin_access.catalog(), nullptr);
  ASSERT_EQ(pin_access.masterPortCandidateGroups(input_port).size(), 1u);
  EXPECT_EQ(pin_access.masterPortCandidateGroup(input_port, 3u).points[1].point, (Point{30, 20}));

  // Each BPin owns its block-absolute candidates independently. The legacy
  // BTerm + ordinal entry point resolves to this same second BPin.
  auto io_candidate = candidate({500, 900}, false);
  io_candidate.vias.push_back(DesignPinAccessVia{.cut_count = 1u, .design_via = io_design_via});
  pin_access.setBlockPinCandidateGroup(second_block_pin, 7u, {std::move(io_candidate)});
  EXPECT_FALSE(pin_access.hasBlockPinCandidateGroup(first_block_pin, 7u));
  ASSERT_EQ(pin_access.blockPinCandidateGroups(second_block_pin).size(), 1u);
  EXPECT_EQ(pin_access.blockPinCandidateGroup(second_block_pin, 7u).points.front().point, (Point{500, 900}));
  ASSERT_EQ(pin_access.ioPinPortCandidateGroups(io, 1u).size(), 1u);
  EXPECT_EQ(pin_access.ioPinPortCandidateGroup(io, 1u, 7u).points.front().point, (Point{500, 900}));
  EXPECT_FALSE(design.routingStorage().destroyVia(io_design_via));

  pin_access.setInstancePinAccessGroup(instance, 3u);
  pin_access.setPreferredInstancePinAccess(
      input_pin, {{.master_port = input_port, .group_index = 3u, .point_index = 1u}});
  pin_access.setPreferredInstancePinAccess(
      output_pin, {{.master_port = output_port, .group_index = 3u, .point_index = 0u}});
  ASSERT_TRUE(pin_access.instancePinAccessGroup(instance).has_value());
  EXPECT_EQ(*pin_access.instancePinAccessGroup(instance), 3u);
  ASSERT_EQ(pin_access.preferredInstancePinAccess(input_pin).size(), 1u);
  EXPECT_EQ(pin_access.preferredInstancePinAccess(input_pin).front().point_index, 1u);
  EXPECT_TRUE(pin_access.referencesDesignVia(design_via));
  EXPECT_TRUE(pin_access.referencesDesignVia(io_design_via));
  EXPECT_EQ(pin_access.referencedDesignVias(), (std::vector<DesignViaId>{design_via, io_design_via}));
  EXPECT_FALSE(design.routingStorage().destroyVia(design_via));
  EXPECT_NO_THROW(pin_access.validateRestoredState());

  // Candidate groups cannot be changed or removed while an instance selects
  // them or an instance pin references a candidate point.
  EXPECT_THROW(pin_access.setMasterPortCandidateGroup(input_port, 3u, {candidate({1, 1})}), std::invalid_argument);
  EXPECT_FALSE(pin_access.clearMasterPortCandidateGroup(input_port, 3u));
  EXPECT_TRUE(pin_access.clearPreferredInstancePinAccess(input_pin));
  ASSERT_EQ(pin_access.preferredInstancePinAccess(output_pin).size(), 1u);
  EXPECT_EQ(pin_access.preferredInstancePinAccess(output_pin).front().master_port, output_port);
  EXPECT_FALSE(pin_access.clearInstancePinAccessGroup(instance));
  EXPECT_TRUE(pin_access.clearPreferredInstancePinAccess(output_pin));
  EXPECT_FALSE(pin_access.clearMasterPortCandidateGroup(input_port, 3u));
  EXPECT_TRUE(pin_access.clearInstancePinAccessGroup(instance));
  EXPECT_TRUE(pin_access.clearMasterPortCandidateGroup(input_port, 3u));
  EXPECT_FALSE(pin_access.referencesDesignVia(design_via));
  EXPECT_TRUE(design.routingStorage().destroyVia(design_via));
  EXPECT_TRUE(pin_access.referencesDesignVia(io_design_via));
  EXPECT_TRUE(pin_access.clearBlockPinCandidateGroup(second_block_pin, 7u));
  EXPECT_FALSE(pin_access.referencesDesignVia(io_design_via));
  EXPECT_TRUE(design.routingStorage().destroyVia(io_design_via));
}

TEST_F(DesignStorageTest, RejectsInvalidCandidateReferencesAndPreferredIndexes)
{
  const auto input_port = library.masterPortStorage().createMasterPort(input_term);
  const auto output_port = library.masterPortStorage().createMasterPort(output_term);
  const auto instance = design.netlistStorage().createInstance(createInstance("u1"));
  const auto input_pin = design.netlistStorage().findInstancePin(instance, input_term);
  DesignPinAccessStorage pin_access{design.designRegistry(), tech, library.libraryRegistry(), design.netlistStorage(), design.rootId()};

  const auto point = DesignPinAccessPoint{.point = {10, 20}, .layer = routing_layer};
  pin_access.setMasterPortCandidateGroup(input_port, 1u, {point});
  pin_access.setInstancePinAccessGroup(instance, 1u);

  EXPECT_THROW(pin_access.setPreferredInstancePinAccess(
                   input_pin, {{.master_port = output_port, .group_index = 1u, .point_index = 0u}}),
               std::invalid_argument);
  EXPECT_THROW(pin_access.setPreferredInstancePinAccess(
                   input_pin, {{.master_port = input_port, .group_index = 1u, .point_index = 1u}}),
               std::invalid_argument);
  EXPECT_THROW(pin_access.setPreferredInstancePinAccess(
                   input_pin,
                   {{.master_port = input_port, .group_index = 1u, .point_index = 0u},
                    {.master_port = input_port, .group_index = 1u, .point_index = 0u}}),
               std::invalid_argument);

  auto no_layer = point;
  no_layer.layer = {};
  EXPECT_THROW(pin_access.setMasterPortCandidateGroup(input_port, 2u, {no_layer}), std::invalid_argument);

  auto both_vias = point;
  both_vias.vias = {{.cut_count = 1u, .tech_via = tech_via, .design_via = DesignViaId{input_pin.entity()}}};
  EXPECT_THROW(pin_access.setMasterPortCandidateGroup(input_port, 2u, {both_vias}), std::invalid_argument);

  auto& registry = design.designRegistry().registry();
  registry.get<DesignPinAccessCatalog>(design.rootId().entity()).master_port_groups.front().groups.front().points.front().layer = {};
  EXPECT_THROW(pin_access.validateRestoredState(), std::runtime_error);
}

TEST_F(DesignStorageTest, AppendsIoPinCandidatesWithoutPartialState)
{
  const auto io = design.netlistStorage().createIoPin(
      DesignIoPin{.name = "IO"},
      {{.flags = DesignIoPinPortFlag::kExplicit}, {.flags = DesignIoPinPortFlag::kExplicit}});
  DesignPinAccessStorage pin_access{design.designRegistry(), tech, library.libraryRegistry(), design.netlistStorage(), design.rootId()};
  const DesignPinAccessPoint first{.point = {100, 200}, .layer = routing_layer};
  const DesignPinAccessPoint second{.point = {300, 400}, .layer = routing_layer};

  pin_access.appendIoPinPortCandidatePoints(io, 0u, 4u, {first, second});
  pin_access.appendIoPinPortCandidatePoints(io, 0u, 4u, {first});
  pin_access.appendIoPinPortCandidatePoints(io, 1u, 9u, {second});
  pin_access.appendIoPinPortCandidatePoints(io, 1u, 10u, {});

  ASSERT_EQ(pin_access.ioPinPortCandidateGroups(io, 0u).size(), 1u);
  const auto& group = pin_access.ioPinPortCandidateGroup(io, 0u, 4u);
  ASSERT_EQ(group.points.size(), 3u);
  EXPECT_EQ(group.points[0].point, first.point);
  EXPECT_EQ(group.points[1].point, second.point);
  EXPECT_EQ(group.points[2].point, first.point);
  EXPECT_TRUE(pin_access.hasIoPinPortCandidateGroup(io, 1u, 9u));
  EXPECT_FALSE(pin_access.hasIoPinPortCandidateGroup(io, 1u, 10u));

  auto invalid = second;
  invalid.layer = {};
  EXPECT_THROW(pin_access.appendIoPinPortCandidatePoints(io, 0u, 4u, {second, invalid}), std::invalid_argument);
  ASSERT_EQ(pin_access.ioPinPortCandidateGroup(io, 0u, 4u).points.size(), 3u);
  EXPECT_THROW(pin_access.appendIoPinPortCandidatePoints(io, 1u, 10u, {second, invalid}), std::invalid_argument);
  EXPECT_FALSE(pin_access.hasIoPinPortCandidateGroup(io, 1u, 10u));
  EXPECT_THROW(pin_access.appendIoPinPortCandidatePoints(io, 2u, 1u, {second}), std::out_of_range);
}

TEST_F(DesignStorageTest, InvalidatesOnlyEditedBlockPinCandidates)
{
  auto& netlist = design.netlistStorage();
  const auto term = netlist.createBlockTerm(
      DesignBlockTerm{.name = "OUT"},
      {{.flags = DesignIoPinPortFlag::kExplicit}, {.flags = DesignIoPinPortFlag::kExplicit}});
  const auto pins = netlist.blockPins(term);
  ASSERT_EQ(pins.size(), 2u);
  const auto first = pins[0];
  const auto second = pins[1];
  DesignPinAccessStorage pin_access{design.designRegistry(), tech, library.libraryRegistry(), netlist, design.rootId()};
  const DesignPinAccessPoint point{.point = {100, 200}, .layer = routing_layer};
  pin_access.setBlockPinCandidateGroup(first, 1u, {point});
  pin_access.setBlockPinCandidateGroup(second, 1u, {point});

  const auto& current = netlist.blockPin(first);
  auto changed = DesignBlockPinInput{.flags = current.flags,
                                     .placement_status = current.placement_status,
                                     .origin = current.origin,
                                     .orientation = current.orientation,
                                     .rectangles = {{.layer = layer, .rectangle = {0, 0, 20, 20}}}};
  netlist.updateBlockPin(first, changed);
  EXPECT_TRUE(pin_access.blockPinCandidateGroups(first).empty());
  ASSERT_EQ(pin_access.blockPinCandidateGroups(second).size(), 1u);
  EXPECT_EQ(pin_access.blockPinCandidateGroup(second, 1u).points.front().point, point.point);
  EXPECT_EQ(netlist.blockPin(first).term, term);
  EXPECT_EQ(netlist.blockPins(term)[0], first);
  EXPECT_NO_THROW(pin_access.validateRestoredState());
}

TEST_F(DesignStorageTest, KeepsBlockPinCandidatesIndependentWhenOnePinIsDeleted)
{
  auto& netlist = design.netlistStorage();
  const auto term = netlist.createIoPin(
      DesignIoPin{.name = "OUT"},
      {{.flags = DesignIoPinPortFlag::kExplicit}, {.flags = DesignIoPinPortFlag::kExplicit}});
  const auto pins = netlist.blockPins(term);
  ASSERT_EQ(pins.size(), 2u);
  const auto first = pins[0];
  const auto second = pins[1];
  DesignPinAccessStorage pin_access{design.designRegistry(), tech, library.libraryRegistry(), netlist, design.rootId()};
  const DesignPinAccessPoint first_point{.point = {100, 200}, .layer = routing_layer};
  const DesignPinAccessPoint second_point{.point = {300, 400}, .layer = routing_layer};

  pin_access.setBlockPinCandidateGroup(first, 1u, {first_point});
  pin_access.setBlockPinCandidateGroup(second, 1u, {second_point});
  pin_access.appendBlockPinCandidatePoints(second, 1u, {first_point});
  EXPECT_EQ(pin_access.blockPinCandidateGroup(first, 1u).points.front().point, first_point.point);
  ASSERT_EQ(pin_access.blockPinCandidateGroup(second, 1u).points.size(), 2u);
  EXPECT_EQ(pin_access.blockPinCandidateGroup(second, 1u).points.front().point, second_point.point);

  ASSERT_TRUE(netlist.destroyBlockPin(first));
  EXPECT_FALSE(netlist.contains(first));
  ASSERT_EQ(netlist.blockPins(term).size(), 1u);
  EXPECT_EQ(netlist.blockPins(term).front(), second);
  EXPECT_EQ(netlist.blockPin(second).term, term);
  EXPECT_THROW((void) pin_access.blockPinCandidateGroups(first), std::out_of_range);
  ASSERT_EQ(pin_access.blockPinCandidateGroups(second).size(), 1u);
  ASSERT_EQ(pin_access.blockPinCandidateGroup(second, 1u).points.size(), 2u);
  EXPECT_EQ(pin_access.blockPinCandidateGroup(second, 1u).points.front().point, second_point.point);
  EXPECT_NO_THROW(pin_access.validateRestoredState());
}

}  // namespace
}  // namespace eccdb
