// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <utility>

#include "antenna/AntennaService.h"
#include "binary/BinaryDatabaseExporter.h"
#include "binary/BinaryDatabaseImporter.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/common/TechLayerAntennaComponents.h"

namespace eccdb {
namespace {

class TemporaryPath
{
 public:
  explicit TemporaryPath(const char* suffix)
  {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    _path = std::filesystem::temp_directory_path() / ("eccdb-antenna-" + std::to_string(nonce) + suffix);
  }
  ~TemporaryPath()
  {
    std::error_code error;
    std::filesystem::remove(_path, error);
  }
  const std::filesystem::path& path() const noexcept { return _path; }

 private:
  std::filesystem::path _path;
};

TEST(AntennaBinaryArchiveTest, PersistsTechAndLibraryAntennaComponents)
{
  TechStore source_technology;
  const auto m1 = source_technology.createRoutingLayer(TechLayerInfo{.name = "M1"}, TechRoutingLayer{});
  const auto m2 = source_technology.createRoutingLayer(TechLayerInfo{.name = "M2"}, TechRoutingLayer{});
  LibraryStore source_library(source_technology.techRegistry());
  const auto master = source_library.cellMasterStorage().createCellMaster(LibraryCellMaster{.name = "CELL"});
  const auto term = source_library.masterTermStorage().createMasterTerm(master, LibraryMasterTerm{.name = "A"});

  AntennaService tech_antenna(source_technology);
  TechLayerAntenna layer_value;
  AntennaModel layer_model{.flags = AntennaModelFlag::kHasValues | AntennaModelFlag::kHasPwls, .oxide = "OXIDE1"};
  layer_model.values.push_back(AntennaValue{.metric = AntennaMetric::kAreaRatio, .value = 12.5});
  layer_model.pwls.push_back(AntennaPwl{.metric = AntennaMetric::kDiffAreaRatioPwl,
                                        .points = {{.x = 0.0, .y = 1.0}, {.x = 2.0, .y = 3.0}}});
  layer_value.flags = TechLayerAntennaFlag::kHasModels;
  layer_value.models.push_back(layer_model);
  AntennaModel second_model{.flags = AntennaModelFlag::kHasValues, .oxide = "OXIDE2"};
  // A present zero is different from an absent metric and must survive the
  // archive round-trip.
  second_model.values.push_back(AntennaValue{.metric = AntennaMetric::kCumAreaRatio, .value = 0.0});
  layer_value.models.push_back(second_model);
  tech_antenna.setLayerAntenna(TechLayerId{m1.entity()}, layer_value);

  AntennaService library_antenna(source_library);
  LibraryMasterTermAntenna term_value;
  term_value.flags = LibraryMasterTermAntennaFlag::kHasValues | LibraryMasterTermAntennaFlag::kHasLayerValues;
  term_value.values.push_back(AntennaValue{.metric = AntennaMetric::kGateArea, .value = 4.0});
  term_value.values.push_back(AntennaValue{.metric = AntennaMetric::kDiffArea, .value = 0.0});
  term_value.layer_values.push_back(
      AntennaLayerValue{.metric = AntennaMetric::kPartialMetalArea, .layer = "M1", .value = 7.5});
  term_value.flags |= LibraryMasterTermAntennaFlag::kHasModels;
  AntennaModel term_model{.flags = AntennaModelFlag::kHasValues, .oxide = "OXIDE1"};
  term_model.values.push_back(AntennaValue{.metric = AntennaMetric::kAreaRatio, .value = 8.5});
  term_value.models.push_back(std::move(term_model));
  library_antenna.setMasterTermAntenna(term, term_value);

  TemporaryPath tech_path("-tech.edb");
  TemporaryPath library_path("-library.edb");
  BinaryDatabaseExporter::saveTech(tech_path.path(), source_technology);
  BinaryDatabaseExporter::saveLibrary(library_path.path(), source_library);

  auto technology = BinaryDatabaseImporter::loadTech(tech_path.path());
  auto library = BinaryDatabaseImporter::loadLibrary(library_path.path(), *technology);
  AntennaService loaded_tech(*technology);
  AntennaService loaded_library(*library);

  const auto* loaded_layer = loaded_tech.layerAntenna(TechLayerId{m1.entity()});
  ASSERT_NE(loaded_layer, nullptr);
  ASSERT_EQ(loaded_layer->models.size(), 2u);
  ASSERT_EQ(loaded_layer->models.front().oxide, "OXIDE1");
  ASSERT_EQ(loaded_layer->models.front().pwls.front().points.size(), 2u);
  EXPECT_DOUBLE_EQ(loaded_layer->models.front().values.front().value, 12.5);
  ASSERT_EQ(loaded_layer->models.back().oxide, "OXIDE2");
  ASSERT_EQ(loaded_layer->models.back().values.size(), 1u);
  EXPECT_DOUBLE_EQ(loaded_layer->models.back().values.front().value, 0.0);
  EXPECT_EQ(loaded_tech.layerAntenna(TechLayerId{m2.entity()}), nullptr);

  const auto* loaded_term = loaded_library.masterTermAntenna(term);
  ASSERT_NE(loaded_term, nullptr);
  ASSERT_EQ(loaded_term->values.size(), 2u);
  ASSERT_EQ(loaded_term->layer_values.size(), 1u);
  EXPECT_DOUBLE_EQ(loaded_term->values.front().value, 4.0);
  EXPECT_DOUBLE_EQ(loaded_term->values.back().value, 0.0);
  EXPECT_EQ(loaded_term->layer_values.front().layer, "M1");
  ASSERT_EQ(loaded_term->models.size(), 1u);
  EXPECT_EQ(loaded_term->models.front().oxide, "OXIDE1");
  ASSERT_EQ(loaded_term->models.front().values.size(), 1u);
  EXPECT_EQ(loaded_term->models.front().values.front().metric, AntennaMetric::kAreaRatio);
  EXPECT_DOUBLE_EQ(loaded_term->models.front().values.front().value, 8.5);
}

}  // namespace
}  // namespace eccdb
