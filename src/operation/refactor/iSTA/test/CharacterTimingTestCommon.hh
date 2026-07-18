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
#pragma once

#include "api/TimingEngine.hh"
#include "api/TimingIDBAdapter.hh"
#include "idm.h"
#include "sta/Sta.hh"
#include "timing_api.hh"
#include "gtest/gtest.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace ista::test {

namespace fs = std::filesystem;

inline const fs::path& repoRoot() {
  static const fs::path kRepoRoot = "/home/zhaoxueyan/code/write-lib_back";
  return kRepoRoot;
}

inline const fs::path& currentWorktreeRoot() {
  static const fs::path kWorktreeRoot =
      fs::path(__FILE__).parent_path()
          .parent_path()
          .parent_path()
          .parent_path()
          .parent_path();
  return kWorktreeRoot;
}

inline const fs::path& goldenCaseDir() {
  static const fs::path kCaseDir =
      repoRoot() / "benchmark/iccad24-benchmark/design/NV_NVDLA_partition_m";
  return kCaseDir;
}

inline const fs::path& asap7Dir() {
  static const fs::path kAsap7Dir = repoRoot() / "benchmark/iccad24-benchmark/ASAP7";
  return kAsap7Dir;
}

inline fs::path defaultOutputRoot() {
  if (const char* output_root = std::getenv("IEDA_CHARACTER_TIMING_OUTPUT_ROOT");
      output_root && *output_root) {
    return output_root;
  }

  return currentWorktreeRoot() / "artifacts/ieda/character_timing";
}

inline bool reuseExistingGeneratedLib() {
  if (const char* reuse_output =
          std::getenv("IEDA_CHARACTER_TIMING_REUSE_OUTPUT");
      reuse_output && *reuse_output) {
    return std::string_view(reuse_output) == "1" ||
           std::string_view(reuse_output) == "true" ||
           std::string_view(reuse_output) == "TRUE";
  }

  // Default to regenerating. Alignment work depends on fresh artifacts.
  return false;
}

inline fs::path goldenCaseOutputDir() {
  return defaultOutputRoot() / "NV_NVDLA_partition_m";
}

inline const fs::path& ics55GcdServiceRoot() {
  static const fs::path kServiceRoot =
      "/nfs/share/home/huangzengrong/benchmark/test/ics55_gcd_service";
  return kServiceRoot;
}

inline const fs::path& ics55GcdHardenRoot() {
  static const fs::path kHardenRoot =
      "/nfs/share/home/huangzengrong/benchmark/test/gcd_harden";
  return kHardenRoot;
}

inline const fs::path& ics55PdkRoot() {
  static const fs::path kPdkRoot = [] {
    if (const char* pdk_root =
            std::getenv("IEDA_CHARACTER_TIMING_ICS55_PDK_ROOT");
        pdk_root && *pdk_root) {
      return fs::path(pdk_root);
    }

    for (const fs::path& candidate : {
             fs::path("/nfs/home/huangzengrong/ecc_project/ecc/chipcompiler/"
                      "thirdparty/icsprout55-pdk"),
             fs::path("/nfs/share/home/zhaoxueyan/icsprout55-pdk")}) {
      if (fs::exists(candidate)) {
        return candidate;
      }
    }

    return fs::path();
  }();
  return kPdkRoot;
}

inline fs::path ics55GcdOutputDir() {
  return defaultOutputRoot() / "ics55_gcd";
}

inline fs::path ics55GcdIntegrationOutputDir() {
  return ics55GcdOutputDir() / "integration";
}

inline fs::path ics55GcdRuntimeOutputDir() {
  return ics55GcdOutputDir() / "runtime";
}

inline fs::path ics55GcdDefPath() {
  return ics55GcdHardenRoot() / "gcd_filler.def.gz";
}

inline fs::path ics55GcdSdcPath() {
  return ics55GcdHardenRoot() / "gcd.sdc";
}

