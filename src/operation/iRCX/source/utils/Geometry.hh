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
template <class C, std::size_t Dim, class Cs>
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
  static coord_t x(const gtl::point_data<T>& p) { return p.x(); }
  static coord_t y(const gtl::point_data<T>& p) { return p.y(); }
};

// bg::model::point<T, Dim, Cs>
template <class T, std::size_t Dim, class Cs>
struct point_traits<bg::model::point<T, Dim, Cs>> {
  static_assert(Dim >= 2, "Boost.Geometry point dimension must be >= 2");
  using coord_t = T;
  static coord_t x(const bg::model::point<T, Dim, Cs>& p) { return bg::get<0>(p); }
  static coord_t y(const bg::model::point<T, Dim, Cs>& p) { return bg::get<1>(p); }
};

template <class P>
using PointCoordT = typename point_traits<remove_cvref_t<P>>::coord_t;

// Unified point accessors

template <class P>
inline auto X(const P& p) -> PointCoordT<P> {
  return point_traits<remove_cvref_t<P>>::x(p);
}

template <class P>
inline auto Y(const P& p) -> PointCoordT<P> {
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
  static gtl::point_data<T> make(coord_t x, coord_t y) {
    return gtl::point_data<T>{x, y};
  }
};

// bg::model::point<T, Dim, Cs>
template <class T, std::size_t Dim, class Cs>
struct point_make_traits<bg::model::point<T, Dim, Cs>> {
  static_assert(Dim >= 2, "Boost.Geometry point dimension must be >= 2");
  using coord_t = T;

  static bg::model::point<T, Dim, Cs> make(coord_t x, coord_t y) {
    bg::model::point<T, Dim, Cs> p{};
    bg::set<0>(p, x);
    bg::set<1>(p, y);
    return p;
  }
};

template <class P>
inline auto MakePoint(PointCoordT<P> x, PointCoordT<P> y) -> remove_cvref_t<P> {
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
  static coord_t min_x(const gtl::rectangle_data<T>& r) { return r.get(gtl::WEST); }
  static coord_t min_y(const gtl::rectangle_data<T>& r) { return r.get(gtl::SOUTH); }
  static coord_t max_x(const gtl::rectangle_data<T>& r) { return r.get(gtl::EAST); }
  static coord_t max_y(const gtl::rectangle_data<T>& r) { return r.get(gtl::NORTH); }
};

// bg::model::box<PointT>
template <class PointT>
struct rect_traits<bg::model::box<PointT>> {
  using coord_t = typename bg::coordinate_type<PointT>::type;
  static coord_t min_x(const bg::model::box<PointT>& b) { return bg::get<bg::min_corner, 0>(b); }
  static coord_t min_y(const bg::model::box<PointT>& b) { return bg::get<bg::min_corner, 1>(b); }
  static coord_t max_x(const bg::model::box<PointT>& b) { return bg::get<bg::max_corner, 0>(b); }
  static coord_t max_y(const bg::model::box<PointT>& b) { return bg::get<bg::max_corner, 1>(b); }
};

template <class R>
using RectCoordT = typename rect_traits<remove_cvref_t<R>>::coord_t;

// Unified rect accessors

template <class R>
inline auto MinX(const R& r) -> RectCoordT<R> {
  return rect_traits<remove_cvref_t<R>>::min_x(r);
}

template <class R>
inline auto MinY(const R& r) -> RectCoordT<R> {
  return rect_traits<remove_cvref_t<R>>::min_y(r);
}

template <class R>
inline auto MaxX(const R& r) -> RectCoordT<R> {
  return rect_traits<remove_cvref_t<R>>::max_x(r);
}

template <class R>
inline auto MaxY(const R& r) -> RectCoordT<R> {
  return rect_traits<remove_cvref_t<R>>::max_y(r);
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
  static gtl::rectangle_data<T> make(coord_t lx, coord_t ly, coord_t hx, coord_t hy) {
    return {lx, ly, hx, hy};
  }
};

// bg::model::box<PointT> : {Point(lx,ly), Point(hx,hy)}
template <class PointT>
struct rect_make_traits<bg::model::box<PointT>> {
  using coord_t = typename bg::coordinate_type<PointT>::type;
  static bg::model::box<PointT> make(coord_t lx, coord_t ly, coord_t hx, coord_t hy) {
    return {PointT(lx, ly), PointT(hx, hy)};
  }
};

