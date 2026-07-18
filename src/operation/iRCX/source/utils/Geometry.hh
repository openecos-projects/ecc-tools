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
/**
 * @file Geometry.hh
 * @brief Generic geometry adapters for GTL and Boost geometry types.
 */
#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <optional>
#include <type_traits>
#include <vector>

#include "Types.hh"

namespace ircx {
namespace geom {

// ============================================================
//  Internal helpers
// ============================================================

template <class T>
struct dependent_false : std::false_type {};

template <class A, class B>
struct dependent_false2 : std::false_type {};

template <class T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

// ------------------------------------------------------------
// Type detectors for common Boost / GTL geometry types
// ------------------------------------------------------------

template <class T>
struct is_gtl_point : std::false_type {};
template <class C>
struct is_gtl_point<gtl::point_data<C>> : std::true_type {};

template <class T>
struct is_bg_point : std::false_type {};
template <class C, Size Dim, class Cs>
struct is_bg_point<bg::model::point<C, Dim, Cs>> : std::true_type {};

template <class T>
struct is_gtl_rect : std::false_type {};
template <class C>
struct is_gtl_rect<gtl::rectangle_data<C>> : std::true_type {};

template <class T>
struct is_gtl_polyset90 : std::false_type {};
template <class C>
struct is_gtl_polyset90<gtl::polygon_90_set_data<C>> : std::true_type {};

template <class T>
struct is_bg_box : std::false_type {};
template <class P>
struct is_bg_box<bg::model::box<P>> : std::true_type {};

// ============================================================
//  point_traits: unify X/Y for GTL point and BG point
// ============================================================

template <class P>
struct point_traits {
  static_assert(dependent_false<P>::value,
                "point_traits<P> is not specialized for this point type");
};

// gtl::point_data<T>
template <class T>
struct point_traits<gtl::point_data<T>> {
  using coord_t = T;
  static auto x(const gtl::point_data<T>& p) -> coord_t { return p.x(); }
  static auto y(const gtl::point_data<T>& p) -> coord_t { return p.y(); }
};

// bg::model::point<T, Dim, Cs>
template <class T, Size Dim, class Cs>
struct point_traits<bg::model::point<T, Dim, Cs>> {
  static_assert(Dim >= 2, "Boost.Geometry point dimension must be >= 2");
  using coord_t = T;
  static auto x(const bg::model::point<T, Dim, Cs>& p) -> coord_t
  {
    return bg::get<0>(p);
  }
  static auto y(const bg::model::point<T, Dim, Cs>& p) -> coord_t
  {
    return bg::get<1>(p);
  }
};

template <class P>
using PointCoordT = typename point_traits<remove_cvref_t<P>>::coord_t;

// Unified point accessors

template <class P>
inline auto x(const P& p) -> PointCoordT<P> {
  return point_traits<remove_cvref_t<P>>::x(p);
}

template <class P>
inline auto y(const P& p) -> PointCoordT<P> {
  return point_traits<remove_cvref_t<P>>::y(p);
}

// ============================================================
//  point_make_traits: unify point construction
// ============================================================

template <class P>
struct point_make_traits {
  static_assert(dependent_false<P>::value,
                "point_make_traits<P> is not specialized for this point type");
};

// gtl::point_data<T>
template <class T>
struct point_make_traits<gtl::point_data<T>> {
  using coord_t = T;
  static auto make(coord_t x,
                   coord_t y) -> gtl::point_data<T>
  {
    return gtl::point_data<T>{x, y};
  }
};

// bg::model::point<T, Dim, Cs>
template <class T, Size Dim, class Cs>
struct point_make_traits<bg::model::point<T, Dim, Cs>> {
  static_assert(Dim >= 2, "Boost.Geometry point dimension must be >= 2");
  using coord_t = T;