inline fs::path ics55GcdVerilogPath() {
  const auto filler_output =
      ics55GcdServiceRoot() / "filler_ecc/output/gcd_filler.v";
  if (fs::exists(filler_output)) {
    return filler_output;
  }

  return ics55GcdServiceRoot() / "origin/gcd.v";
}

inline fs::path ics55GcdTechLefPath() {
  return ics55PdkRoot() / "prtech/techLEF/N551P6M.lef";
}

inline std::vector<std::string> ics55GcdLefFiles() {
  const auto pdk_root = ics55PdkRoot();
  return {
      (pdk_root /
       "IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CR/lef/"
       "ics55_LLSC_H7CR_ecos.lef")
          .string(),
      (pdk_root /
       "IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CL/lef/"
       "ics55_LLSC_H7CL_ecos.lef")
          .string(),
  };
}

inline std::vector<std::string> ics55GcdLibertyFiles() {
  const auto pdk_root = ics55PdkRoot();
  return {
      (pdk_root /
       "IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CR/liberty/"
       "ics55_LLSC_H7CR_ss_rcworst_1p08_125_nldm.lib")
          .string(),
      (pdk_root /
       "IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CL/liberty/"
       "ics55_LLSC_H7CL_ss_rcworst_1p08_125_nldm.lib")
          .string(),
  };
}

inline bool ics55GcdCaseAvailable() {
  if (ics55PdkRoot().empty()) {
    return false;
  }

  std::vector<fs::path> required_paths = {
      ics55GcdServiceRoot(),
      ics55GcdHardenRoot(),
      ics55GcdTechLefPath(),
      ics55GcdDefPath(),
      ics55GcdSdcPath(),
      ics55GcdVerilogPath(),
  };
  for (const auto& lef_file : ics55GcdLefFiles()) {
    required_paths.emplace_back(lef_file);
  }
  for (const auto& liberty_file : ics55GcdLibertyFiles()) {
    required_paths.emplace_back(liberty_file);
  }

  return std::all_of(required_paths.begin(), required_paths.end(),
                     [](const fs::path& candidate) {
                       return !candidate.empty() && fs::exists(candidate);
                     });
}

inline fs::path goldenCaseIntegrationOutputDir() {
  return goldenCaseOutputDir() / "integration";
}

inline fs::path goldenCaseRuntimeOutputDir() {
  return goldenCaseOutputDir() / "runtime";
}

inline fs::path goldenCaseMaxLibPath() {
  return goldenCaseOutputDir() / "NV_NVDLA_partition_m.characterized.max.lib";
}

inline fs::path goldenCaseMaxCheckSourcePath() {
  return goldenCaseMaxLibPath().string() + ".check_sources.tsv";
}

inline fs::path goldenCaseMinLibPath() {
  return goldenCaseOutputDir() / "NV_NVDLA_partition_m.characterized.min.lib";
}

inline fs::path openroadGoldenLibPath() {
  const auto compat_path =
      repoRoot() /
      "artifacts/NV_NVDLA_partition_m/openroad_compat_sdc/NV_NVDLA_partition_m.lib";
  if (fs::exists(compat_path)) {
    return compat_path;
  }

  return repoRoot() / "artifacts/NV_NVDLA_partition_m/openroad/NV_NVDLA_partition_m.lib";
}

inline fs::path goldenCaseVerilogPath() {
  return goldenCaseDir() / "NV_NVDLA_partition_m.v";
}

inline fs::path goldenCaseDefPath() {
  return goldenCaseDir() / "NV_NVDLA_partition_m.def";
}

inline fs::path goldenCaseSdcPath() {
  return goldenCaseDir() /
         "workspace/output/dreamplace/compat/"
         "NV_NVDLA_partition_m_optimizer_compat.sdc";
}

inline fs::path goldenCaseOriginalSdcPath() {
  return goldenCaseDir() / "NV_NVDLA_partition_m.sdc";
}

