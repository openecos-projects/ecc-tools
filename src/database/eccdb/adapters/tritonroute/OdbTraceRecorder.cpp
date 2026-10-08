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

#include "OdbTraceRecorder.h"
#include "OdbTraceDesignProjection.h"
#include "OdbTraceTechProjection.h"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

#include "../../benchmarks/DrtTraceOdb.h"
#include "odb/db.h"

namespace eccdb::tritonroute {
namespace {
namespace json = boost::json;

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
  throw std::runtime_error("unsupported ODB orientation for trace");
}

[[nodiscard]] json::value pathValue(const std::filesystem::path& path)
{
  if (path.empty()) {
    return json::value(nullptr);
  }
  return json::value(path.string());
}

[[nodiscard]] json::value gcellGridValue(odb::dbGCellGrid* grid)
{
  if (grid == nullptr || grid->getNumGridPatternsX() != 1
      || grid->getNumGridPatternsY() != 1) {
    return nullptr;
  }
  int x_start = 0;
  int x_count = 0;
  int x_step = 0;
  int y_start = 0;
  int y_count = 0;
  int y_step = 0;
  grid->getGridPatternX(0, x_start, x_count, x_step);
  grid->getGridPatternY(0, y_start, y_count, y_step);
  return json::array{x_start, x_count, x_step, y_start, y_count, y_step};
}

[[nodiscard]] json::object eventBase(std::string_view op,
                                     std::string_view source)
{
  json::object event;
  event["op"] = std::string(op);
  event["source"] = std::string(source);
  return event;
}

void requireRegularNet(const odb::dbNet* net, std::string_view operation)
{
  if (net->isSpecial()) {
    throw std::runtime_error("trace operation " + std::string(operation)
                             + " does not support special nets");
  }
}

[[nodiscard]] json::value projectionExpected(
    const odb_trace_projection::Projection& projection,
    std::string_view operation,
    std::string_view source,
    std::string_view object)
{
  if (projection.complete()) {
    return *projection.expected;
  }

  std::string message = "cannot record " + std::string(operation) + " at "
                        + std::string(source) + " for " + std::string(object);
  if (!projection.expected.has_value()) {
    message += ": projection has no expected value";
  }
  for (const auto& reason : projection.unrecordable) {
    message += "; " + reason;
  }
  throw std::runtime_error(message);
}

void attachProjection(json::object& event,
                      const odb_trace_projection::Projection& projection,
                      std::string_view operation,
                      std::string_view source,
                      std::string_view object)
{
  event["expected"] = projectionExpected(projection, operation, source, object);
  if (!projection.ignored.empty()) {
    json::array ignored;
    for (const auto& reason : projection.ignored) {
      ignored.emplace_back(reason);
    }
    event["ignored"] = std::move(ignored);
  }
}

[[nodiscard]] std::size_t masterPinOrdinal(odb::dbMPin* pin)
{
  if (pin == nullptr || pin->getMTerm() == nullptr) {
    throw std::runtime_error("cannot find an ordinal for a null master pin");
  }
  std::size_t ordinal = 0;
  for (auto* candidate : pin->getMTerm()->getMPins()) {
    if (candidate == pin) {
      return ordinal;
    }
    ++ordinal;
  }
  throw std::runtime_error("cannot find a master pin in its owning terminal");
}

[[nodiscard]] json::value writerAccessPointValue(
    odb::dbAccessPoint* access_point,
    std::string_view operation,
    std::string_view source,
    std::string_view object)
{
  const auto projection = odb_trace_projection::pinAccess(access_point);
  json::value value = projectionExpected(projection, operation, source, object);
  if (value.is_null()) {
    throw std::runtime_error("cannot record " + std::string(operation) + " at "
                             + std::string(source) + " for "
                             + std::string(object)
                             + ": Writer produced an access point filtered by DRT");
  }
  return value;
}

[[nodiscard]] json::array trackValues(odb::dbNet* net)
{
  if (net == nullptr) {
    throw std::runtime_error("cannot project tracks for a null net");
  }

  json::array tracks;
  // dbNetTrack::create pushes at the list head. Preserve dbNet::getTracks()
  // order because it is the actual final ODB state, not input insertion order.
  for (auto* track : net->getTracks()) {
    auto* layer = track->getLayer();
    if (layer == nullptr) {
      throw std::runtime_error("track on net " + net->getName()
                               + " has no technology layer");
    }
    const odb::Rect box = track->getBox();
    tracks.emplace_back(json::array{layer->getName(),
                                    box.xMin(),
                                    box.yMin(),
                                    box.xMax(),
                                    box.yMax()});
  }
  return tracks;
}

[[nodiscard]] json::array masterGroupValues(
    odb::dbMaster* master,
    const std::vector<int>& groups,
    std::string_view operation,
    std::string_view source)
{
  if (master == nullptr) {
    throw std::runtime_error("cannot project master pin access for a null master");
  }

  json::array values;
  for (auto* term : master->getMTerms()) {
    if (term == nullptr) {
      throw std::runtime_error("master " + master->getName()
                               + " contains a null terminal");
    }
    std::size_t ordinal = 0;
    for (auto* pin : term->getMPins()) {
      if (pin == nullptr) {
        throw std::runtime_error("master " + master->getName() + "/"
                                 + term->getName()
                                 + " contains a null physical pin");
      }
      const auto pin_access = pin->getPinAccess();
      for (const int group : groups) {
        if (group < 0) {
          throw std::runtime_error("master " + master->getName()
                                   + " has a negative pin-access group");
        }
        json::array points;
        const auto group_index = static_cast<std::size_t>(group);
        if (group_index < pin_access.size()) {
          for (auto* access_point : pin_access[group_index]) {
            points.emplace_back(writerAccessPointValue(
                access_point,
                operation,
                source,
                master->getName() + "/" + term->getName()));
          }
        }
        values.emplace_back(json::array{term->getName(),
                                        static_cast<uint64_t>(ordinal),
                                        group,
                                        std::move(points)});
      }
      ++ordinal;
    }
  }
  return values;
}

[[nodiscard]] json::value accessPointLocator(odb::dbAccessPoint* access_point,
                                              odb::dbMPin* pin,
                                              std::string_view object)
{
  if (access_point == nullptr || pin == nullptr || access_point->getMPin() != pin) {
    throw std::runtime_error("cannot locate a preferred access point for "
                             + std::string(object));
  }
  const auto pin_access = pin->getPinAccess();
  for (std::size_t group = 0; group < pin_access.size(); ++group) {
    for (std::size_t index = 0; index < pin_access[group].size(); ++index) {
      if (pin_access[group][index] == access_point) {
        return json::array{static_cast<uint64_t>(group),
                           static_cast<uint64_t>(index)};
      }
    }
  }
  throw std::runtime_error("preferred access point is absent from its master pin for "
                           + std::string(object));
}

struct InstancePinAccessValues
{
  json::array payload_terms;
  json::array expected_terms;
};

[[nodiscard]] InstancePinAccessValues instancePinAccessValues(
    odb::dbInst* instance,
    std::string_view operation,
    std::string_view source)
{
  if (instance == nullptr) {
    throw std::runtime_error("cannot project instance pin access for a null instance");
  }

  InstancePinAccessValues result;
  for (auto* term : instance->getITerms()) {
    if (term == nullptr || term->getMTerm() == nullptr) {
      throw std::runtime_error("instance " + instance->getName()
                               + " has an incomplete instance terminal");
    }
    const std::string object = instance->getName() + "/" + term->getMTerm()->getName();
    const auto preferred = term->getPrefAccessPoints();
    json::array locators;
    json::array expected;
    std::size_t ordinal = 0;
    for (auto* pin : term->getMTerm()->getMPins()) {
      if (pin == nullptr) {
        throw std::runtime_error("instance terminal " + object
                                 + " has a null master pin");
      }
      odb::dbAccessPoint* selected = nullptr;
      for (auto* access_point : preferred) {
        if (access_point == nullptr) {
          throw std::runtime_error("instance terminal " + object
                                   + " has a null preferred access point");
        }
        if (access_point->getMPin() == pin) {
          if (selected != nullptr) {
            throw std::runtime_error("instance terminal " + object
                                     + " has duplicate preferred access points");
          }
          selected = access_point;
        }
      }
      if (selected == nullptr) {
        locators.emplace_back(json::array{static_cast<uint64_t>(ordinal), nullptr});
        expected.emplace_back(json::array{static_cast<uint64_t>(ordinal), nullptr});
      } else {
        locators.emplace_back(
            json::array{static_cast<uint64_t>(ordinal),
                        accessPointLocator(selected, pin, object)});
        expected.emplace_back(
            json::array{static_cast<uint64_t>(ordinal),
                        writerAccessPointValue(selected, operation, source, object)});
      }
      ++ordinal;
    }
    result.payload_terms.emplace_back(
        json::array{term->getMTerm()->getName(), std::move(locators)});
    result.expected_terms.emplace_back(
        json::array{term->getMTerm()->getName(), std::move(expected)});
  }
  return result;
}

[[nodiscard]] json::array btermAccessPointValues(odb::dbBPin* pin,
                                                   std::string_view operation,
                                                   std::string_view source,
                                                   std::string_view object)
{
  if (pin == nullptr) {
    throw std::runtime_error("cannot project access points for a null BPin");
  }
  json::array values;
  for (auto* access_point : pin->getAccessPoints()) {
    values.emplace_back(
        writerAccessPointValue(access_point, operation, source, object));
  }
  return values;
}

[[nodiscard]] std::size_t bpinOrdinal(odb::dbBTerm* term, odb::dbBPin* pin)
{
  if (term == nullptr || pin == nullptr) {
    throw std::runtime_error("cannot find an ordinal for a null block-terminal pin");
  }
  std::size_t ordinal = 0;
  for (auto* candidate : term->getBPins()) {
    if (candidate == pin) {
      return ordinal;
    }
    ++ordinal;
  }
  throw std::runtime_error("cannot find a block-terminal pin in its owner");
}

}  // namespace

