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
#include <string>
#include <string_view>

namespace ircx {

// Canonicalize a name/path for the native writer's '/' hierarchy and '[]' bus
// delimiters, preserving existing escape pairs. Compose instance:pin separately.
std::string escapeSPEFName(std::string_view name);

// Remove empty optional sections and escape unquoted '$' in backend SPEF output.
// Preserve comments and quoted text; this is not a general SPEF validator.
bool sanitizeSPEFFile(const std::filesystem::path& spef_path);

// Sanitize all .spef files inside output_dir; returns false on directory or file errors.
bool sanitizeSPEFDirectory(const std::filesystem::path& output_dir);

// Read JSON config at config_path, resolve relative "output" against its parent directory, and sanitize it.
bool sanitizeSPEFOutputsForConfig(const std::filesystem::path& config_path);

}  // namespace ircx
