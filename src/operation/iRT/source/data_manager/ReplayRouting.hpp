#pragma once
#include <map>
#include <string>
#include <vector>

#include "EXTLayerRect.hpp"
#include "LayerCoord.hpp"
#include "Segment.hpp"

namespace irt {
// Called after worker results are joined; never reads a concurrently-mutating RTDM.
void captureReplayRouting(const std::string& phase, const std::map<int32_t, std::vector<Segment<LayerCoord>>>& routes,
                          const std::map<int32_t, std::vector<EXTLayerRect>>& patches, int iteration = 0, int violations = -1);
}  // namespace irt
