#pragma once
#include "MultiNeighborCache.hpp"
namespace ircx::pattern {
struct FallbackSides
{
  TopoEdge* lower = nullptr;
  TopoEdge* upper = nullptr;
  double lower_spacing = 0, upper_spacing = 0;
};
inline FallbackSides fallbackSides(TopoEdge& master, const MultiNeighborCache::Piece& part, double unit)
{
  FallbackSides out;
  out.lower = part.nleft ? part.neighbors.front() : nullptr;
  out.upper = part.neighbors.size() > part.nleft ? part.neighbors[part.nleft] : nullptr;
  bool h = master.get_line_segment().get_is_horizontal();
  auto low = [&](TopoEdge& e) { return h ? boost::polygon::yl(e.get_shape()) : boost::polygon::xl(e.get_shape()); };
  auto high = [&](TopoEdge& e) { return h ? boost::polygon::yh(e.get_shape()) : boost::polygon::xh(e.get_shape()); };
  if (out.lower)
    out.lower_spacing = unit * (low(master) - high(*out.lower));
  if (out.upper)
    out.upper_spacing = unit * (low(*out.upper) - high(master));
  return out;
}
inline double fallbackCouplingWeight(const TopoEdge& neighbor, bool two_sides)
{
  return neighbor.get_is_special_net() && two_sides ? .5 : 1.;
}
inline bool sameSignalNet(const TopoEdge& neighbor, int32_t master_net)
{
  return !neighbor.get_is_special_net() && neighbor.get_net_idx() == master_net;
}
}  // namespace ircx::pattern
