// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "MetalFillKernel.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace imj {

void MetalFillKernel::build(MIModel& mi_model, const MetalFillSource& source)
{
  for (auto& mi_layer : mi_model.get_mi_layer_list()) {
    buildLayer(mi_model, mi_layer, source);
  }
}

double MetalFillKernel::unionArea(const std::vector<MIRect>& rectangles)
{
  std::vector<int32_t> x_coordinates;
  x_coordinates.reserve(rectangles.size() * 2u);
  for (const auto& rectangle : rectangles) {
    if (!rectangle.is_valid()) {
      continue;
    }
    x_coordinates.push_back(rectangle.get_ll_x());
    x_coordinates.push_back(rectangle.get_ur_x());
  }
  std::sort(x_coordinates.begin(), x_coordinates.end());
  x_coordinates.erase(std::unique(x_coordinates.begin(), x_coordinates.end()), x_coordinates.end());

  double result = 0.0;
  for (std::size_t x_index = 0; x_index + 1u < x_coordinates.size(); ++x_index) {
    const auto lower_x = x_coordinates[x_index];
    const auto upper_x = x_coordinates[x_index + 1u];
    std::vector<std::pair<int32_t, int32_t>> y_intervals;
    for (const auto& rectangle : rectangles) {
      if (rectangle.get_ll_x() < upper_x && lower_x < rectangle.get_ur_x()) {
        y_intervals.emplace_back(rectangle.get_ll_y(), rectangle.get_ur_y());
      }
    }
    std::sort(y_intervals.begin(), y_intervals.end());
    int32_t lower_y = 0;
    int32_t upper_y = 0;
    for (const auto& [begin, end] : y_intervals) {
      if (lower_y == upper_y) {
        lower_y = begin;
        upper_y = end;
      } else if (begin <= upper_y) {
        upper_y = std::max(upper_y, end);
      } else {
        result += static_cast<double>(upper_x - lower_x) * static_cast<double>(upper_y - lower_y);
        lower_y = begin;
        upper_y = end;
      }
    }
    result += static_cast<double>(upper_x - lower_x) * static_cast<double>(upper_y - lower_y);
  }
  return result;
}

void MetalFillKernel::buildLayer(MIModel& mi_model, MILayer& mi_layer, const MetalFillSource& source)
{
  buildDensityWindows(mi_model, mi_layer, source);
  buildLayerFill(mi_model, mi_layer, source);
}

void MetalFillKernel::buildDensityWindows(MIModel& mi_model, MILayer& mi_layer, const MetalFillSource& source)
{
  const auto window_size = static_cast<int32_t>(std::llround(mi_model.get_mi_com_param().get_density_window_size_micron()
                                                              * mi_model.get_micron_dbu()));
  const auto window_step = static_cast<int32_t>(std::llround(mi_model.get_mi_com_param().get_density_window_step_micron()
                                                              * mi_model.get_micron_dbu()));
  if (window_size <= 0 || window_step <= 0) {
    throw std::invalid_argument("metal fill density window is invalid");
  }

  const auto& die = mi_model.get_die();
  std::vector<MIDensityWindow> windows;
  int32_t y_count = 0;
  for (int32_t y = die.get_ll_y(); y < die.get_ur_y(); y += window_step) {
    int32_t x_count = 0;
    for (int32_t x = die.get_ll_x(); x < die.get_ur_x(); x += window_step) {
      const MIRect window{x, y, std::min(x + window_size, die.get_ur_x()), std::min(y + window_size, die.get_ur_y())};
      windows.emplace_back(window, metalArea(source, mi_layer, window));
      ++x_count;
    }
    mi_layer.set_density_window_x_num(x_count);
    ++y_count;
  }
  mi_layer.set_density_window_y_num(y_count);
  mi_layer.set_density_window_list(windows);
}

double MetalFillKernel::metalArea(const MetalFillSource& source, const MILayer& mi_layer, const MIRect& region)
{
  std::vector<MIRect> rectangles;
  for (const auto& occupancy : source.query(mi_layer, region)) {
    if (!occupancy.contributes_density) {
      continue;
    }
    const auto intersection = occupancy.bounds.get_intersect_rect(region);
    if (intersection.is_valid()) {
      rectangles.push_back(intersection);
    }
  }
  return unionArea(rectangles);
}

