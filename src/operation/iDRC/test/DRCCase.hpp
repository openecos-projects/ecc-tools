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

#include <map>
#include <set>
#include <string>
#include <vector>

#include "DRCShape.hpp"
#include "Database.hpp"
#include "Violation.hpp"
#include "ViolationType.hpp"

namespace idrc {

// A minimal DRC problem: the technology the rule validator reads, the shapes it checks and the
// violations it must report. The case format is described in README.md in this directory.
class DRCCase
{
 public:
  std::string case_path;
  Database database;
  std::vector<DRCShape> env_shape_list;
  std::vector<DRCShape> result_shape_list;
  std::set<ViolationType> check_type_set;
  // canonical violation lines, sorted
  std::vector<std::string> expected_violation_list;
  std::vector<int32_t> vary_pitch_list;
  std::vector<std::string> routing_layer_name_list;
  std::vector<std::string> cut_layer_name_list;
  std::vector<std::string> net_name_list;
  std::map<std::string, int32_t> net_name_to_idx_map;
};

DRCCase parseDRCCase(const std::string& case_path);

// One line per violation, in the same syntax as an `expect` statement without the keyword.
std::string formatViolation(const DRCCase& drc_case, const Violation& violation);

std::vector<std::string> formatViolationList(const DRCCase& drc_case, const std::vector<Violation>& violation_list);

}  // namespace idrc
