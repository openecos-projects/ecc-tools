// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
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

#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>

namespace eccdb::binary_detail {

enum class BinaryDatabaseKind : uint32_t
{
  kTech = 1,
  kLibrary = 2,
  kDesign = 3
};

struct BinaryFileHeader
{
  BinaryDatabaseKind kind = BinaryDatabaseKind::kTech;
  uint32_t schema_version = 0;
  uint64_t payload_size = 0;
};

using BinaryPayloadWriter = std::function<void(std::ostream&)>;
using BinaryPayloadReader = std::function<void(std::istream&, const BinaryFileHeader&)>;

void writeBinaryFile(const std::filesystem::path& path, BinaryFileHeader header, const BinaryPayloadWriter& writer);
void readBinaryFile(const std::filesystem::path& path, BinaryDatabaseKind expected_kind, uint32_t expected_schema_version,
                    const BinaryPayloadReader& reader);

}  // namespace eccdb::binary_detail
