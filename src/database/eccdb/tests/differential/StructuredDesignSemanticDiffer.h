// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "DesignSemanticSnapshot.h"

namespace eccdb::test {

inline DesignSignalUse canonicalSignalUse(DesignSignalUse use)
{
  return use == DesignSignalUse::kNone ? DesignSignalUse::kSignal : use;
}

namespace structured {

inline bool pointLess(const Point& lhs, const Point& rhs)
{
  return std::tie(lhs.x, lhs.y) < std::tie(rhs.x, rhs.y);
}

inline bool pointVectorLess(const std::vector<Point>& lhs, const std::vector<Point>& rhs)
{
  return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), pointLess);
}

inline std::vector<Point> canonicalPolygon(std::vector<Point> points)
{
  if (points.size() > 1u && points.front() == points.back()) {
    points.pop_back();
  }
  if (points.size() < 2u) {
    return points;
  }

  std::vector<Point> best;
  const auto consider = [&best](const std::vector<Point>& sequence) {
    for (std::size_t offset = 0; offset < sequence.size(); ++offset) {
      std::vector<Point> candidate;
      candidate.reserve(sequence.size());
      for (std::size_t index = 0; index < sequence.size(); ++index) {
        candidate.push_back(sequence[(offset + index) % sequence.size()]);
      }
      if (best.empty() || pointVectorLess(candidate, best)) {
        best = std::move(candidate);
      }
    }
  };

  consider(points);
  std::reverse(points.begin(), points.end());
  consider(points);
  return best;
}

inline Rect canonicalRect(Rect rectangle)
{
  return rectangle.normalized();
}

template <typename T, typename Less>
inline void canonicalSort(std::vector<T>& values, Less less)
{
  std::sort(values.begin(), values.end(), less);
}

struct CanonicalPin
{
  std::string instance;
  std::string master_term;
  std::string net;
  std::string special_net;

  bool operator==(const CanonicalPin&) const = default;
};

inline bool operator<(const CanonicalPin& lhs, const CanonicalPin& rhs)
{
  return std::tie(lhs.instance, lhs.master_term, lhs.net, lhs.special_net)
         < std::tie(rhs.instance, rhs.master_term, rhs.net, rhs.special_net);
}

struct CanonicalSegment
{
  std::string layer;
  DesignWireStatus status = DesignWireStatus::kRouted;
  std::string shield_net;
  uint32_t path_flags = 0;
  int32_t width = 0;
  uint32_t mask = 0;
  std::string taper_rule;
  std::string shape;
  int32_t style = 0;
  Point first;
  uint32_t first_flags = 0;
  int32_t first_extension = 0;
  Point second;
  uint32_t second_flags = 0;
  int32_t second_extension = 0;

  bool operator==(const CanonicalSegment&) const = default;
};

inline bool operator<(const CanonicalSegment& lhs, const CanonicalSegment& rhs)
{
  return std::tie(lhs.layer, lhs.status, lhs.shield_net, lhs.path_flags, lhs.width, lhs.mask, lhs.taper_rule, lhs.shape, lhs.style,
                  lhs.first.x, lhs.first.y, lhs.first_flags, lhs.first_extension, lhs.second.x, lhs.second.y, lhs.second_flags,
                  lhs.second_extension)
         < std::tie(rhs.layer, rhs.status, rhs.shield_net, rhs.path_flags, rhs.width, rhs.mask, rhs.taper_rule, rhs.shape, rhs.style,
                    rhs.first.x, rhs.first.y, rhs.first_flags, rhs.first_extension, rhs.second.x, rhs.second.y, rhs.second_flags,
                    rhs.second_extension);
}

struct CanonicalPoint
{
  std::string layer;
  DesignWireStatus status = DesignWireStatus::kRouted;
  std::string shield_net;
  uint32_t path_flags = 0;
  int32_t width = 0;
  uint32_t mask = 0;
  std::string taper_rule;
  std::string shape;
  int32_t style = 0;
  Point position;
  uint32_t flags = 0;
  int32_t extension = 0;

  bool operator==(const CanonicalPoint&) const = default;
};

inline bool operator<(const CanonicalPoint& lhs, const CanonicalPoint& rhs)
{
  return std::tie(lhs.layer, lhs.status, lhs.shield_net, lhs.path_flags, lhs.width, lhs.mask, lhs.taper_rule, lhs.shape, lhs.style,
                  lhs.position.x, lhs.position.y, lhs.flags, lhs.extension)
         < std::tie(rhs.layer, rhs.status, rhs.shield_net, rhs.path_flags, rhs.width, rhs.mask, rhs.taper_rule, rhs.shape, rhs.style,
                    rhs.position.x, rhs.position.y, rhs.flags, rhs.extension);
}

struct CanonicalWireVia
{
  DesignWireStatus status = DesignWireStatus::kRouted;
  std::string shield_net;
  Point anchor;
  uint32_t anchor_flags = 0;
  int32_t anchor_extension = 0;
  std::string name;
  DesignOrientation orientation = DesignOrientation::kN;
  uint32_t flags = 0;
  uint32_t top_mask = 0;
  uint32_t cut_mask = 0;
  uint32_t bottom_mask = 0;
  uint32_t rows = 1;
  uint32_t columns = 1;
  int32_t step_x = 0;
  int32_t step_y = 0;

  bool operator==(const CanonicalWireVia&) const = default;
};

inline bool operator<(const CanonicalWireVia& lhs, const CanonicalWireVia& rhs)
{
  return std::tie(lhs.status, lhs.shield_net, lhs.anchor.x, lhs.anchor.y, lhs.anchor_flags, lhs.anchor_extension, lhs.name,
                  lhs.orientation, lhs.flags, lhs.top_mask, lhs.cut_mask, lhs.bottom_mask, lhs.rows, lhs.columns, lhs.step_x, lhs.step_y)
         < std::tie(rhs.status, rhs.shield_net, rhs.anchor.x, rhs.anchor.y, rhs.anchor_flags, rhs.anchor_extension, rhs.name,
                    rhs.orientation, rhs.flags, rhs.top_mask, rhs.cut_mask, rhs.bottom_mask, rhs.rows, rhs.columns, rhs.step_x, rhs.step_y);
}

struct CanonicalWireRectangle
{
  std::string layer;
  DesignWireStatus status = DesignWireStatus::kRouted;
  std::string shield_net;
  uint32_t path_flags = 0;
  int32_t width = 0;
  uint32_t mask = 0;
  std::string taper_rule;
  std::string shape;
  int32_t style = 0;
  Rect rectangle;

