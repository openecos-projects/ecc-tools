// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#pragma once

#include "EMIRHeader.hpp"

namespace iemir {

enum class EMEvalStatus { kNotEvaluated, kEvaluated, kNoRule, kInvalidResistance, kInvalidGeometry, kInvalidLimit,
                          kNonFiniteInput, kInvalidNode, kExcluded };
enum class EMRuleMatch { kNone, kExact, kNormalized, kAlias, kFallback };

inline const char* emStatusName(EMEvalStatus status)
{
  switch (status) {
    case EMEvalStatus::kEvaluated: return "EVALUATED";
    case EMEvalStatus::kNoRule: return "NO_RULE";
    case EMEvalStatus::kInvalidResistance: return "INVALID_RESISTANCE";
    case EMEvalStatus::kInvalidGeometry: return "INVALID_GEOMETRY";
    case EMEvalStatus::kInvalidLimit: return "INVALID_LIMIT";
    case EMEvalStatus::kNonFiniteInput: return "NON_FINITE_INPUT";
    case EMEvalStatus::kInvalidNode: return "INVALID_NODE";
    case EMEvalStatus::kExcluded: return "EXCLUDED";
    default: return "NOT_EVALUATED";
  }
}

inline const char* emMatchName(EMRuleMatch match)
{
  switch (match) {
    case EMRuleMatch::kExact: return "EXACT";
    case EMRuleMatch::kNormalized: return "NORMALIZED";
    case EMRuleMatch::kAlias: return "ALIAS";
    case EMRuleMatch::kFallback: return "FALLBACK";
    default: return "NONE";
  }
}

struct EMEvaluation
{
  EMEvalStatus status = EMEvalStatus::kNotEvaluated;
  EMRuleMatch match = EMRuleMatch::kNone;
  double voltage_delta_v = std::numeric_limits<double>::quiet_NaN();
  double signed_current_a = std::numeric_limits<double>::quiet_NaN();
  double effective_width_um = std::numeric_limits<double>::quiet_NaN();
};

}  // namespace iemir
