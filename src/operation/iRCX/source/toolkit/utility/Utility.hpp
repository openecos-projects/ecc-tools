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
#pragma once

#include "IntervalRange.hpp"
#include "Logger.hpp"
#include "RCXHeader.hpp"
#include "RectRelation.hpp"

namespace ircx {

#define RCXUTIL (ircx::Utility::getInst())

class Utility
{
 public:
  static void initInst();
  static Utility& getInst();
  static void destroyInst();
  // function

#if 1  // boost

  template <class T>
  using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

  template <class...>
  static constexpr bool kAlwaysFalse = false;

  template <class T>
  static constexpr bool isGtlPoint(const T*)
  {
    return false;
  }

  template <class T>
  static constexpr bool isGtlPoint(const boost::polygon::point_data<T>*)
  {
    return true;
  }

  template <class T>
  static constexpr bool isBgPoint(const T*)
  {
    return false;
  }

  template <class T, size_t Dim, class Cs>
  static constexpr bool isBgPoint(const boost::geometry::model::point<T, Dim, Cs>*)
  {
    return true;
  }

  template <class T>
  static constexpr bool isGtlRect(const T*)
  {
    return false;
  }

  template <class T>
  static constexpr bool isGtlRect(const boost::polygon::rectangle_data<T>*)
  {
    return true;
  }

  template <class T>
  static constexpr bool isGtlPolyset90(const T*)
  {
    return false;
  }

  template <class T>
  static constexpr bool isGtlPolyset90(const boost::polygon::polygon_90_set_data<T>*)
  {
    return true;
  }

  template <class T>
  static constexpr bool isBgBox(const T*)
  {
    return false;
  }

  template <class PointT>
  static constexpr bool isBgBox(const boost::geometry::model::box<PointT>*)
  {
    return true;
  }

  template <class T>
  static T x(const boost::polygon::point_data<T>& point)
  {
    return point.x();
  }

  template <class T, size_t Dim, class Cs>
  static T x(const boost::geometry::model::point<T, Dim, Cs>& point)
  {
    static_assert(Dim >= 2, "Boost.Geometry point dimension must be >= 2");
    return boost::geometry::get<0>(point);
  }

  template <class T>
  static T y(const boost::polygon::point_data<T>& point)
  {
    return point.y();
  }

  template <class T, size_t Dim, class Cs>
  static T y(const boost::geometry::model::point<T, Dim, Cs>& point)
  {
    static_assert(Dim >= 2, "Boost.Geometry point dimension must be >= 2");
    return boost::geometry::get<1>(point);
  }

  template <class P>
  using PointCoordT = remove_cvref_t<decltype(x(std::declval<const remove_cvref_t<P>&>()))>;

  template <class P>
  using PointDistanceT = decltype(std::abs(x(std::declval<const P&>()) - x(std::declval<const P&>()))
                                  + std::abs(y(std::declval<const P&>()) - y(std::declval<const P&>())));

  template <class P>
  static remove_cvref_t<P> makePoint(PointCoordT<P> x, PointCoordT<P> y)
  {
    using PP = remove_cvref_t<P>;
    if constexpr (isGtlPoint(static_cast<PP*>(nullptr))) {
      return PP{x, y};
    } else if constexpr (isBgPoint(static_cast<PP*>(nullptr))) {
      PP point = {};
      boost::geometry::set<0>(point, x);
      boost::geometry::set<1>(point, y);
      return point;
    } else {
      static_assert(kAlwaysFalse<PP>, "makePoint(P): unsupported point type");
    }
  }

  template <class T>
  static T minX(const boost::polygon::rectangle_data<T>& rect)
  {
    return rect.get(boost::polygon::WEST);
  }

  template <class T>
  static T minY(const boost::polygon::rectangle_data<T>& rect)
  {
    return rect.get(boost::polygon::SOUTH);
  }

  template <class T>
  static T maxX(const boost::polygon::rectangle_data<T>& rect)
  {
    return rect.get(boost::polygon::EAST);
  }

  template <class T>
  static T maxY(const boost::polygon::rectangle_data<T>& rect)
  {
    return rect.get(boost::polygon::NORTH);
  }

  template <class PointT>
  static typename boost::geometry::coordinate_type<PointT>::type minX(const boost::geometry::model::box<PointT>& box)
  {
    return boost::geometry::get<boost::geometry::min_corner, 0>(box);
  }