  bool operator==(const CanonicalWireRectangle&) const = default;
};

inline bool operator<(const CanonicalWireRectangle& lhs, const CanonicalWireRectangle& rhs)
{
  return std::tie(lhs.layer, lhs.status, lhs.shield_net, lhs.path_flags, lhs.width, lhs.mask, lhs.taper_rule, lhs.shape, lhs.style,
                  lhs.rectangle.ll_x, lhs.rectangle.ll_y, lhs.rectangle.ur_x, lhs.rectangle.ur_y)
         < std::tie(rhs.layer, rhs.status, rhs.shield_net, rhs.path_flags, rhs.width, rhs.mask, rhs.taper_rule, rhs.shape, rhs.style,
                    rhs.rectangle.ll_x, rhs.rectangle.ll_y, rhs.rectangle.ur_x, rhs.rectangle.ur_y);
}

struct CanonicalNetRectangle
{
  std::string layer;
  Rect rectangle;
  DesignWireStatus status = DesignWireStatus::kRouted;
  uint32_t flags = 0;
  uint32_t mask = 0;
  std::string shield_net;
  std::string shape;

  bool operator==(const CanonicalNetRectangle&) const = default;
};

inline bool operator<(const CanonicalNetRectangle& lhs, const CanonicalNetRectangle& rhs)
{
  return std::tie(lhs.layer, lhs.rectangle.ll_x, lhs.rectangle.ll_y, lhs.rectangle.ur_x, lhs.rectangle.ur_y, lhs.status, lhs.flags,
                  lhs.mask, lhs.shield_net, lhs.shape)
         < std::tie(rhs.layer, rhs.rectangle.ll_x, rhs.rectangle.ll_y, rhs.rectangle.ur_x, rhs.rectangle.ur_y, rhs.status, rhs.flags,
                    rhs.mask, rhs.shield_net, rhs.shape);
}

struct CanonicalNetPolygon
{
  std::string layer;
  std::vector<Point> points;
  DesignWireStatus status = DesignWireStatus::kRouted;
  uint32_t flags = 0;
  uint32_t mask = 0;
  std::string shield_net;
  std::string shape;

  bool operator==(const CanonicalNetPolygon&) const = default;
};

inline bool operator<(const CanonicalNetPolygon& lhs, const CanonicalNetPolygon& rhs)
{
  if (lhs.layer != rhs.layer) {
    return lhs.layer < rhs.layer;
  }
  if (lhs.points != rhs.points) {
    return pointVectorLess(lhs.points, rhs.points);
  }
  return std::tie(lhs.status, lhs.flags, lhs.mask, lhs.shield_net, lhs.shape)
         < std::tie(rhs.status, rhs.flags, rhs.mask, rhs.shield_net, rhs.shape);
}

struct CanonicalNetVia
{
  std::string name;
  std::vector<Point> origins;
  DesignOrientation orientation = DesignOrientation::kN;
  DesignWireStatus status = DesignWireStatus::kRouted;
  uint32_t flags = 0;
  uint32_t top_mask = 0;
  uint32_t cut_mask = 0;
  uint32_t bottom_mask = 0;
  std::string shield_net;
  std::string shape;

  bool operator==(const CanonicalNetVia&) const = default;
};

inline bool operator<(const CanonicalNetVia& lhs, const CanonicalNetVia& rhs)
{
  if (lhs.name != rhs.name) {
    return lhs.name < rhs.name;
  }
  if (lhs.origins != rhs.origins) {
    return pointVectorLess(lhs.origins, rhs.origins);
  }
  return std::tie(lhs.orientation, lhs.status, lhs.flags, lhs.top_mask, lhs.cut_mask, lhs.bottom_mask, lhs.shield_net, lhs.shape)
         < std::tie(rhs.orientation, rhs.status, rhs.flags, rhs.top_mask, rhs.cut_mask, rhs.bottom_mask, rhs.shield_net, rhs.shape);
}

struct CanonicalSpacing
{
  std::string layer;
  int32_t spacing = 0;
  uint32_t flags = 0;
  int32_t range_left = 0;
  int32_t range_right = 0;

  bool operator==(const CanonicalSpacing&) const = default;
};

inline bool operator<(const CanonicalSpacing& lhs, const CanonicalSpacing& rhs)
{
  return std::tie(lhs.layer, lhs.spacing, lhs.flags, lhs.range_left, lhs.range_right)
         < std::tie(rhs.layer, rhs.spacing, rhs.flags, rhs.range_left, rhs.range_right);
}

struct CanonicalNetOptions
{
  bool present = false;
  uint32_t flags = 0;
  std::string original;
  DesignNetPattern pattern = DesignNetPattern::kNone;
  double estimated_capacitance = 0.0;
  double frequency = 0.0;
  int32_t xtalk = 0;
  int32_t style = 0;
  int32_t voltage = 0;
  std::vector<CanonicalSpacing> spacing_rules;

  bool operator==(const CanonicalNetOptions&) const = default;
};

struct CanonicalNet
{
  std::string name;
  bool special = false;
  DesignSignalUse use = DesignSignalUse::kNone;
  DesignNetSource source = DesignNetSource::kNone;
  uint32_t flags = 0;
  int32_t weight = 0;
  std::string non_default_rule;
  CanonicalNetOptions options;
  std::vector<CanonicalPin> pins;
  std::vector<CanonicalSegment> segments;
  std::vector<CanonicalPoint> points;
  std::vector<CanonicalWireVia> wire_vias;
  std::vector<CanonicalWireRectangle> wire_rectangles;
  std::vector<CanonicalNetRectangle> rectangles;
  std::vector<CanonicalNetPolygon> polygons;
  std::vector<CanonicalNetVia> net_vias;

  bool operator==(const CanonicalNet&) const = default;
};

inline std::string netReference(const DesignStore& design, DesignNetId id)
{
  return detail::netName(design, id);
}

