#include "design/routing/pool/WireRoutingInput.h"

#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace eccdb {
namespace {

uint32_t checkedPoolEnd(std::size_t value)
{
  if (value > std::numeric_limits<uint32_t>::max()) {
    throw std::overflow_error("wire routing input exceeds uint32_t");
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

void DesignWireRoutingInput::appendPath(DesignWirePath path)
{
  const auto old_path_count = path_records.size();
  const auto old_point_count = points.size();
  const auto old_via_count = vias.size();
  const auto old_rectangle_count = rectangles.size();
  const auto old_extra_count = extras.size();
  const auto has_extra = needsExtra(path);

  DesignWireInputPathRecord record{.layer = path.layer, .flags = path.flags};
  try {
    points.insert(points.end(), std::make_move_iterator(path.points.begin()), std::make_move_iterator(path.points.end()));
    vias.insert(vias.end(), std::make_move_iterator(path.vias.begin()), std::make_move_iterator(path.vias.end()));
    rectangles.insert(rectangles.end(), std::make_move_iterator(path.rectangles.begin()),
                      std::make_move_iterator(path.rectangles.end()));

    record.point_end.value = checkedPoolEnd(points.size());
    record.via_end.value = checkedPoolEnd(vias.size());
    record.rectangle_end.value = checkedPoolEnd(rectangles.size());
    if (has_extra) {
      record.extra_index = checkedPoolEnd(extras.size());
      extras.push_back(DesignWirePathExtra{.width = path.width,
                                           .mask = path.mask,
                                           .taper_rule = std::move(path.taper_rule),
                                           .shape = std::move(path.shape),
                                           .style = path.style});
    }
    path_records.push_back(record);
  } catch (...) {
    path_records.resize(old_path_count);
    points.resize(old_point_count);
    vias.resize(old_via_count);
    rectangles.resize(old_rectangle_count);
    extras.resize(old_extra_count);
    throw;
  }
}

DesignWirePathView DesignWireRoutingInput::path(std::size_t index) const
{
  if (index >= path_records.size()) {
    throw std::out_of_range("wire input path index is out of range");
  }

  const auto& record = path_records[index];
  const auto point_begin = index == 0u ? 0u : path_records[index - 1u].point_end.value;
  const auto via_begin = index == 0u ? 0u : path_records[index - 1u].via_end.value;
  const auto rectangle_begin = index == 0u ? 0u : path_records[index - 1u].rectangle_end.value;
  if (point_begin > record.point_end.value || record.point_end.value > points.size() || via_begin > record.via_end.value
      || record.via_end.value > vias.size() || rectangle_begin > record.rectangle_end.value
      || record.rectangle_end.value > rectangles.size()) {
    throw std::logic_error("wire input routing ranges are inconsistent");
  }

  const DesignWirePathExtra* extra = nullptr;
  if (record.extra_index != kInvalidDesignWirePathExtraIndex) {
    if (record.extra_index >= extras.size()) {
      throw std::logic_error("wire input path extra index is inconsistent");
    }
    extra = &extras[record.extra_index];
  }

  return DesignWirePathView::create(
      record.layer,
      record.flags,
      extra,
      DesignWirePointRange::fromValues(std::span<const DesignWirePoint>{points.data(), points.size()}.subspan(
          point_begin, record.point_end.value - point_begin)),
      DesignWireViaRange::fromValues(std::span<const DesignWireVia>{vias.data(), vias.size()}.subspan(
          via_begin, record.via_end.value - via_begin)),
      std::span<const DesignWireRectangle>{rectangles.data(), rectangles.size()}.subspan(
          rectangle_begin, record.rectangle_end.value - rectangle_begin));
}

}  // namespace eccdb
