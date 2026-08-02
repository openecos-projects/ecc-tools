#include "GeometryBuilder.h"

#include "IdbBlockages.h"
#include "IdbBus.h"
#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbFill.h"
#include "IdbGroup.h"
#include "IdbGCellGrid.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbRegion.h"
#include "IdbRegularWire.h"
#include "IdbSlot.h"
#include "IdbSpecialNet.h"
#include "IdbSpecialWire.h"
#include "IdbTrackGrid.h"
#include "IdbViaMaster.h"
#include "IdbVias.h"

#include <algorithm>
#include <limits>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace ecc::geometry {

namespace {

constexpr LayerId kLayoutGeometryLayer = 0;
constexpr OwnerId kDerivedOwnerPayloadMask = 0x00ffffffffffffffULL;

OwnerId make_derived_owner_id(OwnerType parent_type, OwnerId parent_owner_id)
{
  return (static_cast<OwnerId>(static_cast<uint8_t>(parent_type)) << 56U)
         | (parent_owner_id & kDerivedOwnerPayloadMask);
}

Rect32 rect_from_idb(idb::IdbRect* rect)
{
  if (rect == nullptr) {
    return Rect32{};
  }

  return Rect32{rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y()};
}

Rect32 rect_from_idb(idb::IdbRect& rect)
{
  return Rect32{rect.get_low_x(), rect.get_low_y(), rect.get_high_x(), rect.get_high_y()};
}

Rect32 rect_from_delta_rect(idb::IdbRect* delta_rect, idb::IdbCoordinate<int32_t>* origin)
{
  if (delta_rect == nullptr || origin == nullptr) {
    return Rect32{};
  }

  return Rect32{origin->get_x() + delta_rect->get_low_x(), origin->get_y() + delta_rect->get_low_y(),
                origin->get_x() + delta_rect->get_high_x(), origin->get_y() + delta_rect->get_high_y()};
}

LayerId layer_id_from_idb(idb::IdbLayer* layer)
{
  if (layer == nullptr) {
    return kLayoutGeometryLayer;
  }

  const int32_t layer_id = layer->get_id();
  const LayerId layer_order = static_cast<LayerId>(layer->get_order());
  // IdbLayer IDs are allocated independently for routing and cut layers, so
  // ordinary LEF data (where ID does not exceed its global order) cannot use
  // them as geometry-wide keys. Use the global order and reserve zero for
  // derived layout geometry. Explicit archive IDs above the order are already
  // global identifiers and remain stable for backwards compatibility.
  return layer_id > static_cast<int32_t>(layer_order) ? static_cast<LayerId>(layer_id)
                                                      : layer_order + 1;
}

const char* layer_type_name(idb::IdbLayerType type)
{
  switch (type) {
    case idb::IdbLayerType::kLayerCut:
      return "cut";
    case idb::IdbLayerType::kLayerImplant:
      return "implant";
    case idb::IdbLayerType::kLayerMasterslice:
      return "masterslice";
    case idb::IdbLayerType::kLayerOverlap:
      return "overlap";
    case idb::IdbLayerType::kLayerRouting:
      return "routing";
    default:
      return "unknown";
  }
}

const char* layer_direction_name(idb::IdbLayerDirection direction)
{
  switch (direction) {
    case idb::IdbLayerDirection::kHorizontal:
      return "horizontal";
    case idb::IdbLayerDirection::kVertical:
      return "vertical";
    case idb::IdbLayerDirection::kDiag45:
      return "diag45";
    case idb::IdbLayerDirection::kDiag135:
      return "diag135";
    default:
      return "unknown";
  }
}

const char* track_direction_name(idb::IdbTrackDirection direction)
{
  switch (direction) {
    case idb::IdbTrackDirection::kDirectionX:
      return "x";
    case idb::IdbTrackDirection::kDirectionY:
      return "y";
    default:
      return "unknown";
  }
}

int32_t min_positive_spacing(idb::IdbLayerSpacingList* spacing_list)
{
  if (spacing_list == nullptr) {
    return 0;
  }

  int32_t min_spacing = 0;
  for (auto* spacing : spacing_list->get_spacing_list()) {
    if (spacing == nullptr || spacing->get_min_spacing() <= 0) {
      continue;
    }
    if (min_spacing == 0 || spacing->get_min_spacing() < min_spacing) {
      min_spacing = spacing->get_min_spacing();
    }
  }
  return min_spacing;
}

int32_t min_positive_area(idb::IdbMinEncloseAreaList* area_list)
{
  if (area_list == nullptr) {
    return 0;
  }

  int32_t min_area = 0;
  for (const auto& area : area_list->get_min_area_list()) {
    if (area._area <= 0) {
      continue;
    }
    if (min_area == 0 || area._area < min_area) {
      min_area = area._area;
    }
  }
  return min_area;
}

int32_t min_positive_cut_spacing(idb::IdbLayerCut* cut_layer)
{
  if (cut_layer == nullptr) {
    return 0;
  }

  int32_t min_spacing = 0;
  for (auto* spacing : cut_layer->get_spacings()) {
    if (spacing == nullptr || spacing->get_spacing() <= 0) {
      continue;
    }
    if (min_spacing == 0 || spacing->get_spacing() < min_spacing) {
      min_spacing = spacing->get_spacing();
    }
  }
  return min_spacing;
}

std::string enclosure_summary(idb::IdbLayerCutEnclosure* enclosure)
{
  if (enclosure == nullptr) {
    return {};
  }
  return std::to_string(enclosure->get_overhang_1()) + "," + std::to_string(enclosure->get_overhang_2());
}

void append_local_name_token(std::string& local_name, const std::string& key, const std::string& value)
{
  if (key.empty() || value.empty()) {
    return;
  }

  if (!local_name.empty()) {
    local_name += " ";
  }
  local_name += key + ":" + value;
}

std::string layer_name(idb::IdbLayer* layer)
{
  if (layer == nullptr) {
    return {};
  }
  return layer->get_name();
}

std::string layer_shape_name(idb::IdbLayerShape* layer_shape)
{
  return layer_shape == nullptr ? std::string{} : layer_name(layer_shape->get_layer());
}

std::string positive_pair_summary(int32_t x, int32_t y)
{
  if (x <= 0 && y <= 0) {
    return {};
  }
  return std::to_string(x) + "," + std::to_string(y);
}

std::string positive_size_summary(int32_t x, int32_t y)
{
  if (x <= 0 && y <= 0) {
    return {};
  }
  return std::to_string(x) + "x" + std::to_string(y);
}

std::vector<std::string> sorted_unique_names(std::vector<std::string> names)
{
  std::erase_if(names, [](const std::string& name) { return name.empty(); });
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  return names;
}

std::vector<std::string> bus_net_names(const idb::IdbBus& bus)
{
  std::vector<std::string> names;
  for (auto* net : bus.getNets()) {
    if (net != nullptr) {
      names.push_back(net->get_net_name());
    }
  }
  return sorted_unique_names(std::move(names));
}

std::string pin_member_name(idb::IdbPin* pin)
{
  if (pin == nullptr) {
    return {};
  }
  const std::string pin_name = pin->get_pin_name();
  auto* instance = pin->get_instance();
  if (instance != nullptr && !instance->get_name().empty()) {
    return instance->get_name() + "/" + pin_name;
  }
  return pin_name;
}

std::vector<std::string> bus_pin_names(const idb::IdbBus& bus)
{
  std::vector<std::string> names;
  for (auto* pin : bus.getPins()) {
    names.push_back(pin_member_name(pin));
  }
  return sorted_unique_names(std::move(names));
}

std::vector<std::string> group_instance_names(idb::IdbGroup* group)
{
  std::vector<std::string> names;
  if (group == nullptr || group->get_instance_list() == nullptr) {
    return names;
  }
  for (auto* instance : group->get_instance_list()->get_instance_list()) {
    if (instance != nullptr) {
      names.push_back(instance->get_name());
    }
  }
  return sorted_unique_names(std::move(names));
}

std::string via_local_name(idb::IdbVia* via)
{
  if (via == nullptr) {
    return {};
  }
  std::string local_name;
  if (!via->get_name().empty()) {
    append_local_name_token(local_name, "via", via->get_name());
  }

  auto* master = via->get_instance();
  if (master == nullptr) {
    return local_name;
  }

  if (local_name.empty() && !master->get_name().empty()) {
    append_local_name_token(local_name, "via", master->get_name());
  }
  append_local_name_token(local_name, "master", master->get_name());

  if (master->is_fix()) {
    append_local_name_token(local_name, "type", "fixed");
    append_local_name_token(local_name, "bottom", layer_shape_name(master->get_bottom_layer_shape()));
    append_local_name_token(local_name, "cut", layer_shape_name(master->get_cut_layer_shape()));
    append_local_name_token(local_name, "top", layer_shape_name(master->get_top_layer_shape()));
  } else if (master->is_generate()) {
    append_local_name_token(local_name, "type", "generated");
    auto* generated = master->get_master_generate();
    if (generated != nullptr) {
      append_local_name_token(local_name, "rule", generated->get_rule_name());
      append_local_name_token(local_name, "bottom", layer_name(generated->get_layer_bottom()));
      append_local_name_token(local_name, "cut", layer_name(generated->get_layer_cut()));
      append_local_name_token(local_name, "top", layer_name(generated->get_layer_top()));
      append_local_name_token(local_name, "cut_size",
                              positive_size_summary(generated->get_cut_size_x(), generated->get_cut_size_y()));
      append_local_name_token(local_name, "cut_spacing",
                              positive_pair_summary(generated->get_cut_spcing_x(), generated->get_cut_spcing_y()));
      append_local_name_token(local_name, "enclosure_bottom",
                              positive_pair_summary(generated->get_enclosure_bottom_x(), generated->get_enclosure_bottom_y()));
      append_local_name_token(local_name, "enclosure_top",
                              positive_pair_summary(generated->get_enclosure_top_x(), generated->get_enclosure_top_y()));
    }
  }

  if (master->get_cut_rows() > 0 && master->get_cut_cols() > 0) {
    append_local_name_token(local_name, "rowcol",
                            std::to_string(master->get_cut_rows()) + "x" + std::to_string(master->get_cut_cols()));
  }
  if (master->is_default()) {
    append_local_name_token(local_name, "default", "true");
  }
  return local_name;
}

std::string via_master_type_name(idb::IdbViaMaster* master)
{
  if (master == nullptr) {
    return "unknown";
  }
  if (master->is_fix()) {
    return "fixed";
  }
  if (master->is_generate()) {
    return "generated";
  }
  return "unknown";
}

GeometryViaMetadata via_metadata_from_idb(idb::IdbVia* via)
{
  GeometryViaMetadata metadata;
  if (via == nullptr) {
    return metadata;
  }

  auto* master = via->get_instance();
  metadata.name = via->get_name();
  metadata.master_name = master == nullptr ? std::string{} : master->get_name();
  if (metadata.name.empty()) {
    metadata.name = metadata.master_name;
  }
  if (master == nullptr) {
    return metadata;
  }

  metadata.via_type = via_master_type_name(master);
  metadata.is_default = master->is_default();
  metadata.rows = master->get_cut_rows() > 0 ? master->get_cut_rows() : 0;
  metadata.cols = master->get_cut_cols() > 0 ? master->get_cut_cols() : 0;

  if (master->is_fix()) {
    metadata.bottom_layer = layer_shape_name(master->get_bottom_layer_shape());
    metadata.cut_layer = layer_shape_name(master->get_cut_layer_shape());
    metadata.top_layer = layer_shape_name(master->get_top_layer_shape());
    idb::IdbRect* cut_rect = nullptr;
    if (auto* cut_shape = master->get_cut_layer_shape(); cut_shape != nullptr && !cut_shape->get_rect_list().empty()) {
      cut_rect = cut_shape->get_rect_list().front();
    } else {
      cut_rect = master->get_cut_rect();
    }
    if (cut_rect != nullptr) {
      metadata.cut_width = cut_rect->get_width();
      metadata.cut_height = cut_rect->get_height();
    }
  } else if (master->is_generate()) {
    auto* generated = master->get_master_generate();
    if (generated != nullptr) {
      metadata.rule_name = generated->get_rule_name();
      metadata.bottom_layer = layer_name(generated->get_layer_bottom());
      metadata.cut_layer = layer_name(generated->get_layer_cut());
      metadata.top_layer = layer_name(generated->get_layer_top());
      metadata.cut_width = std::max(generated->get_cut_size_x(), 0);
      metadata.cut_height = std::max(generated->get_cut_size_y(), 0);
      metadata.cut_spacing_x = std::max(generated->get_cut_spcing_x(), 0);
      metadata.cut_spacing_y = std::max(generated->get_cut_spcing_y(), 0);
      metadata.enclosure_bottom_x = std::max(generated->get_enclosure_bottom_x(), 0);
      metadata.enclosure_bottom_y = std::max(generated->get_enclosure_bottom_y(), 0);
      metadata.enclosure_top_x = std::max(generated->get_enclosure_top_x(), 0);
      metadata.enclosure_top_y = std::max(generated->get_enclosure_top_y(), 0);
      if (generated->get_cut_rows() > 0) {
        metadata.rows = generated->get_cut_rows();
      }
      if (generated->get_cut_cols() > 0) {
        metadata.cols = generated->get_cut_cols();
      }
    }
  }

  return metadata;
}

std::vector<std::string> grid_layer_names(idb::IdbTrackGrid* track_grid, idb::IdbLayer* fallback_layer)
{
  std::vector<std::string> names;
  if (track_grid == nullptr) {
    return names;
  }

  std::vector<idb::IdbLayer*> layers = track_grid->get_layer_list();
  if (layers.empty() && fallback_layer != nullptr) {
    layers.push_back(fallback_layer);
  }

  for (auto* layer : layers) {
    const std::string name = layer_name(layer);
    if (!name.empty()) {
      names.push_back(name);
    }
  }
  return sorted_unique_names(std::move(names));
}

GeometryGridMetadata track_grid_metadata_from_idb(idb::IdbTrackGrid* track_grid, idb::IdbLayer* fallback_layer,
                                                  uint32_t index)
{
  GeometryGridMetadata metadata;
  metadata.grid_type = "track";
  metadata.index = index;
  if (track_grid == nullptr || track_grid->get_track() == nullptr) {
    return metadata;
  }

  auto* track = track_grid->get_track();
  metadata.direction = track_direction_name(track->get_direction());
  metadata.start = track->get_start();
  metadata.step = track->get_pitch();
  metadata.count = track_grid->get_track_num();
  metadata.width = static_cast<int32_t>(track->get_width());
  metadata.layer_names = grid_layer_names(track_grid, fallback_layer);
  return metadata;
}

GeometryGridMetadata gcell_grid_metadata_from_idb(idb::IdbGCellGrid* gcell_grid, uint32_t index)
{
  GeometryGridMetadata metadata;
  metadata.grid_type = "gcell";
  metadata.index = index;
  metadata.width = 1;
  if (gcell_grid == nullptr) {
    return metadata;
  }

  metadata.direction = track_direction_name(gcell_grid->get_direction());
  metadata.start = gcell_grid->get_start();
  metadata.step = gcell_grid->get_space();
  metadata.count = gcell_grid->get_num() > 0 ? static_cast<uint32_t>(gcell_grid->get_num()) : 0;
  return metadata;
}

OwnerRef with_via_local_name(GeometryStore& store, idb::IdbVia* via, OwnerRef owner)
{
  const std::string local_name = via_local_name(via);
  if (!local_name.empty()) {
    owner.name_id = store.add_local_name(local_name);
  }
  return owner;
}

std::string master_local_name(idb::IdbCellMaster* master)
{
  if (master == nullptr || master->get_name().empty()) {
    return {};
  }

  std::string local_name = "master:" + master->get_name();
  if (master->get_site() != nullptr && !master->get_site()->get_name().empty()) {
    local_name += " site:" + master->get_site()->get_name();
  }
  return local_name;
}

OwnerRef with_master_local_name(GeometryStore& store, idb::IdbCellMaster* master, OwnerRef owner)
{
  const std::string local_name = master_local_name(master);
  if (!local_name.empty()) {
    owner.name_id = store.add_local_name(local_name);
  }
  return owner;
}

uint32_t routing_lef58_rule_count(idb::IdbLayerRouting* routing_layer)
{
  if (routing_layer == nullptr) {
    return 0;
  }

  uint32_t count = 0;
  count += static_cast<uint32_t>(routing_layer->get_lef58_spacing_eol_list().size());
  count += static_cast<uint32_t>(routing_layer->get_lef58_area().size());
  count += routing_layer->get_lef58_corner_fill_spacing() != nullptr ? 1U : 0U;
  count += static_cast<uint32_t>(routing_layer->get_lef58_corner_spacing_list().size());
  count += static_cast<uint32_t>(routing_layer->get_lef58_minimum_cut().size());
  count += static_cast<uint32_t>(routing_layer->get_lef58_min_step().size());
  count += routing_layer->get_lef58_spacing_notchlength() != nullptr ? 1U : 0U;
  count += routing_layer->get_lef58_spacingtable_jogtojog() != nullptr ? 1U : 0U;
  return count;
}

uint32_t cut_lef58_rule_count(idb::IdbLayerCut* cut_layer)
{
  if (cut_layer == nullptr) {
    return 0;
  }

  uint32_t count = 0;
  count += static_cast<uint32_t>(cut_layer->get_lef58_cutclass_list().size());
  count += static_cast<uint32_t>(cut_layer->get_lef58_enclosure_list().size());
  count += static_cast<uint32_t>(cut_layer->get_lef58_enclosure_edge_list().size());
  count += cut_layer->get_lef58_eol_enclosure() != nullptr ? 1U : 0U;
  count += cut_layer->get_lef58_eol_spacing() != nullptr ? 1U : 0U;
  count += static_cast<uint32_t>(cut_layer->get_lef58_spacing_table().size());
  return count;
}

const char* site_class_name(idb::IdbSiteClass site_class)
{
  switch (site_class) {
    case idb::IdbSiteClass::kCore:
      return "CORE";
    case idb::IdbSiteClass::kPad:
      return "PAD";
    case idb::IdbSiteClass::kCorner:
      return "CORNER";
    default:
      return "unknown";
  }
}

const char* site_symmetry_name(idb::IdbSymmetry symmetry)
{
  switch (symmetry) {
    case idb::IdbSymmetry::kX:
      return "X";
    case idb::IdbSymmetry::kY:
      return "Y";
    case idb::IdbSymmetry::kR90:
      return "R90";
    default:
      return "";
  }
}

std::string orient_name(idb::IdbOrient orient)
{
  if (auto* site_property = idb::IdbEnum::GetInstance()->get_site_property(); site_property != nullptr) {
    return site_property->get_orient_name(orient);
  }
  return {};
}

std::string cell_master_type_name(idb::CellMasterType type)
{
  if (auto* cell_property = idb::IdbEnum::GetInstance()->get_cell_property(); cell_property != nullptr) {
    std::string name = cell_property->get_name(type);
    if (!name.empty()) {
      return name;
    }
  }
  return "unknown";
}

std::string master_symmetry_name(idb::IdbCellMaster* master)
{
  if (master == nullptr) {
    return {};
  }

  std::string symmetry;
  const auto append = [&symmetry](const char* value) {
    if (!symmetry.empty()) {
      symmetry += ',';
    }
    symmetry += value;
  };

  if (master->is_symmetry_x()) {
    append("X");
  }
  if (master->is_symmetry_y()) {
    append("Y");
  }
  if (master->is_symmetry_R90()) {
    append("R90");
  }
  return symmetry;
}

const char* bus_type_name(idb::IdbBus::kBusType type)
{
  switch (type) {
    case idb::IdbBus::kBusNet:
      return "net";
    case idb::IdbBus::kBusInstancePin:
      return "instance_pin";
    case idb::IdbBus::kBusIo:
      return "io";
    default:
      return "unknown";
  }
}

const char* net_kind_name(idb::IdbNet* net)
{
  if (net == nullptr) {
    return "other";
  }
  if (net->is_clock()) {
    return "clock";
  }
  if (net->is_signal() || net->get_connect_type() == idb::IdbConnectType::kNone) {
    return "signal";
  }
  if (net->is_power()) {
    return "power";
  }
  if (net->is_ground()) {
    return "ground";
  }
  return "other";
}

GeometryConnectivityMetadata connectivity_metadata_from_pin(idb::IdbNet* net, idb::IdbPin* pin,
                                                            const char* endpoint_type)
{
  GeometryConnectivityMetadata metadata;
  metadata.net_name = net == nullptr ? std::string{} : net->get_net_name();
  metadata.net_kind = net_kind_name(net);
  metadata.endpoint_type = endpoint_type == nullptr ? "unknown" : endpoint_type;
  metadata.pin_name = pin == nullptr ? std::string{} : pin->get_pin_name();
  auto* instance = pin == nullptr ? nullptr : pin->get_instance();
  if (instance != nullptr) {
    metadata.instance_name = instance->get_name();
    if (instance->get_cell_master() != nullptr) {
      metadata.master_name = instance->get_cell_master()->get_name();
    }
  }
  return metadata;
}

GeometryLayerMetadata layer_metadata_from_idb(idb::IdbLayer* layer)
{
  GeometryLayerMetadata metadata;
  if (layer == nullptr) {
    return metadata;
  }

  metadata.layer_id = layer_id_from_idb(layer);
  metadata.order = layer->get_order();
  metadata.name = layer->get_name();
  metadata.type = layer_type_name(layer->get_type());

  if (auto* routing_layer = dynamic_cast<idb::IdbLayerRouting*>(layer); routing_layer != nullptr) {
    metadata.direction = layer_direction_name(routing_layer->get_direction());
    metadata.width = routing_layer->get_width();
    metadata.pitch_x = routing_layer->get_pitch_x();
    metadata.pitch_y = routing_layer->get_pitch_y();
    metadata.min_spacing = min_positive_spacing(routing_layer->get_spacing_list());
    metadata.min_area = routing_layer->get_area();
    if (metadata.min_area <= 0) {
      metadata.min_area = min_positive_area(routing_layer->get_min_enclose_area_list());
    }
    if (auto min_step = routing_layer->get_min_step(); min_step != nullptr) {
      metadata.min_step = min_step->get_min_step_length();
    }
    if (metadata.min_step <= 0 && !routing_layer->get_lef58_min_step().empty()
        && routing_layer->get_lef58_min_step().front() != nullptr) {
      metadata.min_step = routing_layer->get_lef58_min_step().front()->get_min_step_length();
    }
    metadata.lef58_rule_count = routing_lef58_rule_count(routing_layer);
  } else if (auto* cut_layer = dynamic_cast<idb::IdbLayerCut*>(layer); cut_layer != nullptr) {
    metadata.width = cut_layer->get_width();
    metadata.cut_spacing = min_positive_cut_spacing(cut_layer);
    metadata.enclosure_below = enclosure_summary(cut_layer->get_enclosure_below());
    metadata.enclosure_above = enclosure_summary(cut_layer->get_enclosure_above());
    metadata.lef58_rule_count = cut_lef58_rule_count(cut_layer);
  }

  return metadata;
}

bool is_non_empty(Rect32 rect)
{
  rect = normalize(rect);
  return rect.lx != rect.hx || rect.ly != rect.hy;
}

bool is_same_rect(Rect32 lhs, Rect32 rhs)
{
  lhs = normalize(lhs);
  rhs = normalize(rhs);
  return lhs.lx == rhs.lx && lhs.ly == rhs.ly && lhs.hx == rhs.hx && lhs.hy == rhs.hy;
}

ShapeId emit_rect_if_present(GeometryStore& store, LayerId layer_id, Rect32 rect, OwnerRef owner)
{
  if (!is_non_empty(rect)) {
    return 0;
  }

  return store.add_rect(layer_id, rect, owner);
}

ShapeId emit_layout_rect_if_present(GeometryStore& store, Rect32 rect, OwnerRef owner)
{
  return emit_rect_if_present(store, kLayoutGeometryLayer, rect, owner);
}

Rect32 grid_reference_bounds(idb::IdbLayout& layout)
{
  if (auto* die = layout.get_die(); die != nullptr) {
    const Rect32 bounds = rect_from_idb(die->get_bounding_box());
    if (is_non_empty(bounds)) {
      return normalize(bounds);
    }
  }

  if (layout.get_rows() != nullptr && layout.get_rows()->get_row_num() > 0) {
    auto* core = layout.get_core();
    const Rect32 bounds = rect_from_idb(core->get_bounding_box());
    if (is_non_empty(bounds)) {
      return normalize(bounds);
    }
  }

  return {};
}

bool coordinate_to_i32(int64_t coordinate, int32_t& out)
{
  if (coordinate < std::numeric_limits<int32_t>::min() || coordinate > std::numeric_limits<int32_t>::max()) {
    return false;
  }
  out = static_cast<int32_t>(coordinate);
  return true;
}

bool make_grid_line(idb::IdbTrackDirection direction, int32_t coordinate, Rect32 bounds, int32_t width, LinePayload& line)
{
  bounds = normalize(bounds);
  if (!is_non_empty(bounds)) {
    return false;
  }

  line.width = width > 0 ? width : 1;
  line.flags = 0;

  switch (direction) {
    case idb::IdbTrackDirection::kDirectionX:
      line.begin = Point32{coordinate, bounds.ly};
      line.end = Point32{coordinate, bounds.hy};
      return true;
    case idb::IdbTrackDirection::kDirectionY:
      line.begin = Point32{bounds.lx, coordinate};
      line.end = Point32{bounds.hx, coordinate};
      return true;
    default:
      return false;
  }
}

uint64_t emit_track_grid_lines(GeometryStore& store, idb::IdbTrackGrid* track_grid, Rect32 bounds, OwnerId owner_id,
                               idb::IdbLayer* fallback_layer)
{
  if (track_grid == nullptr || track_grid->get_track() == nullptr || track_grid->get_track_num() == 0) {
    return 0;
  }

  idb::IdbTrack* track = track_grid->get_track();
  const uint32_t pitch = track->get_pitch();
  if (pitch == 0) {
    return 0;
  }

  std::vector<idb::IdbLayer*> layers = track_grid->get_layer_list();
  if (layers.empty() && fallback_layer != nullptr) {
    layers.push_back(fallback_layer);
  }
  if (layers.empty()) {
    layers.push_back(nullptr);
  }

  uint64_t shape_count = 0;
  uint32_t layer_index = 0;
  for (auto* layer : layers) {
    const LayerId layer_id = layer_id_from_idb(layer);
    for (uint32_t track_index = 0; track_index < track_grid->get_track_num(); ++track_index) {
      int32_t coordinate = 0;
      if (!coordinate_to_i32(static_cast<int64_t>(track->get_start()) + static_cast<int64_t>(track_index) * pitch, coordinate)) {
        continue;
      }

      LinePayload line;
      if (!make_grid_line(track->get_direction(), coordinate, bounds, static_cast<int32_t>(track->get_width()), line)) {
        continue;
      }

      OwnerRef owner;
      owner.type = OwnerType::kTrackGrid;
      owner.owner_id = owner_id;
      owner.path0 = layer_index;
      owner.path1 = track_index;
      owner.path2 = static_cast<uint32_t>(track->get_direction());

      if (store.add_line(layer_id, line, owner) != 0) {
        ++shape_count;
      }
    }

    ++layer_index;
  }

  return shape_count;
}

uint64_t emit_gcell_grid_lines(GeometryStore& store, idb::IdbGCellGrid* gcell_grid, Rect32 bounds, OwnerId owner_id)
{
  if (gcell_grid == nullptr || gcell_grid->get_num() <= 0 || gcell_grid->get_space() <= 0) {
    return 0;
  }

  uint64_t shape_count = 0;
  for (int32_t line_index = 0; line_index < gcell_grid->get_num(); ++line_index) {
    int32_t coordinate = 0;
    if (!coordinate_to_i32(static_cast<int64_t>(gcell_grid->get_start())
                               + static_cast<int64_t>(line_index) * gcell_grid->get_space(),
                           coordinate)) {
      continue;
    }

    LinePayload line;
    if (!make_grid_line(gcell_grid->get_direction(), coordinate, bounds, 1, line)) {
      continue;
    }

    OwnerRef owner;
    owner.type = OwnerType::kGCellGrid;
    owner.owner_id = owner_id;
    owner.path0 = static_cast<uint32_t>(line_index);
    owner.path1 = static_cast<uint32_t>(gcell_grid->get_direction());

    if (store.add_line(kLayoutGeometryLayer, line, owner) != 0) {
      ++shape_count;
    }
  }

  return shape_count;
}

uint64_t emit_layer_shape_rects(GeometryStore& store, idb::IdbLayerShape& layer_shape, OwnerRef owner,
                                uint32_t path3_prefix)
{
  if (layer_shape.get_layer() == nullptr) {
    return 0;
  }

  uint64_t shape_count = 0;
  uint32_t rect_index = 0;
  for (auto* rect : layer_shape.get_rect_list()) {
    OwnerRef rect_owner = owner;
    rect_owner.path3 = path3_prefix + rect_index++;
    if (emit_rect_if_present(store, layer_id_from_idb(layer_shape.get_layer()), rect_from_idb(rect), rect_owner) != 0) {
      ++shape_count;
    }
  }

  return shape_count;
}

uint64_t emit_via_shapes(GeometryStore& store, idb::IdbVia* via, OwnerRef owner)
{
  if (via == nullptr || via->get_instance() == nullptr) {
    return 0;
  }

  owner = with_via_local_name(store, via, owner);
  idb::IdbLayerShape bottom_shape = via->get_bottom_layer_shape();
  idb::IdbLayerShape cut_shape = via->get_cut_layer_shape();
  idb::IdbLayerShape top_shape = via->get_top_layer_shape();
  return emit_layer_shape_rects(store, bottom_shape, owner, 0U)
         + emit_layer_shape_rects(store, cut_shape, owner, 1U << 30U)
         + emit_layer_shape_rects(store, top_shape, owner, 2U << 30U);
}

uint64_t emit_via_shapes_at(GeometryStore& store, idb::IdbVia* via, idb::IdbCoordinate<int32_t>* origin, OwnerRef owner)
{
  if (via == nullptr || origin == nullptr || via->get_instance() == nullptr) {
    return 0;
  }

  owner = with_via_local_name(store, via, owner);
  idb::IdbLayerShape bottom_shape;
  via->get_instance()->get_bottom_layer_shape()->clone(bottom_shape);
  bottom_shape.moveToLocation(origin);
  idb::IdbLayerShape cut_shape;
  via->get_instance()->get_cut_layer_shape()->clone(cut_shape);
  cut_shape.moveToLocation(origin);
  idb::IdbLayerShape top_shape;
  via->get_instance()->get_top_layer_shape()->clone(top_shape);
  top_shape.moveToLocation(origin);
  return emit_layer_shape_rects(store, bottom_shape, owner, 0U)
         + emit_layer_shape_rects(store, cut_shape, owner, 1U << 30U)
         + emit_layer_shape_rects(store, top_shape, owner, 2U << 30U);
}

ShapeId find_shape_by_owner_path(const GeometryStore& store, OwnerType type, OwnerId owner_id, uint32_t path0, uint32_t path1,
                                 uint32_t path2, uint32_t path3)
{
  for (const ShapeId shape_id : store.query_owner(type, owner_id)) {
    const OwnerRef owner = store.owner_of(shape_id);
    if (owner.path0 == path0 && owner.path1 == path1 && owner.path2 == path2 && owner.path3 == path3) {
      return shape_id;
    }
  }

  return 0;
}

bool resolve_regular_net_owner_id(idb::IdbDesign& design, idb::IdbNet& net, OwnerId& owner_id)
{
  auto* net_list = design.get_net_list();
  if (net_list == nullptr) {
    return false;
  }

  uint32_t net_index = 0;
  for (auto* candidate : net_list->get_net_list()) {
    if (candidate == &net) {
      owner_id = net.get_id() != 0 ? net.get_id() : net_index;
      return true;
    }
    ++net_index;
  }

  return false;
}

bool resolve_special_net_owner_id(idb::IdbDesign& design, idb::IdbSpecialNet& net, OwnerId& owner_id)
{
  auto* net_list = design.get_special_net_list();
  if (net_list == nullptr) {
    return false;
  }

  uint32_t net_index = 0;
  for (auto* candidate : net_list->get_net_list()) {
    if (candidate == &net) {
      owner_id = net_index;
      return true;
    }
    ++net_index;
  }

  return false;
}

bool resolve_blockage_owner_id(idb::IdbDesign& design, idb::IdbBlockage& blockage, OwnerId& owner_id)
{
  auto* blockage_list = design.get_blockage_list();
  if (blockage_list == nullptr) {
    return false;
  }

  uint32_t blockage_index = 0;
  for (auto* candidate : blockage_list->get_blockage_list()) {
    if (candidate == &blockage) {
      owner_id = blockage_index;
      return true;
    }
    ++blockage_index;
  }

  return false;
}

bool resolve_region_owner_id(idb::IdbDesign& design, idb::IdbRegion& region, OwnerId& owner_id)
{
  auto* region_list = design.get_region_list();
  if (region_list == nullptr) {
    return false;
  }

  uint32_t region_index = 0;
  for (auto* candidate : region_list->get_region_list()) {
    if (candidate == &region) {
      owner_id = region_index;
      return true;
    }
    ++region_index;
  }

  return false;
}

bool resolve_slot_owner_id(idb::IdbDesign& design, idb::IdbSlot& slot, OwnerId& owner_id)
{
  auto* slot_list = design.get_slot_list();
  if (slot_list == nullptr) {
    return false;
  }

  uint32_t slot_index = 0;
  for (auto* candidate : slot_list->get_slot_list()) {
    if (candidate == &slot) {
      owner_id = slot_index;
      return true;
    }
    ++slot_index;
  }

  return false;
}

bool resolve_fill_owner_id(idb::IdbDesign& design, idb::IdbFill& fill, OwnerId& owner_id)
{
  auto* fill_list = design.get_fill_list();
  if (fill_list == nullptr) {
    return false;
  }

  uint32_t fill_index = 0;
  for (auto* candidate : fill_list->get_fill_list()) {
    if (candidate == &fill) {
      owner_id = fill_index;
      return true;
    }
    ++fill_index;
  }

  return false;
}

bool resolve_io_pin_path(idb::IdbDesign& design, idb::IdbPin& pin, uint32_t& pin_index)
{
  auto* pin_list = design.get_io_pin_list();
  if (pin_list == nullptr) {
    return false;
  }

  uint32_t index = 0;
  for (auto* candidate : pin_list->get_pin_list()) {
    if (candidate == &pin) {
      pin_index = index;
      return true;
    }
    ++index;
  }

  return false;
}

void delete_unseen_owner_shapes(GeometryStore& store, OwnerType type, OwnerId owner_id,
                                const std::unordered_set<ShapeId>& seen_shape_ids, GeometrySyncResult& result)
{
  for (const ShapeId shape_id : store.query_owner(type, owner_id)) {
    if (seen_shape_ids.contains(shape_id)) {
      continue;
    }
    if (store.delete_shape(shape_id)) {
      ++result.deleted_shape_count;
    } else {
      ++result.missing_shape_count;
    }
  }
}

std::vector<ShapeId> collect_alive_owner_type_shapes(const GeometryStore& store, OwnerType type)
{
  std::vector<ShapeId> shape_ids;
  for (const ShapeRecord& record : store.records()) {
    if (record.id == 0 || record.state != ShapeState::kAlive) {
      continue;
    }
    if (store.owner_of(record.id).type == type) {
      shape_ids.push_back(record.id);
    }
  }
  return shape_ids;
}

void delete_owner_type_shapes(GeometryStore& store, OwnerType type, GeometrySyncResult& result)
{
  for (const ShapeId shape_id : collect_alive_owner_type_shapes(store, type)) {
    if (store.delete_shape(shape_id)) {
      ++result.deleted_shape_count;
    } else {
      ++result.missing_shape_count;
    }
  }
}

void delete_unseen_owner_type_shapes(GeometryStore& store, OwnerType type, const std::unordered_set<ShapeId>& seen_shape_ids,
                                     GeometrySyncResult& result)
{
  for (const ShapeId shape_id : collect_alive_owner_type_shapes(store, type)) {
    if (seen_shape_ids.contains(shape_id)) {
      continue;
    }
    if (store.delete_shape(shape_id)) {
      ++result.deleted_shape_count;
    } else {
      ++result.missing_shape_count;
    }
  }
}

void reconcile_rect_shape(GeometryStore& store, OwnerType type, OwnerId owner_id, LayerId layer_id, Rect32 bbox, OwnerRef owner,
                          std::unordered_set<ShapeId>& seen_shape_ids, GeometrySyncResult& result)
{
  bbox = normalize(bbox);
  const ShapeId shape_id = find_shape_by_owner_path(store, type, owner_id, owner.path0, owner.path1, owner.path2, owner.path3);
  if (shape_id == 0) {
    const ShapeId added_shape_id = store.add_rect(layer_id, bbox, owner);
    if (added_shape_id != 0) {
      seen_shape_ids.insert(added_shape_id);
      ++result.added_shape_count;
    } else {
      ++result.missing_shape_count;
    }
    return;
  }

  seen_shape_ids.insert(shape_id);

  const ShapeRecord* record = store.find_shape(shape_id);
  if (record == nullptr || record->state != ShapeState::kAlive || record->kind != ShapeKind::kRect) {
    ++result.missing_shape_count;
    return;
  }

  if (record->layer_id != layer_id) {
    const bool deleted = store.delete_shape(shape_id);
    const ShapeId added_shape_id = deleted ? store.add_rect(layer_id, bbox, owner) : 0;
    if (deleted && added_shape_id != 0) {
      seen_shape_ids.insert(added_shape_id);
      ++result.deleted_shape_count;
      ++result.added_shape_count;
    } else {
      ++result.missing_shape_count;
    }
    return;
  }

  if (is_same_rect(record->bbox, bbox)) {
    return;
  }

  if (store.update_rect(shape_id, bbox)) {
    ++result.updated_shape_count;
  } else {
    ++result.missing_shape_count;
  }
}

void reconcile_layer_shape_rects(GeometryStore& store, idb::IdbLayerShape& layer_shape, OwnerRef owner,
                                 uint32_t path3_prefix, std::unordered_set<ShapeId>& seen_shape_ids,
                                 GeometrySyncResult& result)
{
  if (layer_shape.get_layer() == nullptr) {
    return;
  }

  uint32_t rect_index = 0;
  for (auto* rect : layer_shape.get_rect_list()) {
    OwnerRef rect_owner = owner;
    rect_owner.path3 = path3_prefix + rect_index++;
    const Rect32 bbox = rect_from_idb(rect);
    if (is_non_empty(bbox)) {
      reconcile_rect_shape(store, rect_owner.type, rect_owner.owner_id, layer_id_from_idb(layer_shape.get_layer()), bbox, rect_owner,
                           seen_shape_ids, result);
    }
  }
}

void reconcile_via_shapes(GeometryStore& store, idb::IdbVia* via, OwnerRef owner,
                          std::unordered_set<ShapeId>& seen_shape_ids, GeometrySyncResult& result)
{
  if (via == nullptr || via->get_instance() == nullptr) {
    return;
  }

  owner = with_via_local_name(store, via, owner);
  idb::IdbLayerShape bottom_shape = via->get_bottom_layer_shape();
  idb::IdbLayerShape cut_shape = via->get_cut_layer_shape();
  idb::IdbLayerShape top_shape = via->get_top_layer_shape();
  reconcile_layer_shape_rects(store, bottom_shape, owner, 0U, seen_shape_ids, result);
  reconcile_layer_shape_rects(store, cut_shape, owner, 1U << 30U, seen_shape_ids, result);
  reconcile_layer_shape_rects(store, top_shape, owner, 2U << 30U, seen_shape_ids, result);
}

void reconcile_via_shapes_at(GeometryStore& store, idb::IdbVia* via, idb::IdbCoordinate<int32_t>* origin, OwnerRef owner,
                             std::unordered_set<ShapeId>& seen_shape_ids, GeometrySyncResult& result)
{
  if (via == nullptr || origin == nullptr || via->get_instance() == nullptr) {
    return;
  }

  owner = with_via_local_name(store, via, owner);
  idb::IdbLayerShape bottom_shape;
  via->get_instance()->get_bottom_layer_shape()->clone(bottom_shape);
  bottom_shape.moveToLocation(origin);
  idb::IdbLayerShape cut_shape;
  via->get_instance()->get_cut_layer_shape()->clone(cut_shape);
  cut_shape.moveToLocation(origin);
  idb::IdbLayerShape top_shape;
  via->get_instance()->get_top_layer_shape()->clone(top_shape);
  top_shape.moveToLocation(origin);
  reconcile_layer_shape_rects(store, bottom_shape, owner, 0U, seen_shape_ids, result);
  reconcile_layer_shape_rects(store, cut_shape, owner, 1U << 30U, seen_shape_ids, result);
  reconcile_layer_shape_rects(store, top_shape, owner, 2U << 30U, seen_shape_ids, result);
}

Rect32 rect_from_regular_segment(idb::IdbRegularWireSegment* segment)
{
  if (segment == nullptr) {
    return Rect32{};
  }

  if (segment->is_rect()) {
    return rect_from_delta_rect(segment->get_delta_rect(), segment->get_point_start());
  }

  if (segment->is_wire() && segment->get_layer() != nullptr) {
    idb::IdbRect rect = segment->get_segment_rect();
    return rect_from_idb(rect);
  }

  return Rect32{};
}

Rect32 rect_from_special_segment(idb::IdbSpecialWireSegment* segment)
{
  if (segment == nullptr) {
    return Rect32{};
  }

  if (segment->is_rect()) {
    return rect_from_idb(segment->get_delta_rect());
  }

  if (is_non_empty(rect_from_idb(segment->get_bounding_box()))) {
    return rect_from_idb(segment->get_bounding_box());
  }

  if (segment->get_point_num() < 2 || segment->get_route_width() < 0) {
    return Rect32{};
  }

  idb::IdbCoordinate<int32_t>* begin = segment->get_point_start();
  idb::IdbCoordinate<int32_t>* end = segment->get_point_second();
  if (begin == nullptr || end == nullptr) {
    return Rect32{};
  }

  const int32_t half_width = segment->get_route_width() / 2;
  if (begin->get_y() == end->get_y()) {
    return Rect32{std::min(begin->get_x(), end->get_x()), begin->get_y() - half_width,
                  std::max(begin->get_x(), end->get_x()), begin->get_y() + segment->get_route_width() - half_width};
  }

  return Rect32{begin->get_x() - half_width, std::min(begin->get_y(), end->get_y()),
                begin->get_x() + segment->get_route_width() - half_width, std::max(begin->get_y(), end->get_y())};
}

struct PinPortShapeCounts
{
  uint64_t port_shape_count = 0;
  uint64_t via_shape_count = 0;
};

OwnerId pin_owner_id_from_path(idb::IdbPin* pin, uint32_t path0, uint32_t path1)
{
  if (pin == nullptr) {
    return 0;
  }

  return pin->get_id() != 0 ? pin->get_id() : (static_cast<OwnerId>(path0) << 32U | path1);
}

PinPortShapeCounts emit_pin_port_shapes(GeometryStore& store, idb::IdbPin* pin, OwnerType pin_owner_type, uint32_t path0,
                                        uint32_t path1, idb::IdbCellMaster* master = nullptr)
{
  if (pin == nullptr) {
    return {};
  }

  const OwnerId pin_owner_id = pin_owner_id_from_path(pin, path0, path1);
  store.add_owner_name(pin_owner_type, pin_owner_id, pin->get_pin_name());
  const OwnerId via_owner_id = make_derived_owner_id(pin_owner_type, pin_owner_id);
  store.add_owner_name(OwnerType::kVia, via_owner_id, pin->get_pin_name());

  PinPortShapeCounts counts;
  uint32_t layer_shape_index = 0;
  for (auto* layer_shape : pin->get_port_box_list()) {
    if (layer_shape == nullptr) {
      ++layer_shape_index;
      continue;
    }

    uint32_t rect_index = 0;
    for (auto* rect : layer_shape->get_rect_list()) {
      OwnerRef owner;
      owner.type = pin_owner_type;
      owner.owner_id = pin_owner_id;
      owner.path0 = path0;
      owner.path1 = path1;
      owner.path2 = layer_shape_index;
      owner.path3 = rect_index++;
      owner = with_master_local_name(store, master, owner);

      if (emit_rect_if_present(store, layer_id_from_idb(layer_shape->get_layer()), rect_from_idb(rect), owner) != 0) {
        ++counts.port_shape_count;
      }
    }

    ++layer_shape_index;
  }

  uint32_t via_index = 0;
  for (auto* via : pin->get_via_list()) {
    OwnerRef owner;
    owner.type = OwnerType::kVia;
    owner.owner_id = via_owner_id;
    owner.path0 = path0;
    owner.path1 = path1;
    owner.path2 = via_index++;
    counts.via_shape_count += emit_via_shapes(store, via, owner);
  }

  return counts;
}

void reconcile_pin_port_shapes(GeometryStore& store, idb::IdbPin* pin, OwnerType pin_owner_type, uint32_t path0,
                               uint32_t path1, idb::IdbCellMaster* master,
                               std::unordered_set<ShapeId>& seen_pin_shape_ids,
                               std::unordered_set<ShapeId>& seen_via_shape_ids, GeometrySyncResult& result)
{
  if (pin == nullptr) {
    return;
  }

  const OwnerId pin_owner_id = pin_owner_id_from_path(pin, path0, path1);
  uint32_t layer_shape_index = 0;
  for (auto* layer_shape : pin->get_port_box_list()) {
    if (layer_shape == nullptr) {
      ++layer_shape_index;
      continue;
    }

    uint32_t rect_index = 0;
    for (auto* rect : layer_shape->get_rect_list()) {
      OwnerRef owner;
      owner.type = pin_owner_type;
      owner.owner_id = pin_owner_id;
      owner.path0 = path0;
      owner.path1 = path1;
      owner.path2 = layer_shape_index;
      owner.path3 = rect_index++;
      owner = with_master_local_name(store, master, owner);
      reconcile_rect_shape(store, owner.type, owner.owner_id, layer_id_from_idb(layer_shape->get_layer()), rect_from_idb(rect),
                           owner, seen_pin_shape_ids, result);
    }

    ++layer_shape_index;
  }

  const OwnerId via_owner_id = make_derived_owner_id(pin_owner_type, pin_owner_id);
  uint32_t via_index = 0;
  for (auto* via : pin->get_via_list()) {
    OwnerRef owner;
    owner.type = OwnerType::kVia;
    owner.owner_id = via_owner_id;
    owner.path0 = path0;
    owner.path1 = path1;
    owner.path2 = via_index++;
    reconcile_via_shapes(store, via, owner, seen_via_shape_ids, result);
  }
}

}  // namespace

