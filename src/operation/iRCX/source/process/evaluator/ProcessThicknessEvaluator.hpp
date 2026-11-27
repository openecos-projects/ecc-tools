#pragma once

#include "ProcessGeometry.hpp"
#include "ProcessResult.hpp"

namespace ircx {

// Width and density bases are resolved by the reader. No layout search, unit
// scaling, or etch takes place here; a missing required density is an error.
// Finite values receive only explicit bounds. Consumers validate physical
// cross sections before using the result in resistance or capacitance.
ProcessResult<double> evaluatePolynomialThickness(const ProcessThicknessPolynomial& model, double nominal_thickness, const WireGeometryQuery& drawn,
                                                  const EffectiveWireGeometry& etched);

// Sorted raw-density endpoints, extrema and clipping crossings for a fixed
// width. Additional thickness levels may be interpolation knots of a consumer.
// Every returned point has a valid thickness, including interior extrema.
ProcessResult<std::vector<double>> thicknessDensityBreakpoints(const ProcessThicknessPolynomial& model, double nominal_thickness,
                                                               const WireGeometryQuery& drawn, const EffectiveWireGeometry& etched, double density_start,
                                                               double density_end, const std::vector<double>& thickness_levels = {});

// Absolute second derivative with respect to u in [0,1], where density is
// linear between the supplied endpoints. The caller splits clipping crossings.
ProcessResult<double> thicknessCurvatureBound(const ProcessThicknessPolynomial& model, double nominal_thickness, const WireGeometryQuery& drawn,
                                              const EffectiveWireGeometry& etched, double density_start, double density_end);

// Thickness variation changes the top surface; the bottom elevation remains at
// the process stack's reference. Tangent is applied to the final thickness.
ProcessResult<EffectiveWireGeometry> finalizeWireGeometry(const ProcessConductor& conductor, const WireGeometryQuery& drawn, EffectiveWireGeometry etched,
                                                          ProcessEffectType effect);

}  // namespace ircx
