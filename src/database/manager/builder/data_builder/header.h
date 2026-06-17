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

#include <cstdint>
#include <memory>
#include <string>

#include "IdbDesign.h"
#include "IdbLayout.h"

namespace idb {

#define kDbSuccess 0
#define kDbFail 1

#define CLOCKS_PER_MS 1000

namespace data_binary {

constexpr uint32_t kArchiveVersion = 2;

enum class ArchiveSection : uint32_t
{
  kLayoutMetadata = 1,
  kLayoutUnits,
  kLayoutDie,
  kLayoutLayers,
  kLayoutSites,
  kLayoutRows,
  kLayoutGCellGrid,
  kLayoutTrackGrid,
  kLayoutCellMasters,
  kLayoutVias,
  kLayoutViaRules,

  kDesignMetadata = 101,
  kDesignInstances,
  kDesignIoPins,
  kDesignVias,
  kDesignNets,
  kDesignSpecialNets,
  kDesignBlockages,
  kDesignRegions,
  kDesignSlots,
  kDesignGroups,
  kDesignFills,
};

bool write_layout(const std::string& folder, IdbLayout* layout, bool parallel = true);
bool read_layout_into(const std::string& folder, IdbLayout* layout, bool parallel = true);
std::unique_ptr<IdbLayout> read_layout(const std::string& folder, bool parallel = true);

bool write_design(const std::string& folder, IdbDesign* design, bool parallel = true);
bool read_design_into(const std::string& folder, IdbDesign* design, IdbLayout* layout, bool parallel = true);
std::unique_ptr<IdbDesign> read_design(const std::string& folder, IdbLayout* layout, bool parallel = true);

}  // namespace data_binary

}  // namespace idb