template <class R>
inline auto MakeRect(RectCoordT<R> lx, RectCoordT<R> ly, RectCoordT<R> hx, RectCoordT<R> hy)
    -> remove_cvref_t<R> {
  using RR = remove_cvref_t<R>;
  return rect_make_traits<RR>::make(lx, ly, hx, hy);
}

// ============================================================
//  Point utilities (generic)
// ============================================================

template <class P>
inline auto Manhattan(const P& a, const P& b)
    -> decltype(std::abs(X(a) - X(b)) + std::abs(Y(a) - Y(b))) {
  const auto dx = std::abs(X(a) - X(b));
  const auto dy = std::abs(Y(a) - Y(b));
  return dx + dy;
}

template <class P>
inline bool IsHorDominant(const P& a, const P& b) {
  return std::abs(X(a) - X(b)) >= std::abs(Y(a) - Y(b));
}

template <class P>
inline bool IsVerDominant(const P& a, const P& b) {
  return !IsHorDominant(a, b);
}

template <class P>
inline bool IsLeftBottom(const P& a, const P& b) {
  return X(a) <= X(b) && Y(a) <= Y(b);
}

template <class P>
inline bool IsRightTop(const P& a, const P& b) {
  return X(a) >= X(b) && Y(a) >= Y(b);
}

// ============================================================
//  Rect utilities (generic)
// ============================================================

template <class R>
inline auto DeltaX(const R& r) -> RectCoordT<R> {
  return MaxX(r) - MinX(r);
}

template <class R>
inline auto DeltaY(const R& r) -> RectCoordT<R> {
  return MaxY(r) - MinY(r);
}

// Use Min + (Max - Min) / 2 to reduce overflow risk for integral coordinates.
template <class R>
inline auto CenterX(const R& r) -> RectCoordT<R> {
  using T = RectCoordT<R>;
  return MinX(r) + (MaxX(r) - MinX(r)) / T{2};
}

template <class R>
inline auto CenterY(const R& r) -> RectCoordT<R> {
  using T = RectCoordT<R>;
  return MinY(r) + (MaxY(r) - MinY(r)) / T{2};
}

// Center always returns gtl::point_data<coord_t>.
// Float rect keeps float precision; integral rect keeps integral midpoint semantics.
template <class R>
inline auto Center(const R& r) -> gtl::point_data<RectCoordT<R>> {
  using T = RectCoordT<R>;
  return gtl::point_data<T>{CenterX(r), CenterY(r)};
}

template <class R>
inline bool IsHorDominant(const R& r) {
  return DeltaX(r) >= DeltaY(r);
}

template <class R>
inline bool IsVerDominant(const R& r) {
  return DeltaX(r) < DeltaY(r);
}

template <class R, class P>
inline bool RectContainsPoint(const R& r, const P& p) {
  return X(p) >= MinX(r) && X(p) <= MaxX(r) &&
         Y(p) >= MinY(r) && Y(p) <= MaxY(r);
}

// ============================================================
//  Area / Intersects
// ============================================================

template <class Shape>
inline double Area(const Shape& s) {
  using S = remove_cvref_t<Shape>;

  if constexpr (is_bg_box<S>::value) {
    return static_cast<double>(bg::area(s));
  } else if constexpr (is_gtl_rect<S>::value || is_gtl_polyset90<S>::value) {
    return static_cast<double>(gtl::area(s));
  } else {
    static_assert(dependent_false<S>::value,
                  "Area(Shape): unsupported Shape type "
                  "(expected bg::model::box<...>, gtl::rectangle_data<...>, "
                  "or gtl::polygon_90_set_data<...>)");
    return 0.0;
  }
}

// Note:
// - This function delegates to the underlying library semantics.
// - If you need strict positive-area overlap, use HasAreaIntersection().
template <class A, class B>
inline bool Intersects(const A& a, const B& b) {
  using AA = remove_cvref_t<A>;
  using BB = remove_cvref_t<B>;

  if constexpr (is_bg_box<AA>::value && is_bg_box<BB>::value) {
    return bg::intersects(a, b);
  } else if constexpr (is_gtl_rect<AA>::value && is_gtl_rect<BB>::value) {
    return gtl::intersects(a, b);
  } else {
    static_assert(dependent_false2<AA, BB>::value,
                  "Intersects(A,B): unsupported type combination "
                  "(supported: bg::box vs bg::box, gtl::rect vs gtl::rect)");
    return false;
  }
}