inline fs::path writeGoldenCaseOriginalSdcForIsta(const fs::path& output_root) {
  std::error_code ec;
  fs::create_directories(output_root, ec);
  EXPECT_FALSE(ec) << "failed to create sdc output directory: " << output_root
                   << ", error=" << ec.message();

  const auto generated_sdc_path =
      output_root / "NV_NVDLA_partition_m.original_ista_compatible.sdc";
  std::ifstream input(goldenCaseOriginalSdcPath());
  EXPECT_TRUE(input.is_open()) << "failed to open original golden SDC: "
                               << goldenCaseOriginalSdcPath();

  std::ofstream output(generated_sdc_path);
  EXPECT_TRUE(output.is_open()) << "failed to create generated SDC: "
                                << generated_sdc_path;

  if (!input.is_open() || !output.is_open()) {
    return generated_sdc_path;
  }

  static const std::regex kUnsupportedCmdPattern(
      R"(^\s*(set_clock_gating_check|set_ideal_network)\b)");
  std::string line;
  while (std::getline(input, line)) {
    if (std::regex_search(line, kUnsupportedCmdPattern)) {
      continue;
    }
    output << line << '\n';
  }

  output.close();
  return generated_sdc_path;
}

inline fs::path goldenCaseDbConfigTemplatePath() {
  return goldenCaseDir() / "workspace/config/iEDA_config/db_default_config.json";
}

inline std::string goldenCaseRoutingType() {
  if (const char* routing_type = std::getenv("IEDA_CHARACTER_TIMING_ROUTING_TYPE");
      routing_type && *routing_type) {
    return routing_type;
  }

  return "FLUTE";
}

inline std::vector<const char*> asap7GoldenLibertyFiles() {
  return {
      "/home/zhaoxueyan/code/write-lib_back/benchmark/iccad24-benchmark/ASAP7/lib/"
      "asap7sc7p5t_AO_RVT_FF_nldm_201020.lib",
      "/home/zhaoxueyan/code/write-lib_back/benchmark/iccad24-benchmark/ASAP7/lib/"
      "asap7sc7p5t_INVBUF_RVT_FF_nldm_201020.lib",
      "/home/zhaoxueyan/code/write-lib_back/benchmark/iccad24-benchmark/ASAP7/lib/"
      "asap7sc7p5t_OA_RVT_FF_nldm_201020.lib",
      "/home/zhaoxueyan/code/write-lib_back/benchmark/iccad24-benchmark/ASAP7/lib/"
      "asap7sc7p5t_SEQ_RVT_FF_nldm_201020.lib",
      "/home/zhaoxueyan/code/write-lib_back/benchmark/iccad24-benchmark/ASAP7/lib/"
      "asap7sc7p5t_SIMPLE_RVT_FF_nldm_201020.lib",
      "/home/zhaoxueyan/code/write-lib_back/benchmark/iccad24-benchmark/ASAP7/lib/"
      "sram_asap7_16x256_1rw.lib",
      "/home/zhaoxueyan/code/write-lib_back/benchmark/iccad24-benchmark/ASAP7/lib/"
      "sram_asap7_32x256_1rw.lib",
      "/home/zhaoxueyan/code/write-lib_back/benchmark/iccad24-benchmark/ASAP7/lib/"
      "sram_asap7_64x256_1rw.lib",
      "/home/zhaoxueyan/code/write-lib_back/benchmark/iccad24-benchmark/ASAP7/lib/"
      "sram_asap7_64x64_1rw.lib"};
}

inline std::vector<std::string> asap7GoldenLefFiles() {
  return {
      (asap7Dir() / "lef/asap7_tech_1x_201209.lef").string(),
      (asap7Dir() / "lef/asap7sc7p5t_27_R_1x_201211.lef").string(),
      (asap7Dir() / "lef/sram_asap7_16x256_1rw.lef").string(),
      (asap7Dir() / "lef/sram_asap7_32x256_1rw.lef").string(),
      (asap7Dir() / "lef/sram_asap7_64x256_1rw.lef").string(),
      (asap7Dir() / "lef/sram_asap7_64x64_1rw.lef").string(),
  };
}

