// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "tech/cut_layer/storage/CutLayerStorage.h"
#include "tech/routing_layer/storage/RoutingLayerStorage.h"

namespace eccdb {
namespace {

static_assert(std::is_same_v<decltype(TechRoutingPrlSpacingTableRule::widths), std::vector<int32_t>>);
static_assert(std::is_same_v<decltype(TechRoutingPrlSpacingTableRule::cells), std::vector<int32_t>>);
static_assert(std::is_same_v<decltype(TechRoutingCurrentDensityRule::table_entries), std::vector<double>>);
static_assert(std::is_same_v<decltype(TechRoutingLef58AreaRule::trim_layer_name), std::string>);
static_assert(std::is_same_v<decltype(TechRoutingLef58MinimumCutClass::cutclass_name), std::string>);

class RoutingLayerStorageTest : public testing::Test
{
 protected:
  void SetUp() override
  {
    TechLayerInfo base{.name = "M1", .mask_count = 2};

    TechRoutingLayer routing;
    routing.width = 80;
    routing.min_width = 40;

    layer = storage.createLayer(std::move(base), std::move(routing));
  }

  TechRegistry registry;
  TechRoutingLayerStorage storage{registry};
  TechRoutingLayerId layer;
};

TEST_F(RoutingLayerStorageTest, CreatesLayerAndRuleInSharedRegistry)
{
  const auto rule = storage.addSpacingRule(layer, TechRoutingSpacingRule{.min_spacing = 90});

  EXPECT_TRUE(storage.contains(layer));
  EXPECT_EQ(storage.layerInfo(layer).name, "M1");
  EXPECT_EQ(storage.layerInfo(layer).mask_count, 2u);
  EXPECT_EQ(storage.routingLayer(layer).width, 80);
  EXPECT_TRUE((storage.registry().all_of<TechLayerInfo, TechRoutingLayer>(layer.entity())));
  EXPECT_TRUE((storage.registry().all_of<TechRuleOwner, TechRoutingSpacingRule>(rule.entity())));
  EXPECT_TRUE((storage.registry().all_of<TechRuleRefs<TechRoutingSpacingRuleId>>(layer.entity())));
  EXPECT_EQ(storage.ruleOwner(rule), layer);
}

TEST_F(RoutingLayerStorageTest, StoresPolyRoutingSubtypeFlag)
{
  auto& routing = storage.routingLayer(layer);
  routing.flags |= TechRoutingLayerFlag::kPolyRouting;

  const auto& stored = std::as_const(storage).routingLayer(layer);
  EXPECT_NE(stored.flags & TechRoutingLayerFlag::kPolyRouting, 0u);
}

TEST(TechRegistryTest, GivesCutAndRoutingLayersOneEntitySpace)
{
  TechRegistry registry;
  TechCutLayerStorage cut_storage{registry};
  TechRoutingLayerStorage routing_storage{registry};

  TechLayerInfo cut_base{.name = "V1"};
  TechLayerInfo routing_base{.name = "M1"};

  const auto cut_layer = cut_storage.createLayer(std::move(cut_base), TechCutLayer{});
  const auto routing_layer = routing_storage.createLayer(std::move(routing_base), TechRoutingLayer{});

  EXPECT_NE(cut_layer.entity(), routing_layer.entity());
  EXPECT_TRUE((registry.registry().all_of<TechLayerInfo, TechCutLayer>(cut_layer.entity())));
  EXPECT_TRUE((registry.registry().all_of<TechLayerInfo, TechRoutingLayer>(routing_layer.entity())));
}

TEST_F(RoutingLayerStorageTest, StoresPrlMatrixInsideItsRuleComponent)
{
  TechRoutingPrlSpacingTableRule table;
  table.widths = {100, 200};
  table.parallel_run_lengths = {0, 100};
  table.cells = {10, 20, 30, 40};
  table.except_withins = {{.width_index = 1, .low = 20, .high = 40}};
  table.influences = {{.width = 100, .within = 200, .spacing = 50}};

  const auto id = storage.addPrlSpacingTableRule(layer, std::move(table));
  const auto& stored = storage.rule(id);

  EXPECT_EQ(stored.widths, (std::vector<int32_t>{100, 200}));
  EXPECT_EQ(stored.parallel_run_lengths, (std::vector<int32_t>{0, 100}));
  EXPECT_EQ(stored.cells, (std::vector<int32_t>{10, 20, 30, 40}));
  EXPECT_EQ(storage.prlSpacingTableCell(id, 1, 0), 30);
  EXPECT_EQ(storage.prlSpacingFor(id, 150, 120, 50), 10);
  EXPECT_EQ(storage.prlSpacingFor(id, 250, 220, 150), 40);
  EXPECT_EQ(storage.prlInfluenceSpacingFor(id, 150, 100), 50);
}

TEST_F(RoutingLayerStorageTest, ReadsTwoWidthAndCurrentDensityMatrices)
{
  TechRoutingTwoWidthsSpacingTableRule two_widths;
  two_widths.widths = {{.width = 100}, {.width = 200}};
  two_widths.cells = {10, 20, 30, 40};
  const auto two_widths_id = storage.addTwoWidthsSpacingTableRule(layer, std::move(two_widths));

  EXPECT_EQ(storage.twoWidthsSpacingTableCell(two_widths_id, 1, 0), 30);
  EXPECT_EQ(storage.twoWidthsSpacingFor(two_widths_id, 250, 150, 0), 30);

  TechRoutingCurrentDensityRule density;
  density.signal = TechRoutingCurrentDensitySignal::kAc;
  density.type = TechRoutingCurrentDensityType::kPeak;
  density.frequencies = {10.0, 20.0};
  density.widths = {100, 200};
  density.table_entries = {1.0, 2.0, 3.0, 4.0};
  const auto density_id = storage.addCurrentDensityRule(layer, std::move(density));

  EXPECT_DOUBLE_EQ(storage.currentDensityTableEntry(density_id, 1, 1), 4.0);
  EXPECT_DOUBLE_EQ(storage.currentDensityAt(density_id, 15.0, 150), 2.5);
}

TEST_F(RoutingLayerStorageTest, SupportsSameWidthTwoWidthAxesWithPrlBreakpoints)
{
  TechRoutingTwoWidthsSpacingTableRule table;
  table.widths = {{.width = 0}, {.width = 0, .has_prl = true, .prl = 0}, {.width = 100, .has_prl = true, .prl = 100}};
  table.cells = {10, 20, 30, 20, 30, 40, 30, 40, 50};

  const auto id = storage.addTwoWidthsSpacingTableRule(layer, std::move(table));

  EXPECT_EQ(storage.twoWidthsSpacingFor(id, 10, 10, 0), 10);
  EXPECT_EQ(storage.twoWidthsSpacingFor(id, 10, 10, 10), 30);
}

TEST_F(RoutingLayerStorageTest, StoresNestedRowsAndNamesWithTheRule)
{
  TechRoutingLef58AreaRule area;
  area.flags = TechRoutingLef58AreaRuleFlag::kHasTrimLayer;
  area.min_area = 1200;
  area.trim_layer_name = "M2";
  area.except_min_sizes = {{.min_width = 20, .min_length = 40}, {.min_width = 30, .min_length = 60}};
  const auto area_id = storage.addLef58AreaRule(layer, std::move(area));

  TechRoutingLef58MinimumCutRule minimum_cut;
  minimum_cut.cutclasses = {{.cutclass_name = "VIA12", .num_cuts = 2}, {.cutclass_name = "VIA23", .num_cuts = 3}};
  const auto minimum_cut_id = storage.addLef58MinimumCutRule(layer, std::move(minimum_cut));

  TechRoutingLef58SpacingEolRule eol;
  eol.flags = TechRoutingLef58SpacingEolRuleFlag::kHasWithCut | TechRoutingLef58SpacingEolRuleFlag::kHasWithCutClass;
  eol.with_cut_class_name = "VIA12";
  const auto eol_id = storage.addLef58SpacingEolRule(layer, std::move(eol));

  EXPECT_EQ(storage.rule(area_id).trim_layer_name, "M2");
  ASSERT_EQ(storage.lef58AreaExceptMinSizes(area_id).size(), 2u);
  EXPECT_EQ(storage.lef58AreaExceptMinSizes(area_id)[1].min_length, 60);
  ASSERT_EQ(storage.lef58MinimumCutClasses(minimum_cut_id).size(), 2u);
  EXPECT_EQ(storage.lef58MinimumCutClasses(minimum_cut_id)[1].cutclass_name, "VIA23");
  EXPECT_TRUE((storage.rule(minimum_cut_id).flags & TechRoutingLef58MinimumCutRuleFlag::kHasCutClasses) != 0u);
  EXPECT_EQ(storage.rule(eol_id).with_cut_class_name, "VIA12");
}

TEST_F(RoutingLayerStorageTest, RejectsInvalidDenseTableShapesBeforeCreatingRule)
{
  TechRoutingPrlSpacingTableRule table;
  table.widths = {100, 200};
  table.parallel_run_lengths = {0, 100};
  table.cells = {10, 20, 30};

  EXPECT_THROW((void) storage.addPrlSpacingTableRule(layer, std::move(table)), std::invalid_argument);
  EXPECT_EQ(storage.ruleCount(layer), 0u);
}

TEST_F(RoutingLayerStorageTest, RejectsNonIncreasingCurrentDensityAxes)
{
  TechRoutingCurrentDensityRule density;
  density.frequencies = {10.0, 10.0};
  density.widths = {100};
  density.table_entries = {1.0, 2.0};

  EXPECT_THROW((void) storage.addCurrentDensityRule(layer, std::move(density)), std::invalid_argument);
  EXPECT_EQ(storage.ruleCount(layer), 0u);
}

}  // namespace
}  // namespace eccdb