// Strict positive-area overlap for any two rect-like objects.
template <class A, class B>
inline bool HasAreaIntersection(const A& a, const B& b) {
  using TA = RectCoordT<A>;
  using TB = RectCoordT<B>;
  using CommonT = std::common_type_t<TA, TB>;

  const CommonT lx = std::max<CommonT>(static_cast<CommonT>(MinX(a)), static_cast<CommonT>(MinX(b)));
  const CommonT ly = std::max<CommonT>(static_cast<CommonT>(MinY(a)), static_cast<CommonT>(MinY(b)));
  const CommonT hx = std::min<CommonT>(static_cast<CommonT>(MaxX(a)), static_cast<CommonT>(MaxX(b)));
  const CommonT hy = std::min<CommonT>(static_cast<CommonT>(MaxY(a)), static_cast<CommonT>(MaxY(b)));

  return (lx < hx) && (ly < hy);
}

// ============================================================
//  Generic intersection / clipping for rect-like objects
// ============================================================

// Same-type intersection: returns std::nullopt for zero-area intersection.
template <class R>
inline auto Intersection(const R& a, const R& b) -> std::optional<remove_cvref_t<R>> {
  using RR = remove_cvref_t<R>;
  using T  = RectCoordT<RR>;

  const T lx = std::max(MinX(a), MinX(b));
  const T ly = std::max(MinY(a), MinY(b));
  const T hx = std::min(MaxX(a), MaxX(b));
  const T hy = std::min(MaxY(a), MaxY(b));

  if (lx < hx && ly < hy) {
    return MakeRect<RR>(lx, ly, hx, hy);
  }
  return std::nullopt;
}

// Cross-type intersection with explicit output rect type.
// Returns std::nullopt for zero-area intersection.
template <class OutRect, class A, class B>
inline auto IntersectionAs(const A& a, const B& b) -> std::optional<remove_cvref_t<OutRect>> {
  using RR = remove_cvref_t<OutRect>;
  using T  = RectCoordT<RR>;

  const auto lx_raw = std::max(MinX(a), MinX(b));
  const auto ly_raw = std::max(MinY(a), MinY(b));
  const auto hx_raw = std::min(MaxX(a), MaxX(b));
  const auto hy_raw = std::min(MaxY(a), MaxY(b));

  if (lx_raw < hx_raw && ly_raw < hy_raw) {
    return MakeRect<RR>(
        static_cast<T>(lx_raw),
        static_cast<T>(ly_raw),
        static_cast<T>(hx_raw),
        static_cast<T>(hy_raw));
  }
  return std::nullopt;
}

// Clip r by win, preserving r's output type.
template <class R, class W>
inline auto Clip(const R& r, const W& win) -> std::optional<remove_cvref_t<R>> {
  return IntersectionAs<remove_cvref_t<R>>(r, win);
}

// Clip r by win, with explicit output type.
template <class OutRect, class R, class W>
inline auto ClipAs(const R& r, const W& win) -> std::optional<remove_cvref_t<OutRect>> {
  return IntersectionAs<remove_cvref_t<OutRect>>(r, win);
}

// ============================================================
//  Convert
// ============================================================

// ToBox: any rect-like -> bg::box< bg::point<T,2,cartesian> >
template <class R>
inline auto ToBox(const R& r)
    -> bg::model::box<bg::model::point<RectCoordT<R>, 2, bg::cs::cartesian>> {
  using T = RectCoordT<R>;
  using P = bg::model::point<T, 2, bg::cs::cartesian>;
  using B = bg::model::box<P>;
  return B(P(MinX(r), MinY(r)), P(MaxX(r), MaxY(r)));
}

// ToRect: any rect-like -> gtl::rectangle_data<T>
template <class R>
inline auto ToRect(const R& r) -> gtl::rectangle_data<RectCoordT<R>> {
  using T = RectCoordT<R>;
  return gtl::rectangle_data<T>(MinX(r), MinY(r), MaxX(r), MaxY(r));
}

// ------------------------------------------------------------
// RectCastDivide: integral rect -> floating-point rect with scaling
// ------------------------------------------------------------

