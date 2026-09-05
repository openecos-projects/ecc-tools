// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "tech/cut_layer/storage/CutLayerStorage.h"

namespace eccdb {
namespace {

static_assert(std::is_same_v<decltype(TechCutLef58CutClassRule::name), std::string>);
static_assert(std::is_same_v<decltype(TechCutLef58SpacingTableRule::cutclass1_names), std::vector<std::string>>);

class CutLayerStorageTest : public testing::Test
{
 protected:
  void SetUp() override
  {
    TechLayerInfo base{.name = "V1", .mask_count = 1};

    TechCutLayer cut;
    cut.flags = TechCutLayerFlag::kHasWidth | TechCutLayerFlag::kHasResistance;
    cut.width = 90;
    cut.resistance_per_cut = 1.5;

    layer = storage.createLayer(std::move(base), std::move(cut));
  }

  TechRegistry registry;
  TechCutLayerStorage storage{registry};
  TechCutLayerId layer;
};

TEST_F(CutLayerStorageTest, CreatesLayerInSharedRegistry)
{
  EXPECT_TRUE(storage.contains(layer));
  EXPECT_EQ(storage.layerInfo(layer).name, "V1");
  EXPECT_EQ(storage.layerInfo(layer).mask_count, 1u);
  EXPECT_EQ(storage.cutLayer(layer).width, 90);
  EXPECT_TRUE((storage.cutLayer(layer).flags & TechCutLayerFlag::kHasResistance) != 0u);
  EXPECT_TRUE((storage.registry().all_of<TechLayerInfo, TechCutLayer>(layer.entity())));
}

TEST_F(CutLayerStorageTest, MaintainsTypedSpacingRuleReferences)
{
  const auto first = storage.addSpacingRule(layer, TechCutSpacingRule{.spacing = 80});
  const auto second = storage.addSpacingRule(
      layer, TechCutSpacingRule{
                 .flags = TechCutSpacingRuleFlag::kHasAdjacentCuts, .spacing = 100, .adjacent_cut_count = 2, .adjacent_cut_within = 200});

  const auto rules = storage.spacingRules(layer);
  ASSERT_EQ(rules.size(), 2u);
  EXPECT_EQ(rules[0], first);
  EXPECT_EQ(rules[1], second);
  EXPECT_EQ(storage.spacingRuleOwner(first), layer);
  EXPECT_EQ(storage.spacingRuleOwner(second), layer);
  EXPECT_TRUE((storage.registry().all_of<TechRuleOwner, TechCutSpacingRule>(first.entity())));
  EXPECT_TRUE((storage.registry().all_of<TechRuleRefs<TechCutSpacingRuleId>>(layer.entity())));

  EXPECT_TRUE(storage.destroySpacingRule(first));
  const auto remaining = storage.spacingRules(layer);
  ASSERT_EQ(remaining.size(), 1u);
  EXPECT_EQ(remaining.front(), second);
}

TEST_F(CutLayerStorageTest, PreservesRepeatableEnclosuresAndOrthogonalTableOrder)
{
  const auto unqualified = storage.addEnclosureRule(
      layer, TechCutEnclosureRule{.flags = TechCutEnclosureRuleFlag::kHasMinLength,
                                  .side = CutLayerSide::kUnknown,
                                  .overhang1 = 10,
                                  .overhang2 = 20,
                                  .min_length = 70});
  const auto first_below = storage.addEnclosureRule(
      layer, TechCutEnclosureRule{.side = CutLayerSide::kBelow, .overhang1 = 30, .overhang2 = 40});
  const auto second_below = storage.addEnclosureRule(
      layer, TechCutEnclosureRule{.flags = TechCutEnclosureRuleFlag::kHasMinWidth,
                                  .side = CutLayerSide::kBelow,
                                  .overhang1 = 50,
                                  .overhang2 = 60,
                                  .min_width = 100});

  const auto enclosures = storage.enclosureRules(layer);
  ASSERT_EQ(enclosures.size(), 3u);
  EXPECT_EQ(enclosures[0], unqualified);
  EXPECT_EQ(enclosures[1], first_below);
  EXPECT_EQ(enclosures[2], second_below);
  EXPECT_EQ(storage.enclosureRule(layer, CutLayerSide::kBelow), first_below);

  TechCutOrthogonalSpacingTableRule table;
  table.items = {{.within = 300, .spacing = 200}, {.within = 100, .spacing = 150}};
  const auto table_id = storage.addOrthogonalSpacingTableRule(layer, std::move(table));
  const auto& stored = storage.orthogonalSpacingTableRule(table_id);
  ASSERT_EQ(stored.items.size(), 2u);
  EXPECT_EQ(stored.items[0].within, 300);
  EXPECT_EQ(stored.items[1].within, 100);
  EXPECT_EQ(storage.orthogonalSpacingTableRuleOwner(table_id), layer);
}

TEST_F(CutLayerStorageTest, ReplacesArraySpacingWithoutChangingRuleId)
{
  TechCutArraySpacingRule initial;
  initial.cut_spacing = 100;
  initial.items = {{.array_cut_count = 2, .spacing = 120}, {.array_cut_count = 4, .spacing = 150}};

  const auto id = storage.setArraySpacingRule(layer, std::move(initial));
  ASSERT_EQ(storage.arraySpacingRule(id).items.size(), 2u);
  EXPECT_EQ(storage.arraySpacingRule(id).items[1].spacing, 150);

  TechCutArraySpacingRule replacement;
  replacement.flags = TechCutArraySpacingRuleFlag::kLongArray;
  replacement.cut_spacing = 140;
  replacement.items = {{.array_cut_count = 8, .spacing = 210}};

  const auto replacement_id = storage.setArraySpacingRule(layer, std::move(replacement));
  EXPECT_EQ(replacement_id, id);
  EXPECT_EQ(storage.arraySpacingRule(id).cut_spacing, 140);
  ASSERT_EQ(storage.arraySpacingRule(id).items.size(), 1u);
  EXPECT_EQ(storage.arraySpacingRule(id).items.front().array_cut_count, 8u);
}

TEST_F(CutLayerStorageTest, StoresRuleNamesAndChildrenAsOwnedStrings)
{
  const auto cutclass = storage.addLef58CutClassRule(layer, TechCutLef58CutClassRule{.name = "CUT_A", .via_width = 80, .via_length = 120});

  TechCutLef58EolSpacingRule eol;
  eol.cutclass_name = "CUT_A";
  eol.cut_spacing1 = 90;
  eol.cut_spacing2 = 100;
  eol.to_classes = {{.cutclass_name = "CUT_B", .cut_spacing1 = 120, .cut_spacing2 = 130}};
  const auto eol_id = storage.setLef58EolSpacingRule(layer, std::move(eol));

  EXPECT_EQ(storage.lef58CutClassRule(cutclass).name, "CUT_A");
  ASSERT_EQ(storage.lef58EolSpacingRule(eol_id).to_classes.size(), 1u);
  EXPECT_EQ(storage.lef58EolSpacingRule(eol_id).to_classes.front().cutclass_name, "CUT_B");
  EXPECT_EQ(storage.lef58EolSpacingRule(eol_id).to_classes.front().cut_spacing2, 130);
}

TEST_F(CutLayerStorageTest, ReadsSpacingTableByClassAxes)
{
  TechCutLef58SpacingTableRule table;
  table.flags = TechCutLef58SpacingTableRuleFlag::kHasPrl;
  table.prl = 200;
  table.cutclass1_names = {"A", "B"};
  table.cutclass2_names = {"C", "D"};
  table.cells = {{.cut_spacing1 = 100, .has_cut_spacing1 = true},
                 {.cut_spacing1 = 110, .has_cut_spacing1 = true},
                 {.cut_spacing1 = 200, .has_cut_spacing1 = true},
                 {.cut_spacing1 = 210, .has_cut_spacing1 = true}};

  const auto id = storage.addLef58SpacingTableRule(layer, std::move(table));
  const auto& stored = storage.lef58SpacingTableRule(id);

  EXPECT_EQ(stored.cutclass1_names, (std::vector<std::string>{"A", "B"}));
  EXPECT_EQ(stored.cutclass2_names, (std::vector<std::string>{"C", "D"}));
  EXPECT_EQ(storage.lef58SpacingTableCell(id, 0, 0).cut_spacing1, 100);
  EXPECT_EQ(storage.lef58SpacingTableCell(id, 1, 0).cut_spacing1, 110);
  EXPECT_EQ(storage.lef58SpacingTableCell(id, 0, 1).cut_spacing1, 200);
  EXPECT_EQ(storage.lef58SpacingTableCell(id, 1, 1).cut_spacing1, 210);
}

TEST_F(CutLayerStorageTest, RejectsSpacingTableWithWrongShape)
{
  TechCutLef58SpacingTableRule table;
  table.cutclass1_names = {"A", "B"};
  table.cutclass2_names = {"C"};
  table.cells = {{.cut_spacing1 = 100, .has_cut_spacing1 = true}};

  EXPECT_THROW((void) storage.addLef58SpacingTableRule(layer, std::move(table)), std::invalid_argument);
  EXPECT_EQ(storage.ruleCount(layer), 0u);
}

TEST_F(CutLayerStorageTest, InterpolatesCurrentDensityTable)
{
  TechCutCurrentDensityRule density;
  density.signal = TechCutCurrentDensitySignal::kAc;
  density.type = TechCutCurrentDensityType::kPeak;
  density.frequencies = {10.0, 20.0};
  density.cut_areas = {100, 200};
  density.table_entries = {1.0, 2.0, 3.0, 4.0};

  const auto id = storage.addCurrentDensityRule(layer, std::move(density));

  EXPECT_DOUBLE_EQ(storage.currentDensityTableEntry(id, 1, 1), 4.0);
  EXPECT_DOUBLE_EQ(storage.currentDensityAt(id, 10.0, 100), 1.0);
  EXPECT_DOUBLE_EQ(storage.currentDensityAt(id, 15.0, 150), 2.5);
}

TEST_F(CutLayerStorageTest, ReturnsScalarCurrentDensity)
{
  TechCutCurrentDensityRule density;
  density.signal = TechCutCurrentDensitySignal::kDc;
  density.type = TechCutCurrentDensityType::kAverage;
  density.flags = TechCutCurrentDensityRuleFlag::kHasScalar;
  density.scalar = 0.36;

  const auto id = storage.addCurrentDensityRule(layer, std::move(density));

  EXPECT_DOUBLE_EQ(storage.currentDensityAt(id, 0.0, 0), 0.36);
}

TEST_F(CutLayerStorageTest, RejectsInvalidCurrentDensityAxes)
{
  TechCutCurrentDensityRule density;
  density.frequencies = {10.0, 10.0};
  density.cut_areas = {100};
  density.table_entries = {1.0, 2.0};

  EXPECT_THROW((void) storage.addCurrentDensityRule(layer, std::move(density)), std::invalid_argument);
  EXPECT_EQ(storage.ruleCount(layer), 0u);
}

}  // namespace
}  // namespace eccdb