void MetalFillKernel::buildLayerFill(MIModel& mi_model, MILayer& mi_layer, const MetalFillSource& source)
{
  const auto tracks = trackCoordinates(mi_layer, mi_model.get_die());
  const auto step = trackStep(mi_layer, source);
  for (std::size_t index = 0; index < tracks.size(); index += static_cast<std::size_t>(step)) {
    for (const auto& fill : fillRectangles(mi_model, mi_layer, source, mi_model.get_die(), tracks[index])) {
      addFillRect(mi_model, mi_layer, fill);
    }
  }
}

std::vector<int32_t> MetalFillKernel::trackCoordinates(const MILayer& mi_layer, const MIRect& region)
{
  const auto lower_half_width = mi_layer.get_wire_width() / 2;
  const auto upper_half_width = mi_layer.get_wire_width() - lower_half_width;
  const auto lower = mi_layer.get_is_horizontal() ? region.get_ll_y() + lower_half_width : region.get_ll_x() + lower_half_width;
  const auto upper = mi_layer.get_is_horizontal() ? region.get_ur_y() - upper_half_width : region.get_ur_x() - upper_half_width;
  std::vector<int32_t> result;
  for (int32_t coordinate = firstTrackCoordinate(lower, mi_layer.get_track_start(), mi_layer.get_track_pitch()); coordinate <= upper;
       coordinate += mi_layer.get_track_pitch()) {
    result.push_back(coordinate);
  }
  return result;
}

int32_t MetalFillKernel::trackStep(const MILayer& mi_layer, const MetalFillSource& source)
{
  return std::max(ceilDiv(mi_layer.get_wire_width() + fillSpacing(mi_layer, source, mi_layer.get_min_wire_length()),
                          mi_layer.get_track_pitch()),
                  1);
}

int32_t MetalFillKernel::firstTrackCoordinate(int32_t coordinate, int32_t track_start, int32_t track_pitch)
{
  if (coordinate <= track_start) {
    return track_start;
  }
  return track_start + ceilDiv(coordinate - track_start, track_pitch) * track_pitch;
}

std::vector<MIRect> MetalFillKernel::fillRectangles(MIModel& mi_model, const MILayer& mi_layer, const MetalFillSource& source,
                                                     const MIRect& fill_region, int32_t track_coordinate)
{
  const auto candidate = fillRect(mi_model, mi_layer, fill_region, track_coordinate);
  if (!candidate.is_valid()) {
    return {};
  }

  auto blocked = blockedIntervals(source, mi_layer, candidate);
  std::sort(blocked.begin(), blocked.end());
  std::vector<std::pair<int32_t, int32_t>> merged;
  for (const auto& interval : blocked) {
    if (merged.empty() || merged.back().second < interval.first) {
      merged.push_back(interval);
    } else {
      merged.back().second = std::max(merged.back().second, interval.second);
    }
  }

  int32_t begin = mi_layer.get_is_horizontal() ? candidate.get_ll_x() : candidate.get_ll_y();
  const int32_t end = mi_layer.get_is_horizontal() ? candidate.get_ur_x() : candidate.get_ur_y();
  std::vector<MIRect> free_rectangles;
  for (const auto& [blocked_begin, blocked_end] : merged) {
    if (begin < blocked_begin) {
      if (mi_layer.get_is_horizontal()) {
        free_rectangles.emplace_back(begin, candidate.get_ll_y(), blocked_begin, candidate.get_ur_y());
      } else {
        free_rectangles.emplace_back(candidate.get_ll_x(), begin, candidate.get_ur_x(), blocked_begin);
      }
    }
    begin = std::max(begin, blocked_end);
  }
  if (begin < end) {
    if (mi_layer.get_is_horizontal()) {
      free_rectangles.emplace_back(begin, candidate.get_ll_y(), end, candidate.get_ur_y());
    } else {
      free_rectangles.emplace_back(candidate.get_ll_x(), begin, candidate.get_ur_x(), end);
    }
  }

  std::vector<MIRect> result;
  for (const auto& free_rect : free_rectangles) {
    auto fills = uniformFillRectangles(mi_model, mi_layer, source, free_rect);
    result.insert(result.end(), fills.begin(), fills.end());
  }
  return result;
}

