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
#include <string>
#include <vector>

#include "report_manager.h"
namespace python_interface {
bool reportDbSummary(const std::optional<std::filesystem::path>& path);
bool reportWireLength(const std::optional<std::filesystem::path>& path);
bool reportCong(const std::optional<std::filesystem::path>& path);
bool reportDanglingNet(const std::optional<std::filesystem::path>& path);
bool reportRoute(const std::optional<std::filesystem::path>& path, const std::string& netname, bool summary);
bool reportPlaceDistribution(const std::vector<std::string>& prefixes);
bool reportPrefixedInst(const std::string& prefix, int level, int num_threshold);

bool reportDRC(const std::filesystem::path& filename);
}  // namespace python_interface