// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace imj {

// Normalized, backend-neutral antenna input. Builders expand each VIA into
// lower/cut/upper conductor nodes and explicit temporal edges. The numerical
// checker never reaches back into iDB or EccDB for geometry.
struct AntennaRect
{
  int64_t ll_x = 0;
  int64_t ll_y = 0;
  int64_t ur_x = 0;
  int64_t ur_y = 0;
};

struct AntennaShape
{
  int layer_order = -1;
  AntennaRect rectangle;
};

struct AntennaGraphNode
{
  int id = -1;
  int time = 0;
  bool is_conductor = false;
  bool is_routing = false;
  bool is_cut = false;
  bool is_side = false;
  double declared_area = 0.0;
  double gate_area = 0.0;
  double diff_area = 0.0;
  bool provides_diff = false;
  std::vector<AntennaShape> shapes;
};

struct AntennaGraphEdge
{
  int first = -1;
  int second = -1;
  int time = 0;
};

struct AntennaGraphInput
{
  std::string net_name;
  std::vector<AntennaGraphNode> nodes;
  std::vector<AntennaGraphEdge> edges;
};

}  // namespace imj
