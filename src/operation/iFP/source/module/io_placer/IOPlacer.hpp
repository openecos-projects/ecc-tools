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
#include "NetPin.hpp"
#include "RoutingLayer.hpp"

#include <memory>
#include <unordered_map>

namespace ifp {

class IOCellPlacer;

#define FPIP (ifp::IOPlacer::getInst())

class IOPlacer
{
 public:
  static void initInst();
  static IOPlacer& getInst();
  static void destroyInst();
  // function
  void place();
  void placeAuto();
  bool planDieForIO();

 private:
  // self
  static IOPlacer* _ip_instance;

  IOPlacer() = default;
  IOPlacer(const IOPlacer& other) = delete;
  IOPlacer(IOPlacer&& other) = delete;
  ~IOPlacer();
  IOPlacer& operator=(const IOPlacer& other) = delete;
  IOPlacer& operator=(IOPlacer&& other) = delete;
  // function

  void placeIOCell();
  void placeIOPin();
  void resetIOPinPlacement();
  void placeIOPinsFromFile(const std::string& file_path);
  void autoPlacePins(std::vector<std::string>& layer_name_list);
  int32_t getLayerMinWidth(std::string layer_name);
  int32_t getLayerMinArea(std::string layer_name);
  int32_t getTrackPitch(std::string layer_name);
  int32_t getTrackOffset(std::string layer_name);
  int32_t calculatePinDepth(IOEdgeType edge_type, int32_t width, const std::string& pin_layer_name, const std::string& access_layer_name);
  void addIOPinPort(IOPin& io_pin, IOEdgeType edge_type, int32_t x, int32_t y, int32_t width, int32_t depth, std::string layer_name);
  void syncPinLocation(IOPin& io_pin, IOPort& io_port, int32_t x, int32_t y);
  void buildNetIOPinIndex();
  void updateNetIOPin(IOPin& io_pin);

  std::unordered_map<std::string, std::vector<NetPin*>> _io_pin_to_net_pin_map;
  // Shared across the capacity checks and the final placement so the IO cell
  // definition is parsed once per run.
  std::unique_ptr<IOCellPlacer> _io_cell_placer;
};

}  // namespace ifp
