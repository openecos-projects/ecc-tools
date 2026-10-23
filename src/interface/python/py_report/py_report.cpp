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
#include "py_report.h"

#include <report_manager.h>

#include "py_path_utils.h"

namespace python_interface {
bool reportDbSummary(const std::optional<std::filesystem::path>& path)
{
  return rptInst->reportDBSummary(path_or_empty(path));
}
bool reportWireLength(const std::optional<std::filesystem::path>& path)
{
  return rptInst->reportWL(path_or_empty(path));
}

bool reportCong(const std::optional<std::filesystem::path>& path)
{
  return rptInst->reportCongestion(path_or_empty(path));
}
bool reportDanglingNet(const std::optional<std::filesystem::path>& path)
{
  return rptInst->reportDanglingNet(path_or_empty(path));
}

bool reportRoute(const std::optional<std::filesystem::path>& path, const std::string& netname, bool summary)
{
  return rptInst->reportRoute(path_or_empty(path), netname, summary);
}

bool reportPlaceDistribution(const std::vector<std::string>& prefixes)
{
  return rptInst->reportPlaceDistribution(prefixes);
}

bool reportPrefixedInst(const std::string& prefix, int level, int num_threshold){
  return rptInst->reportInstLevel(prefix, level,  num_threshold);
}

bool reportDRC(const std::filesystem::path& filename){
  return rptInst->reportDRC(filename.string());
}
}  // namespace python_interface