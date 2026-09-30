// SPDX-License-Identifier: Apache-2.0

#include "DrtTraceOdb.h"

#include "../adapters/tritonroute/OdbTraceTechProjection.h"
#include "../adapters/tritonroute/OdbTraceDesignProjection.h"
#include "../adapters/tritonroute/IspdGuideReader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "odb/db.h"
#include "odb/dbWireCodec.h"
#include "odb/defin.h"
#include "odb/lefin.h"
#include "utl/Logger.h"

namespace {
namespace json = boost::json;

[[nodiscard]] uint32_t traceToken(const json::value& value, std::string_view context)
{
  const int64_t token = drt_trace::integer(value);
  if (token < 0 || token > std::numeric_limits<uint32_t>::max()) {
    throw std::runtime_error("invalid trace token in " + std::string(context));
  }
  return static_cast<uint32_t>(token);
}

[[nodiscard]] const char* defOrientation(const odb::dbOrientType& orient)
{
  switch (orient.getValue()) {
    case odb::dbOrientType::R0:
      return "N";
    case odb::dbOrientType::R90:
      return "W";
    case odb::dbOrientType::R180:
      return "S";
    case odb::dbOrientType::R270:
      return "E";
    case odb::dbOrientType::MY:
      return "FN";
    case odb::dbOrientType::MYR90:
      return "FE";
    case odb::dbOrientType::MX:
      return "FS";
    case odb::dbOrientType::MXR90:
      return "FW";
  }
  throw std::runtime_error("unsupported ODB instance orientation");
}

[[nodiscard]] int checkedDelta(int32_t high, int32_t low, std::string_view context)
{
  const int64_t delta = static_cast<int64_t>(high) - static_cast<int64_t>(low);
  if (delta < std::numeric_limits<int>::min() || delta > std::numeric_limits<int>::max()) {
    throw std::runtime_error("trace rectangle delta overflows ODB integer in " + std::string(context));
  }
  return static_cast<int>(delta);
}

void restoreInitialGCellGrid(odb::dbBlock* block, const json::object& header)
{
  const auto* initial = header.if_contains("initial_gcell_grid");
  if (initial == nullptr || initial->is_null()) return;
  if (!initial->is_array() || initial->as_array().size() != 6) {
    throw std::runtime_error("invalid initial GCell grid in trace header");
  }
  const auto& fields = initial->as_array();
  auto* grid = block->getGCellGrid();
  if (grid == nullptr) grid = odb::dbGCellGrid::create(block);
  else grid->resetGrid();
  grid->addGridPatternX(drt_trace::coordinate(fields[0]),
                        drt_trace::coordinate(fields[1]),
                        drt_trace::coordinate(fields[2]));
  grid->addGridPatternY(drt_trace::coordinate(fields[3]),
                        drt_trace::coordinate(fields[4]),
                        drt_trace::coordinate(fields[5]));
}

void importGuides(odb::dbBlock* block, const std::filesystem::path& path)
{
  eccdb::tritonroute::guide_input::readGuides(
      path, [&](const std::string& name, const auto& rectangles) {
        auto* net = block->findNet(name.c_str());
        if (net == nullptr) throw std::runtime_error("unknown guide net " + name);
        for (const auto& rectangle : rectangles) {
          auto* layer = block->getTech()->findLayer(rectangle.layer.c_str());
          if (layer == nullptr) throw std::runtime_error("unknown guide layer " + rectangle.layer);
          if (odb::dbGuide::create(net, layer, layer,
                                   {rectangle.x1, rectangle.y1, rectangle.x2, rectangle.y2}, false) == nullptr) {
            throw std::runtime_error("cannot create ODB replay guide for " + name);
          }
        }
      });
}

[[nodiscard]] json::value projected(eccdb::tritonroute::odb_trace_projection::Projection projection,
                                    std::string_view operation)
{
  if (!projection.complete()) {
    throw std::runtime_error("incomplete ODB projection for " + std::string(operation));
  }
  return std::move(*projection.expected);
}

[[nodiscard]] const json::object& payloadObject(const drt_trace::Event& event)
{
  if (!event.payload.is_object()) {
    throw std::runtime_error("trace payload is not an object for " + event.op);
  }
  return event.payload.as_object();
}

[[nodiscard]] const json::value& payloadField(const json::object& object,
                                               std::string_view name,
                                               std::string_view operation)
{
  const std::string key(name);
  const auto* value = object.if_contains(key.c_str());
  if (value == nullptr) {
    throw std::runtime_error("trace payload has no " + std::string(name) + " for "
                             + std::string(operation));
  }
  return *value;
}

[[nodiscard]] const json::array& payloadArray(const json::object& object,
                                               std::string_view name,
                                               std::string_view operation)
{
  const auto& value = payloadField(object, name, operation);
  if (!value.is_array()) {
    throw std::runtime_error("trace payload field " + std::string(name)
                             + " is not an array for " + std::string(operation));
  }
  return value.as_array();
}

[[nodiscard]] std::string payloadText(const json::value& value, std::string_view context)
{
  if (!value.is_string()) {
    throw std::runtime_error("trace expects text in " + std::string(context));
  }
  return drt_trace::text(value);
}

[[nodiscard]] int payloadCoordinate(const json::value& value, std::string_view context)
{
  const int64_t coordinate = drt_trace::integer(value);
  if (coordinate < std::numeric_limits<int>::min()
      || coordinate > std::numeric_limits<int>::max()) {
    throw std::runtime_error("trace coordinate overflows ODB integer in "
                             + std::string(context));
  }
  return static_cast<int>(coordinate);
}

[[nodiscard]] int payloadGroup(const json::value& value, std::string_view context)
{
  const uint32_t group = traceToken(value, context);
  if (group > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    throw std::runtime_error("trace pin-access group overflows ODB integer in "
                             + std::string(context));
  }
  return static_cast<int>(group);
}

void requireRectangle(int x1, int y1, int x2, int y2, std::string_view context)
{
  if (x1 > x2 || y1 > y2) {
    throw std::runtime_error("trace rectangle is not lower-left/upper-right in "
                             + std::string(context));
  }
}

class OdbBackend
{
 public:
  static constexpr bool retains_extensions = true;

  OdbBackend(odb::dbDatabase* database,
             odb::dbBlock* block,
             const json::object& header)
      : database_(database), block_(block)
  {
    if (database_ == nullptr || block_ == nullptr) {
      throw std::runtime_error("cannot bind ODB trace without a database and block");
    }
    bindInstances(header);
    bindNets(header);
  }

