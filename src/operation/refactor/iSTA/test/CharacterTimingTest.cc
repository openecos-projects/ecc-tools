// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "api/TimingEngine.hh"
#include "CharacterTimingTestCommon.hh"
#include "gtest/gtest.h"
#include "log/Log.hh"
#include "idm.h"
#include "sta/Sta.hh"
#include "sta/StaVertex.hh"
#include "usage/usage.hh"

#include <filesystem>

using namespace ista;
using namespace ieda;

namespace {

class CharacterTimingTest : public testing::Test {
 protected:
  void SetUp() final {
    char config[] = "test";
    char* argv[] = {config};
    Log::init(argv);
  }
  void TearDown() final {
    ieval::TimingAPI::destroyInst();
    TimingEngine::destroyTimingEngine();
    Sta::destroySta();
    dmInst->reset();
    Log::end();
  }
};

TimingEngine* buildGoldenCaseTimingWithSdc(const std::filesystem::path& sdc_path,
                                           const std::filesystem::path& output_root) {
  std::error_code ec;
  std::filesystem::create_directories(output_root, ec);
  EXPECT_FALSE(ec) << "failed to create timing test output directory: "
                   << output_root << ", error=" << ec.message();

  const auto db_config_path = ista::test::writeGoldenCaseDbConfig(output_root);
  EXPECT_TRUE(std::filesystem::exists(db_config_path))
      << "generated integration db config missing: " << db_config_path;
  if (!std::filesystem::exists(db_config_path)) {
    return nullptr;
  }

  ieval::TimingAPI::destroyInst();
  TimingEngine::destroyTimingEngine();
  Sta::destroySta();
  dmInst->reset();
  auto* timing_api = ieval::TimingAPI::getInst();

  EXPECT_TRUE(dmInst->init(db_config_path.string()))
      << "failed to initialize dmInst with " << db_config_path;
  auto& data_config = dmInst->get_config();
  data_config.set_output_path(output_root.string());
  data_config.set_sdc_path(sdc_path.string());

  const auto liberty_files_raw = ista::test::asap7GoldenLibertyFiles();
  std::vector<std::string> liberty_files;
  liberty_files.reserve(liberty_files_raw.size());
  for (const auto* liberty_file : liberty_files_raw) {
    liberty_files.emplace_back(liberty_file);
  }
  data_config.set_lib_paths(liberty_files);

  timing_api->evalTiming(ista::test::goldenCaseRoutingType());

  auto* timing_engine = TimingEngine::getOrCreateTimingEngine();
  timing_engine->set_num_threads(8);
  timing_engine->set_design_work_space(output_root.string().c_str());

  return timing_engine;
}

TEST_F(CharacterTimingTest, example1) {
  Stats stats;
  const auto output_lib =
      ista::test::generateGoldenCaseTimingModel(AnalysisMode::kMax,
                                                ista::test::goldenCaseMaxLibPath());

  double memory_delta = stats.memoryDelta();
  LOG_INFO << "extract timing lib memory usage " << memory_delta << "MB";
  double time_delta = stats.elapsedRunTime();
  LOG_INFO << "extract timing lib time elapsed " << time_delta << "s";

  ASSERT_TRUE(std::filesystem::exists(output_lib))
      << "timing model was not generated at " << output_lib;
}

TEST_F(CharacterTimingTest,
       ideal_clock_transition_from_original_sdc_reaches_internal_clock_pin) {
  const auto output_root =
      ista::test::goldenCaseIntegrationOutputDir() / "ideal_clock_transition";
  const auto sdc_path = ista::test::writeGoldenCaseOriginalSdcForIsta(output_root);
  auto* timing_engine = buildGoldenCaseTimingWithSdc(sdc_path, output_root);
  ASSERT_NE(timing_engine, nullptr);

  auto* target_vertex = timing_engine->findVertex(
      "u_NV_NVDLA_cmac_u_reg_cmac_a2csb_resp_valid_reg:CLK");
  ASSERT_NE(target_vertex, nullptr);

  auto* rise_slew =
      target_vertex->getWorstSlewData(AnalysisMode::kMax, TransType::kRise);
  ASSERT_NE(rise_slew, nullptr);

  EXPECT_GT(FS_TO_NS(rise_slew->get_slew()), 0.0)
      << "ideal-clock set_clock_transition from the original SDC should reach "
         "the internal sequential CLK pin";
}

}  // namespace