GeometryBuildResult GeometryBuilder::rebuild_from_design(idb::IdbDesign& design, idb::IdbLayout& layout, GeometryStore& store) const
{
  store.clear_preserving_shape_ids();

  GeometryBuildResult result;

  if (auto* die = layout.get_die(); die != nullptr) {
    OwnerRef owner;
    owner.type = OwnerType::kDie;
    owner.owner_id = die->get_id();
    if (emit_layout_rect_if_present(store, rect_from_idb(die->get_bounding_box()), owner) != 0) {
      ++result.die_shape_count;
    }
  }

  if (layout.get_rows() != nullptr && layout.get_rows()->get_row_num() > 0) {
    auto* core = layout.get_core();
    OwnerRef owner;
    owner.type = OwnerType::kCore;
    owner.owner_id = core->get_id();
    if (emit_layout_rect_if_present(store, rect_from_idb(core->get_bounding_box()), owner) != 0) {
      ++result.core_shape_count;
    }
  }

  if (auto* rows = layout.get_rows(); rows != nullptr) {
    uint32_t row_index = 0;
    for (auto* row : rows->get_row_list()) {
      if (row == nullptr) {
        ++row_index;
        continue;
      }

      OwnerRef owner;
      owner.type = OwnerType::kRow;
      owner.owner_id = row->get_id();
      owner.path0 = row_index++;
      if (emit_layout_rect_if_present(store, rect_from_idb(row->get_bounding_box()), owner) != 0) {
        ++result.row_shape_count;
      }
    }
  }

  const Rect32 grid_bounds = grid_reference_bounds(layout);
  std::unordered_set<idb::IdbTrackGrid*> emitted_track_grids;
  OwnerId track_grid_owner_id = 0;
  if (auto* track_grids = layout.get_track_grid_list(); track_grids != nullptr) {
    for (auto* track_grid : track_grids->get_track_grid_list()) {
      if (track_grid == nullptr || !emitted_track_grids.insert(track_grid).second) {
        continue;
      }
      result.track_grid_shape_count += emit_track_grid_lines(store, track_grid, grid_bounds, track_grid_owner_id++, nullptr);
    }
  }

  if (auto* layers = layout.get_layers(); layers != nullptr) {
    for (auto* layer : layers->get_layers()) {
      auto* routing_layer = dynamic_cast<idb::IdbLayerRouting*>(layer);
      if (routing_layer == nullptr) {
        continue;
      }

      for (auto* track_grid : routing_layer->get_track_grid_list()) {
        if (track_grid == nullptr || !emitted_track_grids.insert(track_grid).second) {
          continue;
        }
        result.track_grid_shape_count += emit_track_grid_lines(store, track_grid, grid_bounds, track_grid_owner_id++, routing_layer);
      }
    }
  }

  if (auto* gcell_grids = layout.get_gcell_grid_list(); gcell_grids != nullptr) {
    OwnerId gcell_grid_owner_id = 0;
    for (auto* gcell_grid : gcell_grids->get_gcell_grid_list()) {
      result.gcell_grid_shape_count += emit_gcell_grid_lines(store, gcell_grid, grid_bounds, gcell_grid_owner_id++);
    }
  }

  if (auto* instances = design.get_instance_list(); instances != nullptr) {
    uint32_t instance_index = 0;
    for (auto* instance : instances->get_instance_list()) {
      if (instance == nullptr) {
        ++instance_index;
        continue;
      }

      if (!instance->is_fixed() && !instance->is_cover() && !instance->is_placed()) {
        ++instance_index;
        continue;
      }

      const OwnerId instance_owner_id = instance->get_id() != 0 ? instance->get_id() : instance_index;
      OwnerRef owner;
      owner.type = OwnerType::kInstanceBBox;
      owner.owner_id = instance_owner_id;
      owner.path0 = instance_index;
      store.add_owner_name(owner.type, owner.owner_id, instance->get_name());
      owner = with_master_local_name(store, instance->get_cell_master(), owner);
      if (emit_layout_rect_if_present(store, rect_from_idb(instance->get_bounding_box()), owner) != 0) {
        ++result.instance_shape_count;
      }

      if (auto* halo = instance->get_halo(); halo != nullptr) {
        instance->set_halo_coodinate();

        OwnerRef halo_owner;
        halo_owner.type = OwnerType::kInstanceHalo;
        halo_owner.owner_id = instance_owner_id;
        halo_owner.path0 = instance_index;
        store.add_owner_name(halo_owner.type, halo_owner.owner_id, instance->get_name());
        halo_owner = with_master_local_name(store, instance->get_cell_master(), halo_owner);
        if (emit_layout_rect_if_present(store, rect_from_idb(halo->get_bounding_box()), halo_owner) != 0) {
          ++result.instance_halo_shape_count;
        }
      }

      if (auto* pins = instance->get_pin_list(); pins != nullptr) {
        uint32_t pin_index = 0;
        for (auto* pin : pins->get_pin_list()) {
          const PinPortShapeCounts pin_counts =
              emit_pin_port_shapes(store, pin, OwnerType::kInstancePinPortShape, instance_index, pin_index++,
                                   instance->get_cell_master());
          result.pin_shape_count += pin_counts.port_shape_count;
          result.via_shape_count += pin_counts.via_shape_count;
        }
      }

      if (instance->get_cell_master() != nullptr) {
        store.add_owner_name(OwnerType::kObs, instance_owner_id, instance->get_name());
        instance->set_obs_box_list();
        uint32_t obs_layer_index = 0;
        for (auto* obs_shape : instance->get_obs_box_list()) {
          if (obs_shape == nullptr) {
            ++obs_layer_index;
            continue;
          }

          uint32_t rect_index = 0;
          for (auto* rect : obs_shape->get_rect_list()) {
            OwnerRef obs_owner;
            obs_owner.type = OwnerType::kObs;
            obs_owner.owner_id = instance_owner_id;
            obs_owner.path0 = instance_index;
            obs_owner.path1 = obs_layer_index;
            obs_owner.path2 = rect_index++;
            obs_owner = with_master_local_name(store, instance->get_cell_master(), obs_owner);

            if (emit_rect_if_present(store, layer_id_from_idb(obs_shape->get_layer()), rect_from_idb(rect), obs_owner) != 0) {
              ++result.obs_shape_count;
            }
          }

          ++obs_layer_index;
        }
      }

      ++instance_index;
    }
  }

  if (auto* io_pins = design.get_io_pin_list(); io_pins != nullptr) {
    uint32_t pin_index = 0;
    for (auto* pin : io_pins->get_pin_list()) {
      const PinPortShapeCounts pin_counts = emit_pin_port_shapes(store, pin, OwnerType::kIoPinPortShape, 0, pin_index++);
      result.pin_shape_count += pin_counts.port_shape_count;
      result.via_shape_count += pin_counts.via_shape_count;
    }
  }

  if (auto* nets = design.get_net_list(); nets != nullptr) {
    uint32_t net_index = 0;
    for (auto* net : nets->get_net_list()) {
      if (net == nullptr) {
        ++net_index;
        continue;
      }

      const OwnerId net_owner_id = net->get_id() != 0 ? net->get_id() : net_index;
      store.add_owner_name(OwnerType::kNetWireSegment, net_owner_id, net->get_net_name());
      const OwnerId via_owner_id = make_derived_owner_id(OwnerType::kNetWireSegment, net_owner_id);
      store.add_owner_name(OwnerType::kVia, via_owner_id, net->get_net_name());

      uint32_t wire_index = 0;
      for (auto* wire : net->get_wire_list()->get_wire_list()) {
        if (wire == nullptr) {
          ++wire_index;
          continue;
        }

        uint32_t segment_index = 0;
        for (auto* segment : wire->get_segment_list()) {
          const uint32_t current_segment_index = segment_index++;
          OwnerRef owner;
          owner.type = OwnerType::kNetWireSegment;
          owner.owner_id = net_owner_id;
          owner.path0 = wire_index;
          owner.path1 = current_segment_index;

          if (emit_rect_if_present(store, layer_id_from_idb(segment == nullptr ? nullptr : segment->get_layer()),
                                   rect_from_regular_segment(segment), owner) != 0) {
            ++result.net_wire_shape_count;
          }

          if (segment != nullptr) {
            uint32_t via_index = 0;
            for (auto* via : segment->get_via_list()) {
              OwnerRef via_owner;
              via_owner.type = OwnerType::kVia;
              via_owner.owner_id = via_owner_id;
              via_owner.path0 = wire_index;
              via_owner.path1 = current_segment_index;
              via_owner.path2 = via_index++;
              result.via_shape_count += emit_via_shapes(store, via, via_owner);
            }
          }
        }

        ++wire_index;
      }

      ++net_index;
    }
  }

  if (auto* special_nets = design.get_special_net_list(); special_nets != nullptr) {
    uint32_t net_index = 0;
    for (auto* net : special_nets->get_net_list()) {
      if (net == nullptr) {
        ++net_index;
        continue;
      }

      const OwnerId net_owner_id = net_index;
      store.add_owner_name(OwnerType::kSpecialWireSegment, net_owner_id, net->get_net_name());
      const OwnerId via_owner_id = make_derived_owner_id(OwnerType::kSpecialWireSegment, net_owner_id);
      store.add_owner_name(OwnerType::kVia, via_owner_id, net->get_net_name());

      uint32_t wire_index = 0;
      for (auto* wire : net->get_wire_list()->get_wire_list()) {
        if (wire == nullptr) {
          ++wire_index;
          continue;
        }

        uint32_t segment_index = 0;
        for (auto* segment : wire->get_segment_list()) {
          const uint32_t current_segment_index = segment_index++;
          OwnerRef owner;
          owner.type = OwnerType::kSpecialWireSegment;
          owner.owner_id = net_owner_id;
          owner.path0 = wire_index;
          owner.path1 = current_segment_index;

          if (segment != nullptr && !segment->is_via()
              && emit_rect_if_present(store, layer_id_from_idb(segment->get_layer()), rect_from_special_segment(segment), owner)
                     != 0) {
            ++result.special_net_wire_shape_count;
          }

          if (segment != nullptr && segment->get_via() != nullptr) {
            OwnerRef via_owner;
            via_owner.type = OwnerType::kVia;
            via_owner.owner_id = via_owner_id;
            via_owner.path0 = wire_index;
            via_owner.path1 = current_segment_index;
            via_owner.path2 = 0;
            result.via_shape_count += emit_via_shapes(store, segment->get_via(), via_owner);
          }
        }

        ++wire_index;
      }

      ++net_index;
    }
  }

  if (auto* blockages = design.get_blockage_list(); blockages != nullptr) {
    uint32_t blockage_index = 0;
    for (auto* blockage : blockages->get_blockage_list()) {
      idb::IdbLayer* layer = nullptr;
      if (auto* routing_blockage = dynamic_cast<idb::IdbRoutingBlockage*>(blockage); routing_blockage != nullptr) {
        layer = routing_blockage->get_layer();
      }

      uint32_t rect_index = 0;
      for (auto* rect : blockage == nullptr ? std::vector<idb::IdbRect*>{} : blockage->get_rect_list()) {
        OwnerRef owner;
        owner.type = OwnerType::kBlockage;
        owner.owner_id = blockage_index;
        owner.path0 = rect_index++;

        if (emit_rect_if_present(store, layer_id_from_idb(layer), rect_from_idb(rect), owner) != 0) {
          ++result.blockage_shape_count;
        }
      }

      ++blockage_index;
    }
  }

  if (auto* fills = design.get_fill_list(); fills != nullptr) {
    uint32_t fill_index = 0;
    for (auto* fill : fills->get_fill_list()) {
      if (fill == nullptr) {
        ++fill_index;
        continue;
      }

      if (fill->get_type() == idb::IdbFill::IdbFillType::kLayer && fill->get_layer() != nullptr) {
        uint32_t rect_index = 0;
        for (auto* rect : fill->get_layer()->get_rect_list()) {
          OwnerRef owner;
          owner.type = OwnerType::kFill;
          owner.owner_id = fill_index;
          owner.path0 = rect_index++;

          if (emit_rect_if_present(store, layer_id_from_idb(fill->get_layer()->get_layer()), rect_from_idb(rect), owner)
              != 0) {
            ++result.fill_shape_count;
          }
        }
      } else if (fill->get_type() == idb::IdbFill::IdbFillType::kVia && fill->get_via() != nullptr
                 && fill->get_via()->get_via() != nullptr) {
        uint32_t coordinate_index = 0;
        for (auto* coordinate : fill->get_via()->get_coordinate_list()) {
          OwnerRef owner;
          owner.type = OwnerType::kFill;
          owner.owner_id = fill_index;
          owner.path0 = coordinate_index++;

          result.fill_shape_count += emit_via_shapes_at(store, fill->get_via()->get_via(), coordinate, owner);
        }
      }

      ++fill_index;
    }
  }

  if (auto* regions = design.get_region_list(); regions != nullptr) {
    uint32_t region_index = 0;
    for (auto* region : regions->get_region_list()) {
      if (region != nullptr) {
        store.add_owner_name(OwnerType::kRegion, region_index, region->get_name());
      }

      uint32_t boundary_index = 0;
      for (auto* rect : region == nullptr ? std::vector<idb::IdbRect*>{} : region->get_boundary()) {
        OwnerRef owner;
        owner.type = OwnerType::kRegion;
        owner.owner_id = region_index;
        owner.path0 = boundary_index++;

        if (emit_layout_rect_if_present(store, rect_from_idb(rect), owner) != 0) {
          ++result.region_shape_count;
        }
      }

      ++region_index;
    }
  }

  if (auto* slots = design.get_slot_list(); slots != nullptr) {
    uint32_t slot_index = 0;
    for (auto* slot : slots->get_slot_list()) {
      uint32_t rect_index = 0;
      for (auto* rect : slot == nullptr ? std::vector<idb::IdbRect*>{} : slot->get_rect_list()) {
        OwnerRef owner;
        owner.type = OwnerType::kSlot;
        owner.owner_id = slot_index;
        owner.path0 = rect_index++;

        if (emit_rect_if_present(store, layer_id_from_idb(slot == nullptr ? nullptr : slot->get_layer()), rect_from_idb(rect),
                                 owner) != 0) {
          ++result.slot_shape_count;
        }
      }

      ++slot_index;
    }
  }

  result.shape_count = result.die_shape_count + result.core_shape_count + result.row_shape_count + result.track_grid_shape_count
                       + result.gcell_grid_shape_count + result.instance_shape_count + result.instance_halo_shape_count
                       + result.net_wire_shape_count + result.special_net_wire_shape_count + result.via_shape_count
                       + result.blockage_shape_count + result.fill_shape_count + result.region_shape_count + result.slot_shape_count
                       + result.pin_shape_count + result.obs_shape_count;
  store.clear_delta_events();
  return result;
}

