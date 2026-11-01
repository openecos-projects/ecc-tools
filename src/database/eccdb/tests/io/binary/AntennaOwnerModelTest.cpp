// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <utility>

#include "antenna/AntennaService.h"
#include "library/LibraryStore.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "tech/TechStore.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace eccdb {
namespace {

AntennaModel makeModel(std::string oxide, double value)
{
  AntennaModel model;
  model.oxide = std::move(oxide);
  model.flags = AntennaModelFlag::kHasValues;
  model.values.push_back(AntennaValue{.metric = AntennaMetric::kGateArea, .value = value});
  return model;
}

const AntennaValue* findValue(const AntennaModel& model, AntennaMetric metric)
{
  const auto found = std::find_if(model.values.begin(), model.values.end(), [metric](const AntennaValue& value) {
    return value.metric == metric;
  });
  return found == model.values.end() ? nullptr : &*found;
}

TEST(AntennaOwnerModelTest, ReplacesOneMasterTermOxideSlotAndKeepsOtherOxides)
{
  TechStore technology;
  LibraryStore library(technology.techRegistry());
  const auto master = library.cellMasterStorage().createCellMaster(LibraryCellMaster{.name = "CELL"});
  const auto term = library.masterTermStorage().createMasterTerm(master, LibraryMasterTerm{.name = "A"});
  AntennaService service(library);

  service.setMasterTermAntennaModel(term, makeModel("", 1.0));
  ASSERT_NE(service.masterTermAntennaModel(term, "OXIDE1"), nullptr);
  EXPECT_DOUBLE_EQ(service.masterTermAntennaModel(term, "OXIDE1")->values.front().value, 1.0);

  // The explicit and implicit default spellings address the same OpenDB-style slot.
  service.setMasterTermAntennaModel(term, makeModel("OXIDE1", 2.0));
  const auto* antenna = service.masterTermAntenna(term);
  ASSERT_NE(antenna, nullptr);
  ASSERT_EQ(antenna->models.size(), 1u);
  EXPECT_DOUBLE_EQ(service.masterTermAntennaModel(term, "")->values.front().value, 2.0);

  service.setMasterTermAntennaModel(term, makeModel("OXIDE2", 3.0));
  ASSERT_EQ(service.masterTermAntenna(term)->models.size(), 2u);
  EXPECT_DOUBLE_EQ(service.masterTermAntennaModel(term, "OXIDE2")->values.front().value, 3.0);

  EXPECT_TRUE(service.clearMasterTermAntennaModel(term, ""));
  EXPECT_EQ(service.masterTermAntennaModel(term, "OXIDE1"), nullptr);
  ASSERT_NE(service.masterTermAntennaModel(term, "OXIDE2"), nullptr);
  EXPECT_FALSE(service.clearMasterTermAntennaModel(term, "OXIDE1"));
}

TEST(AntennaOwnerModelTest, NormalizesDuplicateModelsWhenReplacingWholeOwnerComponent)
{
  TechStore technology;
  LibraryStore library(technology.techRegistry());
  const auto master = library.cellMasterStorage().createCellMaster(LibraryCellMaster{.name = "CELL"});
  const auto term = library.masterTermStorage().createMasterTerm(master, LibraryMasterTerm{.name = "A"});

  auto first = makeModel("OXIDE1", 1.0);
  first.values.push_back(AntennaValue{.metric = AntennaMetric::kDiffArea, .value = 4.0});
  auto update = makeModel("", 9.0);
  LibraryMasterTermAntenna antenna;
  antenna.models = {std::move(first), std::move(update), makeModel("OXIDE2", 2.0)};
  AntennaService service(library);
  service.setMasterTermAntenna(term, std::move(antenna));

  const auto* stored = service.masterTermAntenna(term);
  ASSERT_NE(stored, nullptr);
  ASSERT_EQ(stored->models.size(), 2u);
  EXPECT_DOUBLE_EQ(service.masterTermAntennaModel(term, "OXIDE1")->values.front().value, 9.0);
  ASSERT_EQ(service.masterTermAntennaModel(term, "OXIDE1")->values.size(), 2u);
  EXPECT_DOUBLE_EQ(service.masterTermAntennaModel(term, "OXIDE1")->values[1].value, 4.0);
  EXPECT_DOUBLE_EQ(service.masterTermAntennaModel(term, "OXIDE2")->values.front().value, 2.0);
}

TEST(AntennaOwnerModelTest, MergesDifferentMetricsAndUsesLastDuplicateMetricValue)
{
  TechStore technology;
  LibraryStore library(technology.techRegistry());
  const auto master = library.cellMasterStorage().createCellMaster(LibraryCellMaster{.name = "CELL"});
  const auto term = library.masterTermStorage().createMasterTerm(master, LibraryMasterTerm{.name = "A"});

  // A legacy importer can emit the default model as an empty oxide string,
  // followed by an explicit OXIDE1 fragment.  The fragments describe one
  // logical owner slot and must be merged rather than replacing the entire
  // model: unrelated metrics must survive, while a repeated metric is
  // last-value-wins.
  AntennaModel legacy = makeModel("OXIDE1", 1.0);
  legacy.values.front().metric = AntennaMetric::kAreaRatio;
  AntennaModel duplicate = makeModel("", 9.0);
  duplicate.values.front().metric = AntennaMetric::kAreaRatio;
  duplicate.values.push_back(AntennaValue{.metric = AntennaMetric::kCumAreaRatio, .value = 3.0});

  LibraryMasterTermAntenna antenna;
  antenna.models = {std::move(legacy), std::move(duplicate)};
  AntennaService service(library);
  service.setMasterTermAntenna(term, std::move(antenna));

  const auto* stored = service.masterTermAntennaModel(term, "OXIDE1");
  ASSERT_NE(stored, nullptr);
  ASSERT_EQ(service.masterTermAntenna(term)->models.size(), 1u);

  const auto* area_ratio = findValue(*stored, AntennaMetric::kAreaRatio);
  const auto* cum_area_ratio = findValue(*stored, AntennaMetric::kCumAreaRatio);
  ASSERT_NE(area_ratio, nullptr);
  ASSERT_NE(cum_area_ratio, nullptr);
  EXPECT_DOUBLE_EQ(area_ratio->value, 9.0);
  EXPECT_DOUBLE_EQ(cum_area_ratio->value, 3.0);
}

TEST(AntennaOwnerModelTest, ReplacesOneTechLayerOxideSlot)
{
  TechStore technology;
  const auto layer = technology.createRoutingLayer(TechLayerInfo{.name = "M1"}, TechRoutingLayer{});
  AntennaService service(technology);

  service.setLayerAntennaModel(TechLayerId{layer.entity()}, makeModel("OXIDE1", 10.0));
  service.setLayerAntennaModel(TechLayerId{layer.entity()}, makeModel("OXIDE1", 20.0));
  service.setLayerAntennaModel(TechLayerId{layer.entity()}, makeModel("OXIDE2", 30.0));

  const auto* antenna = service.layerAntenna(TechLayerId{layer.entity()});
  ASSERT_NE(antenna, nullptr);
  ASSERT_EQ(antenna->models.size(), 2u);
  EXPECT_DOUBLE_EQ(service.layerAntennaModel(TechLayerId{layer.entity()}, "OXIDE1")->values.front().value, 20.0);
  EXPECT_DOUBLE_EQ(service.layerAntennaModel(TechLayerId{layer.entity()}, "OXIDE2")->values.front().value, 30.0);
  EXPECT_TRUE(service.clearLayerAntennaModel(TechLayerId{layer.entity()}, "OXIDE2"));
  EXPECT_EQ(service.layerAntennaModel(TechLayerId{layer.entity()}, "OXIDE2"), nullptr);
}

}  // namespace
}  // namespace eccdb