inline CanonicalNet canonicalNet(const DesignStore& design, DesignNetId id)
{
  const auto& net = design.netlistStorage().net(id);
  CanonicalNet result;
  result.name = net.name;
  result.special = design.netlistStorage().isSpecialNet(id);
  result.use = canonicalSignalUse(net.use);
  result.source = net.source;
  result.flags = net.flags;
  result.weight = net.weight;
  result.non_default_rule = net.design_non_default_rule
                                ? detail::ndrName(design, net.design_non_default_rule)
                                : detail::ndrName(design, net.non_default_rule);

  if (const auto* options = design.netlistStorage().netOptions(id); options != nullptr) {
    result.options.present = true;
    result.options.flags = options->flags;
    result.options.original = options->original;
    result.options.pattern = options->pattern;
    result.options.estimated_capacitance = options->estimated_capacitance;
    result.options.frequency = options->frequency;
    result.options.xtalk = options->xtalk;
    result.options.style = options->style;
    result.options.voltage = options->voltage;
    for (const auto& spacing : options->spacing_rules) {
      result.options.spacing_rules.push_back(CanonicalSpacing{.layer = detail::layerName(design, spacing.layer),
                                                              .spacing = spacing.spacing,
                                                              .flags = spacing.flags,
                                                              .range_left = spacing.range_left,
                                                              .range_right = spacing.range_right});
    }
    canonicalSort(result.options.spacing_rules, std::less{});
  }

  for (const auto pin_id : design.netlistStorage().instancePins(id)) {
    const auto& pin = design.netlistStorage().instancePin(pin_id);
    result.pins.push_back(CanonicalPin{.instance = detail::instanceName(design, pin.instance),
                                       .master_term = detail::masterTermName(design, pin.master_term),
                                       .net = netReference(design, pin.net),
                                       .special_net = netReference(design, pin.special_net)});
  }
  for (const auto pin_id : design.netlistStorage().ioPins(id)) {
    const auto& pin = design.netlistStorage().ioPin(pin_id);
    result.pins.push_back(CanonicalPin{.instance = "PIN/" + pin.name,
                                       .master_term = {},
                                       .net = netReference(design, pin.net),
                                       .special_net = netReference(design, pin.special_net)});
  }
  canonicalSort(result.pins, std::less{});

  for (const auto wire_id : design.routingStorage().wires(id)) {
    const auto& wire = design.routingStorage().wire(wire_id);
    for (std::size_t path_index = 0; path_index < design.routingStorage().pathCount(wire_id); ++path_index) {
      const auto path = design.routingStorage().path(wire_id, path_index);
      const auto layer = detail::layerName(design, path.layer());
      for (std::size_t point_index = 1; point_index < path.points().size(); ++point_index) {
        auto first = path.points()[point_index - 1u];
        auto second = path.points()[point_index];
        if (structured::pointLess(second.position, first.position)) {
          std::swap(first, second);
        }
        result.segments.push_back(CanonicalSegment{.layer = layer,
                                                   .status = wire.status,
                                                   .shield_net = wire.shield_net,
                                                   .path_flags = path.flags(),
                                                   .width = path.width(),
                                                   .mask = path.mask(),
                                                   .taper_rule = std::string{path.taperRule()},
                                                   .shape = std::string{path.shape()},
                                                   .style = path.style(),
                                                   .first = first.position,
                                                   .first_flags = first.flags,
                                                   .first_extension = first.extension,
                                                   .second = second.position,
                                                   .second_flags = second.flags,
                                                   .second_extension = second.extension});
      }
      if (path.points().size() == 1u && path.vias().empty() && path.rectangles().empty()) {
        const auto& point = path.points().front();
        result.points.push_back(CanonicalPoint{.layer = layer,
                                               .status = wire.status,
                                               .shield_net = wire.shield_net,
                                               .path_flags = path.flags(),
                                               .width = path.width(),
                                               .mask = path.mask(),
                                               .taper_rule = std::string{path.taperRule()},
                                               .shape = std::string{path.shape()},
                                               .style = path.style(),
                                               .position = point.position,
                                               .flags = point.flags,
                                               .extension = point.extension});
      }
      for (const auto& via : path.vias()) {
        const auto& anchor = path.points()[via.point_index];
        result.wire_vias.push_back(CanonicalWireVia{.status = wire.status,
                                                    .shield_net = wire.shield_net,
                                                    .anchor = anchor.position,
                                                    .anchor_flags = anchor.flags,
                                                    .anchor_extension = anchor.extension,
                                                    .name = detail::viaName(design, via.tech_via, via.design_via),
                                                    .orientation = via.orientation,
                                                    .flags = via.flags,
                                                    .top_mask = via.top_mask,
                                                    .cut_mask = via.cut_mask,
                                                    .bottom_mask = via.bottom_mask,
                                                    .rows = via.rows,
                                                    .columns = via.columns,
                                                    .step_x = via.step_x,
                                                    .step_y = via.step_y});
      }
      for (const auto& rectangle : path.rectangles()) {
        const auto& anchor = path.points()[rectangle.point_index];
        result.wire_rectangles.push_back(CanonicalWireRectangle{.layer = layer,
                                                                .status = wire.status,
                                                                .shield_net = wire.shield_net,
                                                                .path_flags = path.flags(),
                                                                .width = path.width(),
                                                                .mask = path.mask(),
                                                                .taper_rule = std::string{path.taperRule()},
                                                                .shape = std::string{path.shape()},
                                                                .style = path.style(),
                                                                .rectangle = canonicalRect(
                                                                    {.ll_x = anchor.position.x + rectangle.delta.ll_x,
                                                                     .ll_y = anchor.position.y + rectangle.delta.ll_y,
                                                                     .ur_x = anchor.position.x + rectangle.delta.ur_x,
                                                                     .ur_y = anchor.position.y + rectangle.delta.ur_y})});
      }
    }
  }

  if (const auto* geometry = design.routingStorage().netGeometry(id); geometry != nullptr) {
    for (const auto& rectangle : geometry->rectangles) {
      result.rectangles.push_back(CanonicalNetRectangle{.layer = detail::layerName(design, rectangle.layer),
                                                        .rectangle = canonicalRect(rectangle.rectangle),
                                                        .status = rectangle.route_status,
                                                        .flags = rectangle.flags,
                                                        .mask = rectangle.mask,
                                                        .shield_net = rectangle.shield_net,
                                                        .shape = rectangle.shape});
    }
    for (const auto& polygon : geometry->polygons) {
      result.polygons.push_back(CanonicalNetPolygon{.layer = detail::layerName(design, polygon.layer),
                                                    .points = canonicalPolygon(polygon.points),
                                                    .status = polygon.route_status,
                                                    .flags = polygon.flags,
                                                    .mask = polygon.mask,
                                                    .shield_net = polygon.shield_net,
                                                    .shape = polygon.shape});
    }
    for (const auto& via : geometry->vias) {
      auto origins = via.origins;
      canonicalSort(origins, structured::pointLess);
      result.net_vias.push_back(CanonicalNetVia{.name = detail::viaName(design, via.tech_via, via.design_via),
                                                .origins = std::move(origins),
                                                .orientation = via.orientation,
                                                .status = via.route_status,
                                                .flags = via.flags,
                                                .top_mask = via.top_mask,
                                                .cut_mask = via.cut_mask,
                                                .bottom_mask = via.bottom_mask,
                                                .shield_net = via.shield_net,
                                                .shape = via.shape});
    }
  }

  canonicalSort(result.segments, std::less{});
  canonicalSort(result.points, std::less{});
  canonicalSort(result.wire_vias, std::less{});
  canonicalSort(result.wire_rectangles, std::less{});
  canonicalSort(result.rectangles, std::less{});
  canonicalSort(result.polygons, std::less{});
  canonicalSort(result.net_vias, std::less{});
  return result;
}

}  // namespace structured