  template <class PointT>
  static typename boost::geometry::coordinate_type<PointT>::type minY(const boost::geometry::model::box<PointT>& box)
  {
    return boost::geometry::get<boost::geometry::min_corner, 1>(box);
  }

  template <class PointT>
  static typename boost::geometry::coordinate_type<PointT>::type maxX(const boost::geometry::model::box<PointT>& box)
  {
    return boost::geometry::get<boost::geometry::max_corner, 0>(box);
  }

  template <class PointT>
  static typename boost::geometry::coordinate_type<PointT>::type maxY(const boost::geometry::model::box<PointT>& box)
  {
    return boost::geometry::get<boost::geometry::max_corner, 1>(box);
  }

  template <class R>
  using RectCoordT = remove_cvref_t<decltype(minX(std::declval<const remove_cvref_t<R>&>()))>;

  template <class R>
  static remove_cvref_t<R> makeRect(RectCoordT<R> lx, RectCoordT<R> ly, RectCoordT<R> hx, RectCoordT<R> hy)
  {
    using RR = remove_cvref_t<R>;
    if constexpr (isGtlRect(static_cast<RR*>(nullptr))) {
      return RR{lx, ly, hx, hy};
    } else if constexpr (isBgBox(static_cast<RR*>(nullptr))) {
      RR box = {};
      boost::geometry::set<boost::geometry::min_corner, 0>(box, lx);
      boost::geometry::set<boost::geometry::min_corner, 1>(box, ly);
      boost::geometry::set<boost::geometry::max_corner, 0>(box, hx);
      boost::geometry::set<boost::geometry::max_corner, 1>(box, hy);
      return box;
    } else {
      static_assert(kAlwaysFalse<RR>, "makeRect(R): unsupported rect type");
    }
  }

  template <class P>
  static PointDistanceT<P> manhattanDistance(const P& a, const P& b)
  {
    PointDistanceT<P> dx = std::abs(x(a) - x(b));
    PointDistanceT<P> dy = std::abs(y(a) - y(b));
    return dx + dy;
  }

  template <class P>
  static bool isHorizontalDominant(const P& a, const P& b)
  {
    return std::abs(x(a) - x(b)) >= std::abs(y(a) - y(b));
  }

  template <class P>
  static bool isVerticalDominant(const P& a, const P& b)
  {
    return !isHorizontalDominant(a, b);
  }

  template <class P>
  static bool isLowerLeft(const P& a, const P& b)
  {
    return x(a) <= x(b) && y(a) <= y(b);
  }

  template <class P>
  static bool isUpperRight(const P& a, const P& b)
  {
    return x(a) >= x(b) && y(a) >= y(b);
  }

  template <class R>
  static RectCoordT<R> deltaX(const R& r)
  {
    return maxX(r) - minX(r);
  }

  template <class R>
  static RectCoordT<R> deltaY(const R& r)
  {
    return maxY(r) - minY(r);
  }

  template <class R>
  static RectCoordT<R> centerX(const R& r)
  {
    using T = RectCoordT<R>;
    return minX(r) + (maxX(r) - minX(r)) / T{2};
  }

  template <class R>
  static RectCoordT<R> centerY(const R& r)
  {
    using T = RectCoordT<R>;
    return minY(r) + (maxY(r) - minY(r)) / T{2};
  }

  template <class R>
  static boost::polygon::point_data<RectCoordT<R>> center(const R& r)
  {
    using T = RectCoordT<R>;
    return boost::polygon::point_data<T>{centerX(r), centerY(r)};
  }

  template <class R>
  static bool isHorizontalDominant(const R& r)
  {
    return deltaX(r) >= deltaY(r);
  }

  template <class R>
  static bool isVerticalDominant(const R& r)
  {
    return deltaX(r) < deltaY(r);
  }

  template <class R, class P>
  static bool rectContainsPoint(const R& r, const P& p)
  {
    return x(p) >= minX(r) && x(p) <= maxX(r) && y(p) >= minY(r) && y(p) <= maxY(r);
  }

  template <class R>
  static remove_cvref_t<R> getBoundingRect(const R& first_rect, const R& second_rect)
  {
    using Rect = remove_cvref_t<R>;
    return makeRect<Rect>(std::min(minX(first_rect), minX(second_rect)), std::min(minY(first_rect), minY(second_rect)),
                          std::max(maxX(first_rect), maxX(second_rect)), std::max(maxY(first_rect), maxY(second_rect)));
  }

