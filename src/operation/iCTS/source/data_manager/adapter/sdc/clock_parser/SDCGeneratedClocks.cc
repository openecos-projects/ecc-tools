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
 * @file SDCGeneratedClocks.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-05
 * @brief Resolves generated waveforms when source declarations identify their master.
 */

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "SDCClockParser.hh"

namespace icts::sdc_reader {
namespace {

auto SourceMatchesClock(const SdcClockDecl& generated, const SdcClockDecl& master) -> bool
{
  if (!generated.master_clock_name.empty()) {
    return generated.master_clock_name == master.clock_name;
  }
  return std::ranges::any_of(generated.generated_sources, [&](const auto& source) -> bool {
    if (source.kind == SdcObjectKind::kClock) {
      return ObjectPatternMatches(source.pattern, master.clock_name);
    }
    return std::ranges::any_of(master.targets, [&](const auto& target) -> bool {
      return (source.kind == target.kind || source.kind == SdcObjectKind::kUnknown || target.kind == SdcObjectKind::kUnknown)
             && (source.pattern == target.pattern || ObjectPatternMatches(source.pattern, target.pattern));
    });
  });
}

auto DeriveWaveform(SdcClockDecl& generated, const SdcClockDecl& master) -> bool
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
    // A power-of-two divider produces equal high/low intervals. Explicit
    // multiplication scales the source edges and optionally sets duty cycle.
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

auto SdcSubsetEvaluator::storeClock(SdcClockDecl clock) -> void
{
  std::erase_if(_data.clocks, [&](const auto& previous) -> bool { return previous.clock_name == clock.clock_name; });
  if (!clock.add) {
    for (auto& previous : _data.clocks) {
      if (previous.is_virtual) {
        continue;
      }
      std::erase_if(previous.targets, [&](const auto& previous_target) -> bool {
        return std::ranges::any_of(clock.targets, [&](const auto& target) -> bool {
          return (target.kind == previous_target.kind || target.kind == SdcObjectKind::kUnknown || previous_target.kind == SdcObjectKind::kUnknown)
                 && target.pattern == previous_target.pattern;
        });
      });
    }
    std::erase_if(_data.clocks, [](const auto& previous) -> bool { return !previous.is_virtual && previous.targets.empty(); });
  }
  _data.clocks.push_back(std::move(clock));
  for (auto& generated : _data.clocks) {
    if (generated.kind == SdcClockDecl::Kind::kGenerated) {
      generated.period_resolved = false;
      generated.waveform_resolved = false;
      generated.period_ns = 0.0;
      generated.waveform_ns.clear();
    }
  }
  resolveGeneratedClocks();
}

auto SdcSubsetEvaluator::resolveGeneratedClocks() -> void
{
  for (std::size_t pass = 0U; pass < _data.clocks.size(); ++pass) {
    bool progress = false;
    for (auto& generated : _data.clocks) {
      if (generated.kind != SdcClockDecl::Kind::kGenerated || generated.waveform_resolved) {
        continue;
      }
      std::vector<const SdcClockDecl*> masters;
      for (const auto& candidate : _data.clocks) {
        if (&candidate != &generated && SourceMatchesClock(generated, candidate)) {
          masters.push_back(&candidate);
        }
      }
      // Physical source pins can be downstream of any declaration. Their
      // master must be resolved on the timing graph, never guessed here.
      if (masters.size() != 1U || !masters.front()->waveform_resolved || masters.front()->waveform_ns.empty()) {
        continue;
      }
      if (!DeriveWaveform(generated, *masters.front())) {
        reportIssue(SdcConstraintStatusCode::kMalformed, "create_generated_clock", "invalid_generated_waveform:" + generated.clock_name);
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

auto SdcSubsetEvaluator::resolveGeneratedClockData(SdcClockData& data) -> void
{
  if (!data.ok()) {
    return;
  }
  SdcSubsetEvaluator evaluator;
  evaluator._data = std::move(data);
  evaluator.resolveGeneratedClocks();
  data = std::move(evaluator._data);
}

}  // namespace icts::sdc_reader

namespace icts {

auto SdcClockReader::resolveGeneratedClocks(SdcClockData& clock_data) -> void
{
  sdc_reader::SdcSubsetEvaluator::resolveGeneratedClockData(clock_data);
}

}  // namespace icts
