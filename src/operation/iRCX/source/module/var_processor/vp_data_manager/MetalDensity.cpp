#include "MetalDensity.hpp"

#include <algorithm>
#include <array>
#include <boost/polygon/polygon.hpp>
#include <cmath>
#include <cstdint>
#include <numeric>

namespace ircx {
namespace {
bool valid(const DensityRect& r)
{
  return std::isfinite(r.x_min) && std::isfinite(r.y_min) && std::isfinite(r.x_max) && std::isfinite(r.y_max) && r.x_min < r.x_max && r.y_min < r.y_max
         && std::isfinite(r.x_max - r.x_min) && std::isfinite(r.y_max - r.y_min);
}
bool intersects(const DensityRect& a, const DensityRect& b)
{
  return a.x_min < b.x_max && b.x_min < a.x_max && a.y_min < b.y_max && b.y_min < a.y_max;
}
ProcessResult<double> fraction(long double value)
{
  if (!std::isfinite(value) || value < -1e-12L || value > 1 + 1e-12L)
    return std::string("invalid metal union fraction");
  return static_cast<double>(std::clamp(value, 0.0L, 1.0L));
}

// Rank compression preserves every physical boundary exactly. Integer set
// operations avoid rounding the layout onto a grid and produce disjoint tiles
// without enumerating every occupied interval at every sweep event.
std::vector<DensityRect> rectangleUnion(std::span<const DensityRect> shapes)
{
  if (shapes.empty())
    return {};
  std::vector<double> xs, ys;
  xs.reserve(shapes.size() * 2);
  ys.reserve(shapes.size() * 2);
  for (const auto& r : shapes) {
    xs.push_back(r.x_min);
    xs.push_back(r.x_max);
    ys.push_back(r.y_min);
    ys.push_back(r.y_max);
  }
  for (auto* axis : {&xs, &ys}) {
    std::sort(axis->begin(), axis->end());
    axis->erase(std::unique(axis->begin(), axis->end()), axis->end());
  }
  auto rank = [](const auto& axis, double value) -> int64_t { return std::lower_bound(axis.begin(), axis.end(), value) - axis.begin(); };
  using Rectangle = boost::polygon::rectangle_data<int64_t>;
  boost::polygon::polygon_90_set_data<int64_t> metal;
  for (const auto& r : shapes)
    metal.insert(Rectangle(rank(xs, r.x_min), rank(ys, r.y_min), rank(xs, r.x_max), rank(ys, r.y_max)));
  std::vector<Rectangle> tiles;
  metal.get_rectangles(tiles);
  std::vector<DensityRect> result;
  result.reserve(tiles.size());
  for (const auto& r : tiles)
    result.push_back({xs[boost::polygon::xl(r)], ys[boost::polygon::yl(r)], xs[boost::polygon::xh(r)], ys[boost::polygon::yh(r)]});
  return result;
}
}  // namespace

struct MetalDensity::Index
{
  struct Node
  {
    DensityRect bounds;
    std::size_t begin, end, left = 0, right = 0;
  };
  std::vector<DensityRect> rectangles;
  std::vector<std::size_t> order;
  std::vector<Node> nodes;

