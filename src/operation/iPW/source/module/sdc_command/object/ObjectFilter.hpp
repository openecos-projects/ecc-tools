// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You may obtain a copy of the License at:
// http://license.coscl.org.cn/MulanPSL2
//
// ***************************************************************************************
#pragma once

#include <map>
#include <string>

namespace ipw::sdc {

// Evaluate the SDC -filter expression against attributes collected for one object.
bool matchesFilter(const std::string& expression, const std::map<std::string, std::string>& attributes, bool regexp = false, bool nocase = false);

}  // namespace ipw::sdc