template <class OutRect, class InRect, class Div>
inline OutRect RectCastDivide(const InRect& r, Div divisor) {
  using InCoord  = RectCoordT<InRect>;
  using OutCoord = RectCoordT<OutRect>;

  static_assert(std::is_integral_v<InCoord>,
                "RectCastDivide: InRect must use integral coordinates");
  static_assert(std::is_floating_point_v<OutCoord>,
                "RectCastDivide: OutRect must use floating-point coordinates");

  assert(divisor != 0 && "RectCastDivide: divisor must not be zero");

  const OutCoord d = static_cast<OutCoord>(divisor);

  return MakeRect<OutRect>(
      static_cast<OutCoord>(MinX(r)) / d,
      static_cast<OutCoord>(MinY(r)) / d,
      static_cast<OutCoord>(MaxX(r)) / d,
      static_cast<OutCoord>(MaxY(r)) / d);
}

// ============================================================
//  Point transforms
// ============================================================

template <class P>
inline auto TranslatePoint(const P& p, PointCoordT<P> dx, PointCoordT<P> dy) -> remove_cvref_t<P> {
  using PP = remove_cvref_t<P>;
  return MakePoint<PP>(X(p) + dx, Y(p) + dy);
}

// Returns a GTL rectangle centered at p with half-size d in both axes.
template <class P>
inline auto RectAround(const P& p, PointCoordT<P> d) -> gtl::rectangle_data<PointCoordT<P>> {
  using T = PointCoordT<P>;
  return gtl::rectangle_data<T>{X(p) - d, Y(p) - d, X(p) + d, Y(p) + d};
}

// Backward-compatible alias for old API name.
template <class P>
inline auto BoxAround(const P& p, PointCoordT<P> d) -> gtl::rectangle_data<PointCoordT<P>> {
  return RectAround(p, d);
}

// ============================================================
//  Rect transforms
// ============================================================

template <class R>
inline auto TranslateRect(const R& r, RectCoordT<R> dx, RectCoordT<R> dy) -> remove_cvref_t<R> {
  using RR = remove_cvref_t<R>;
  return MakeRect<RR>(MinX(r) + dx, MinY(r) + dy, MaxX(r) + dx, MaxY(r) + dy);
}

template <class R>
inline auto InflateX(const R& r, RectCoordT<R> dx) -> remove_cvref_t<R> {
  using RR = remove_cvref_t<R>;
  return MakeRect<RR>(MinX(r) - dx, MinY(r), MaxX(r) + dx, MaxY(r));
}

template <class R>
inline auto InflateY(const R& r, RectCoordT<R> dy) -> remove_cvref_t<R> {
  using RR = remove_cvref_t<R>;
  return MakeRect<RR>(MinX(r), MinY(r) - dy, MaxX(r), MaxY(r) + dy);
}

template <class R>
inline auto Inflate(const R& r, RectCoordT<R> d) -> remove_cvref_t<R> {
  using RR = remove_cvref_t<R>;
  return MakeRect<RR>(MinX(r) - d, MinY(r) - d, MaxX(r) + d, MaxY(r) + d);
}

// ============================================================
//  Polyset conversion helpers
// ============================================================

inline std::vector<GtlRectI> PolysetToRects(const GtlPolysetI& ps) {
  std::vector<GtlRectI> rects;
  ps.get_rectangles(rects);
  return rects;
}

inline std::vector<GtlRectF> PolysetToRects(const GtlPolysetF& ps) {
  std::vector<GtlRectF> rects;
  ps.get_rectangles(rects);
  return rects;
}

inline GtlPolysetI RectsToPolyset(const std::vector<GtlRectI>& rects) {
  GtlPolysetI ps;
  for (const auto& r : rects) {
    ps += r;
  }
  return ps;
}

inline GtlPolysetF RectsToPolyset(const std::vector<GtlRectF>& rects) {
  GtlPolysetF ps;
  for (const auto& r : rects) {
    ps += r;
  }
  return ps;
}

// Returns the bounding box of all rects, or std::nullopt when rects is empty.
template <class Rect>
inline auto RectsToBbox(const std::vector<Rect>& rects) -> std::optional<Rect> {
  if (rects.empty()) {
    return std::nullopt;
  }

  using T = RectCoordT<Rect>;

  T minx = MinX(rects[0]);
  T maxx = MaxX(rects[0]);
  T miny = MinY(rects[0]);
  T maxy = MaxY(rects[0]);

  for (std::size_t i = 1; i < rects.size(); ++i) {
    minx = std::min(minx, MinX(rects[i]));
    maxx = std::max(maxx, MaxX(rects[i]));
    miny = std::min(miny, MinY(rects[i]));
    maxy = std::max(maxy, MaxY(rects[i]));
  }

  return MakeRect<Rect>(minx, miny, maxx, maxy);
}

}  // namespace geom
}  // namespace ircx
