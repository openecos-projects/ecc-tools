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
#pragma once

#include <filesystem>
#include <optional>
#include <variant>
#include <vector>

namespace eccdb {

// The binary archive intentionally stores each registry domain separately.
struct BinaryFiles
{
  std::filesystem::path technology;
  std::filesystem::path library;
  std::filesystem::path design;
};

struct LefDefInput
{
  // Files are passed to both the technology and library importers. This
  // supports combined LEFs as well as separate technology and macro LEFs.
  std::vector<std::filesystem::path> lef_files;
  std::filesystem::path def_file;
};

struct BinaryInput
{
  BinaryFiles files;
};

using DatabaseInput = std::variant<LefDefInput, BinaryInput>;

enum class PolygonMode
{
  kNative,
  kRectangularized
};

struct RuntimeOptions
{
  PolygonMode polygon_mode = PolygonMode::kNative;
};

struct Config
{
  DatabaseInput input = LefDefInput{};
  RuntimeOptions runtime;

  // When set for LEF/DEF input, Database::open writes a reusable binary
  // snapshot after the text import succeeds.
  std::optional<BinaryFiles> binary_cache;
};

}  // namespace eccdb