OdbTraceRecorder* OdbTraceRecorder::active_ = nullptr;

OdbTraceRecorder::OdbTraceRecorder(const std::filesystem::path& path,
                                   odb::dbBlock* block,
                                   const OdbTraceInputPaths& input_paths)
{
  if (active_ != nullptr) {
    throw std::runtime_error("an ODB trace recorder is already active");
  }
  if (block == nullptr) {
    throw std::runtime_error("cannot begin an ODB trace without a block");
  }

  file_.open(path);
  if (!file_) {
    throw std::runtime_error("cannot open ODB trace: " + path.string());
  }

  json::array instances;
  for (auto* instance : block->getInsts()) {
    instances.emplace_back(
        json::array{static_cast<uint32_t>(instance->getId()), instance->getName()});
  }
  json::array nets;
  for (auto* net : block->getNets()) {
    if (!net->isSpecial()) {
      nets.emplace_back(
          json::array{static_cast<uint32_t>(net->getId()), net->getName()});
    }
  }

  if (auto* tech = block->getTech(); tech != nullptr) {
    for (auto* via : tech->getVias()) {
      known_vias_.emplace(true, via->getName());
    }
  }
  for (auto* via : block->getVias()) {
    known_vias_.emplace(false, via->getName());
  }

  json::object paths;
  paths["lef"] = pathValue(input_paths.lef);
  paths["def"] = pathValue(input_paths.def);
  paths["guides"] = pathValue(input_paths.guides);
  json::object header;
  header["schema"] = "tritonroute_db_trace_v4";
  header["instances"] = std::move(instances);
  header["nets"] = std::move(nets);
  header["initial_gcell_grid"] = gcellGridValue(block->getGCellGrid());
  header["scope"] = "bounded Parser/GuideProcessor ODB input projections plus the explicitly listed Writer mutations, captured at real TritonRoute ODB call sites; not automatic interception of every ODB getter or complete Writer output coverage";
  header["supported_operations"] = json::array{
      "read_tech_header", "read_tech_layers", "read_routing_layer_rules",
      "read_cut_layer_rules", "read_tech_vias", "read_design_vias",
      "read_via_generate_rules", "read_ndrs", "read_library_masters",
      "read_library_terms", "read_library_mpin_geometry",
      "read_library_obstructions", "read_blockages", "read_bterms",
      "read_bterm_pin_access", "read_master_pin_access",
      "read_preferred_pin_access", "read_special_wire", "read_guides",
      "read_die", "scan_tracks",
      "scan_instances", "read_instance", "read_instance_pins", "scan_nets",
      "read_net_attributes", "net_pins", "read_wire", "read_gcell_grid",
      "find_instance", "find_pin", "define_design_via", "clear_wire",
      "replace_wire", "append_track_assignment",
      "replace_master_pin_access", "replace_instance_pin_access",
      "append_bterm_pin_access", "replace_guides",
      "append_bterm_stack_wire"};
  header["excluded"] = json::array{
      "backside technology layers and objects filtered by Parser",
      "advanced LEF58 routing and cut constraints",
      "LEF58 metal-width-via map and related advanced technology rules",
      "library polygon geometry, antenna data, masks and non-geometry terminal attributes",
      "non-listed Writer mutations, ODB callbacks, and object-identity side effects",
      "pin-access object identity and non-listed access-point mutations", "drc",
      "special-wire layer masks"};
  header["unsupported"] = json::array{"non_routed_wires",
                                       "wire_non_default_routing_rules",
                                       "wire_junctions",
                                       "wire_short_paths",
                                       "wire_virtual_paths",
                                       "wire_masks",
                                       "via_definition_masks"};
  header["provenance"] = json::object{{"input_paths", std::move(paths)}};
  file_ << json::serialize(header) << '\n';
  if (!file_) {
    throw std::runtime_error("cannot write ODB trace header: " + path.string());
  }
  active_ = this;
}