  json::value execute(const drt_trace::Event& event)
  {
    if (event.op == "read_tech_header") return readTechHeader();
    if (event.op == "read_tech_layers") return eccdb::tritonroute::trace_projection::techLayers(block_->getTech());
    if (event.op == "read_routing_layer_rules") return eccdb::tritonroute::trace_projection::routingLayerRules(block_->getTech());
    if (event.op == "read_cut_layer_rules") return eccdb::tritonroute::trace_projection::cutLayerRules(block_->getTech());
    if (event.op == "read_tech_vias") return eccdb::tritonroute::trace_projection::techVias(block_->getTech());
    if (event.op == "read_design_vias") return eccdb::tritonroute::trace_projection::designVias(block_);
    if (event.op == "read_via_generate_rules") return eccdb::tritonroute::trace_projection::viaGenerateRules(block_->getTech());
    if (event.op == "read_ndrs") return eccdb::tritonroute::trace_projection::ndrs(database_);
    if (event.op == "read_library_masters") return eccdb::tritonroute::trace_projection::libraryMasters(database_);
    if (event.op == "read_library_terms") return eccdb::tritonroute::trace_projection::libraryTerms(database_);
    if (event.op == "read_library_mpin_geometry") return eccdb::tritonroute::trace_projection::libraryMPinGeometry(database_);
    if (event.op == "read_library_obstructions") return eccdb::tritonroute::trace_projection::libraryObstructions(database_);
    if (event.op == "read_blockages") return projected(eccdb::tritonroute::odb_trace_projection::blockages(block_), event.op);
    if (event.op == "read_bterms") return projected(eccdb::tritonroute::odb_trace_projection::bterms(block_), event.op);
    if (event.op == "read_bterm_pin_access") return readBTermPinAccess(event);
    if (event.op == "read_master_pin_access") return readMasterPinAccess(event);
    if (event.op == "read_preferred_pin_access") return readPreferredPinAccess(event);
    if (event.op == "read_special_wire") return readSpecialWire(event.name);
    if (event.op == "read_guides") return readGuides(event.name);
    if (event.op == "read_die") return readDie();
    if (event.op == "scan_tracks") return scanTracks();
    if (event.op == "read_instance_pins") return readInstancePins(requireInstance(event.object));
    if (event.op == "read_net_attributes") return readNetAttributes(requireNet(event.object));
    if (event.op == "read_gcell_grid") return readGCellGrid();
    if (event.op == "scan_instances") return scanInstances();
    if (event.op == "scan_nets") return scanNets();
    if (event.op == "read_instance") return readInstance(requireInstance(event.object));
    if (event.op == "net_pins") return netPins(requireNet(event.object));
    if (event.op == "read_wire") return readWire(requireNet(event.object));
    if (event.op == "find_instance") return findInstance(event.name);
    if (event.op == "find_pin") return findPin(requireInstance(event.object), event.name);
    if (event.op == "define_design_via") {
      defineDesignVia(event);
      return viaDefinition(event.name);
    }
    if (event.op == "clear_wire") {
      clearWire(requireNet(event.object));
      return nullptr;
    }
    if (event.op == "replace_wire") {
      replaceWire(requireNet(event.object), event.shapes);
      return nullptr;
    }
    if (event.op == "append_track_assignment") return appendTrackAssignment(event);
    if (event.op == "replace_master_pin_access") return replaceMasterPinAccess(event);
    if (event.op == "replace_instance_pin_access") return replaceInstancePinAccess(event);
    if (event.op == "append_bterm_pin_access") return appendBTermPinAccess(event);
    if (event.op == "replace_guides") return replaceGuides(event);
    if (event.op == "append_bterm_stack_wire") return appendBTermStackWire(event);
    throw std::runtime_error("unsupported ODB trace operation: " + event.op);
  }

  std::vector<drt_trace::Shape> wire(uint32_t token)
  {
    return drt_trace::readOdbWire(requireNet(token));
  }

  json::value viaDefinition(std::string_view name) const
  {
    auto* via = block_->findVia(std::string(name).c_str());
    if (via == nullptr) throw std::runtime_error("missing ODB design VIA after replay: " + std::string(name));
    std::vector<drt_trace::ViaRect> rectangles;
    for (auto* box : via->getBoxes()) {
      auto* layer = box->getTechLayer();
      if (layer == nullptr || box->getLayerMask() != 0u || box->isVia()) {
        throw std::runtime_error("ODB design VIA has unsupported box geometry: " + std::string(name));
      }
      rectangles.push_back({layer->getName(), box->xMin(), box->yMin(), box->xMax(), box->yMax()});
    }
    return drt_trace::viaValue(name, rectangles);
  }

 private:
  [[nodiscard]] json::value readBTermPinAccess(const drt_trace::Event& event) const
  {
    auto* term = block_->findBTerm(event.name.c_str());
    if (term == nullptr) {
      throw std::runtime_error("missing trace BTerm: " + event.name);
    }
    // Older trace files predate the explicit BPin ordinal and only exercised
    // one-port terminals. Ordinal zero preserves that historical behavior.
    const uint32_t ordinal = event.ordinal.value_or(0);
    auto* pin = btermPinAt(term, ordinal, "read_bterm_pin_access");
    return projected(eccdb::tritonroute::odb_trace_projection::btermPinAccess(pin),
                     "read_bterm_pin_access");
  }

  [[nodiscard]] json::value readMasterPinAccess(const drt_trace::Event& event) const
  {
    if (!event.ordinal) throw std::runtime_error("master pin access event has no ordinal");
    auto* master = database_->findMaster(event.master.c_str());
    auto* term = master == nullptr ? nullptr : master->findMTerm(event.term.c_str());
    if (term == nullptr) throw std::runtime_error("missing trace master/term " + event.master + "/" + event.term);
    std::size_t index = 0;
    for (auto* pin : term->getMPins()) {
      if (index++ == *event.ordinal) {
        return projected(eccdb::tritonroute::odb_trace_projection::masterPinAccess(pin),
                         "read_master_pin_access");
      }
    }
    throw std::runtime_error("missing trace master-pin ordinal for " + event.master + "/" + event.term);
  }

  [[nodiscard]] json::value readPreferredPinAccess(const drt_trace::Event& event) const
  {
    auto* instance = block_->findInst(event.instance.c_str());
    auto* term = instance == nullptr ? nullptr : instance->findITerm(event.term.c_str());
    if (term == nullptr) throw std::runtime_error("missing trace instance pin " + event.instance + "/" + event.term);
    return projected(eccdb::tritonroute::odb_trace_projection::preferredPinAccess(term),
                     "read_preferred_pin_access");
  }

