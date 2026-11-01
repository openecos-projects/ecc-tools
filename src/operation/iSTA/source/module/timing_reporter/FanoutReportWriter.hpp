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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once
#include "Config.hpp"
#include "Database.hpp"
#include "QorSummaryBuilder.hpp"
#include "ReportOptions.hpp"

namespace ista {
class FanoutReportWriter
{
 public:
  explicit FanoutReportWriter(Database& database, const Config& config) : _database(database), _config(config), _qor_summary_builder(database) {}
  void write();

 private:
  Database& _database;
  const Config& _config;
  QorSummaryBuilder _qor_summary_builder;
};
}  // namespace ista
