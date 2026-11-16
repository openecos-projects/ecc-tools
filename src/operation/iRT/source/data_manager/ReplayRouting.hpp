#pragma once
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "EXTLayerRect.hpp"
#include "LayerCoord.hpp"
#include "Segment.hpp"
#include "Violation.hpp"
#include "json.hpp"

namespace irt {
// Called after worker results are joined; never reads a concurrently-mutating RTDM.
void captureReplayRouting(std::string_view phase, const std::map<int32_t, std::vector<Segment<LayerCoord>>>& routes,
                          const std::map<int32_t, std::vector<EXTLayerRect>>& patches, int iteration = 0, int violations = -1,
                          const std::vector<Violation>* violation_shapes = nullptr, nlohmann::json extras = nullptr);
// Serialize read-only router shapes. Call only inside replay::guarded.
nlohmann::json replayRoutingObjects(const std::map<int32_t, std::vector<Segment<LayerCoord>>>& routes,
                                    const std::map<int32_t, std::vector<EXTLayerRect>>& patches);
void appendReplayRegion(nlohmann::json& objects, const std::string& id, const PlanarRect& area, bool active);
void appendReplayViolations(nlohmann::json& objects, const std::vector<Violation>& violations);
}  // namespace irt