inline fs::path writeGoldenCaseDbConfig(const fs::path& output_root) {
  const auto config_dir = output_root / "config";
  std::error_code ec;
  fs::create_directories(config_dir, ec);
  EXPECT_FALSE(ec) << "failed to create config directory: " << config_dir
                   << ", error=" << ec.message();

  const auto config_path = config_dir / "NV_NVDLA_partition_m.integration.db.json";
  std::ofstream output(config_path);
  EXPECT_TRUE(output.is_open()) << "failed to open generated db config at "
                                << config_path;

  const auto liberty_files = asap7GoldenLibertyFiles();
  const auto lef_files = asap7GoldenLefFiles();
  output << "{\n";
  output << "  \"INPUT\": {\n";
  output << "    \"tech_lef_path\": \"" << lef_files.front() << "\",\n";
  output << "    \"lef_paths\": [\n";
  for (size_t i = 1; i < lef_files.size(); ++i) {
    output << "      \"" << lef_files[i] << "\"";
    output << (i + 1 == lef_files.size() ? "\n" : ",\n");
  }
  output << "    ],\n";
  output << "    \"def_path\": \"" << goldenCaseDefPath().string() << "\",\n";
  output << "    \"verilog_path\": \"" << goldenCaseVerilogPath().string() << "\",\n";
  output << "    \"lib_path\": [\n";
  for (size_t i = 0; i < liberty_files.size(); ++i) {
    output << "      \"" << liberty_files[i] << "\"";
    output << (i + 1 == liberty_files.size() ? "\n" : ",\n");
  }
  output << "    ],\n";
  output << "    \"sdc_path\": \"" << goldenCaseSdcPath().string() << "\"\n";
  output << "  },\n";
  output << "  \"OUTPUT\": {\n";
  output << "    \"output_dir_path\": \"" << output_root.string() << "\"\n";
  output << "  },\n";
  output << "  \"LayerSettings\": {\n";
  output << "    \"routing_layer_1st\": \"MET1\"\n";
  output << "  }\n";
  output << "}\n";
  output.close();

  return config_path;
}

inline fs::path writeIcs55GcdDbConfig(const fs::path& output_root) {
  const auto config_dir = output_root / "config";
  std::error_code ec;
  fs::create_directories(config_dir, ec);
  EXPECT_FALSE(ec) << "failed to create config directory: " << config_dir
                   << ", error=" << ec.message();

  const auto config_path = config_dir / "ics55_gcd.integration.db.json";
  std::ofstream output(config_path);
  EXPECT_TRUE(output.is_open()) << "failed to open generated db config at "
                                << config_path;

  const auto lef_files = ics55GcdLefFiles();
  const auto liberty_files = ics55GcdLibertyFiles();
  output << "{\n";
  output << "  \"INPUT\": {\n";
  output << "    \"tech_lef_path\": \"" << ics55GcdTechLefPath().string()
         << "\",\n";
  output << "    \"lef_paths\": [\n";
  for (size_t i = 0; i < lef_files.size(); ++i) {
    output << "      \"" << lef_files[i] << "\"";
    output << (i + 1 == lef_files.size() ? "\n" : ",\n");
  }
  output << "    ],\n";
  output << "    \"def_path\": \"" << ics55GcdDefPath().string() << "\",\n";
  output << "    \"verilog_path\": \"" << ics55GcdVerilogPath().string()
         << "\",\n";
  output << "    \"lib_path\": [\n";
  for (size_t i = 0; i < liberty_files.size(); ++i) {
    output << "      \"" << liberty_files[i] << "\"";
    output << (i + 1 == liberty_files.size() ? "\n" : ",\n");
  }
  output << "    ],\n";
  output << "    \"sdc_path\": \"" << ics55GcdSdcPath().string() << "\"\n";
  output << "  },\n";
  output << "  \"OUTPUT\": {\n";
  output << "    \"output_dir_path\": \"" << output_root.string() << "\"\n";
  output << "  },\n";
  output << "  \"LayerSettings\": {\n";
  output << "    \"routing_layer_1st\": \"MET1\"\n";
  output << "  }\n";
  output << "}\n";
  output.close();

  return config_path;
}

