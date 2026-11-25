#pragma once

#include <cstddef>
#include <limits>
#include <vector>

#include "ProcessConductor.hpp"

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
inline constexpr std::size_t kNoWireNeighbor = std::numeric_limits<std::size_t>::max();
struct WireGeometrySpan
{
  double start;
  double end;
  std::size_t lower_neighbor = kNoWireNeighbor;
  std::size_t upper_neighbor = kNoWireNeighbor;
};
struct WireGeometryInput
{
  const ProcessConductor* conductor;
  double center;
  double width;
  std::vector<WireGeometrySpan> spans;
};
struct EffectiveWireSegment
{
  std::size_t source_span;
  double start;
  double end;
  EffectiveWireGeometry geometry;
  double lower_spacing = -1;
  double upper_spacing = -1;
};
using WireGeometryProfiles = std::vector<std::vector<EffectiveWireSegment>>;

}  // namespace ircx