  [[nodiscard]] json::value readSpecialWire(std::string_view name) const
  {
    auto* net = block_->findNet(std::string(name).c_str());
    if (net == nullptr) throw std::runtime_error("missing trace special net " + std::string(name));
    return projected(eccdb::tritonroute::odb_trace_projection::specialWire(net),
                     "read_special_wire");
  }

  [[nodiscard]] json::value readGuides(std::string_view name) const
  {
    auto* net = block_->findNet(std::string(name).c_str());
    if (net == nullptr) throw std::runtime_error("missing trace guide net " + std::string(name));
    return projected(eccdb::tritonroute::odb_trace_projection::guides(net),
                     "read_guides");
  }

  [[nodiscard]] odb::dbMaster* requireMaster(std::string_view name) const
  {
    auto* master = database_->findMaster(std::string(name).c_str());
    if (master == nullptr) {
      throw std::runtime_error("unknown ODB master in trace: " + std::string(name));
    }
    return master;
  }

  [[nodiscard]] odb::dbNet* requireRegularNamedNet(std::string_view name,
                                                    std::string_view operation) const
  {
    auto* net = block_->findNet(std::string(name).c_str());
    if (net == nullptr) {
      throw std::runtime_error("unknown ODB net in " + std::string(operation) + ": "
                               + std::string(name));
    }
    if (net->isSpecial()) {
      throw std::runtime_error("trace context mutation cannot edit special net "
                               + std::string(name));
    }
    return net;
  }

  [[nodiscard]] odb::dbMPin* masterPinAt(odb::dbMTerm* term,
                                          uint32_t ordinal,
                                          std::string_view operation) const
  {
    if (term == nullptr) {
      throw std::runtime_error("null master terminal in " + std::string(operation));
    }
    uint32_t index = 0;
    for (auto* pin : term->getMPins()) {
      if (pin == nullptr) {
        throw std::runtime_error("null ODB master pin in " + std::string(operation));
      }
      if (index == ordinal) return pin;
      ++index;
    }
    throw std::runtime_error("missing ODB master-pin ordinal " + std::to_string(ordinal)
                             + " for " + term->getName() + " in " + std::string(operation));
  }

  [[nodiscard]] odb::dbBPin* btermPinAt(odb::dbBTerm* term,
                                         uint32_t ordinal,
                                         std::string_view operation) const
  {
    if (term == nullptr) {
      throw std::runtime_error("null block terminal in " + std::string(operation));
    }
    uint32_t index = 0;
    for (auto* pin : term->getBPins()) {
      if (pin == nullptr) {
        throw std::runtime_error("null ODB block-terminal pin in " + std::string(operation));
      }
      if (index == ordinal) return pin;
      ++index;
    }
    throw std::runtime_error("missing ODB block-terminal pin ordinal "
                             + std::to_string(ordinal) + " for " + term->getName()
                             + " in " + std::string(operation));
  }

  [[nodiscard]] static odb::dbAccessType accessType(std::string_view name,
                                                     std::string_view operation)
  {
    if (name == "OnGrid") return odb::dbAccessType(odb::dbAccessType::OnGrid);
    if (name == "HalfGrid") return odb::dbAccessType(odb::dbAccessType::HalfGrid);
    if (name == "Center") return odb::dbAccessType(odb::dbAccessType::Center);
    if (name == "EncOpt") return odb::dbAccessType(odb::dbAccessType::EncOpt);
    if (name == "NearbyGrid") return odb::dbAccessType(odb::dbAccessType::NearbyGrid);
    throw std::runtime_error("unknown access-point type " + std::string(name) + " in "
                             + std::string(operation));
  }

  [[nodiscard]] json::value accessPointValue(odb::dbAccessPoint* access_point,
                                              std::string_view operation) const
  {
    json::value value = projected(
        eccdb::tritonroute::odb_trace_projection::pinAccess(access_point), operation);
    if (value.is_null()) {
      throw std::runtime_error("trace context mutation produced a filtered access point in "
                               + std::string(operation));
    }
    return value;
  }

