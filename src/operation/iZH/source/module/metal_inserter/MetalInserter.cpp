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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "MetalInserter.hpp"

#include "GeometryBuilder.h"
#include "IdbDesign.h"
#include "IdbFill.h"
#include "IdbGeometry.h"
#include "IdbLayer.h"
#include "IdbLayout.h"
#include "Utility.hpp"
#include "idm.h"

namespace izh {

// public

void MetalInserter::initInst()
{
  if (_mi_instance == nullptr) {
    _mi_instance = new MetalInserter();
  }
}

MetalInserter& MetalInserter::getInst()
{
  if (_mi_instance == nullptr) {
    ZHLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_mi_instance;
}

void MetalInserter::destroyInst()
{
  if (_mi_instance != nullptr) {
    delete _mi_instance;
    _mi_instance = nullptr;
  }
}

// function

void MetalInserter::insert(std::map<std::string, std::any> config_map)
{
  Monitor monitor;
  ZHLOG.info(Loc::current(), "Starting...");

  MIModel mi_model = initMIModel(config_map);
  buildMetalFill(mi_model);

  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(0), "ZH insertMetal");
  ZHLOG.info(Loc::current(), "Inserted ", mi_model.get_inserted_metal_num(), " metal fills");

  ZHLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

MIModel MetalInserter::initMIModel(std::map<std::string, std::any>& config_map)
{
  MIModel mi_model;
  initRuleFilePath(mi_model, config_map);
  initFillArea(mi_model, config_map);
  initResetFill(mi_model, config_map);
  initLayerRuleList(mi_model);

  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(1), "rule_file_path: ", mi_model.get_rule_file_path());
  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(1), "fill_area: (", mi_model.get_fill_area().get_ll_x(), ",",
             mi_model.get_fill_area().get_ll_y(), ")-(", mi_model.get_fill_area().get_ur_x(), ",", mi_model.get_fill_area().get_ur_y(), ")");
  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(1), "layer_rule_num: ", mi_model.get_layer_rule_list().size());
  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(1), "reset_fill: ", mi_model.get_reset_fill());
  return mi_model;
}

void MetalInserter::initRuleFilePath(MIModel& mi_model, std::map<std::string, std::any>& config_map)
{
  if (!Utility::exist(config_map, std::string("-rules"))) {
    ZHLOG.error(Loc::current(), "The -rules option is required!");
  }
  mi_model.set_rule_file_path(ZHUTIL.getConfigValue<std::string>(config_map, "-rules", ""));
  if (mi_model.get_rule_file_path().empty()) {
    ZHLOG.error(Loc::current(), "The -rules option is empty!");
  }
}

void MetalInserter::initFillArea(MIModel& mi_model, std::map<std::string, std::any>& config_map)
{
  if (Utility::exist(config_map, std::string("-area"))) {
    std::vector<int> area = ZHUTIL.getConfigValue<std::vector<int>>(config_map, "-area", {});
    if (static_cast<int32_t>(area.size()) != 4) {
      ZHLOG.error(Loc::current(), "The -area option requires exactly four DBU coordinates!");
    }
    mi_model.set_fill_area(MIRect(area[0], area[1], area[2], area[3]));
  } else {
    idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
    if (idb_layout == nullptr) {
      ZHLOG.error(Loc::current(), "The idb layout is null!");
    }
    idb::IdbRect* idb_rect = nullptr;
    if (idb_layout->get_core() != nullptr && idb_layout->get_core()->get_bounding_box() != nullptr) {
      idb_rect = idb_layout->get_core()->get_bounding_box();
    } else if (idb_layout->get_die() != nullptr && idb_layout->get_die()->get_bounding_box() != nullptr) {
      idb_rect = idb_layout->get_die()->get_bounding_box();
    }
    if (idb_rect == nullptr) {
      ZHLOG.error(Loc::current(), "The idb core and die are null!");
    }
    mi_model.set_fill_area(MIRect(idb_rect->get_low_x(), idb_rect->get_low_y(), idb_rect->get_high_x(), idb_rect->get_high_y()));
  }
  if (!mi_model.get_fill_area().isValid()) {
    ZHLOG.error(Loc::current(), "The metal fill area is invalid!");
  }
}

