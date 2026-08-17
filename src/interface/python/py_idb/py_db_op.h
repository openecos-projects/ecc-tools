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

#include <algorithm>

#include <idm.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace python_interface {
bool setNet(const std::string& net_name, const std::string& type)
{
  return dmInst->setNetType(net_name, type);
}

template <typename T>
bool write_placement_back(pybind11::array_t<T, pybind11::array::c_style | pybind11::array::forcecast> const& x,
                          pybind11::array_t<T, pybind11::array::c_style | pybind11::array::forcecast> const& y)
{
  // assume all the movable nodes are in front of fixed nodes
  // this is ensured by sortNodeByPlaceStatus()
  auto len = std::min(x.size(), y.size());
  dmInst->write_placement_back(x.data(), y.data(), static_cast<int>(len));

  return true;
}


}  // namespace python_interface
