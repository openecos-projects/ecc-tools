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
#include "ARReporter.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>

#include "Logger.hpp"
#include "json.hpp"

namespace imj {
namespace {

bool ensureParentDir(const std::filesystem::path& file_path, const char* kind)
{
  const std::filesystem::path parent = file_path.parent_path();
  if (parent.empty()) {
    return true;
  }
  std::error_code ec;
  std::filesystem::create_directories(parent, ec);
  if (ec) {
    MJLOG.warn(Loc::current(), "Failed to create directory for ", kind, ": ", parent.string(), " (", ec.message(), ")");
    return false;
  }
  return true;
}

}  // namespace

bool writeRepairOutputs(const ARSummary& summary, const std::string& report_path, const std::string& step_json_path)
{
  bool all_ok = true;

  if (!report_path.empty()) {
    const std::filesystem::path rpath(report_path);
    if (!ensureParentDir(rpath, "antenna repair report")) {
      all_ok = false;
    } else {
      std::ofstream stream(rpath);
      if (stream.is_open()) {
        stream << "========================================================================\n"
               << "                      Antenna Repair Report                           \n"
               << "========================================================================\n"
               << "Violations Before : " << summary.violations_before << "\n"
               << "Violations After  : " << summary.violations_after << "\n"
               << "Diodes Inserted   : " << summary.diodes_inserted << "\n"
               << "Nets Repaired     : " << summary.nets_repaired << "\n"
               << "Nets Unrepaired   : " << summary.nets_unrepaired << "\n"
               << "Iterations        : " << summary.iterations << "\n\n";
        if (!summary.placed_diodes.empty()) {
          stream << "Placed Diodes:\n"
                 << "------------------------------------------------------------------------\n";
          for (const ARPlacedDiode& d : summary.placed_diodes) {
            stream << "  " << d.instance_name << " (" << d.master_name << ") -> net " << d.net_name << " near "
                   << d.target_pin << " at (" << d.x_dbu << ", " << d.y_dbu << ")"
                   << " [wires=" << d.wire_segments_added << ", vias=" << d.vias_added << "]\n";
          }
        }
        stream.flush();
        if (!stream) {
          MJLOG.warn(Loc::current(), "Failed while flushing antenna repair report: ", report_path);
          all_ok = false;
        }
      } else {
        MJLOG.warn(Loc::current(), "Failed to open antenna repair report: ", report_path);
        all_ok = false;
      }
    }
  }

  if (!step_json_path.empty()) {
    const std::filesystem::path jpath(step_json_path);
    if (!ensureParentDir(jpath, "antenna repair step JSON")) {
      all_ok = false;
    } else {
      nlohmann::json payload = {
          {"schema_version", 1},
          {"step", "antenna_repair"},
          {"antenna_repair",
           {{"violations_before", summary.violations_before},
            {"violations_after", summary.violations_after},
            {"diodes_inserted", summary.diodes_inserted},
            {"nets_repaired", summary.nets_repaired},
            {"nets_unrepaired", summary.nets_unrepaired},
            {"iterations", summary.iterations},
            {"diode_masters_used", summary.diode_masters_used}}},
          {"placed_diodes", nlohmann::json::array()}};
      for (const ARPlacedDiode& d : summary.placed_diodes) {
        payload["placed_diodes"].push_back({
            {"instance", d.instance_name},
            {"master", d.master_name},
            {"net", d.net_name},
            {"target_pin", d.target_pin},
            {"x_dbu", d.x_dbu},
            {"y_dbu", d.y_dbu},
            {"wire_segments_added", d.wire_segments_added},
            {"vias_added", d.vias_added},
        });
      }
      std::ofstream jstream(jpath);
      if (!jstream.is_open()) {
        MJLOG.warn(Loc::current(), "Failed to open antenna repair step JSON: ", step_json_path);
        all_ok = false;
      } else {
        jstream << payload.dump(2) << '\n';
        jstream.flush();
        if (!jstream) {
          MJLOG.warn(Loc::current(), "Failed while flushing antenna repair step JSON: ", step_json_path);
          all_ok = false;
        }
      }
    }
  }

  return all_ok;
}

}  // namespace imj