  void populateAccessPoint(odb::dbAccessPoint* access_point,
                           const json::value& encoded,
                           std::string_view operation) const
  {
    if (access_point == nullptr || !encoded.is_array()) {
      throw std::runtime_error("invalid access-point payload in " + std::string(operation));
    }
    const auto& fields = encoded.as_array();
    if (fields.size() != 8) {
      throw std::runtime_error("access-point payload has an invalid field count in "
                               + std::string(operation));
    }

    access_point->setPoint(
        {payloadCoordinate(fields[0], operation), payloadCoordinate(fields[1], operation)});
    access_point->setLayer(requireRoutingLayer(payloadText(fields[2], operation)));

    if (!fields[3].is_array()) {
      throw std::runtime_error("access-point directions are not an array in "
                               + std::string(operation));
    }
    bool seen_directions[6] = {};
    for (const auto& value : fields[3].as_array()) {
      const std::string direction = payloadText(value, operation);
      odb::dbDirection::Value value_type = odb::dbDirection::NONE;
      int index = -1;
      if (direction == "N") {
        value_type = odb::dbDirection::NORTH;
        index = 0;
      } else if (direction == "E") {
        value_type = odb::dbDirection::EAST;
        index = 1;
      } else if (direction == "S") {
        value_type = odb::dbDirection::SOUTH;
        index = 2;
      } else if (direction == "W") {
        value_type = odb::dbDirection::WEST;
        index = 3;
      } else if (direction == "U") {
        value_type = odb::dbDirection::UP;
        index = 4;
      } else if (direction == "D") {
        value_type = odb::dbDirection::DOWN;
        index = 5;
      } else {
        throw std::runtime_error("unknown access-point direction " + direction + " in "
                                 + std::string(operation));
      }
      if (seen_directions[index]) {
        throw std::runtime_error("duplicate access-point direction in "
                                 + std::string(operation));
      }
      seen_directions[index] = true;
      access_point->setAccess(true, odb::dbDirection(value_type));
    }
    access_point->setLowType(accessType(payloadText(fields[4], operation), operation));
    access_point->setHighType(accessType(payloadText(fields[5], operation), operation));

    if (!fields[6].is_array()) {
      throw std::runtime_error("access-point via groups are not an array in "
                               + std::string(operation));
    }
    const auto& via_groups = fields[6].as_array();
    for (std::size_t group_index = 0; group_index < via_groups.size(); ++group_index) {
      const auto& group = via_groups[group_index];
      if (!group.is_array() || group.as_array().size() != 2) {
        throw std::runtime_error("invalid access-point via group in " + std::string(operation));
      }
      const auto& group_fields = group.as_array();
      const int cut_count = payloadGroup(group_fields[0], operation);
      if (cut_count != static_cast<int>(group_index + 1)) {
        throw std::runtime_error("non-contiguous access-point via groups in "
                                 + std::string(operation));
      }
      if (!group_fields[1].is_array()) {
        throw std::runtime_error("access-point via list is not an array in "
                                 + std::string(operation));
      }
      for (const auto& via_value : group_fields[1].as_array()) {
        if (!via_value.is_array() || via_value.as_array().size() != 2) {
          throw std::runtime_error("invalid access-point via in " + std::string(operation));
        }
        const auto& via_fields = via_value.as_array();
        const std::string kind = payloadText(via_fields[0], operation);
        const std::string name = payloadText(via_fields[1], operation);
        if (kind == "tech") {
          auto* via = block_->getTech()->findVia(name.c_str());
          if (via == nullptr) {
            throw std::runtime_error("unknown technology VIA in " + std::string(operation)
                                     + ": " + name);
          }
          access_point->addTechVia(cut_count, via);
        } else if (kind == "design") {
          auto* via = block_->findVia(name.c_str());
          if (via == nullptr) {
            throw std::runtime_error("unknown design VIA in " + std::string(operation)
                                     + ": " + name);
          }
          access_point->addBlockVia(cut_count, via);
        } else {
          throw std::runtime_error("unknown access-point VIA kind " + kind + " in "
                                   + std::string(operation));
        }
      }
    }

    if (!fields[7].is_array()) {
      throw std::runtime_error("access-point segments are not an array in "
                               + std::string(operation));
    }
    for (const auto& segment : fields[7].as_array()) {
      if (!segment.is_array() || segment.as_array().size() != 6) {
        throw std::runtime_error("invalid access-point segment in " + std::string(operation));
      }
      const auto& segment_fields = segment.as_array();
      const int x1 = payloadCoordinate(segment_fields[0], operation);
      const int y1 = payloadCoordinate(segment_fields[1], operation);
      const int x2 = payloadCoordinate(segment_fields[2], operation);
      const int y2 = payloadCoordinate(segment_fields[3], operation);
      requireRectangle(x1, y1, x2, y2, operation);
      if (!segment_fields[4].is_bool() || !segment_fields[5].is_bool()) {
        throw std::runtime_error("access-point segment truncation flags are invalid in "
                                 + std::string(operation));
      }
      access_point->addSegment({x1, y1, x2, y2},
                               segment_fields[4].as_bool(),
                               segment_fields[5].as_bool());
    }
  }

  [[nodiscard]] json::array trackValues(odb::dbNet* net) const
  {
    json::array tracks;
    for (auto* track : net->getTracks()) {
      auto* layer = track == nullptr ? nullptr : track->getLayer();
      if (layer == nullptr) {
        throw std::runtime_error("ODB net track has no routing layer");
      }
      const odb::Rect box = track->getBox();
      tracks.emplace_back(
          json::array{layer->getName(), box.xMin(), box.yMin(), box.xMax(), box.yMax()});
    }
    return tracks;
  }

  [[nodiscard]] json::array masterGroupValues(odb::dbMaster* master,
                                               const std::vector<int>& groups,
                                               std::string_view operation) const
  {
    json::array values;
    for (auto* term : master->getMTerms()) {
      if (term == nullptr) {
        throw std::runtime_error("null ODB master terminal in " + std::string(operation));
      }
      uint32_t ordinal = 0;
      for (auto* pin : term->getMPins()) {
        if (pin == nullptr) {
          throw std::runtime_error("null ODB master pin in " + std::string(operation));
        }
        const auto pin_access = pin->getPinAccess();
        for (const int group : groups) {
          json::array points;
          const auto group_index = static_cast<std::size_t>(group);
          if (group_index < pin_access.size()) {
            for (auto* access_point : pin_access[group_index]) {
              points.emplace_back(accessPointValue(access_point, operation));
            }
          }
          values.emplace_back(json::array{term->getName(), ordinal, group, std::move(points)});
        }
        ++ordinal;
      }
    }
    return values;
  }

  [[nodiscard]] json::array instancePinAccessValues(odb::dbInst* instance,
                                                     std::string_view operation) const
  {
    json::array values;
    for (auto* term : instance->getITerms()) {
      auto* master_term = term == nullptr ? nullptr : term->getMTerm();
      if (master_term == nullptr) {
        throw std::runtime_error("incomplete ODB instance terminal in "
                                 + std::string(operation));
      }
      const auto preferred = term->getPrefAccessPoints();
      json::array ports;
      uint32_t ordinal = 0;
      for (auto* pin : master_term->getMPins()) {
        if (pin == nullptr) {
          throw std::runtime_error("null ODB master pin in " + std::string(operation));
        }
        odb::dbAccessPoint* selected = nullptr;
        for (auto* access_point : preferred) {
          if (access_point == nullptr) {
            throw std::runtime_error("null preferred access point in " + std::string(operation));
          }
          if (access_point->getMPin() == pin) {
            if (selected != nullptr) {
              throw std::runtime_error("duplicate preferred access point in "
                                       + std::string(operation));
            }
            selected = access_point;
          }
        }
        ports.emplace_back(json::array{ordinal,
                                       selected == nullptr
                                           ? json::value(nullptr)
                                           : accessPointValue(selected, operation)});
        ++ordinal;
      }
      values.emplace_back(json::array{master_term->getName(), std::move(ports)});
    }
    return values;
  }

  [[nodiscard]] json::array btermAccessPointValues(odb::dbBPin* pin,
                                                    std::string_view operation) const
  {
    json::array values;
    for (auto* access_point : pin->getAccessPoints()) {
      values.emplace_back(accessPointValue(access_point, operation));
    }
    return values;
  }

  [[nodiscard]] json::value appendTrackAssignment(const drt_trace::Event& event)
  {
    auto* net = requireRegularNamedNet(event.name, event.op);
    const auto& appended = payloadArray(payloadObject(event), "append", event.op);
    for (const auto& value : appended) {
      if (!value.is_array() || value.as_array().size() != 5) {
        throw std::runtime_error("invalid net-track payload in " + event.op);
      }
      const auto& fields = value.as_array();
      const int x1 = payloadCoordinate(fields[1], event.op);
      const int y1 = payloadCoordinate(fields[2], event.op);
      const int x2 = payloadCoordinate(fields[3], event.op);
      const int y2 = payloadCoordinate(fields[4], event.op);
      requireRectangle(x1, y1, x2, y2, event.op);
      if (odb::dbNetTrack::create(net,
                                   requireRoutingLayer(payloadText(fields[0], event.op)),
                                   {x1, y1, x2, y2})
          == nullptr) {
        throw std::runtime_error("cannot create ODB net track in " + event.op);
      }
    }
    return trackValues(net);
  }

