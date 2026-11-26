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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ARInternalTypes.hpp"
#include "IdbDesign.h"
#include "IdbRow.h"

namespace imj {

class RowSiteGrid
{
 public:
  void init(idb::IdbDesign* design);

  std::vector<SiteCandidate> findCandidates(const DiodeCandidateMaster& dm, int32_t anchor_x, int32_t anchor_y,
                                            int32_t radius_dbu, size_t max_candidates = 24) const;

  void markOccupied(size_t row_idx, int32_t site_idx, int32_t sites_needed);
  void unmarkOccupied(size_t row_idx, int32_t site_idx, int32_t sites_needed);

 private:
  struct RowOccupancy
  {
    idb::IdbRow* row = nullptr;
    std::string site_name;
    int32_t x0 = 0;
    int32_t y0 = 0;
    int32_t step_x = 0;
    int32_t site_width = 0;
    int32_t height = 0;
    int32_t num_sites = 0;
    idb::IdbOrient orient = idb::IdbOrient::kN_R0;
    std::vector<bool> occupied;

    void blockRect(const idb::IdbRect& r);
  };

  void setOccupiedRange(size_t row_idx, int32_t site_idx, int32_t sites_needed, bool value);

  std::vector<RowOccupancy> _rows;
};

}  // namespace imj
