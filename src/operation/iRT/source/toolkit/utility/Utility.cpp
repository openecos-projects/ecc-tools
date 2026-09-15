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
#include "Utility.hpp"

#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace irt {

// public

void Utility::initInst()
{
  if (_util_instance == nullptr) {
    _util_instance = new Utility();
  }
}

Utility& Utility::getInst()
{
  if (_util_instance == nullptr) {
    initInst();
  }
  return *_util_instance;
}

void Utility::destroyInst()
{
  if (_util_instance != nullptr) {
    delete _util_instance;
    _util_instance = nullptr;
  }
}

std::string Utility::getCurrentRSS()
{
  std::ifstream statm_file("/proc/self/statm");
  long virtual_page_num = 0;
  long resident_page_num = 0;
  if (statm_file.is_open()) {
    statm_file >> virtual_page_num >> resident_page_num;
  }
  double rss = static_cast<double>(resident_page_num * sysconf(_SC_PAGESIZE)) / 1000000.0;
  return getString(formatByTwoDecimalPlaces(rss), "MB");
}

void Utility::releaseMemory(const std::string& stage)
{
#if defined(__GLIBC__)
  std::string rss_before = getCurrentRSS();
  malloc_trim(0);
  RTLOG.info(Loc::current(), "Memory trim after ", stage, " (rss_before = ", rss_before, ", rss_after = ", getCurrentRSS(), ")");
#endif
}

// private

Utility* Utility::_util_instance = nullptr;

}  // namespace irt
