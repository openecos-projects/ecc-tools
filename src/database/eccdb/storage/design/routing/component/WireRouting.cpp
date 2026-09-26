#include "design/routing/component/WireRouting.h"

#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace eccdb {
namespace {

uint32_t checkedSize(std::size_t value)
{
  if (value > std::numeric_limits<uint32_t>::max()) {
    throw std::overflow_error("wire routing vector exceeds uint32_t");
  }
  return static_cast<uint32_t>(value);
}

bool needsExtra(const DesignWirePath& path)
{
  constexpr uint32_t kValueFlags = DesignWirePathFlag::kHasWidth | DesignWirePathFlag::kHasMask
                                   | DesignWirePathFlag::kHasTaperRule | DesignWirePathFlag::kHasShape
                                   | DesignWirePathFlag::kHasStyle;
  return (path.flags & kValueFlags) != 0u || path.width != 0 || path.mask != 0u || !path.taper_rule.empty()
         || !path.shape.empty() || path.style != 0;
}

}  // namespace

void DesignWireRouting::appendPath(DesignWirePath path)
{
  const auto old_path_count = paths.size();
  const auto old_point_count = points.size();
  const auto old_via_count = vias.size();
  const auto old_rectangle_count = rectangles.size();
  const auto old_extra_count = extras.size();
  const auto has_extra = needsExtra(path);

  DesignWirePathRecord record{.layer = path.layer,
                              .flags = path.flags,
                              .points = {.begin = checkedSize(old_point_count)},
                              .vias = {.begin = checkedSize(old_via_count)},
                              .rectangles = {.begin = checkedSize(old_rectangle_count)}};
  try {
    points.insert(points.end(), std::make_move_iterator(path.points.begin()), std::make_move_iterator(path.points.end()));
    vias.insert(vias.end(), std::make_move_iterator(path.vias.begin()), std::make_move_iterator(path.vias.end()));
    rectangles.insert(rectangles.end(), std::make_move_iterator(path.rectangles.begin()),
                      std::make_move_iterator(path.rectangles.end()));

    record.points.count = checkedSize(points.size() - old_point_count);
    record.vias.count = checkedSize(vias.size() - old_via_count);
    record.rectangles.count = checkedSize(rectangles.size() - old_rectangle_count);
    if (has_extra) {
      record.extra_index = checkedSize(extras.size());
      extras.push_back(DesignWirePathExtra{.width = path.width,
                                           .mask = path.mask,
                                           .taper_rule = std::move(path.taper_rule),
                                           .shape = std::move(path.shape),
                                           .style = path.style});
    }
    paths.push_back(record);
  } catch (...) {
    paths.resize(old_path_count);
    points.resize(old_point_count);
    vias.resize(old_via_count);
    rectangles.resize(old_rectangle_count);
    extras.resize(old_extra_count);
    throw;
  }
}

void DesignWireRouting::shrinkToFit()
{
  paths.shrink_to_fit();
  points.shrink_to_fit();
  vias.shrink_to_fit();
  rectangles.shrink_to_fit();
  extras.shrink_to_fit();
}

DesignWirePathView DesignWireRouting::path(std::size_t index) const
{
  if (index >= paths.size()) {
    throw std::out_of_range("wire path index is out of range");
  }

  const auto& record = paths[index];
  const auto validRange = [](DesignWireRoutingRange range, std::size_t size) {
    return range.begin <= size && range.count <= size - range.begin;
  };
  if (!validRange(record.points, points.size()) || !validRange(record.vias, vias.size())
      || !validRange(record.rectangles, rectangles.size())) {
    throw std::logic_error("wire routing ranges are inconsistent");
  }

  const DesignWirePathExtra* extra = nullptr;
  if (record.extra_index != kInvalidDesignWirePathExtraIndex) {
    if (record.extra_index >= extras.size()) {
      throw std::logic_error("wire path extra index is inconsistent");
    }
    extra = &extras[record.extra_index];
  }

  return DesignWirePathView::create(
      record.layer,
      record.flags,
      extra,
      DesignWirePointRange::fromValues(std::span<const DesignWirePoint>{points}.subspan(record.points.begin, record.points.count)),
      DesignWireViaRange::fromValues(std::span<const DesignWireVia>{vias}.subspan(record.vias.begin, record.vias.count)),
      std::span<const DesignWireRectangle>{rectangles}.subspan(record.rectangles.begin, record.rectangles.count));
}

}  // namespace eccdb
