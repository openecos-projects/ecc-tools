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
 * @file Instantiation.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-30
 * @brief CTS instantiation module entry implementation.
 */

#include "instantiation/Instantiation.hh"

#include "LogTable.hh"
#include "Logger.hh"
#include "Monitor.hh"
#include "data_manager/DataManager.hh"
#include "design/Design.hh"
#include "io/Wrapper.hh"

namespace icts {

auto Instantiation::run() -> InstantiationSummary
{
  Monitor monitor;
  CTSLOG.info(Loc::current(), "Starting CTS instantiation...");
  auto& design = CTSDM.getDesign();
  auto& wrapper = CTSDM.getWrapper();
  auto summary = InstantiationSummary{
      .design_ready = wrapper.is_design_ready(),
      .success = false,
      .clock_count = design.get_clocks().size(),
      .inserted_inst_count = 0U,
      .inserted_net_count = 0U,
      .failure_reason = "n/a",
  };
  WrapperWriteSummary write_summary;
  if (summary.design_ready) {
    write_summary = wrapper.writeClocksDetailed(design, design.get_clocks());
    summary.success = write_summary.success;
    summary.inserted_inst_count = write_summary.inserted_inst_count;
    summary.inserted_net_count = write_summary.inserted_net_count;
    if (!summary.success) {
      summary.failure_reason = write_summary.reason.empty() ? "idb_writeback_failed" : write_summary.reason;
    }
  } else {
    summary.failure_reason = "design_not_ready";
  }
  if (!summary.success) {
    CTSLOG.warn(Loc::current(), "CTS instantiation failed for clock \"", write_summary.failed_clock.empty() ? "n/a" : write_summary.failed_clock, "\", net \"",
                write_summary.failed_net.empty() ? "n/a" : write_summary.failed_net, "\": ", summary.failure_reason, ".");
  }
  if (summary.success) {
    const auto commit_status = CTSDM.commitInstantiation(summary);
    if (!commit_status.ok()) {
      summary.success = false;
      summary.failure_reason = commit_status.message;
    }
  }
  EmitLogTable(Loc::current(), "CTS Instantiation Overview", {"Property", "Value"},
               {{"Design Ready", ToLogTableCell(summary.design_ready)},
                {"Clocks", ToLogTableCell(summary.clock_count)},
                {"Write Success", ToLogTableCell(write_summary.success)},
                {"iDB Clock Tree Restored", ToLogTableCell(write_summary.idb_clock_tree_restored)},
                {"Inserted Instances", ToLogTableCell(summary.inserted_inst_count)},
                {"Inserted Nets", ToLogTableCell(summary.inserted_net_count)},
                {"Success", ToLogTableCell(summary.success)},
                {"Failed Clock", write_summary.failed_clock.empty() ? "n/a" : write_summary.failed_clock},
                {"Failed Net", write_summary.failed_net.empty() ? "n/a" : write_summary.failed_net},
                {"Reason", summary.failure_reason.empty() ? "n/a" : summary.failure_reason}});
  CTSLOG.info(Loc::current(), "Completed CTS instantiation", monitor.getStatsInfo());
  return summary;
}

}  // namespace icts
