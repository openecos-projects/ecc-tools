// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <span>
#include <vector>

#include "DesignTestFixture.h"
#include "context/DatabaseContext.h"
#include "design/geometry/DesignGeometryStorage.h"
#include "design/guide/storage/DesignGuideStorage.h"
#include "design/pin_access/model/PinAccessComponents.h"
#include "design/routing/storage/DesignRoutingStorage.h"
#include "service/connectivity/ConnectivityService.h"

namespace eccdb {
namespace {

class ConnectivityServiceTest : public DesignStorageTest
{
 protected:
  void SetUp() override
  {
    DesignStorageTest::SetUp();
    context = std::make_unique<DatabaseContext>(design);
    service = std::make_unique<ConnectivityService>(*context);
  }

  std::unique_ptr<DatabaseContext> context;
  std::unique_ptr<ConnectivityService> service;
};

TEST_F(ConnectivityServiceTest, RegularAndSpecialRelationsCanCoexistAndReconnect)
{
  auto& netlist = design.netlistStorage();
  const auto instance = netlist.createInstance(createInstance("u1"));
  const auto term = netlist.findInstanceTerm(instance, "A");
  const auto regular = service->createNet(DesignNet{.name = "n1"});
  const auto replacement = service->createNet(DesignNet{.name = "n2"});
  const auto special = service->createNet(DesignNet{.name = "VDD"}, true);

  service->connect(term, regular);
  service->connect(term, special);
  EXPECT_EQ(netlist.instanceTerm(term).net, regular);
  EXPECT_EQ(netlist.instanceTerm(term).special_net, special);

  service->reconnect(term, replacement);
  EXPECT_EQ(netlist.instanceTerm(term).net, replacement);
  EXPECT_EQ(netlist.instanceTerm(term).special_net, special);
  EXPECT_TRUE(netlist.instanceTerms(regular).empty());
  ASSERT_EQ(netlist.instanceTerms(replacement).size(), 1u);
  EXPECT_EQ(netlist.instanceTerms(replacement).front(), term);
}

TEST_F(ConnectivityServiceTest, ReconnectScopeRejectsBeforeChangingEitherNet)
{
  auto& netlist = design.netlistStorage();
  const auto instance = netlist.createInstance(createInstance("u1"));
  const auto term = netlist.findInstanceTerm(instance, "A");
  const auto source = service->createNet(DesignNet{.name = "source"});
  const auto target = service->createNet(DesignNet{.name = "target"});
  const auto unrelated = service->createNet(DesignNet{.name = "unrelated"});
  service->connect(term, source);

  const std::array allowed{unrelated};
  EXPECT_THROW(service->reconnect(term, target, allowed), std::invalid_argument);
  EXPECT_EQ(netlist.instanceTerm(term).net, source);
  ASSERT_EQ(netlist.instanceTerms(source).size(), 1u);
  EXPECT_EQ(netlist.instanceTerms(source).front(), term);
  EXPECT_TRUE(netlist.instanceTerms(target).empty());
}

TEST_F(ConnectivityServiceTest, BatchReplacementMovesBothTermKindsAndRejectsDuplicates)
{
  auto& netlist = design.netlistStorage();
  const auto first_instance = netlist.createInstance(createInstance("u1"));
  const auto second_instance = netlist.createInstance(createInstance("u2"));
  const auto first_term = netlist.findInstanceTerm(first_instance, "A");
  const auto second_term = netlist.findInstanceTerm(second_instance, "A");
  const auto block_term = service->createBlockTerm(DesignBlockTerm{.name = "IN"});
  const auto source = service->createNet(DesignNet{.name = "source"});
  const auto target = service->createNet(DesignNet{.name = "target"});
  const auto unrelated = service->createNet(DesignNet{.name = "unrelated"});
  service->connect(first_term, source);

  const std::array instance_terms{first_term, second_term};
  const std::array block_terms{block_term};
  const std::array rejected_scope{unrelated};
  EXPECT_THROW(service->replaceNetConnections(target, instance_terms, block_terms, rejected_scope), std::invalid_argument);
  EXPECT_EQ(netlist.instanceTerm(first_term).net, source);
  EXPECT_FALSE(netlist.instanceTerm(second_term).net);
  EXPECT_FALSE(netlist.blockTerm(block_term).net);
  EXPECT_TRUE(netlist.instanceTerms(target).empty());

  const std::array allowed{source};
  service->replaceNetConnections(target, instance_terms, block_terms, allowed);

  EXPECT_TRUE(netlist.instanceTerms(source).empty());
  ASSERT_EQ(netlist.instanceTerms(target).size(), 2u);
  EXPECT_EQ(netlist.instanceTerm(first_term).net, target);
  EXPECT_EQ(netlist.instanceTerm(second_term).net, target);
  ASSERT_EQ(netlist.blockTerms(target).size(), 1u);
  EXPECT_EQ(netlist.blockTerm(block_term).net, target);

  const std::array duplicate_terms{first_term, first_term};
  EXPECT_THROW(service->replaceNetConnections(target,
                                              duplicate_terms,
                                              std::span<const DesignBlockTermId>{},
                                              std::span<const DesignNetId>{}),
               std::invalid_argument);
  EXPECT_EQ(netlist.instanceTerm(first_term).net, target);
  EXPECT_EQ(netlist.instanceTerm(second_term).net, target);
  EXPECT_EQ(netlist.blockTerm(block_term).net, target);
}

TEST_F(ConnectivityServiceTest, DeleteNetDetachesTermsAndDestroysOwnedRouting)
{
  auto& netlist = design.netlistStorage();
  auto& routing = design.routingStorage();
  auto& geometry = design.geometryStorage();
  auto& guides = design.guideStorage();
  const auto instance = netlist.createInstance(createInstance("u1"));
  const auto term = netlist.findInstanceTerm(instance, "A");
  const auto net = service->createNet(DesignNet{.name = "route"}, true);
  service->connect(term, net);
  const auto wire = routing.createWire(DesignWire{.net = net});
  const auto standalone = geometry.appendNetShape(
      net, DesignNetRectangle{.layer = routing_layer, .rectangle = {0, 0, 20, 20}});
  guides.replaceGuides(net, {{.box = {0, 0, 20, 20}, .layer = routing_layer}});
  const auto guide = guides.guides(net).front();
  guides.replaceTrackAssignments(net, {{.layer = routing_layer, .box = {0, 10, 20, 10}}});
  const auto track = guides.trackAssignments(net).front();

  ASSERT_TRUE(routing.contains(wire));
  ASSERT_TRUE(design.geometryRegistry().contains(standalone));
  ASSERT_TRUE(design.geometryRegistry().contains(guide));
  ASSERT_TRUE(design.geometryRegistry().contains(track));
  EXPECT_TRUE(service->deleteNet(net));
  EXPECT_FALSE(netlist.contains(net));
  EXPECT_FALSE(routing.contains(wire));
  EXPECT_FALSE(design.geometryRegistry().contains(standalone));
  EXPECT_FALSE(design.geometryRegistry().contains(guide));
  EXPECT_FALSE(design.geometryRegistry().contains(track));
  EXPECT_FALSE(netlist.instanceTerm(term).net);
  EXPECT_FALSE(netlist.instanceTerm(term).special_net);
}

TEST_F(ConnectivityServiceTest, MustJoinRejectsInvalidPublicConnectivityEdits)
{
  auto& netlist = design.netlistStorage();
  const auto first_instance = netlist.createInstance(createInstance("u1"));
  const auto second_instance = netlist.createInstance(createInstance("u2"));
  const auto first_term = netlist.findInstanceTerm(first_instance, "A");
  const auto second_term = netlist.findInstanceTerm(second_instance, "A");
  const auto block_term = service->createBlockTerm(DesignBlockTerm{.name = "IN"});
  const auto must_join = service->createNet(
      DesignNet{.name = "__mustjoin_service", .flags = DesignNetFlag::kMustJoin});

  service->connect(first_term, must_join);
  EXPECT_THROW(service->connect(second_term, must_join), std::invalid_argument);
  EXPECT_THROW(service->connect(block_term, must_join), std::invalid_argument);
  EXPECT_THROW(service->disconnect(first_term, must_join), std::invalid_argument);
  EXPECT_THROW(service->replaceNetConnections(must_join,
                                              std::array{first_term, second_term},
                                              std::span<const DesignBlockTermId>{}),
               std::invalid_argument);
  ASSERT_EQ(netlist.instanceTerms(must_join).size(), 1u);
  EXPECT_EQ(netlist.instanceTerms(must_join).front(), first_term);
}

TEST_F(ConnectivityServiceTest, DeleteBlockTermClearsPinsAndPinAccessCandidates)
{
  const auto net = service->createNet(DesignNet{.name = "input_net"});
  const auto term = service->createBlockTerm(
      DesignBlockTerm{.name = "IN"},
      {{.rectangles = {{.layer = layer, .rectangle = {0, 0, 10, 10}}}}});
  service->connect(term, net);
  const auto pin = design.netlistStorage().blockPins(term).front();
  design.pinAccessStorage().setBlockPinCandidateGroup(
      pin,
      0,
      {DesignPinAccessPoint{.point = {2, 2},
                            .layer = routing_layer,
                            .access_directions = DesignPinAccessDirection::kEast}});
  ASSERT_TRUE(design.pinAccessStorage().hasBlockPinCandidateGroup(pin, 0));

  EXPECT_TRUE(service->deleteBlockTerm(term));
  EXPECT_FALSE(design.netlistStorage().contains(term));
  EXPECT_FALSE(design.netlistStorage().contains(pin));
  EXPECT_TRUE(design.netlistStorage().blockTerms(net).empty());
  EXPECT_FALSE(design.designRegistry().registry().all_of<DesignBlockPinAccessGroups>(pin.entity()));
}

}  // namespace
}  // namespace eccdb
