// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "ACAntennaRule.hpp"
#include "ACGraphNode.hpp"
#include "ACUFNode.hpp"
#include "ACViolation.hpp"
#include "IdbGeometry.h"

namespace izh {

class AntennaGeometry
{
 public:
  static void unionArea(const std::vector<idb::IdbRect>& rects, int micron_dbu, double& area_um);
  static void unionAreaPerimeter(const std::vector<idb::IdbRect>& rects, int micron_dbu, double& area_um, double& perimeter_um);
  static void evaluateCut(const ACAntennaRule& cut_rule, const ACUFNode& root_data, double cut_area_um, bool diff_active, int micron_dbu,
                          bool has_bbox, int64_t bbox_lx, int64_t bbox_ly, int64_t bbox_hx, int64_t bbox_hy, const std::string& net_name,
                          const std::string& pin_name, const std::string& inst_name, int layer_order, double gate_area, double diff_area,
                          double metal_area, std::vector<ACViolation>& out_violations, double& cum_cut_num);
  static void evaluateRouting(const ACAntennaRule& routing_rule, const ACUFNode& root_data, double metal_area_um, double side_area_um,
                              bool diff_active, int micron_dbu, bool has_bbox, int64_t bbox_lx, int64_t bbox_ly, int64_t bbox_hx,
                              int64_t bbox_hy, const std::string& net_name, const std::string& pin_name, const std::string& inst_name,
                              int layer_order, double gate_area, double diff_area, double cut_area, std::vector<ACViolation>& out_violations,
                              double& cum_area_num, double& cum_side_num);
};

}  // namespace izh
