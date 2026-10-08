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

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "database/DatabaseConfig.h"

namespace eccdb {

class DatabaseContext;

struct ImportDiagnostic
{
  std::string source;
  std::string statement;
  std::size_t occurrence_count = 0;
};

// Owns the native technology, library and design stores and their shared
// service context. Read native objects through the stores; coordinate business
// mutations through services. This container does not define another object
// model or delegate per-object CRUD.
class Database
{
 public:
  [[nodiscard]] static Database open(const Config& config);
  [[nodiscard]] static bool supportsLefDef() noexcept;

  ~Database();
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  Database(Database&&) noexcept;
  Database& operator=(Database&&) noexcept;

  [[nodiscard]] const std::vector<ImportDiagnostic>& diagnostics() const noexcept;
  void writeDef(const std::filesystem::path& file) const;
  void writeBinary(const BinaryFiles& files) const;
  // Borrowed services follow the owned state through moves. Move assignment
  // destroys the target's previous state and invalidates its previous borrows.
  [[nodiscard]] DatabaseContext& context() const;

 private:
  struct State;
  explicit Database(std::unique_ptr<State> state) noexcept;
  [[nodiscard]] State& state() const;

  std::unique_ptr<State> _state;
};

}  // namespace eccdb