std::vector<GeometryLayerMetadata> GeometryBuilder::collect_layer_metadata(idb::IdbLayout& layout) const
{
  std::vector<GeometryLayerMetadata> layers;
  auto* idb_layers = layout.get_layers();
  if (idb_layers == nullptr) {
    return layers;
  }

  for (auto* layer : idb_layers->get_layers()) {
    if (layer == nullptr) {
      continue;
    }
    layers.push_back(layer_metadata_from_idb(layer));
  }

  std::sort(layers.begin(), layers.end(), [](const GeometryLayerMetadata& lhs, const GeometryLayerMetadata& rhs) {
    if (lhs.order != rhs.order) {
      return lhs.order < rhs.order;
    }
    return lhs.layer_id < rhs.layer_id;
  });
  return layers;
}

std::vector<GeometrySiteMetadata> GeometryBuilder::collect_site_metadata(idb::IdbLayout& layout) const
{
  std::vector<GeometrySiteMetadata> sites;
  auto* idb_sites = layout.get_sites();
  if (idb_sites == nullptr) {
    return sites;
  }

  for (auto* site : idb_sites->get_site_list()) {
    if (site == nullptr) {
      continue;
    }

    GeometrySiteMetadata metadata;
    metadata.name = site->get_name();
    metadata.site_class = site_class_name(site->get_site_class());
    metadata.symmetry = site_symmetry_name(site->get_symmetry());
    metadata.orient = orient_name(site->get_orient());
    metadata.width = site->get_width();
    metadata.height = site->get_height();
    metadata.is_overlap = site->is_overlap();
    sites.push_back(metadata);
  }

  std::sort(sites.begin(), sites.end(), [](const GeometrySiteMetadata& lhs, const GeometrySiteMetadata& rhs) {
    return lhs.name < rhs.name;
  });
  return sites;
}