OdbTraceRecorder::~OdbTraceRecorder()
{
  if (active_ == this) {
    active_ = nullptr;
  }
}

void OdbTraceRecorder::recordReadTechHeader(std::string_view source,
                                            odb::dbTech* tech)
{
  if (tech == nullptr) {
    throw std::runtime_error("cannot record a null technology header");
  }
  auto event = eventBase("read_tech_header", source);
  event["expected"] = json::array{
      tech->getDbUnitsPerMicron(),
      tech->getManufacturingGrid(),
      tech->getUseMinSpacingObs() == odb::dbOnOffType::ON};
  record(std::move(event));
}

void OdbTraceRecorder::recordReadTechLayers(std::string_view source,
                                            odb::dbTech* tech)
{
  if (tech == nullptr) {
    throw std::runtime_error("cannot record layers for a null technology");
  }
  auto event = eventBase("read_tech_layers", source);
  event["expected"] = trace_projection::techLayers(tech);
  record(std::move(event));

  event = eventBase("read_routing_layer_rules", source);
  event["expected"] = trace_projection::routingLayerRules(tech);
  record(std::move(event));

  event = eventBase("read_cut_layer_rules", source);
  event["expected"] = trace_projection::cutLayerRules(tech);
  record(std::move(event));
}

void OdbTraceRecorder::recordReadTechVias(std::string_view source,
                                          odb::dbTech* tech)
{
  if (tech == nullptr) {
    throw std::runtime_error("cannot record vias for a null technology");
  }
  auto event = eventBase("read_tech_vias", source);
  event["expected"] = trace_projection::techVias(tech);
  record(std::move(event));
}

