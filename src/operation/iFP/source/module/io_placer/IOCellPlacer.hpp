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

namespace ifp {

class IOCellPlacer
{
 public:
  bool place();

 private:
  struct IOCellPowerMember
  {
    // Logical role used to derive a stable instance-name prefix.  The role is
    // technology data (for example VDD1/VSSIO3), while the electrical net is
    // still taken from net_patterns below.
    std::string role;
    std::string master_name;
    std::string pin_name;
    std::vector<std::string> net_patterns;
  };

  struct IOCellPowerGroup
  {
    std::string name;
    std::vector<IOCellPowerMember> members;
    int32_t min_groups_per_side = 1;
    double max_spacing_micron = 1000.0;
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
    std::vector<IOCellPowerGroup> power_groups;
    std::vector<IOCellFillerMaster> filler_masters;
  };

  struct Segment
  {
    int32_t begin = 0;
    int32_t end = 0;
  };

  static bool isPowerMaster(const IOCellDefinition& definition, const std::string& master);
  static bool isSignalMaster(const IOCellDefinition& definition, const std::string& master);
  bool loadDefinition(IOCellDefinition& definition);
  bool validateDefinition(const IOCellDefinition& definition);
  bool placeAuto(const IOCellDefinition& definition);
  bool placeFromFile(const IOCellDefinition& definition);
  bool placeCorners(const IOCellDefinition& definition);
  bool placeSide(IOEdgeType edge, const IOCellDefinition& definition, const std::vector<int32_t>& signal_indices);
  bool normalizePowerInstanceNames(const IOCellDefinition& definition);
  bool bindIOPins(const IOCellDefinition& definition);
  bool updateIOPinPorts();
  bool placeCell(Instance& instance, IOEdgeType edge, int32_t along, PlacementOrientation orient, bool fixed = true);
  Instance* findOrCreatePowerInstance(const IOCellPowerMember& member);
  Instance* createPhysicalInstance(const std::string& name, const std::string& master_name);
  bool fillSegment(IOEdgeType edge, const Segment& segment, const IOCellDefinition& definition, int32_t& filler_index);
  std::vector<int32_t> solveFiller(int32_t gap, const std::vector<int32_t>& widths) const;
  int32_t getCellSpan(const Instance& instance) const;
  int32_t getOrientedWidth(const Instance& instance, PlacementOrientation orient) const;
  int32_t getOrientedHeight(const Instance& instance, PlacementOrientation orient) const;
  int32_t micronToDbu(double micron) const;
  PlacementOrientation edgeOrientation(IOEdgeType edge) const;
  int32_t sideLength(IOEdgeType edge) const;
  int32_t sideStart(IOEdgeType edge) const;
  std::string edgeName(IOEdgeType edge) const;
  void resetExistingPlacement(const IOCellDefinition& definition);

  std::vector<Instance*> _power_instances;
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
