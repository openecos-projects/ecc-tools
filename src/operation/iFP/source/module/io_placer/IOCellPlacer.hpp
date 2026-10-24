// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
//
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "FPHeader.hpp"
#include "IOEdgeType.hpp"
#include "Instance.hpp"
#include "PlacementOrientation.hpp"

#include <array>

namespace ifp {

class Config;

struct IOSideCapacity
{
  IOEdgeType edge = IOEdgeType::kNone;
  std::string edge_name;
  int32_t line_length = 0;
  int32_t package_span = 0;
  int32_t group_count = 0;
  int32_t power_length = 0;
  int32_t clearance = 0;
  int32_t signal_length = 0;
  int32_t free_length = 0;
  int32_t deficit = 0;
  int32_t required_line = 0;
  std::string error;
};

struct IOCapacityReport
{
  bool fits = true;
  std::string error;
  std::array<IOSideCapacity, 4> sides;
  int32_t required_die_width_dbu = -1;
  int32_t required_die_height_dbu = -1;
};

class IOCellPlacer
{
 public:
  explicit IOCellPlacer(const Config& config) : _config(config) {}
  bool place();
  IOCapacityReport checkCapacity();

 private:
  const Config& _config;
  struct IOCellPowerMember
  {
    std::string master_name;
    std::string pin_name;
    std::vector<std::string> net_patterns;
  };

  struct IOCellFillerMaster
  {
    std::string master_name;
    double width_micron = 0.0;
  };

  struct IOCellDefinition
  {
    std::string corner_master_name;
    std::vector<std::string> signal_master_names;
    std::vector<IOCellPowerMember> power_members;
    std::vector<IOCellFillerMaster> filler_masters;
  };

  struct Segment
  {
    int32_t begin = 0;
    int32_t end = 0;
  };

  struct SideLayout
  {
    int32_t corner_span = 0;
    int32_t line_begin = 0;
    int32_t line_end = 0;
    int32_t line_length = 0;
    int32_t package_span = 0;
    int32_t group_count = 0;
    int32_t clearance = 0;
    int32_t dbu_spacing = 0;
    std::vector<const IOCellPowerMember*> package;
    std::vector<int32_t> filler_widths;
    std::vector<int32_t> anchors;
    std::vector<Segment> free_segments;
    std::string error;
  };

  static bool isPowerMaster(const IOCellDefinition& definition, const std::string& master);
  static bool isSignalMaster(const IOCellDefinition& definition, const std::string& master);
  bool loadDefinition(IOCellDefinition& definition);
  bool validateDefinition(const IOCellDefinition& definition);
  bool placeAuto(const IOCellDefinition& definition);
  bool placeFromFile(const IOCellDefinition& definition);
  bool placeCorners(const IOCellDefinition& definition);
  bool placeSide(IOEdgeType edge, const IOCellDefinition& definition, const std::vector<int32_t>& signal_indices);
  std::array<std::vector<int32_t>, 4> buildSignalSideIndices(const IOCellDefinition& definition) const;
  bool buildSideLayout(IOEdgeType edge, const IOCellDefinition& definition, SideLayout& layout) const;
  bool packSignalsDryRun(IOEdgeType edge, const std::vector<int32_t>& signal_indices, const SideLayout& layout, int32_t& deficit) const;
  bool canFillGap(int32_t gap, const std::vector<int32_t>& filler_widths) const;
  int32_t requiredLineLength(int32_t signal_length, int32_t package_span, int32_t spacing, int32_t clearance, int32_t filler_gcd) const;
  int32_t fillerWidth(const IOCellFillerMaster& filler) const;
  int32_t fillerGcd(const IOCellDefinition& definition) const;
  bool normalizePowerInstanceNames(const IOCellDefinition& definition);
  bool bindIOPins(const IOCellDefinition& definition);
  bool updateIOPinPorts();
  bool placeCell(Instance& instance, IOEdgeType edge, int32_t along, PlacementOrientation orient, bool fixed = true);
  Instance* findOrCreatePowerInstance(const IOCellPowerMember& member);
  Instance* createPhysicalInstance(const std::string& name, const std::string& master_name);
  bool fillSegment(IOEdgeType edge, const Segment& segment, const IOCellDefinition& definition, int32_t& filler_index);
  std::vector<int32_t> solveFiller(int32_t gap, const std::vector<int32_t>& widths) const;
  int32_t getCellSpan(const Instance& instance, IOEdgeType edge, PlacementOrientation orient) const;
  int32_t getOrientedWidth(const Instance& instance, PlacementOrientation orient) const;
  int32_t getOrientedHeight(const Instance& instance, PlacementOrientation orient) const;
  int32_t micronToDbu(double micron) const;
  PlacementOrientation edgeOrientation(IOEdgeType edge) const;
  int32_t sideLength(IOEdgeType edge) const;
  int32_t sideStart(IOEdgeType edge) const;
  std::string edgeName(IOEdgeType edge) const;
  void resetExistingPlacement(const IOCellDefinition& definition);

  // Indices stay valid when createPhysicalInstance grows the instance vector;
  // raw pointers would dangle after the next reallocation.
  std::vector<int32_t> _power_instance_indices;
  std::set<std::string> _used_instance_names;
  int32_t _next_power_temp_id = 0;
  // Keep the instance and its terminal together; a net may also connect internal macros.
  struct PortBinding
  {
    std::string instance_name;
    std::string pin_name;
  };
  std::map<std::string, std::vector<PortBinding>> _port_bindings;
};

}  // namespace ifp