void OdbTraceRecorder::recordReadDesignVias(std::string_view source,
                                            odb::dbBlock* block)
{
  if (block == nullptr) {
    throw std::runtime_error("cannot record vias for a null block");
  }
  auto event = eventBase("read_design_vias", source);
  event["expected"] = trace_projection::designVias(block);
  record(std::move(event));
}

void OdbTraceRecorder::recordReadViaGenerateRules(std::string_view source,
                                                   odb::dbTech* tech)
{
  if (tech == nullptr) {
    throw std::runtime_error("cannot record via rules for a null technology");
  }
  auto event = eventBase("read_via_generate_rules", source);
  event["expected"] = trace_projection::viaGenerateRules(tech);
  record(std::move(event));
}

void OdbTraceRecorder::recordReadNdrs(std::string_view source,
                                      odb::dbDatabase* database)
{
  if (database == nullptr || database->getTech() == nullptr) {
    throw std::runtime_error("cannot record NDRs for a database without technology");
  }
  auto event = eventBase("read_ndrs", source);
  event["expected"] = trace_projection::ndrs(database);
  record(std::move(event));
}

void OdbTraceRecorder::recordReadLibrary(std::string_view source,
                                         odb::dbDatabase* database)
{
  if (database == nullptr) {
    throw std::runtime_error("cannot record libraries for a null database");
  }

  auto event = eventBase("read_library_masters", source);
  event["expected"] = trace_projection::libraryMasters(database);
  record(std::move(event));

  event = eventBase("read_library_terms", source);
  event["expected"] = trace_projection::libraryTerms(database);
  record(std::move(event));

  event = eventBase("read_library_mpin_geometry", source);
  event["expected"] = trace_projection::libraryMPinGeometry(database);
  record(std::move(event));

  event = eventBase("read_library_obstructions", source);
  event["expected"] = trace_projection::libraryObstructions(database);
  record(std::move(event));
}

void OdbTraceRecorder::recordReadBlockages(std::string_view source,
                                           odb::dbBlock* block)
{
  auto projection = odb_trace_projection::blockages(block);
  auto event = eventBase("read_blockages", source);
  attachProjection(event, projection, "read_blockages", source, "block");
  record(std::move(event));
}

void OdbTraceRecorder::recordReadBTerms(std::string_view source,
                                        odb::dbBlock* block)
{
  auto projection = odb_trace_projection::bterms(block);
  auto event = eventBase("read_bterms", source);
  attachProjection(event, projection, "read_bterms", source, "block");
  record(std::move(event));
}

