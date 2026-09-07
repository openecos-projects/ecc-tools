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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "FIModel.hpp"
#include "FIRow.hpp"
#include "FillerInserter.hpp"

int main()
{
  izh::FIRow fi_row;
  fi_row.set_site_count(8);
  fi_row.initSiteAvailableList();
  fi_row.blockSiteRange(-2, 1);
  fi_row.blockSiteRange(4, 5);
  fi_row.buildAvailableSegmentList();
  std::vector<izh::FISegment>& available_segment_list = fi_row.get_available_segment_list();
  if (available_segment_list.size() != 2 || available_segment_list[0].get_begin_site_idx() != 2 || available_segment_list[0].get_end_site_idx() != 3
      || available_segment_list[1].get_begin_site_idx() != 6 || available_segment_list[1].get_end_site_idx() != 7) {
    return 1;
  }

  izh::FIModel fi_model;
  fi_model.set_min_filler_site_count(2);
  fi_model.addInsertedFillerNum();
  if (fi_model.get_min_filler_site_count() != 2 || fi_model.get_inserted_filler_num() != 1) {
    return 1;
  }

  izh::FillerInserter::initInst();
  izh::FillerInserter* fi_instance = &ZHFI;
  izh::FillerInserter::initInst();
  if (fi_instance != &ZHFI) {
    return 1;
  }
  izh::FillerInserter::destroyInst();
  return 0;
}