inline bool isSemanticallyComparableNet(const DesignStore& design, DesignNetId id)
{
  return !detail::isEmptyRegularAliasOfSpecialNet(design, id);
}

struct NetPair
{
  DesignNetId expected;
  DesignNetId actual;
  std::string name;
};

struct NetMismatch
{
  std::string name;
  std::string section;
};

inline std::string canonicalNetMismatch(const structured::CanonicalNet& expected, const structured::CanonicalNet& actual)
{
  const auto text_mismatch = [](std::string_view field, const std::string& lhs, const std::string& rhs) {
    return "field=" + std::string(field) + " expected=\"" + lhs + "\" actual=\"" + rhs + "\"";
  };
  const auto scalar_mismatch = [](std::string_view field, auto lhs, auto rhs) {
    return "field=" + std::string(field) + " expected=" + std::to_string(static_cast<int64_t>(lhs))
           + " actual=" + std::to_string(static_cast<int64_t>(rhs));
  };
  const auto double_mismatch = [](std::string_view field, double lhs, double rhs) {
    return "field=" + std::string(field) + " expected=" + std::to_string(lhs) + " actual=" + std::to_string(rhs);
  };
  const auto point_mismatch = [&](std::string_view field, const Point& lhs, const Point& rhs) -> std::optional<std::string> {
    if (lhs.x != rhs.x) {
      return scalar_mismatch(std::string(field) + ".x", lhs.x, rhs.x);
    }
    if (lhs.y != rhs.y) {
      return scalar_mismatch(std::string(field) + ".y", lhs.y, rhs.y);
    }
    return std::nullopt;
  };
  const auto rect_mismatch = [&](std::string_view field, const Rect& lhs, const Rect& rhs) -> std::optional<std::string> {
    if (lhs.ll_x != rhs.ll_x) {
      return scalar_mismatch(std::string(field) + ".ll_x", lhs.ll_x, rhs.ll_x);
    }
    if (lhs.ll_y != rhs.ll_y) {
      return scalar_mismatch(std::string(field) + ".ll_y", lhs.ll_y, rhs.ll_y);
    }
    if (lhs.ur_x != rhs.ur_x) {
      return scalar_mismatch(std::string(field) + ".ur_x", lhs.ur_x, rhs.ur_x);
    }
    if (lhs.ur_y != rhs.ur_y) {
      return scalar_mismatch(std::string(field) + ".ur_y", lhs.ur_y, rhs.ur_y);
    }
    return std::nullopt;
  };
  const auto size_mismatch = [](std::string_view field, std::size_t lhs, std::size_t rhs) {
    return "field=" + std::string(field) + ".count expected=" + std::to_string(lhs) + " actual=" + std::to_string(rhs);
  };
  if (expected.name != actual.name) {
    return text_mismatch("name", expected.name, actual.name);
  }
  if (expected.special != actual.special) {
    return scalar_mismatch("special", expected.special, actual.special);
  }
  if (expected.use != actual.use) {
    return scalar_mismatch("use", expected.use, actual.use);
  }
  if (expected.source != actual.source) {
    return scalar_mismatch("source", expected.source, actual.source);
  }
  if (expected.flags != actual.flags) {
    return scalar_mismatch("flags", expected.flags, actual.flags);
  }
  if (expected.weight != actual.weight) {
    return scalar_mismatch("weight", expected.weight, actual.weight);
  }
  if (expected.non_default_rule != actual.non_default_rule) {
    return text_mismatch("non_default_rule", expected.non_default_rule, actual.non_default_rule);
  }
  if (expected.options != actual.options) {
    if (expected.options.present != actual.options.present) {
      return scalar_mismatch("options.present", expected.options.present, actual.options.present);
    }
    if (expected.options.flags != actual.options.flags) {
      return scalar_mismatch("options.flags", expected.options.flags, actual.options.flags);
    }
    if (expected.options.original != actual.options.original) {
      return text_mismatch("options.original", expected.options.original, actual.options.original);
    }
    if (expected.options.pattern != actual.options.pattern) {
      return scalar_mismatch("options.pattern", expected.options.pattern, actual.options.pattern);
    }
    if (expected.options.estimated_capacitance != actual.options.estimated_capacitance) {
      return double_mismatch("options.estimated_capacitance", expected.options.estimated_capacitance,
                             actual.options.estimated_capacitance);
    }
    if (expected.options.frequency != actual.options.frequency) {
      return double_mismatch("options.frequency", expected.options.frequency, actual.options.frequency);
    }
    if (expected.options.xtalk != actual.options.xtalk) {
      return scalar_mismatch("options.xtalk", expected.options.xtalk, actual.options.xtalk);
    }
    if (expected.options.style != actual.options.style) {
      return scalar_mismatch("options.style", expected.options.style, actual.options.style);
    }
    if (expected.options.voltage != actual.options.voltage) {
      return scalar_mismatch("options.voltage", expected.options.voltage, actual.options.voltage);
    }
    if (expected.options.spacing_rules.size() != actual.options.spacing_rules.size()) {
      return size_mismatch("options.spacing_rules", expected.options.spacing_rules.size(), actual.options.spacing_rules.size());
    }
    for (std::size_t index = 0; index < expected.options.spacing_rules.size(); ++index) {
      const auto prefix = "options.spacing_rules[" + std::to_string(index) + "].";
      const auto& lhs = expected.options.spacing_rules[index];
      const auto& rhs = actual.options.spacing_rules[index];
      if (lhs.layer != rhs.layer) {
        return text_mismatch(prefix + "layer", lhs.layer, rhs.layer);
      }
      if (lhs.spacing != rhs.spacing) {
        return scalar_mismatch(prefix + "spacing", lhs.spacing, rhs.spacing);
      }
      if (lhs.flags != rhs.flags) {
        return scalar_mismatch(prefix + "flags", lhs.flags, rhs.flags);
      }
      if (lhs.range_left != rhs.range_left) {
        return scalar_mismatch(prefix + "range_left", lhs.range_left, rhs.range_left);
      }
      if (lhs.range_right != rhs.range_right) {
        return scalar_mismatch(prefix + "range_right", lhs.range_right, rhs.range_right);
      }
    }
  }
  if (expected.pins != actual.pins) {
    if (expected.pins.size() != actual.pins.size()) {
      return size_mismatch("pins", expected.pins.size(), actual.pins.size());
    }
    for (std::size_t index = 0; index < expected.pins.size(); ++index) {
      const auto prefix = "pins[" + std::to_string(index) + "].";
      const auto& lhs = expected.pins[index];
      const auto& rhs = actual.pins[index];
      if (lhs.instance != rhs.instance) {
        return text_mismatch(prefix + "instance", lhs.instance, rhs.instance);
      }
      if (lhs.master_term != rhs.master_term) {
        return text_mismatch(prefix + "master_term", lhs.master_term, rhs.master_term);
      }
      if (lhs.net != rhs.net) {
        return text_mismatch(prefix + "net", lhs.net, rhs.net);
      }
      if (lhs.special_net != rhs.special_net) {
        return text_mismatch(prefix + "special_net", lhs.special_net, rhs.special_net);
      }
    }
  }
  if (expected.segments != actual.segments) {
    if (expected.segments.size() != actual.segments.size()) {
      return size_mismatch("wire.segments", expected.segments.size(), actual.segments.size());
    }
    for (std::size_t index = 0; index < expected.segments.size(); ++index) {
      const auto prefix = "wire.segments[" + std::to_string(index) + "].";
      const auto& lhs = expected.segments[index];
      const auto& rhs = actual.segments[index];
      if (lhs.layer != rhs.layer) return text_mismatch(prefix + "layer", lhs.layer, rhs.layer);
      if (lhs.status != rhs.status) return scalar_mismatch(prefix + "status", lhs.status, rhs.status);
      if (lhs.shield_net != rhs.shield_net) return text_mismatch(prefix + "shield_net", lhs.shield_net, rhs.shield_net);
      if (lhs.path_flags != rhs.path_flags) return scalar_mismatch(prefix + "path_flags", lhs.path_flags, rhs.path_flags);
      if (lhs.width != rhs.width) return scalar_mismatch(prefix + "width", lhs.width, rhs.width);
      if (lhs.mask != rhs.mask) return scalar_mismatch(prefix + "mask", lhs.mask, rhs.mask);
      if (lhs.taper_rule != rhs.taper_rule) return text_mismatch(prefix + "taper_rule", lhs.taper_rule, rhs.taper_rule);
      if (lhs.shape != rhs.shape) return text_mismatch(prefix + "shape", lhs.shape, rhs.shape);
      if (lhs.style != rhs.style) return scalar_mismatch(prefix + "style", lhs.style, rhs.style);
      if (const auto mismatch = point_mismatch(prefix + "first", lhs.first, rhs.first)) return *mismatch;
      if (lhs.first_flags != rhs.first_flags) return scalar_mismatch(prefix + "first_flags", lhs.first_flags, rhs.first_flags);
      if (lhs.first_extension != rhs.first_extension) return scalar_mismatch(prefix + "first_extension", lhs.first_extension, rhs.first_extension);
      if (const auto mismatch = point_mismatch(prefix + "second", lhs.second, rhs.second)) return *mismatch;
      if (lhs.second_flags != rhs.second_flags) return scalar_mismatch(prefix + "second_flags", lhs.second_flags, rhs.second_flags);
      if (lhs.second_extension != rhs.second_extension) return scalar_mismatch(prefix + "second_extension", lhs.second_extension, rhs.second_extension);
    }
  }
  if (expected.points != actual.points) {
    if (expected.points.size() != actual.points.size()) {
      return size_mismatch("wire.points", expected.points.size(), actual.points.size());
    }
    for (std::size_t index = 0; index < expected.points.size(); ++index) {
      const auto prefix = "wire.points[" + std::to_string(index) + "].";
      const auto& lhs = expected.points[index];
      const auto& rhs = actual.points[index];
      if (lhs.layer != rhs.layer) return text_mismatch(prefix + "layer", lhs.layer, rhs.layer);
      if (lhs.status != rhs.status) return scalar_mismatch(prefix + "status", lhs.status, rhs.status);
      if (lhs.shield_net != rhs.shield_net) return text_mismatch(prefix + "shield_net", lhs.shield_net, rhs.shield_net);
      if (lhs.path_flags != rhs.path_flags) return scalar_mismatch(prefix + "path_flags", lhs.path_flags, rhs.path_flags);
      if (lhs.width != rhs.width) return scalar_mismatch(prefix + "width", lhs.width, rhs.width);
      if (lhs.mask != rhs.mask) return scalar_mismatch(prefix + "mask", lhs.mask, rhs.mask);
      if (lhs.taper_rule != rhs.taper_rule) return text_mismatch(prefix + "taper_rule", lhs.taper_rule, rhs.taper_rule);
      if (lhs.shape != rhs.shape) return text_mismatch(prefix + "shape", lhs.shape, rhs.shape);
      if (lhs.style != rhs.style) return scalar_mismatch(prefix + "style", lhs.style, rhs.style);
      if (const auto mismatch = point_mismatch(prefix + "position", lhs.position, rhs.position)) return *mismatch;
      if (lhs.flags != rhs.flags) return scalar_mismatch(prefix + "flags", lhs.flags, rhs.flags);
      if (lhs.extension != rhs.extension) return scalar_mismatch(prefix + "extension", lhs.extension, rhs.extension);
    }
  }
  if (expected.wire_vias != actual.wire_vias) {
    if (expected.wire_vias.size() != actual.wire_vias.size()) return size_mismatch("wire.vias", expected.wire_vias.size(), actual.wire_vias.size());
    for (std::size_t index = 0; index < expected.wire_vias.size(); ++index) {
      const auto prefix = "wire.vias[" + std::to_string(index) + "].";
      const auto& lhs = expected.wire_vias[index];
      const auto& rhs = actual.wire_vias[index];
      if (lhs.status != rhs.status) return scalar_mismatch(prefix + "status", lhs.status, rhs.status);
      if (lhs.shield_net != rhs.shield_net) return text_mismatch(prefix + "shield_net", lhs.shield_net, rhs.shield_net);
      if (const auto mismatch = point_mismatch(prefix + "anchor", lhs.anchor, rhs.anchor)) return *mismatch;
      if (lhs.anchor_flags != rhs.anchor_flags) return scalar_mismatch(prefix + "anchor_flags", lhs.anchor_flags, rhs.anchor_flags);
      if (lhs.anchor_extension != rhs.anchor_extension) return scalar_mismatch(prefix + "anchor_extension", lhs.anchor_extension, rhs.anchor_extension);
      if (lhs.name != rhs.name) return text_mismatch(prefix + "name", lhs.name, rhs.name);
      if (lhs.orientation != rhs.orientation) return scalar_mismatch(prefix + "orientation", lhs.orientation, rhs.orientation);
      if (lhs.flags != rhs.flags) return scalar_mismatch(prefix + "flags", lhs.flags, rhs.flags);
      if (lhs.top_mask != rhs.top_mask) return scalar_mismatch(prefix + "top_mask", lhs.top_mask, rhs.top_mask);
      if (lhs.cut_mask != rhs.cut_mask) return scalar_mismatch(prefix + "cut_mask", lhs.cut_mask, rhs.cut_mask);
      if (lhs.bottom_mask != rhs.bottom_mask) return scalar_mismatch(prefix + "bottom_mask", lhs.bottom_mask, rhs.bottom_mask);
      if (lhs.rows != rhs.rows) return scalar_mismatch(prefix + "rows", lhs.rows, rhs.rows);
      if (lhs.columns != rhs.columns) return scalar_mismatch(prefix + "columns", lhs.columns, rhs.columns);
      if (lhs.step_x != rhs.step_x) return scalar_mismatch(prefix + "step_x", lhs.step_x, rhs.step_x);
      if (lhs.step_y != rhs.step_y) return scalar_mismatch(prefix + "step_y", lhs.step_y, rhs.step_y);
    }
  }
  if (expected.wire_rectangles != actual.wire_rectangles) {
    if (expected.wire_rectangles.size() != actual.wire_rectangles.size()) return size_mismatch("wire.rectangles", expected.wire_rectangles.size(), actual.wire_rectangles.size());
    for (std::size_t index = 0; index < expected.wire_rectangles.size(); ++index) {
      const auto prefix = "wire.rectangles[" + std::to_string(index) + "].";
      const auto& lhs = expected.wire_rectangles[index];
      const auto& rhs = actual.wire_rectangles[index];
      if (lhs.layer != rhs.layer) return text_mismatch(prefix + "layer", lhs.layer, rhs.layer);
      if (lhs.status != rhs.status) return scalar_mismatch(prefix + "status", lhs.status, rhs.status);
      if (lhs.shield_net != rhs.shield_net) return text_mismatch(prefix + "shield_net", lhs.shield_net, rhs.shield_net);
      if (lhs.path_flags != rhs.path_flags) return scalar_mismatch(prefix + "path_flags", lhs.path_flags, rhs.path_flags);
      if (lhs.width != rhs.width) return scalar_mismatch(prefix + "width", lhs.width, rhs.width);
      if (lhs.mask != rhs.mask) return scalar_mismatch(prefix + "mask", lhs.mask, rhs.mask);
      if (lhs.taper_rule != rhs.taper_rule) return text_mismatch(prefix + "taper_rule", lhs.taper_rule, rhs.taper_rule);
      if (lhs.shape != rhs.shape) return text_mismatch(prefix + "shape", lhs.shape, rhs.shape);
      if (lhs.style != rhs.style) return scalar_mismatch(prefix + "style", lhs.style, rhs.style);
      if (const auto mismatch = rect_mismatch(prefix + "rectangle", lhs.rectangle, rhs.rectangle)) return *mismatch;
    }
  }
  if (expected.rectangles != actual.rectangles) {
    if (expected.rectangles.size() != actual.rectangles.size()) return size_mismatch("geometry.rectangles", expected.rectangles.size(), actual.rectangles.size());
    for (std::size_t index = 0; index < expected.rectangles.size(); ++index) {
      const auto prefix = "geometry.rectangles[" + std::to_string(index) + "].";
      const auto& lhs = expected.rectangles[index];
      const auto& rhs = actual.rectangles[index];
      if (lhs.layer != rhs.layer) return text_mismatch(prefix + "layer", lhs.layer, rhs.layer);
      if (const auto mismatch = rect_mismatch(prefix + "rectangle", lhs.rectangle, rhs.rectangle)) return *mismatch;
      if (lhs.status != rhs.status) return scalar_mismatch(prefix + "status", lhs.status, rhs.status);
      if (lhs.flags != rhs.flags) return scalar_mismatch(prefix + "flags", lhs.flags, rhs.flags);
      if (lhs.mask != rhs.mask) return scalar_mismatch(prefix + "mask", lhs.mask, rhs.mask);
      if (lhs.shield_net != rhs.shield_net) return text_mismatch(prefix + "shield_net", lhs.shield_net, rhs.shield_net);
      if (lhs.shape != rhs.shape) return text_mismatch(prefix + "shape", lhs.shape, rhs.shape);
    }
  }
  if (expected.polygons != actual.polygons) {
    if (expected.polygons.size() != actual.polygons.size()) return size_mismatch("geometry.polygons", expected.polygons.size(), actual.polygons.size());
    for (std::size_t index = 0; index < expected.polygons.size(); ++index) {
      const auto prefix = "geometry.polygons[" + std::to_string(index) + "].";
      const auto& lhs = expected.polygons[index];
      const auto& rhs = actual.polygons[index];
      if (lhs.layer != rhs.layer) return text_mismatch(prefix + "layer", lhs.layer, rhs.layer);
      if (lhs.points.size() != rhs.points.size()) return size_mismatch(prefix + "points", lhs.points.size(), rhs.points.size());
      for (std::size_t point_index = 0; point_index < lhs.points.size(); ++point_index) {
        if (const auto mismatch = point_mismatch(prefix + "points[" + std::to_string(point_index) + "]", lhs.points[point_index], rhs.points[point_index])) return *mismatch;
      }
      if (lhs.status != rhs.status) return scalar_mismatch(prefix + "status", lhs.status, rhs.status);
      if (lhs.flags != rhs.flags) return scalar_mismatch(prefix + "flags", lhs.flags, rhs.flags);
      if (lhs.mask != rhs.mask) return scalar_mismatch(prefix + "mask", lhs.mask, rhs.mask);
      if (lhs.shield_net != rhs.shield_net) return text_mismatch(prefix + "shield_net", lhs.shield_net, rhs.shield_net);
      if (lhs.shape != rhs.shape) return text_mismatch(prefix + "shape", lhs.shape, rhs.shape);
    }
  }
  if (expected.net_vias != actual.net_vias) {
    if (expected.net_vias.size() != actual.net_vias.size()) return size_mismatch("geometry.vias", expected.net_vias.size(), actual.net_vias.size());
    for (std::size_t index = 0; index < expected.net_vias.size(); ++index) {
      const auto prefix = "geometry.vias[" + std::to_string(index) + "].";
      const auto& lhs = expected.net_vias[index];
      const auto& rhs = actual.net_vias[index];
      if (lhs.name != rhs.name) return text_mismatch(prefix + "name", lhs.name, rhs.name);
      if (lhs.origins.size() != rhs.origins.size()) return size_mismatch(prefix + "origins", lhs.origins.size(), rhs.origins.size());
      for (std::size_t point_index = 0; point_index < lhs.origins.size(); ++point_index) {
        if (const auto mismatch = point_mismatch(prefix + "origins[" + std::to_string(point_index) + "]", lhs.origins[point_index], rhs.origins[point_index])) return *mismatch;
      }
      if (lhs.orientation != rhs.orientation) return scalar_mismatch(prefix + "orientation", lhs.orientation, rhs.orientation);
      if (lhs.status != rhs.status) return scalar_mismatch(prefix + "status", lhs.status, rhs.status);
      if (lhs.flags != rhs.flags) return scalar_mismatch(prefix + "flags", lhs.flags, rhs.flags);
      if (lhs.top_mask != rhs.top_mask) return scalar_mismatch(prefix + "top_mask", lhs.top_mask, rhs.top_mask);
      if (lhs.cut_mask != rhs.cut_mask) return scalar_mismatch(prefix + "cut_mask", lhs.cut_mask, rhs.cut_mask);
      if (lhs.bottom_mask != rhs.bottom_mask) return scalar_mismatch(prefix + "bottom_mask", lhs.bottom_mask, rhs.bottom_mask);
      if (lhs.shield_net != rhs.shield_net) return text_mismatch(prefix + "shield_net", lhs.shield_net, rhs.shield_net);
      if (lhs.shape != rhs.shape) return text_mismatch(prefix + "shape", lhs.shape, rhs.shape);
    }
  }
  return "unknown net difference";
}