inline bool prepareIcs55GcdDatabase(
    const fs::path& integration_output_dir = ics55GcdIntegrationOutputDir()) {
  std::error_code ec;
  fs::create_directories(integration_output_dir, ec);
  EXPECT_FALSE(ec) << "failed to create ICS55 gcd integration directory: "
                   << integration_output_dir << ", error=" << ec.message();

  const auto db_config_path = writeIcs55GcdDbConfig(integration_output_dir);
  EXPECT_TRUE(fs::exists(db_config_path))
      << "generated db config missing: " << db_config_path;
  if (!fs::exists(db_config_path)) {
    return false;
  }

  ieval::TimingAPI::destroyInst();
  TimingEngine::destroyTimingEngine();
  Sta::destroySta();
  dmInst->reset();
  return dmInst->init(db_config_path.string());
}

inline unsigned readLibertySequentially(TimingEngine* timing_engine,
                                        const std::vector<std::string>& lib_files) {
  EXPECT_NE(timing_engine, nullptr);
  if (!timing_engine) {
    return 0;
  }

  unsigned read_ok = 1;
  for (const auto& lib_file : lib_files) {
    read_ok &= timing_engine->get_ista()->readLiberty(lib_file.c_str());
  }

  return read_ok;
}

inline fs::path generateGoldenCaseTimingModel(AnalysisMode analysis_mode,
                                              const fs::path& output_lib) {
  static std::mutex generation_mutex;
  std::lock_guard<std::mutex> lock(generation_mutex);

  if (reuseExistingGeneratedLib() && fs::exists(output_lib) &&
      fs::file_size(output_lib) > 0) {
    return output_lib;
  }

  std::error_code ec;
  fs::create_directories(output_lib.parent_path(), ec);
  EXPECT_FALSE(ec) << "failed to create output directory: "
                   << output_lib.parent_path() << ", error=" << ec.message();

  if (fs::exists(output_lib)) {
    fs::remove(output_lib, ec);
    EXPECT_FALSE(ec) << "failed to remove stale output lib: " << output_lib
                     << ", error=" << ec.message();
  }

  const auto integration_output_dir = goldenCaseIntegrationOutputDir();
  fs::create_directories(integration_output_dir, ec);
  EXPECT_FALSE(ec) << "failed to create integration output directory: "
                   << integration_output_dir << ", error=" << ec.message();

  const auto db_config_path = writeGoldenCaseDbConfig(integration_output_dir);
  EXPECT_TRUE(fs::exists(db_config_path))
      << "generated integration db config missing: " << db_config_path;
  if (!fs::exists(db_config_path)) {
    return output_lib;
  }

  ieval::TimingAPI::destroyInst();
  TimingEngine::destroyTimingEngine();
  Sta::destroySta();
  dmInst->reset();
  auto* timing_api = ieval::TimingAPI::getInst();

  EXPECT_TRUE(dmInst->init(db_config_path.string()))
      << "failed to initialize dmInst with " << db_config_path;
  auto& data_config = dmInst->get_config();
  data_config.set_output_path(integration_output_dir.string());
  data_config.set_sdc_path(goldenCaseSdcPath().string());
  const auto liberty_files_raw = asap7GoldenLibertyFiles();
  std::vector<std::string> liberty_files;
  liberty_files.reserve(liberty_files_raw.size());
  for (const auto* liberty_file : liberty_files_raw) {
    liberty_files.emplace_back(liberty_file);
  }
  data_config.set_lib_paths(liberty_files);

  timing_api->evalTiming(goldenCaseRoutingType());

  auto* timing_engine = TimingEngine::getOrCreateTimingEngine();
  timing_engine->set_num_threads(8);
  const auto output_dir_string = output_lib.parent_path().string();
  timing_engine->set_design_work_space(output_dir_string.c_str());
  timing_engine->get_ista()->set_top_module_name("NV_NVDLA_partition_m");
  timing_engine->get_ista()->set_n_worst_path_per_clock(1);
  timing_engine->get_ista()->set_n_worst_path_per_endpoint(512);
  timing_engine->get_ista()->set_analysis_mode(AnalysisMode::kMaxMin);
  timing_engine->updateTiming();
  timing_engine->get_ista()->set_analysis_mode(analysis_mode);
  timing_engine->extractTimingModel(analysis_mode, output_lib.c_str());

  EXPECT_TRUE(fs::exists(output_lib)) << "timing model was not generated at "
                                      << output_lib;
  EXPECT_GT(fs::file_size(output_lib), 0)
      << "generated timing model is empty: " << output_lib;

  return output_lib;
}