  template <class Shape>
  static double area(const Shape& s)
  {
    using S = remove_cvref_t<Shape>;

    if constexpr (isBgBox(static_cast<S*>(nullptr))) {
      return static_cast<double>(boost::geometry::area(s));
    } else if constexpr (isGtlRect(static_cast<S*>(nullptr)) || isGtlPolyset90(static_cast<S*>(nullptr))) {
      return static_cast<double>(boost::polygon::area(s));
    } else {
      static_assert(kAlwaysFalse<S>,
                    "area(Shape): unsupported Shape type "
                    "(expected boost::geometry::model::box<...>, boost::polygon::rectangle_data<...>, "
                    "or boost::polygon::polygon_90_set_data<...>)");
      return 0.0;
    }
  }

  template <class A, class B>
  static bool intersects(const A& a, const B& b)
  {
    using AA = remove_cvref_t<A>;
    using BB = remove_cvref_t<B>;

    if constexpr (isBgBox(static_cast<AA*>(nullptr)) && isBgBox(static_cast<BB*>(nullptr))) {
      return boost::geometry::intersects(a, b);
    } else if constexpr (isGtlRect(static_cast<AA*>(nullptr)) && isGtlRect(static_cast<BB*>(nullptr))) {
      return boost::polygon::intersects(a, b);
    } else {
      static_assert(kAlwaysFalse<AA, BB>,
                    "intersects(A,B): unsupported type combination "
                    "(supported: boost::geometry::box vs boost::geometry::box, boost::polygon::rect vs boost::polygon::rect)");
      return false;
    }
  }

  template <class A, class B>
  static bool hasAreaOverlap(const A& a, const B& b)
  {
    using TA = RectCoordT<A>;
    using TB = RectCoordT<B>;
    using CommonT = std::common_type_t<TA, TB>;

    CommonT lx = std::max<CommonT>(static_cast<CommonT>(minX(a)), static_cast<CommonT>(minX(b)));
    CommonT ly = std::max<CommonT>(static_cast<CommonT>(minY(a)), static_cast<CommonT>(minY(b)));
    CommonT hx = std::min<CommonT>(static_cast<CommonT>(maxX(a)), static_cast<CommonT>(maxX(b)));
    CommonT hy = std::min<CommonT>(static_cast<CommonT>(maxY(a)), static_cast<CommonT>(maxY(b)));

    return (lx < hx) && (ly < hy);
  }

  template <class R>
  static std::optional<remove_cvref_t<R>> intersection(const R& a, const R& b)
  {
    using RR = remove_cvref_t<R>;
    using T = RectCoordT<RR>;

    T lx = std::max(minX(a), minX(b));
    T ly = std::max(minY(a), minY(b));
    T hx = std::min(maxX(a), maxX(b));
    T hy = std::min(maxY(a), maxY(b));

    if (lx < hx && ly < hy) {
      return makeRect<RR>(lx, ly, hx, hy);
    }
    return std::nullopt;
  }

  template <class OutRect, class A, class B>
  static std::optional<remove_cvref_t<OutRect>> intersectionAs(const A& a, const B& b)
  {
    using RR = remove_cvref_t<OutRect>;
    using T = RectCoordT<RR>;
    using CommonCoordT = std::common_type_t<RectCoordT<A>, RectCoordT<B>>;

    CommonCoordT lx_raw = std::max(static_cast<CommonCoordT>(minX(a)), static_cast<CommonCoordT>(minX(b)));
    CommonCoordT ly_raw = std::max(static_cast<CommonCoordT>(minY(a)), static_cast<CommonCoordT>(minY(b)));
    CommonCoordT hx_raw = std::min(static_cast<CommonCoordT>(maxX(a)), static_cast<CommonCoordT>(maxX(b)));
    CommonCoordT hy_raw = std::min(static_cast<CommonCoordT>(maxY(a)), static_cast<CommonCoordT>(maxY(b)));

    if (lx_raw < hx_raw && ly_raw < hy_raw) {
      return makeRect<RR>(static_cast<T>(lx_raw), static_cast<T>(ly_raw), static_cast<T>(hx_raw), static_cast<T>(hy_raw));
    }
    return std::nullopt;
  }