  [[nodiscard]] json::value replaceMasterPinAccess(const drt_trace::Event& event)
  {
    auto* master = requireMaster(event.master);
    const auto& payload = payloadObject(event);
    const auto& clear_groups = payloadArray(payload, "clear_groups", event.op);
    std::vector<int> groups;
    groups.reserve(clear_groups.size());
    for (const auto& value : clear_groups) {
      groups.push_back(payloadGroup(value, event.op));
    }
    if (groups.empty()) {
      throw std::runtime_error("trace has no master pin-access groups in " + event.op);
    }
    for (const int group : groups) {
      master->clearPinAccess(group);
    }

    const auto& created = payloadArray(payload, "create", event.op);
    for (const auto& value : created) {
      if (!value.is_array() || value.as_array().size() != 4) {
        throw std::runtime_error("invalid master access-point payload in " + event.op);
      }
      const auto& fields = value.as_array();
      const std::string term_name = payloadText(fields[0], event.op);
      const uint32_t ordinal = traceToken(fields[1], event.op);
      const int group = payloadGroup(fields[2], event.op);
      if (std::find(groups.begin(), groups.end(), group) == groups.end()) {
        throw std::runtime_error("master access-point payload references an uncleared group in "
                                 + event.op);
      }
      auto* term = master->findMTerm(term_name.c_str());
      if (term == nullptr) {
        throw std::runtime_error("unknown ODB master terminal in " + event.op + ": "
                                 + term_name);
      }
      auto* pin = masterPinAt(term, ordinal, event.op);
      if (!fields[3].is_array()) {
        throw std::runtime_error("master access-point list is not an array in " + event.op);
      }
      for (const auto& access_point : fields[3].as_array()) {
        auto* created_point = odb::dbAccessPoint::create(
            block_, pin, static_cast<uint32_t>(group));
        if (created_point == nullptr) {
          throw std::runtime_error("cannot create ODB master access point in " + event.op);
        }
        populateAccessPoint(created_point, access_point, event.op);
      }
    }
    return masterGroupValues(master, groups, event.op);
  }

  [[nodiscard]] json::value replaceInstancePinAccess(const drt_trace::Event& event)
  {
    auto* instance = block_->findInst(event.instance.c_str());
    if (instance == nullptr) {
      throw std::runtime_error("unknown ODB instance in " + event.op + ": " + event.instance);
    }
    const auto& payload = payloadObject(event);
    instance->setPinAccessIdx(traceToken(payloadField(payload, "pin_access_index", event.op),
                                         event.op));

    std::vector<odb::dbITerm*> instance_terms;
    for (auto* term : instance->getITerms()) {
      if (term == nullptr || term->getMTerm() == nullptr) {
        throw std::runtime_error("incomplete ODB instance terminal in " + event.op);
      }
      term->clearPrefAccessPoints();
      instance_terms.push_back(term);
    }

    const auto& terms = payloadArray(payload, "terms", event.op);
    if (terms.size() != instance_terms.size()) {
      throw std::runtime_error("instance pin-access payload has a mismatched terminal count in "
                               + event.op);
    }
    std::vector<odb::dbITerm*> updated;
    updated.reserve(terms.size());
    for (const auto& value : terms) {
      if (!value.is_array() || value.as_array().size() != 2) {
        throw std::runtime_error("invalid instance pin-access payload in " + event.op);
      }
      const auto& fields = value.as_array();
      const std::string term_name = payloadText(fields[0], event.op);
      auto* term = instance->findITerm(term_name.c_str());
      if (term == nullptr || term->getMTerm() == nullptr) {
        throw std::runtime_error("unknown ODB instance terminal in " + event.op + ": "
                                 + term_name);
      }
      if (std::find(updated.begin(), updated.end(), term) != updated.end()) {
        throw std::runtime_error("duplicate instance terminal in " + event.op + ": "
                                 + term_name);
      }
      updated.push_back(term);
      if (!fields[1].is_array()) {
        throw std::runtime_error("instance port locator list is not an array in " + event.op);
      }
      const auto& locators = fields[1].as_array();
      std::size_t pin_count = 0;
      for (auto* ignored : term->getMTerm()->getMPins()) {
        if (ignored == nullptr) {
          throw std::runtime_error("null ODB master pin in " + event.op);
        }
        ++pin_count;
      }
      if (locators.size() != pin_count) {
        throw std::runtime_error("instance port locator count disagrees with ODB in " + event.op);
      }
      for (std::size_t index = 0; index < locators.size(); ++index) {
        const auto& locator = locators[index];
        if (!locator.is_array() || locator.as_array().size() != 2) {
          throw std::runtime_error("invalid instance port locator in " + event.op);
        }
        const auto& locator_fields = locator.as_array();
        const uint32_t ordinal = traceToken(locator_fields[0], event.op);
        if (ordinal != index) {
          throw std::runtime_error("instance port locator ordinal is out of order in " + event.op);
        }
        auto* pin = masterPinAt(term->getMTerm(), ordinal, event.op);
        if (locator_fields[1].is_null()) {
          term->setAccessPoint(pin, nullptr);
          continue;
        }
        if (!locator_fields[1].is_array() || locator_fields[1].as_array().size() != 2) {
          throw std::runtime_error("invalid instance access-point reference in " + event.op);
        }
        const auto& reference = locator_fields[1].as_array();
        const uint32_t group = traceToken(reference[0], event.op);
        const uint32_t point_index = traceToken(reference[1], event.op);
        const auto pin_access = pin->getPinAccess();
        if (group >= pin_access.size() || point_index >= pin_access[group].size()
            || pin_access[group][point_index] == nullptr) {
          throw std::runtime_error("unknown instance access-point reference in " + event.op);
        }
        term->setAccessPoint(pin, pin_access[group][point_index]);
      }
    }
    return instancePinAccessValues(instance, event.op);
  }

