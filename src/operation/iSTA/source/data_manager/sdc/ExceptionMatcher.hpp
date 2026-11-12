// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/AnalysisType.hpp"
#include "network/Network.hpp"
#include "sdc/TimingException.hpp"

namespace ista {
class Sdc;

// Hierarchical through selectors grouped by the net they can cross. Rebuild at
// the start of a propagation/search pass; constraints and connectivity stay
// unchanged until that pass ends. This index owns names, never model pointers.
class ExceptionThroughIndex
{
 public:
  ExceptionThroughIndex() = default;
  ExceptionThroughIndex(const Sdc& sdc, const Network& network) { rebuild(sdc, network); }
  void rebuild(const Sdc& sdc, const Network& network);
  const std::vector<std::string>& candidates(std::size_t exception, std::size_t through, const std::string& net) const;

 private:
  using NetCandidates = std::unordered_map<std::string, std::vector<std::string>>;
  std::vector<std::vector<NetCandidates>> _groups;
};

// Selector progress belongs to each path; the matcher has no mutable scratch.
// Sdc and Network must outlive the matcher and remain unchanged
// during evaluation. Resolved exception pointers borrow entries from Sdc.
class ExceptionMatcher
{
 public:
  ExceptionMatcher(const Sdc& sdc, const Network& network) : _sdc(sdc), _network(network) {}
  std::vector<int32_t> initState(std::string_view start, std::string_view clock, TransType start_trans_type, AnalysisType analysis_type,
                                 TransType launch_transition) const;
  std::vector<int> startSpecificity(std::string_view start, std::string_view clock) const;
  bool matchesEndpointSelector(const ExceptionObjects& objects, std::string_view pin, std::string_view clock, bool start) const
  {
    return matchesEndpointObjects(objects, pin, clock, start);
  }
  std::vector<int32_t> advanceState(const ExceptionThroughIndex& index, const std::vector<int32_t>& state_list, std::string_view pin_name, TransType trans_type,
                                    std::string_view source_pin = {}) const;
  std::string makePathStateTag(std::string_view clock, TransType launch_transition, const std::vector<int32_t>& exception_state_list,
                               bool clock_source_path = false, std::string_view crpr_path = {},
                               std::optional<std::size_t> launch_edge_index = std::nullopt) const;
  bool isFalsePath(std::string_view start, std::string_view launch_clock, std::string_view end, std::string_view capture_clock, AnalysisType analysis_type,
                   TransType end_trans_type, TransType capture_clock_trans_type, const std::vector<int32_t>& exception_state_list) const;
  ResolvedTimingExceptions resolve(std::string_view start, std::string_view launch_clock, std::string_view end, std::string_view capture_clock,
                                   AnalysisType analysis_type, TransType end_trans_type, TransType capture_clock_trans_type,
                                   const std::vector<int32_t>& exception_state_list) const;

 private:
  bool matchesEndpointObjects(const ExceptionObjects& objects, std::string_view pin_name, std::string_view clock_name, bool start) const;
  bool matchesThroughObjects(const ExceptionObjects& objects, std::string_view pin_name) const;
  const Sdc& _sdc;
  const Network& _network;
};
}  // namespace ista
