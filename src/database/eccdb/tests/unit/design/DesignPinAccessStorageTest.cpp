// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "DesignTestFixture.h"
#include "design/pin_access/storage/DesignPinAccessStorage.h"
#include "design/via/component/ViaComponents.h"

namespace eccdb {
namespace {

TEST_F(DesignStorageTest, StoresSharedMasterCandidatesIoCandidatesAndInstancePreferences)
{
  const auto input_port = library.masterPortStorage().createMasterPort(input_term);
  const auto output_port = library.masterPortStorage().createMasterPort(output_term);
  const auto design_via = design.routingStorage().createVia(
      DesignVia{.name = "DVIA", .rectangles = {{.layer = layer, .rectangle = {-2, -2, 2, 2}}}});
  const auto io_design_via = design.routingStorage().createVia(
      DesignVia{.name = "IO_DVIA", .rectangles = {{.layer = layer, .rectangle = {-2, -2, 2, 2}}}});
  const auto instance = design.netlistStorage().createInstance(createInstance("u1", {100, 200}));
  const auto input_pin = design.netlistStorage().findInstancePin(instance, input_term);
  const auto io = design.netlistStorage().createIoPin(
      DesignIoPin{.name = "OUT", .ports = {{.flags = DesignIoPinPortFlag::kExplicit}, {.flags = DesignIoPinPortFlag::kExplicit}}});

  DesignPinAccessStorage pin_access{design.designRegistry(), tech, library.libraryRegistry(), design.rootId()};
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

  // IO candidates are grouped separately by DEF PORT and remain block-absolute.
  auto io_candidate = candidate({500, 900}, false);
  io_candidate.vias.push_back(DesignPinAccessVia{.cut_count = 1u, .design_via = io_design_via});
  pin_access.setIoPinPortCandidateGroup(io, 1u, 7u, {std::move(io_candidate)});
  ASSERT_EQ(pin_access.ioPinPortCandidateGroups(io, 1u).size(), 1u);
  EXPECT_EQ(pin_access.ioPinPortCandidateGroup(io, 1u, 7u).points.front().point, (Point{500, 900}));
  EXPECT_FALSE(design.routingStorage().destroyVia(io_design_via));

  pin_access.setInstancePinAccessGroup(instance, 3u);
  pin_access.setPreferredInstancePinAccess(
      input_pin, {{.master_port = input_port, .group_index = 3u, .point_index = 1u}});
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
  EXPECT_FALSE(pin_access.clearMasterPortCandidateGroup(input_port, 3u));
  EXPECT_TRUE(pin_access.clearInstancePinAccessGroup(instance));
  EXPECT_TRUE(pin_access.clearMasterPortCandidateGroup(input_port, 3u));
  EXPECT_FALSE(pin_access.referencesDesignVia(design_via));
  EXPECT_TRUE(design.routingStorage().destroyVia(design_via));
  EXPECT_TRUE(pin_access.referencesDesignVia(io_design_via));
  EXPECT_TRUE(pin_access.clearIoPinPortCandidateGroup(io, 1u, 7u));
  EXPECT_FALSE(pin_access.referencesDesignVia(io_design_via));
  EXPECT_TRUE(design.routingStorage().destroyVia(io_design_via));
}

TEST_F(DesignStorageTest, RejectsInvalidCandidateReferencesAndPreferredIndexes)
{
  const auto input_port = library.masterPortStorage().createMasterPort(input_term);
  const auto output_port = library.masterPortStorage().createMasterPort(output_term);
  const auto instance = design.netlistStorage().createInstance(createInstance("u1"));
  const auto input_pin = design.netlistStorage().findInstancePin(instance, input_term);
  DesignPinAccessStorage pin_access{design.designRegistry(), tech, library.libraryRegistry(), design.rootId()};

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

}  // namespace
}  // namespace eccdb
