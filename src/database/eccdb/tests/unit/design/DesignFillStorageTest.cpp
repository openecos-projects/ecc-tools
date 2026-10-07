// SPDX-License-Identifier: Apache-2.0

#include <stdexcept>

#include "DesignTestFixture.h"
#include "design/fill/model/FillComponents.h"

namespace eccdb {
namespace {

TEST_F(DesignStorageTest, ManagesRectangularLayerFills)
{
  tech.registry().get<TechLayerInfo>(layer.entity()).mask_count = 3;
  const auto id = design.fillStorage().createFill(DesignFillInput{.layer = layer,
                                                                  .flags = DesignFillFlag::kOpc | DesignFillFlag::kHasMask,
                                                                  .mask = 2,
                                                                  .rectangles = {Rect{0, 0, 10, 20}, Rect{30, 40, 50, 60}}});

  ASSERT_TRUE(design.fillStorage().contains(id));
  EXPECT_EQ(design.fillStorage().fillCount(), 1u);
  EXPECT_EQ(design.fillStorage().fill(id).rectangles.size(), 2u);
  EXPECT_NE(design.fillStorage().fill(id).flags & DesignFillFlag::kOpc, 0u);

  design.fillStorage().updateFill(id, DesignFillInput{.layer = layer, .rectangles = {Rect{-10, -20, 10, 20}}});
  EXPECT_EQ(design.fillStorage().rectangle(design.fillStorage().fill(id).rectangles.front()), (Rect{-10, -20, 10, 20}));

  EXPECT_THROW((void) design.fillStorage().createFill(
                   DesignFillInput{.layer = layer, .flags = DesignFillFlag::kHasMask, .mask = 4, .rectangles = {Rect{0, 0, 10, 10}}}),
               std::invalid_argument);
  EXPECT_THROW((void) design.fillStorage().createFill(DesignFillInput{.layer = layer, .rectangles = {Rect{0, 0, 0, 10}}}),
               std::invalid_argument);

  EXPECT_TRUE(design.fillStorage().destroyFill(id));
  EXPECT_FALSE(design.fillStorage().contains(id));
}

TEST_F(DesignStorageTest, StoresMustJoinComponentPinOnRegularNet)
{
  const auto first = design.netlistStorage().createInstance(createInstance("u1"));
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "__mustjoin_test", .flags = DesignNetFlag::kMustJoin});
  const auto pin = design.netlistStorage().findInstancePin(first, output_term);
  design.netlistStorage().connect(pin, net);
  ASSERT_EQ(design.netlistStorage().instancePins(net).size(), 1u);
  EXPECT_EQ(design.netlistStorage().instancePins(net).front(), pin);
  EXPECT_EQ(design.netlistStorage().instancePin(pin).net, net);

  EXPECT_THROW(
      (void) design.netlistStorage().createSpecialNet(DesignNet{.name = "invalid_special_mustjoin", .flags = DesignNetFlag::kMustJoin}),
      std::invalid_argument);
}

}  // namespace
}  // namespace eccdb
