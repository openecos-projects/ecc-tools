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

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "design/DesignStore.h"

namespace eccdb {

struct DefDesignImportDiagnostic
{
  std::string statement;
  std::size_t occurrence_count = 0;
};

// One-shot importer from SI2 DEF callbacks into one empty DesignStore.
// Callbacks create EnTT objects immediately. Only unresolved cross-section
// references are retained until the referenced object is available.
class DefDesignImporter
{
 public:
  explicit DefDesignImporter(DesignStore& design) : _design(design) {}

  void import(const std::filesystem::path& file);

  [[nodiscard]] const std::vector<DefDesignImportDiagnostic>& diagnostics() const noexcept { return _diagnostics; }

 private:
  DesignStore& _design;
  std::vector<DefDesignImportDiagnostic> _diagnostics;
  bool _used = false;
};

}  // namespace eccdb