std::vector<GeometryMasterMetadata> GeometryBuilder::collect_master_metadata(idb::IdbLayout& layout) const
{
  std::vector<GeometryMasterMetadata> masters;
  auto* master_list = layout.get_cell_master_list();
  if (master_list == nullptr) {
    return masters;
  }

  for (auto* master : master_list->get_cell_master()) {
    if (master == nullptr) {
      continue;
    }

    GeometryMasterMetadata metadata;
    metadata.name = master->get_name();
    metadata.master_type = cell_master_type_name(master->get_type());
    metadata.site = master->get_site() == nullptr ? std::string{} : master->get_site()->get_name();
    metadata.symmetry = master_symmetry_name(master);
    metadata.origin_x = master->get_origin_x();
    metadata.origin_y = master->get_origin_y();
    metadata.width = master->get_width();
    metadata.height = master->get_height();
    metadata.term_count = static_cast<uint32_t>(master->get_term_num());
    metadata.obs_count = static_cast<uint32_t>(master->get_obs_list().size());
    masters.push_back(metadata);
  }

  std::sort(masters.begin(), masters.end(), [](const GeometryMasterMetadata& lhs, const GeometryMasterMetadata& rhs) {
    return lhs.name < rhs.name;
  });
  return masters;
}

std::vector<GeometryViaMetadata> GeometryBuilder::collect_via_metadata(idb::IdbLayout& layout, idb::IdbDesign& design) const
{
  std::vector<GeometryViaMetadata> vias;
  std::unordered_set<std::string> seen_keys;

  const auto collect_from_list = [&](idb::IdbVias* via_list) {
    if (via_list == nullptr) {
      return;
    }

    for (auto* via : via_list->get_via_list()) {
      if (via == nullptr) {
        continue;
      }

      GeometryViaMetadata metadata = via_metadata_from_idb(via);
      const std::string key = metadata.name.empty() ? metadata.master_name : metadata.name;
      if (key.empty() || seen_keys.contains(key)) {
        continue;
      }

      seen_keys.insert(key);
      vias.push_back(std::move(metadata));
    }
  };

  collect_from_list(layout.get_via_list());
  collect_from_list(design.get_via_list());

  std::sort(vias.begin(), vias.end(), [](const GeometryViaMetadata& lhs, const GeometryViaMetadata& rhs) {
    return std::tie(lhs.name, lhs.master_name) < std::tie(rhs.name, rhs.master_name);
  });
  return vias;
}

