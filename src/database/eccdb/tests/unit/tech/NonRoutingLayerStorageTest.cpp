// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "tech/TechStore.h"

namespace eccdb {
namespace {

static_assert(std::is_same_v<decltype(TechLayerInfo::name), std::string>);
static_assert(std::is_same_v<decltype(TechImplantLayer::flags), uint32_t>);
static_assert(std::is_same_v<decltype(TechImplantLayer::min_width), int32_t>);
static_assert(std::is_same_v<decltype(TechImplantSpacingRule::flags), uint32_t>);
static_assert(std::is_same_v<decltype(TechImplantSpacingRule::other_layer), TechImplantLayerId>);
static_assert(std::is_same_v<decltype(TechTrimmedMetalRule::flags), uint32_t>);
static_assert(std::is_same_v<decltype(TechTrimmedMetalRule::mask), uint32_t>);

class NonRoutingLayerStorageTest : public testing::Test
{
 protected:
  void SetUp() override
  {
    poly = database.createMastersliceLayer(TechLayerInfo{.name = "POLY"}, TechMastersliceLayer{.subtype = TechMastersliceType::kTrimMetal});
    n_implant = database.createImplantLayer(TechLayerInfo{.name = "NIMP"},
                                            TechImplantLayer{.flags = TechImplantLayerFlag::kHasMinWidth, .min_width = 40});
    m1 = database.createRoutingLayer(TechLayerInfo{.name = "M1", .mask_count = 2}, TechRoutingLayer{.width = 80});
    v1 = database.createCutLayer(TechLayerInfo{.name = "V1"}, TechCutLayer{});
    overlap = database.createOverlapLayer(TechLayerInfo{.name = "OVERLAP"});
  }

