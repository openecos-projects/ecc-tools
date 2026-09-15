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

#include "STAHeader.hpp"

namespace ista {

class TimingException
{
 public:
  TimingException() = default;
  ~TimingException() = default;
  // getter
  const std::set<std::string>& get_from_objects() const { return _from_objects; }
  const std::set<std::string>& get_to_objects() const { return _to_objects; }
  bool get_setup() const { return _setup; }
  bool get_hold() const { return _hold; }
  // setter
  void set_from_objects(const std::set<std::string>& objects) { _from_objects = objects; }
  void set_to_objects(const std::set<std::string>& objects) { _to_objects = objects; }
  void set_setup(bool value) { _setup = value; }
  void set_hold(bool value) { _hold = value; }
  // function

 private:
  std::set<std::string> _from_objects;
  std::set<std::string> _to_objects;
  bool _setup = true;
  bool _hold = true;
};

class TimingClockGroup
{
 public:
  TimingClockGroup() = default;
  ~TimingClockGroup() = default;
  // getter
  const std::vector<std::set<std::string>>& get_groups() const { return _groups; }
  bool get_allow_paths() const { return _allow_paths; }
  // setter
  void set_groups(const std::vector<std::set<std::string>>& groups) { _groups = groups; }
  void set_allow_paths(bool value) { _allow_paths = value; }
  // function

 private:
  std::vector<std::set<std::string>> _groups;
  bool _allow_paths = false;
};

}  // namespace ista