std::vector<GeometryGridMetadata> GeometryBuilder::collect_grid_metadata(idb::IdbLayout& layout) const
{
  std::vector<GeometryGridMetadata> grids;
  std::unordered_set<idb::IdbTrackGrid*> emitted_track_grids;
  uint32_t track_grid_index = 0;

  const auto collect_track_grid = [&](idb::IdbTrackGrid* track_grid, idb::IdbLayer* fallback_layer) {
    if (track_grid == nullptr || !emitted_track_grids.insert(track_grid).second) {
      return;
    }
    grids.push_back(track_grid_metadata_from_idb(track_grid, fallback_layer, track_grid_index++));
  };

  if (auto* track_grids = layout.get_track_grid_list(); track_grids != nullptr) {
    for (auto* track_grid : track_grids->get_track_grid_list()) {
      collect_track_grid(track_grid, nullptr);
    }
  }

  if (auto* layers = layout.get_layers(); layers != nullptr) {
    for (auto* layer : layers->get_layers()) {
      auto* routing_layer = dynamic_cast<idb::IdbLayerRouting*>(layer);
      if (routing_layer == nullptr) {
        continue;
      }

      for (auto* track_grid : routing_layer->get_track_grid_list()) {
        collect_track_grid(track_grid, routing_layer);
      }
    }
  }

  uint32_t gcell_grid_index = 0;
  if (auto* gcell_grids = layout.get_gcell_grid_list(); gcell_grids != nullptr) {
    for (auto* gcell_grid : gcell_grids->get_gcell_grid_list()) {
      if (gcell_grid == nullptr) {
        continue;
      }
      grids.push_back(gcell_grid_metadata_from_idb(gcell_grid, gcell_grid_index++));
    }
  }

  return grids;
}