void OdbTraceRecorder::recordReadBTermPinAccess(std::string_view source,
                                                odb::dbBTerm* term,
                                                odb::dbBPin* pin)
{
  if (term == nullptr || pin == nullptr) {
    throw std::runtime_error("cannot record pin access for a null BTerm or BPin");
  }
  auto projection = odb_trace_projection::btermPinAccess(pin);
  auto event = eventBase("read_bterm_pin_access", source);
  event["name"] = term->getName();
  event["ordinal"] = static_cast<uint64_t>(bpinOrdinal(term, pin));
  attachProjection(
      event, projection, "read_bterm_pin_access", source, term->getName());
  record(std::move(event));
}

void OdbTraceRecorder::recordReadMasterPinAccess(std::string_view source,
                                                 odb::dbMPin* pin)
{
  if (pin == nullptr || pin->getMaster() == nullptr || pin->getMTerm() == nullptr) {
    throw std::runtime_error("cannot record pin access for an incomplete master pin");
  }
  auto projection = odb_trace_projection::masterPinAccess(pin);
  auto event = eventBase("read_master_pin_access", source);
  event["master"] = pin->getMaster()->getName();
  event["term"] = pin->getMTerm()->getName();
  event["ordinal"] = static_cast<uint64_t>(masterPinOrdinal(pin));
  const std::string object = pin->getMaster()->getName() + "/"
                             + pin->getMTerm()->getName();
  attachProjection(event, projection, "read_master_pin_access", source, object);
  record(std::move(event));
}

void OdbTraceRecorder::recordReadPreferredPinAccess(std::string_view source,
                                                    odb::dbInst* instance,
                                                    odb::dbITerm* term)
{
  if (instance == nullptr || term == nullptr || term->getMTerm() == nullptr
      || term->getInst() != instance) {
    throw std::runtime_error("cannot record preferred access for an invalid instance pin");
  }
  auto projection = odb_trace_projection::preferredPinAccess(term);
  auto event = eventBase("read_preferred_pin_access", source);
  event["instance"] = instance->getName();
  event["term"] = term->getMTerm()->getName();
  const std::string object = instance->getName() + "/"
                             + term->getMTerm()->getName();
  attachProjection(event, projection, "read_preferred_pin_access", source, object);
  record(std::move(event));
}

void OdbTraceRecorder::recordReadSpecialWire(std::string_view source,
                                             odb::dbNet* net)
{
  if (net == nullptr || !net->isSpecial()) {
    throw std::runtime_error("cannot record a special wire for an ordinary or null net");
  }
  auto projection = odb_trace_projection::specialWire(net);
  auto event = eventBase("read_special_wire", source);
  // Special nets are deliberately name-bound instead of reusing ordinary-net
  // token bindings from the trace header.
  event["name"] = net->getName();
  attachProjection(event, projection, "read_special_wire", source, net->getName());
  record(std::move(event));
}

void OdbTraceRecorder::recordReadGuides(std::string_view source, odb::dbNet* net)
{
  if (net == nullptr) {
    throw std::runtime_error("cannot record guides for a null net");
  }
  auto projection = odb_trace_projection::guides(net);
  auto event = eventBase("read_guides", source);
  event["name"] = net->getName();
  attachProjection(event, projection, "read_guides", source, net->getName());
  record(std::move(event));
}

void OdbTraceRecorder::recordReadDie(std::string_view source,
                                     odb::dbBlock* block)
{
  if (block == nullptr) {
    throw std::runtime_error("cannot record a null die block");
  }
  const odb::Rect die = block->getDieArea();
  auto event = eventBase("read_die", source);
  event["expected"] = json::array{block->getDbUnitsPerMicron(),
                                    die.xMin(),
                                    die.yMin(),
                                    die.xMax(),
                                    die.yMax()};
  record(std::move(event));
}

void OdbTraceRecorder::recordScanTracks(std::string_view source,
                                        odb::dbBlock* block)
{
  if (block == nullptr) {
    throw std::runtime_error("cannot record tracks for a null block");
  }

  json::array tracks;
  for (auto* track : block->getTrackGrids()) {
    auto* layer = track->getTechLayer();
    // Parser::setTracks skips backside layers before reading their patterns.
    if (layer->isBackside()) {
      continue;
    }
    for (int index = 0; index < track->getNumGridPatternsX(); ++index) {
      int start = 0;
      int count = 0;
      int step = 0;
      track->getGridPatternX(index, start, count, step);
      tracks.emplace_back(
          json::array{layer->getName(), "X", start, count, step});
    }
    for (int index = 0; index < track->getNumGridPatternsY(); ++index) {
      int start = 0;
      int count = 0;
      int step = 0;
      track->getGridPatternY(index, start, count, step);
      tracks.emplace_back(
          json::array{layer->getName(), "Y", start, count, step});
    }
  }

  auto event = eventBase("scan_tracks", source);
  event["expected"] = drt_trace::canonical(std::move(tracks));
  record(std::move(event));
}