void MetalInserter::initResetFill(MIModel& mi_model, std::map<std::string, std::any>& config_map)
{
  mi_model.set_reset_fill(ZHUTIL.getConfigValue<int32_t>(config_map, "-reset_fill", 0) != 0);
}

void MetalInserter::initLayerRuleList(MIModel& mi_model)
{
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  if (idb_layout == nullptr || idb_layout->get_units() == nullptr || idb_layout->get_units()->get_micron_dbu() <= 0) {
    ZHLOG.error(Loc::current(), "The idb DBU unit is invalid!");
  }

  std::ifstream rule_file(mi_model.get_rule_file_path());
  if (!rule_file.is_open()) {
    ZHLOG.error(Loc::current(), "Failed to open metal fill rule file: ", mi_model.get_rule_file_path());
  }

  nlohmann::json root;
  rule_file >> root;

  std::vector<MILayerRule> layer_rule_list;
  for (auto& [layer_group_name, layer_group] : root.at("layers").items()) {
    nlohmann::json& non_opc = layer_group.at("non-opc");
    std::vector<std::string> layer_name_list = getLayerNameList(layer_group);
    std::vector<MIFillShape> fill_shape_list = getFillShapeList(non_opc, idb_layout->get_units()->get_micron_dbu());
    int32_t space_to_fill = getDbuValue(non_opc.at("space_to_fill").get<double>(), idb_layout->get_units()->get_micron_dbu());
    int32_t space_to_non_fill = getDbuValue(non_opc.at("space_to_non_fill").get<double>(), idb_layout->get_units()->get_micron_dbu());

    for (const std::string& layer_name : layer_name_list) {
      MILayerRule layer_rule;
      layer_rule.set_layer_name(layer_name);
      layer_rule.set_fill_shape_list(fill_shape_list);
      layer_rule.set_space_to_fill(space_to_fill);
      layer_rule.set_space_to_non_fill(space_to_non_fill);
      layer_rule_list.push_back(layer_rule);
    }
  }
  mi_model.set_layer_rule_list(layer_rule_list);
}

std::vector<std::string> MetalInserter::getLayerNameList(nlohmann::json& layer_group)
{
  std::vector<std::string> layer_name_list;
  if (layer_group.contains("names")) {
    for (nlohmann::json& layer_name : layer_group.at("names")) {
      layer_name_list.push_back(layer_name.get<std::string>());
    }
  } else if (layer_group.contains("name")) {
    layer_name_list.push_back(layer_group.at("name").get<std::string>());
  }
  if (layer_name_list.empty()) {
    ZHLOG.error(Loc::current(), "The metal fill layer group has no layer name!");
  }
  return layer_name_list;
}

std::vector<MIFillShape> MetalInserter::getFillShapeList(nlohmann::json& non_opc, int32_t dbu_per_micron)
{
  std::vector<double> width_list = getNumberList(non_opc.at("width"));
  std::vector<double> height_list = getNumberList(non_opc.at("height"));
  if (width_list.size() != height_list.size()) {
    ZHLOG.error(Loc::current(), "The metal fill width and height list sizes are different!");
  }

  std::vector<MIFillShape> fill_shape_list;
  for (int32_t shape_idx = 0; shape_idx < static_cast<int32_t>(width_list.size()); ++shape_idx) {
    MIFillShape fill_shape(getDbuValue(width_list[shape_idx], dbu_per_micron), getDbuValue(height_list[shape_idx], dbu_per_micron));
    if (!fill_shape.isValid()) {
      ZHLOG.error(Loc::current(), "The metal fill shape is invalid!");
    }
    fill_shape_list.push_back(fill_shape);
  }
  return fill_shape_list;
}

std::vector<double> MetalInserter::getNumberList(nlohmann::json& number_config)
{
  std::vector<double> number_list;
  if (number_config.is_array()) {
    for (nlohmann::json& number : number_config) {
      number_list.push_back(number.get<double>());
    }
  } else {
    number_list.push_back(number_config.get<double>());
  }
  return number_list;
}

int32_t MetalInserter::getDbuValue(double micron_value, int32_t dbu_per_micron)
{
  return static_cast<int32_t>(std::llround(micron_value * dbu_per_micron));
}