  template <class R, class W>
  static std::optional<remove_cvref_t<R>> clip(const R& r, const W& win)
  {
    return intersectionAs<remove_cvref_t<R>>(r, win);
  }

  template <class OutRect, class R, class W>
  static std::optional<remove_cvref_t<OutRect>> clipAs(const R& r, const W& win)
  {
    return intersectionAs<remove_cvref_t<OutRect>>(r, win);
  }

  template <class R>
  static boost::geometry::model::box<boost::geometry::model::point<RectCoordT<R>, 2, boost::geometry::cs::cartesian>> toBox(const R& r)
  {
    using T = RectCoordT<R>;
    using P = boost::geometry::model::point<T, 2, boost::geometry::cs::cartesian>;
    using B = boost::geometry::model::box<P>;
    return B(P(minX(r), minY(r)), P(maxX(r), maxY(r)));
  }

  template <class R>
  static boost::polygon::rectangle_data<RectCoordT<R>> toRect(const R& r)
  {
    using T = RectCoordT<R>;
    return boost::polygon::rectangle_data<T>(minX(r), minY(r), maxX(r), maxY(r));
  }

  template <class OutRect, class InRect, class Div>
  static OutRect divideRectAs(const InRect& r, Div divisor)
  {
    using InCoord = RectCoordT<InRect>;
    using OutCoord = RectCoordT<OutRect>;

    static_assert(std::is_integral_v<InCoord>, "divide_rect_as: InRect must use integral coordinates");
    static_assert(std::is_floating_point_v<OutCoord>, "divide_rect_as: OutRect must use floating-point coordinates");

    if (divisor == 0) {
      RCXLOG.error(Loc::current(), "The divisor must not be zero!");
    }

    OutCoord d = static_cast<OutCoord>(divisor);

    return makeRect<OutRect>(static_cast<OutCoord>(minX(r)) / d, static_cast<OutCoord>(minY(r)) / d, static_cast<OutCoord>(maxX(r)) / d,
                             static_cast<OutCoord>(maxY(r)) / d);
  }

  template <class P>
  static remove_cvref_t<P> translatePoint(const P& p, PointCoordT<P> dx, PointCoordT<P> dy)
  {
    using PP = remove_cvref_t<P>;
    return makePoint<PP>(x(p) + dx, y(p) + dy);
  }

  template <class P>
  static boost::polygon::rectangle_data<PointCoordT<P>> rectAround(const P& p, PointCoordT<P> d)
  {
    using T = PointCoordT<P>;
    return boost::polygon::rectangle_data<T>{x(p) - d, y(p) - d, x(p) + d, y(p) + d};
  }

  template <class P>
  static boost::polygon::rectangle_data<PointCoordT<P>> boxAround(const P& p, PointCoordT<P> d)
  {
    return rectAround(p, d);
  }

  template <class R>
  static remove_cvref_t<R> translateRect(const R& r, RectCoordT<R> dx, RectCoordT<R> dy)
  {
    using RR = remove_cvref_t<R>;
    return makeRect<RR>(minX(r) + dx, minY(r) + dy, maxX(r) + dx, maxY(r) + dy);
  }

  template <class R>
  static remove_cvref_t<R> inflateX(const R& r, RectCoordT<R> dx)
  {
    using RR = remove_cvref_t<R>;
    return makeRect<RR>(minX(r) - dx, minY(r), maxX(r) + dx, maxY(r));
  }

  template <class R>
  static remove_cvref_t<R> inflateY(const R& r, RectCoordT<R> dy)
  {
    using RR = remove_cvref_t<R>;
    return makeRect<RR>(minX(r), minY(r) - dy, maxX(r), maxY(r) + dy);
  }

  template <class R>
  static remove_cvref_t<R> inflate(const R& r, RectCoordT<R> d)
  {
    using RR = remove_cvref_t<R>;
    return makeRect<RR>(minX(r) - d, minY(r) - d, maxX(r) + d, maxY(r) + d);
  }

  static std::vector<GTLRectInt> polysetToRects(const GTLPolySetInt& ps)
  {
    std::vector<GTLRectInt> rects;
    ps.get_rectangles(rects);
    return rects;
  }

  static std::vector<GTLRectDBL> polysetToRects(const GTLPolySetDBL& ps)
  {
    std::vector<GTLRectDBL> rects;
    ps.get_rectangles(rects);
    return rects;
  }

