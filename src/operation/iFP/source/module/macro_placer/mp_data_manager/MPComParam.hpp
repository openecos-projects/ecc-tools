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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "FPHeader.hpp"

namespace ifp {

class MPComParam
{
 public:
  MPComParam() = default;
  ~MPComParam() = default;
  // getter
  double get_placement_halo_micron() const { return _placement_halo_micron; }
  double get_routing_halo_micron() const { return _routing_halo_micron; }

  // setter
  void set_placement_halo_micron(double placement_halo_micron) { _placement_halo_micron = placement_halo_micron; }
  void set_routing_halo_micron(double routing_halo_micron) { _routing_halo_micron = routing_halo_micron; }

  // function

 private:
  double _placement_halo_micron = -1.0;
  double _routing_halo_micron = -1.0;
};

}  // namespace ifp
