#pragma once

#include <span>
#include <string>

#include "ProcessEffectType.hpp"

namespace ircx {

// A readable declaration can require a model unavailable to the evaluator.
struct ProcessEvaluationIssue
{
  std::string field;
  ProcessEffectType effect = ProcessEffectType::kBoth;
  std::string message;
};

inline const ProcessEvaluationIssue* findEvaluationIssue(std::span<const ProcessEvaluationIssue> issues, ProcessEffectType effect)
{
  for (const auto& issue : issues)
    if (issue.effect == ProcessEffectType::kBoth || issue.effect == effect)
      return &issue;
  return nullptr;
}

}  // namespace ircx
