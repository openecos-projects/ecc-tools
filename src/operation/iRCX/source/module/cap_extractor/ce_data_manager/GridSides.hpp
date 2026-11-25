#pragma once
#include "GridNeighborhood.hpp"
namespace ircx::pattern {
struct GridSides
{
  TopoEdge* lower = nullptr;
  TopoEdge* upper = nullptr;
  double lower_spacing = 0, upper_spacing = 0;
};
inline GridSides gridSides(TopoEdge& master, const GridNeighborhood::Piece& part, double unit)
{
  GridSides out;
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
}  // namespace ircx::pattern