inline std::optional<std::string> basicNetMismatch(const DesignStore& expected, DesignNetId expected_id,
                                                   const DesignStore& actual, DesignNetId actual_id)
{
  const auto& expected_net = expected.netlistStorage().net(expected_id);
  const auto& actual_net = actual.netlistStorage().net(actual_id);
  const auto text_mismatch = [](std::string_view field, const std::string& lhs, const std::string& rhs) {
    return "field=" + std::string(field) + " expected=\"" + lhs + "\" actual=\"" + rhs + "\"";
  };
  const auto scalar_mismatch = [](std::string_view field, auto lhs, auto rhs) {
    return "field=" + std::string(field) + " expected=" + std::to_string(static_cast<int64_t>(lhs))
           + " actual=" + std::to_string(static_cast<int64_t>(rhs));
  };
  if (expected_net.name != actual_net.name) {
    return text_mismatch("name", expected_net.name, actual_net.name);
  }
  if (expected.netlistStorage().isSpecialNet(expected_id) != actual.netlistStorage().isSpecialNet(actual_id)) {
    return scalar_mismatch("special", expected.netlistStorage().isSpecialNet(expected_id),
                           actual.netlistStorage().isSpecialNet(actual_id));
  }
  if (canonicalSignalUse(expected_net.use) != canonicalSignalUse(actual_net.use)) {
    return scalar_mismatch("use", canonicalSignalUse(expected_net.use), canonicalSignalUse(actual_net.use));
  }
  if (expected_net.source != actual_net.source) {
    return scalar_mismatch("source", expected_net.source, actual_net.source);
  }
  if (expected_net.flags != actual_net.flags) {
    return scalar_mismatch("flags", expected_net.flags, actual_net.flags);
  }
  if (expected_net.weight != actual_net.weight) {
    return scalar_mismatch("weight", expected_net.weight, actual_net.weight);
  }
  const auto expected_ndr = expected_net.design_non_default_rule
                                ? detail::ndrName(expected, expected_net.design_non_default_rule)
                                : detail::ndrName(expected, expected_net.non_default_rule);
  const auto actual_ndr = actual_net.design_non_default_rule
                              ? detail::ndrName(actual, actual_net.design_non_default_rule)
                              : detail::ndrName(actual, actual_net.non_default_rule);
  if (expected_ndr != actual_ndr) {
    return text_mismatch("non_default_rule", expected_ndr, actual_ndr);
  }
  return std::nullopt;
}

