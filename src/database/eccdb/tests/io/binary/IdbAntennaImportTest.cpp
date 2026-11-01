// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <gtest/gtest.h>

#include "IdbLayout.h"
#include "antenna/AntennaService.h"
#include "idb/IdbLibraryImporter.h"
#include "idb/IdbTechImporter.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

TEST(IdbAntennaImportTest, MapsTermAndLayerAntennaThroughService)
{
  ::idb::IdbLayout source;
  auto* source_layer = dynamic_cast<::idb::IdbLayerRouting*>(source.get_layers()->set_layer("M1", "ROUTING"));
  ASSERT_NE(source_layer, nullptr);
  source_layer->set_antenna_area_ratio(10.0);
  source_layer->set_antenna_diff_area_ratio(2.0);
  source_layer->set_antenna_diff_area_ratio_pwl({{0.0, 1.0}, {2.0, 3.0}});
  source_layer->set_antenna_cum_diff_area_ratio_pwl({{4.0, 5.0}, {6.0, 7.0}});
  source_layer->set_antenna_area_factor(1.2);
  source_layer->set_antenna_area_factor_diffuse_only(true);

  // MASTERSLICE layers expose the same legacy iDB antenna property facade;
  // verify that the importer does not silently drop it.  The explicit zero
  // also exercises the has-value bit rather than treating zero as absent.
  auto* source_masterslice = dynamic_cast<::idb::IdbLayerMasterslice*>(source.get_layers()->set_layer("POLY", "MASTERSLICE"));
  ASSERT_NE(source_masterslice, nullptr);
  source_masterslice->set_antenna_cum_area_ratio(0.0);

  auto* source_master = source.get_cell_master_list()->set_cell_master("CELL");
  ASSERT_NE(source_master, nullptr);
  auto* source_term = source_master->add_term("A");
  ASSERT_NE(source_term, nullptr);
  source_term->set_antenna_gate_area(4.0);
  source_term->set_antenna_diff_area(1.5);
  source_term->add_antenna_partial_metal_area("M1", 8.0);
  source_term->add_antenna_partial_metal_side_area("M1", 9.0);
  source_term->add_antenna_partial_cut_area("V1", 2.0);

  TechStore technology;
  IdbTechImporter tech_importer(technology);
  tech_importer.import(source);
  LibraryStore library(technology.techRegistry());
  IdbLibraryImporter library_importer(library, tech_importer);
  library_importer.import(source);

  AntennaService tech_service(technology);
  const auto layer_id = technology.findLayer("M1");
  const auto* layer_antenna = tech_service.layerAntenna(layer_id);
  ASSERT_NE(layer_antenna, nullptr);
  ASSERT_EQ(layer_antenna->models.size(), 1u);
  const auto& model = layer_antenna->models.front();
  ASSERT_EQ(model.values.size(), 3u);
  EXPECT_DOUBLE_EQ(model.values[0].value, 10.0);
  EXPECT_DOUBLE_EQ(model.values[1].value, 1.2);
  EXPECT_TRUE(model.values[1].flags & AntennaValueFlag::kDiffuseOnly);
  EXPECT_DOUBLE_EQ(model.values[2].value, 2.0);
  ASSERT_EQ(model.pwls.size(), 2u);
  EXPECT_EQ(model.pwls[0].metric, AntennaMetric::kDiffAreaRatioPwl);
  EXPECT_EQ(model.pwls.front().points.size(), 2u);
  EXPECT_EQ(model.pwls[1].metric, AntennaMetric::kCumDiffAreaRatioPwl);
  EXPECT_DOUBLE_EQ(model.pwls[1].points[1].y, 7.0);

  const auto masterslice_id = technology.findLayer("POLY");
  const auto* masterslice_antenna = tech_service.layerAntenna(masterslice_id);
  ASSERT_NE(masterslice_antenna, nullptr);
  ASSERT_EQ(masterslice_antenna->models.size(), 1u);
  ASSERT_EQ(masterslice_antenna->models.front().values.size(), 1u);
  EXPECT_EQ(masterslice_antenna->models.front().values.front().metric, AntennaMetric::kCumAreaRatio);
  EXPECT_DOUBLE_EQ(masterslice_antenna->models.front().values.front().value, 0.0);

  AntennaService library_service(library);
  const auto master_id = library.cellMasterStorage().findCellMaster("CELL");
  ASSERT_TRUE(master_id);
  const auto term_id = library.masterTermStorage().findMasterTerm(master_id, "A");
  ASSERT_TRUE(term_id);
  const auto* term_antenna = library_service.masterTermAntenna(term_id);
  ASSERT_NE(term_antenna, nullptr);
  ASSERT_EQ(term_antenna->values.size(), 2u);
  ASSERT_EQ(term_antenna->layer_values.size(), 3u);
  EXPECT_DOUBLE_EQ(term_antenna->values[0].value, 4.0);
  EXPECT_EQ(term_antenna->layer_values[0].layer, "M1");
  EXPECT_EQ(term_antenna->layer_values[2].layer, "V1");
}

}  // namespace
}  // namespace eccdb
