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
/**
 * @file SDCClockReader.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-15
 * @brief Side-effect-free SDC clock subset reader implementation for iCTS.
 */

#include "SDCClockReader.hh"

#include <filesystem>
#include <ostream>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "Logger.hh"
#include "clock_parser/SDCClockParser.hh"
#include "clock_trace/ClockTraceResolver.hh"
#include "dm_config.h"
#include "idm.h"

namespace icts {
namespace {

auto configuredSdcPath() -> std::string
{
  return dmInst->get_config().get_sdc_path();
}

}  // namespace

SdcClockReader::SdcClockReader() : SdcClockReader(configuredSdcPath())
{
}

SdcClockReader::SdcClockReader(std::string sdc_path) : _sdc_path(std::move(sdc_path))
{
}

auto SdcClockReader::readClockData() const -> SdcClockData
{
  SdcClockData data;
  if (_sdc_path.empty()) {
    CTSLOG.warn(Loc::current(), "SdcClockReader: SDC path is empty; no clock declarations are available.");
    return data;
  }
  if (!std::filesystem::exists(_sdc_path)) {
    CTSLOG.warn(Loc::current(), "SdcClockReader: SDC file does not exist: ", _sdc_path);
    return data;
  }

  data = sdc_reader::SdcSubsetEvaluator().readFile(_sdc_path);
  for (const auto& diagnostic : data.diagnostics) {
    if (diagnostic.starts_with("ignored_sdc_command:")) {
      continue;
    }
    CTSLOG.warn(Loc::current(), "SdcClockReader: ", diagnostic);
  }
  return data;
}

auto SdcClockReader::readDeclarationsOnly() const -> std::vector<std::tuple<std::string, std::string, double, bool>>
{
  std::vector<std::tuple<std::string, std::string, double, bool>> declarations;
  const auto data = readClockData();
  declarations.reserve(data.clocks.size());
  for (const auto& clock : data.clocks) {
    declarations.emplace_back(clock.clock_name, sdc_reader::PrimarySourceExpression(clock), clock.period_ns, clock.period_resolved);
  }
  return declarations;
}

auto SdcClockReader::traceClockTargets(const SdcClockData& clock_data, idb::IdbDesign* idb_design, const SdcLibertyCellLookup& liberty_cell_lookup,
                                       std::size_t max_fanout) -> ClockTraceBuild
{
  return ClockTraceResolver::resolve(clock_data, idb_design, liberty_cell_lookup, max_fanout);
}

}  // namespace icts