void MetalInserter::buildMetalFill(MIModel& mi_model)
{
  resetMetalFill(mi_model);

  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  if (idb_design == nullptr || idb_layout == nullptr) {
    ZHLOG.error(Loc::current(), "The idb design or layout is null!");
  }

  ecc::geometry::GeometryBuilder geometry_builder;
  ecc::geometry::GeometryStore geometry_store;
  geometry_builder.rebuild_from_design(*idb_design, *idb_layout, geometry_store);
  std::vector<ecc::geometry::GeometryLayerMetadata> geometry_layer_list = geometry_builder.collect_layer_metadata(*idb_layout);
  for (MILayerRule& layer_rule : mi_model.get_layer_rule_list()) {
    initLayerDirection(layer_rule);
    buildLayerMetalFill(mi_model, geometry_store, geometry_layer_list, layer_rule);
  }
}

void MetalInserter::resetMetalFill(MIModel& mi_model)
{
  if (!mi_model.get_reset_fill()) {
    return;
  }
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  if (idb_design == nullptr || idb_design->get_fill_list() == nullptr) {
    ZHLOG.error(Loc::current(), "The idb design fill list is null!");
  }
  idb_design->get_fill_list()->reset();
}

void MetalInserter::initLayerDirection(MILayerRule& layer_rule)
{
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  if (idb_layout == nullptr || idb_layout->get_layers() == nullptr) {
    ZHLOG.error(Loc::current(), "The idb layer list is null!");
  }
  idb::IdbLayer* idb_layer = idb_layout->get_layers()->find_layer(layer_rule.get_layer_name());
  idb::IdbLayerRouting* idb_routing_layer = dynamic_cast<idb::IdbLayerRouting*>(idb_layer);
  if (idb_routing_layer == nullptr) {
    ZHLOG.error(Loc::current(), "The metal fill layer is unknown or non-routing: ", layer_rule.get_layer_name());
  }
  layer_rule.set_is_horizontal(idb_routing_layer->is_horizontal());
}

void MetalInserter::buildLayerMetalFill(MIModel& mi_model, ecc::geometry::GeometryStore& geometry_store,
                                         const std::vector<ecc::geometry::GeometryLayerMetadata>& geometry_layer_list,
                                         MILayerRule& layer_rule)
{
  int32_t geometry_layer_idx = getGeometryLayerIdx(geometry_layer_list, layer_rule);
  if (geometry_layer_idx < 0) {
    ZHLOG.error(Loc::current(), "The geometry layer does not exist: ", layer_rule.get_layer_name());
  }
  std::vector<MIRect> metal_rect_list = buildFillRectList(geometry_store, geometry_layer_idx, layer_rule, mi_model.get_fill_area());
  writeMetalFill(mi_model, layer_rule, metal_rect_list);
}

int32_t MetalInserter::getGeometryLayerIdx(const std::vector<ecc::geometry::GeometryLayerMetadata>& geometry_layer_list,
                                            const MILayerRule& layer_rule)
{
  for (const ecc::geometry::GeometryLayerMetadata& geometry_layer : geometry_layer_list) {
    if (geometry_layer.name == layer_rule.get_layer_name()) {
      return geometry_layer.layer_id;
    }
  }
  return -1;
}

std::vector<MIRect> MetalInserter::buildFillRectList(ecc::geometry::GeometryStore& geometry_store, int32_t geometry_layer_idx,
                                                      const MILayerRule& layer_rule, const MIRect& fill_area)
{
  std::vector<MIRect> metal_rect_list;
  ecc::geometry::GeometryStore metal_store;
  for (MIFillShape raw_fill_shape : layer_rule.get_fill_shape_list()) {
    MIFillShape fill_shape = getOrientFillShape(raw_fill_shape, layer_rule.get_is_horizontal());
    int32_t x_step = fill_shape.get_width() + layer_rule.get_space_to_fill();
    int32_t y_step = fill_shape.get_height() + layer_rule.get_space_to_fill();
    if (!fill_shape.isValid() || x_step <= 0 || y_step <= 0) {
      continue;
    }

    for (int32_t x = fill_area.get_ll_x(); x + fill_shape.get_width() <= fill_area.get_ur_x(); x += x_step) {
      for (int32_t y = fill_area.get_ll_y(); y + fill_shape.get_height() <= fill_area.get_ur_y(); y += y_step) {
        MIRect metal_rect(x, y, x + fill_shape.get_width(), y + fill_shape.get_height());
        if (isBlocked(geometry_store, geometry_layer_idx, metal_rect, layer_rule.get_space_to_non_fill())) {
          continue;
        }
        if (isBlocked(metal_store, geometry_layer_idx, metal_rect, layer_rule.get_space_to_fill())) {
          continue;
        }
        ecc::geometry::OwnerRef owner;
        owner.type = ecc::geometry::OwnerType::kFill;
        owner.owner_id = static_cast<ecc::geometry::OwnerId>(metal_rect_list.size());
        metal_store.add_rect(static_cast<ecc::geometry::LayerId>(geometry_layer_idx), getGeometryRect(metal_rect), owner);
        metal_rect_list.push_back(metal_rect);
      }
    }
  }
  return metal_rect_list;
}

