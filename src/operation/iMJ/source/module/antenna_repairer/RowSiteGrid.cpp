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
#include "RowSiteGrid.hpp"

#include <algorithm>
#include <cstdlib>

#include "IdbBlockages.h"
#include "IdbHalo.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbRegion.h"
#include "IdbSite.h"

namespace imj {

namespace {

bool macroSupportsOrient(idb::IdbCellMaster* master, idb::IdbOrient orient)
{
  if (!master) {
    return false;
  }
  const bool sym_x = master->is_symmetry_x();
  const bool sym_y = master->is_symmetry_y();
  const bool sym_r90 = master->is_symmetry_R90();
  switch (normalizeOrient(orient)) {
    case idb::IdbOrient::kN_R0:
      return true;
    case idb::IdbOrient::kFS_MX:
      return sym_x;
    case idb::IdbOrient::kFN_MY:
      return sym_y;
    case idb::IdbOrient::kS_R180:
      return sym_x && sym_y;
    case idb::IdbOrient::kW_R90:
      return sym_r90;
    case idb::IdbOrient::kE_R270:
      return sym_r90 && sym_x && sym_y;
    case idb::IdbOrient::kFW_MX90:
      return sym_r90 && sym_x;
    case idb::IdbOrient::kFE_MY90:
      return sym_r90 && sym_y;
    default:
      return false;
  }
}

}  // namespace

void RowSiteGrid::RowOccupancy::blockRect(const idb::IdbRect& r)
{
  if (step_x <= 0 || height <= 0 || num_sites <= 0) {
    return;
  }
  if (r.get_low_y() >= y0 + height || r.get_high_y() <= y0) {
    return;
  }
  if (r.get_high_x() <= r.get_low_x()) {
    return;
  }
  const int32_t sw = site_width > 0 ? site_width : step_x;
  const int64_t first_s =
      std::max<int64_t>(0, ceilDiv(static_cast<int64_t>(r.get_low_x()) - x0 - sw + 1, step_x));
  const int64_t last_s =
      std::min<int64_t>(num_sites - 1, floorDiv(static_cast<int64_t>(r.get_high_x()) - x0 - 1, step_x));
  for (int64_t s = first_s; s <= last_s; ++s) {
    occupied[static_cast<size_t>(s)] = true;
  }
}

void RowSiteGrid::init(idb::IdbDesign* design)
{
  _rows.clear();
  if (!design || !design->get_layout() || !design->get_layout()->get_rows()) {
    return;
  }
  for (idb::IdbRow* row : design->get_layout()->get_rows()->get_row_list()) {
    if (!row || !row->is_horizontal() || !row->get_site() || !row->get_original_coordinate()) {
      continue;
    }
    const int32_t site_w = row->get_site()->get_width();
    const int32_t step_x = row->get_step_x() > 0 ? row->get_step_x() : site_w;
    const int32_t height = row->get_site()->get_height();
    const int32_t num_sites = row->get_row_num_x();
    if (step_x <= 0 || height <= 0 || num_sites <= 0) {
      continue;
    }
    RowOccupancy ro;
    ro.row = row;
    ro.site_name = row->get_site()->get_name();
    ro.x0 = row->get_original_coordinate()->get_x();
    ro.y0 = row->get_original_coordinate()->get_y();
    ro.step_x = step_x;
    ro.site_width = site_w > 0 ? site_w : step_x;
    ro.height = height;
    ro.num_sites = num_sites;
    ro.orient = normalizeOrient(row->get_orient());
    ro.occupied.assign(static_cast<size_t>(num_sites), false);
    _rows.push_back(std::move(ro));
  }

  if (design->get_instance_list()) {
    for (idb::IdbInstance* inst : design->get_instance_list()->get_instance_list()) {
      if (!inst || inst->is_unplaced() || !inst->has_placed()) {
        continue;
      }
      idb::IdbRect* bb = inst->get_bounding_box();
      if (bb == nullptr || bb->get_width() <= 0 || bb->get_height() <= 0) {
        inst->set_bounding_box();
        bb = inst->get_bounding_box();
      }
      if (!bb) {
        continue;
      }
      for (RowOccupancy& ro : _rows) {
        ro.blockRect(*bb);
      }
      if (idb::IdbHalo* halo = inst->get_halo()) {
        const int32_t ext_l = std::max(0, halo->get_extend_lef());
        const int32_t ext_b = std::max(0, halo->get_extend_bottom());
        const int32_t ext_r = std::max(0, halo->get_extend_right());
        const int32_t ext_t = std::max(0, halo->get_extend_top());
        idb::IdbRect halo_rect(bb->get_low_x() - ext_l, bb->get_low_y() - ext_b, bb->get_high_x() + ext_r,
                               bb->get_high_y() + ext_t);
        for (RowOccupancy& ro : _rows) {
          ro.blockRect(halo_rect);
        }
      }
    }
  }

  if (design->get_blockage_list()) {
    for (idb::IdbBlockage* blk : design->get_blockage_list()->get_blockage_list()) {
      auto* pblk = dynamic_cast<idb::IdbPlacementBlockage*>(blk);
      if (!pblk) {
        continue;
      }
      for (idb::IdbRect* r : pblk->get_rect_list()) {
        if (!r) {
          continue;
        }
        for (RowOccupancy& ro : _rows) {
          ro.blockRect(*r);
        }
      }
    }
  }

  if (design->get_region_list()) {
    for (idb::IdbRegion* region : design->get_region_list()->get_region_list()) {
      if (!region || region->get_type() != idb::IdbRegionType::kFence) {
        continue;
      }
      for (idb::IdbRect* r : region->get_boundary()) {
        if (!r) {
          continue;
        }
        for (RowOccupancy& ro : _rows) {
          ro.blockRect(*r);
        }
      }
    }
  }
}

std::vector<SiteCandidate> RowSiteGrid::findCandidates(const DiodeCandidateMaster& dm, int32_t anchor_x,
                                                       int32_t anchor_y, int32_t radius_dbu, size_t max_candidates) const
{
  std::vector<SiteCandidate> candidates;
  if (!dm.master) {
    return candidates;
  }
  const idb::IdbSite* master_site = dm.master->get_site();
  const std::string master_site_name = master_site ? master_site->get_name() : "";

  for (size_t ri = 0; ri < _rows.size(); ++ri) {
    const RowOccupancy& ro = _rows[ri];
    if (std::abs(ro.y0 + ro.height / 2 - anchor_y) > radius_dbu + ro.height) {
      continue;
    }
    if (!master_site_name.empty() && ro.site_name != master_site_name) {
      continue;
    }
    if (!macroSupportsOrient(dm.master, ro.orient)) {
      continue;
    }
    if (dm.master->get_height() > static_cast<uint32_t>(ro.height)) {
      continue;
    }
    if (ro.step_x > ro.site_width && dm.master->get_width() > static_cast<uint32_t>(ro.site_width)) {
      continue;
    }
    const int32_t sites_needed =
        static_cast<int32_t>(std::max<int64_t>(1, ceilDiv(dm.master->get_width(), ro.step_x)));
    if (sites_needed > ro.num_sites) {
      continue;
    }
    const int32_t min_s = static_cast<int32_t>(
        std::max<int64_t>(0, floorDiv(static_cast<int64_t>(anchor_x - radius_dbu) - ro.x0, ro.step_x)));
    const int32_t max_s = static_cast<int32_t>(std::min<int64_t>(
        ro.num_sites - sites_needed, floorDiv(static_cast<int64_t>(anchor_x + radius_dbu) - ro.x0, ro.step_x)));

    for (int32_t s = min_s; s <= max_s; ++s) {
      bool free_run = true;
      for (int32_t k = 0; k < sites_needed; ++k) {
        if (ro.occupied[static_cast<size_t>(s + k)]) {
          free_run = false;
          break;
        }
      }
      if (!free_run) {
        continue;
      }
      const int32_t inst_x = ro.x0 + s * ro.step_x;
      const int32_t inst_y = ro.y0;
      const int64_t approx_dist = static_cast<int64_t>(std::abs(inst_x - anchor_x)) + std::abs(inst_y - anchor_y);
      candidates.push_back({ri, s, sites_needed, inst_x, inst_y, ro.orient, approx_dist});
    }
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const SiteCandidate& a, const SiteCandidate& b) { return a.dist < b.dist; });
  if (candidates.size() > max_candidates) {
    candidates.resize(max_candidates);
  }
  return candidates;
}

void RowSiteGrid::markOccupied(size_t row_idx, int32_t site_idx, int32_t sites_needed)
{
  setOccupiedRange(row_idx, site_idx, sites_needed, true);
}

void RowSiteGrid::unmarkOccupied(size_t row_idx, int32_t site_idx, int32_t sites_needed)
{
  setOccupiedRange(row_idx, site_idx, sites_needed, false);
}

void RowSiteGrid::setOccupiedRange(size_t row_idx, int32_t site_idx, int32_t sites_needed, bool value)
{
  if (row_idx >= _rows.size() || site_idx < 0 || sites_needed <= 0) {
    return;
  }
  RowOccupancy& ro = _rows[row_idx];
  const int32_t end_s = std::min(ro.num_sites, site_idx + sites_needed);
  for (int32_t s = site_idx; s < end_s; ++s) {
    ro.occupied[static_cast<size_t>(s)] = value;
  }
}

}  // namespace imj
