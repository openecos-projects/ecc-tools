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
 * @file SDCGeneratedClockResolver.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-10-28
 * @brief Derives unresolved generated clocks without parsing SDC text.
 */

#include "SDCGeneratedClockResolver.hh"

#include <fnmatch.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "timing/TimingConstraints.hh"

namespace icts {
namespace {

auto objectPatternMatches(const std::string& pattern, const std::string& name) -> bool
{
  return pattern == name || fnmatch(pattern.c_str(), name.c_str(), 0) == 0;
}

auto sourceMatchesClock(const SdcClockDecl& generated, const SdcClockDecl& master) -> bool
{
  if (!generated.master_clock_name.empty()) {
    return generated.master_clock_name == master.clock_name;
  }
  return std::ranges::any_of(generated.generated_sources, [&](const auto& source) -> bool {
    if (source.kind == SdcObjectKind::kClock) {
      return objectPatternMatches(source.pattern, master.clock_name);
    }
    return std::ranges::any_of(master.targets, [&](const auto& target) -> bool {
      return (source.kind == target.kind || source.kind == SdcObjectKind::kUnknown || target.kind == SdcObjectKind::kUnknown)
             && (source.pattern == target.pattern || objectPatternMatches(source.pattern, target.pattern));
    });
  });
}

auto deriveWaveform(SdcClockDecl& generated, const SdcClockDecl& master) -> bool
{
  if (master.waveform_ns.size() < 2U || master.waveform_ns.size() % 2U != 0U || !std::isfinite(master.period_ns) || master.period_ns <= 0.0
      || generated.divide_by < 1 || generated.multiply_by < 1) {
    return false;
  }
  if (!generated.generated_edges.empty()) {
    if (generated.generated_edges.size() != 3U || generated.generated_edges.front() < 1 || !std::ranges::is_sorted(generated.generated_edges)
        || std::ranges::adjacent_find(generated.generated_edges) != generated.generated_edges.end()
        || (!generated.generated_edge_shifts_ns.empty() && generated.generated_edge_shifts_ns.size() != generated.generated_edges.size())) {
      return false;
    }
    std::vector<double> times;
    for (std::size_t index = 0U; index < generated.generated_edges.size(); ++index) {
      const auto edge = static_cast<std::size_t>(generated.generated_edges[index] - 1);
      const auto cycle = edge / master.waveform_ns.size();
      double time = master.waveform_ns[edge % master.waveform_ns.size()] + static_cast<double>(cycle) * master.period_ns;
      if (!generated.generated_edge_shifts_ns.empty()) {
        time += generated.generated_edge_shifts_ns[index];
      }
      times.push_back(time);
    }
    generated.period_ns = times[2] - times[0];
    generated.waveform_ns = {times[0], times[1]};
  } else {
    const double scale = static_cast<double>(generated.divide_by) / static_cast<double>(generated.multiply_by);
    generated.period_ns = master.period_ns * scale;
    const bool divider = generated.divide_by_explicit && generated.divide_by > 1 && (generated.divide_by & (generated.divide_by - 1)) == 0;
    if (divider) {
      generated.waveform_ns = {master.waveform_ns.front(), master.waveform_ns.front() + generated.period_ns / 2.0};
    } else if (generated.duty_cycle_percent) {
      const double first = master.waveform_ns.front() * scale;
      generated.waveform_ns = {first, first + generated.period_ns * *generated.duty_cycle_percent / 100.0};
    } else {
      generated.waveform_ns.clear();
      for (const double edge : master.waveform_ns) {
        generated.waveform_ns.push_back(edge * scale);
      }
    }
  }
  if (!(generated.period_ns > 0.0) || !std::isfinite(generated.period_ns)
      || std::ranges::any_of(generated.waveform_ns, [](double edge) -> bool { return !std::isfinite(edge); }) || !std::ranges::is_sorted(generated.waveform_ns)
      || generated.waveform_ns.back() >= generated.waveform_ns.front() + generated.period_ns) {
    return false;
  }
  if (generated.invert) {
    const double first = generated.waveform_ns.front();
    const double offset = first >= generated.period_ns ? generated.period_ns : 0.0;
    std::ranges::rotate(generated.waveform_ns, generated.waveform_ns.begin() + 1);
    generated.waveform_ns.back() = first + generated.period_ns;
    for (double& edge : generated.waveform_ns) {
      edge -= offset;
    }
  }
  return true;
}

}  // namespace

auto SDCGeneratedClockResolver::resolve(SdcClockData& data) -> void
{
  if (!data.ok()) {
    return;
  }
  for (std::size_t pass = 0U; pass < data.clocks.size(); ++pass) {
    bool progress = false;
    for (auto& generated : data.clocks) {
      if (generated.kind != SdcClockDecl::Kind::kGenerated || generated.waveform_resolved) {
        continue;
      }
      std::vector<const SdcClockDecl*> masters;
      for (const auto& candidate : data.clocks) {
        if (&candidate != &generated && sourceMatchesClock(generated, candidate)) {
          masters.push_back(&candidate);
        }
      }
      // Physical sources need their unique master identified on the timing
      // graph. Do not guess a master from an ambiguous declaration match.
      if (masters.size() != 1U || !masters.front()->waveform_resolved || masters.front()->waveform_ns.empty()) {
        continue;
      }
      if (!deriveWaveform(generated, *masters.front())) {
        const std::string detail = "invalid_generated_waveform:" + generated.clock_name;
        data.issues.push_back({SdcConstraintStatusCode::kMalformed, "create_generated_clock", detail});
        data.diagnostics.push_back("create_generated_clock:" + detail);
        data.status = SdcConstraintStatusCode::kMalformed;
        return;
      }
      generated.period_resolved = true;
      generated.waveform_resolved = true;
      progress = true;
    }
    if (!progress) {
      return;
    }
  }
}

}  // namespace icts