inline std::size_t netComparisonThreadCount(std::size_t pair_count)
{
  if (pair_count == 0u) {
    return 0u;
  }
  std::size_t configured = 8u;
  if (const char* value = std::getenv("ECCDB_NET_COMPARE_THREADS"); value != nullptr && *value != '\0') {
    char* end = nullptr;
    const auto parsed = std::strtoull(value, &end, 10);
    if (end != value && *end == '\0') {
      configured = parsed == 0u ? 1u : static_cast<std::size_t>(parsed);
    }
  }
  return std::max<std::size_t>(1u, std::min({pair_count, configured, std::size_t{128}}));
}

class NetDiffProgress
{
 public:
  NetDiffProgress(std::size_t total, const std::atomic<std::size_t>& completed, std::atomic<bool>& done)
      : _total(total), _completed(completed), _done(done)
  {
#if defined(_WIN32)
    _interactive = ::_isatty(::_fileno(stderr)) != 0;
#else
    _interactive = ::isatty(::fileno(stderr)) != 0;
#endif
  }

  void start()
  {
    _started_at = std::chrono::steady_clock::now();
    _thread = std::thread([this] {
      const auto interval = _interactive ? std::chrono::milliseconds(100) : std::chrono::seconds(1);
      render();
      while (!_done.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(interval);
        if (!_done.load(std::memory_order_acquire)) {
          render();
        }
      }
      render(true);
    });
  }

