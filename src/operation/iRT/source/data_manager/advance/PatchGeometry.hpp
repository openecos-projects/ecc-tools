#pragma once

#include "Utility.hpp"

namespace irt {

class PatchGeometry
{
 public:
  static PlanarRect getCuttingRect(const std::vector<GTLRectInt>& rect_list, Direction direction)
  {
    if (rect_list.empty() || (direction != Direction::kHorizontal && direction != Direction::kVertical)) {
      RTLOG.error(Loc::current(), "Invalid patch cutting rectangles or direction!");
    }
    GTLRectInt best_rect = rect_list.front();
    int32_t max_span = 0;
    for (const GTLRectInt& rect : rect_list) {
      int32_t span = direction == Direction::kHorizontal ? std::abs(gtl::xl(rect) - gtl::xh(rect)) : std::abs(gtl::yl(rect) - gtl::yh(rect));
      if (max_span <= span) {
        max_span = span;
        best_rect = rect;
      }
    }
    return Utility::convertToPlanarRect(best_rect);
  }

  static std::vector<int32_t> getSampleCoordList(int32_t start_coord, int32_t end_coord, int32_t manufacture_grid, int32_t sample_step, bool is_initial_sample)
  {
    if (manufacture_grid <= 0 || sample_step <= 0) {
      RTLOG.error(Loc::current(), "Invalid patch sample grid or step!");
    }
    if (end_coord < start_coord) {
      return {};
    }
    int64_t position_num = (static_cast<int64_t>(end_coord) - start_coord) / manufacture_grid + 1;
    int64_t sample_begin = is_initial_sample ? 0 : sample_step;
    int64_t sample_interval = is_initial_sample ? sample_step : 2LL * sample_step;
    std::vector<int32_t> coord_list;
    for (int64_t position_idx = sample_begin; position_idx < position_num; position_idx += sample_interval) {
      if (!is_initial_sample && position_idx == position_num - 1) {
        continue;
      }
      coord_list.push_back(static_cast<int32_t>(start_coord + position_idx * manufacture_grid));
    }
    if (is_initial_sample && (position_num - 1) % sample_step != 0) {
      coord_list.push_back(end_coord);
    }
    return coord_list;
  }

  static bool enlargeToMinArea(const GTLPolyInt& poly, int32_t min_area, int32_t manufacture_grid, const PlanarRect& die_rect, Orientation orientation,
                               PlanarRect& patch_rect)
  {
    if (manufacture_grid <= 0 || patch_rect.getXSpan() <= 0 || patch_rect.getYSpan() <= 0
        || (orientation != Orientation::kEast && orientation != Orientation::kWest && orientation != Orientation::kNorth
            && orientation != Orientation::kSouth)) {
      RTLOG.error(Loc::current(), "Invalid compact patch geometry, grid or orientation!");
    }
    int64_t poly_area = static_cast<int64_t>(gtl::area(poly));
    while (Utility::isInside(die_rect, patch_rect)) {
      int64_t patch_area = static_cast<int64_t>(patch_rect.getXSpan()) * patch_rect.getYSpan();
      int64_t overlap_area = static_cast<int64_t>(gtl::area(poly & Utility::convertToGTLRectInt(patch_rect)));
      int64_t curr_area = poly_area + patch_area - overlap_area;
      if (min_area <= curr_area) {
        return true;
      }
      int32_t cross_span = orientation == Orientation::kEast || orientation == Orientation::kWest ? patch_rect.getYSpan() : patch_rect.getXSpan();
      int64_t missing_area = min_area - curr_area;
      int64_t grow_span = (missing_area + cross_span - 1) / cross_span;
      grow_span = ((grow_span + manufacture_grid - 1) / manufacture_grid) * manufacture_grid;
      if (orientation == Orientation::kEast) {
        if (grow_span > static_cast<int64_t>(die_rect.get_ur_x()) - patch_rect.get_ur_x()) {
          return false;
        }
        patch_rect.set_ur_x(static_cast<int32_t>(patch_rect.get_ur_x() + grow_span));
      } else if (orientation == Orientation::kWest) {
        if (grow_span > static_cast<int64_t>(patch_rect.get_ll_x()) - die_rect.get_ll_x()) {
          return false;
        }
        patch_rect.set_ll_x(static_cast<int32_t>(patch_rect.get_ll_x() - grow_span));
      } else if (orientation == Orientation::kNorth) {
        if (grow_span > static_cast<int64_t>(die_rect.get_ur_y()) - patch_rect.get_ur_y()) {
          return false;
        }
        patch_rect.set_ur_y(static_cast<int32_t>(patch_rect.get_ur_y() + grow_span));
      } else {
        if (grow_span > static_cast<int64_t>(patch_rect.get_ll_y()) - die_rect.get_ll_y()) {
          return false;
        }
        patch_rect.set_ll_y(static_cast<int32_t>(patch_rect.get_ll_y() - grow_span));
      }
    }
    return false;
  }
};

}
