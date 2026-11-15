#pragma once

#include <string>

namespace ifp {
// No model traversal or allocation when capture is disabled.
void beginReplayCapture();
void captureReplay(const std::string& phase, const std::string& subflow,
                   const std::string& domain = "algorithm_state");
void endReplayCapture();
}  // namespace ifp