MIFillShape MetalInserter::getOrientFillShape(MIFillShape fill_shape, bool is_horizontal)
{
  if ((is_horizontal && fill_shape.get_width() < fill_shape.get_height())
      || (!is_horizontal && fill_shape.get_height() < fill_shape.get_width())) {
    int32_t width = fill_shape.get_width();
    fill_shape.set_width(fill_shape.get_height());
    fill_shape.set_height(width);
  }
  return fill_shape;
}

bool MetalInserter::isBlocked(ecc::geometry::GeometryStore& geometry_store, int32_t geometry_layer_idx, const MIRect& metal_rect,
                              int32_t spacing)
{
  MIRect expand_rect = metal_rect.getExpandRect(spacing);
  std::vector<ecc::geometry::ShapeId> shape_id_list =
      geometry_store.query_intersect(static_cast<ecc::geometry::LayerId>(geometry_layer_idx), getGeometryRect(expand_rect));
  for (ecc::geometry::ShapeId shape_id : shape_id_list) {
    ecc::geometry::OwnerRef owner = geometry_store.owner_of(shape_id);
    if (owner.type == ecc::geometry::OwnerType::kTrackGrid || owner.type == ecc::geometry::OwnerType::kGCellGrid) {
      continue;
    }
    const ecc::geometry::ShapeRecord* shape_record = geometry_store.find_shape(shape_id);
    if (shape_record == nullptr) {
      continue;
    }
    MIRect occupied_rect(shape_record->bbox.lx, shape_record->bbox.ly, shape_record->bbox.hx, shape_record->bbox.hy);
    if (expand_rect.isIntersect(occupied_rect)) {
      return true;
    }
  }
  return false;
}

ecc::geometry::Rect32 MetalInserter::getGeometryRect(const MIRect& metal_rect)
{
  return ecc::geometry::Rect32{metal_rect.get_ll_x(), metal_rect.get_ll_y(), metal_rect.get_ur_x(), metal_rect.get_ur_y()};
}

void MetalInserter::writeMetalFill(MIModel& mi_model, const MILayerRule& layer_rule, const std::vector<MIRect>& metal_rect_list)
{
  if (metal_rect_list.empty()) {
    return;
  }
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  if (idb_design == nullptr || idb_layout == nullptr || idb_design->get_fill_list() == nullptr || idb_layout->get_layers() == nullptr) {
    ZHLOG.error(Loc::current(), "The idb metal fill data is null!");
  }
  idb::IdbLayer* idb_layer = idb_layout->get_layers()->find_layer(layer_rule.get_layer_name());
  if (idb_layer == nullptr) {
    ZHLOG.error(Loc::current(), "The metal fill layer is null: ", layer_rule.get_layer_name());
  }
  idb::IdbFillLayer* idb_fill_layer = idb_design->get_fill_list()->add_fill_layer(idb_layer);
  for (const MIRect& metal_rect : metal_rect_list) {
    idb_fill_layer->add_rect(metal_rect.get_ll_x(), metal_rect.get_ll_y(), metal_rect.get_ur_x(), metal_rect.get_ur_y());
  }
  mi_model.addInsertedMetalNum(static_cast<int32_t>(metal_rect_list.size()));
}

MetalInserter* MetalInserter::_mi_instance = nullptr;

}  // namespace izh
