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

#include <pybind11/pybind11.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace python_interface {

bool initIdb(const std::filesystem::path& config_path);
bool initTechLef(const std::filesystem::path& techlef_path);
bool initLef(const std::vector<std::filesystem::path>& lef_paths);
bool initDef(const std::filesystem::path& def_path);
bool initVerilog(const std::filesystem::path& verilog_path, const std::string& top_module);
bool initLvsVerilog(const std::filesystem::path& verilog_path, const std::string& top_module);
bool initLib(const std::vector<std::filesystem::path>& lib_paths, std::optional<int32_t> thread_number = std::nullopt);
bool initSdc(const std::filesystem::path& sdc_path);
bool initSpef(const std::filesystem::path& spef_path);
bool initVcd(const std::filesystem::path& vcd_path);
bool saveDef(const std::filesystem::path& def_name);
bool saveMacroTCL(const std::filesystem::path& tcl_name);
bool saveNetList(const std::filesystem::path& netlist_path, std::set<std::string> exclude_cell_names = {},
                 bool is_add_space_for_escape_name = false);
bool saveGDSII(const std::filesystem::path& gds_name, const std::filesystem::path& layer_map_path);
bool saveJson(const std::filesystem::path& path);
bool saveGeometrySnapshot(const std::filesystem::path& output_dir, bool include_drc = false,
                          const std::optional<std::filesystem::path>& thumbnail_path = std::nullopt,
                          const std::optional<std::string>& clock_tree_stage = std::nullopt,
                          const pybind11::object& clock_tree_graph = pybind11::none());
std::size_t canonicalizeNetNames();
bool placeInstance(const std::string& inst_name, int llx, int lly, const std::string& orient, const std::string& cellmaster,
                   const std::string& source = "", const std::string& placement_status = "fixed", bool create_if_missing = true);
bool initializeGeometrySession();
pybind11::dict syncInstanceGeometry(const std::string& inst_name);
bool saveGeometrySessionSnapshot(const std::filesystem::path& output_dir);
bool resetGeometrySession();
bool saveData(const std::filesystem::path& path);
bool resetData();
bool loadData(const std::filesystem::path& path);
bool writeAbstractLef(const std::filesystem::path& output_lef_path);

}  // namespace python_interface
