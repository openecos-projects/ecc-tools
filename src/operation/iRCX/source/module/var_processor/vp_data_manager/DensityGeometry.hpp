#pragma once

#include "MetalDensity.hpp"
#include "ProcessGeometry.hpp"

namespace ircx {
// Attach exact density functions to the etch snapshot without rerunning etch.
// A missing required snapshot remains a checked process-evaluation error.
ProcessResult<std::vector<EffectiveWireSegment>> finalizeDensityGeometry(const WireGeometryInput& wire, std::span<const EffectiveWireSegment> etched,
                                                                         bool horizontal, const MetalDensity* drawn, const MetalDensity* fabricated,
                                                                         ProcessEffectType effect);
}  // namespace ircx
