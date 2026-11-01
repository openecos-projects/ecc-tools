// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "OdbTraceDesignProjection.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "odb/db.h"
#include "odb/dbObject.h"

namespace eccdb::tritonroute::odb_trace_projection {
namespace {
namespace json = boost::json;

[[nodiscard]] Projection failure(std::string reason)
{
  Projection result;
  result.unrecordable.emplace_back(std::move(reason));
  return result;
}

[[nodiscard]] Projection value(json::value expected)
{
  Projection result;
  result.expected.emplace(std::move(expected));
  return result;
}

void appendReasons(Projection& destination,
                   const Projection& source,
                   const std::string& prefix)
{
  for (const auto& reason : source.unrecordable) {
    destination.unrecordable.emplace_back(prefix + reason);
  }
  for (const auto& reason : source.ignored) {
    destination.ignored.emplace_back(prefix + reason);
  }
}

void canonicalize(json::array& values)
{
  std::sort(values.begin(), values.end(), [](const json::value& lhs,
                                              const json::value& rhs) {
    return json::serialize(lhs) < json::serialize(rhs);
  });
}

[[nodiscard]] std::string objectName(const odb::dbNet* net)
{
  return net == nullptr ? "<null>" : net->getName();
}

[[nodiscard]] std::string objectName(const odb::dbBTerm* term)
{
  return term == nullptr ? "<null>" : term->getName();
}

[[nodiscard]] std::string objectName(const odb::dbITerm* term)
{
  return term == nullptr ? "<null>" : term->getName();
}

[[nodiscard]] std::string viaName(odb::dbObject* object,
                                  std::string& kind)
{
  if (object == nullptr) {
    return {};
  }
  if (object->getObjectType() == odb::dbObjectType::dbViaObj) {
    kind = "design";
    return static_cast<odb::dbVia*>(object)->getName();
  }
  if (object->getObjectType() == odb::dbObjectType::dbTechViaObj) {
    kind = "tech";
    return static_cast<odb::dbTechVia*>(object)->getName();
  }
  return {};
}

[[nodiscard]] Projection specialPath(odb::dbSBox* box)
{
  if (box == nullptr) {
    return failure("null special-wire shape");
  }
  auto* layer = box->getTechLayer();
  if (layer == nullptr) {
    return failure("special-wire path has no technology layer");
  }
  if (layer->isBackside()) {
    auto result = value(json::value(nullptr));
    result.ignored.emplace_back("special-wire path on backside layer "
                                + layer->getName());
    return result;
  }

  int x1 = box->xMin();
  int y1 = box->yMin();
  int x2 = box->xMax();
  int y2 = box->yMax();
  const uint32_t dx = box->getDX();
  const uint32_t dy = box->getDY();
  uint32_t width = 0;

  switch (box->getDirection()) {
    case odb::dbSBox::UNDEFINED: {
      const bool dx_even = (dx & 1u) == 0u;
      const bool dy_even = (dy & 1u) == 0u;
      if (dx_even && dy_even) {
        if (dy < dx) {
          width = dy;
          const uint32_t half = dy >> 1u;
          y1 += static_cast<int>(half);
          y2 -= static_cast<int>(half);
        } else {
          width = dx;
          const uint32_t half = dx >> 1u;
          x1 += static_cast<int>(half);
          x2 -= static_cast<int>(half);
        }
      } else if (dx_even) {
        width = dx;
        const uint32_t half = dx >> 1u;
        x1 += static_cast<int>(half);
        x2 -= static_cast<int>(half);
      } else if (dy_even) {
        width = dy;
        const uint32_t half = dy >> 1u;
        y1 += static_cast<int>(half);
        y2 -= static_cast<int>(half);
      } else {
        return failure("special-wire path has odd dimensions in both axes");
      }
      break;
    }
    case odb::dbSBox::HORIZONTAL: {
      width = dy;
      const uint32_t half = dy >> 1u;
      y1 += static_cast<int>(half);
      y2 -= static_cast<int>(half);
      break;
    }
    case odb::dbSBox::VERTICAL: {
      width = dx;
      const uint32_t half = dx >> 1u;
      x1 += static_cast<int>(half);
      x2 -= static_cast<int>(half);
      break;
    }
    case odb::dbSBox::OCTILINEAR: {
      const odb::Oct oct = box->getOct();
      x1 = oct.getCenterLow().getX();
      y1 = oct.getCenterLow().getY();
      x2 = oct.getCenterHigh().getX();
      y2 = oct.getCenterHigh().getY();
      width = static_cast<uint32_t>(oct.getWidth());
      break;
    }
    default:
      return failure("special-wire path has an unknown direction");
  }

  return value(json::array{"path",
                           layer->getName(),
                           box->getWireShapeType().getString(),
                           x1,
                           y1,
                           x2,
                           y2,
                           width});
}

[[nodiscard]] Projection specialVia(odb::dbSBox* box)
{
  if (box == nullptr) {
    return failure("null special-wire via");
  }
  auto* tech_via = box->getTechVia();
  auto* design_via = box->getBlockVia();
  if ((tech_via == nullptr) == (design_via == nullptr)) {
    return failure("special-wire via does not resolve to exactly one VIA definition");
  }

  const auto* bottom_layer = tech_via != nullptr ? tech_via->getBottomLayer()
                                                   : design_via->getBottomLayer();
  const auto* top_layer = tech_via != nullptr ? tech_via->getTopLayer()
                                                : design_via->getTopLayer();
  if (bottom_layer == nullptr || top_layer == nullptr) {
    return failure("special-wire via has no top or bottom technology layer");
  }
  const std::string name = tech_via != nullptr ? tech_via->getName()
                                                : design_via->getName();
  if (bottom_layer->isBackside() || top_layer->isBackside()) {
    auto result = value(json::value(nullptr));
    result.ignored.emplace_back("special-wire via " + name
                                + " touches a backside layer");
    return result;
  }

  auto result = value(json::array{"via",
                                  tech_via != nullptr ? "tech" : "design",
                                  name,
                                  box->getViaXY().getX(),
                                  box->getViaXY().getY()});
  if (box->hasViaLayerMasks()) {
    result.ignored.emplace_back("special-wire via " + name
                                + " has layer masks ignored by DRT");
  }
  return result;
}

[[nodiscard]] std::optional<std::size_t> portOrdinal(odb::dbMPin* pin)
{
  if (pin == nullptr || pin->getMTerm() == nullptr) {
    return std::nullopt;
  }
  std::size_t ordinal = 0;
  for (auto* candidate : pin->getMTerm()->getMPins()) {
    if (candidate == pin) {
      return ordinal;
    }
    ++ordinal;
  }
  return std::nullopt;
}

}  // namespace

Projection blockages(odb::dbBlock* block)
{
  if (block == nullptr) {
    return failure("null block while projecting obstructions");
  }

  Projection result;
  json::array values;
  for (auto* obstruction : block->getObstructions()) {
    if (obstruction == nullptr || obstruction->getBBox() == nullptr) {
      result.unrecordable.emplace_back("obstruction without a bounding box");
      continue;
    }
    auto* box = obstruction->getBBox();
    auto* layer = box->getTechLayer();
    if (layer == nullptr) {
      result.unrecordable.emplace_back("obstruction without a technology layer");
      continue;
    }
    if (layer->isBackside()) {
      result.ignored.emplace_back("obstruction on backside layer " + layer->getName());
      continue;
    }
    values.emplace_back(
        json::array{layer->getName(), box->xMin(), box->yMin(), box->xMax(), box->yMax()});
  }
  if (!result.unrecordable.empty()) {
    return result;
  }
  canonicalize(values);
  result.expected.emplace(std::move(values));
  return result;
}

Projection bterms(odb::dbBlock* block)
{
  if (block == nullptr) {
    return failure("null block while projecting BTerms");
  }

  Projection result;
  json::array terms;
  for (auto* term : block->getBTerms()) {
    if (term == nullptr) {
      result.unrecordable.emplace_back("null BTerm");
      continue;
    }
    json::array pins;
    std::size_t pin_index = 0;
    for (auto* pin : term->getBPins()) {
      if (pin == nullptr) {
        result.unrecordable.emplace_back("BTerm " + objectName(term)
                                         + " has a null BPin");
        ++pin_index;
        continue;
      }
      json::array boxes;
      for (auto* box : pin->getBoxes()) {
        if (box == nullptr) {
          result.unrecordable.emplace_back("BTerm " + objectName(term)
                                           + " has a null BPin box");
          continue;
        }
        auto* layer = box->getTechLayer();
        if (layer == nullptr) {
          result.unrecordable.emplace_back("BTerm " + objectName(term)
                                           + " has a box without a technology layer");
          continue;
        }
        if (layer->isBackside()) {
          result.ignored.emplace_back("BTerm " + objectName(term)
                                      + " BPin " + std::to_string(pin_index)
                                      + " box on backside layer " + layer->getName());
          continue;
        }
        boxes.emplace_back(
            json::array{layer->getName(), box->xMin(), box->yMin(), box->xMax(), box->yMax()});
      }
      canonicalize(boxes);
      pins.emplace_back(std::move(boxes));
      ++pin_index;
    }
    canonicalize(pins);
    terms.emplace_back(json::array{term->getName(),
                                   term->getSigType().getString(),
                                   term->getIoType().getString(),
                                   term->getNet() == nullptr ? json::value(nullptr)
                                                             : json::value(term->getNet()->getName()),
                                   std::move(pins)});
  }
  if (!result.unrecordable.empty()) {
    return result;
  }
  canonicalize(terms);
  result.expected.emplace(std::move(terms));
  return result;
}

Projection specialWire(odb::dbNet* net)
{
  if (net == nullptr) {
    return failure("null net while projecting special wire");
  }
  if (!net->isSpecial()) {
    return failure("special-wire projection requested for ordinary net "
                   + objectName(net));
  }

  Projection result;
  json::array wires;
  for (auto* wire : net->getSWires()) {
    if (wire == nullptr) {
      result.unrecordable.emplace_back("special net " + objectName(net)
                                       + " has a null special wire");
      continue;
    }
    json::array shapes;
    for (auto* box : wire->getWires()) {
      const Projection shape = box != nullptr && box->isVia() ? specialVia(box)
                                                                : specialPath(box);
      appendReasons(result, shape, "special net " + objectName(net) + ": ");
      if (!shape.complete()) {
        continue;
      }
      if (!shape.expected->is_null()) {
        shapes.emplace_back(*shape.expected);
      }
    }
    wires.emplace_back(json::array{wire->getWireType().getString(), std::move(shapes)});
  }
  if (!result.unrecordable.empty()) {
    return result;
  }
  result.expected.emplace(std::move(wires));
  return result;
}

Projection guides(odb::dbNet* net)
{
  if (net == nullptr) {
    return failure("null net while projecting guides");
  }

  Projection result;
  json::array values;
  for (auto* guide : net->getGuides()) {
    if (guide == nullptr) {
      result.unrecordable.emplace_back("net " + objectName(net) + " has a null guide");
      continue;
    }
    auto* layer = guide->getLayer();
    if (layer == nullptr) {
      result.unrecordable.emplace_back("guide on net " + objectName(net)
                                       + " has no routing layer");
      continue;
    }
    if (layer->isBackside()) {
      result.unrecordable.emplace_back("guide on net " + objectName(net)
                                       + " uses a backside layer unsupported by DRT");
      continue;
    }
    auto* via_layer = guide->getViaLayer();
    if (via_layer != nullptr && via_layer->isBackside()) {
      result.unrecordable.emplace_back("guide on net " + objectName(net)
                                       + " has a backside via layer unsupported by DRT");
      continue;
    }
    const odb::Rect box = guide->getBox();
    values.emplace_back(json::array{layer->getName(),
                                    via_layer == nullptr ? json::value(nullptr)
                                                         : json::value(via_layer->getName()),
                                    box.xMin(),
                                    box.yMin(),
                                    box.xMax(),
                                    box.yMax(),
                                    guide->isCongested(),
                                    guide->isJumper(),
                                    guide->isConnectedToTerm()});
  }
  if (!result.unrecordable.empty()) {
    return result;
  }
  result.expected.emplace(std::move(values));
  return result;
}

Projection pinAccess(odb::dbAccessPoint* access_point)
{
  if (access_point == nullptr) {
    return failure("null access point");
  }
  auto* layer = access_point->getLayer();
  if (layer == nullptr) {
    return failure("access point has no technology layer");
  }
  if (layer->isBackside()) {
    auto result = value(json::value(nullptr));
    result.ignored.emplace_back("access point on backside layer " + layer->getName());
    return result;
  }

  Projection result;
  json::array directions;
  constexpr std::pair<odb::dbDirection::Value, const char*> direction_names[] = {
      {odb::dbDirection::NORTH, "N"}, {odb::dbDirection::EAST, "E"},
      {odb::dbDirection::SOUTH, "S"}, {odb::dbDirection::WEST, "W"},
      {odb::dbDirection::UP, "U"},    {odb::dbDirection::DOWN, "D"}};
  for (const auto& [direction, name] : direction_names) {
    if (access_point->hasAccess(direction)) {
      directions.emplace_back(name);
    }
  }

  json::array via_groups;
  const auto groups = access_point->getVias();
  for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
    json::array vias;
    for (auto* object : groups[group_index]) {
      std::string kind;
      const std::string name = viaName(object, kind);
      if (name.empty()) {
        result.unrecordable.emplace_back("access point has a non-VIA object in cut-count group "
                                         + std::to_string(group_index + 1));
        continue;
      }
      vias.emplace_back(json::array{kind, name});
    }
    via_groups.emplace_back(json::array{group_index + 1, std::move(vias)});
  }