std::vector<GeometryConnectivityMetadata> GeometryBuilder::collect_connectivity_metadata(idb::IdbDesign& design) const
{
  std::vector<GeometryConnectivityMetadata> connectivity;
  auto* net_list = design.get_net_list();
  if (net_list == nullptr) {
    return connectivity;
  }

  for (auto* net : net_list->get_net_list()) {
    if (net == nullptr) {
      continue;
    }

    if (auto* io_pins = net->get_io_pins(); io_pins != nullptr) {
      for (auto* pin : io_pins->get_pin_list()) {
        if (pin != nullptr) {
          connectivity.push_back(connectivity_metadata_from_pin(net, pin, "io"));
        }
      }
    }

    if (auto* instance_pins = net->get_instance_pin_list(); instance_pins != nullptr) {
      for (auto* pin : instance_pins->get_pin_list()) {
        if (pin != nullptr) {
          connectivity.push_back(connectivity_metadata_from_pin(net, pin, "instance"));
        }
      }
    }
  }

  std::sort(connectivity.begin(), connectivity.end(),
            [](const GeometryConnectivityMetadata& lhs, const GeometryConnectivityMetadata& rhs) {
              return std::tie(lhs.net_name, lhs.endpoint_type, lhs.instance_name, lhs.pin_name)
                     < std::tie(rhs.net_name, rhs.endpoint_type, rhs.instance_name, rhs.pin_name);
            });
  return connectivity;
}

std::vector<GeometryNetMetadata> GeometryBuilder::collect_net_metadata(idb::IdbDesign& design) const
{
  std::vector<GeometryNetMetadata> nets;
  auto* net_list = design.get_net_list();
  if (net_list == nullptr) {
    return nets;
  }

  for (auto* net : net_list->get_net_list()) {
    if (net == nullptr) {
      continue;
    }

    GeometryNetMetadata metadata;
    metadata.name = net->get_net_name();
    metadata.kind = net_kind_name(net);
    nets.push_back(metadata);
  }

  std::sort(nets.begin(), nets.end(), [](const GeometryNetMetadata& lhs, const GeometryNetMetadata& rhs) {
    return lhs.name < rhs.name;
  });
  return nets;
}

