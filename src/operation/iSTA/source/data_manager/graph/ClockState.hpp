// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "common/AnalysisType.hpp"
#include "common/TransType.hpp"

namespace ista {
// One clock at one graph point. Effective values drive timing analysis;
// physical values retain the calculated waveform and insertion trace.
struct ClockWaveform
{
  std::map<AnalysisType, std::map<TransType, double>> arrival_map;
  std::map<AnalysisType, std::map<TransType, double>> slew_map;
  std::map<AnalysisType, std::map<TransType, double>> physical_arrival_map;
  std::map<AnalysisType, std::map<TransType, double>> physical_slew_map;
  std::map<AnalysisType, std::map<TransType, std::string>> predecessor_map;
  std::map<AnalysisType, std::map<TransType, double>> predecessor_arc_delay_map;
  std::map<AnalysisType, std::map<TransType, TransType>> predecessor_trans_type_map;
  std::map<AnalysisType, std::map<TransType, std::string>> physical_predecessor_map;
  std::map<AnalysisType, std::map<TransType, int32_t>> predecessor_vertex_map;
  std::map<AnalysisType, std::map<TransType, TransType>> predecessor_source_map;
  std::map<AnalysisType, std::map<TransType, double>> physical_predecessor_arc_delay_map;
  std::map<AnalysisType, std::map<TransType, TransType>> physical_predecessor_trans_type_map;
  std::optional<double> arrival(AnalysisType analysis, TransType transition) const { return findValue(arrival_map, analysis, transition); }
  std::optional<double> slew(AnalysisType analysis, TransType transition) const { return findValue(slew_map, analysis, transition); }

  std::optional<double> physicalArrival(AnalysisType analysis, TransType transition) const { return findValue(physical_arrival_map, analysis, transition); }
  std::optional<double> physicalSlew(AnalysisType analysis, TransType transition) const { return findValue(physical_slew_map, analysis, transition); }

  TransType predecessorSource(AnalysisType mode, TransType edge, TransType source) const
  {
    const auto m = predecessor_source_map.find(mode);
    return m != predecessor_source_map.end() && m->second.contains(edge) ? m->second.at(edge) : source;
  }
  ClockWaveform physicalEdge(TransType edge) const
  {
    ClockWaveform result;
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
      auto copy = [mode, edge](auto& to, const auto& from) {
        const auto m = from.find(mode);
        if (m != from.end() && m->second.contains(edge))
          to[mode][edge] = m->second.at(edge);
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

 private:
  static std::optional<double> findValue(const std::map<AnalysisType, std::map<TransType, double>>& values, AnalysisType analysis, TransType transition)
  {
    const auto mode = values.find(analysis);
    if (mode == values.end())
      return std::nullopt;
    const auto value = mode->second.find(transition);
    return value == mode->second.end() ? std::nullopt : std::optional<double>(value->second);
  }
};

// The base is the phase-independent envelope used for physical reports and DRC.
// Timing paths must select a source waveform: opposite source edges must never
// compete for a single arrival winner just because their local edge is equal.
struct ClockState : ClockWaveform
{
  bool is_source_path_only = false;
  std::map<TransType, ClockWaveform> waveforms;

  const ClockWaveform* waveform(std::optional<TransType> source = std::nullopt) const
  {
    if (!source)
      return this;
    const auto found = waveforms.find(*source);
    return found == waveforms.end() ? nullptr : &found->second;
  }

  void summarize()
  {
    static_cast<ClockWaveform&>(*this) = ClockWaveform{};
    for (const auto& [source, wave] : waveforms)
      for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
        for (auto edge : {TransType::kRise, TransType::kFall}) {
          auto better
              = [mode](double value, std::optional<double> current) { return !current || (mode == AnalysisType::kMax ? value > *current : value < *current); };
          auto copy = [mode, edge](auto& to, const auto& from) {
            const auto m = from.find(mode);
            if (m != from.end() && m->second.contains(edge))
              to[mode][edge] = m->second.at(edge);
          };
          if (auto value = wave.physicalArrival(mode, edge); value && better(*value, physicalArrival(mode, edge))) {
            physical_arrival_map[mode][edge] = *value;
            copy(physical_predecessor_map, wave.physical_predecessor_map);
            copy(physical_predecessor_arc_delay_map, wave.physical_predecessor_arc_delay_map);
            copy(physical_predecessor_trans_type_map, wave.physical_predecessor_trans_type_map);
            copy(predecessor_vertex_map, wave.predecessor_vertex_map);
            copy(predecessor_source_map, wave.predecessor_source_map);
          }
          if (auto value = wave.physicalSlew(mode, edge); value && better(*value, physicalSlew(mode, edge)))
            physical_slew_map[mode][edge] = *value;
          if (auto value = wave.arrival(mode, edge); value && better(*value, arrival(mode, edge))) {
            arrival_map[mode][edge] = *value;
            copy(predecessor_map, wave.predecessor_map);
            copy(predecessor_arc_delay_map, wave.predecessor_arc_delay_map);
            copy(predecessor_trans_type_map, wave.predecessor_trans_type_map);
          }
          if (auto value = wave.slew(mode, edge); value && better(*value, slew(mode, edge)))
            slew_map[mode][edge] = *value;
        }
  }
};

}  // namespace ista