  void finish()
  {
    _done.store(true, std::memory_order_release);
    if (_thread.joinable()) {
      _thread.join();
    }
  }

 private:
  void render(bool final = false)
  {
    const auto completed = std::min(_completed.load(std::memory_order_relaxed), _total);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - _started_at).count();
    const auto rate = elapsed > 0.0 ? static_cast<double>(completed) / elapsed : 0.0;
    const auto fraction = _total == 0u ? 1.0 : static_cast<double>(completed) / static_cast<double>(_total);
    const auto percent = 100.0 * fraction;
    const auto remaining = _total > completed ? _total - completed : 0u;
    const auto eta = rate > 0.0 ? static_cast<double>(remaining) / rate : 0.0;
    constexpr std::size_t bar_width = 32u;
    const auto filled = static_cast<std::size_t>(fraction * static_cast<double>(bar_width));

    if (_interactive) {
      std::fprintf(stderr, "\r\033[2K[net-diff] [");
      for (std::size_t index = 0; index < bar_width; ++index) {
        std::fputc(index < filled ? '#' : '-', stderr);
      }
      if (completed >= _total || rate > 0.0) {
        std::fprintf(stderr, "] %6.2f%% %zu/%zu nets %.0f nets/s elapsed %.1fs ETA %.1fs", percent, completed, _total, rate,
                     elapsed, eta);
      } else {
        std::fprintf(stderr, "] %6.2f%% %zu/%zu nets %.0f nets/s elapsed %.1fs ETA --", percent, completed, _total, rate, elapsed);
      }
      if (final) {
        std::fputc('\n', stderr);
      }
      std::fflush(stderr);
      return;
    }