  json::array segments;
  for (const auto& [segment, begin_truncated, end_truncated] : access_point->getSegments()) {
    segments.emplace_back(json::array{segment.xMin(),
                                      segment.yMin(),
                                      segment.xMax(),
                                      segment.yMax(),
                                      begin_truncated,
                                      end_truncated});
  }
  if (!result.unrecordable.empty()) {
    return result;
  }
  const odb::Point point = access_point->getPoint();
  result.expected.emplace(json::array{point.getX(),
                                      point.getY(),
                                      layer->getName(),
                                      std::move(directions),
                                      access_point->getLowType().getString(),
                                      access_point->getHighType().getString(),
                                      std::move(via_groups),
                                      std::move(segments)});
  return result;
}

Projection btermPinAccess(odb::dbBPin* pin)
{
  if (pin == nullptr) {
    return failure("null BPin while projecting access points");
  }
  json::array points;
  Projection result;
  for (auto* access_point : pin->getAccessPoints()) {
    const auto point = pinAccess(access_point);
    appendReasons(result, point, "BPin access point: ");
    if (!point.complete()) {
      continue;
    }
    if (!point.expected->is_null()) {
      points.emplace_back(*point.expected);
    }
  }
  if (!result.unrecordable.empty()) {
    return result;
  }
  result.expected.emplace(std::move(points));
  return result;
}