  TechStore database;
  TechMastersliceLayerId poly;
  TechImplantLayerId n_implant;
  TechRoutingLayerId m1;
  TechCutLayerId v1;
  TechOverlapLayerId overlap;
};

TEST_F(NonRoutingLayerStorageTest, SharesOneRegistryButExcludesOverlapFromProcessOrder)
{
  const auto& registry = database.techRegistry().registry();

  EXPECT_NE(poly.entity(), n_implant.entity());
  EXPECT_NE(n_implant.entity(), m1.entity());
  EXPECT_NE(m1.entity(), v1.entity());
  EXPECT_TRUE((registry.all_of<TechLayerInfo, TechMastersliceLayer>(poly.entity())));
  EXPECT_TRUE((registry.all_of<TechLayerInfo, TechImplantLayer>(n_implant.entity())));
  EXPECT_TRUE((registry.all_of<TechLayerInfo, TechRoutingLayer>(m1.entity())));
  EXPECT_TRUE((registry.all_of<TechLayerInfo, TechCutLayer>(v1.entity())));
  EXPECT_TRUE((registry.all_of<TechLayerInfo, TechOverlapLayer>(overlap.entity())));

  EXPECT_EQ(database.layerSequence().size(), 4u);
  EXPECT_TRUE(database.isBelow(TechLayerId{poly.entity()}, TechLayerId{m1.entity()}));
  EXPECT_EQ(database.routingLevel(m1), 1u);
  EXPECT_FALSE(database.layerPosition(TechLayerId{overlap.entity()}).has_value());
}

TEST_F(NonRoutingLayerStorageTest, StoresMastersliceSubtypeAndTrimmedMetalRule)
{
  auto& masterslice = database.mastersliceLayerStorage();
  masterslice.setTrimmedMetalRule(poly, TechTrimmedMetalRule{.flags = TechTrimmedMetalRuleFlag::kHasMask, .metal_layer = m1, .mask = 2});

  EXPECT_EQ(masterslice.mastersliceLayer(poly).subtype, TechMastersliceType::kTrimMetal);
  ASSERT_TRUE(masterslice.hasTrimmedMetalRule(poly));
  const auto& trim = masterslice.trimmedMetalRule(poly);
  EXPECT_EQ(trim.metal_layer, m1);
  EXPECT_NE((trim.flags & TechTrimmedMetalRuleFlag::kHasMask), 0u);
  EXPECT_EQ(trim.mask, 2u);

  const auto non_trim
      = database.createMastersliceLayer(TechLayerInfo{.name = "NWELL"}, TechMastersliceLayer{.subtype = TechMastersliceType::kNWell});
  EXPECT_THROW((void) masterslice.setTrimmedMetalRule(non_trim, TechTrimmedMetalRule{.metal_layer = m1}), std::invalid_argument);
}

TEST_F(NonRoutingLayerStorageTest, ValidatesTrimmedMetalMaskFlags)
{
  auto& masterslice = database.mastersliceLayerStorage();

  masterslice.setTrimmedMetalRule(poly, TechTrimmedMetalRule{.metal_layer = m1});
  const auto& absent_mask = masterslice.trimmedMetalRule(poly);
  EXPECT_EQ(absent_mask.flags, 0u);
  EXPECT_EQ(absent_mask.mask, 0u);

  EXPECT_THROW((void) masterslice.setTrimmedMetalRule(poly, TechTrimmedMetalRule{.metal_layer = m1, .mask = 1}), std::invalid_argument);
  EXPECT_THROW(
      (void) masterslice.setTrimmedMetalRule(poly, TechTrimmedMetalRule{.flags = TechTrimmedMetalRuleFlag::kHasMask, .metal_layer = m1}),
      std::invalid_argument);
  EXPECT_THROW((void) masterslice.setTrimmedMetalRule(
                   poly, TechTrimmedMetalRule{.flags = TechTrimmedMetalRuleFlag::kHasMask, .metal_layer = m1, .mask = 3}),
               std::invalid_argument);
  EXPECT_THROW((void) masterslice.setTrimmedMetalRule(poly, TechTrimmedMetalRule{.flags = 1u << 31, .metal_layer = m1}),
               std::invalid_argument);
}

TEST_F(NonRoutingLayerStorageTest, StoresImplantSpacingAsTypedValuesOnTheLayerEntity)
{
  const auto p_implant = database.createImplantLayer(TechLayerInfo{.name = "PIMP"},
                                                     TechImplantLayer{.flags = TechImplantLayerFlag::kHasMinWidth, .min_width = 50});
  auto& implants = database.implantLayerStorage();
  const auto& registry = database.techRegistry().registry();
  const auto entity_count = registry.storage<TechEntity>()->size();
  implants.addSpacingRule(
      n_implant, TechImplantSpacingRule{.flags = TechImplantSpacingRuleFlag::kHasOtherLayer, .min_spacing = 30, .other_layer = p_implant});

  ASSERT_EQ(implants.spacingRules(n_implant).size(), 1u);
  const auto& rule = implants.spacingRules(n_implant).front();
  EXPECT_EQ(rule.min_spacing, 30);
  EXPECT_NE((rule.flags & TechImplantSpacingRuleFlag::kHasOtherLayer), 0u);
  EXPECT_EQ(rule.other_layer, p_implant);
  EXPECT_EQ(registry.storage<TechEntity>()->size(), entity_count);
  EXPECT_TRUE(registry.all_of<TechImplantSpacingRules>(n_implant.entity()));
}

TEST_F(NonRoutingLayerStorageTest, ValidatesImplantFlagControlledPayloads)
{
  auto& implants = database.implantLayerStorage();
  const auto p_implant = database.createImplantLayer(TechLayerInfo{.name = "PIMP"},
                                                     TechImplantLayer{.flags = TechImplantLayerFlag::kHasMinWidth, .min_width = 50});
  const auto absent_width = database.createImplantLayer(TechLayerInfo{.name = "AIMP"}, TechImplantLayer{});

  EXPECT_EQ(implants.implantLayer(absent_width).flags, 0u);
  EXPECT_EQ(implants.implantLayer(absent_width).min_width, 0);
  EXPECT_THROW((void) database.createImplantLayer(TechLayerInfo{.name = "BADWIDTH"}, TechImplantLayer{.min_width = 1}),
               std::invalid_argument);
  EXPECT_THROW(
      (void) database.createImplantLayer(TechLayerInfo{.name = "ZEROWIDTH"}, TechImplantLayer{.flags = TechImplantLayerFlag::kHasMinWidth}),
      std::invalid_argument);
  EXPECT_THROW((void) database.createImplantLayer(TechLayerInfo{.name = "UNKNOWNWIDTH"}, TechImplantLayer{.flags = 1u << 31}),
               std::invalid_argument);

  implants.addSpacingRule(n_implant, TechImplantSpacingRule{.min_spacing = 30});
  ASSERT_EQ(implants.spacingRules(n_implant).size(), 1u);
  EXPECT_EQ(implants.spacingRules(n_implant).front().flags, 0u);
  EXPECT_FALSE(static_cast<bool>(implants.spacingRules(n_implant).front().other_layer));
  EXPECT_THROW(implants.addSpacingRule(n_implant, TechImplantSpacingRule{.min_spacing = 30, .other_layer = p_implant}),
               std::invalid_argument);
  EXPECT_THROW(
      implants.addSpacingRule(n_implant, TechImplantSpacingRule{.flags = TechImplantSpacingRuleFlag::kHasOtherLayer, .min_spacing = 30}),
      std::invalid_argument);
  EXPECT_THROW(implants.addSpacingRule(n_implant, TechImplantSpacingRule{.flags = TechImplantSpacingRuleFlag::kHasOtherLayer,
                                                                         .min_spacing = 30,
                                                                         .other_layer = n_implant}),
               std::invalid_argument);
  EXPECT_THROW(implants.addSpacingRule(n_implant, TechImplantSpacingRule{.flags = TechImplantSpacingRuleFlag::kHasOtherLayer,
                                                                         .min_spacing = 30,
                                                                         .other_layer = TechImplantLayerId{m1.entity()}}),
               std::invalid_argument);
  EXPECT_THROW(implants.addSpacingRule(n_implant, TechImplantSpacingRule{.flags = 1u << 31, .min_spacing = 30}), std::invalid_argument);
}

TEST_F(NonRoutingLayerStorageTest, AllowsFixedViaBetweenMastersliceAndRouting)
{
  const auto via = database.viaMasterStorage().createFixedViaMaster(
      TechViaMaster{.name = "CONT"}, TechViaMasterShapeInput{.bottom_layer = poly,
                                                             .bottom_geometry = {.rects = {{.ll_x = 0, .ll_y = 0, .ur_x = 20, .ur_y = 20}}},
                                                             .cut_layer = v1,
                                                             .cut_geometry = {.rects = {{.ll_x = 5, .ll_y = 5, .ur_x = 15, .ur_y = 15}}},
                                                             .top_layer = m1,
                                                             .top_geometry = {.rects = {{.ll_x = 0, .ll_y = 0, .ur_x = 20, .ur_y = 20}}}});

  const auto& geometry = database.viaMasterStorage().geometry(via);
  EXPECT_EQ(geometry.bottom_layer.kind, TechConductorLayerKind::kMasterslice);
  EXPECT_EQ(geometry.bottom_layer.layer(), TechLayerId{poly.entity()});
  EXPECT_EQ(geometry.top_layer.kind, TechConductorLayerKind::kRouting);
  EXPECT_EQ(geometry.top_layer.layer(), TechLayerId{m1.entity()});
}

}  // namespace
}  // namespace eccdb
