// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "AnalysisType.hpp"
#include "TransType.hpp"

namespace ipw {
enum class DerateDelay
{
  kCell,
  kNet,
  kCheck,
  kPeriod,
  kWidth
};
enum class DerateObject
{
  kDesign,
  kHierarchy,
  kLibCell,
  kCell,
  kNet
};

struct DerateContext
{
  std::string_view instance;
  std::string lib_cell;
  std::string_view net;
};

// Factors belong to path consumption, not to nominal delay/slew caches.
class TimingDerates
{
 public:
  using Key = std::tuple<AnalysisType, bool, TransType, DerateDelay, DerateObject, std::string, bool>;
  void set(
      AnalysisType analysis,
      bool clock_path,
      TransType edge,
      DerateDelay kind,
      double factor,
      DerateObject object = DerateObject::kDesign,
      std::string name = {},
      bool increment = false
  )
  {
    _factors[{analysis, clock_path, edge, kind, object, std::move(name), increment}] = factor;
  }
  double factor(
      AnalysisType analysis,
      bool clock_path,
      TransType edge,
      DerateDelay kind,
      const DerateContext& context = {}
  ) const
  {
    // Baseline and increment have independent scope precedence. A later broad
    // assignment cannot erase a narrower assignment of either component.
    auto resolve = [&](bool increment) {
      auto find = [&](DerateObject object, std::string_view name) -> std::optional<double> {
        const auto found = _factors.find({analysis, clock_path, edge, kind, object, std::string(name), increment});
        return found == _factors.end() ? std::nullopt : std::optional<double>(found->second);
      };
      if (kind == DerateDelay::kNet) {
        if (auto value = find(DerateObject::kNet, context.net))
          return *value;
      } else {
        if (auto value = find(DerateObject::kCell, context.instance))
          return *value;
        if (auto value = find(DerateObject::kLibCell, context.lib_cell))
          return *value;
      }
      auto enclosed = kind == DerateDelay::kNet ? context.net : context.instance;
      for (auto slash = enclosed.rfind('/'); slash != std::string_view::npos; slash = enclosed.rfind('/')) {
        enclosed = enclosed.substr(0, slash);
        if (auto value = find(DerateObject::kHierarchy, enclosed))
          return *value;
      }
      return find(DerateObject::kDesign, {}).value_or(increment ? 0. : 1.);
    };
    return resolve(false) + resolve(true);
  }
  double apply(
      double nominal,
      AnalysisType analysis,
      bool clock_path,
      TransType edge,
      DerateDelay kind,
      const DerateContext& context = {}
  ) const
  {
    const double scale = factor(analysis, clock_path, edge, kind, context);
    // Keep an unconstrained delay bit-for-bit unchanged. Negative delays use
    // an absolute variation, so a late factor cannot make them earlier.
    return scale == 1.0 ? nominal : nominal + (scale - 1.0) * std::abs(nominal);
  }
  const std::map<Key, double>& entries() const { return _factors; }

 private:
  std::map<Key, double> _factors;
};
}  // namespace ipw
