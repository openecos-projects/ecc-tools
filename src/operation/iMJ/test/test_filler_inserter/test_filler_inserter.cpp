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
#include "MJInterface.hpp"
#include "idm.h"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void testFillerSelection()
{
  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
  const auto data = std::filesystem::path(__FILE__).parent_path() / "data";
  const auto output = std::filesystem::temp_directory_path() / ("test_imj_filler_" + std::to_string(getpid()));
  dmInst->reset();
  require(dmInst->readLef(std::vector<std::string>{(data / "filler.lef").string()}, true), "read filler LEF");
  require(dmInst->readDef((data / "filler.def.in").string()), "read filler DEF");
  MJI.initMJ({{"-temp_directory_path", output.string()}});

  auto* layout = dmInst->get_idb_layout();
  auto* corner = layout->get_cell_master_list()->find_cell_master("IO_CORNER");
  require(corner->is_endcap() && corner->get_site()->is_pad_site(), "fixture must model an IO endcap");
  MJI.insertFiller({});
  auto* instances = dmInst->get_idb_design()->get_instance_list();
  std::map<std::string, int> counts;
  for (auto* instance : instances->get_instance_list()) {
    counts[instance->get_cell_master()->get_name()]++;
  }
  require(counts == std::map<std::string, int>{{"IO_CORNER", 1}, {"PG_FILL4", 1}, {"FILL1", 3}}, "automatic filler masters");
  auto* original_corner = instances->find_instance("corner");
  require(original_corner->get_status() == idb::IdbPlacementStatus::kFixed && original_corner->get_coordinate()->get_x() == 15000
              && original_corner->get_coordinate()->get_y() == 15000,
          "existing IO corner changed");

  // An explicit list must not turn any ENDCAP subclass into a core filler.
  for (auto type : {idb::CellMasterType::kEndcap, idb::CellMasterType::kEndcapPre, idb::CellMasterType::kEndcapPost,
                    idb::CellMasterType::kEndcapTopLeft, idb::CellMasterType::kEndcapTopRight, idb::CellMasterType::kEndcapBottomLeft,
                    idb::CellMasterType::kEndcapBottomRight}) {
    corner->set_type(type);
    bool rejected = false;
    try {
      MJI.insertFiller({{"-filler", std::string("IO_CORNER")}});
    } catch (const std::runtime_error& error) {
      rejected = std::string(error.what()).find("The master is not a core filler: IO_CORNER") != std::string::npos;
      imj::FillerInserter::destroyInst();
    }
    require(rejected, "explicit ENDCAP filler must be rejected before width validation");
    require(instances->get_instance_list().size() == 5, "rejected filler mutated instances");
  }
  imj::MJInterface::destroyInst();
  dmInst->reset();
}
}  // namespace

int main()
{
  imj::FIRow fi_row;
  fi_row.set_site_count(8);
  fi_row.initSiteAvailableList();
  fi_row.blockSiteRange(-2, 1);
  fi_row.blockSiteRange(4, 5);
  fi_row.buildAvailableSegmentList();
  std::vector<imj::FISegment>& available_segment_list = fi_row.get_available_segment_list();
  if (available_segment_list.size() != 2 || available_segment_list[0].get_begin_site_idx() != 2 || available_segment_list[0].get_end_site_idx() != 3
      || available_segment_list[1].get_begin_site_idx() != 6 || available_segment_list[1].get_end_site_idx() != 7) {
    return 1;
  }

  imj::FIModel fi_model;
  fi_model.set_min_filler_site_count(2);
  fi_model.addInsertedFillerNum();
  if (fi_model.get_min_filler_site_count() != 2 || fi_model.get_inserted_filler_num() != 1) {
    return 1;
  }

  imj::FillerInserter::initInst();
  imj::FillerInserter* fi_instance = &MJFI;
  imj::FillerInserter::initInst();
  if (fi_instance != &MJFI) {
    return 1;
  }
  imj::FillerInserter::destroyInst();
  testFillerSelection();
  return 0;
}
