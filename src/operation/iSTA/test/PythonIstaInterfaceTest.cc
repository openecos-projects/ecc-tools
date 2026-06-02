// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include "CharacterTimingTestCommon.hh"
#include "py_db.h"
#include "py_ista.h"

#include "gtest/gtest.h"

#include <filesystem>
#include <string>

namespace {

namespace fs = std::filesystem;

TEST(PythonIstaInterfaceTest, write_timing_model_exports_liberty_from_runtime) {
  const fs::path output_root =
      ista::test::goldenCaseRuntimeOutputDir() / "python_interface";
  auto* timing_engine = ista::test::prepareGoldenCaseTimingRuntime(output_root);
  ASSERT_NE(timing_engine, nullptr);

  const fs::path output_lib = output_root / "NV_NVDLA_partition_m.python.max.lib";
  std::error_code ec;
  fs::remove(output_lib, ec);

  EXPECT_TRUE(
      python_interface::writeTimingModel(output_lib.string(), "max"));
  ASSERT_TRUE(fs::exists(output_lib)) << output_lib;
  EXPECT_GT(fs::file_size(output_lib), 0);

  const std::string liberty_text = ista::test::readFile(output_lib);
  EXPECT_NE(liberty_text.find("library (NV_NVDLA_partition_m)"),
            std::string::npos);
  EXPECT_NE(liberty_text.find("cell (NV_NVDLA_partition_m)"),
            std::string::npos);
}

TEST(PythonIstaInterfaceTest, write_abstract_lef_exports_block_macro_from_runtime) {
  const fs::path output_root =
      ista::test::goldenCaseRuntimeOutputDir() / "python_interface";
  auto* timing_engine = ista::test::prepareGoldenCaseTimingRuntime(output_root);
  ASSERT_NE(timing_engine, nullptr);

  const fs::path output_lef = output_root / "NV_NVDLA_partition_m.python.lef";
  std::error_code ec;
  fs::remove(output_lef, ec);

  EXPECT_TRUE(python_interface::writeAbstractLef(output_lef.string()));
  ASSERT_TRUE(fs::exists(output_lef)) << output_lef;
  EXPECT_GT(fs::file_size(output_lef), 0);

  const std::string lef_text = ista::test::readFile(output_lef);
  EXPECT_NE(lef_text.find("MACRO NV_NVDLA_partition_m"), std::string::npos);
  EXPECT_NE(lef_text.find("CLASS BLOCK"), std::string::npos);
  EXPECT_NE(lef_text.find("OBS"), std::string::npos);
}

}  // namespace