std::vector<std::pair<int32_t, int32_t>> MetalFillKernel::blockedIntervals(const MetalFillSource& source, const MILayer& mi_layer,
                                                                            const MIRect& fill_rect)
{
  const auto query_rect = fill_rect.get_expand_rect(mi_layer.get_max_spacing());
  std::vector<std::pair<int32_t, int32_t>> result;
  for (const auto& occupancy : source.query(mi_layer, query_rect)) {
    if (!occupancy.blocks_fill || !occupancy.bounds.is_valid()) {
      continue;
    }
    const auto required_spacing = requiredSpacing(mi_layer, source, fill_rect, occupancy.bounds);
    if (mi_layer.get_is_horizontal()) {
      if (occupancy.bounds.get_ur_y() + required_spacing <= fill_rect.get_ll_y()
          || fill_rect.get_ur_y() + required_spacing <= occupancy.bounds.get_ll_y()) {
        continue;
      }
      const auto begin = std::max(fill_rect.get_ll_x(), occupancy.bounds.get_ll_x() - required_spacing);
      const auto end = std::min(fill_rect.get_ur_x(), occupancy.bounds.get_ur_x() + required_spacing);
      if (begin < end) {
        result.emplace_back(begin, end);
      }
    } else {
      if (occupancy.bounds.get_ur_x() + required_spacing <= fill_rect.get_ll_x()
          || fill_rect.get_ur_x() + required_spacing <= occupancy.bounds.get_ll_x()) {
        continue;
      }
      const auto begin = std::max(fill_rect.get_ll_y(), occupancy.bounds.get_ll_y() - required_spacing);
      const auto end = std::min(fill_rect.get_ur_y(), occupancy.bounds.get_ur_y() + required_spacing);
      if (begin < end) {
        result.emplace_back(begin, end);
      }
    }
  }
  return result;
}

MIRect MetalFillKernel::fillRect(const MIModel& mi_model, const MILayer& mi_layer, const MIRect& fill_region, int32_t track_coordinate)
{
  const auto lower_half_width = mi_layer.get_wire_width() / 2;
  const auto upper_half_width = mi_layer.get_wire_width() - lower_half_width;
  MIRect result;
  if (mi_layer.get_is_horizontal()) {
    result = MIRect{alignUp(fill_region.get_ll_x(), mi_model.get_manufacture_grid()), track_coordinate - lower_half_width,
                    alignDown(fill_region.get_ur_x(), mi_model.get_manufacture_grid()), track_coordinate + upper_half_width};
  } else {
    result = MIRect{track_coordinate - lower_half_width, alignUp(fill_region.get_ll_y(), mi_model.get_manufacture_grid()),
                    track_coordinate + upper_half_width, alignDown(fill_region.get_ur_y(), mi_model.get_manufacture_grid())};
  }
  if (!result.is_valid() || std::max(result.get_width(), result.get_height()) < mi_layer.get_min_wire_length()) {
    return {};
  }
  return result;
}

std::vector<MIRect> MetalFillKernel::uniformFillRectangles(const MIModel& mi_model, const MILayer& mi_layer,
                                                            const MetalFillSource& source, const MIRect& free_fill_rect)
{
  const auto grid = mi_model.get_manufacture_grid();
  const auto fill_length = alignUp(mi_layer.get_min_wire_length(), grid);
  const auto spacing = alignUp(fillSpacing(mi_layer, source, mi_layer.get_wire_width()), grid);
  const auto begin = mi_layer.get_is_horizontal() ? alignUp(free_fill_rect.get_ll_x(), grid) : alignUp(free_fill_rect.get_ll_y(), grid);
  const auto end = mi_layer.get_is_horizontal() ? alignDown(free_fill_rect.get_ur_x(), grid) : alignDown(free_fill_rect.get_ur_y(), grid);
  const auto available = end - begin;
  const auto fill_count = (available + spacing) / (fill_length + spacing);
  if (fill_count <= 0) {
    return {};
  }

  const auto extra_grid_count = (available - fill_count * fill_length - (fill_count - 1) * spacing) / grid;
  const auto extra_per_gap = extra_grid_count / (fill_count + 1);
  const auto extra_remainder = extra_grid_count % (fill_count + 1);
  int32_t coordinate = begin + (extra_per_gap + (extra_remainder > 0 ? 1 : 0)) * grid;
  std::vector<MIRect> result;
  result.reserve(static_cast<std::size_t>(fill_count));
  for (int32_t index = 0; index < fill_count; ++index) {
    if (mi_layer.get_is_horizontal()) {
      result.emplace_back(coordinate, free_fill_rect.get_ll_y(), coordinate + fill_length, free_fill_rect.get_ur_y());
    } else {
      result.emplace_back(free_fill_rect.get_ll_x(), coordinate, free_fill_rect.get_ur_x(), coordinate + fill_length);
    }
    const auto extra_for_gap = extra_per_gap + (index + 1 < extra_remainder ? 1 : 0);
    coordinate += fill_length + spacing + extra_for_gap * grid;
  }
  return result;
}

