#pragma once
#include "ReplayCapture.h"

namespace ircx::replay {
inline void phase(const char* name)
{
  ecc::replay::guarded([&] { ecc::replay::sample(name, "run rcx", nlohmann::json::object(), "patch", "analysis_state"); });
}
}  // namespace ircx::replay
