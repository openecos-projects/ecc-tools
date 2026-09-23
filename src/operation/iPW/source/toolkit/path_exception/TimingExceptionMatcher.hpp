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

#include "AnalysisType.hpp"
#include "PWHeader.hpp"
#include "TimingException.hpp"
#include "TransType.hpp"

namespace ipw {

class Database;

// Tracks selector progress during propagation and resolves endpoint exceptions.
class TimingExceptionMatcher
{
 public:
  static std::vector<int32_t> initState(Database& database, std::string_view start, std::string_view clock, TransType start_trans_type,
                                        AnalysisType analysis_type);
  static std::vector<int32_t> advanceState(Database& database, const std::vector<int32_t>& state_list, std::string_view pin_name,
                                           TransType trans_type);
  static std::string makePathStateTag(Database& database, std::string_view start, std::string_view clock,
                                      const std::vector<int32_t>& exception_state_list);
  static bool isFalsePath(Database& database, std::string_view start, std::string_view launch_clock, std::string_view end,
                          std::string_view capture_clock, AnalysisType analysis_type, TransType end_trans_type,
                          TransType capture_clock_trans_type, const std::vector<int32_t>& exception_state_list);
  static ResolvedTimingExceptions resolve(Database& database, std::string_view start, std::string_view launch_clock, std::string_view end,
                                           std::string_view capture_clock, AnalysisType analysis_type, TransType end_trans_type,
                                           TransType capture_clock_trans_type, const std::vector<int32_t>& exception_state_list);

 private:
  static bool matchesEndpointObjects(Database& database, const std::set<std::string>& objects, std::string_view pin_name,
                                     std::string_view clock_name, bool start);
  static bool matchesThroughObjects(Database& database, const std::set<std::string>& objects, std::string_view pin_name);
};

}  // namespace ipw
