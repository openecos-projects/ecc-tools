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

#include <unordered_map>

#include "FPHeader.hpp"
#include "IOPin.hpp"
#include "Instance.hpp"
#include "InstancePinShape.hpp"
#include "PGConnectivity.hpp"
#include "PGModel.hpp"
#include "PGNet.hpp"
#include "PGSegment.hpp"
#include "PlanarCoord.hpp"
#include "PlanarRect.hpp"
#include "RoutingLayer.hpp"

namespace ifp {

#define FPPG (ifp::PDNGenerator::getInst())

class PDNGenerator
{
 public:
  static void initInst();
  static PDNGenerator& getInst();
  static void destroyInst();
  // function
  void generate();

 private:
  // self
  static PDNGenerator* _pg_instance;

  PDNGenerator() = default;
  PDNGenerator(const PDNGenerator& other) = delete;
  PDNGenerator(PDNGenerator&& other) = delete;
  ~PDNGenerator() = default;
  PDNGenerator& operator=(const PDNGenerator& other) = delete;
  PDNGenerator& operator=(PDNGenerator&& other) = delete;
  // function

  void generatePDN(PGModel& pg_model);

  void buildPGNet(PGModel& pg_model);
  void resolveIOCoreConnectDefaults(PGModel& pg_model);
  PGNet& getPGNet(std::string net_name);

  void buildRail(PGModel& pg_model);
  void mergeRailSegmentList();
  void mergeIoWireSegmentList();
  RoutingLayer* findRoutingLayer(std::string layer_name);
  void addLineSegment(std::string net_name, std::string layer_name, PGSegmentType segment_type, int32_t width, int32_t start_x, int32_t start_y, int32_t end_x,
                      int32_t end_y);
  void addUnblockedLineSegment(std::string net_name, std::string layer_name, PGSegmentType segment_type, int32_t width, int32_t start_x, int32_t start_y,
                               int32_t end_x, int32_t end_y);
  std::vector<std::pair<int32_t, int32_t>> getMacroBlockageIntervalList(std::string layer_name, int32_t width, int32_t start_x, int32_t start_y, int32_t end_x,
                                                                        int32_t end_y);
  int32_t getMacroTopLayerOrder(Instance& instance);

  void buildStripe(PGModel& pg_model);
  void addUncoveredEdgeStripes(PGModel& pg_model);
  void buildRing(PGModel& pg_model);
  void alignStripeSegmentList();
  void alignStripeSegment(PGSegment& stripe_segment);
  int32_t getClosestRailEdgeCoord(PGSegment& stripe_segment, Instance& instance, bool high_side);
  int32_t getClosestCrossStripeEdgeCoord(PGSegment& stripe_segment, Instance& instance, int32_t rail_coord, bool high_side);
  void extendStripeToRail(PGSegment& stripe_segment);
  PlanarRect getOverlapRect(PlanarRect first_rect, PlanarRect second_rect);
  void initPowerLayerPair(PGModel& pg_model);
  void buildLayerConnect(PGModel& pg_model);
  bool addViaSegment(PGModel& pg_model, std::string net_name, std::string bottom_layer_name, std::string top_layer_name, std::string cut_layer_name, int32_t x,
                     int32_t y, int32_t width, int32_t height);

  void buildIOCoreConnect(PGModel& pg_model);
  void buildMacroConnect(PGModel& pg_model);
  void mergeOverlappingViaStacks();
  void pruneDeadEndStripes();
  void trimStripeEnds();
  void trimDanglingFollowPins();

  void repairPGConnectivity(PGModel& pg_model);
  std::vector<const PGSegment*> getIsolatedRailList(const std::string& net_name);
  bool isRailConnectedToStripe(const PGSegment& rail);
  bool isLayerPairConfigured(const std::string& first_layer_name, const std::string& second_layer_name) const;
  std::string getRepairStripeLayerName();
  std::string getRepairTopLayerName(const std::string& repair_layer_name);
  int32_t getRepairStripeWidth(const std::string& layer_name);
  bool isRepairSegmentLegal(const std::string& net_name, const std::string& layer_name, int32_t width, int32_t x, int32_t y1, int32_t y2);
  bool findRepairTopStripeY(const std::string& net_name, const std::string& top_layer_name, int32_t x, int32_t y1, int32_t y2, int32_t& top_y);
  void validatePGConnectivity(PGModel& pg_model);
  PGConnectivity buildPGConnectivity();

#if 1  // output
  void outputPowerLocation(PGModel& pg_model);
#endif

  std::vector<Instance*> _placed_macro_list;
  std::unordered_map<const Instance*, int32_t> _macro_top_layer_order;
  int32_t _rejected_via_count = 0;
  std::map<std::pair<std::string, std::string>, int32_t> _grown_via_counts;
};

}  // namespace ifp
