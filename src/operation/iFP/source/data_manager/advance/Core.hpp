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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "PlanarRect.hpp"

namespace ifp {

class Core : public PlanarRect
{
 public:
  Core() = default;
  ~Core() = default;
  // getter
  std::string& get_core_site_name() { return _core_site_name; }
  std::string& get_io_site_name() { return _io_site_name; }
  std::string& get_corner_site_name() { return _corner_site_name; }

  // const getter
  const std::string& get_core_site_name() const { return _core_site_name; }
  const std::string& get_io_site_name() const { return _io_site_name; }
  const std::string& get_corner_site_name() const { return _corner_site_name; }

  // setter
  void set_core_site_name(std::string core_site_name) { _core_site_name = core_site_name; }
  void set_io_site_name(std::string io_site_name) { _io_site_name = io_site_name; }
  void set_corner_site_name(std::string corner_site_name) { _corner_site_name = corner_site_name; }

  // function

 private:
  std::string _core_site_name;
  std::string _io_site_name;
  std::string _corner_site_name;
};

}  // namespace ifp