    if (final || completed != _last_logged_completed) {
      if (completed >= _total || rate > 0.0) {
        std::fprintf(stderr, "[net-diff] compared %zu/%zu nets (%.2f%%, %.0f nets/s, elapsed %.1fs, ETA %.1fs)\n", completed,
                     _total, percent, rate, elapsed, eta);
      } else {
        std::fprintf(stderr, "[net-diff] compared %zu/%zu nets (%.2f%%, %.0f nets/s, elapsed %.1fs, ETA --)\n", completed,
                     _total, percent, rate, elapsed);
      }
      std::fflush(stderr);
      _last_logged_completed = completed;
    }
  }

  std::size_t _total = 0u;
  const std::atomic<std::size_t>& _completed;
  std::atomic<bool>& _done;
  std::thread _thread;
  std::chrono::steady_clock::time_point _started_at;
  std::size_t _last_logged_completed = std::numeric_limits<std::size_t>::max();
  bool _interactive = false;
};

inline std::optional<NetMismatch> compareNetPairs(const DesignStore& expected, const DesignStore& actual,
                                                  const std::vector<NetPair>& pairs)
{
  const auto thread_count = netComparisonThreadCount(pairs.size());
  std::atomic<std::size_t> next_pair{0u};
  std::atomic<std::size_t> completed_pairs{0u};
  std::atomic<bool> stop_requested{false};
  std::atomic<bool> progress_done{false};
  std::atomic<std::size_t> first_mismatch_index{pairs.size()};
  NetDiffProgress progress(pairs.size(), completed_pairs, progress_done);
  progress.start();
  std::vector<std::thread> workers;
  workers.reserve(thread_count);

  for (std::size_t worker_index = 0; worker_index < thread_count; ++worker_index) {
    workers.emplace_back([&] {
      constexpr std::size_t chunk_size = 32u;
      while (true) {
        if (stop_requested.load(std::memory_order_acquire)) {
          break;
        }
        const auto begin = next_pair.fetch_add(chunk_size, std::memory_order_relaxed);
        if (begin >= pairs.size()) {
          break;
        }
        const auto end = std::min(begin + chunk_size, pairs.size());
        for (std::size_t index = begin; index < end; ++index) {
          if (stop_requested.load(std::memory_order_acquire)) {
            break;
          }
          const auto& pair = pairs[index];
          completed_pairs.fetch_add(1u, std::memory_order_relaxed);
          const auto basic_mismatch = basicNetMismatch(expected, pair.expected, actual, pair.actual);
          if (basic_mismatch) {
            auto unset_index = pairs.size();
            if (first_mismatch_index.compare_exchange_strong(unset_index, index, std::memory_order_acq_rel)) {
              stop_requested.store(true, std::memory_order_release);
            }
            break;
          }
          const auto expected_net = structured::canonicalNet(expected, pair.expected);
          const auto actual_net = structured::canonicalNet(actual, pair.actual);
          if (expected_net != actual_net) {
            auto unset_index = pairs.size();
            if (first_mismatch_index.compare_exchange_strong(unset_index, index, std::memory_order_acq_rel)) {
              stop_requested.store(true, std::memory_order_release);
            }
            break;
          }
        }
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  progress.finish();
  const auto mismatch_index = first_mismatch_index.load(std::memory_order_acquire);
  if (mismatch_index >= pairs.size()) {
    return std::nullopt;
  }
  const auto& mismatch_pair = pairs[mismatch_index];
  if (const auto basic_mismatch = basicNetMismatch(expected, mismatch_pair.expected, actual, mismatch_pair.actual)) {
    std::fprintf(stderr, "[net-diff] first mismatch at %zu/%zu nets\n", mismatch_index + 1u, pairs.size());
    return NetMismatch{.name = mismatch_pair.name, .section = *basic_mismatch};
  }
  const auto expected_net = structured::canonicalNet(expected, mismatch_pair.expected);
  const auto actual_net = structured::canonicalNet(actual, mismatch_pair.actual);
  std::fprintf(stderr, "[net-diff] first mismatch at %zu/%zu nets\n", mismatch_index + 1u, pairs.size());
  return NetMismatch{.name = mismatch_pair.name, .section = canonicalNetMismatch(expected_net, actual_net)};
}

inline void expectStructuredNetSemantics(const DesignStore& expected, const DesignStore& actual)
{
  std::size_t expected_count = 0;
  std::size_t actual_count = 0;
  std::vector<NetPair> pairs;
  for (const auto id : expected.netlistStorage().nets()) {
    if (!isSemanticallyComparableNet(expected, id)) {
      continue;
    }
    ++expected_count;
    const auto& net = expected.netlistStorage().net(id);
    const auto actual_id = expected.netlistStorage().isSpecialNet(id) ? actual.netlistStorage().findSpecialNet(net.name)
                                                                       : actual.netlistStorage().findRegularNet(net.name);
    if (!actual_id) {
      ADD_FAILURE() << "net " << net.name << " differs: missing net";
      return;
    }
    pairs.push_back(NetPair{.expected = id, .actual = actual_id, .name = net.name});
  }
  for (const auto id : actual.netlistStorage().nets()) {
    actual_count += isSemanticallyComparableNet(actual, id);
  }
  ASSERT_EQ(expected_count, actual_count);

  const auto mismatch = compareNetPairs(expected, actual, pairs);
  if (mismatch) {
    ADD_FAILURE() << "net " << mismatch->name << " differs: " << mismatch->section;
  }
}

}  // namespace eccdb::test
