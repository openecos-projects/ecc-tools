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
 * @file CompareMath.hh
 * @brief compare_spef implementation detail.
 */
#pragma once

#include "RCXHeader.hpp"
#include "Types.hh"

namespace ircx {
namespace compare_spef {
namespace math {

inline constexpr F64 kEpsilon = 1e-12;

inline auto roundToSignificantDigitsImpl(F64 value, int digits, F64 epsilon) -> F64
{
  if (std::abs(value) <= epsilon || !std::isfinite(value)) {
    return value;
  }

  const F64 scale = std::pow(10.0, static_cast<F64>(digits - 1) - std::floor(std::log10(std::abs(value))));
  return std::round(value * scale) / scale;
}

inline auto roundToSignificantDigitsHalfEvenImpl(F64 value, int digits, F64 epsilon) -> F64
{
  if (std::abs(value) <= epsilon || !std::isfinite(value)) {
    return value;
  }

  const F64 scale = std::pow(10.0, static_cast<F64>(digits - 1) - std::floor(std::log10(std::abs(value))));
  const F64 scaled_value = value * scale;
  const F64 lower = std::floor(scaled_value);
  const F64 fraction = scaled_value - lower;
  if (std::abs(fraction - 0.5) <= 1e-9) {
    return (std::fmod(lower, 2.0) == 0.0 ? lower : lower + 1.0) / scale;
  }
  return std::round(scaled_value) / scale;
}

inline auto absoluteRelativeDelta(F64 test, F64 reference) -> std::optional<F64>
{
  if (std::abs(reference) <= kEpsilon) {
    return std::nullopt;
  }
  return (test - reference) / reference;
}

inline auto roundToSignificantDigits(F64 value, int digits = 6) -> F64
{
  return roundToSignificantDigitsImpl(value, digits, kEpsilon);
}

inline auto roundToSignificantDigitsHalfEven(F64 value, int digits = 6) -> F64
{
  return roundToSignificantDigitsHalfEvenImpl(value, digits, kEpsilon);
}

inline auto capacitanceRelativeDelta(F64 test, F64 reference) -> std::optional<F64>
{
  return absoluteRelativeDelta(roundToSignificantDigits(test), roundToSignificantDigits(reference));
}

inline auto couplingRelativeDelta(F64 test, F64 reference, F64 denominator) -> std::optional<F64>
{
  const F64 rounded_denominator = roundToSignificantDigits(denominator);
  if (std::abs(rounded_denominator) <= kEpsilon) {
    return std::nullopt;
  }
  return (roundToSignificantDigitsHalfEven(test) - roundToSignificantDigitsHalfEven(reference)) / rounded_denominator;
}

}  // namespace math
}  // namespace compare_spef
}  // namespace ircx
