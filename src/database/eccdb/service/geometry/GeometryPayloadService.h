// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "geometry/GeometryQueryService.h"

namespace eccdb {

// Copy of the optional DEF route fields.  Keeping this separate from the
// canonical storage component makes every payload batch safe to retain after
// the database is edited.
struct GeometryPayloadRouteAttributes
{
  int32_t width = 0;
  uint32_t mask = 0;
  std::string taper_rule;
  std::string shape;
  int32_t style = 0;
};

struct GeometryPayloadSegment
{
  uint32_t header_index = UINT32_MAX;
  Point p0;
  Point p1;
  int32_t width = 0;
  // Resolved along-centerline cap distances. These already account for
  // SHAPE truncation, endpoint extensions, direction, and default width.
  double begin_cap = 0.0;
  double end_cap = 0.0;
  uint32_t begin_flags = 0;
  uint32_t end_flags = 0;
  int32_t begin_extension = 0;
  int32_t end_extension = 0;
  uint32_t mask = 0;
  int32_t style = 0;
  std::optional<GeometryPayloadRouteAttributes> attributes;
};

struct GeometryPayloadRectangle
{
  uint32_t header_index = UINT32_MAX;
  uint64_t layer = UINT64_MAX;
  Rect rectangle;
  uint64_t via_layer = UINT64_MAX;
  uint32_t flags = 0;
  uint32_t mask = 0;
  int32_t spacing = 0;
  int32_t design_rule_width = 0;
  std::optional<GeometryPayloadRouteAttributes> route_attributes;
};

struct GeometryPayloadPolygon
{
  uint32_t header_index = UINT32_MAX;
  uint64_t layer = UINT64_MAX;
  uint32_t point_offset = 0;
  uint32_t point_count = 0;
  uint32_t flags = 0;
  uint32_t mask = 0;
  int32_t spacing = 0;
  int32_t design_rule_width = 0;
  std::optional<GeometryPayloadRouteAttributes> route_attributes;
};

struct GeometryPayloadPoint
{
  uint32_t header_index = UINT32_MAX;
  uint64_t layer = UINT64_MAX;
  Point point;
  uint32_t flags = 0;
  int32_t extension = 0;
  uint32_t mask = 0;
  int32_t style = 0;
  std::optional<GeometryPayloadRouteAttributes> attributes;
};

struct GeometryPayloadVia
{
  uint32_t header_index = UINT32_MAX;
  Point origin;
  uint64_t tech_via = UINT64_MAX;
  uint64_t design_via = UINT64_MAX;
  uint8_t orientation = 0;
  uint32_t flags = 0;
  uint32_t top_mask = 0;
  uint32_t cut_mask = 0;
  uint32_t bottom_mask = 0;
  uint32_t rows = 1;
  uint32_t columns = 1;
  int32_t step_x = 0;
  int32_t step_y = 0;
  uint64_t exit_layer = UINT64_MAX;
  std::optional<GeometryPayloadRouteAttributes> route_attributes;
};

// A detached exact-geometry snapshot.  `headers` owns the common metadata;
// each primitive stores the index of the header it was expanded from.  VIA
// templates may expand into several rectangle/polygon entries on distinct
// physical layers, while retaining one logical VIA placement record.
struct GeometryPayloadBatch
{
  std::vector<GeometryShapeRef> headers;
  std::vector<GeometryPayloadSegment> segments;
  std::vector<GeometryPayloadRectangle> rectangles;
  std::vector<GeometryPayloadPolygon> polygons;
  std::vector<Point> polygon_points;
  std::vector<GeometryPayloadPoint> points;
  std::vector<GeometryPayloadVia> vias;
  uint64_t geometry_epoch = 0;
};

class GeometryPayloadService
{
 public:
  explicit GeometryPayloadService(DatabaseContext& context) noexcept : _context(&context) {}

  [[nodiscard]] GeometryPayloadBatch fetchShapes(std::span<const uint64_t> ids) const;
  [[nodiscard]] GeometryPayloadBatch queryGeometry(Rect box, const GeometryQueryFilter& filter = {}) const;

 private:
  DatabaseContext* _context;
};

}  // namespace eccdb