void OdbTraceRecorder::recordScanInstances(std::string_view source,
                                           odb::dbBlock* block)
{
  json::array names;
  for (auto* instance : block->getInsts()) {
    names.emplace_back(instance->getName());
  }
  auto event = eventBase("scan_instances", source);
  event["expected"] = drt_trace::canonical(std::move(names));
  record(std::move(event));
}

void OdbTraceRecorder::recordScanNets(std::string_view source,
                                      odb::dbBlock* block)
{
  json::array names;
  for (auto* net : block->getNets()) {
    if (!net->isSpecial()) {
      names.emplace_back(net->getName());
    }
  }
  auto event = eventBase("scan_nets", source);
  event["expected"] = drt_trace::canonical(std::move(names));
  record(std::move(event));
}

void OdbTraceRecorder::recordReadInstance(std::string_view source,
                                          odb::dbInst* instance,
                                          std::string_view name,
                                          std::string_view master,
                                          int x,
                                          int y,
                                          const odb::dbOrientType& orient)
{
  auto event = eventBase("read_instance", source);
  event["object"] = static_cast<uint32_t>(instance->getId());
  event["expected"] = json::array{std::string(name), std::string(master), x, y,
                                    defOrientation(orient)};
  record(std::move(event));
}

void OdbTraceRecorder::recordReadInstancePins(std::string_view source,
                                              odb::dbInst* instance)
{
  if (instance == nullptr) {
    throw std::runtime_error("cannot record pins for a null instance");
  }
  json::array names;
  for (auto* term : instance->getITerms()) {
    names.emplace_back(term->getMTerm()->getName());
  }
  auto event = eventBase("read_instance_pins", source);
  event["object"] = static_cast<uint32_t>(instance->getId());
  // The ODB ITerm collection includes an ITerm even when it has no net.
  event["expected"] = drt_trace::canonical(std::move(names));
  record(std::move(event));
}

void OdbTraceRecorder::recordReadNetAttributes(std::string_view source,
                                                odb::dbNet* net)
{
  requireRegularNet(net, "read_net_attributes");
  auto* ndr = net->getNonDefaultRule();
  auto event = eventBase("read_net_attributes", source);
  event["object"] = static_cast<uint32_t>(net->getId());
  event["expected"] = json::array{
      std::string(net->getSigType().getString()),
      ndr == nullptr ? json::value(nullptr) : json::value(ndr->getName()),
      net->hasJumpers(),
      net->isConnectedByAbutment()};
  record(std::move(event));
}

void OdbTraceRecorder::recordNetPins(std::string_view source, odb::dbNet* net)
{
  requireRegularNet(net, "net_pins");
  json::array pins;
  for (auto* term : net->getITerms()) {
    pins.emplace_back(
        json::array{"I", term->getInst()->getName(), term->getMTerm()->getName()});
  }
  for (auto* term : net->getBTerms()) {
    pins.emplace_back(json::array{"B", term->getName(), ""});
  }
  auto event = eventBase("net_pins", source);
  event["object"] = static_cast<uint32_t>(net->getId());
  event["expected"] = drt_trace::canonical(std::move(pins));
  record(std::move(event));
}

void OdbTraceRecorder::recordReadWire(std::string_view source, odb::dbNet* net)
{
  requireRegularNet(net, "read_wire");
  auto event = eventBase("read_wire", source);
  event["object"] = static_cast<uint32_t>(net->getId());
  event["expected"] = drt_trace::shapeValues(drt_trace::readOdbWire(net), false);
  record(std::move(event));
}

void OdbTraceRecorder::recordReadGCellGrid(std::string_view source,
                                           odb::dbGCellGrid* grid)
{
  auto event = eventBase("read_gcell_grid", source);
  event["expected"] = gcellGridValue(grid);
  record(std::move(event));
}

void OdbTraceRecorder::recordFindInstance(std::string_view source,
                                          std::string_view query,
                                          odb::dbInst* instance)
{
  auto event = eventBase("find_instance", source);
  event["name"] = std::string(query);
  event["expected"] = instance == nullptr ? json::value(nullptr)
                                            : json::value(instance->getName());
  record(std::move(event));
}

void OdbTraceRecorder::recordFindPin(std::string_view source,
                                     odb::dbInst* instance,
                                     std::string_view query,
                                     odb::dbITerm* term)
{
  auto event = eventBase("find_pin", source);
  event["object"] = static_cast<uint32_t>(instance->getId());
  event["name"] = std::string(query);
  event["expected"] = term == nullptr ? json::value(nullptr)
                                        : json::value(term->getMTerm()->getName());
  record(std::move(event));
}

