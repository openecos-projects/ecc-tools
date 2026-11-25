// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "common/AnalysisType.hpp"
#include "common/TransType.hpp"

namespace ista {

// Slot index for a (mode, edge) pair. Only kMax/kMin x kRise/kFall exist as
// keys; anything else (kNone) behaves as "no key", matching a std::map miss.
// Slot order is enum-ordinal — the same order the nested std::maps iterated.
inline constexpr int clockWaveSlot(AnalysisType mode, TransType edge)
{
  return (static_cast<int>(mode) - 1) * 2 + (static_cast<int>(edge) - 1);
}

inline constexpr bool clockWaveSlotValid(AnalysisType mode, TransType edge)
{
  return (mode == AnalysisType::kMax || mode == AnalysisType::kMin) && (edge == TransType::kRise || edge == TransType::kFall);
}

// Fixed 2x2 grid replacing std::map<AnalysisType, std::map<TransType, T>>.
// Presence bits reproduce key-existence semantics exactly: absent != stored
// zero, and each() walks enum-ordinal order like the map did.
template <typename T>
class ClockWaveGrid
{
 public:
  std::optional<T> get(AnalysisType mode, TransType edge) const
  {
    if (!clockWaveSlotValid(mode, edge))
      return std::nullopt;
    const int slot = clockWaveSlot(mode, edge);
    if (!(_present & (1 << slot)))
      return std::nullopt;
    return _values[slot];
  }
  T getOr(AnalysisType mode, TransType edge, T fallback) const
  {
    const auto value = get(mode, edge);
    return value ? *value : fallback;
  }
  void set(AnalysisType mode, TransType edge, const T& value)
  {
    if (!clockWaveSlotValid(mode, edge))
      return;
    const int slot = clockWaveSlot(mode, edge);
    _values[slot] = value;
    _present |= (1 << slot);
  }
  bool erase(AnalysisType mode, TransType edge)
  {
    if (!clockWaveSlotValid(mode, edge))
      return false;
    const int slot = clockWaveSlot(mode, edge);
    const bool was = _present & (1 << slot);
    _present &= ~(1 << slot);
    return was;
  }
  void clear() { _present = 0; }
  bool empty() const { return _present == 0; }
  // Iteration order is enum-ordinal: (kMax,kRise), (kMax,kFall), (kMin,kRise),
  // (kMin,kFall) — identical to the nested std::map order this replaces.
  template <typename F>
  void each(F&& f) const
  {
    for (int mode : {1, 2})
      for (int edge : {1, 2}) {
        const int slot = clockWaveSlot(static_cast<AnalysisType>(mode), static_cast<TransType>(edge));
        if (_present & (1 << slot))
          f(static_cast<AnalysisType>(mode), static_cast<TransType>(edge), _values[slot]);
      }
  }

 private:
  std::array<T, 4> _values{};
  uint8_t _present = 0;
};

// One clock at one graph point. Effective values drive timing analysis;
// physical values retain the calculated waveform and insertion trace.
struct ClockWaveform
{
  ClockWaveGrid<double> arrival_map;
  ClockWaveGrid<double> slew_map;
  ClockWaveGrid<double> physical_arrival_map;
  ClockWaveGrid<double> physical_slew_map;
  ClockWaveGrid<std::string> predecessor_map;
  ClockWaveGrid<double> predecessor_arc_delay_map;
  ClockWaveGrid<TransType> predecessor_trans_type_map;
  ClockWaveGrid<std::string> physical_predecessor_map;
  ClockWaveGrid<int32_t> predecessor_vertex_map;
  ClockWaveGrid<TransType> predecessor_source_map;
  ClockWaveGrid<double> physical_predecessor_arc_delay_map;
  ClockWaveGrid<TransType> physical_predecessor_trans_type_map;
  std::optional<double> arrival(AnalysisType analysis, TransType transition) const { return arrival_map.get(analysis, transition); }
  std::optional<double> slew(AnalysisType analysis, TransType transition) const { return slew_map.get(analysis, transition); }

  std::optional<double> physicalArrival(AnalysisType analysis, TransType transition) const { return physical_arrival_map.get(analysis, transition); }
  std::optional<double> physicalSlew(AnalysisType analysis, TransType transition) const { return physical_slew_map.get(analysis, transition); }

