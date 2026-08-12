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

#include "design_read.h"

namespace idb {

DesignRead::DesignRead(IdbLayout* layout) : _layout(layout)
{
}

IdbDesign* DesignRead::readDesign(const char* folder)
{
  return folder == nullptr ? nullptr : readDesign(std::string(folder), true);
}

IdbDesign* DesignRead::readDesign(const std::string& folder, bool parallel)
{
  auto design = data_binary::read_design(folder, _layout, parallel);
  return design.release();
}

bool DesignRead::readDesign(IdbDesign* design, const std::string& folder, bool parallel)
{
  return data_binary::read_design_into(folder, design, _layout, parallel);
}

}  // namespace idb