  [[nodiscard]] json::value appendBTermPinAccess(const drt_trace::Event& event)
  {
    auto* term = block_->findBTerm(event.name.c_str());
    if (term == nullptr) {
      throw std::runtime_error("unknown ODB block terminal in " + event.op + ": " + event.name);
    }
    const auto& payload = payloadObject(event);
    const uint32_t ordinal = traceToken(payloadField(payload, "bpin_ordinal", event.op),
                                        event.op);
    auto* pin = btermPinAt(term, ordinal, event.op);
    const auto& appended = payloadArray(payload, "append", event.op);
    for (const auto& value : appended) {
      auto* access_point = odb::dbAccessPoint::create(pin);
      if (access_point == nullptr) {
        throw std::runtime_error("cannot create ODB block-terminal access point in " + event.op);
      }
      populateAccessPoint(access_point, value, event.op);
    }
    return btermAccessPointValues(pin, event.op);
  }

  [[nodiscard]] json::value replaceGuides(const drt_trace::Event& event)
  {
    auto* net = requireRegularNamedNet(event.name, event.op);
    const auto& guides = payloadArray(payloadObject(event), "guides", event.op);
    net->clearGuides();
    for (const auto& value : guides) {
      if (!value.is_array() || value.as_array().size() != 5) {
        throw std::runtime_error("invalid guide payload in " + event.op);
      }
      const auto& fields = value.as_array();
      const int x1 = payloadCoordinate(fields[1], event.op);
      const int y1 = payloadCoordinate(fields[2], event.op);
      const int x2 = payloadCoordinate(fields[3], event.op);
      const int y2 = payloadCoordinate(fields[4], event.op);
      requireRectangle(x1, y1, x2, y2, event.op);
      auto* layer = requireRoutingLayer(payloadText(fields[0], event.op));
      if (odb::dbGuide::create(net, layer, layer, {x1, y1, x2, y2}, false) == nullptr) {
        throw std::runtime_error("cannot create ODB guide in " + event.op);
      }
    }
    auto odb_guides = net->getGuides();
    if (odb_guides.orderReversed() && odb_guides.reversible()) {
      odb_guides.reverse();
    }
    return readGuides(event.name);
  }

  [[nodiscard]] json::value appendBTermStackWire(const drt_trace::Event& event)
  {
    auto* net = requireNet(event.object);
    if (net->isSpecial()) {
      throw std::runtime_error("trace cannot append a block-terminal stack to special net "
                               + std::string(net->getName()));
    }
    const auto& payload = payloadObject(event);
    auto* layer = requireRoutingLayer(payloadText(payloadField(payload, "layer", event.op), event.op));
    const int x = payloadCoordinate(payloadField(payload, "x", event.op), event.op);
    const int y = payloadCoordinate(payloadField(payload, "y", event.op), event.op);
    const auto& vias = payloadArray(payload, "vias", event.op);

    auto* wire = net->getWire();
    odb::dbWireEncoder encoder;
    if (wire == nullptr) {
      wire = odb::dbWire::create(net);
      if (wire == nullptr) {
        throw std::runtime_error("cannot create ODB stack wire in " + event.op);
      }
      encoder.begin(wire);
    } else {
      encoder.append(wire);
    }
    encoder.newPath(layer, odb::dbWireType::ROUTED);
    encoder.addPoint(x, y);
    auto* current_layer = layer;
    for (const auto& value : vias) {
      const std::string name = payloadText(value, event.op);
      auto* via = block_->getTech()->findVia(name.c_str());
      if (via == nullptr) {
        throw std::runtime_error("unknown technology VIA in " + event.op + ": " + name);
      }
      auto* bottom = via->getBottomLayer();
      auto* top = via->getTopLayer();
      if (bottom == nullptr || top == nullptr
          || (bottom != current_layer && top != current_layer)) {
        throw std::runtime_error("stack VIA does not connect the current routing layer in "
                                 + event.op + ": " + name);
      }
      encoder.addTechVia(via);
      current_layer = bottom == current_layer ? top : bottom;
    }
    encoder.end();
    return drt_trace::shapeValues(drt_trace::readOdbWire(net), false);
  }

  void bindInstances(const json::object& header)
  {
    const auto* bindings = header.if_contains("instances");
    if (bindings == nullptr || !bindings->is_array()) {
      throw std::runtime_error("trace header has no instances binding array");
    }
    for (const auto& binding : bindings->as_array()) {
      if (!binding.is_array() || binding.as_array().size() != 2) {
        throw std::runtime_error("invalid trace instance binding");
      }
      const auto& pair = binding.as_array();
      const uint32_t token = traceToken(pair[0], "instances");
      const std::string name = drt_trace::text(pair[1]);
      auto* instance = block_->findInst(name.c_str());
      if (instance == nullptr) throw std::runtime_error("trace instance is absent after ODB import: " + name);
      if (token >= instances_.size()) instances_.resize(static_cast<std::size_t>(token) + 1, nullptr);
      if (instances_[token] != nullptr) {
        throw std::runtime_error("duplicate trace instance token: " + std::to_string(token));
      }
      instances_[token] = instance;
    }
  }

  void bindNets(const json::object& header)
  {
    const auto* bindings = header.if_contains("nets");
    if (bindings == nullptr || !bindings->is_array()) {
      throw std::runtime_error("trace header has no nets binding array");
    }
    for (const auto& binding : bindings->as_array()) {
      if (!binding.is_array() || binding.as_array().size() != 2) {
        throw std::runtime_error("invalid trace net binding");
      }
      const auto& pair = binding.as_array();
      const uint32_t token = traceToken(pair[0], "nets");
      const std::string name = drt_trace::text(pair[1]);
      auto* net = block_->findNet(name.c_str());
      if (net == nullptr) throw std::runtime_error("trace net is absent after ODB import: " + name);
      if (token >= nets_.size()) nets_.resize(static_cast<std::size_t>(token) + 1, nullptr);
      if (nets_[token] != nullptr) {
        throw std::runtime_error("duplicate trace net token: " + std::to_string(token));
      }
      nets_[token] = net;
    }
  }

  [[nodiscard]] odb::dbInst* requireInstance(uint32_t token) const
  {
    if (token >= instances_.size() || instances_[token] == nullptr) {
      throw std::runtime_error("unbound trace instance token: " + std::to_string(token));
    }
    return instances_[token];
  }

  [[nodiscard]] odb::dbNet* requireNet(uint32_t token) const
  {
    if (token >= nets_.size() || nets_[token] == nullptr) {
      throw std::runtime_error("unbound trace net token: " + std::to_string(token));
    }
    return nets_[token];
  }

  [[nodiscard]] odb::dbTechLayer* requireRoutingLayer(std::string_view name) const
  {
    auto* layer = block_->getTech()->findLayer(std::string(name).c_str());
    if (layer == nullptr) throw std::runtime_error("unknown ODB layer in trace: " + std::string(name));
    if (layer->getType() != odb::dbTechLayerType::ROUTING) {
      throw std::runtime_error("trace primitive uses non-routing layer: " + std::string(name));
    }
    return layer;
  }

