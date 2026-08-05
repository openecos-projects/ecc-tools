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
#include "py_icts.h"

#include <algorithm>

#include "CTSAPI.hh"

namespace python_interface {
bool CtsAutoRun(const std::string& cts_config, const std::string& cts_work_dir)
{
  return CTS_API_INST.runCTS(cts_config, cts_work_dir).ok();
}

bool CtsReport(const std::string& path)
{
  return CTS_API_INST.report(path).ok();
}

pybind11::dict CtsTimingFeature()
{
  namespace py = pybind11;

  const auto clocks = icts::CTSAPI::outputClockTiming();
  py::dict result;
  result["schema_version"] = 1;
  result["analysis_stage"] = "cts_fast_sta_post_optimization";
  result["availability"] = clocks.empty() ? "unavailable" : "available";
  result["clock_count"] = clocks.size();

  py::list clock_list;
  double worst_optimized_skew_ns = 0.0;
  double worst_max_insertion_latency_ns = 0.0;
  std::size_t target_unmet_count = 0U;
  for (const auto& clock : clocks) {
    py::dict clock_dict;
    clock_dict["clock"] = clock.clock;
    clock_dict["sink_count"] = clock.sink_count;
    clock_dict["target_skew_ns"] = clock.target_skew_ns;
    clock_dict["initial_skew_ns"] = clock.initial_skew_ns;
    clock_dict["optimized_skew_ns"] = clock.optimized_skew_ns;
    clock_dict["min_insertion_latency_ns"] = clock.min_insertion_latency_ns;
    clock_dict["max_insertion_latency_ns"] = clock.max_insertion_latency_ns;
    clock_dict["mean_insertion_latency_ns"] = clock.mean_insertion_latency_ns;
    clock_dict["target_met"] = clock.target_met;
    clock_list.append(clock_dict);
    worst_optimized_skew_ns = std::max(worst_optimized_skew_ns, clock.optimized_skew_ns);
    worst_max_insertion_latency_ns = std::max(worst_max_insertion_latency_ns, clock.max_insertion_latency_ns);
    target_unmet_count += clock.target_met ? 0U : 1U;
  }
  result["clocks"] = clock_list;
  if (!clocks.empty()) {
    result["worst_optimized_skew_ns"] = worst_optimized_skew_ns;
    result["worst_max_insertion_latency_ns"] = worst_max_insertion_latency_ns;
    result["target_unmet_count"] = target_unmet_count;
  }
  return result;
}

}  // namespace python_interface
