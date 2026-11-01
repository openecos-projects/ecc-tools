// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace ista {

enum class TimingPhase : std::size_t
{
  kStructuralPreparation,
  kClockNetwork,
  kClockPropagation,
  kDataSlew,
  kDataArrival,
  kRequired,
  kPathAnalysis,
  kDelayGraphInitialization,
  kCount
};

// Session-local observations at the actual phase entries. Times are wall
// durations and can be nested; they must not be summed as exclusive costs.
class TimingExecutionStats
{
 public:
  struct Metric
  {
    int64_t calls = 0;
    double seconds = 0;
  };
  class Scope
  {
   public:
    explicit Scope(Metric& metric) : _metric(metric), _start(std::chrono::steady_clock::now()) { ++_metric.calls; }
    ~Scope() { _metric.seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - _start).count(); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

   private:
    Metric& _metric;
    std::chrono::steady_clock::time_point _start;
  };

  Scope measure(TimingPhase phase) { return Scope(_values[static_cast<std::size_t>(phase)]); }
  const auto& values() const { return _values; }
  static constexpr std::array<std::string_view, static_cast<std::size_t>(TimingPhase::kCount)> names{
      "structural_preparation", "clock_network", "clock_propagation", "data_slew", "data_arrival", "required", "path_analysis", "delay_graph_initialization"};

 private:
  std::array<Metric, static_cast<std::size_t>(TimingPhase::kCount)> _values{};
};

}  // namespace ista
