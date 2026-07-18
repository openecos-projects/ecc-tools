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
 * @file LayerTable.hh
 * @brief Design-to-process layer mapping table.
 */
#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Types.hh"

namespace ircx {

// LayerTable
//
// Centralizes all design ↔ process layer name/id mappings.
//
// Populated in three stages matching the RCX pipeline:
//   1. readCorner()  → registerProcessLayer()
//   2. adaptDB()     → registerDesignLayer()
//   3. readMapping() → registerMapping()
//
// All query methods throw std::out_of_range on a missing key.
class LayerTable {
 public:
  // Registration
  void clear() {
    clearDesignLayers();
    clearProcessLayers();
    clearMappings();
  }

  void clearDesignLayers() {
    design_id_to_name_.clear();
    design_name_to_id_.clear();
  }

  void clearProcessLayers() {
    process_id_to_name_.clear();
    process_name_to_id_.clear();
  }

  void clearMappings() {
    design_to_process_name_.clear();
    process_to_design_name_.clear();
  }

  void registerDesignLayer(Size design_id,
                           std::string name) {
    design_id_to_name_[design_id] = name;
    design_name_to_id_[std::move(name)] = design_id;
  }

  void copyDesignLayersFrom(const LayerTable& other) {
    clearDesignLayers();
    for (const auto& [id, name] : other.designLayers()) {
      registerDesignLayer(id, name);
    }
  }

  void registerProcessLayer(Size process_id,
                            std::string name) {
    process_id_to_name_[process_id] = name;
    process_name_to_id_[std::move(name)] = process_id;
  }

  // Registers both directions of the design↔process name mapping.
  void registerMapping(const std::string& design_name,
                       const std::string& process_name)
  {
    design_to_process_name_[design_name] = process_name;
    process_to_design_name_[process_name] = design_name;
  }

  // Single-domain queries

  Size designId(const std::string& name) const {
    return design_name_to_id_.at(name);
  }
  const std::string& designName(Size design_id) const {
    return design_id_to_name_.at(design_id);
  }

  std::vector<std::pair<Size, std::string>> designLayers() const {
    std::vector<std::pair<Size, std::string>> layers;
    layers.reserve(design_id_to_name_.size());
    for (const auto& [id, name] : design_id_to_name_) {
      layers.emplace_back(id, name);
    }
    return layers;
  }

  Size processId(const std::string& name) const {
    return process_name_to_id_.at(name);
  }
  const std::string& processName(Size process_id) const {
    return process_id_to_name_.at(process_id);
  }

  // Cross-domain queries

  Size designToProcessId(Size design_id) const {
    const std::string& pname = design_to_process_name_.at(design_id_to_name_.at(design_id));
    return process_name_to_id_.at(pname);
  }

  Size processToDesignId(Size process_id) const {
    const std::string& dname = process_to_design_name_.at(process_id_to_name_.at(process_id));
    return design_name_to_id_.at(dname);
  }

 private:
  std::unordered_map<Size, std::string> design_id_to_name_;
  std::unordered_map<std::string, Size> design_name_to_id_;

  std::unordered_map<Size, std::string> process_id_to_name_;
  std::unordered_map<std::string, Size> process_name_to_id_;

  std::unordered_map<std::string, std::string> design_to_process_name_;
  std::unordered_map<std::string, std::string> process_to_design_name_;
};

}  // namespace ircx