  static auto make(coord_t x,
                   coord_t y) -> bg::model::point<T, Dim, Cs>
  {
    bg::model::point<T, Dim, Cs> p{};
    bg::set<0>(p, x);
    bg::set<1>(p, y);
    return p;
  }
};

template <class P>
inline auto makePoint(PointCoordT<P> x,
                       PointCoordT<P> y) -> remove_cvref_t<P> {
  using PP = remove_cvref_t<P>;
  return point_make_traits<PP>::make(x, y);
}

// ============================================================
//  rect_traits: unify Min/Max for GTL rect and BG box
// ============================================================

template <class R>
struct rect_traits {
  static_assert(dependent_false<R>::value,
                "rect_traits<R> is not specialized for this rect/box type");
};

// gtl::rectangle_data<T>
template <class T>
struct rect_traits<gtl::rectangle_data<T>> {
  using coord_t = T;
  static auto minX(const gtl::rectangle_data<T>& r) -> coord_t { return r.get(gtl::WEST); }
  static auto minY(const gtl::rectangle_data<T>& r) -> coord_t { return r.get(gtl::SOUTH); }
  static auto maxX(const gtl::rectangle_data<T>& r) -> coord_t { return r.get(gtl::EAST); }
  static auto maxY(const gtl::rectangle_data<T>& r) -> coord_t { return r.get(gtl::NORTH); }
};

// bg::model::box<PointT>
template <class PointT>
struct rect_traits<bg::model::box<PointT>> {
  using coord_t = typename bg::coordinate_type<PointT>::type;
  static auto minX(const bg::model::box<PointT>& b) -> coord_t
  {
    return bg::get<bg::min_corner, 0>(b);
  }
  static auto minY(const bg::model::box<PointT>& b) -> coord_t
  {
    return bg::get<bg::min_corner, 1>(b);
  }
  static auto maxX(const bg::model::box<PointT>& b) -> coord_t
  {
    return bg::get<bg::max_corner, 0>(b);
  }
  static auto maxY(const bg::model::box<PointT>& b) -> coord_t
  {
    return bg::get<bg::max_corner, 1>(b);
  }
};

template <class R>
using RectCoordT = typename rect_traits<remove_cvref_t<R>>::coord_t;

// Unified rect accessors

template <class R>
inline auto minX(const R& r) -> RectCoordT<R> {
  return rect_traits<remove_cvref_t<R>>::minX(r);
}

template <class R>
inline auto minY(const R& r) -> RectCoordT<R> {
  return rect_traits<remove_cvref_t<R>>::minY(r);
}

template <class R>
inline auto maxX(const R& r) -> RectCoordT<R> {
  return rect_traits<remove_cvref_t<R>>::maxX(r);
}

template <class R>
inline auto maxY(const R& r) -> RectCoordT<R> {
  return rect_traits<remove_cvref_t<R>>::maxY(r);
}

// ============================================================
//  rect_make_traits: unify rect/box construction
// ============================================================

template <class R>
struct rect_make_traits {
  static_assert(dependent_false<R>::value,
                "rect_make_traits<R> is not specialized for this rect/box type");
};

// gtl::rectangle_data<T> : {lx, ly, hx, hy}
template <class T>
struct rect_make_traits<gtl::rectangle_data<T>> {
  using coord_t = T;
  static auto make(coord_t lx,
                   coord_t ly,
                   coord_t hx,
                   coord_t hy) -> gtl::rectangle_data<T>
  {
    return {lx, ly, hx, hy};
  }
};

// bg::model::box<PointT> : {Point(lx,ly), Point(hx,hy)}
template <class PointT>
struct rect_make_traits<bg::model::box<PointT>> {
  using coord_t = typename bg::coordinate_type<PointT>::type;
  static auto make(coord_t lx,
                   coord_t ly,
                   coord_t hx,
                   coord_t hy) -> bg::model::box<PointT>
  {
    return {PointT(lx, ly), PointT(hx, hy)};
  }
};

template <class R>
inline auto makeRect(RectCoordT<R> lx,
                      RectCoordT<R> ly,
                      RectCoordT<R> hx,
                      RectCoordT<R> hy)
    -> remove_cvref_t<R> {
  using RR = remove_cvref_t<R>;
  return rect_make_traits<RR>::make(lx, ly, hx, hy);
}

// ============================================================
//  Point utilities (generic)
// ============================================================

template <class P>
inline auto manhattanDistance(const P& a,
                               const P& b)
    -> decltype(std::abs(geom::x(a) - geom::x(b)) + std::abs(geom::y(a) - geom::y(b))) {
  const auto dx = std::abs(geom::x(a) - geom::x(b));
  const auto dy = std::abs(geom::y(a) - geom::y(b));
  return dx + dy;
}

template <class P>
inline auto isHorizontalDominant(const P& a,
                                   const P& b) -> bool
{
  return std::abs(geom::x(a) - geom::x(b)) >= std::abs(geom::y(a) - geom::y(b));
}

template <class P>
inline auto isVerticalDominant(const P& a,
                                 const P& b) -> bool
{
  return !isHorizontalDominant(a, b);
}

template <class P>
inline auto isLowerLeft(const P& a,
                          const P& b) -> bool
{
  return geom::x(a) <= geom::x(b) && geom::y(a) <= geom::y(b);
}

template <class P>
inline auto isUpperRight(const P& a,
                           const P& b) -> bool
{
  return geom::x(a) >= geom::x(b) && geom::y(a) >= geom::y(b);
}

// ============================================================
//  Rect utilities (generic)
// ============================================================

template <class R>
inline auto deltaX(const R& r) -> RectCoordT<R> {
  return maxX(r) - minX(r);
}

template <class R>
inline auto deltaY(const R& r) -> RectCoordT<R> {
  return maxY(r) - minY(r);
}

// Use Min + (Max - Min) / 2 to reduce overflow risk for integral coordinates.
template <class R>
inline auto centerX(const R& r) -> RectCoordT<R> {
  using T = RectCoordT<R>;
  return minX(r) + (maxX(r) - minX(r)) / T{2};
}

template <class R>
inline auto centerY(const R& r) -> RectCoordT<R> {
  using T = RectCoordT<R>;
  return minY(r) + (maxY(r) - minY(r)) / T{2};
}

// center always returns gtl::point_data<coord_t>.
// Floating-point rect keeps precision; integral rect keeps integral midpoint semantics.
template <class R>
inline auto center(const R& r) -> gtl::point_data<RectCoordT<R>> {
  using T = RectCoordT<R>;
  return gtl::point_data<T>{centerX(r), centerY(r)};
}

template <class R>
inline auto isHorizontalDominant(const R& r) -> bool
{
  return deltaX(r) >= deltaY(r);
}

template <class R>
inline auto isVerticalDominant(const R& r) -> bool
{
  return deltaX(r) < deltaY(r);
}

template <class R, class P>
inline auto rectContainsPoint(const R& r,
                                const P& p) -> bool
{
  return geom::x(p) >= minX(r) && geom::x(p) <= maxX(r) &&
         geom::y(p) >= minY(r) && geom::y(p) <= maxY(r);
}

// ============================================================
//  area / intersects
// ============================================================

template <class Shape>
inline auto area(const Shape& s) -> F64
{
  using S = remove_cvref_t<Shape>;

  if constexpr (is_bg_box<S>::value) {
    return static_cast<F64>(bg::area(s));
  } else if constexpr (is_gtl_rect<S>::value || is_gtl_polyset90<S>::value) {
    return static_cast<F64>(gtl::area(s));
  } else {
    static_assert(dependent_false<S>::value,
                  "area(Shape): unsupported Shape type "
                  "(expected bg::model::box<...>, gtl::rectangle_data<...>, "
                  "or gtl::polygon_90_set_data<...>)");
    return 0.0;
  }
}

// Note:
// - This function delegates to the underlying library semantics.
// - If you need strict positive-area overlap, use has_area_overlap().
template <class A, class B>
inline auto intersects(const A& a,
                       const B& b) -> bool
{
  using AA = remove_cvref_t<A>;
  using BB = remove_cvref_t<B>;

  if constexpr (is_bg_box<AA>::value && is_bg_box<BB>::value) {
    return bg::intersects(a, b);
  } else if constexpr (is_gtl_rect<AA>::value && is_gtl_rect<BB>::value) {
    return gtl::intersects(a, b);
  } else {
    static_assert(dependent_false2<AA, BB>::value,
                  "intersects(A,B): unsupported type combination "
                  "(supported: bg::box vs bg::box, gtl::rect vs gtl::rect)");
    return false;
  }
}

// Strict positive-area overlap for any two rect-like objects.
template <class A, class B>
inline auto hasAreaOverlap(const A& a,
                             const B& b) -> bool
{
  using TA = RectCoordT<A>;
  using TB = RectCoordT<B>;
  using CommonT = std::common_type_t<TA, TB>;

  const CommonT lx = std::max<CommonT>(
      static_cast<CommonT>(minX(a)),
      static_cast<CommonT>(minX(b)));
  const CommonT ly = std::max<CommonT>(
      static_cast<CommonT>(minY(a)),
      static_cast<CommonT>(minY(b)));
  const CommonT hx = std::min<CommonT>(
      static_cast<CommonT>(maxX(a)),
      static_cast<CommonT>(maxX(b)));
  const CommonT hy = std::min<CommonT>(
      static_cast<CommonT>(maxY(a)),
      static_cast<CommonT>(maxY(b)));

  return (lx < hx) && (ly < hy);
}

// ============================================================
//  Generic intersection / clipping for rect-like objects
// ============================================================

// Same-type intersection: returns std::nullopt for zero-area intersection.
template <class R>
inline auto intersection(const R& a,
                         const R& b) -> std::optional<remove_cvref_t<R>> {
  using RR = remove_cvref_t<R>;
  using T  = RectCoordT<RR>;

  const T lx = std::max(minX(a), minX(b));
  const T ly = std::max(minY(a), minY(b));
  const T hx = std::min(maxX(a), maxX(b));
  const T hy = std::min(maxY(a), maxY(b));

  if (lx < hx && ly < hy) {
    return makeRect<RR>(lx, ly, hx, hy);
  }
  return std::nullopt;
}

// Cross-type intersection with explicit output rect type.
// Returns std::nullopt for zero-area intersection.
template <class OutRect, class A, class B>
inline auto intersectionAs(const A& a,
                            const B& b) -> std::optional<remove_cvref_t<OutRect>> {
  using RR = remove_cvref_t<OutRect>;
  using T  = RectCoordT<RR>;

  const auto lx_raw = std::max(minX(a), minX(b));
  const auto ly_raw = std::max(minY(a), minY(b));
  const auto hx_raw = std::min(maxX(a), maxX(b));
  const auto hy_raw = std::min(maxY(a), maxY(b));

  if (lx_raw < hx_raw && ly_raw < hy_raw) {
    return makeRect<RR>(
        static_cast<T>(lx_raw),
        static_cast<T>(ly_raw),
        static_cast<T>(hx_raw),
        static_cast<T>(hy_raw));
  }
  return std::nullopt;
}

// clip r by win, preserving r's output type.
template <class R, class W>
inline auto clip(const R& r,
                 const W& win) -> std::optional<remove_cvref_t<R>> {
  return intersectionAs<remove_cvref_t<R>>(r, win);
}

// clip r by win, with explicit output type.
template <class OutRect, class R, class W>
inline auto clipAs(const R& r,
                    const W& win) -> std::optional<remove_cvref_t<OutRect>> {
  return intersectionAs<remove_cvref_t<OutRect>>(r, win);
}

// ============================================================
//  Convert
// ============================================================

// to_box: any rect-like -> bg::box< bg::point<T,2,cartesian> >
template <class R>
inline auto toBox(const R& r)
    -> bg::model::box<bg::model::point<RectCoordT<R>, 2, bg::cs::cartesian>> {
  using T = RectCoordT<R>;
  using P = bg::model::point<T, 2, bg::cs::cartesian>;
  using B = bg::model::box<P>;
  return B(P(minX(r), minY(r)), P(maxX(r), maxY(r)));
}

// to_rect: any rect-like -> gtl::rectangle_data<T>
template <class R>
inline auto toRect(const R& r) -> gtl::rectangle_data<RectCoordT<R>> {
  using T = RectCoordT<R>;
  return gtl::rectangle_data<T>(minX(r), minY(r), maxX(r), maxY(r));
}

// ------------------------------------------------------------
// divide_rect_as: integral rect -> floating-point rect with scaling
// ------------------------------------------------------------

template <class OutRect, class InRect, class Div>
inline auto divideRectAs(const InRect& r,
                           Div divisor) -> OutRect
{
  using InCoord  = RectCoordT<InRect>;
  using OutCoord = RectCoordT<OutRect>;

  static_assert(std::is_integral_v<InCoord>,
                "divide_rect_as: InRect must use integral coordinates");
  static_assert(std::is_floating_point_v<OutCoord>,
                "divide_rect_as: OutRect must use floating-point coordinates");

  assert(divisor != 0 && "divide_rect_as: divisor must not be zero");

  const OutCoord d = static_cast<OutCoord>(divisor);

  return makeRect<OutRect>(
      static_cast<OutCoord>(minX(r)) / d,
      static_cast<OutCoord>(minY(r)) / d,
      static_cast<OutCoord>(maxX(r)) / d,
      static_cast<OutCoord>(maxY(r)) / d);
}

// ============================================================
//  Point transforms
// ============================================================

template <class P>
inline auto translatePoint(const P& p,
                            PointCoordT<P> dx,
                            PointCoordT<P> dy) -> remove_cvref_t<P> {
  using PP = remove_cvref_t<P>;
  return makePoint<PP>(geom::x(p) + dx, geom::y(p) + dy);
}

// Returns a GTL rectangle centered at p with half-size d in both axes.
template <class P>
inline auto rectAround(const P& p,
                        PointCoordT<P> d) -> gtl::rectangle_data<PointCoordT<P>> {
  using T = PointCoordT<P>;
  return gtl::rectangle_data<T>{geom::x(p) - d, geom::y(p) - d, geom::x(p) + d, geom::y(p) + d};
}

// Backward-compatible alias for old API name.
template <class P>
inline auto boxAround(const P& p,
                       PointCoordT<P> d) -> gtl::rectangle_data<PointCoordT<P>> {
  return rectAround(p, d);
}

// ============================================================
//  Rect transforms
// ============================================================

template <class R>
inline auto translateRect(const R& r,
                           RectCoordT<R> dx,
                           RectCoordT<R> dy) -> remove_cvref_t<R> {
  using RR = remove_cvref_t<R>;
  return makeRect<RR>(minX(r) + dx, minY(r) + dy, maxX(r) + dx, maxY(r) + dy);
}

template <class R>
inline auto inflateX(const R& r,
                      RectCoordT<R> dx) -> remove_cvref_t<R> {
  using RR = remove_cvref_t<R>;
  return makeRect<RR>(minX(r) - dx, minY(r), maxX(r) + dx, maxY(r));
}

template <class R>
inline auto inflateY(const R& r,
                      RectCoordT<R> dy) -> remove_cvref_t<R> {
  using RR = remove_cvref_t<R>;
  return makeRect<RR>(minX(r), minY(r) - dy, maxX(r), maxY(r) + dy);
}

template <class R>
inline auto inflate(const R& r,
                    RectCoordT<R> d) -> remove_cvref_t<R> {
  using RR = remove_cvref_t<R>;
  return makeRect<RR>(minX(r) - d, minY(r) - d, maxX(r) + d, maxY(r) + d);
}

// ============================================================
//  Polyset conversion helpers
// ============================================================

inline auto polysetToRects(const GtlPolySetI& ps) -> std::vector<GtlRectI>
{
  std::vector<GtlRectI> rects;
  ps.get_rectangles(rects);
  return rects;
}

inline auto polysetToRects(const GtlPolySetF& ps) -> std::vector<GtlRectF>
{
  std::vector<GtlRectF> rects;
  ps.get_rectangles(rects);
  return rects;
}

inline auto rectsToPolyset(const std::vector<GtlRectI>& rects) -> GtlPolySetI
{
  GtlPolySetI ps;
  for (const auto& r : rects) {
    ps += r;
  }
  return ps;
}

inline auto rectsToPolyset(const std::vector<GtlRectF>& rects) -> GtlPolySetF
{
  GtlPolySetF ps;
  for (const auto& r : rects) {
    ps += r;
  }
  return ps;
}

// Returns the bounding box of all rects, or std::nullopt when rects is empty.
template <class Rect>
inline auto rectsToBbox(const std::vector<Rect>& rects) -> std::optional<Rect> {
  if (rects.empty()) {
    return std::nullopt;
  }

  using T = RectCoordT<Rect>;

  T minx = minX(rects[0]);
  T maxx = maxX(rects[0]);
  T miny = minY(rects[0]);
  T maxy = maxY(rects[0]);

  for (Size i = 1; i < rects.size(); ++i) {
    minx = std::min(minx, minX(rects[i]));
    maxx = std::max(maxx, maxX(rects[i]));
    miny = std::min(miny, minY(rects[i]));
    maxy = std::max(maxy, maxY(rects[i]));
  }

  return makeRect<Rect>(minx, miny, maxx, maxy);
}

template <typename T>
struct RectRelation
{
  T overlap_x = 0;
  T overlap_y = 0;
  T gap_x = 0;
  T gap_y = 0;
};

template <typename T>
inline auto rectRelation(T a_llx,
                          T a_lly,
                          T a_urx,
                          T a_ury,
                          T b_llx,
                          T b_lly,
                          T b_urx,
                          T b_ury) -> RectRelation<T>
{
  RectRelation<T> relation;
  relation.overlap_x = std::max(T{}, std::min(a_urx, b_urx) - std::max(a_llx, b_llx));
  relation.overlap_y = std::max(T{}, std::min(a_ury, b_ury) - std::max(a_lly, b_lly));
  relation.gap_x = std::max(T{}, std::max(a_llx, b_llx) - std::min(a_urx, b_urx));
  relation.gap_y = std::max(T{}, std::max(a_lly, b_lly) - std::min(a_ury, b_ury));
  return relation;
}

template <typename PointT, typename RectT>
inline auto pointToRectDistance2(PointT px,
                                    PointT py,
                                    RectT llx,
                                    RectT lly,
                                    RectT urx,
                                    RectT ury) -> F64
{
  const auto clamped_x = std::clamp(
      static_cast<F64>(px),
      static_cast<F64>(std::min(llx, urx)),
      static_cast<F64>(std::max(llx, urx)));
  const auto clamped_y = std::clamp(
      static_cast<F64>(py),
      static_cast<F64>(std::min(lly, ury)),
      static_cast<F64>(std::max(lly, ury)));
  const F64 dx = static_cast<F64>(px) - clamped_x;
  const F64 dy = static_cast<F64>(py) - clamped_y;
  return dx * dx + dy * dy;
}

}  // namespace geom
}  // namespace ircx