  std::size_t build(std::size_t begin, std::size_t end)
  {
    auto bounds = rectangles[order[begin]];
    for (auto i = begin + 1; i < end; ++i) {
      const auto& r = rectangles[order[i]];
      bounds = {std::min(bounds.x_min, r.x_min), std::min(bounds.y_min, r.y_min), std::max(bounds.x_max, r.x_max), std::max(bounds.y_max, r.y_max)};
    }
    const auto index = nodes.size();
    nodes.push_back({bounds, begin, end});
    if (end - begin > 16) {
      const bool horizontal = bounds.x_max - bounds.x_min >= bounds.y_max - bounds.y_min;
      const auto mid = begin + (end - begin) / 2;
      std::nth_element(order.begin() + begin, order.begin() + mid, order.begin() + end, [&](auto a, auto b) {
        const auto& x = rectangles[a];
        const auto& y = rectangles[b];
        return horizontal ? x.x_min / 2 + x.x_max / 2 < y.x_min / 2 + y.x_max / 2 : x.y_min / 2 + x.y_max / 2 < y.y_min / 2 + y.y_max / 2;
      });
      const auto left = build(begin, mid), right = build(mid, end);
      nodes[index].left = left;
      nodes[index].right = right;
    }
    return index;
  }
  template <class Visitor>
  void visit(const DensityRect& window, Visitor&& visitor, std::size_t node = 0) const
  {
    if (nodes.empty() || !intersects(window, nodes[node].bounds))
      return;
    const auto& entry = nodes[node];
    if (entry.left) {
      visit(window, visitor, entry.left);
      visit(window, visitor, entry.right);
    } else
      for (auto i = entry.begin; i < entry.end; ++i) {
        const auto& r = rectangles[order[i]];
        if (intersects(window, r))
          visitor(r);
      }
  }
};

ProcessResult<MetalDensity> MetalDensity::build(std::span<const DensityRect> shapes)
{
  for (const auto& shape : shapes)
    if (!valid(shape))
      return std::string("invalid metal density rectangle");
  auto index = std::make_shared<Index>();
  index->rectangles = rectangleUnion(shapes);
  index->order.resize(index->rectangles.size());
  std::iota(index->order.begin(), index->order.end(), 0);
  if (!index->order.empty())
    index->build(0, index->order.size());
  return MetalDensity(std::move(index));
}

ProcessResult<double> MetalDensity::density(double x, double y, double window) const
{
  if (!std::isfinite(window) || window <= 0)
    return std::string("invalid density window size");
  const double half = window / 2;
  const DensityRect box{x - half, y - half, x + half, y + half};
  if (!valid(box))
    return std::string("invalid density window coordinates");
  long double area = 0;
  _index->visit(box, [&](const DensityRect& r) {
    area += (static_cast<long double>(std::min(box.x_max, r.x_max)) - std::max(box.x_min, r.x_min))
            * (static_cast<long double>(std::min(box.y_max, r.y_max)) - std::max(box.y_min, r.y_min));
  });
  return fraction(area / (static_cast<long double>(window) * window));
}

ProcessResult<std::vector<DensitySpan>> MetalDensity::profile(bool horizontal, double center, double start, double end, double window) const
{
  if (!std::isfinite(window) || window <= 0 || !std::isfinite(start) || !std::isfinite(end) || start >= end)
    return std::string("invalid density profile interval or window");
  const double half = window / 2;
  const DensityRect box
      = horizontal ? DensityRect{start - half, center - half, end + half, center + half} : DensityRect{center - half, start - half, center + half, end + half};
  if (!valid(box))
    return std::string("invalid density profile coordinates");
  const long double area = static_cast<long double>(window) * window;
  struct Event
  {
    double position;
    long double delta;
  };
  std::vector<Event> events;
  long double initial = 0, slope = 0;
  _index->visit(box, [&](const DensityRect& r) {
    const double lo = horizontal ? r.x_min : r.y_min, hi = horizontal ? r.x_max : r.y_max;
    const double bottom = horizontal ? r.y_min : r.x_min, top = horizontal ? r.y_max : r.x_max;
    const long double height = static_cast<long double>(std::min(center + half, top)) - std::max(center - half, bottom);
    initial += std::max(0.0L, static_cast<long double>(std::min(start + half, hi)) - std::max(start - half, lo)) * height / area;
    for (const auto& event :
         std::array<Event, 4>{{{lo - half, height / area}, {lo + half, -height / area}, {hi - half, -height / area}, {hi + half, height / area}}}) {
      if (event.position <= start)
        slope += event.delta;
      else if (event.position < end)
        events.push_back(event);
    }
  });
  std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
  std::vector<DensitySpan> result;
  double position = start;
  auto append = [&](double next) -> bool {
    const long double finish = initial + slope * (static_cast<long double>(next) - position);
    const auto first = fraction(initial), last = fraction(finish);
    if (!first || !last)
      return false;
    result.push_back({position, next, *first, *last});
    position = next;
    initial = finish;
    return true;
  };
  for (std::size_t i = 0; i < events.size();) {
    const double next = events[i].position;
    long double delta = 0;
    do {
      delta += events[i++].delta;
    } while (i < events.size() && events[i].position == next);
    if (delta == 0)
      continue;
    if (!append(next))
      return std::string("invalid sliding metal union fraction");
    slope += delta;
  }
  if (!append(end))
    return std::string("invalid sliding metal union fraction");
  return result;
}
}  // namespace ircx