  void defineDesignVia(const drt_trace::Event& event) const
  {
    if (block_->findVia(event.name.c_str()) != nullptr) {
      throw std::runtime_error("trace design VIA already exists in ODB: " + event.name);
    }
    auto* via = odb::dbVia::create(block_, event.name.c_str());
    if (via == nullptr) throw std::runtime_error("cannot create ODB design VIA: " + event.name);
    via->setDefault(event.via_default);
    for (const auto& rectangle : event.via_rectangles) {
      auto* layer = block_->getTech()->findLayer(rectangle.layer.c_str());
      if (layer == nullptr || (layer->getType() != odb::dbTechLayerType::ROUTING
                               && layer->getType() != odb::dbTechLayerType::CUT)) {
        throw std::runtime_error("unknown ODB VIA layer in trace: " + rectangle.layer);
      }
      if (odb::dbBox::create(via, layer, rectangle.x1, rectangle.y1, rectangle.x2, rectangle.y2) == nullptr) {
        throw std::runtime_error("cannot create ODB VIA rectangle: " + event.name);
      }
    }
    if (via->isDefault() != event.via_default) {
      throw std::runtime_error("ODB design VIA default flag mismatch: " + event.name);
    }
  }

  static void requireEditable(odb::dbNet* net)
  {
    if (net->isSpecial()) throw std::runtime_error("trace cannot edit special net " + net->getName());
    if (net->getNonDefaultRule() != nullptr) {
      throw std::runtime_error("trace v1 cannot edit NDR net " + net->getName());
    }
    if (net->getWire() != nullptr && net->getWireType() != odb::dbWireType::ROUTED) {
      throw std::runtime_error("trace cannot overwrite non-ROUTED wire on net " + net->getName());
    }
  }

  [[nodiscard]] json::value readTechHeader() const
  {
    auto* tech = block_->getTech();
    if (tech == nullptr) throw std::runtime_error("ODB trace block has no technology");
    return json::array{tech->getDbUnitsPerMicron(), tech->getManufacturingGrid(),
                       tech->getUseMinSpacingObs() == odb::dbOnOffType::ON};
  }

  [[nodiscard]] json::value readDie() const
  {
    const auto box = block_->getDieArea();
    return json::array{block_->getDbUnitsPerMicron(), box.xMin(), box.yMin(), box.xMax(), box.yMax()};
  }

  [[nodiscard]] json::value scanTracks() const
  {
    json::array patterns;
    for (auto* grid : block_->getTrackGrids()) {
      if (grid->getTechLayer()->isBackside()) continue;
      const std::string layer = grid->getTechLayer()->getName();
      for (int index = 0; index < grid->getNumGridPatternsX(); ++index) {
        int start = 0, count = 0, step = 0;
        grid->getGridPatternX(index, start, count, step);
        patterns.emplace_back(json::array{layer, "X", start, count, step});
      }
      for (int index = 0; index < grid->getNumGridPatternsY(); ++index) {
        int start = 0, count = 0, step = 0;
        grid->getGridPatternY(index, start, count, step);
        patterns.emplace_back(json::array{layer, "Y", start, count, step});
      }
    }
    return patterns;
  }

  [[nodiscard]] json::value readInstancePins(odb::dbInst* instance) const
  {
    json::array terms;
    for (auto* pin : instance->getITerms()) {
      auto* term = pin->getMTerm();
      if (term == nullptr) throw std::runtime_error("ODB instance pin has no master terminal");
      terms.emplace_back(term->getName());
    }
    return terms;
  }

  [[nodiscard]] json::value readNetAttributes(odb::dbNet* net) const
  {
    if (net->isSpecial()) throw std::runtime_error("trace ordinary net attribute read saw special net");
    auto* rule = net->getNonDefaultRule();
    return json::array{net->getSigType().getString(),
                       rule == nullptr ? json::value(nullptr) : json::value(rule->getName()),
                       net->hasJumpers(), net->isConnectedByAbutment()};
  }

  [[nodiscard]] json::value readGCellGrid() const
  {
    auto* grid = block_->getGCellGrid();
    if (grid == nullptr || grid->getNumGridPatternsX() != 1 || grid->getNumGridPatternsY() != 1) {
      return nullptr;
    }
    int x_start = 0, x_count = 0, x_step = 0;
    int y_start = 0, y_count = 0, y_step = 0;
    grid->getGridPatternX(0, x_start, x_count, x_step);
    grid->getGridPatternY(0, y_start, y_count, y_step);
    return json::array{x_start, x_count, x_step, y_start, y_count, y_step};
  }

  [[nodiscard]] json::array scanInstances() const
  {
    json::array names;
    for (auto* instance : block_->getInsts()) names.emplace_back(instance->getName());
    return names;
  }

  [[nodiscard]] json::array scanNets() const
  {
    json::array names;
    for (auto* net : block_->getNets()) {
      if (!net->isSpecial()) names.emplace_back(net->getName());
    }
    return names;
  }

  [[nodiscard]] json::array readInstance(odb::dbInst* instance) const
  {
    auto* master = instance->getMaster();
    if (master == nullptr) throw std::runtime_error("ODB instance has no master: " + instance->getName());
    int x = 0;
    int y = 0;
    instance->getLocation(x, y);
    return {instance->getName(), master->getName(), x, y, defOrientation(instance->getOrient())};
  }

  [[nodiscard]] json::array netPins(odb::dbNet* net) const
  {
    if (net->isSpecial()) throw std::runtime_error("trace cannot read special net pins: " + net->getName());
    json::array pins;
    for (auto* term : net->getITerms()) {
      auto* instance = term->getInst();
      auto* master_term = term->getMTerm();
      if (instance == nullptr || master_term == nullptr) {
        throw std::runtime_error("ODB net has an incomplete instance terminal");
      }
      pins.emplace_back(json::array{"I", instance->getName(), master_term->getName()});
    }
    for (auto* term : net->getBTerms()) {
      pins.emplace_back(json::array{"B", term->getName(), ""});
    }
    return pins;
  }

  [[nodiscard]] json::array readWire(odb::dbNet* net) const
  {
    return drt_trace::shapeProjection(drt_trace::readOdbWire(net), false);
  }

  [[nodiscard]] json::value findInstance(std::string_view name) const
  {
    auto* instance = block_->findInst(std::string(name).c_str());
    return instance == nullptr ? json::value(nullptr) : json::value(instance->getName());
  }