  static GTLPolySetInt rectsToPolyset(const std::vector<GTLRectInt>& rects)
  {
    GTLPolySetInt ps;
    for (const GTLRectInt& rect : rects) {
      ps += rect;
    }
    return ps;
  }

  static GTLPolySetDBL rectsToPolyset(const std::vector<GTLRectDBL>& rects)
  {
    GTLPolySetDBL ps;
    for (const GTLRectDBL& rect : rects) {
      ps += rect;
    }
    return ps;
  }

  template <class Rect>
  static std::optional<Rect> rectsToBbox(const std::vector<Rect>& rects)
  {
    if (rects.empty()) {
      return std::nullopt;
    }

    using T = RectCoordT<Rect>;

    T minx = minX(rects[0]);
    T maxx = maxX(rects[0]);
    T miny = minY(rects[0]);
    T maxy = maxY(rects[0]);

    for (int32_t rect_idx = 1; rect_idx < static_cast<int32_t>(rects.size()); ++rect_idx) {
      minx = std::min(minx, minX(rects[rect_idx]));
      maxx = std::max(maxx, maxX(rects[rect_idx]));
      miny = std::min(miny, minY(rects[rect_idx]));
      maxy = std::max(maxy, maxY(rects[rect_idx]));
    }

    return makeRect<Rect>(minx, miny, maxx, maxy);
  }

  template <typename T>
  static RectRelation<T> rectRelation(T a_llx, T a_lly, T a_urx, T a_ury, T b_llx, T b_lly, T b_urx, T b_ury)
  {
    RectRelation<T> relation;
    relation.set_overlap_x(std::max(T{}, std::min(a_urx, b_urx) - std::max(a_llx, b_llx)));
    relation.set_overlap_y(std::max(T{}, std::min(a_ury, b_ury) - std::max(a_lly, b_lly)));
    relation.set_gap_x(std::max(T{}, std::max(a_llx, b_llx) - std::min(a_urx, b_urx)));
    relation.set_gap_y(std::max(T{}, std::max(a_lly, b_lly) - std::min(a_ury, b_ury)));
    return relation;
  }

  template <typename PointT, typename RectT>
  static double pointToRectDistance2(PointT px, PointT py, RectT llx, RectT lly, RectT urx, RectT ury)
  {
    double clamped_x
        = std::clamp(static_cast<double>(px), static_cast<double>(std::min(llx, urx)), static_cast<double>(std::max(llx, urx)));
    double clamped_y
        = std::clamp(static_cast<double>(py), static_cast<double>(std::min(lly, ury)), static_cast<double>(std::max(lly, ury)));
    double dx = static_cast<double>(px) - clamped_x;
    double dy = static_cast<double>(py) - clamped_y;
    return dx * dx + dy * dy;
  }

#endif

#if 1  // math

  static int32_t ceilDivPositive(int32_t value, int32_t divisor)
  {
    if (value <= 0 || divisor <= 0) {
      return 0;
    }
    return (value + divisor - 1) / divisor;
  }

#endif

#if 1  // omp

  static int32_t getThreadNum(int32_t work_item_num, int32_t requested_thread_num)
  {
    if (work_item_num <= 0) {
      return 1;
    }
    return std::min(std::max(1, requested_thread_num), work_item_num);
  }

#endif

#if 1  // std数据结构工具函数

  template <typename T>
  static void normalizeInterval(T& start, T& end)
  {
    if (start > end) {
      std::swap(start, end);
    }
  }

  template <typename T>
  static void sortAndUnique(std::vector<T>& list)
  {
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
  }

  template <typename T>
  static bool isIntervalValid(T start, T end)
  {
    return start < end;
  }

  template <typename T>
  static bool isIntervalOverlap(T first_start, T first_end, T second_start, T second_end)
  {
    normalizeInterval(first_start, first_end);
    normalizeInterval(second_start, second_end);
    return std::max(first_start, second_start) < std::min(first_end, second_end);
  }

  template <typename T>
  static IntervalRange<T> getIntervalIntersection(T first_start, T first_end, T second_start, T second_end)
  {
    normalizeInterval(first_start, first_end);
    normalizeInterval(second_start, second_end);
    return IntervalRange<T>(std::max(first_start, second_start), std::min(first_end, second_end));
  }

