// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <utility>

#include "DesignTestFixture.h"
#include "service/routing/DesignViaService.h"

namespace eccdb {
namespace {

TEST_F(DesignStorageTest, DesignViaServiceCreatesFindsUpdatesAndDestroysVias)
{
  DesignViaService service{design};
  const auto via = service.createVia(
      DesignViaInput{.name = "LOCAL_VIA", .rectangles = {{.layer = layer, .rectangle = {-4, -4, 4, 4}}}});

  ASSERT_TRUE(via);
  EXPECT_EQ(service.findVia("LOCAL_VIA"), via);
  const auto original_rectangle = service.via(via).rectangles.front();
  EXPECT_EQ(design.geometryRegistry().get<const DesignViaRectangle>(original_rectangle).rectangle, (Rect{-4, -4, 4, 4}));

  service.updateVia(via,
                    DesignViaInput{.name = "LOCAL_VIA_RENAMED",
                                   .rectangles = {{.layer = layer, .rectangle = {-6, -5, 7, 8}, .mask = 3u}}});
  EXPECT_FALSE(service.findVia("LOCAL_VIA"));
  EXPECT_EQ(service.findVia("LOCAL_VIA_RENAMED"), via);
  const auto updated_rectangle = service.via(via).rectangles.front();
  EXPECT_EQ(design.geometryRegistry().get<const DesignViaRectangle>(updated_rectangle).rectangle, (Rect{-6, -5, 7, 8}));
  EXPECT_EQ(design.geometryRegistry().get<const DesignViaRectangle>(updated_rectangle).mask, 3u);
  EXPECT_FALSE(design.geometryRegistry().contains(original_rectangle));

  EXPECT_TRUE(service.destroyVia(via));
  EXPECT_FALSE(design.geometryRegistry().contains(updated_rectangle));
  EXPECT_FALSE(service.findVia("LOCAL_VIA_RENAMED"));
  EXPECT_FALSE(service.destroyVia(via));
  EXPECT_THROW((void) service.via(via), std::out_of_range);
}

TEST_F(DesignStorageTest, DesignViaServicePreservesDuplicateNameAndGeometryValidation)
{
  DesignViaService service{design};
  const auto first = service.createVia(
      DesignViaInput{.name = "LOCAL_VIA", .rectangles = {{.layer = layer, .rectangle = {-4, -4, 4, 4}}}});
  const auto second = service.createVia(
      DesignViaInput{.name = "OTHER_VIA", .rectangles = {{.layer = layer, .rectangle = {-3, -3, 3, 3}}}});

  EXPECT_THROW((void) service.createVia(
                   DesignViaInput{.name = "LOCAL_VIA", .rectangles = {{.layer = layer, .rectangle = {-2, -2, 2, 2}}}}),
               std::invalid_argument);
  EXPECT_THROW(service.updateVia(second,
                                 DesignViaInput{.name = "LOCAL_VIA", .rectangles = {{.layer = layer, .rectangle = {-2, -2, 2, 2}}}}),
               std::invalid_argument);
  EXPECT_THROW((void) service.createVia(
                   DesignViaInput{.name = "INVALID_GEOMETRY", .rectangles = {{.layer = layer, .rectangle = {0, 0, 0, 4}}}}),
               std::invalid_argument);

  EXPECT_EQ(service.findVia("LOCAL_VIA"), first);
  EXPECT_EQ(service.findVia("OTHER_VIA"), second);
}

TEST_F(DesignStorageTest, DesignViaServiceDoesNotDestroyWireReferencedVia)
{
  DesignViaService service{design};
  const auto via = service.createVia(
      DesignViaInput{.name = "WIRE_VIA", .rectangles = {{.layer = layer, .rectangle = {-2, -2, 2, 2}}}});
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "signal"});

  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto shape = design.routingStorage().createShape(
      wire,
      DesignRouteShape{.wire = wire,
                       .layer = routing_layer,
                       .geometry = DesignRouteVia{.origin = {.position = {100, 200}},
                                                  .via = {.design_via = via},
                                                  .exit_layer = routing_layer}});
  EXPECT_TRUE(design.routingStorage().contains(shape));

  EXPECT_FALSE(service.destroyVia(via));
  EXPECT_TRUE(design.routingStorage().destroyWire(wire));
  EXPECT_TRUE(service.destroyVia(via));
}

}  // namespace
}  // namespace eccdb
