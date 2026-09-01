// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "Utility.hpp"

namespace ircx::geom {

template <typename Point>
typename Utility::PointCoordT<Point> x(const Point& point)
{
  return Utility::x(point);
}

template <typename Point>
typename Utility::PointCoordT<Point> y(const Point& point)
{
  return Utility::y(point);
}

template <typename Rect>
typename Utility::RectCoordT<Rect> minX(const Rect& rect)
{
  return Utility::minX(rect);
}

template <typename Rect>
typename Utility::RectCoordT<Rect> minY(const Rect& rect)
{
  return Utility::minY(rect);
}

template <typename Rect>
typename Utility::RectCoordT<Rect> maxX(const Rect& rect)
{
  return Utility::maxX(rect);
}

template <typename Rect>
typename Utility::RectCoordT<Rect> maxY(const Rect& rect)
{
  return Utility::maxY(rect);
}

template <typename Rect>
typename Utility::remove_cvref_t<Rect> makeRect(typename Utility::RectCoordT<Rect> lower_x, typename Utility::RectCoordT<Rect> lower_y,
                                                typename Utility::RectCoordT<Rect> upper_x, typename Utility::RectCoordT<Rect> upper_y)
{
  return Utility::makeRect<Rect>(lower_x, lower_y, upper_x, upper_y);
}

template <typename Point>
bool isHorizontalDominant(const Point& first_point, const Point& second_point)
{
  return Utility::isHorizontalDominant(first_point, second_point);
}

template <typename Point>
bool isLowerLeft(const Point& first_point, const Point& second_point)
{
  return Utility::isLowerLeft(first_point, second_point);
}

template <typename Point>
GTLRectInt boxAround(const Point& point, typename Utility::PointCoordT<Point> distance)
{
  return Utility::boxAround(point, distance);
}

}  // namespace ircx::geom
