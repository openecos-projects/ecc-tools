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
/**
 * @file TestArtifactIO.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-30
 * @brief Test artifact path resolution, sanitization, and report emission helpers.
 */

#pragma once

#include <filesystem>
#include <string>

#include "toolkit/io/InfoReport.hh"

namespace icts_test::toolkit::io {

auto WriteTextArtifact(const std::filesystem::path& path, const std::string& content) -> bool;
auto EmitInfoReport(const InfoReport& report) -> void;
auto SanitizeOutputName(const std::string& raw_name) -> std::string;
auto PrepareCleanOutputDir(const std::filesystem::path& path) -> std::filesystem::path;

auto ResolveOutputDir() -> std::filesystem::path;
auto ResolveTopologyGenOutputDir() -> std::filesystem::path;
auto ResolveClusteringOutputDir() -> std::filesystem::path;

}  // namespace icts_test::toolkit::io