std::vector<GeometryBusMetadata> GeometryBuilder::collect_bus_metadata(idb::IdbDesign& design) const
{
  std::vector<GeometryBusMetadata> buses;
  auto* bus_list = design.get_bus_list();
  if (bus_list == nullptr) {
    return buses;
  }

  for (const idb::IdbBus& bus : bus_list->get_bus_list()) {
    GeometryBusMetadata metadata;
    metadata.name = bus.get_name();
    metadata.bus_type = bus_type_name(bus.get_type());
    metadata.left = bus.get_left();
    metadata.right = bus.get_right();
    metadata.net_count = static_cast<uint32_t>(bus.getNets().size());
    metadata.pin_count = static_cast<uint32_t>(bus.getPins().size());
    metadata.net_names = bus_net_names(bus);
    metadata.pin_names = bus_pin_names(bus);
    buses.push_back(metadata);
  }

  std::sort(buses.begin(), buses.end(), [](const GeometryBusMetadata& lhs, const GeometryBusMetadata& rhs) {
    return lhs.name < rhs.name;
  });
  return buses;
}

std::vector<GeometryGroupMetadata> GeometryBuilder::collect_group_metadata(idb::IdbDesign& design) const
{
  std::vector<GeometryGroupMetadata> groups;
  auto* group_list = design.get_group_list();
  if (group_list == nullptr) {
    return groups;
  }

  for (auto* group : group_list->get_group_list()) {
    if (group == nullptr) {
      continue;
    }

    GeometryGroupMetadata metadata;
    metadata.name = group->get_group_name();
    metadata.region_name = group->get_region() == nullptr ? std::string{} : group->get_region()->get_name();
    metadata.instance_count = group->get_instance_list() == nullptr
                                  ? 0
                                  : static_cast<uint32_t>(group->get_instance_list()->get_instance_list().size());
    metadata.instance_names = group_instance_names(group);
    groups.push_back(metadata);
  }

  std::sort(groups.begin(), groups.end(), [](const GeometryGroupMetadata& lhs, const GeometryGroupMetadata& rhs) {
    return lhs.name < rhs.name;
  });
  return groups;
}

GeometrySyncResult GeometryBuilder::sync_layout(idb::IdbLayout& layout, GeometryStore& store) const
{
  GeometrySyncResult result;
  std::unordered_set<ShapeId> seen_layout_shape_ids;

  if (auto* die = layout.get_die(); die != nullptr) {
    OwnerRef owner;
    owner.type = OwnerType::kDie;
    owner.owner_id = die->get_id();
    const Rect32 bbox = rect_from_idb(die->get_bounding_box());
    if (is_non_empty(bbox)) {
      reconcile_rect_shape(store, owner.type, owner.owner_id, kLayoutGeometryLayer, bbox, owner, seen_layout_shape_ids, result);
    }
  }

  if (layout.get_rows() != nullptr && layout.get_rows()->get_row_num() > 0) {
    auto* core = layout.get_core();
    OwnerRef owner;
    owner.type = OwnerType::kCore;
    owner.owner_id = core->get_id();
    const Rect32 bbox = rect_from_idb(core->get_bounding_box());
    if (is_non_empty(bbox)) {
      reconcile_rect_shape(store, owner.type, owner.owner_id, kLayoutGeometryLayer, bbox, owner, seen_layout_shape_ids, result);
    }
  }

  if (auto* rows = layout.get_rows(); rows != nullptr) {
    uint32_t row_index = 0;
    for (auto* row : rows->get_row_list()) {
      if (row == nullptr) {
        ++row_index;
        continue;
      }

      OwnerRef owner;
      owner.type = OwnerType::kRow;
      owner.owner_id = row->get_id();
      owner.path0 = row_index++;
      const Rect32 bbox = rect_from_idb(row->get_bounding_box());
      if (is_non_empty(bbox)) {
        reconcile_rect_shape(store, owner.type, owner.owner_id, kLayoutGeometryLayer, bbox, owner, seen_layout_shape_ids, result);
      }
    }
  }

  delete_unseen_owner_type_shapes(store, OwnerType::kDie, seen_layout_shape_ids, result);
  delete_unseen_owner_type_shapes(store, OwnerType::kCore, seen_layout_shape_ids, result);
  delete_unseen_owner_type_shapes(store, OwnerType::kRow, seen_layout_shape_ids, result);

  delete_owner_type_shapes(store, OwnerType::kTrackGrid, result);
  delete_owner_type_shapes(store, OwnerType::kGCellGrid, result);

  const Rect32 grid_bounds = grid_reference_bounds(layout);
  std::unordered_set<idb::IdbTrackGrid*> emitted_track_grids;
  OwnerId track_grid_owner_id = 0;
  if (auto* track_grids = layout.get_track_grid_list(); track_grids != nullptr) {
    for (auto* track_grid : track_grids->get_track_grid_list()) {
      if (track_grid == nullptr || !emitted_track_grids.insert(track_grid).second) {
        continue;
      }
      result.added_shape_count += emit_track_grid_lines(store, track_grid, grid_bounds, track_grid_owner_id++, nullptr);
    }
  }

  if (auto* layers = layout.get_layers(); layers != nullptr) {
    for (auto* layer : layers->get_layers()) {
      auto* routing_layer = dynamic_cast<idb::IdbLayerRouting*>(layer);
      if (routing_layer == nullptr) {
        continue;
      }

      for (auto* track_grid : routing_layer->get_track_grid_list()) {
        if (track_grid == nullptr || !emitted_track_grids.insert(track_grid).second) {
          continue;
        }
        result.added_shape_count += emit_track_grid_lines(store, track_grid, grid_bounds, track_grid_owner_id++, routing_layer);
      }
    }
  }

  if (auto* gcell_grids = layout.get_gcell_grid_list(); gcell_grids != nullptr) {
    OwnerId gcell_grid_owner_id = 0;
    for (auto* gcell_grid : gcell_grids->get_gcell_grid_list()) {
      result.added_shape_count += emit_gcell_grid_lines(store, gcell_grid, grid_bounds, gcell_grid_owner_id++);
    }
  }

  result.ok = result.missing_shape_count == 0;
  return result;
}

GeometrySyncResult GeometryBuilder::sync_net(idb::IdbDesign& design, idb::IdbNet& net, GeometryStore& store) const
{
  GeometrySyncResult result;
  OwnerId owner_id = 0;
  if (!resolve_regular_net_owner_id(design, net, owner_id)) {
    result.missing_shape_count = 1;
    return result;
  }

  const OwnerId via_owner_id = make_derived_owner_id(OwnerType::kNetWireSegment, owner_id);

  std::unordered_set<ShapeId> seen_shape_ids;
  std::unordered_set<ShapeId> seen_via_shape_ids;
  if (auto* wire_list = net.get_wire_list(); wire_list != nullptr) {
    uint32_t wire_index = 0;
    for (auto* wire : wire_list->get_wire_list()) {
      if (wire == nullptr) {
        ++wire_index;
        continue;
      }

      uint32_t segment_index = 0;
      for (auto* segment : wire->get_segment_list()) {
        const uint32_t current_segment_index = segment_index++;
        OwnerRef owner;
        owner.type = OwnerType::kNetWireSegment;
        owner.owner_id = owner_id;
        owner.path0 = wire_index;
        owner.path1 = current_segment_index;

        const Rect32 bbox = rect_from_regular_segment(segment);
        if (is_non_empty(bbox)) {
          reconcile_rect_shape(store, owner.type, owner.owner_id, layer_id_from_idb(segment == nullptr ? nullptr : segment->get_layer()),
                               bbox, owner, seen_shape_ids, result);
        }

        if (segment != nullptr) {
          uint32_t via_index = 0;
          for (auto* via : segment->get_via_list()) {
            OwnerRef via_owner;
            via_owner.type = OwnerType::kVia;
            via_owner.owner_id = via_owner_id;
            via_owner.path0 = wire_index;
            via_owner.path1 = current_segment_index;
            via_owner.path2 = via_index++;
            reconcile_via_shapes(store, via, via_owner, seen_via_shape_ids, result);
          }
        }
      }

      ++wire_index;
    }
  }

  for (const ShapeId shape_id : store.query_owner(OwnerType::kNetWireSegment, owner_id)) {
    if (seen_shape_ids.contains(shape_id)) {
      continue;
    }
    if (store.delete_shape(shape_id)) {
      ++result.deleted_shape_count;
    } else {
      ++result.missing_shape_count;
    }
  }

  for (const ShapeId shape_id : store.query_owner(OwnerType::kVia, via_owner_id)) {
    if (seen_via_shape_ids.contains(shape_id)) {
      continue;
    }
    if (store.delete_shape(shape_id)) {
      ++result.deleted_shape_count;
    } else {
      ++result.missing_shape_count;
    }
  }

  result.ok = result.missing_shape_count == 0;
  return result;
}

GeometrySyncResult GeometryBuilder::sync_special_net(idb::IdbDesign& design, idb::IdbSpecialNet& net, GeometryStore& store) const
{
  GeometrySyncResult result;
  OwnerId owner_id = 0;
  if (!resolve_special_net_owner_id(design, net, owner_id)) {
    result.missing_shape_count = 1;
    return result;
  }

  const OwnerId via_owner_id = make_derived_owner_id(OwnerType::kSpecialWireSegment, owner_id);

  std::unordered_set<ShapeId> seen_shape_ids;
  std::unordered_set<ShapeId> seen_via_shape_ids;
  if (auto* wire_list = net.get_wire_list(); wire_list != nullptr) {
    uint32_t wire_index = 0;
    for (auto* wire : wire_list->get_wire_list()) {
      if (wire == nullptr) {
        ++wire_index;
        continue;
      }

      uint32_t segment_index = 0;
      for (auto* segment : wire->get_segment_list()) {
        const uint32_t current_segment_index = segment_index++;
        OwnerRef owner;
        owner.type = OwnerType::kSpecialWireSegment;
        owner.owner_id = owner_id;
        owner.path0 = wire_index;
        owner.path1 = current_segment_index;

        if (segment != nullptr && !segment->is_rect()) {
          segment->set_bounding_box();
        }

        const Rect32 bbox = segment != nullptr && !segment->is_via() ? rect_from_special_segment(segment) : Rect32{};
        if (is_non_empty(bbox)) {
          reconcile_rect_shape(store, owner.type, owner.owner_id, layer_id_from_idb(segment == nullptr ? nullptr : segment->get_layer()),
                               bbox, owner, seen_shape_ids, result);
        }

        if (segment != nullptr && segment->get_via() != nullptr) {
          OwnerRef via_owner;
          via_owner.type = OwnerType::kVia;
          via_owner.owner_id = via_owner_id;
          via_owner.path0 = wire_index;
          via_owner.path1 = current_segment_index;
          via_owner.path2 = 0;
          reconcile_via_shapes(store, segment->get_via(), via_owner, seen_via_shape_ids, result);
        }
      }

      ++wire_index;
    }
  }

  for (const ShapeId shape_id : store.query_owner(OwnerType::kSpecialWireSegment, owner_id)) {
    if (seen_shape_ids.contains(shape_id)) {
      continue;
    }
    if (store.delete_shape(shape_id)) {
      ++result.deleted_shape_count;
    } else {
      ++result.missing_shape_count;
    }
  }

  for (const ShapeId shape_id : store.query_owner(OwnerType::kVia, via_owner_id)) {
    if (seen_via_shape_ids.contains(shape_id)) {
      continue;
    }
    if (store.delete_shape(shape_id)) {
      ++result.deleted_shape_count;
    } else {
      ++result.missing_shape_count;
    }
  }

  result.ok = result.missing_shape_count == 0;
  return result;
}

