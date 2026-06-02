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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <optional>
#include <sstream>
#include <source_location>
#include <string>
#include <utility>

#include "log/Log.hh"
#include "usage/usage.hh"

namespace ircx {

template <typename... Args>
inline void logStageInfo(const std::source_location& location, const Args&... args)
{
  std::ostringstream stream;
  (stream << ... << args);
  google::LogMessage(location.file_name(), static_cast<int>(location.line()), google::GLOG_INFO).stream() << stream.str();
}

class StageLog
{
 public:
  explicit StageLog(std::string stage, std::source_location location = std::source_location::current())
      : stage_(std::move(stage)), location_(location)
  {
    logStageInfo(location_, stage_, " begin.");
  }
  ~StageLog() { logStageInfo(location_, stage_, " end: ", (success_ ? "success" : "failed"), "."); }

  StageLog(const StageLog&) = delete;
  StageLog& operator=(const StageLog&) = delete;

  void set_success(bool success = true) { success_ = success; }

 private:
  std::string stage_;
  std::source_location location_;
  bool success_{false};
};

struct StageLogOptions
{
  bool profile{false};
};

template <typename Func>
auto runStage(std::string stage, Func&& func, StageLogOptions options = {},
              std::source_location location = std::source_location::current()) -> bool
{
  std::optional<ieda::Stats> stats;
  if (options.profile) {
    stats.emplace();
  }

  StageLog stage_log(stage, location);
  const bool success = std::forward<Func>(func)();
  stage_log.set_success(success);
  if (stats.has_value()) {
    logStageInfo(location, "  - memory usage: ", stats->memoryDelta(), "MB");
    logStageInfo(location, "  - time elapsed: ", stats->elapsedRunTime(), "s");
  }
  return success;
}

}  // namespace ircx
