#pragma once

#include "ProcessGeometry.hpp"
#include "ProcessResult.hpp"

namespace ircx {
ProcessResult<EffectiveWireGeometry> evaluateSegmentGeometry(const ProcessConductor& conductor, const WireGeometryQuery& drawn,
                                                             const EffectiveWireSegment& segment, double position, ProcessEffectType effect);

// Validate a whole segment and partition it at thickness extrema and clipping
// boundaries. Thickness is monotone between the returned physical positions.
ProcessResult<std::vector<double>> segmentGeometryBreakpoints(const ProcessConductor& conductor, const WireGeometryQuery& drawn,
                                                              const EffectiveWireSegment& segment, ProcessEffectType effect,
                                                              const std::vector<double>& thickness_levels = {});

// Integrate resistance over a subset of the segment, in physical micrometres.
// Density clipping, thickness bounds and resistivity interpolation occur at
// the integration points, after the ordered etch snapshot has been finalized.
ProcessResult<double> integrateWireResistance(const ProcessConductor& conductor, const WireGeometryQuery& drawn, const EffectiveWireSegment& segment,
                                              double start, double end, double temperature, double nominal_temperature);
}  // namespace ircx