  template <typename T>
  static T getIntervalMidpoint(T first_coord, T second_coord)
  {
    return first_coord + (second_coord - first_coord) / 2;
  }

  template <typename IntervalT>
  using IntervalCoordT = remove_cvref_t<decltype(std::declval<const IntervalT&>().get_start())>;

  template <typename IntervalT>
  static std::vector<IntervalT> subtractInterval(const std::vector<IntervalT>& interval_list, IntervalCoordT<IntervalT> cut_start,
                                                 IntervalCoordT<IntervalT> cut_end)
  {
    using Coordinate = IntervalCoordT<IntervalT>;

    std::vector<IntervalT> next_interval_list;
    normalizeInterval(cut_start, cut_end);

    for (const IntervalT& interval : interval_list) {
      if (!isIntervalOverlap(interval.get_start(), interval.get_end(), cut_start, cut_end)) {
        next_interval_list.push_back(interval);
        continue;
      }

      IntervalT left;
      left.set_start(interval.get_start());
      left.set_end(static_cast<Coordinate>(std::min(interval.get_end(), cut_start)));
      if (isIntervalValid(left.get_start(), left.get_end())) {
        next_interval_list.push_back(left);
      }

      IntervalT right;
      right.set_start(static_cast<Coordinate>(std::max(interval.get_start(), cut_end)));
      right.set_end(interval.get_end());
      if (isIntervalValid(right.get_start(), right.get_end())) {
        next_interval_list.push_back(right);
      }
    }

    return next_interval_list;
  }

  template <typename IntervalT, typename Mergeable>
  static std::vector<IntervalT> clipInterval(const std::vector<IntervalT>& interval_list, IntervalCoordT<IntervalT> clip_start,
                                             IntervalCoordT<IntervalT> clip_end, Mergeable mergeable)
  {
    std::vector<IntervalT> clipped_interval_list;
    if (!isIntervalValid(clip_start, clip_end)) {
      return clipped_interval_list;
    }

    for (const IntervalT& interval : interval_list) {
      IntervalCoordT<IntervalT> start = std::max(clip_start, interval.get_start());
      IntervalCoordT<IntervalT> end = std::min(clip_end, interval.get_end());
      if (!isIntervalValid(start, end)) {
        continue;
      }

      if (!clipped_interval_list.empty() && clipped_interval_list.back().get_end() == start
          && mergeable(clipped_interval_list.back(), interval)) {
        clipped_interval_list.back().set_end(end);
        continue;
      }

      IntervalT clipped_interval = interval;
      clipped_interval.set_start(start);
      clipped_interval.set_end(end);
      clipped_interval_list.push_back(std::move(clipped_interval));
    }

    return clipped_interval_list;
  }

  static std::string getAbsolutePath(const std::filesystem::path& directory_path, const std::string& file_path)
  {
    std::filesystem::path path(file_path);
    if (path.is_absolute()) {
      return path.string();
    }
    return std::filesystem::absolute(directory_path / path).string();
  }

  template <typename T, typename... Args>
  static std::string getString(T value, Args... args)
  {
    std::stringstream oss;
    pushStream(oss, value, args...);
    std::string string = oss.str();
    oss.clear();
    return string;
  }