void OdbTraceRecorder::recordDefineDesignVia(std::string_view source,
                                             odb::dbVia* via)
{
  if (active_ != this || finished_) {
    return;
  }
  if (via == nullptr) {
    throw std::runtime_error("cannot record a null design via");
  }

  const std::string name = via->getName();
  if (name.empty()) {
    throw std::runtime_error("cannot record an unnamed design via");
  }
  const auto key = std::make_pair(false, name);
  if (known_vias_.contains(key)) {
    throw std::runtime_error("cannot define an already known design via "
                             + name);
  }

  json::array rectangles;
  for (auto* box : via->getBoxes()) {
    if (box == nullptr) {
      throw std::runtime_error("design via " + name + " contains a null box");
    }
    if (box->getLayerMask() != 0) {
      throw std::runtime_error("design via " + name
                               + " has unsupported nonzero layer mask");
    }
    auto* layer = box->getTechLayer();
    if (layer == nullptr) {
      throw std::runtime_error("design via " + name + " has a non-tech-layer box");
    }
    const int x1 = box->xMin();
    const int y1 = box->yMin();
    const int x2 = box->xMax();
    const int y2 = box->yMax();
    if (x1 >= x2 || y1 >= y2) {
      throw std::runtime_error("design via " + name + " has an invalid rectangle");
    }
    // Keep ODB's box order and duplicates: they are part of the definition.
    rectangles.emplace_back(json::array{layer->getName(), x1, y1, x2, y2});
  }
  if (rectangles.empty()) {
    throw std::runtime_error("design via " + name + " has no rectangles");
  }

  auto event = eventBase("define_design_via", source);
  event["name"] = name;
  event["default"] = via->isDefault();
  event["rects"] = std::move(rectangles);
  record(std::move(event));
  known_vias_.emplace(std::move(key));
}

void OdbTraceRecorder::recordClearWire(std::string_view source, odb::dbNet* net)
{
  requireRegularNet(net, "clear_wire");
  auto event = eventBase("clear_wire", source);
  event["object"] = static_cast<uint32_t>(net->getId());
  record(std::move(event));
}

void OdbTraceRecorder::recordReplaceWire(
    std::string_view source,
    odb::dbNet* net,
    const std::vector<drt_trace::WireAction>& wire_actions)
{
  requireRegularNet(net, "replace_wire");
  const auto shapes = drt_trace::readOdbWire(net);
  for (const auto& shape : shapes) {
    if (shape.kind == "via" && !known_vias_.contains(std::make_pair(shape.tech_via, shape.via))) {
      throw std::runtime_error("cannot record unknown "
                               + std::string(shape.tech_via ? "technology" : "design")
                               + " via " + shape.via
                               + "; runtime design vias require an earlier definition event");
    }
  }
  json::array payload;
  payload.reserve(shapes.size());
  for (const auto& shape : shapes) {
    payload.emplace_back(drt_trace::shapeRecord(shape));
  }
  json::array actions;
  actions.reserve(wire_actions.size());
  for (const auto& action : wire_actions) {
    actions.emplace_back(drt_trace::wireActionRecord(action));
  }
  auto event = eventBase("replace_wire", source);
  event["object"] = static_cast<uint32_t>(net->getId());
  // Keep both representations: actions are the original encoder call stream,
  // while shapes are decoded after writing for independent semantic validation.
  event["wire_actions"] = std::move(actions);
  event["shapes"] = std::move(payload);
  record(std::move(event));
}

void OdbTraceRecorder::recordAppendTrackAssignment(
    std::string_view source,
    odb::dbNet* net,
    json::array appended)
{
  if (net == nullptr) {
    throw std::runtime_error("cannot record track assignment for a null net");
  }
  if (appended.empty()) {
    return;
  }

  auto event = eventBase("append_track_assignment", source);
  event["name"] = net->getName();
  event["payload"] = json::object{{"append", std::move(appended)}};
  event["expected"] = trackValues(net);
  record(std::move(event));
}

void OdbTraceRecorder::recordReplaceMasterPinAccess(
    std::string_view source,
    odb::dbMaster* master,
    const std::vector<int>& groups)
{
  if (master == nullptr) {
    throw std::runtime_error("cannot record pin access for a null master");
  }
  if (groups.empty()) {
    return;
  }

  json::array clear_groups;
  clear_groups.reserve(groups.size());
  for (const int group : groups) {
    if (group < 0) {
      throw std::runtime_error("cannot record a negative master pin-access group");
    }
    clear_groups.emplace_back(group);
  }
  const json::array created = masterGroupValues(
      master, groups, "replace_master_pin_access", source);
  auto event = eventBase("replace_master_pin_access", source);
  event["master"] = master->getName();
  event["payload"] = json::object{{"clear_groups", std::move(clear_groups)},
                                   {"create", created}};
  event["expected"] = created;
  record(std::move(event));
}

