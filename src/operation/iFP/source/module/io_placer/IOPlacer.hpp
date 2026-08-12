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

#include "Core.hpp"
#include "IOEdgeType.hpp"
#include "IOPin.hpp"
#include "RoutingLayer.hpp"

namespace ifp {

#define FPIP (ifp::IOPlacer::getInst())

class IOPlacer
{
 public:
  static void initInst();
  static IOPlacer& getInst();
  static void destroyInst();
  // function
  void place();

 private:
  // self
  static IOPlacer* _ip_instance;

  IOPlacer() = default;
  IOPlacer(const IOPlacer& other) = delete;
  IOPlacer(IOPlacer&& other) = delete;
  ~IOPlacer() = default;
  IOPlacer& operator=(const IOPlacer& other) = delete;
  IOPlacer& operator=(IOPlacer&& other) = delete;
  // function

  void placeIOPin();
  void autoPlacePins(std::vector<std::string>& layer_name_list, int32_t width, int32_t depth);
  int32_t getTrackPitch(std::string layer_name);
  void placeIOPinsOnEdge(IOEdgeType edge_type, std::vector<IOPin>& io_pin_list, int32_t& io_pin_idx, int32_t edge_pin_num,
                         std::string horizontal_layer_name, std::string vertical_layer_name, int32_t width, int32_t depth,
                         int32_t horizontal_pitch, int32_t vertical_pitch, int32_t manufacture_grid);
  int32_t getAlongCoord(int32_t range_low, int32_t range_high, int32_t die_low, int32_t die_high, int32_t pin_span, int32_t access_pitch,
                        int32_t side_pin_num, int32_t pin_idx, int32_t manufacture_grid);
  void addIOPinPort(IOPin& io_pin, int32_t x, int32_t y, int32_t rect_width, int32_t rect_height, int32_t manufacture_grid,
                    std::string layer_name);
  void syncPinLocation(IOPin& io_pin, IOPort& io_port, int32_t x, int32_t y);
  void updateNetIOPin(IOPin& io_pin);
};

}  // namespace ifp