GeometrySyncResult GeometryBuilder::sync_blockage(idb::IdbDesign& design, idb::IdbBlockage& blockage, GeometryStore& store) const
{
  GeometrySyncResult result;
  OwnerId owner_id = 0;
  if (!resolve_blockage_owner_id(design, blockage, owner_id)) {
    result.missing_shape_count = 1;
    return result;
  }

  idb::IdbLayer* layer = nullptr;
  if (auto* routing_blockage = dynamic_cast<idb::IdbRoutingBlockage*>(&blockage); routing_blockage != nullptr) {
    layer = routing_blockage->get_layer();
  }

  std::unordered_set<ShapeId> seen_shape_ids;
  uint32_t rect_index = 0;
  for (auto* rect : blockage.get_rect_list()) {
    OwnerRef owner;
    owner.type = OwnerType::kBlockage;
    owner.owner_id = owner_id;
    owner.path0 = rect_index++;

    const Rect32 bbox = rect_from_idb(rect);
    if (is_non_empty(bbox)) {
      reconcile_rect_shape(store, owner.type, owner.owner_id, layer_id_from_idb(layer), bbox, owner, seen_shape_ids, result);
    }
  }

  for (const ShapeId shape_id : store.query_owner(OwnerType::kBlockage, owner_id)) {
    if (seen_shape_ids.contains(shape_id)) {
      continue;
    }
    if (store.delete_shape(shape_id)) {
      ++result.deleted_shape_count;
    } else {
      ++result.missing_shape_count;
    }
  }

  result.ok = result.missing_shape_count == 0;
  return result;
}

GeometrySyncResult GeometryBuilder::sync_region(idb::IdbDesign& design, idb::IdbRegion& region, GeometryStore& store) const
{
  GeometrySyncResult result;
  OwnerId owner_id = 0;
  if (!resolve_region_owner_id(design, region, owner_id)) {
    result.missing_shape_count = 1;
    return result;
  }

  store.add_owner_name(OwnerType::kRegion, owner_id, region.get_name());

  std::unordered_set<ShapeId> seen_shape_ids;
  uint32_t boundary_index = 0;
  for (auto* rect : region.get_boundary()) {
    OwnerRef owner;
    owner.type = OwnerType::kRegion;
    owner.owner_id = owner_id;
    owner.path0 = boundary_index++;

    const Rect32 bbox = rect_from_idb(rect);
    if (is_non_empty(bbox)) {
      reconcile_rect_shape(store, owner.type, owner.owner_id, kLayoutGeometryLayer, bbox, owner, seen_shape_ids, result);
    }
  }

  delete_unseen_owner_shapes(store, OwnerType::kRegion, owner_id, seen_shape_ids, result);
  result.ok = result.missing_shape_count == 0;
  return result;
}

GeometrySyncResult GeometryBuilder::sync_slot(idb::IdbDesign& design, idb::IdbSlot& slot, GeometryStore& store) const
{
  GeometrySyncResult result;
  OwnerId owner_id = 0;
  if (!resolve_slot_owner_id(design, slot, owner_id)) {
    result.missing_shape_count = 1;
    return result;
  }

  std::unordered_set<ShapeId> seen_shape_ids;
  uint32_t rect_index = 0;
  for (auto* rect : slot.get_rect_list()) {
    OwnerRef owner;
    owner.type = OwnerType::kSlot;
    owner.owner_id = owner_id;
    owner.path0 = rect_index++;

    const Rect32 bbox = rect_from_idb(rect);
    if (is_non_empty(bbox)) {
      reconcile_rect_shape(store, owner.type, owner.owner_id, layer_id_from_idb(slot.get_layer()), bbox, owner, seen_shape_ids,
                           result);
    }
  }

  delete_unseen_owner_shapes(store, OwnerType::kSlot, owner_id, seen_shape_ids, result);
  result.ok = result.missing_shape_count == 0;
  return result;
}

GeometrySyncResult GeometryBuilder::sync_fill(idb::IdbDesign& design, idb::IdbFill& fill, GeometryStore& store) const
{
  GeometrySyncResult result;
  OwnerId owner_id = 0;
  if (!resolve_fill_owner_id(design, fill, owner_id)) {
    result.missing_shape_count = 1;
    return result;
  }

  std::unordered_set<ShapeId> seen_shape_ids;
  if (fill.get_type() == idb::IdbFill::IdbFillType::kLayer && fill.get_layer() != nullptr) {
    uint32_t rect_index = 0;
    for (auto* rect : fill.get_layer()->get_rect_list()) {
      OwnerRef owner;
      owner.type = OwnerType::kFill;
      owner.owner_id = owner_id;
      owner.path0 = rect_index++;

      const Rect32 bbox = rect_from_idb(rect);
      if (is_non_empty(bbox)) {
        reconcile_rect_shape(store, owner.type, owner.owner_id, layer_id_from_idb(fill.get_layer()->get_layer()), bbox, owner,
                             seen_shape_ids, result);
      }
    }
  } else if (fill.get_type() == idb::IdbFill::IdbFillType::kVia && fill.get_via() != nullptr
             && fill.get_via()->get_via() != nullptr) {
    uint32_t coordinate_index = 0;
    for (auto* coordinate : fill.get_via()->get_coordinate_list()) {
      OwnerRef owner;
      owner.type = OwnerType::kFill;
      owner.owner_id = owner_id;
      owner.path0 = coordinate_index++;

      reconcile_via_shapes_at(store, fill.get_via()->get_via(), coordinate, owner, seen_shape_ids, result);
    }
  }

  delete_unseen_owner_shapes(store, OwnerType::kFill, owner_id, seen_shape_ids, result);
  result.ok = result.missing_shape_count == 0;
  return result;
}

GeometrySyncResult GeometryBuilder::sync_instance(idb::IdbInstance& instance, GeometryStore& store) const
{
  GeometrySyncResult result;
  const OwnerId instance_owner_id = instance.get_id();
  uint32_t instance_path0 = 0;
  const std::vector<ShapeId> instance_shape_ids = store.query_owner(OwnerType::kInstanceBBox, instance_owner_id);
  if (!instance_shape_ids.empty()) {
    instance_path0 = store.owner_of(instance_shape_ids[0]).path0;
  }

  const auto sync_owner_rect = [&](OwnerType owner_type, Rect32 bbox) {
    const std::vector<ShapeId> shape_ids = store.query_owner(owner_type, instance_owner_id);
    if (shape_ids.empty()) {
      ++result.missing_shape_count;
      return;
    }

    for (const ShapeId shape_id : shape_ids) {
      const ShapeRecord* record = store.find_shape(shape_id);
      if (record == nullptr || record->state != ShapeState::kAlive || record->kind != ShapeKind::kRect) {
        ++result.missing_shape_count;
        continue;
      }

      if (is_same_rect(record->bbox, bbox)) {
        continue;
      }

      if (store.update_rect(shape_id, bbox)) {
        ++result.updated_shape_count;
      } else {
        ++result.missing_shape_count;
      }
    }
  };

  sync_owner_rect(OwnerType::kInstanceBBox, rect_from_idb(instance.get_bounding_box()));

  if (auto* halo = instance.get_halo(); halo != nullptr) {
    instance.set_halo_coodinate();
    sync_owner_rect(OwnerType::kInstanceHalo, rect_from_idb(halo->get_bounding_box()));
  }

  if (instance.get_cell_master() != nullptr) {
    instance.set_obs_box_list();
    std::unordered_set<ShapeId> seen_obs_shape_ids;
    uint32_t obs_layer_index = 0;
    for (auto* obs_shape : instance.get_obs_box_list()) {
      if (obs_shape == nullptr) {
        ++obs_layer_index;
        continue;
      }

      uint32_t rect_index = 0;
      for (auto* rect : obs_shape->get_rect_list()) {
        OwnerRef obs_owner;
        obs_owner.type = OwnerType::kObs;
        obs_owner.owner_id = instance_owner_id;
        obs_owner.path0 = instance_path0;
        obs_owner.path1 = obs_layer_index;
        obs_owner.path2 = rect_index++;
        obs_owner = with_master_local_name(store, instance.get_cell_master(), obs_owner);

        reconcile_rect_shape(store, obs_owner.type, obs_owner.owner_id, layer_id_from_idb(obs_shape->get_layer()),
                             rect_from_idb(rect), obs_owner, seen_obs_shape_ids, result);
      }

      ++obs_layer_index;
    }

    for (const ShapeId shape_id : store.query_owner(OwnerType::kObs, instance_owner_id)) {
      if (seen_obs_shape_ids.contains(shape_id)) {
        continue;
      }
      if (store.delete_shape(shape_id)) {
        ++result.deleted_shape_count;
      } else {
        ++result.missing_shape_count;
      }
    }
  }

  if (auto* pins = instance.get_pin_list(); pins != nullptr) {
    std::unordered_set<ShapeId> seen_pin_shape_ids;
    std::unordered_set<ShapeId> seen_pin_via_shape_ids;
    std::vector<OwnerId> current_pin_owner_ids;
    std::vector<OwnerId> current_pin_via_owner_ids;
    uint32_t pin_index = 0;
    for (auto* pin : pins->get_pin_list()) {
      const OwnerId pin_owner_id = pin_owner_id_from_path(pin, instance_path0, pin_index);
      current_pin_owner_ids.push_back(pin_owner_id);
      current_pin_via_owner_ids.push_back(make_derived_owner_id(OwnerType::kInstancePinPortShape, pin_owner_id));
      reconcile_pin_port_shapes(store, pin, OwnerType::kInstancePinPortShape, instance_path0, pin_index++,
                                instance.get_cell_master(), seen_pin_shape_ids, seen_pin_via_shape_ids, result);
    }

    for (const OwnerId pin_owner_id : current_pin_owner_ids) {
      for (const ShapeId shape_id : store.query_owner(OwnerType::kInstancePinPortShape, pin_owner_id)) {
        if (seen_pin_shape_ids.contains(shape_id)) {
          continue;
        }
        if (store.delete_shape(shape_id)) {
          ++result.deleted_shape_count;
        } else {
          ++result.missing_shape_count;
        }
      }
    }

    for (const OwnerId via_owner_id : current_pin_via_owner_ids) {
      for (const ShapeId shape_id : store.query_owner(OwnerType::kVia, via_owner_id)) {
        if (seen_pin_via_shape_ids.contains(shape_id)) {
          continue;
        }
        if (store.delete_shape(shape_id)) {
          ++result.deleted_shape_count;
        } else {
          ++result.missing_shape_count;
        }
      }
    }
  }

  result.ok = result.missing_shape_count == 0;
  return result;
}

GeometrySyncResult GeometryBuilder::sync_io_pin(idb::IdbDesign& design, idb::IdbPin& pin, GeometryStore& store) const
{
  GeometrySyncResult result;
  uint32_t pin_index = 0;
  if (!resolve_io_pin_path(design, pin, pin_index)) {
    result.missing_shape_count = 1;
    return result;
  }

  std::unordered_set<ShapeId> seen_pin_shape_ids;
  std::unordered_set<ShapeId> seen_via_shape_ids;
  reconcile_pin_port_shapes(store, &pin, OwnerType::kIoPinPortShape, 0, pin_index, nullptr, seen_pin_shape_ids,
                            seen_via_shape_ids, result);

  const OwnerId pin_owner_id = pin_owner_id_from_path(&pin, 0, pin_index);
  delete_unseen_owner_shapes(store, OwnerType::kIoPinPortShape, pin_owner_id, seen_pin_shape_ids, result);

  const OwnerId via_owner_id = make_derived_owner_id(OwnerType::kIoPinPortShape, pin_owner_id);
  delete_unseen_owner_shapes(store, OwnerType::kVia, via_owner_id, seen_via_shape_ids, result);

  result.ok = result.missing_shape_count == 0;
  return result;
}

}  // namespace ecc::geometry