  TransType predecessorSource(AnalysisType mode, TransType edge, TransType source) const { return predecessor_source_map.getOr(mode, edge, source); }
  ClockWaveform physicalEdge(TransType edge) const
  {
    ClockWaveform result;
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
      auto copy = [mode, edge](auto& to, const auto& from) {
        if (const auto value = from.get(mode, edge))
          to.set(mode, edge, *value);
      };
      copy(result.physical_arrival_map, physical_arrival_map);
      copy(result.physical_slew_map, physical_slew_map);
      copy(result.physical_predecessor_map, physical_predecessor_map);
      copy(result.physical_predecessor_arc_delay_map, physical_predecessor_arc_delay_map);
      copy(result.physical_predecessor_trans_type_map, physical_predecessor_trans_type_map);
      copy(result.predecessor_vertex_map, predecessor_vertex_map);
      copy(result.predecessor_source_map, predecessor_source_map);
    }
    return result;
  }
};

// The base is the phase-independent envelope used for physical reports and DRC.
// Timing paths must select a source waveform: opposite source edges must never
// compete for a single arrival winner just because their local edge is equal.
struct ClockState : ClockWaveform
{
  bool is_source_path_only = false;
  // Structural model preparation records logical clock reachability before
  // numeric clock propagation has run.  It is consumed only by queries that
  // need legal source-edge identity, never as a numeric arrival/slew value.
  bool is_structural = false;

  const ClockWaveform* waveform(std::optional<TransType> source = std::nullopt) const
  {
    if (!source)
      return this;
    if (!clockWaveSlotValid(AnalysisType::kMax, *source))
      return nullptr;
    const int slot = static_cast<int>(*source) - 1;
    return (_waveform_present & (1 << slot)) ? &_waveforms[slot] : nullptr;
  }
  // Creates the per-source-edge waveform on first access, like map operator[].
  ClockWaveform& waveformSlot(TransType source)
  {
    const int slot = static_cast<int>(source) - 1;
    _waveform_present |= (1 << slot);
    return _waveforms[slot];
  }
  bool hasWaveforms() const { return _waveform_present != 0; }
  std::size_t waveformCount() const { return (_waveform_present & 1) + ((_waveform_present >> 1) & 1); }
  void clearWaveforms()
  {
    _waveforms = {};
    _waveform_present = 0;
  }
  // kRise then kFall — the old std::map<TransType, ClockWaveform> order.
  template <typename F>
  void eachWaveform(F&& f)
  {
    for (int edge : {1, 2})
      if (_waveform_present & (1 << (edge - 1)))
        f(static_cast<TransType>(edge), _waveforms[edge - 1]);
  }
  template <typename F>
  void eachWaveform(F&& f) const
  {
    for (int edge : {1, 2})
      if (_waveform_present & (1 << (edge - 1)))
        f(static_cast<TransType>(edge), _waveforms[edge - 1]);
  }

  void summarize()
  {
    static_cast<ClockWaveform&>(*this) = ClockWaveform{};
    eachWaveform([this](TransType source, ClockWaveform& wave) {
      for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
        for (auto edge : {TransType::kRise, TransType::kFall}) {
          auto better = [mode](double value, std::optional<double> current) { return !current || (mode == AnalysisType::kMax ? value > *current : value < *current); };
          auto copy = [mode, edge](auto& to, const auto& from) {
            if (const auto value = from.get(mode, edge))
              to.set(mode, edge, *value);
          };
          if (auto value = wave.physicalArrival(mode, edge); value && better(*value, physicalArrival(mode, edge))) {
            physical_arrival_map.set(mode, edge, *value);
            copy(physical_predecessor_map, wave.physical_predecessor_map);
            copy(physical_predecessor_arc_delay_map, wave.physical_predecessor_arc_delay_map);
            copy(physical_predecessor_trans_type_map, wave.physical_predecessor_trans_type_map);
            copy(predecessor_vertex_map, wave.predecessor_vertex_map);
            copy(predecessor_source_map, wave.predecessor_source_map);
          }
          if (auto value = wave.physicalSlew(mode, edge); value && better(*value, physicalSlew(mode, edge)))
            physical_slew_map.set(mode, edge, *value);
          if (auto value = wave.arrival(mode, edge); value && better(*value, arrival(mode, edge))) {
            arrival_map.set(mode, edge, *value);
            copy(predecessor_map, wave.predecessor_map);
            copy(predecessor_arc_delay_map, wave.predecessor_arc_delay_map);
            copy(predecessor_trans_type_map, wave.predecessor_trans_type_map);
          }
          if (auto value = wave.slew(mode, edge); value && better(*value, slew(mode, edge)))
            slew_map.set(mode, edge, *value);
        }
    });
  }

 private:
  std::array<ClockWaveform, 2> _waveforms;  // slot 0 = kRise, 1 = kFall
  uint8_t _waveform_present = 0;
};

}  // namespace ista
