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

class GBSummary
{
 public:
  GBSummary() = default;
  ~GBSummary() = default;
};

class DCSummary
{
 public:
  DCSummary() = default;
  ~DCSummary() = default;
};

class DLSummary
{
 public:
  DLSummary() = default;
  ~DLSummary() = default;
};

class CMSummary
{
 public:
  CMSummary() = default;
  ~CMSummary() = default;
};

class TASummary
{
 public:
  TASummary() = default;
  ~TASummary() = default;
  std::size_t timing_path_num = 0;
  std::size_t checked_end_point_num = 0;
  std::size_t unconstrained_end_point_num = 0;
  std::size_t violating_end_point_num = 0;
  double worst_slack = 0;
  double total_negative_slack = 0;
  std::string worst_end_point;
};

class TRSummary
{
 public:
  TRSummary() = default;
  ~TRSummary() = default;
};

class Summary
{
 public:
  Summary() = default;
  ~Summary() = default;
  DLSummary dl_summary;
  CMSummary cm_summary;
  GBSummary gb_summary;
  DCSummary dc_summary;
  TASummary ta_summary;
  TRSummary tr_summary;
};

}  // namespace ista