void OdbTraceRecorder::recordReplaceInstancePinAccess(
    std::string_view source,
    odb::dbInst* instance)
{
  if (instance == nullptr) {
    throw std::runtime_error("cannot record pin access for a null instance");
  }
  const auto values = instancePinAccessValues(
      instance, "replace_instance_pin_access", source);
  auto event = eventBase("replace_instance_pin_access", source);
  event["instance"] = instance->getName();
  event["payload"] = json::object{
      {"pin_access_index", instance->getPinAccessIdx()},
      {"terms", values.payload_terms}};
  event["expected"] = values.expected_terms;
  record(std::move(event));
}

void OdbTraceRecorder::recordAppendBTermPinAccess(
    std::string_view source,
    odb::dbBTerm* term,
    odb::dbBPin* pin,
    std::size_t previous_count)
{
  if (term == nullptr || pin == nullptr || pin->getBTerm() != term) {
    throw std::runtime_error("cannot record pin access for an invalid block terminal");
  }
  const auto values = btermAccessPointValues(
      pin, "append_bterm_pin_access", source, term->getName());
  if (previous_count > values.size()) {
    throw std::runtime_error("block-terminal access-point count shrank while recording "
                             + term->getName());
  }
  if (previous_count == values.size()) {
    return;
  }

  json::array appended;
  appended.reserve(values.size() - previous_count);
  for (std::size_t index = previous_count; index < values.size(); ++index) {
    appended.emplace_back(values[index]);
  }
  auto event = eventBase("append_bterm_pin_access", source);
  event["name"] = term->getName();
  event["payload"] = json::object{
      {"bpin_ordinal", static_cast<uint64_t>(bpinOrdinal(term, pin))},
      {"append", std::move(appended)}};
  event["expected"] = values;
  record(std::move(event));
}

void OdbTraceRecorder::recordReplaceGuides(std::string_view source,
                                           odb::dbNet* net,
                                           json::array guides)
{
  if (net == nullptr) {
    throw std::runtime_error("cannot record guide replacement for a null net");
  }
  const auto projection = odb_trace_projection::guides(net);
  auto event = eventBase("replace_guides", source);
  event["name"] = net->getName();
  event["payload"] = json::object{{"guides", std::move(guides)}};
  attachProjection(event, projection, "replace_guides", source, net->getName());
  record(std::move(event));
}

void OdbTraceRecorder::recordAppendBTermStackWire(
    std::string_view source,
    odb::dbNet* net,
    std::string_view layer,
    int x,
    int y,
    const std::vector<std::string>& vias)
{
  requireRegularNet(net, "append_bterm_stack_wire");
  if (layer.empty()) {
    throw std::runtime_error("cannot record a block-terminal via stack without a layer");
  }
  json::array via_names;
  via_names.reserve(vias.size());
  for (const auto& via : vias) {
    if (via.empty()) {
      throw std::runtime_error("cannot record an unnamed block-terminal stack via");
    }
    via_names.emplace_back(via);
  }

  auto event = eventBase("append_bterm_stack_wire", source);
  event["object"] = static_cast<uint32_t>(net->getId());
  event["payload"] = json::object{{"layer", std::string(layer)},
                                   {"x", x},
                                   {"y", y},
                                   {"vias", std::move(via_names)}};
  event["expected"] = drt_trace::shapeValues(drt_trace::readOdbWire(net), false);
  record(std::move(event));
}

void OdbTraceRecorder::finish()
{
  if (finished_) {
    return;
  }
  if (active_ != this) {
    throw std::runtime_error("cannot finish an inactive ODB trace recorder");
  }
  json::object footer;
  footer["op"] = "end";
  footer["complete"] = true;
  footer["events"] = events_;
  file_ << json::serialize(footer) << '\n';
  file_.flush();
  if (!file_) {
    throw std::runtime_error("cannot finish ODB trace");
  }
  finished_ = true;
  active_ = nullptr;
}

void OdbTraceRecorder::record(boost::json::object event)
{
  if (active_ != this || finished_) {
    return;
  }
  file_ << boost::json::serialize(event) << '\n';
  if (!file_) {
    throw std::runtime_error("cannot write ODB trace event");
  }
  ++events_;
}

}  // namespace eccdb::tritonroute
