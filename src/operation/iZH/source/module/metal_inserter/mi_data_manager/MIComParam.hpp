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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "ZHHeader.hpp"

namespace izh {

class MIComParam
{
 public:
  MIComParam() = default;
  MIComParam(double density_window_size_micron, double density_window_step_micron, double min_density, double max_density)
      : _density_window_size_micron(density_window_size_micron),
        _density_window_step_micron(density_window_step_micron),
        _min_density(min_density),
        _max_density(max_density)
  {
  }
  ~MIComParam() = default;
  // getter
  double get_density_window_size_micron() const { return _density_window_size_micron; }
  double get_density_window_step_micron() const { return _density_window_step_micron; }
  double get_min_density() const { return _min_density; }
  double get_max_density() const { return _max_density; }
  // setter
  void set_density_window_size_micron(double density_window_size_micron) { _density_window_size_micron = density_window_size_micron; }
  void set_density_window_step_micron(double density_window_step_micron) { _density_window_step_micron = density_window_step_micron; }
  void set_min_density(double min_density) { _min_density = min_density; }
  void set_max_density(double max_density) { _max_density = max_density; }

 private:
  double _density_window_size_micron = 0.0;
  double _density_window_step_micron = 0.0;
  double _min_density = 0.0;
  double _max_density = 0.0;
};

}  // namespace izh