int32_t MetalFillKernel::fillSpacing(const MILayer& mi_layer, const MetalFillSource& source, int32_t parallel_length)
{
  return source.spacing(mi_layer, mi_layer.get_wire_width(), parallel_length);
}

int32_t MetalFillKernel::requiredSpacing(const MILayer& mi_layer, const MetalFillSource& source, const MIRect& first,
                                         const MIRect& second)
{
  const auto first_width = std::min(first.get_width(), first.get_height());
  const auto second_width = std::min(second.get_width(), second.get_height());
  return source.spacing(mi_layer, std::max(first_width, second_width), parallelLength(first, second));
}

int32_t MetalFillKernel::parallelLength(const MIRect& first, const MIRect& second)
{
  const auto x_length = std::min(first.get_ur_x(), second.get_ur_x()) - std::max(first.get_ll_x(), second.get_ll_x());
  const auto y_length = std::min(first.get_ur_y(), second.get_ur_y()) - std::max(first.get_ll_y(), second.get_ll_y());
  return std::max(x_length, y_length);
}

std::vector<int32_t> MetalFillKernel::affectedDensityWindowIndices(const MIModel& mi_model, const MILayer& mi_layer,
                                                                    const MIRect& rectangle)
{
  const auto window_size = static_cast<int32_t>(std::llround(mi_model.get_mi_com_param().get_density_window_size_micron()
                                                              * mi_model.get_micron_dbu()));
  const auto window_step = static_cast<int32_t>(std::llround(mi_model.get_mi_com_param().get_density_window_step_micron()
                                                              * mi_model.get_micron_dbu()));
  const auto& die = mi_model.get_die();
  const auto x_delta = rectangle.get_ll_x() - die.get_ll_x() - window_size;
  const auto y_delta = rectangle.get_ll_y() - die.get_ll_y() - window_size;
  auto begin_x = x_delta < 0 ? 0 : x_delta / window_step + 1;
  auto begin_y = y_delta < 0 ? 0 : y_delta / window_step + 1;
  auto end_x = (rectangle.get_ur_x() - die.get_ll_x() - 1) / window_step;
  auto end_y = (rectangle.get_ur_y() - die.get_ll_y() - 1) / window_step;
  begin_x = std::max(begin_x, 0);
  begin_y = std::max(begin_y, 0);
  end_x = std::min(end_x, mi_layer.get_density_window_x_num() - 1);
  end_y = std::min(end_y, mi_layer.get_density_window_y_num() - 1);

  std::vector<int32_t> result;
  for (int32_t y = begin_y; y <= end_y; ++y) {
    for (int32_t x = begin_x; x <= end_x; ++x) {
      result.push_back(mi_layer.get_density_window_idx(x, y));
    }
  }
  return result;
}

void MetalFillKernel::addFillRect(MIModel& mi_model, MILayer& mi_layer, const MIRect& fill_rect)
{
  mi_layer.get_fill_rect_list().push_back(fill_rect);
  mi_layer.add_inserted_metal_num();
  mi_model.add_inserted_metal_num();
  updateDensityWindows(mi_model, mi_layer, fill_rect);
}

void MetalFillKernel::updateDensityWindows(MIModel& mi_model, MILayer& mi_layer, const MIRect& fill_rect)
{
  for (const auto index : affectedDensityWindowIndices(mi_model, mi_layer, fill_rect)) {
    auto& window = mi_layer.get_density_window_list().at(static_cast<std::size_t>(index));
    window.add_metal_area(fill_rect.get_intersect_rect(window.get_rect()).get_area());
  }
}

int32_t MetalFillKernel::ceilDiv(int32_t dividend, int32_t divisor)
{
  if (dividend <= 0 || divisor <= 0) {
    return 0;
  }
  return (dividend - 1) / divisor + 1;
}

int32_t MetalFillKernel::alignUp(int32_t coordinate, int32_t grid)
{
  return grid <= 1 ? coordinate : ceilDiv(coordinate, grid) * grid;
}

int32_t MetalFillKernel::alignDown(int32_t coordinate, int32_t grid)
{
  return grid <= 1 ? coordinate : coordinate / grid * grid;
}

}  // namespace imj
