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
#include <eccdb/eccdb.h>

#include <type_traits>
#include <unordered_map>

int main()
{
  static_assert(!std::is_copy_constructible_v<eccdb::Database>);
  static_assert(std::is_trivially_copyable_v<eccdb::NetId>);
  static_assert(std::is_copy_constructible_v<eccdb::NetData>);
  static_assert(std::is_copy_constructible_v<eccdb::NetRef>);
  std::unordered_map<eccdb::NetId, eccdb::NetData> snapshots;
  snapshots.emplace(eccdb::NetId{1}, eccdb::NetData{.name = "example"});
  eccdb::Config config{
      .input = eccdb::BinaryInput{.files = {.technology = "tech.edb", .library = "library.edb", .design = "design.edb"}},
  };
  static_cast<void>(snapshots);
  static_cast<void>(config);
  return eccdb::Database::supportsLefDef() ? 0 : 0;
}
