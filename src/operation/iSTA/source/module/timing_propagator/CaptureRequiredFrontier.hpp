// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <iterator>
#include <list>
#include <map>
#include <string_view>
#include <tuple>

#include "TimingTolerance.hpp"
#include "path/PathEnd.hpp"

namespace ista::detail {
struct CaptureRequired
{
  const PathCheck* check;
  double required = 0.;  // Required without CPPR, less the downstream data delay.
  double bound = 0.;
  double downstream_delay = 0.;
};

// Preserve sequential dominance and tie ordering, without scanning every
// surviving capture for each insertion. Checks must outlive the frontier.
class CaptureRequiredFrontier
{
  struct DominatedAbove
  {
    double upper;
  };
  struct RequiredLess
  {
    using is_transparent = void;
    bool operator()(double left, double right) const { return left < right; }
    bool operator()(DominatedAbove left, double right) const
    {
      // Keep the original subtraction: moving the tolerance across the
      // comparison changes rounding at its boundary. This is monotone in right.
      return left.upper < right - 4. * kTimingTolerance;
    }
  };
  struct Entry;
  using Values = std::list<Entry>;
  using RequiredIndex = std::multimap<double, Values::iterator, RequiredLess>;
  using UpperIndex = std::multimap<double, Values::iterator>;
  struct Entry : CaptureRequired
  {
    RequiredIndex::iterator required_entry;
    UpperIndex::iterator upper_entry;
  };
  using Identity = std::tuple<std::string_view, std::string_view, TransType, TransType, bool, std::uint64_t>;

 public:
  // Expose only the value: construction iterators become invalid when the
  // indexes are released and must never be copied by a range-for caller.
  class const_iterator
  {
   public:
    using value_type = CaptureRequired;
    using reference = const CaptureRequired&;
    using pointer = const CaptureRequired*;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    const_iterator() = default;
    explicit const_iterator(Values::const_iterator value) : _value(value) {}
    reference operator*() const { return *_value; }
    pointer operator->() const { return &*_value; }
    const_iterator& operator++() { ++_value; return *this; }
    const_iterator operator++(int) { auto previous = *this; ++*this; return previous; }
    bool operator==(const const_iterator&) const = default;

   private:
    Values::const_iterator _value;
  };
  CaptureRequiredFrontier() = default;
  CaptureRequiredFrontier(const CaptureRequiredFrontier&) = delete;
  CaptureRequiredFrontier& operator=(const CaptureRequiredFrontier&) = delete;
  CaptureRequiredFrontier(CaptureRequiredFrontier&&) = default;
  CaptureRequiredFrontier& operator=(CaptureRequiredFrontier&&) = default;
  const_iterator begin() const { return const_iterator(_values.begin()); }
  const_iterator end() const { return const_iterator(_values.end()); }

  // Once this vertex is complete, predecessors only iterate its candidates.
  // Release construction indexes instead of retaining three trees per vertex.
  void releaseIndexes()
  {
    _linear = true;
    _identities.clear();
    _required.clear();
    _upper.clear();
  }

  void insert(CaptureRequired candidate)
  {
    const double upper = candidate.required + candidate.bound;
    if (!_linear && (std::isnan(candidate.required) || std::isnan(upper))) {
      // NaNs cannot be ordered and may permit multiple entries per identity.
      // Retain the original comparisons for this exceptional frontier.
      releaseIndexes();
    }
    if (_linear) {
      for (const auto& current : _values)
        if ((identity(current) == identity(candidate) && current.required <= candidate.required)
            || current.required + current.bound < candidate.required - 4. * kTimingTolerance)
          return;
      std::erase_if(_values, [&](const auto& current) {
        return (identity(current) == identity(candidate) && candidate.required < current.required)
               || upper < current.required - 4. * kTimingTolerance;
      });
      _values.push_back({candidate, {}, {}});
      return;
    }
    auto same = _identities.find(identity(candidate));
    if ((same != _identities.end() && same->second->required <= candidate.required)
        || (!_upper.empty() && _upper.begin()->first < candidate.required - 4. * kTimingTolerance))
      return;
    if (same != _identities.end())
      erase(same->second);
    auto dominated = _required.upper_bound(DominatedAbove{upper});
    while (dominated != _required.end()) {
      const auto victim = dominated++->second;
      erase(victim);
    }
    auto value = _values.insert(_values.end(), {candidate, {}, {}});
    value->required_entry = _required.emplace(candidate.required, value);
    value->upper_entry = _upper.emplace(upper, value);
    _identities.emplace(identity(candidate), value);
  }

 private:
  static Identity identity(const CaptureRequired& value)
  {
    return {value.check->capture_pin, value.check->capture_clock, value.check->capture_pin_transition, value.check->capture_transition,
            value.check->has_required_adjustment, std::bit_cast<std::uint64_t>(value.check->required_adjustment)};
  }
  void erase(Values::iterator value)
  {
    _identities.erase(identity(*value));
    _required.erase(value->required_entry);
    _upper.erase(value->upper_entry);
    _values.erase(value);
  }
  Values _values;
  std::map<Identity, Values::iterator> _identities;
  RequiredIndex _required;
  UpperIndex _upper;
  bool _linear = false;
};
}  // namespace ista::detail
