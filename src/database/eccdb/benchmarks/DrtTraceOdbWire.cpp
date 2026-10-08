// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
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

#include "DrtTraceOdb.h"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "odb/db.h"
#include "odb/dbWireCodec.h"

namespace drt_trace {
namespace {

struct ActivePoint
{
  int x = 0;
  int y = 0;
  std::optional<int32_t> extension;
};

[[noreturn]] void unsupported(const odb::dbNet* net, const std::string& detail)
{
  const std::string name = net == nullptr ? "<null>" : net->getName();
  throw std::runtime_error("unsupported ODB wire for net " + name + ": " + detail);
}

void requireRoutingLayer(odb::dbNet* net, odb::dbTechLayer* layer)
{
  if (layer == nullptr) unsupported(net, "missing routing layer");
  if (layer->getType() != odb::dbTechLayerType::ROUTING) {
    unsupported(net, "non-routing layer " + layer->getName());
  }
}

void requireNoMasks(odb::dbNet* net, const odb::dbWireDecoder& decoder)
{
  if (decoder.getColor().has_value() || decoder.getViaColor().has_value()) {
    unsupported(net, "mask metadata is outside the trace v1 profile");
  }
}

int32_t checkedCoordinate(odb::dbNet* net, int origin, int delta)
{
  const int64_t coordinate = static_cast<int64_t>(origin) + static_cast<int64_t>(delta);
  if (coordinate < INT32_MIN || coordinate > INT32_MAX) {
    unsupported(net, "RECT coordinate overflows trace v1 integer range");
  }
  return static_cast<int32_t>(coordinate);
}

}  // namespace

std::vector<Shape> readOdbWire(odb::dbNet* net)
{
  if (net == nullptr) return {};
  if (net->isSpecial()) unsupported(net, "special nets are outside the trace v1 profile");
  if (net->getNonDefaultRule() != nullptr) unsupported(net, "non-default routing rules are outside the trace v1 profile");

  odb::dbWire* wire = net->getWire();
  if (wire == nullptr) return {};
  if (net->getWireType() != odb::dbWireType::ROUTED) {
    unsupported(net, "net wire type is not ROUTED");
  }

  std::vector<Shape> shapes;
  odb::dbWireDecoder decoder;
  decoder.begin(wire);
  odb::dbTechLayer* layer = nullptr;
  std::optional<ActivePoint> active;
  int points_in_path = 0;

  for (auto opcode = decoder.next(); opcode != odb::dbWireDecoder::END_DECODE; opcode = decoder.next()) {
    switch (opcode) {
      case odb::dbWireDecoder::PATH:
        requireNoMasks(net, decoder);
        if (decoder.getWireType() != odb::dbWireType::ROUTED) {
          unsupported(net, "PATH wire type is not ROUTED");
        }
        layer = decoder.getLayer();
        requireRoutingLayer(net, layer);
        active.reset();
        points_in_path = 0;
        break;

      case odb::dbWireDecoder::POINT:
      case odb::dbWireDecoder::POINT_EXT: {
        requireNoMasks(net, decoder);
        if (layer == nullptr) unsupported(net, "point has no PATH layer");
        ActivePoint point;
        if (opcode == odb::dbWireDecoder::POINT_EXT) {
          int extension = 0;
          decoder.getPoint(point.x, point.y, extension);
          point.extension = extension;
        } else {
          decoder.getPoint(point.x, point.y);
        }
        int property = 0;
        if (points_in_path != 0 && wire->getProperty(decoder.getJunctionId(), property) && property != 0) {
          unsupported(net, "point properties are outside the trace v1 profile");
        }
        ++points_in_path;
        if (active && (active->x != point.x || active->y != point.y)) {
          shapes.push_back({.kind = "segment",
                            .layer = layer->getName(),
                            .via = {},
                            .x1 = active->x,
                            .y1 = active->y,
                            .x2 = point.x,
                            .y2 = point.y,
                            .begin_extension = active->extension,
                            .end_extension = point.extension,
                            .tech_via = true});
        }
        active = std::move(point);
        break;
      }

      case odb::dbWireDecoder::VIA:
      case odb::dbWireDecoder::TECH_VIA: {
        requireNoMasks(net, decoder);
        if (!active) unsupported(net, "VIA has no anchor point");
        odb::dbTechLayer* bottom = nullptr;
        std::string via_name;
        bool tech_via = opcode == odb::dbWireDecoder::TECH_VIA;
        if (tech_via) {
          auto* via = decoder.getTechVia();
          if (via == nullptr) unsupported(net, "missing technology VIA");
          bottom = via->getBottomLayer();
          via_name = via->getName();
        } else {
          auto* via = decoder.getVia();
          if (via == nullptr) unsupported(net, "missing block VIA");
          if (via->isViaRotated() || via->getOrient().getValue() != odb::dbOrientType::R0) {
            unsupported(net, "oriented block VIA " + via->getName());
          }
          bottom = via->getBottomLayer();
          via_name = via->getName();
        }
        requireRoutingLayer(net, bottom);
        shapes.push_back({.kind = "via",
                          .layer = bottom->getName(),
                          .via = std::move(via_name),
                          .x1 = active->x,
                          .y1 = active->y,
                          .x2 = active->x,
                          .y2 = active->y,
                          .begin_extension = {},
                          .end_extension = {},
                          .tech_via = tech_via});
        layer = decoder.getLayer();
        requireRoutingLayer(net, layer);
        break;
      }

      case odb::dbWireDecoder::RECT: {
        requireNoMasks(net, decoder);
        if (!active || layer == nullptr) unsupported(net, "RECT has no anchor point or routing layer");
        int dx1 = 0;
        int dy1 = 0;
        int dx2 = 0;
        int dy2 = 0;
        decoder.getRect(dx1, dy1, dx2, dy2);
        const int32_t x1 = checkedCoordinate(net, active->x, std::min(dx1, dx2));
        const int32_t y1 = checkedCoordinate(net, active->y, std::min(dy1, dy2));
        const int32_t x2 = checkedCoordinate(net, active->x, std::max(dx1, dx2));
        const int32_t y2 = checkedCoordinate(net, active->y, std::max(dy1, dy2));
        shapes.push_back({.kind = "patch",
                          .layer = layer->getName(),
                          .via = {},
                          .x1 = x1,
                          .y1 = y1,
                          .x2 = x2,
                          .y2 = y2,
                          .begin_extension = {},
                          .end_extension = {},
                          .tech_via = true});
        break;
      }

      case odb::dbWireDecoder::JUNCTION:
        unsupported(net, "JUNCTION paths are outside the trace v1 profile");
      case odb::dbWireDecoder::SHORT:
        unsupported(net, "SHORT paths are outside the trace v1 profile");
      case odb::dbWireDecoder::VWIRE:
        unsupported(net, "virtual-wire paths are outside the trace v1 profile");
      case odb::dbWireDecoder::RULE:
        unsupported(net, "per-path routing rules are outside the trace v1 profile");
      case odb::dbWireDecoder::ITERM:
        unsupported(net, "ITERM wire opcodes are outside the trace v1 profile");
      case odb::dbWireDecoder::BTERM:
        unsupported(net, "BTERM wire opcodes are outside the trace v1 profile");
      case odb::dbWireDecoder::END_DECODE:
        break;
    }
  }
  requireNoMasks(net, decoder);
  return shapes;
}

}  // namespace drt_trace
