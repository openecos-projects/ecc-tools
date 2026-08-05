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

#include "CellMaster.hpp"
#include "PPModel.hpp"
#include "Row.hpp"

namespace ifp {

#define FPPP (ifp::PhyPlacer::getInst())

class PhyPlacer
{
 public:
  static void initInst();
  static PhyPlacer& getInst();
  static void destroyInst();
  // function
  void place();

 private:
  // self
  static PhyPlacer* _pp_instance;

  PhyPlacer() = default;
  PhyPlacer(const PhyPlacer& other) = delete;
  PhyPlacer(PhyPlacer&& other) = delete;
  ~PhyPlacer() = default;
  PhyPlacer& operator=(const PhyPlacer& other) = delete;
  PhyPlacer& operator=(PhyPlacer&& other) = delete;
  // function

  void placePhyCell(PPModel& pp_model);
  void adjustTapDistance(int32_t& inst_space);

  int32_t buildPPRegionList(PPModel& pp_model);
  void buildPPRegionInRow(PPModel& pp_model, Row& row, int32_t row_idx);
  std::vector<std::pair<int32_t, int32_t>> getMacroBlockageIntervalList(Row& row);
  void addPPRegion(PPModel& pp_model, Row& row, int32_t row_idx, int32_t start_coord, int32_t end_coord);

  void buildPPBoundaryRegionList(PPModel& pp_model);
  void addCorePPBoundaryRegion(PPModel& pp_model);
  void addPPBoundaryRegion(PPModel& pp_model, PPRegion& pp_region, int32_t start_coord, int32_t end_coord,
                           PPBoundaryType boundary_type);
  void addMacroPPBoundaryRegion(PPModel& pp_model);
  void addMacroPPBoundaryRegionInRow(PPModel& pp_model, Row& row, PlanarRect& placement_halo_rect,
                                     PPBoundaryType boundary_type);

  void insertSideEndcap(PPModel& pp_model, int32_t& endcap_idx);
  int32_t getCellMasterWidthByOrient(CellMaster& cell_master, PlacementOrientation orient);
  void addPhyCell(PPModel& pp_model, PPRegion& pp_region, std::string instance_name, std::string cell_master_name,
                  int32_t x_coord);
  bool isPhyCellOnSite(PPRegion& pp_region, CellMaster& cell_master, int32_t x_coord);

  void insertWellTap(PPModel& pp_model, int32_t tap_distance, int32_t& tapcell_idx);
  void insertWellTapInRegion(PPModel& pp_model, PPRegion& pp_region, int32_t tap_distance, int32_t tap_offset,
                             int32_t& tapcell_idx);
  void insertBoundaryWellTap(PPModel& pp_model, int32_t tap_distance, int32_t& tapcell_idx);
  int32_t getAvailableCellCoord(PPModel& pp_model, PPRegion& pp_region, int32_t start_coord, int32_t end_coord,
                                CellMaster& cell_master);
  bool isCellAvailable(PPModel& pp_model, int32_t start_coord, int32_t end_coord, int32_t y_coord);

  void insertBoundaryTap(PPModel& pp_model, int32_t boundary_tap_rule, int32_t& boundary_tap_idx);
  std::vector<std::string>& getBoundaryTapNameList(PPBoundaryType boundary_type);

  void insertEdgeEndcap(PPModel& pp_model, int32_t& endcap_idx);
  std::vector<std::string>& getEdgeEndcapNameList(PPBoundaryType boundary_type);
  std::vector<PPRegion> getEmptyPPRegionList(PPModel& pp_model, PPRegion& boundary_region);
  void fillEdgeEndcap(PPModel& pp_model, PPRegion& empty_region, std::vector<std::string>& endcap_name_list, int32_t& endcap_idx);
  std::string getFittingCellMasterName(std::vector<std::string>& cell_master_name_list, int32_t max_width, PlacementOrientation orient);
};

}  // namespace ifp
