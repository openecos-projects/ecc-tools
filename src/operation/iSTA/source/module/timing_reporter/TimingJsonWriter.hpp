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

#include <fstream>
#include <string>

#include "Config.hpp"
#include "Database.hpp"
#include "ReportOptions.hpp"
#include "ReportPathContext.hpp"
#include "ReportPathSelector.hpp"

namespace ista {
class TimingJsonWriter
{
 public:
  explicit TimingJsonWriter(Database& database, const Config& config)
      : _database(database), _config(config), _report_path_context(database), _report_path_selector(database, config)
  {
  }
  void write();

 private:
  std::string getTimingPathsJsonFilePath();
  void outputTimingPathJson(std::ostream& json_file, TimingPath& timing_path, std::string& path_group_name, DelayType delay_type);
  std::string getTimingPathId(TimingPath& timing_path, std::string& path_group_name, DelayType delay_type);
  Database& _database;
  const Config& _config;
  ReportPathContext _report_path_context;
  ReportPathSelector _report_path_selector;
};
}  // namespace ista
