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

#include "FPHeader.hpp"
#include "IOPin.hpp"
#include "Instance.hpp"
#include "InstancePinShape.hpp"
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
  PGNet& getPGNet(std::string net_name);

  void buildRail(PGModel& pg_model);
  void mergeRailSegmentList();
  RoutingLayer* findRoutingLayer(std::string layer_name);
  void addLineSegment(std::string net_name, std::string layer_name, PGSegmentType segment_type, int32_t width, int32_t start_x, int32_t start_y, int32_t end_x,
                      int32_t end_y);
  void addUnblockedLineSegment(std::string net_name, std::string layer_name, PGSegmentType segment_type, int32_t width, int32_t start_x, int32_t start_y,
                               int32_t end_x, int32_t end_y);
  std::vector<std::pair<int32_t, int32_t>> getMacroBlockageIntervalList(std::string layer_name, int32_t width, int32_t start_x, int32_t start_y, int32_t end_x,
                                                                        int32_t end_y);
  int32_t getMacroTopLayerOrder(Instance& instance);

  void buildStripe(PGModel& pg_model);
  void alignStripeSegmentList();
  void alignStripeSegment(PGSegment& stripe_segment);
  int32_t getClosestRailEdgeCoord(PGSegment& stripe_segment, Instance& instance, bool high_side);
  int32_t getClosestCrossStripeEdgeCoord(PGSegment& stripe_segment, Instance& instance, int32_t rail_coord, bool high_side);

  void buildLayerConnect(PGModel& pg_model);
  PlanarRect getOverlapRect(PlanarRect first_rect, PlanarRect second_rect);
  void addViaSegment(PGModel& pg_model, std::string net_name, std::string bottom_layer_name, std::string top_layer_name, std::string cut_layer_name, int32_t x,
                     int32_t y, int32_t width, int32_t height);

  void buildMacroConnect(PGModel& pg_model);
  void connectMacroPin(PGModel& pg_model, PGNet& pg_net, InstancePinShape& pin_shape);
};

}  // namespace ifp
