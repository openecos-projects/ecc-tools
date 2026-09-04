// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "Utility.hpp"

namespace idrc {

// public

void Utility::initInst()
{
  if (_util_instance == nullptr) {
    _util_instance = new Utility();
  }
}

Utility& Utility::getInst()
{
  if (_util_instance == nullptr) {
    initInst();
  }
  return *_util_instance;
}

void Utility::destroyInst()
{
  if (_util_instance != nullptr) {
    delete _util_instance;
    _util_instance = nullptr;
  }
}

int32_t Utility::getManhattanDistance(PlanarCoord start_coord, PlanarCoord end_coord)
{
  return std::abs(start_coord.get_x() - end_coord.get_x()) + std::abs(start_coord.get_y() - end_coord.get_y());
}

double Utility::getEuclideanDistance(const PlanarRect& a, const PlanarRect& b)
{
  int32_t x_spacing = std::max(b.get_ll_x() - a.get_ur_x(), a.get_ll_x() - b.get_ur_x());
  int32_t y_spacing = std::max(b.get_ll_y() - a.get_ur_y(), a.get_ll_y() - b.get_ur_y());

  if (x_spacing > 0 && y_spacing > 0) {
    double x = x_spacing;
    double y = y_spacing;
    return std::sqrt(x * x + y * y);
  } else {
    return std::max(std::max(x_spacing, y_spacing), 0);
  }
}

double Utility::getProjectionDistance(PlanarRect a, PlanarRect b)
{
  int32_t x_spacing = std::max(b.get_ll_x() - a.get_ur_x(), a.get_ll_x() - b.get_ur_x());
  int32_t y_spacing = std::max(b.get_ll_y() - a.get_ur_y(), a.get_ll_y() - b.get_ur_y());
  if (getParallelLength(a, b) >= 0) {
    return getEuclideanDistance(a, b);
  } else {
    return std::max(std::abs(x_spacing), std::abs(y_spacing));
  }
}

int32_t Utility::getParallelLength(const PlanarRect& a, const PlanarRect& b)
{
  int32_t x_parallel_length = std::min(a.get_ur_x(), b.get_ur_x()) - std::max(a.get_ll_x(), b.get_ll_x());
  int32_t y_parallel_length = std::min(a.get_ur_y(), b.get_ur_y()) - std::max(a.get_ll_y(), b.get_ll_y());
  return std::max(x_parallel_length, y_parallel_length);
}

int32_t Utility::getOrientEdgeDistance(PlanarRect& a, PlanarRect& b, Orientation orient)
{
  PlanarRect a_edge_rect = getRect(a.getOrientEdge(orient));
  PlanarRect b_edge_rect = getRect(b.getOrientEdge(orient));
  return static_cast<int32_t>(getEuclideanDistance(a_edge_rect, b_edge_rect));
}

int32_t Utility::getOrientEnclosure(PlanarRect master, PlanarRect insider, Orientation orient)
{
  bool isBeyond = false;
  switch (orient) {
    case Orientation::kNorth:
      isBeyond = (insider.get_ur_y() >= master.get_ur_y());
      break;
    case Orientation::kWest:
      isBeyond = (insider.get_ll_x() <= master.get_ll_x());
      break;
    case Orientation::kSouth:
      isBeyond = (insider.get_ll_y() <= master.get_ll_y());
      break;
    case Orientation::kEast:
      isBeyond = (insider.get_ur_x() >= master.get_ur_x());
      break;
    default:
      return -1;
  }

  if (isBeyond)
    return 0;

  return getOrientEdgeDistance(master, insider, orient);
}

std::vector<Orientation> Utility::getOrientationList(const PlanarCoord& start_coord, const PlanarCoord& end_coord, Orientation point_orientation)
{
  std::set<Orientation> orientation_set;
  orientation_set.insert(getOrientation(start_coord, PlanarCoord(start_coord.get_x(), end_coord.get_y()), point_orientation));
  orientation_set.insert(getOrientation(start_coord, PlanarCoord(end_coord.get_x(), start_coord.get_y()), point_orientation));
  orientation_set.erase(Orientation::kNone);
  return std::vector<Orientation>(orientation_set.begin(), orientation_set.end());
}

Orientation Utility::getOrientation(const LayerCoord& start_coord, const LayerCoord& end_coord, Orientation point_orientation)
{
  Orientation orientation;

  if (start_coord.get_layer_idx() == end_coord.get_layer_idx()) {
    if (isProximal(start_coord, end_coord)) {
      orientation = point_orientation;
    } else if (isHorizontal(start_coord, end_coord)) {
      orientation = (start_coord.get_x() - end_coord.get_x()) > 0 ? Orientation::kWest : Orientation::kEast;
    } else if (isVertical(start_coord, end_coord)) {
      orientation = (start_coord.get_y() - end_coord.get_y()) > 0 ? Orientation::kSouth : Orientation::kNorth;
    } else {
      orientation = Orientation::kOblique;
    }
  } else {
    if (isProximal(start_coord, end_coord)) {
      orientation = (start_coord.get_layer_idx() - end_coord.get_layer_idx()) > 0 ? Orientation::kBelow : Orientation::kAbove;
    } else {
      orientation = Orientation::kOblique;
    }
  }
  return orientation;
}

Orientation Utility::getOppositeOrientation(Orientation orientation)
{
  Orientation opposite_orientation = Orientation::kNone;
  switch (orientation) {
    case Orientation::kEast:
      opposite_orientation = Orientation::kWest;
      break;
    case Orientation::kWest:
      opposite_orientation = Orientation::kEast;
      break;
    case Orientation::kSouth:
      opposite_orientation = Orientation::kNorth;
      break;
    case Orientation::kNorth:
      opposite_orientation = Orientation::kSouth;
      break;
    case Orientation::kAbove:
      opposite_orientation = Orientation::kBelow;
      break;
    case Orientation::kBelow:
      opposite_orientation = Orientation::kAbove;
      break;
    default:
      DRCLOG.error(Loc::current(), "The orientation is error!");
      break;
  }
  return opposite_orientation;
}

std::vector<Orientation> Utility::getOrthogonalOrientationList(Orientation orientation)
{
  std::vector<Orientation> orientation_list;
  if (orientation == Orientation::kEast || orientation == Orientation::kWest) {
    orientation_list.push_back(Orientation::kNorth);
    orientation_list.push_back(Orientation::kSouth);
  } else if (orientation == Orientation::kSouth || orientation == Orientation::kNorth) {
    orientation_list.push_back(Orientation::kEast);
    orientation_list.push_back(Orientation::kWest);
  } else {
    DRCLOG.error(Loc::current(), "The orientation is error!");
  }
  return orientation_list;
}

bool Utility::getCornerOrientsInRect(const PlanarRect& rect, const PlanarCoord& corner_point, Orientation& orient1, Orientation& orient2)
{
  int32_t x = corner_point.get_x();
  int32_t y = corner_point.get_y();
  if (x == rect.get_ll_x() && y == rect.get_ll_y()) {
    orient1 = Orientation::kEast;
    orient2 = Orientation::kNorth;
    return true;
  }
  if (x == rect.get_ur_x() && y == rect.get_ur_y()) {
    orient1 = Orientation::kWest;
    orient2 = Orientation::kSouth;
    return true;
  }
  if (x == rect.get_ll_x() && y == rect.get_ur_y()) {
    orient1 = Orientation::kEast;
    orient2 = Orientation::kSouth;
    return true;
  }
  if (x == rect.get_ur_x() && y == rect.get_ll_y()) {
    orient1 = Orientation::kWest;
    orient2 = Orientation::kNorth;
    return true;
  }
  return false;
}

Direction Utility::getOppositeDirection(Direction direction)
{
  if (direction == Direction::kHorizontal) {
    return Direction::kVertical;
  } else if (direction == Direction::kVertical) {
    return Direction::kHorizontal;
  } else {
    return Direction::kNone;
  }
}

Orientation Utility::getOrientaionFromDirection(Direction direction, bool is_ur)
{
  if (direction == Direction::kHorizontal) {
    return is_ur ? Orientation::kNorth : Orientation::kSouth;
  } else if (direction == Direction::kVertical) {
    return is_ur ? Orientation::kEast : Orientation::kWest;
  }
  return Orientation::kNone;
}

Direction Utility::getDirection(PlanarCoord start_coord, PlanarCoord end_coord)
{
  if (start_coord == end_coord) {
    return Direction::kProximal;
  }

  bool is_h = (start_coord.get_y() == end_coord.get_y());
  bool is_v = (start_coord.get_x() == end_coord.get_x());
  return is_h ? Direction::kHorizontal : is_v ? Direction::kVertical : Direction::kOblique;
}

bool Utility::isProximal(const PlanarCoord& start_coord, const PlanarCoord& end_coord)
{
  return getDirection(start_coord, end_coord) == Direction::kProximal;
}

bool Utility::isHorizontal(const PlanarCoord& start_coord, const PlanarCoord& end_coord)
{
  return getDirection(start_coord, end_coord) == Direction::kHorizontal;
}

bool Utility::isVertical(const PlanarCoord& start_coord, const PlanarCoord& end_coord)
{
  return getDirection(start_coord, end_coord) == Direction::kVertical;
}

bool Utility::isOblique(const PlanarCoord& start_coord, const PlanarCoord& end_coord)
{
  return getDirection(start_coord, end_coord) == Direction::kOblique;
}

bool Utility::isRightAngled(const PlanarCoord& start_coord, const PlanarCoord& end_coord)
{
  return isProximal(start_coord, end_coord) || isHorizontal(start_coord, end_coord) || isVertical(start_coord, end_coord);
}

bool Utility::isCollinear(PlanarCoord& first_coord, PlanarCoord& second_coord, PlanarCoord& third_coord)
{
  return getDirection(first_coord, second_coord) == getDirection(second_coord, third_coord);
}

bool Utility::isConvexCorner(Rotation rotation, PlanarCoord& first_coord, PlanarCoord& second_coord, PlanarCoord& third_coord)
{
  if (isCollinear(first_coord, second_coord, third_coord)) {
    return false;
  }

  return crossProduct(rotation, first_coord, second_coord, third_coord) < 0;
}

bool Utility::isConcaveCorner(Rotation rotation, PlanarCoord& first_coord, PlanarCoord& second_coord, PlanarCoord& third_coord)
{
  if (isCollinear(first_coord, second_coord, third_coord)) {
    return false;
  }

  return crossProduct(rotation, first_coord, second_coord, third_coord) > 0;
}

bool Utility::isInside(const PlanarRect& master, const PlanarRect& rect)
{
  return (isInside(master, rect.get_ll()) && isInside(master, rect.get_ur()));
}

bool Utility::isInside(const PlanarRect& rect, const PlanarCoord& coord, bool boundary)
{
  int32_t coord_x = coord.get_x(), coord_y = coord.get_y();
  int32_t rect_ll_x = rect.get_ll_x(), rect_ll_y = rect.get_ll_y();
  int32_t rect_ur_x = rect.get_ur_x(), rect_ur_y = rect.get_ur_y();
  if (boundary) {
    return (rect_ll_x <= coord_x && coord_x <= rect_ur_x) && (rect_ll_y <= coord_y && coord_y <= rect_ur_y);
  }
  return (rect_ll_x < coord_x && coord_x < rect_ur_x) && (rect_ll_y < coord_y && coord_y < rect_ur_y);
}

bool Utility::isInside(const PlanarRect& master, const Segment<PlanarCoord>& seg)
{
  return isInside(master, seg.get_first()) && isInside(master, seg.get_second());
}

bool Utility::isInside(const PlanarRect& master, const Segment<PlanarCoord>& seg, bool boundary)
{
  if (!isInside(master, seg.get_first(), true) || !isInside(master, seg.get_second(), true)) {
    return false;
  }
  if (!boundary) {
    PlanarCoord p1 = seg.get_first();
    PlanarCoord p2 = seg.get_second();

    int32_t x1 = p1.get_x(), y1 = p1.get_y();
    int32_t x2 = p2.get_x(), y2 = p2.get_y();
    int32_t xmin = master.get_ll_x(), ymin = master.get_ll_y();
    int32_t xmax = master.get_ur_x(), ymax = master.get_ur_y();

    // 判断垂直线段：如果线段在 master 的左边界或右边界上完全/部分重叠
    if (x1 == x2) {
      if (x1 == xmin || x1 == xmax) {
        return false;
      }
    }

    // 判断水平线段：如果线段在 master 的下边界或上边界上完全/部分重叠
    if (y1 == y2) {
      if (y1 == ymin || y1 == ymax) {
        return false;
      }
    }
  }
  return isInside(master, seg);
}

bool Utility::isInside(const Segment<PlanarCoord>& master, const Segment<PlanarCoord>& seg)
{
  if (!isRightAngled(master.get_first(), master.get_second()) || !isRightAngled(seg.get_first(), seg.get_second())) {
    DRCLOG.error(Loc::current(), "The segment is error!");
  }
  PlanarRect rect = getRect(master.get_first(), master.get_second());
  return isInside(rect, seg.get_first()) && isInside(rect, seg.get_second());
}

bool Utility::isOpenOverlap(const PlanarRect& a, const PlanarRect& b)
{
  return isOverlap(a, b, false);
}

bool Utility::isOpenOverlap(const PlanarCoord& start_coord, const PlanarCoord& end_coord, const PlanarRect& rect)
{
  return isOverlap(getRect(start_coord, end_coord), rect, false);
}

bool Utility::isClosedOverlap(const PlanarRect& a, const PlanarRect& b)
{
  return isOverlap(a, b, true);
}

bool Utility::isOverlap(const PlanarRect& a, const PlanarRect& b, bool consider_edge)
{
  int32_t x_spacing = std::max(b.get_ll_x() - a.get_ur_x(), a.get_ll_x() - b.get_ur_x());
  int32_t y_spacing = std::max(b.get_ll_y() - a.get_ur_y(), a.get_ll_y() - b.get_ur_y());

  if (x_spacing == 0 && y_spacing <= 0) {
    return consider_edge;
  } else if (x_spacing <= 0 && y_spacing == 0) {
    return consider_edge;
  } else {
    return (x_spacing < 0 && y_spacing < 0);
  }
}

bool Utility::isDirectionOverlap(const PlanarRect& master, const PlanarRect& rect, Direction direction)
{
  if (direction == Direction::kHorizontal) {
    return isOpenOverlap(getRect(rect.getOrientEdge(Orientation::kNorth)), master) || isOpenOverlap(getRect(rect.getOrientEdge(Orientation::kSouth)), master);
  } else if (direction == Direction::kVertical) {
    return isOpenOverlap(getRect(rect.getOrientEdge(Orientation::kWest)), master) || isOpenOverlap(getRect(rect.getOrientEdge(Orientation::kEast)), master);
  }
  return false;
}

Orientation Utility::getTouchedEdgeOrient(PlanarRect rect, Segment<PlanarCoord> segment)
{
  Orientation edge_orient = Orientation::kNone;
  for (Orientation orient : {Orientation::kNorth, Orientation::kSouth, Orientation::kWest, Orientation::kEast}) {
    Segment<PlanarCoord> orient_edge = rect.getOrientEdge(orient);
    if (isInside(orient_edge, segment) || isInside(segment, orient_edge)) {
      edge_orient = orient;
      break;
    }
  }
  return edge_orient;
}

bool Utility::isRectConnectInPolyset(const GTLPolySetInt& polyset, const PlanarRect& rect1, const PlanarRect& rect2)
{
  std::vector<GTLPolyInt> components;
  polyset.get(components);

  for (const auto& poly : components) {
    GTLPolySetInt test_set;
    test_set += poly;

    if (!gtl::empty(test_set & convertToGTLRectInt(rect1)) && !gtl::empty(test_set & convertToGTLRectInt(rect2))) {
      return true;
    }
  }
  return false;
}

bool Utility::isPolyRectIntersect(const GTLPolyInt& poly, const GTLRectInt& rect)
{
  GTLPolySetInt temp_set;
  temp_set += poly;
  return !gtl::empty(temp_set & rect);
}

int32_t Utility::getProjectionLength(const GTLPolySetInt& ps, int32_t axis)
{
  std::vector<GTLRectInt> rects;
  gtl::get_max_rectangles(rects, ps);
  if (rects.empty())
    return 0;

  std::vector<std::pair<int32_t, int32_t>> intervals;
  intervals.reserve(rects.size());
  for (const auto& r : rects) {
    if (axis == 0)
      intervals.push_back({gtl::xl(r), gtl::xh(r)});
    else
      intervals.push_back({gtl::yl(r), gtl::yh(r)});
  }

  std::sort(intervals.begin(), intervals.end());
  int32_t totalLen = 0;
  if (!intervals.empty()) {
    int32_t curLow = intervals[0].first;
    int32_t curHigh = intervals[0].second;
    for (size_t i = 1; i < intervals.size(); ++i) {
      if (intervals[i].first < curHigh) {
        curHigh = std::max(curHigh, intervals[i].second);
      } else {
        totalLen += (curHigh - curLow);
        curLow = intervals[i].first;
        curHigh = intervals[i].second;
      }
    }
    totalLen += (curHigh - curLow);
  }
  return totalLen;
}

int32_t Utility::getDirectPRL(const GTLPolySetInt& polyset, const PlanarRect& rect1, const PlanarRect& rect2)
{
  GTLRectInt g1 = convertToGTLRectInt(rect1);
  GTLRectInt g2 = convertToGTLRectInt(rect2);

  int32_t overlapX_low = std::max(gtl::xl(g1), gtl::xl(g2));
  int32_t overlapX_high = std::min(gtl::xh(g1), gtl::xh(g2));
  int32_t overlapY_low = std::max(gtl::yl(g1), gtl::yl(g2));
  int32_t overlapY_high = std::min(gtl::yh(g1), gtl::yh(g2));

  bool hasXOverlap = overlapX_low < overlapX_high;
  bool hasYOverlap = overlapY_low < overlapY_high;

  if (!hasXOverlap && !hasYOverlap)
    return 0;

  GTLRectInt gapRect;
  int32_t axisToMeasure = -1;

  // 优先处理左右相对 (Y方向重叠)
  if (hasYOverlap) {
    int32_t x_low = std::min(gtl::xh(g1), gtl::xh(g2));
    int32_t x_high = std::max(gtl::xl(g1), gtl::xl(g2));
    if (x_low < x_high) {
      axisToMeasure = 1;
      gtl::set_points(gapRect, gtl::point_data<int32_t>(x_low, overlapY_low), gtl::point_data<int32_t>(x_high, overlapY_high));
    }
  }

  // 如果没有水平间隙，尝试处理垂直相对 (X方向重叠)
  if (axisToMeasure == -1 && hasXOverlap) {
    int32_t y_low = std::min(gtl::yh(g1), gtl::yh(g2));
    int32_t y_high = std::max(gtl::yl(g1), gtl::yl(g2));
    if (y_low < y_high) {
      axisToMeasure = 0;
      gtl::set_points(gapRect, gtl::point_data<int32_t>(overlapX_low, y_low), gtl::point_data<int32_t>(overlapX_high, y_high));
    }
  }

  if (axisToMeasure == -1)
    return 0;

  std::vector<GTLPolyInt> components;
  polyset.get(components);

  GTLPolySetInt bridge_in_gap;
  GTLPolySetInt gapSet;
  gapSet += gapRect;

  for (const auto& poly : components) {
    if (isPolyRectIntersect(poly, g1) && isPolyRectIntersect(poly, g2)) {
      GTLPolySetInt poly_set;
      poly_set += poly;
      poly_set -= g1;
      poly_set -= g2;

      bridge_in_gap += (poly_set & gapSet);
    }
  }

  GTLPolySetInt freeSpace = gapSet - bridge_in_gap;

  return getProjectionLength(freeSpace, axisToMeasure);
}

int32_t Utility::getPolysetMaxPRL(const GTLPolySetInt& polyset, PlanarRect rect)
{
  std::vector<GTLRectInt> gtl_rects;
  gtl::get_max_rectangles(gtl_rects, polyset);
  int32_t max_prl = std::numeric_limits<int32_t>::min();
  for (const GTLRectInt& gtl_rect : gtl_rects) {
    PlanarRect max_rect = convertToPlanarRect(gtl_rect);
    int32_t prl = getParallelLength(rect, max_rect);
    max_prl = std::max(max_prl, prl);
  }
  return max_prl;
}

bool Utility::isPolysetExternal(const GTLPolySetInt& polyset, PlanarRect rect, Orientation orient)
{
  std::vector<GTLHolePolyInt> polys;
  polyset.get(polys);

  Orientation target_orient = getOppositeOrientation(orient);

  for (const auto& poly : polys) {
    std::vector<GTLPointInt> pts(poly.begin(), poly.end());
    size_t n = pts.size();

    for (size_t i = 0; i < n; ++i) {
      const auto& prev_pt = pts[i];
      const auto& curr_pt = pts[(i + 1) % n];
      Orientation curr_orient = Orientation::kNone;

      if (curr_pt.y() == prev_pt.y()) {  // 水平边
        if (curr_pt.x() < prev_pt.x())
          curr_orient = Orientation::kNorth;
        else if (curr_pt.x() > prev_pt.x())
          curr_orient = Orientation::kSouth;
      } else if (curr_pt.x() == prev_pt.x()) {  // 垂直边
        if (curr_pt.y() > prev_pt.y())
          curr_orient = Orientation::kEast;
        else if (curr_pt.y() < prev_pt.y())
          curr_orient = Orientation::kWest;
      }

      if (curr_orient == target_orient) {
        PlanarRect edge_rect{std::min(prev_pt.x(), curr_pt.x()), std::min(prev_pt.y(), curr_pt.y()), std::max(prev_pt.x(), curr_pt.x()),
                             std::max(prev_pt.y(), curr_pt.y())};

        if (isOpenOverlap(rect, edge_rect)) {
          return true;
        }
      }
    }
  }
  return false;
}

Rotation Utility::getRotation(GTLPolyInt& gtl_poly)
{
  gtl::direction_1d gtl_rotation = gtl::winding(gtl_poly);
  if (gtl::direction_1d(gtl::direction_1d_enum::CLOCKWISE) == gtl_rotation) {
    return Rotation::kClockwise;
  } else if (gtl::direction_1d(gtl::direction_1d_enum::COUNTERCLOCKWISE) == gtl_rotation) {
    return Rotation::kCounterclockwise;
  } else {
    return Rotation::kNone;
  }
}

Rotation Utility::getRotation(GTLHolePolyInt& gtl_holy_poly)
{
  gtl::direction_1d gtl_rotation = gtl::winding(gtl_holy_poly);
  if (gtl::direction_1d(gtl::direction_1d_enum::CLOCKWISE) == gtl_rotation) {
    return Rotation::kClockwise;
  } else if (gtl::direction_1d(gtl::direction_1d_enum::COUNTERCLOCKWISE) == gtl_rotation) {
    return Rotation::kCounterclockwise;
  } else {
    return Rotation::kNone;
  }
}

std::map<Orientation, std::vector<PlanarRect>> Utility::getPolyExtEdges(GTLPolySetInt polyset)
{
  std::map<Orientation, std::vector<PlanarRect>> result;
  std::vector<GTLHolePolyInt> poly_list;
  polyset.get(poly_list);

  for (GTLHolePolyInt& poly : poly_list) {
    std::vector<std::pair<std::vector<PlanarCoord>, bool>> rings;

    // 处理外轮廓
    std::vector<PlanarCoord> outer_coords;
    for (auto it = poly.begin(); it != poly.end(); ++it) {
      outer_coords.push_back(convertToPlanarCoord(*it));
    }
    rings.push_back({std::move(outer_coords), false});

    // 处理内孔
    for (auto h_it = poly.begin_holes(); h_it != poly.end_holes(); ++h_it) {
      std::vector<PlanarCoord> hole_coords;
      for (auto p_it = (*h_it).begin(); p_it != (*h_it).end(); ++p_it) {
        hole_coords.push_back(convertToPlanarCoord(*p_it));
      }
      rings.push_back({std::move(hole_coords), true});
    }

    bool is_ccw = (getRotation(poly) == Rotation::kCounterclockwise);

    for (const auto& [coords, is_hole] : rings) {
      int32_t n = static_cast<int32_t>(coords.size());
      if (n < 2)
        continue;

      for (int32_t i = 0; i < n; ++i) {
        const PlanarCoord& p1 = coords[i];
        const PlanarCoord& p2 = coords[(i + 1) % n];  // 闭合回路

        if (p1 == p2)
          continue;

        // 对于 CCW 外轮廓：+X方向的边位于底部(South)，+Y方向的边位于右侧(East)，以此类推
        // 如果是 CW 或者内孔，则逻辑翻转
        bool reverse = !is_ccw;

        Orientation orient = Orientation::kNone;
        if (p1.get_y() == p2.get_y()) {  // 水平边
          if (p2.get_x() > p1.get_x()) {
            orient = reverse ? Orientation::kNorth : Orientation::kSouth;
          } else {
            orient = reverse ? Orientation::kSouth : Orientation::kNorth;
          }
        } else if (p1.get_x() == p2.get_x()) {  // 垂直边
          if (p2.get_y() > p1.get_y()) {
            orient = reverse ? Orientation::kWest : Orientation::kEast;
          } else {
            orient = reverse ? Orientation::kEast : Orientation::kWest;
          }
        }

        if (orient != Orientation::kNone) {
          result[orient].push_back(getRect(Segment<PlanarCoord>(p1, p2)));
        }
      }
    }
  }
  return result;
}

PlanarCoord Utility::convertToPlanarCoord(GTLPointInt gtl_point)
{
  return PlanarCoord(gtl_point.x(), gtl_point.y());
}

PlanarRect Utility::convertToPlanarRect(const GTLRectInt& gtl_rect)
{
  return PlanarRect(gtl::xl(gtl_rect), gtl::yl(gtl_rect), gtl::xh(gtl_rect), gtl::yh(gtl_rect));
}

PlanarRect Utility::convertToPlanarRect(const BGRectInt& boost_box)
{
  return PlanarRect(boost_box.min_corner().x(), boost_box.min_corner().y(), boost_box.max_corner().x(), boost_box.max_corner().y());
}

BGRectInt Utility::convertToBGRectInt(const PlanarRect& rect)
{
  BGPointInt p1(rect.get_ll_x(), rect.get_ll_y());
  BGPointInt p2(rect.get_ur_x(), rect.get_ur_y());
  BGRectInt bg_rect(p1, p2);
  // 防止出现ll > ur的情况
  bg::correct(bg_rect);
  return bg_rect;
}

BGRectInt Utility::convertToBGRectInt(GTLRectInt& gtl_rect)
{
  return BGRectInt(BGPointInt(gtl::xl(gtl_rect), gtl::yl(gtl_rect)), BGPointInt(gtl::xh(gtl_rect), gtl::yh(gtl_rect)));
}

GTLRectInt Utility::convertToGTLRectInt(const PlanarRect& rect)
{
  return GTLRectInt(rect.get_ll_x(), rect.get_ll_y(), rect.get_ur_x(), rect.get_ur_y());
}

GTLRectInt Utility::convertToGTLRectInt(BGRectInt& boost_box)
{
  return GTLRectInt(boost_box.min_corner().x(), boost_box.min_corner().y(), boost_box.max_corner().x(), boost_box.max_corner().y());
}

int32_t Utility::getLength(BGRectInt& a)
{
  return std::abs(a.max_corner().x() - a.min_corner().x());
}

int32_t Utility::getWidth(BGRectInt& a)
{
  return std::abs(a.max_corner().y() - a.min_corner().y());
}

PlanarCoord Utility::getCenter(BGRectInt& a)
{
  int32_t center_x = std::abs(a.max_corner().x() + a.min_corner().x()) / 2;
  int32_t center_y = std::abs(a.max_corner().y() + a.min_corner().y()) / 2;
  return PlanarCoord(center_x, center_y);
}

BGRectInt Utility::enlargeBGRectInt(BGRectInt& a, int32_t enlarge_size)
{
  return BGRectInt(BGPointInt(a.min_corner().x() - enlarge_size, a.min_corner().y() - enlarge_size),
                   BGPointInt(a.max_corner().x() + enlarge_size, a.max_corner().y() + enlarge_size));
}

void Utility::offsetBGRectInt(BGRectInt& boost_box, PlanarCoord& coord)
{
  boost_box.min_corner().set<0>(boost_box.min_corner().x() + coord.get_x());
  boost_box.min_corner().set<1>(boost_box.min_corner().y() + coord.get_y());

  boost_box.max_corner().set<0>(boost_box.max_corner().x() + coord.get_x());
  boost_box.max_corner().set<1>(boost_box.max_corner().y() + coord.get_y());
}

bool Utility::isOverlap(GTLPolySetInt a, GTLRectInt b, bool consider_edge)
{
  GTLPolySetInt gtl_poly_set;
  gtl_poly_set += b;
  if (consider_edge) {
    a.interact(gtl_poly_set);
  } else {
    a &= gtl_poly_set;
  }
  return gtl::area(a) > 0;
}

bool Utility::isOverlap(BGRectInt& a, BGRectInt& b, bool consider_edge)
{
  int32_t a_ll_x = a.min_corner().x(), a_ll_y = a.min_corner().y();
  int32_t a_ur_x = a.max_corner().x(), a_ur_y = a.max_corner().y();

  int32_t b_ll_x = b.min_corner().x(), b_ll_y = b.min_corner().y();
  int32_t b_ur_x = b.max_corner().x(), b_ur_y = b.max_corner().y();

  int32_t x_spacing = std::max(b_ll_x - a_ur_x, a_ll_x - b_ur_x);
  int32_t y_spacing = std::max(b_ll_y - a_ur_y, a_ll_y - b_ur_y);

  if (x_spacing == 0 || y_spacing == 0) {
    return consider_edge;
  } else {
    return (x_spacing < 0 && y_spacing < 0);
  }
}

BGRectInt Utility::getOverlap(BGRectInt& a, BGRectInt& b)
{
  int32_t overlap_ll_x = std::max(a.min_corner().x(), b.min_corner().x());
  int32_t overlap_ll_y = std::max(a.min_corner().y(), b.min_corner().y());
  int32_t overlap_ur_x = std::min(a.max_corner().x(), b.max_corner().x());
  int32_t overlap_ur_y = std::min(a.max_corner().y(), b.max_corner().y());

  if (overlap_ll_x > overlap_ur_x || overlap_ll_y > overlap_ur_y) {
    return BGRectInt(BGPointInt(0, 0), BGPointInt(0, 0));
  } else {
    return BGRectInt(BGPointInt(overlap_ll_x, overlap_ll_y), BGPointInt(overlap_ur_x, overlap_ur_y));
  }
}

bool Utility::isHorizontal(BGRectInt a)
{
  return (a.max_corner().x() - a.min_corner().x()) >= (a.max_corner().y() - a.min_corner().y());
}

int32_t Utility::getDiagonalLength(BGRectInt& a)
{
  double length = getLength(a);
  double width = getWidth(a);
  return static_cast<int32_t>(std::sqrt(length * length + width * width));
}

int32_t Utility::getEuclideanDistance(BGRectInt& a, BGRectInt& b)
{
  int32_t a_ll_x = a.min_corner().x(), a_ll_y = a.min_corner().y();
  int32_t a_ur_x = a.max_corner().x(), a_ur_y = a.max_corner().y();

  int32_t b_ll_x = b.min_corner().x(), b_ll_y = b.min_corner().y();
  int32_t b_ur_x = b.max_corner().x(), b_ur_y = b.max_corner().y();

  int32_t x_spacing = std::max(b_ll_x - a_ur_x, a_ll_x - b_ur_x);
  int32_t y_spacing = std::max(b_ll_y - a_ur_y, a_ll_y - b_ur_y);

  if (x_spacing > 0 && y_spacing > 0) {
    double x = x_spacing;
    double y = y_spacing;
    return static_cast<int32_t>(std::sqrt(x * x + y * y));
  } else {
    return std::max(std::max(x_spacing, y_spacing), 0);
  }
}

void Utility::printTableList(const std::vector<fort::char_table>& table_list)
{
  std::vector<std::vector<std::string>> print_table_list;
  for (const fort::char_table& table : table_list) {
    if (!table.is_empty()) {
      print_table_list.push_back(splitString(table.to_string(), '\n'));
    }
  }

  int32_t max_size = INT_MIN;
  for (std::vector<std::string>& table : print_table_list) {
    max_size = std::max(max_size, static_cast<int32_t>(table.size()));
  }
  for (std::vector<std::string>& table : print_table_list) {
    for (int32_t i = static_cast<int32_t>(table.size()); i < max_size; i++) {
      std::string table_str;
      table_str.append(table.front().length(), ' ');
      table.push_back(table_str);
    }
  }

  for (int32_t i = 0; i < max_size; i++) {
    std::string table_str;
    for (std::vector<std::string>& table : print_table_list) {
      table_str += table[i];
      table_str += " ";
    }
    DRCLOG.info(Loc::current(), table_str);
  }
}

Segment<PlanarCoord> Utility::getReversedSegment(Segment<PlanarCoord> segment)
{
  return Segment<PlanarCoord>{segment.get_second(), segment.get_first()};
}

PlanarRect Utility::getRect(PlanarCoord start_coord, PlanarCoord end_coord)
{
  PlanarRect rect;
  rect.set_ll_x(std::min(start_coord.get_x(), end_coord.get_x()));
  rect.set_ll_y(std::min(start_coord.get_y(), end_coord.get_y()));
  rect.set_ur_x(std::max(start_coord.get_x(), end_coord.get_x()));
  rect.set_ur_y(std::max(start_coord.get_y(), end_coord.get_y()));
  return rect;
}

PlanarRect Utility::getRect(Segment<PlanarCoord> segment)
{
  return getRect(segment.get_first(), segment.get_second());
}

int64_t Utility::crossProduct(Rotation rotation, PlanarCoord& first_coord, PlanarCoord& second_coord, PlanarCoord& third_coord)
{
  // Use 64-bit arithmetic here because large layout coordinates can make the
  // area term exceed int32_t and flip the sign, which breaks convex/concave checks.
  int64_t cross_product = 0;
  if (rotation == Rotation::kClockwise) {
    cross_product = (static_cast<int64_t>(second_coord.get_x()) - first_coord.get_x()) * (static_cast<int64_t>(third_coord.get_y()) - first_coord.get_y())
                    - (static_cast<int64_t>(second_coord.get_y()) - first_coord.get_y()) * (static_cast<int64_t>(third_coord.get_x()) - first_coord.get_x());
  } else if (rotation == Rotation::kCounterclockwise) {
    cross_product = (static_cast<int64_t>(second_coord.get_x()) - third_coord.get_x()) * (static_cast<int64_t>(first_coord.get_y()) - third_coord.get_y())
                    - (static_cast<int64_t>(second_coord.get_y()) - third_coord.get_y()) * (static_cast<int64_t>(first_coord.get_x()) - third_coord.get_x());
  } else {
    DRCLOG.error(Loc::current(), "The rotation is error!");
  }
  return cross_product;
}

PlanarRect Utility::getOffsetRect(PlanarRect rect, PlanarCoord offset_coord)
{
  int32_t offset_x = offset_coord.get_x();
  int32_t offset_y = offset_coord.get_y();

  addOffset(rect.get_ll(), offset_x, offset_y);
  addOffset(rect.get_ur(), offset_x, offset_y);
  return rect;
}

PlanarRect Utility::getEnlargedRect(PlanarCoord start_coord, PlanarCoord end_coord, int32_t enlarge_size)
{
  if (!CmpPlanarCoordByXASC()(start_coord, end_coord)) {
    std::swap(start_coord, end_coord);
  }
  PlanarRect rect(start_coord, end_coord);

  if (isRightAngled(start_coord, end_coord)) {
    rect = getEnlargedRect(rect, enlarge_size);
  } else {
    DRCLOG.error(Loc::current(), "The segment is oblique!");
  }
  return rect;
}

PlanarRect Utility::getEnlargedRect(PlanarCoord start_coord, PlanarCoord end_coord, int32_t ll_x_minus_offset, int32_t ll_y_minus_offset,
                                    int32_t ur_x_add_offset, int32_t ur_y_add_offset)
{
  if (!CmpPlanarCoordByXASC()(start_coord, end_coord)) {
    std::swap(start_coord, end_coord);
  }
  PlanarRect rect(start_coord, end_coord);

  if (isRightAngled(start_coord, end_coord)) {
    rect = getEnlargedRect(rect, ll_x_minus_offset, ll_y_minus_offset, ur_x_add_offset, ur_y_add_offset);
  } else {
    DRCLOG.error(Loc::current(), "The segment is oblique!");
  }
  return rect;
}

PlanarRect Utility::getRegularRect(PlanarRect rect, PlanarRect border)
{
  PlanarRect regular_rect;
  regular_rect.set_ll(std::max(rect.get_ll_x(), border.get_ll_x()), std::max(rect.get_ll_y(), border.get_ll_y()));
  regular_rect.set_ur(std::min(rect.get_ur_x(), border.get_ur_x()), std::min(rect.get_ur_y(), border.get_ur_y()));
  return regular_rect;
}

PlanarRect Utility::getEnlargedRect(PlanarCoord center_coord, int32_t enlarge_size)
{
  return getEnlargedRect(center_coord, enlarge_size, enlarge_size, enlarge_size, enlarge_size);
}

PlanarRect Utility::getEnlargedRect(PlanarCoord center_coord, int32_t ll_x_minus_offset, int32_t ll_y_minus_offset, int32_t ur_x_add_offset,
                                    int32_t ur_y_add_offset)
{
  PlanarRect rect(center_coord, center_coord);
  minusOffset(rect.get_ll(), ll_x_minus_offset, ll_y_minus_offset);
  addOffset(rect.get_ur(), ur_x_add_offset, ur_y_add_offset);
  return rect;
}

PlanarRect Utility::getEnlargedRect(PlanarRect rect, int32_t enlarge_size)
{
  return getEnlargedRect(rect, enlarge_size, enlarge_size, enlarge_size, enlarge_size);
}

PlanarRect Utility::getEnlargedRect(PlanarRect rect, int32_t x_enlarge_size, int32_t y_enlarge_size)
{
  return getEnlargedRect(rect, x_enlarge_size, y_enlarge_size, x_enlarge_size, y_enlarge_size);
}

PlanarRect Utility::getEnlargedRect(PlanarRect rect, Orientation orientation, int32_t enlarge_size)
{
  if (orientation == Orientation::kNorth) {
    rect = getEnlargedRect(rect, 0, 0, 0, enlarge_size);
  } else if (orientation == Orientation::kSouth) {
    rect = getEnlargedRect(rect, 0, enlarge_size, 0, 0);
  } else if (orientation == Orientation::kWest) {
    rect = getEnlargedRect(rect, enlarge_size, 0, 0, 0);
  } else if (orientation == Orientation::kEast) {
    rect = getEnlargedRect(rect, 0, 0, enlarge_size, 0);
  }
  return rect;
}

PlanarRect Utility::getEnlargedPartRect(PlanarRect rect, Orientation orientation, int32_t enlarge_size)
{
  if (orientation == Orientation::kNorth) {
    rect = getEnlargedRect(rect.get_ur(), rect.getXSpan(), 0, 0, enlarge_size);
  } else if (orientation == Orientation::kSouth) {
    rect = getEnlargedRect(rect.get_ll(), 0, enlarge_size, rect.getXSpan(), 0);
  } else if (orientation == Orientation::kWest) {
    rect = getEnlargedRect(rect.get_ll(), enlarge_size, 0, 0, rect.getYSpan());
  } else if (orientation == Orientation::kEast) {
    rect = getEnlargedRect(rect.get_ur(), 0, rect.getYSpan(), enlarge_size, 0);
  }
  return rect;
}

PlanarRect Utility::getEnlargedRect(PlanarRect rect, int32_t ll_x_minus_offset, int32_t ll_y_minus_offset, int32_t ur_x_add_offset, int32_t ur_y_add_offset)
{
  minusOffset(rect.get_ll(), ll_x_minus_offset, ll_y_minus_offset);
  addOffset(rect.get_ur(), ur_x_add_offset, ur_y_add_offset);
  return rect;
}

void Utility::minusOffset(PlanarCoord& coord, int32_t x_offset, int32_t y_offset)
{
  coord.set_x((coord.get_x() - x_offset) < 0 ? 0 : (coord.get_x() - x_offset));
  coord.set_y((coord.get_y() - y_offset) < 0 ? 0 : (coord.get_y() - y_offset));
}

void Utility::addOffset(PlanarCoord& coord, int32_t x_offset, int32_t y_offset)
{
  coord.set_x(coord.get_x() + x_offset);
  coord.set_y(coord.get_y() + y_offset);
}

PlanarRect Utility::getSpacingRect(PlanarRect a, PlanarRect b)
{
  int32_t x_spacing = std::max(0, std::max(a.get_ll_x() - b.get_ur_x(), b.get_ll_x() - a.get_ur_x()));
  int32_t y_spacing = std::max(0, std::max(a.get_ll_y() - b.get_ur_y(), b.get_ll_y() - a.get_ur_y()));
  PlanarRect spacing_rect;
  if (x_spacing > 0 && y_spacing > 0) {
    if (a.get_ur_x() < b.get_ll_x()) {
      spacing_rect.set_ll_x(a.get_ur_x());
      spacing_rect.set_ur_x(b.get_ll_x());
    } else {
      spacing_rect.set_ll_x(b.get_ur_x());
      spacing_rect.set_ur_x(a.get_ll_x());
    }
    if (a.get_ur_y() < b.get_ll_y()) {
      spacing_rect.set_ll_y(a.get_ur_y());
      spacing_rect.set_ur_y(b.get_ll_y());
    } else {
      spacing_rect.set_ll_y(b.get_ur_y());
      spacing_rect.set_ur_y(a.get_ll_y());
    }
  } else {
    if (x_spacing == 0) {
      spacing_rect = getOverlap(getEnlargedRect(a, 0, y_spacing), getEnlargedRect(b, 0, y_spacing));
    } else if (y_spacing == 0) {
      spacing_rect = getOverlap(getEnlargedRect(a, x_spacing, 0), getEnlargedRect(b, x_spacing, 0));
    }
  }
  return spacing_rect;
}

PlanarRect Utility::getOverlap(PlanarRect a, PlanarRect b)
{
  int32_t overlap_ll_x = std::max(a.get_ll_x(), b.get_ll_x());
  int32_t overlap_ur_x = std::min(a.get_ur_x(), b.get_ur_x());
  int32_t overlap_ll_y = std::max(a.get_ll_y(), b.get_ll_y());
  int32_t overlap_ur_y = std::min(a.get_ur_y(), b.get_ur_y());

  if (overlap_ll_x > overlap_ur_x || overlap_ll_y > overlap_ur_y) {
    return PlanarRect(0, 0, 0, 0);
  } else {
    return PlanarRect(overlap_ll_x, overlap_ll_y, overlap_ur_x, overlap_ur_y);
  }
}

bool Utility::hasShrinkedRect(PlanarRect rect, int32_t shrinked_size)
{
  addOffset(rect.get_ll(), shrinked_size, shrinked_size);
  minusOffset(rect.get_ur(), shrinked_size, shrinked_size);

  return rect.get_ll_x() <= rect.get_ur_x() && rect.get_ll_y() <= rect.get_ur_y();
}

PlanarRect Utility::getShrinkedRect(PlanarRect rect, int32_t shrinked_size)
{
  return getShrinkedRect(rect, shrinked_size, shrinked_size, shrinked_size, shrinked_size);
}

PlanarRect Utility::getShrinkedRect(PlanarRect rect, int32_t ll_x_add_offset, int32_t ll_y_add_offset, int32_t ur_x_minus_offset, int32_t ur_y_minus_offset)
{
  addOffset(rect.get_ll(), ll_x_add_offset, ll_y_add_offset);
  minusOffset(rect.get_ur(), ur_x_minus_offset, ur_y_minus_offset);
  return rect;
}

PlanarRect Utility::getBoundingBox(const std::vector<PlanarRect>& rect_list)
{
  int32_t ll_x = INT32_MAX;
  int32_t ll_y = INT32_MAX;
  int32_t ur_x = INT32_MIN;
  int32_t ur_y = INT32_MIN;

  for (size_t i = 0; i < rect_list.size(); i++) {
    ll_x = std::min(ll_x, rect_list[i].get_ll_x());
    ll_y = std::min(ll_y, rect_list[i].get_ll_y());
    ur_x = std::max(ur_x, rect_list[i].get_ur_x());
    ur_y = std::max(ur_y, rect_list[i].get_ur_y());
  }
  return PlanarRect(ll_x, ll_y, ur_x, ur_y);
}

PlanarRect Utility::getBoundingBox(const std::vector<PlanarCoord>& coord_list)
{
  PlanarRect bounding_box;
  if (coord_list.empty()) {
    DRCLOG.warn(Loc::current(), "The coord list size is empty!");
  } else {
    int32_t ll_x = INT32_MAX;
    int32_t ll_y = INT32_MAX;
    int32_t ur_x = INT32_MIN;
    int32_t ur_y = INT32_MIN;
    for (size_t i = 0; i < coord_list.size(); i++) {
      const PlanarCoord& coord = coord_list[i];

      ll_x = std::min(ll_x, coord.get_x());
      ll_y = std::min(ll_y, coord.get_y());
      ur_x = std::max(ur_x, coord.get_x());
      ur_y = std::max(ur_y, coord.get_y());
    }
    bounding_box.set_ll(ll_x, ll_y);
    bounding_box.set_ur(ur_x, ur_y);
  }
  return bounding_box;
}

std::vector<PlanarRect> Utility::getOverlap(std::vector<PlanarRect> a_rect_list, std::vector<PlanarRect> b_rect_list)
{
  std::vector<PlanarRect> overlap_rect_list;
  for (const PlanarRect& a_rect : a_rect_list) {
    for (const PlanarRect& b_rect : b_rect_list) {
      if (isClosedOverlap(a_rect, b_rect)) {
        overlap_rect_list.push_back(getOverlap(a_rect, b_rect));
      }
    }
  }
  // rect去重
  std::sort(overlap_rect_list.begin(), overlap_rect_list.end(), CmpPlanarRectByXASC());
  overlap_rect_list.erase(std::unique(overlap_rect_list.begin(), overlap_rect_list.end()), overlap_rect_list.end());
  return overlap_rect_list;
}

int32_t Utility::getFirstDigit(int32_t n)
{
  n = n >= 100000000 ? (n / 100000000) : n;
  n = n >= 10000 ? (n / 10000) : n;
  n = n >= 100 ? (n / 100) : n;
  n = n >= 10 ? (n / 10) : n;
  return n;
}

int32_t Utility::getDigitNum(int32_t n)
{
  int32_t count = 0;

  while (n != 0) {
    n /= 10;
    count++;
  }
  return count;
}

int32_t Utility::getBatchSize(size_t total_size)
{
  return getBatchSize(static_cast<int32_t>(total_size));
}

int32_t Utility::getBatchSize(int32_t total_size)
{
  int32_t batch_size = 10000;

  if (total_size < 0) {
    DRCLOG.error(Loc::current(), "The total of size < 0!");
  } else if (total_size <= 10) {
    batch_size = 5;
  } else {
    batch_size = std::max(5, total_size / 10);
    int32_t factor = static_cast<int32_t>(std::pow(10, getDigitNum(batch_size) - 1));
    batch_size = batch_size / factor * factor;
  }
  return batch_size;
}

bool Utility::isDivisible(int32_t dividend, int32_t divisor)
{
  if (dividend % divisor == 0) {
    return true;
  }
  return false;
}

bool Utility::isDivisible(double dividend, double divisor)
{
  double merchant = dividend / divisor;
  return equalDoubleByError(merchant, static_cast<int32_t>(merchant), DRC_ERROR);
}

int32_t Utility::getOffset(const int32_t start, const int32_t end)
{
  int32_t offset = 0;
  if (start < end) {
    offset = 1;
  } else if (start > end) {
    offset = -1;
  } else {
    DRCLOG.warn(Loc::current(), "The step == 0!");
  }
  return offset;
}

bool Utility::equalDoubleByError(double a, double b, double error)
{
  return std::abs(a - b) < error;
}

double Utility::sigmoid(double value, double threshold)
{
  if (-0.01 < threshold && threshold < 0) {
    threshold = -0.01;
  } else if (0 <= threshold && threshold < 0.01) {
    threshold = 0.01;
  }
  double result = (1.0 / (1 + std::exp(4.5951 * (1 - 2 * value / threshold))));
  if (std::isnan(result)) {
    DRCLOG.error(Loc::current(), "The value is nan!");
  }
  return result;
}

std::ifstream* Utility::getInputFileStream(std::string file_path)
{
  return getFileStream<std::ifstream>(file_path);
}

std::ofstream* Utility::getOutputFileStream(std::string file_path)
{
  return getFileStream<std::ofstream>(file_path);
}

std::string Utility::getBooleanName(bool value)
{
  return value ? "true" : "false";
}

std::string Utility::escapeBackslash(std::string a)
{
  a.erase(std::remove(a.begin(), a.end(), '\\'), a.end());
  return a;
}

bool Utility::isInteger(double a)
{
  return equalDoubleByError(a, static_cast<int32_t>(a), DRC_ERROR);
}

void Utility::checkFile(std::string file_path)
{
  if (!std::filesystem::exists(file_path)) {
    DRCLOG.error(Loc::current(), "The file ", file_path, " does not exist!");
  }
}

void Utility::createDirByFile(std::string file_path)
{
  createDir(dirname((char*) file_path.c_str()));
}

void Utility::createDir(std::string dir_path)
{
  if (!std::filesystem::exists(dir_path)) {
    std::error_code system_error;
    if (!std::filesystem::create_directories(dir_path, system_error)) {
      DRCLOG.error(Loc::current(), "Failed to create directory '", dir_path, "', system_error:", system_error.message());
    }
  }
}

bool Utility::existFile(const std::string& file_path)
{
  return std::filesystem::exists(file_path);
}

void Utility::changePermissions(const std::string& dir_path, std::filesystem::perms permissions)
{
  std::error_code system_error;
  std::filesystem::permissions(dir_path, permissions, system_error);
  if (system_error) {
    DRCLOG.error(Loc::current(), "Failed to change permissions for '", dir_path, "', system_error: ", system_error.message());
  }
}

void Utility::removeDir(const std::string& dir_path)
{
  std::error_code system_error;

  // 检查文件夹是否存在
  if (std::filesystem::exists(dir_path, system_error)) {
    // 尝试删除文件夹
    if (!std::filesystem::remove_all(dir_path, system_error)) {
      DRCLOG.error(Loc::current(), "Failed to remove directory '", dir_path, "'. Error: ", system_error.message());
    }
  }
}

std::string Utility::getFileName(std::string file_path)
{
  size_t loc = file_path.find_last_of('/');
  if (loc == std::string::npos) {
    return file_path;
  }
  return file_path.substr(loc + 1);
}

std::string Utility::getSpaceByTabNum(int32_t tab_num)
{
  return tab_num > 0 ? std::string(static_cast<size_t>(tab_num) * 2, ' ') : "";
}

std::string Utility::getHex(int32_t number)
{
  std::stringstream ss;
  ss << std::hex << number;
  return ss.str();
}

std::vector<std::string> Utility::splitString(std::string a, char tok)
{
  std::vector<std::string> result_list;

  std::stringstream ss(a);
  std::string result_token;
  while (std::getline(ss, result_token, tok)) {
    if (result_token == "") {
      continue;
    }
    result_list.push_back(result_token);
  }
  return result_list;
}

std::string Utility::getCompressedBase62(uint64_t origin)
{
  std::string base = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

  std::string result = "";
  while (origin != 0) {
    result.push_back(base[origin % base.size()]);
    origin /= base.size();
  }
  return result;
}

uint64_t Utility::getDecompressedBase62(std::string origin)
{
  std::string base = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

  std::map<char, uint64_t> base_map;
  for (size_t i = 0; i < base.size(); i++) {
    base_map.insert(std::make_pair(base[i], i));
  }

  uint64_t result = 0;
  for (int32_t i = static_cast<int32_t>(origin.size()) - 1; i >= 0; i--) {
    result = result * base.size() + base_map[origin[i]];
  }
  return result;
}

std::string Utility::getCompressedBase128(uint64_t origin)
{
  std::string result = "";
  while (origin != 0) {
    result.push_back(static_cast<char>(origin % 128));
    origin /= 128;
  }
  return result;
}

uint64_t Utility::getDecompressedBase128(std::string origin)
{
  uint64_t result = 0;
  for (int32_t i = static_cast<int32_t>(origin.size()) - 1; i >= 0; i--) {
    result = result * 128 + static_cast<uint64_t>(origin[i]);
  }
  return result;
}

std::string Utility::getTimestamp()
{
  time_t now = time(nullptr);
  tm* t = localtime(&now);
  char buffer[32];
  strftime(buffer, 32, "%Y%m%d %H:%M:%S", t);
  return buffer;
}

std::string Utility::formatSec(double sec)
{
  int32_t integer_sec = static_cast<int32_t>(std::round(sec));
  int32_t h = integer_sec / 3600;
  int32_t m = (integer_sec % 3600) / 60;
  int32_t s = (integer_sec % 3600) % 60;
  char buffer[32];
  sprintf(buffer, "%02d:%02d:%02d", h, m, s);
  return buffer;
}

std::string Utility::formatByTwoDecimalPlaces(double digit)
{
  char buffer[32];
  sprintf(buffer, "%02.2f", digit);
  return buffer;
}

// private

Utility* Utility::_util_instance = nullptr;

}  // namespace idrc
