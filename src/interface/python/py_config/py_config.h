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

namespace python_interface {
bool flow_init(const std::filesystem::path& flow_config);

bool db_init(const std::optional<std::filesystem::path>& config_path, const std::optional<std::filesystem::path>& tech_lef_path,
             const std::vector<std::filesystem::path>& lef_paths, const std::optional<std::filesystem::path>& def_path,
             const std::optional<std::filesystem::path>& verilog_path, const std::optional<std::filesystem::path>& output_path,
             const std::optional<std::filesystem::path>& feature_path, const std::vector<std::filesystem::path>& lib_paths,
             const std::optional<std::filesystem::path>& sdc_path);
}  // namespace python_interface