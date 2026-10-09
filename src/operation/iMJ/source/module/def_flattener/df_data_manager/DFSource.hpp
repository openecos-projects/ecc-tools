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

#include "DFDieArea.hpp"
#include "builder.h"

namespace imj {

class DFSource
{
 public:
  DFSource() = default;
  ~DFSource() = default;
  DFSource(const DFSource& other) = delete;
  DFSource(DFSource&& other) = default;
  DFSource& operator=(const DFSource& other) = delete;
  DFSource& operator=(DFSource&& other) = default;
  // getter
  std::string& get_master_name() { return _master_name; }
  std::string& get_def_path() { return _def_path; }
  std::vector<std::string>& get_pin_name_list() { return _pin_name_list; }
  std::unique_ptr<idb::IdbBuilder>& get_idb_builder() { return _idb_builder; }
  idb::IdbDefService* get_def_service() { return _idb_builder == nullptr ? nullptr : _idb_builder->get_def_service(); }
  idb::IdbDesign* get_design()
  {
    idb::IdbDefService* def_service = get_def_service();
    return def_service == nullptr ? nullptr : def_service->get_design();
  }
  idb::IdbLayout* get_layout()
  {
    idb::IdbDefService* def_service = get_def_service();
    return def_service == nullptr ? nullptr : def_service->get_layout();
  }
  DFDieArea& get_die_area() { return _die_area; }
  // const getter
  const std::string& get_master_name() const { return _master_name; }
  const std::string& get_def_path() const { return _def_path; }
  const std::vector<std::string>& get_pin_name_list() const { return _pin_name_list; }
  // setter
  void set_master_name(const std::string& master_name) { _master_name = master_name; }
  void set_def_path(const std::string& def_path) { _def_path = def_path; }
  void set_pin_name_list(const std::vector<std::string>& pin_name_list) { _pin_name_list = pin_name_list; }
  void set_idb_builder(std::unique_ptr<idb::IdbBuilder> idb_builder) { _idb_builder = std::move(idb_builder); }
  void set_die_area(const DFDieArea& die_area) { _die_area = die_area; }
  // function
  void clear_pin_name_list() { _pin_name_list.clear(); }
  void add_pin_name(const std::string& pin_name) { _pin_name_list.push_back(pin_name); }

 private:
  std::string _master_name;
  std::string _def_path;
  std::vector<std::string> _pin_name_list;
  std::unique_ptr<idb::IdbBuilder> _idb_builder;
  DFDieArea _die_area;
};

}  // namespace imj