  static std::string getUpperString(std::string text)
  {
    for (char& character : text) {
      character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return text;
  }

  static std::string getTrimmedString(std::string text)
  {
    size_t first_pos = text.find_first_not_of(" \t\r\n");
    if (first_pos == std::string::npos) {
      return "";
    }
    size_t last_pos = text.find_last_not_of(" \t\r\n");
    return text.substr(first_pos, last_pos - first_pos + 1);
  }

  static bool getDouble(std::string& text, double& number)
  {
    char* end_ptr = nullptr;
    const char* start_ptr = text.c_str();
    number = std::strtod(start_ptr, &end_ptr);
    return start_ptr != end_ptr && *end_ptr == '\0';
  }

  template <typename Stream, typename T, typename... Args>
  static void pushStream(Stream* stream, T value, Args... args)
  {
    pushStream(*stream, value, args...);
  }

  template <typename Stream, typename T, typename... Args>
  static void pushStream(Stream& stream, T value, Args... args)
  {
    stream << value;
    pushStream(stream, args...);
  }

  template <typename Stream, typename T>
  static void pushStream(Stream& stream, T value)
  {
    stream << value;
  }

  static std::string formatSec(double sec)
  {
    std::string sec_string;

    int32_t integer_sec = static_cast<int32_t>(std::round(sec));
    int32_t h = integer_sec / 3600;
    int32_t m = (integer_sec % 3600) / 60;
    int32_t s = (integer_sec % 3600) % 60;
    char* buffer = new char[32];
    sprintf(buffer, "%02d:%02d:%02d", h, m, s);
    sec_string = buffer;
    delete[] buffer;
    buffer = nullptr;

    return sec_string;
  }

  static std::string formatByTwoDecimalPlaces(double digit)
  {
    std::string digit_string;

    char* buffer = new char[32];
    sprintf(buffer, "%02.2f", digit);
    digit_string = buffer;
    delete[] buffer;
    buffer = nullptr;

    return digit_string;
  }

  template <typename T>
  static T getConfigValue(std::map<std::string, std::any>& config_map, const std::string& config_name, const T& default_value)
  {
    T value;
    if (exist(config_map, config_name)) {
      value = std::any_cast<T>(config_map[config_name]);
    } else {
      RCXLOG.warn(Loc::current(), "The config '", config_name, "' uses the default value!");
      value = default_value;
    }
    return value;
  }

  template <typename Key>
  static bool exist(const std::vector<Key>& vector, const Key& key)
  {
    for (int32_t value_idx = 0; value_idx < static_cast<int32_t>(vector.size()); ++value_idx) {
      if (vector[value_idx] == key) {
        return true;
      }
    }
    return false;
  }

  template <typename Key, typename Compare = std::less<Key>>
  static bool exist(const std::set<Key, Compare>& set, const Key& key)
  {
    return (set.find(key) != set.end());
  }

  template <typename Key, typename Hash = std::hash<Key>>
  static bool exist(const std::unordered_set<Key, Hash>& set, const Key& key)
  {
    return (set.find(key) != set.end());
  }

  template <typename Key, typename Value, typename Compare = std::less<Key>>
  static bool exist(const std::map<Key, Value, Compare>& map, const Key& key)
  {
    return (map.find(key) != map.end());
  }

  template <typename Key, typename Value, typename Hash = std::hash<Key>>
  static bool exist(const std::unordered_map<Key, Value, Hash>& map, const Key& key)
  {
    return (map.find(key) != map.end());
  }

  static void createDirByFile(std::string file_path) { createDir(dirname((char*) file_path.c_str())); }

  static void createDir(std::string dir_path)
  {
    if (!std::filesystem::exists(dir_path)) {
      std::error_code system_error;
      if (!std::filesystem::create_directories(dir_path, system_error)) {
        RCXLOG.error(Loc::current(), "Failed to create directory '", dir_path, "', system_error:", system_error.message());
      }
    }
  }

  static void removeDir(const std::string& dir_path)
  {
    std::error_code system_error;

    if (std::filesystem::exists(dir_path, system_error)) {
      if (!std::filesystem::remove_all(dir_path, system_error)) {
        RCXLOG.error(Loc::current(), "Failed to remove directory '", dir_path, "'. Error: ", system_error.message());
      }
    }
  }

  static std::string getSpaceByTabNum(int32_t tab_num)
  {
    std::string all = "";
    for (int32_t i = 0; i < tab_num; i++) {
      all += "  ";
    }
    return all;
  }

  static std::ifstream* getInputFileStream(std::string file_path) { return getFileStream<std::ifstream>(file_path); }

  static std::ofstream* getOutputFileStream(std::string file_path) { return getFileStream<std::ofstream>(file_path); }

  template <typename T>
  static T* getFileStream(std::string file_path)
  {
    T* file = new T(file_path);
    if (!file->is_open()) {
      RCXLOG.error(Loc::current(), "Failed to open file '", file_path, "'!");
    }
    return file;
  }

  template <typename T>
  static void closeFileStream(T* t)
  {
    if (t != nullptr) {
      t->close();
      delete t;
    }
  }

#endif

 private:
  static Utility* _util_instance;

  Utility() = default;
  Utility(const Utility& other) = delete;
  Utility(Utility&& other) = delete;
  ~Utility() = default;
  Utility& operator=(const Utility& other) = delete;
  Utility& operator=(Utility&& other) = delete;
  // function
};

}  // namespace ircx