  [[nodiscard]] json::value findPin(odb::dbInst* instance, std::string_view name) const
  {
    auto* term = instance->findITerm(std::string(name).c_str());
    return term == nullptr ? json::value(nullptr) : json::value(term->getMTerm()->getName());
  }

  void clearWire(odb::dbNet* net) const
  {
    requireEditable(net);
    if (auto* wire = net->getWire()) odb::dbWire::destroy(wire);
  }

  void addSegment(odb::dbWireEncoder& encoder, const drt_trace::Shape& shape) const
  {
    if (!shape.via.empty() || !shape.tech_via) throw std::runtime_error("trace segment carries unsupported metadata");
    if (shape.x1 == shape.x2 && shape.y1 == shape.y2) {
      throw std::runtime_error("trace cannot encode a zero-length segment");
    }
    if (shape.x1 != shape.x2 && shape.y1 != shape.y2) {
      throw std::runtime_error("trace cannot encode a non-orthogonal segment");
    }
    auto* layer = requireRoutingLayer(shape.layer);
    encoder.newPath(layer, odb::dbWireType::ROUTED);
    if (shape.begin_extension) {
      encoder.addPoint(shape.x1, shape.y1, *shape.begin_extension, 0);
    } else {
      encoder.addPoint(shape.x1, shape.y1);
    }
    if (shape.end_extension) {
      encoder.addPoint(shape.x2, shape.y2, *shape.end_extension, 0);
    } else {
      encoder.addPoint(shape.x2, shape.y2);
    }
  }

  void addVia(odb::dbWireEncoder& encoder, const drt_trace::Shape& shape) const
  {
    if (shape.via.empty() || shape.x2 != shape.x1 || shape.y2 != shape.y1 || shape.begin_extension || shape.end_extension) {
      throw std::runtime_error("trace VIA carries unsupported metadata");
    }
    auto* layer = requireRoutingLayer(shape.layer);
    if (shape.tech_via) {
      auto* via = block_->getTech()->findVia(shape.via.c_str());
      if (via == nullptr) throw std::runtime_error("unknown technology VIA in trace: " + shape.via);
      if (via->getBottomLayer() != layer) {
        throw std::runtime_error("technology VIA bottom layer disagrees with trace: " + shape.via);
      }
      encoder.newPath(layer, odb::dbWireType::ROUTED);
      encoder.addPoint(shape.x1, shape.y1);
      encoder.addTechVia(via);
      return;
    }

    auto* via = block_->findVia(shape.via.c_str());
    if (via == nullptr) throw std::runtime_error("unknown block VIA in trace: " + shape.via);
    if (via->isViaRotated() || via->getOrient().getValue() != odb::dbOrientType::R0) {
      throw std::runtime_error("trace v1 cannot encode an oriented block VIA: " + shape.via);
    }
    if (via->getBottomLayer() != layer) {
      throw std::runtime_error("block VIA bottom layer disagrees with trace: " + shape.via);
    }
    encoder.newPath(layer, odb::dbWireType::ROUTED);
    encoder.addPoint(shape.x1, shape.y1);
    encoder.addVia(via);
  }

  void addPatch(odb::dbWireEncoder& encoder, const drt_trace::Shape& shape) const
  {
    if (!shape.via.empty() || !shape.tech_via || shape.begin_extension || shape.end_extension) {
      throw std::runtime_error("trace patch carries unsupported metadata");
    }
    if (shape.x1 > shape.x2 || shape.y1 > shape.y2) {
      throw std::runtime_error("trace patch is not an absolute lower-left/upper-right rectangle");
    }
    auto* layer = requireRoutingLayer(shape.layer);
    encoder.newPath(layer, odb::dbWireType::ROUTED);
    encoder.addPoint(shape.x1, shape.y1);
    encoder.addRect(0, 0, checkedDelta(shape.x2, shape.x1, shape.layer), checkedDelta(shape.y2, shape.y1, shape.layer));
  }

  void replaceWire(odb::dbNet* net, const std::vector<drt_trace::Shape>& shapes) const
  {
    requireEditable(net);
    if (auto* old_wire = net->getWire()) odb::dbWire::destroy(old_wire);
    auto* wire = odb::dbWire::create(net);
    if (wire == nullptr) throw std::runtime_error("cannot create replacement ODB wire for net " + net->getName());

    odb::dbWireEncoder encoder;
    encoder.begin(wire);
    for (const auto& shape : shapes) {
      if (shape.kind == "segment") {
        addSegment(encoder, shape);
      } else if (shape.kind == "via") {
        addVia(encoder, shape);
      } else if (shape.kind == "patch") {
        addPatch(encoder, shape);
      } else {
        throw std::runtime_error("unsupported trace primitive kind: " + shape.kind);
      }
    }
    encoder.end();
  }

  odb::dbDatabase* database_;
  odb::dbBlock* block_;
  std::vector<odb::dbInst*> instances_;
  std::vector<odb::dbNet*> nets_;
};

}  // namespace

int main(int argc, char** argv)
{
  try {
    const auto options = drt_trace::options(argc, argv);
    if (options.backend != "odb") throw std::runtime_error("DrtTraceOdb requires --backend odb");
    drt_trace::TraceReader trace(options.trace);

    using DatabasePtr = std::unique_ptr<odb::dbDatabase, void (*)(odb::dbDatabase*)>;
    DatabasePtr database(odb::dbDatabase::create(), odb::dbDatabase::destroy);
    if (!database) throw std::runtime_error("cannot create ODB database");

    utl::Logger logger;
    database->setLogger(&logger);
    odb::lefin lef_reader(database.get(), &logger, false);
    if (lef_reader.createTechAndLib("drt_trace_odb_tech", "drt_trace_odb_lib", options.lef.c_str()) == nullptr) {
      throw std::runtime_error("OpenDB LEF import failed");
    }
    auto* chip = odb::dbChip::create(database.get(), database->getTech());
    if (chip == nullptr) throw std::runtime_error("cannot create ODB chip");
    std::vector<odb::dbLib*> libraries;
    for (auto* library : database->getLibs()) libraries.push_back(library);
    odb::defin def_reader(database.get(), &logger);
    def_reader.readChip(libraries, options.def.c_str(), chip);
    auto* block = chip->getBlock();
    if (block == nullptr) throw std::runtime_error("OpenDB DEF import failed");
    block->setMinRoutingLayer(1);
    if (!options.guides.empty()) importGuides(block, options.guides);
    restoreInitialGCellGrid(block, trace.header());

    OdbBackend backend(database.get(), block, trace.header());
    drt_trace::replayStream(backend, trace, options);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
