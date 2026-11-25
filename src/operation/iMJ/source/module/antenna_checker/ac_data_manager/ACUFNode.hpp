// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "MJHeader.hpp"

namespace imj {

// Per-pin contribution from LAYER-qualified ANTENNAGATEAREA / ANTENNADIFFAREA.
// In LEF 5.8, each pin specifies its total area visible at a layer rather than additive pieces,
// so merging multiple layers of the same pin retains the highest reached layer's total.
struct ACLayerPinContrib
{
  int gate_order = -1;
  double gate_area = 0.0;
  int diff_order = -1;
  double diff_area = 0.0;
  bool provides_diff = false;
};

class ACUFNode
{
 public:
  int parent = -1;
  int rank = 0;
  int32_t gate_node_idx = -1;
  double unqualified_gate_area = 0.0;
  double unqualified_diff_area = 0.0;
  bool unqualified_diff_connected = false;
  double qualified_gate_area = 0.0;
  double qualified_diff_area = 0.0;
  int qualified_diff_count = 0;
  std::map<int, ACLayerPinContrib> qualified_pins;
  double gate_area = 0.0;
  double diff_area = 0.0;
  bool diff_connected = false;
  double cum_area_num = 0.0;
  double cum_side_num = 0.0;
  double cum_cut_num = 0.0;
};

}  // namespace imj
