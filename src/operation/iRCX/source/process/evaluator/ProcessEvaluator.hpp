#pragma once

#include "ProcessConductor.hpp"
#include "ProcessVia.hpp"

namespace ircx {
// All lengths are micrometres, resistances ohms and temperatures degrees Celsius.
// Callers supply scaled drawn geometry. A negative spacing denotes no neighbour.
struct WireGeometryQuery
{
  double width;
  double lower_spacing = -1;
  double upper_spacing = -1;
};
struct EffectiveWireGeometry
{
  double width;
  double thickness;
  double center_shift = 0;
};
ProcessResult<EffectiveWireGeometry> evaluateWireGeometry(const ProcessConductor& conductor, const WireGeometryQuery& query, ProcessEffectType effect);
ProcessResult<double> evaluateWireResistance(const ProcessConductor& conductor, const WireGeometryQuery& query, double length, double temperature,
                                             double nominal_temperature);
ProcessResult<double> evaluateViaResistance(const ProcessVia& via, double width, double length, double temperature, double nominal_temperature);
}  // namespace ircx