inline TimingEngine* prepareGoldenCaseTimingRuntime(
    const fs::path& design_workspace,
    const fs::path& integration_output_dir = goldenCaseRuntimeOutputDir() /
                                             "integration") {
  static std::mutex runtime_mutex;
  std::lock_guard<std::mutex> lock(runtime_mutex);

  std::error_code ec;
  fs::create_directories(design_workspace, ec);
  EXPECT_FALSE(ec) << "failed to create runtime output directory: "
                   << design_workspace << ", error=" << ec.message();

  fs::create_directories(integration_output_dir, ec);
  EXPECT_FALSE(ec) << "failed to create runtime integration directory: "
                   << integration_output_dir << ", error=" << ec.message();

  const auto db_config_path = writeGoldenCaseDbConfig(integration_output_dir);
  EXPECT_TRUE(fs::exists(db_config_path))
      << "generated runtime db config missing: " << db_config_path;
  if (!fs::exists(db_config_path)) {
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
  data_config.set_output_path(integration_output_dir.string());
  data_config.set_sdc_path(goldenCaseSdcPath().string());
  const auto liberty_files_raw = asap7GoldenLibertyFiles();
  std::vector<std::string> liberty_files;
  liberty_files.reserve(liberty_files_raw.size());
  for (const auto* liberty_file : liberty_files_raw) {
    liberty_files.emplace_back(liberty_file);
  }
  data_config.set_lib_paths(liberty_files);

  timing_api->evalTiming(goldenCaseRoutingType());

  auto* timing_engine = TimingEngine::getOrCreateTimingEngine();
  timing_engine->set_num_threads(8);
  const auto output_dir_string = design_workspace.string();
  timing_engine->set_design_work_space(output_dir_string.c_str());
  timing_engine->get_ista()->set_top_module_name("NV_NVDLA_partition_m");
  timing_engine->get_ista()->set_n_worst_path_per_clock(1);
  timing_engine->get_ista()->set_n_worst_path_per_endpoint(512);
  timing_engine->get_ista()->set_analysis_mode(AnalysisMode::kMaxMin);
  timing_engine->updateTiming();

  return timing_engine;
}

inline TimingEngine* prepareIcs55GcdTimingRuntime(
    const fs::path& design_workspace,
    const fs::path& integration_output_dir = ics55GcdRuntimeOutputDir() /
                                             "integration") {
  static std::mutex runtime_mutex;
  std::lock_guard<std::mutex> lock(runtime_mutex);

  std::error_code ec;
  fs::create_directories(design_workspace, ec);
  EXPECT_FALSE(ec) << "failed to create runtime output directory: "
                   << design_workspace << ", error=" << ec.message();

  fs::create_directories(integration_output_dir, ec);
  EXPECT_FALSE(ec) << "failed to create runtime integration directory: "
                   << integration_output_dir << ", error=" << ec.message();

  const auto db_config_path = writeIcs55GcdDbConfig(integration_output_dir);
  EXPECT_TRUE(fs::exists(db_config_path))
      << "generated runtime db config missing: " << db_config_path;
  if (!fs::exists(db_config_path)) {
    return nullptr;
  }

  ieval::TimingAPI::destroyInst();
  TimingEngine::destroyTimingEngine();
  Sta::destroySta();
  dmInst->reset();

  EXPECT_TRUE(dmInst->init(db_config_path.string()))
      << "failed to initialize dmInst with " << db_config_path;

  auto* timing_engine = TimingEngine::getOrCreateTimingEngine();
  timing_engine->set_num_threads(1);

  EXPECT_TRUE(readLibertySequentially(timing_engine, ics55GcdLibertyFiles()))
      << "failed to load ICS55 liberty files";

  auto db_adapter =
      std::make_unique<TimingIDBAdapter>(timing_engine->get_ista());
  db_adapter->set_idb(dmInst->get_idb_builder());
  EXPECT_TRUE(db_adapter->convertDBToTimingNetlist(true))
      << "failed to convert ICS55 gcd DB into timing netlist";
  timing_engine->set_db_adapter(std::move(db_adapter));

  timing_engine->readSdc(ics55GcdSdcPath().c_str());

  const auto output_dir_string = design_workspace.string();
  timing_engine->set_design_work_space(output_dir_string.c_str());
  timing_engine->get_ista()->set_top_module_name("gcd");
  timing_engine->get_ista()->set_n_worst_path_per_clock(1);
  timing_engine->get_ista()->set_n_worst_path_per_endpoint(256);
  timing_engine->get_ista()->set_analysis_mode(AnalysisMode::kMaxMin);
  timing_engine->buildGraph();

  auto* clk_port = timing_engine->get_ista()->get_netlist()->findPort("clk");
  EXPECT_NE(clk_port, nullptr) << "ICS55 gcd netlist is missing port clk";
  if (!clk_port) {
    return nullptr;
  }

  auto* clk_vertex = timing_engine->get_ista()->findVertex(clk_port);
  EXPECT_NE(clk_vertex, nullptr)
      << "ICS55 gcd graph is missing clk vertex after buildGraph";
  if (!clk_vertex) {
    return nullptr;
  }

  timing_engine->updateTiming();

  return timing_engine;
}

inline std::string readFile(const fs::path& file_path) {
  std::ifstream input(file_path);
  return std::string((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
}

inline size_t countRegexMatches(const std::string& text,
                                const std::regex& pattern) {
  return static_cast<size_t>(
      std::distance(std::sregex_iterator(text.begin(), text.end(), pattern),
                    std::sregex_iterator()));
}

inline std::unordered_map<std::string, size_t> countLogicalPins(
    const std::string& liberty_text) {
  static const std::regex kPinRegex(R"(pin\s*\(\s*([^)]+?)\s*\)\s*\{)");
  std::unordered_map<std::string, size_t> pin_counts;

  for (std::sregex_iterator iter(liberty_text.begin(), liberty_text.end(),
                                 kPinRegex);
       iter != std::sregex_iterator(); ++iter) {
    ++pin_counts[(*iter)[1].str()];
  }

  return pin_counts;
}

inline size_t countDuplicatePinNames(const std::string& liberty_text) {
  size_t duplicate_pin_names = 0;
  for (const auto& [pin_name, count] : countLogicalPins(liberty_text)) {
    if (count > 1) {
      ++duplicate_pin_names;
    }
  }

  return duplicate_pin_names;
}

inline size_t countTimingBlocksMissingTimingType(const std::string& liberty_text) {
  static const std::regex kTimingBlockRegex(
      R"(timing\s*\(\s*\)\s*\{([\s\S]*?)\n\s*\})");
  size_t missing_count = 0;

  for (std::sregex_iterator iter(liberty_text.begin(), liberty_text.end(),
                                 kTimingBlockRegex);
       iter != std::sregex_iterator(); ++iter) {
    const auto block_text = (*iter)[1].str();
    if (block_text.find("timing_type") == std::string::npos) {
      ++missing_count;
    }
  }

  return missing_count;
}

inline size_t countLiteral(std::string_view text, std::string_view token) {
  size_t count = 0;
  size_t pos = 0;
  while ((pos = text.find(token, pos)) != std::string_view::npos) {
    ++count;
    pos += token.size();
  }

  return count;
}

inline bool containsLiteral(std::string_view text, std::string_view token) {
  return text.find(token) != std::string_view::npos;
}

}  // namespace ista::test
