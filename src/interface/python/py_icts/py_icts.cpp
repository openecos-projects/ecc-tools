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
#include "py_icts.h"

#include <tool_manager.h>

namespace python_interface {
bool CtsAutoRun(const std::filesystem::path& cts_config, const std::filesystem::path& cts_work_dir)
{
  const std::string cts_config_ = cts_config.string();
  const std::string cts_work_dir_ = cts_work_dir.string();
  bool cts_run_ok = iplf::tmInst->autoRunCTS(cts_config_, cts_work_dir_);
  return cts_run_ok;
}

bool CtsReport(const std::filesystem::path& path)
{
  const std::string path_ = path.string();
  return iplf::tmInst->reportCTS(path_);
}

}  // namespace python_interface
