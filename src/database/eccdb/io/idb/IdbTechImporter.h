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

#include <string_view>

#include <entt/container/dense_map.hpp>

#include "tech/TechStore.h"

namespace idb {
class IdbLayer;
class IdbLayout;
}  // namespace idb

namespace eccdb {

// One-shot adapter from the legacy LEF/iDB layout into the EnTT Tech model.
// Source pointers are used only while importing; no legacy pointer or ID is
// written into a persistent component.
class IdbTechImporter
{
 public:
  explicit IdbTechImporter(TechStore& database) : _database(database) {}

  void import(::idb::IdbLayout& source);

  [[nodiscard]] TechLayerId layerId(const ::idb::IdbLayer* source) const;
  [[nodiscard]] TechViaMasterId viaMasterId(std::string_view name) const;

 private:
  TechStore& _database;
  entt::dense_map<const ::idb::IdbLayer*, TechLayerId> _layer_ids;
  bool _imported = false;
};

}  // namespace eccdb