Projection masterPinAccess(odb::dbMPin* pin)
{
  if (pin == nullptr) {
    return failure("null master pin while projecting pin access");
  }

  Projection result;
  json::array groups;
  const auto pin_access = pin->getPinAccess();
  for (std::size_t group_index = 0; group_index < pin_access.size(); ++group_index) {
    json::array points;
    for (auto* access_point : pin_access[group_index]) {
      const Projection point = pinAccess(access_point);
      appendReasons(result,
                    point,
                    "master-pin access group " + std::to_string(group_index) + ": ");
      if (!point.complete()) {
        continue;
      }
      if (!point.expected->is_null()) {
        points.emplace_back(*point.expected);
      }
    }
    groups.emplace_back(json::array{group_index, std::move(points)});
  }
  if (!result.unrecordable.empty()) {
    return result;
  }
  result.expected.emplace(std::move(groups));
  return result;
}

Projection preferredPinAccess(odb::dbITerm* term)
{
  if (term == nullptr) {
    return failure("null instance terminal while projecting preferred pin access");
  }
  if (term->getMTerm() == nullptr) {
    return failure("instance terminal " + objectName(term) + " has no master terminal");
  }

  Projection result;
  json::array values;
  for (auto* access_point : term->getPrefAccessPoints()) {
    if (access_point == nullptr || access_point->getMPin() == nullptr) {
      result.unrecordable.emplace_back("instance terminal " + objectName(term)
                                       + " has a preferred access point without a master pin");
      continue;
    }
    auto* master_pin = access_point->getMPin();
    if (master_pin->getMTerm() != term->getMTerm()) {
      result.unrecordable.emplace_back("instance terminal " + objectName(term)
                                       + " refers to a preferred access point from another master terminal");
      continue;
    }
    const auto ordinal = portOrdinal(master_pin);
    if (!ordinal) {
      result.unrecordable.emplace_back("cannot locate the master-pin ordinal for "
                                       + objectName(term));
      continue;
    }
    const Projection point = pinAccess(access_point);
    appendReasons(result, point, "preferred access point: ");
    if (!point.complete()) {
      continue;
    }
    if (point.expected->is_null()) {
      result.unrecordable.emplace_back("instance terminal " + objectName(term)
                                       + " prefers an access point that DRT filters");
      continue;
    }
    values.emplace_back(json::array{*ordinal, *point.expected});
  }
  if (!result.unrecordable.empty()) {
    return result;
  }
  result.expected.emplace(std::move(values));
  return result;
}

Projection preferredPinAccess(odb::dbInst* instance)
{
  if (instance == nullptr) {
    return failure("null instance while projecting preferred pin access");
  }

  Projection result;
  json::array terms;
  for (auto* term : instance->getITerms()) {
    const Projection values = preferredPinAccess(term);
    appendReasons(result, values, "instance " + instance->getName() + ": ");
    if (!values.complete()) {
      continue;
    }
    terms.emplace_back(json::array{term->getMTerm()->getName(), *values.expected});
  }
  if (!result.unrecordable.empty()) {
    return result;
  }
  canonicalize(terms);
  result.expected.emplace(std::move(terms));
  return result;
}

}  // namespace eccdb::tritonroute::odb_trace_projection
