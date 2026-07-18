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

#include "gtest/gtest.h"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

namespace fs = std::filesystem;

class ScopedEnvVar {
 public:
  explicit ScopedEnvVar(const char* name) : _name(name) {
    const char* original = std::getenv(_name.c_str());
    if (original) {
      _had_original = true;
      _original_value = original;
    }
  }

  ~ScopedEnvVar() {
    if (_had_original) {
      setenv(_name.c_str(), _original_value.c_str(), 1);
    } else {
      unsetenv(_name.c_str());
    }
  }

 private:
  std::string _name;
  bool _had_original = false;
  std::string _original_value;
};

TEST(CharacterTimingTestCommonTest,
     default_output_root_uses_worktree_local_artifacts_when_env_is_unset) {
  ScopedEnvVar output_root_guard("IEDA_CHARACTER_TIMING_OUTPUT_ROOT");
  unsetenv("IEDA_CHARACTER_TIMING_OUTPUT_ROOT");

  const fs::path expected =
      ista::test::currentWorktreeRoot() / "artifacts/ieda/character_timing";
  EXPECT_EQ(ista::test::defaultOutputRoot(), expected);
}

TEST(CharacterTimingTestCommonTest,
     default_output_root_honors_explicit_env_override) {
  ScopedEnvVar output_root_guard("IEDA_CHARACTER_TIMING_OUTPUT_ROOT");
  const fs::path override_root =
      fs::temp_directory_path() / "ieda_character_timing_override";
  setenv("IEDA_CHARACTER_TIMING_OUTPUT_ROOT", override_root.c_str(), 1);

  EXPECT_EQ(ista::test::defaultOutputRoot(), override_root);
}

}  // namespace
