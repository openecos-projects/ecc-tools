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

#include "gds_write.h"

#include <gdstk/gdstk.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>

#include "../../../data/design/IdbDesign.h"
#include "boost_definition.h"

namespace idb {

Def2GdsWrite::Def2GdsWrite(IdbDefService* def_service) : _def_service(def_service), _library(std::make_unique<gdstk::Library>())
{
}

Def2GdsWrite::~Def2GdsWrite()
{
}

bool Def2GdsWrite::writeDb(const char* file)
{
  if (set_units() != kDbSuccess) {
    return false;
  }

  if (!writeChip()) {
    _library->free_all();
    return false;
  }

  return finishWrite(file);
}

bool Def2GdsWrite::writeHardenedDb(const char* file)
{
  if (set_units() != kDbSuccess) {
    return false;
  }

  write_version();
  write_design();
  write_die();
  write_harden_macro_pins();
  write_harden_macro_obs();

  return finishWrite(file);
}

bool Def2GdsWrite::finishWrite(const char* file)
{
  auto error = _library->write_gds(file, 8190, nullptr);
  _library->free_all();
  _top_cell = nullptr;
  _cell_name_count.clear();
  _used_cell_names.clear();
  return error == gdstk::ErrorCode::NoError;
}

bool Def2GdsWrite::writeChip()
{
  write_version();
  write_design();
  write_die();
  write_pin();
  write_component();
  write_fill();
  write_special_net();
  write_net();
  return true;
}

int32_t Def2GdsWrite::set_units()
{
  IdbDesign* design = _def_service->get_design();
  IdbUnits* def_units = design == nullptr ? nullptr : design->get_units();
  IdbUnits* lef_units = design == nullptr ? nullptr : design->get_layout()->get_units();
  if (def_units == nullptr && lef_units == nullptr) {
    std::cout << "Write UNITS failed..." << std::endl;
    return kDbFail;
  }

  _unit_microns = def_units != nullptr && def_units->get_micron_dbu() > 0 ? def_units->get_micron_dbu() : lef_units->get_micron_dbu();
  if (_unit_microns <= 0) {
    std::cout << "Write UNITS failed..." << std::endl;
    return kDbFail;
  }

  _library->init("ecc", 1.0e-6, 1.0e-6 / _unit_microns);
  return kDbSuccess;
}

string Def2GdsWrite::sanitizeCellName(const string& name)
{
  string base;
  base.reserve(name.size());
  for (char c : name) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc) || c == '_' || c == '-' || c == '.' || c == '$') {
      base.push_back(c);
    } else {
      base.push_back('_');
    }
  }

  if (base.empty()) {
    base = "cell";
  }

  string candidate = base;
  auto [it, inserted] = _used_cell_names.insert(candidate);
  if (inserted) {
    _cell_name_count[base] = 0;
    return candidate;
  }

  int& count = _cell_name_count[base];
  do {
    ++count;
    candidate = base + "_" + std::to_string(count);
  } while (!_used_cell_names.insert(candidate).second);

  return candidate;
}

gdstk::Cell* Def2GdsWrite::createCell(const string& name)
{
  auto* cell = (gdstk::Cell*) gdstk::allocate_clear(sizeof(gdstk::Cell));
  string safe_name = sanitizeCellName(name);
  cell->init(safe_name.c_str());
  _library->cell_array.append(cell);
  return cell;
}

void Def2GdsWrite::addReferenceDefault(gdstk::Cell* child)
{
  if (_top_cell == nullptr || child == nullptr || child == _top_cell) {
    return;
  }

  auto* ref = (gdstk::Reference*) gdstk::allocate_clear(sizeof(gdstk::Reference));
  ref->init(child);
  ref->origin = gdstk::Vec2{0, 0};
  _top_cell->reference_array.append(ref);
}

void Def2GdsWrite::addLabel(gdstk::Cell* gds_cell, const string& text, int32_t x, int32_t y, int32_t layer, int32_t datatype)
{
  if (gds_cell == nullptr) {
    return;
  }

  auto* label = (gdstk::Label*) gdstk::allocate_clear(sizeof(gdstk::Label));
  label->init(text.c_str());
  label->origin = gdstk::Vec2{transDB2Unit(x), transDB2Unit(y)};
  label->anchor = gdstk::Anchor::O;
  label->tag = gdstk::make_tag(layer, datatype);
  gds_cell->label_array.append(label);
}

int32_t Def2GdsWrite::write_version()
{
  IdbDesign* design = _def_service->get_design();
  string version = design == nullptr || design->get_version().empty() ? "5.8" : design->get_version();

  auto* cell = createCell("VERSION");
  addLabel(cell, version, 0, 0, 0, 0);
  addReferenceDefault(cell);
  return kDbSuccess;
}

int32_t Def2GdsWrite::write_design()
{
  IdbDesign* design = _def_service->get_design();
  string design_name = design == nullptr ? "UNKNOWN" : design->get_design_name();

  auto* cell = createCell("Design Name");
  addLabel(cell, design_name, 0, -5, 0, 0);
  addReferenceDefault(cell);
  return kDbSuccess;
}

int32_t Def2GdsWrite::write_die()
{
  IdbLayout* layout = _def_service->get_layout();
  IdbDie* die = layout == nullptr ? nullptr : layout->get_die();
  if (die == nullptr) {
    std::cout << "Write DIE failed..." << std::endl;
    return kDbFail;
  }

  _top_cell = createCell("DIEAREA");
  auto* bbox = die->get_bounding_box();
  if (bbox != nullptr) {
    packRect(_top_cell, bbox->get_low_x(), bbox->get_low_y(), bbox->get_high_x(), bbox->get_high_y(), 0, 2);
  }

  return kDbSuccess;
}

int32_t Def2GdsWrite::write_track_grid()
{
  IdbLayout* layout = _def_service->get_layout();
  IdbTrackGridList* track_grid_list = layout == nullptr ? nullptr : layout->get_track_grid_list();
  IdbDie* die = layout == nullptr ? nullptr : layout->get_die();
  if (track_grid_list == nullptr || die == nullptr) {
    std::cout << "Write Track Grid failed..." << std::endl;
    return kDbFail;
  }

  auto* cell = createCell("TRACKS");
  const int32_t width = die->get_width();
  const int32_t height = die->get_height();

  for (IdbTrackGrid* track_grid : track_grid_list->get_track_grid_list()) {
    IdbTrack* track = track_grid->get_track();
    if (track == nullptr) {
      continue;
    }
    int32_t start = track->get_start();
    int32_t pitch = track->get_pitch();

    for (IdbLayer* layer : track_grid->get_layer_list()) {
      if (track->is_track_direction_y()) {
        for (uint i = 0; i < track_grid->get_track_num(); ++i) {
          int32_t y = start + pitch * i;
          packRect(cell, 0, y, width, y + 1, layer);
        }
      } else {
        for (uint i = 0; i < track_grid->get_track_num(); ++i) {
          int32_t x = start + pitch * i;
          packRect(cell, x, 0, x + 1, height, layer);
        }
      }
    }
  }

  addReferenceDefault(cell);
  return kDbSuccess;
}

void Def2GdsWrite::packLayerShape(gdstk::Cell* gds_cell, IdbLayerShape* layer_shape)
{
  if (gds_cell == nullptr || layer_shape == nullptr) {
    return;
  }

  for (auto rect : layer_shape->get_rect_list()) {
    packRect(gds_cell, rect, layer_shape->get_layer());
  }
}

void Def2GdsWrite::packRect(gdstk::Cell* gds_cell, IdbRect* rect, int32_t layer_id)
{
  if (rect == nullptr) {
    return;
  }

  packRect(gds_cell, rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y(), layer_id, 0);
}

void Def2GdsWrite::packRect(gdstk::Cell* gds_cell, IdbRect* rect, IdbLayer* layer)
{
  if (rect == nullptr) {
    return;
  }

  IdbLayout* layout = _def_service->get_layout();
  auto layer_list = layout == nullptr ? nullptr : layout->get_layers();
  int32_t order = layer == nullptr && layer_list != nullptr ? layer_list->get_bottom_routing_layer()->get_order() : (layer == nullptr ? 0 : layer->get_order());
  packRect(gds_cell, rect, order);
}

void Def2GdsWrite::packRect(gdstk::Cell* gds_cell, int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y, IdbLayer* layer)
{
  IdbLayout* layout = _def_service->get_layout();
  auto layer_list = layout == nullptr ? nullptr : layout->get_layers();
  int32_t order = layer == nullptr && layer_list != nullptr ? layer_list->get_bottom_routing_layer()->get_order() : (layer == nullptr ? 0 : layer->get_order());
  packRect(gds_cell, ll_x, ll_y, ur_x, ur_y, order, 0);
}

void Def2GdsWrite::packRect(gdstk::Cell* gds_cell, int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y, int32_t layer_id,
                            int32_t datatype)
{
  if (gds_cell == nullptr || ll_x == ur_x || ll_y == ur_y) {
    return;
  }

  int32_t x0 = std::min(ll_x, ur_x);
  int32_t y0 = std::min(ll_y, ur_y);
  int32_t x1 = std::max(ll_x, ur_x);
  int32_t y1 = std::max(ll_y, ur_y);

  auto* polygon = (gdstk::Polygon*) gdstk::allocate_clear(sizeof(gdstk::Polygon));
  *polygon = gdstk::rectangle(gdstk::Vec2{transDB2Unit(x0), transDB2Unit(y0)}, gdstk::Vec2{transDB2Unit(x1), transDB2Unit(y1)},
                              gdstk::make_tag(static_cast<uint32_t>(std::max(layer_id, 0)),
                                               static_cast<uint32_t>(std::max(datatype, 0))));
  gds_cell->polygon_array.append(polygon);
}

void Def2GdsWrite::packVia(gdstk::Cell* gds_cell, IdbVia* via)
{
  if (gds_cell == nullptr || via == nullptr) {
    return;
  }

  auto top_layer_shape = via->get_top_layer_shape();
  packLayerShape(gds_cell, &top_layer_shape);

  auto cut_layer_shape = via->get_cut_layer_shape();
  packLayerShape(gds_cell, &cut_layer_shape);

  auto bottom_layer_shape = via->get_bottom_layer_shape();
  packLayerShape(gds_cell, &bottom_layer_shape);
}

void Def2GdsWrite::packPin(gdstk::Cell* gds_cell, IdbPin* pin)
{
  if (gds_cell == nullptr || pin == nullptr || pin->get_term() == nullptr) {
    return;
  }

  if (pin->get_term()->is_port_exist()) {
    for (auto layer_shape : pin->get_port_box_list()) {
      packLayerShape(gds_cell, layer_shape);
    }

    for (auto via : pin->get_via_list()) {
      packVia(gds_cell, via);
    }
  }
}

void Def2GdsWrite::packSegment(gdstk::Cell* gds_cell, IdbLayerRouting* routing_layer, IdbCoordinate<int32_t>* point_1,
                               IdbCoordinate<int32_t>* point_2, int32_t width)
{
  if (gds_cell == nullptr || routing_layer == nullptr || point_1 == nullptr || point_2 == nullptr) {
    return;
  }

  int32_t routing_width = width > 0 ? width : routing_layer->get_width();

  int32_t ll_x = 0;
  int32_t ll_y = 0;
  int32_t ur_x = 0;
  int32_t ur_y = 0;
  if (point_1->get_y() == point_2->get_y()) {
    ll_x = std::min(point_1->get_x(), point_2->get_x()) - routing_width / 2;
    ll_y = std::min(point_1->get_y(), point_2->get_y()) - routing_width / 2;
    ur_x = std::max(point_1->get_x(), point_2->get_x()) + routing_width / 2;
    ur_y = ll_y + routing_width;
  } else if (point_1->get_x() == point_2->get_x()) {
    ll_x = std::min(point_1->get_x(), point_2->get_x()) - routing_width / 2;
    ll_y = std::min(point_1->get_y(), point_2->get_y()) - routing_width / 2;
    ur_x = ll_x + routing_width;
    ur_y = std::max(point_1->get_y(), point_2->get_y()) + routing_width / 2;
  } else {
    std::cout << "Error...Regular segment only support horizontal & vertical direction... " << std::endl;
    return;
  }

  packRect(gds_cell, ll_x, ll_y, ur_x, ur_y, routing_layer);
}

int32_t Def2GdsWrite::write_via()
{
  return kDbSuccess;
}

int32_t Def2GdsWrite::write_row()
{
  IdbLayout* layout = _def_service->get_layout();
  IdbRows* rows = layout == nullptr ? nullptr : layout->get_rows();
  if (rows == nullptr) {
    std::cout << "Write ROWS failed..." << std::endl;
    return kDbFail;
  }

  auto* cell = createCell("Rows");
  for (IdbRow* row : rows->get_row_list()) {
    packRect(cell, row->get_bounding_box(), 0);
  }

  addReferenceDefault(cell);
  return kDbSuccess;
}

int32_t Def2GdsWrite::write_component()
{
  IdbDesign* design = _def_service->get_design();
  IdbInstanceList* instance_list = design == nullptr ? nullptr : design->get_instance_list();
  if (instance_list == nullptr || instance_list->get_num() == 0) {
    std::cout << "Write COMPONENTS failed..." << std::endl;
    return kDbFail;
  }

  int x = 0;
  int max_num = instance_list->get_num();
  for (IdbInstance* instance : instance_list->get_instance_list()) {
    if (instance == nullptr) {
      continue;
    }

    auto* cell = createCell("Instance_" + instance->get_name());
    addReferenceDefault(cell);

    packRect(cell, instance->get_bounding_box(), 0);

    if (instance->get_pin_list() != nullptr) {
      for (auto pin : instance->get_pin_list()->get_pin_list()) {
        packPin(cell, pin);
      }
    }

    for (auto obs_shape : instance->get_obs_box_list()) {
      packLayerShape(cell, obs_shape);
    }

    x++;
    if (x % 1000 == 0) {
      std::cout << "Write COMPONENTS. " << x << " / " << max_num << std::endl;
    }
  }

  std::cout << "Write COMPONENTS success. " << max_num << " / " << max_num << std::endl;
  return kDbSuccess;
}

int32_t Def2GdsWrite::write_pin()
{
  IdbDesign* design = _def_service->get_design();
  IdbPins* pin_list = design == nullptr ? nullptr : design->get_io_pin_list();
  if (pin_list == nullptr) {
    std::cout << "Write PINS failed..." << std::endl;
    return kDbFail;
  }

  auto* cell = createCell("PINS");
  for (IdbPin* pin : pin_list->get_pin_list()) {
    packPin(cell, pin);
  }

  addReferenceDefault(cell);
  return kDbSuccess;
}

int32_t Def2GdsWrite::write_blockage()
{
  IdbDesign* design = _def_service->get_design();
  IdbBlockageList* blockage_list = design == nullptr ? nullptr : design->get_blockage_list();
  if (blockage_list == nullptr || blockage_list->get_num() == 0) {
    std::cout << "Write blocakge failed..." << std::endl;
    return kDbFail;
  }

  for (IdbBlockage* blockage : blockage_list->get_blockage_list()) {
    auto* cell = createCell("Blockage_" + blockage->get_instance_name());
    addReferenceDefault(cell);

    if (blockage->is_palcement_blockage()) {
      for (auto idb_rect : blockage->get_rect_list()) {
        packRect(cell, idb_rect, ((IdbPlacementBlockage*) blockage)->get_layer());
      }
    } else {
      for (auto idb_rect : blockage->get_rect_list()) {
        packRect(cell, idb_rect, ((IdbRoutingBlockage*) blockage)->get_layer());
      }
    }
  }

  return kDbSuccess;
}

int32_t Def2GdsWrite::write_specialnet_wire_segment_points(gdstk::Cell* gds_cell, IdbSpecialWireSegment* segment)
{
  if (segment->get_point_list().size() < _POINT_MAX_) {
    std::cout << "Specialnet wire points are less than 2..." << std::endl;
    return kDbFail;
  }

  if (segment->get_point_num() >= _POINT_MAX_) {
    IdbLayerRouting* routing_layer = dynamic_cast<IdbLayerRouting*>(segment->get_layer());
    int32_t routing_width = segment->get_route_width() == 0 ? routing_layer->get_width() : segment->get_route_width();

    IdbCoordinate<int32_t>* point_1 = segment->get_point_start();
    IdbCoordinate<int32_t>* point_2 = segment->get_point_second();

    packSegment(gds_cell, routing_layer, point_1, point_2, routing_width);
  }

  return kDbSuccess;
}

int32_t Def2GdsWrite::write_specialnet_wire_segment_via(gdstk::Cell* gds_cell, IdbSpecialWireSegment* segment)
{
  if (segment->get_point_list().size() <= 0 || segment->get_layer() == nullptr || segment->get_via() == nullptr) {
    std::cout << "No net wire segment via..." << std::endl;
    return kDbFail;
  }

  packVia(gds_cell, segment->get_via());

  if (segment->get_point_list().size() >= _POINT_MAX_) {
    return write_specialnet_wire_segment_points(gds_cell, segment);
  }

  return kDbSuccess;
}

int32_t Def2GdsWrite::write_specialnet_wire_segment_rect(gdstk::Cell* gds_cell, IdbSpecialWireSegment* segment)
{
  if (segment->get_layer() == nullptr || segment->get_delta_rect() == nullptr) {
    std::cout << "No special wire segment rect..." << std::endl;
    return kDbFail;
  }

  IdbRect* rect = new IdbRect(segment->get_delta_rect());
  packRect(gds_cell, rect, segment->get_layer());
  delete rect;

  return kDbSuccess;
}

int32_t Def2GdsWrite::write_specialnet_wire_segment(gdstk::Cell* gds_cell, IdbSpecialWireSegment* segment)
{
  if (segment == nullptr) {
    return kDbFail;
  }

  if (segment->is_via()) {
    return write_specialnet_wire_segment_via(gds_cell, segment);
  }
  if (segment->is_rect()) {
    return write_specialnet_wire_segment_rect(gds_cell, segment);
  }
  return write_specialnet_wire_segment_points(gds_cell, segment);
}

int32_t Def2GdsWrite::write_specialnet_wire(gdstk::Cell* gds_cell, IdbSpecialWire* wire)
{
  if (wire == nullptr) {
    return kDbFail;
  }

  for (IdbSpecialWireSegment* segment : wire->get_segment_list()) {
    write_specialnet_wire_segment(gds_cell, segment);
  }

  return kDbSuccess;
}

int32_t Def2GdsWrite::write_special_net()
{
  IdbSpecialNetList* special_net_list = _def_service->get_design()->get_special_net_list();
  if (special_net_list == nullptr || special_net_list->get_num() == 0) {
    std::cout << "No SPECIALNETS..." << std::endl;
    return kDbFail;
  }

  for (IdbSpecialNet* special_net : special_net_list->get_net_list()) {
    if (special_net == nullptr) {
      continue;
    }
    auto* cell = createCell(special_net->get_net_name());
    addReferenceDefault(cell);

    for (IdbSpecialWire* wire : special_net->get_wire_list()->get_wire_list()) {
      write_specialnet_wire(cell, wire);
    }
  }

  return kDbSuccess;
}

int32_t Def2GdsWrite::write_net()
{
  IdbDesign* design = _def_service->get_design();
  IdbNetList* net_list = design == nullptr ? nullptr : design->get_net_list();
  if (net_list == nullptr) {
    std::cout << "No NET To Write..." << std::endl;
    return kDbFail;
  }

  if (net_list->get_num() == 0) {
    std::cout << "NO NET ..." << std::endl;
    return kDbFail;
  }

  int x = 0;
  int max_num = net_list->get_num();
  for (IdbNet* net : net_list->get_net_list()) {
    if (net == nullptr) {
      continue;
    }
    auto* cell = createCell(net->get_net_name());
    addReferenceDefault(cell);

    if (net->get_wire_list()->get_num() > 0) {
      for (IdbRegularWire* wire : net->get_wire_list()->get_wire_list()) {
        write_net_wire(cell, wire);
      }
    }

    x++;
    if (x % 1000 == 0) {
      std::cout << "Write NETS. " << x << " / " << max_num << std::endl;
    }
  }

  std::cout << "Write NETS success. " << max_num << " / " << max_num << std::endl;
  return kDbSuccess;
}

int32_t Def2GdsWrite::write_net_wire(gdstk::Cell* gds_cell, IdbRegularWire* wire)
{
  if (wire == nullptr) {
    return kDbFail;
  }

  for (IdbRegularWireSegment* segment : wire->get_segment_list()) {
    write_net_wire_segment(gds_cell, segment);
  }

  return kDbSuccess;
}

int32_t Def2GdsWrite::write_net_wire_segment(gdstk::Cell* gds_cell, IdbRegularWireSegment* segment)
{
  if (segment == nullptr) {
    return kDbFail;
  }

  if (segment->is_rect()) {
    return write_net_wire_segment_rect(gds_cell, segment);
  }
  if (segment->is_via()) {
    return write_net_wire_segment_via(gds_cell, segment);
  }
  return write_net_wire_segment_points(gds_cell, segment);
}

int32_t Def2GdsWrite::write_net_wire_segment_points(gdstk::Cell* gds_cell, IdbRegularWireSegment* segment)
{
  if (segment->get_point_list().size() < _POINT_MAX_ || segment->get_layer() == nullptr) {
    return kDbFail;
  }

  IdbLayerRouting* routing_layer = dynamic_cast<IdbLayerRouting*>(segment->get_layer());
  IdbCoordinate<int32_t>* point_1 = segment->get_point_start();
  IdbCoordinate<int32_t>* point_2 = segment->get_point_second();

  packSegment(gds_cell, routing_layer, point_1, point_2);
  return kDbSuccess;
}

int32_t Def2GdsWrite::write_net_wire_segment_via(gdstk::Cell* gds_cell, IdbRegularWireSegment* segment)
{
  if (segment->get_point_list().size() <= 0 || segment->get_layer() == nullptr || segment->get_via_list().size() <= 0) {
    std::cout << "No net wire segment via..." << std::endl;
    return kDbFail;
  }

  packVia(gds_cell, segment->get_via_list().at(_POINT_START_));

  if (segment->get_point_number() >= _POINT_MAX_) {
    return write_net_wire_segment_points(gds_cell, segment);
  }

  return kDbSuccess;
}

int32_t Def2GdsWrite::write_net_wire_segment_rect(gdstk::Cell* gds_cell, IdbRegularWireSegment* segment)
{
  if (segment->get_point_list().size() <= 0 || segment->get_layer() == nullptr || segment->get_delta_rect() == nullptr) {
    std::cout << "No net wire segment rect..." << std::endl;
    return kDbFail;
  }

  IdbCoordinate<int32_t>* coordinate = segment->get_point_start();
  IdbRect* rect_delta = segment->get_delta_rect();

  if (coordinate->get_x() < 0 || coordinate->get_y() < 0) {
    std::cout << "Error...Coordinate error...x = " << coordinate->get_x() << " y = " << coordinate->get_y() << std::endl;
  }

  IdbRect* rect = new IdbRect(rect_delta);
  rect->moveByStep(coordinate->get_x(), coordinate->get_y());
  packRect(gds_cell, rect, segment->get_layer());
  delete rect;

  return kDbSuccess;
}

int32_t Def2GdsWrite::write_gcell_grid()
{
  return kDbSuccess;
}

int32_t Def2GdsWrite::write_region()
{
  return kDbSuccess;
}

int32_t Def2GdsWrite::write_slot()
{
  return kDbSuccess;
}

int32_t Def2GdsWrite::write_group()
{
  return kDbSuccess;
}

int32_t Def2GdsWrite::write_fill()
{
  IdbDesign* design = _def_service->get_design();
  IdbFillList* fill_list = design == nullptr ? nullptr : design->get_fill_list();
  if (fill_list == nullptr || fill_list->get_num_fill() == 0) {
    std::cout << "No FILLS ..." << std::endl;
    return kDbFail;
  }

  auto* cell = createCell("Fills");

  for (IdbFill* fill : fill_list->get_fill_list()) {
    if (fill == nullptr) {
      continue;
    }

    if (fill->get_layer() != nullptr) {
      for (IdbRect* rect : fill->get_layer()->get_rect_list()) {
        packRect(cell, rect, fill->get_layer()->get_layer());
      }
    }

    if (fill->get_via() != nullptr && fill->get_via()->get_via() != nullptr) {
      IdbVia* via = fill->get_via()->get_via()->clone();
      for (IdbCoordinate<int32_t>* point : fill->get_via()->get_coordinate_list()) {
        via->set_coordinate(point);
        packVia(cell, via);
      }
      delete via;
    }
  }

  addReferenceDefault(cell);
  return kDbSuccess;
}

std::pair<int32_t, int32_t> Def2GdsWrite::get_pdn_layer_order_range()
{
  auto* design = _def_service->get_design();
  auto* layout = _def_service->get_layout();
  auto* layers = layout == nullptr ? nullptr : layout->get_layers();
  auto* pdn_list = design == nullptr ? nullptr : design->get_special_net_list();
  if (layers == nullptr || pdn_list == nullptr || pdn_list->get_num() == 0) {
    return {0, -1};
  }

  int min_layer = layers->get_layers_num() - 1;
  int max_layer = 0;
  bool has_routing_data = false;

  for (auto* net : pdn_list->get_net_list()) {
    if (net == nullptr || net->get_wire_list() == nullptr) {
      continue;
    }

    for (auto* wire : net->get_wire_list()->get_wire_list()) {
      if (wire == nullptr) {
        continue;
      }

      for (auto* segment : wire->get_segment_list()) {
        if (segment == nullptr || (segment->is_via() && segment->get_point_num() < 2) || segment->get_layer() == nullptr) {
          continue;
        }

        const int order = segment->get_layer()->get_order();
        min_layer = std::min(min_layer, order);
        max_layer = std::max(max_layer, order);
        has_routing_data = true;
      }
    }
  }

  return has_routing_data ? std::make_pair(min_layer, max_layer) : std::make_pair(0, -1);
}

int32_t Def2GdsWrite::write_harden_macro_pins()
{
  auto* design = _def_service->get_design();
  auto* layout = _def_service->get_layout();
  if (design == nullptr || layout == nullptr) {
    std::cout << "Write harden macro pins failed..." << std::endl;
    return kDbFail;
  }

  auto* pin_list = design->get_io_pin_list();
  auto* layers = layout->get_layers();
  auto* pdn_list = design->get_special_net_list();

  auto* cell = createCell("HARDEN_MACRO_PINS");

  if (pin_list != nullptr) {
    for (auto* pin : pin_list->get_pin_list()) {
      if (pin != nullptr) {
        packPin(cell, pin);
      }
    }
  }

  auto get_top_pdn_rect = [&](IdbLayer* layer, bool is_power) -> std::vector<IdbRect> {
    std::vector<IdbRect> pdn_rects;
    auto* routing_layer = dynamic_cast<IdbLayerRouting*>(layer);
    if (routing_layer == nullptr || pdn_list == nullptr) {
      return pdn_rects;
    }

    for (auto* net : pdn_list->get_net_list()) {
      if (net == nullptr || net->get_wire_list() == nullptr) {
        continue;
      }

      if ((net->is_vdd() && !is_power) || (net->is_vss() && is_power)) {
        continue;
      }

      for (auto* wire : net->get_wire_list()->get_wire_list()) {
        if (wire == nullptr) {
          continue;
        }

        for (auto* segment : wire->get_segment_list()) {
          if (segment == nullptr || (segment->is_via() && segment->get_point_num() < 2) || segment->get_layer() != layer) {
            continue;
          }

          auto* point_1 = segment->get_point_start();
          auto* point_2 = segment->get_point_second();
          if (point_1 == nullptr || point_2 == nullptr) {
            continue;
          }

          const int32_t routing_width = segment->get_route_width() == 0 ? routing_layer->get_width() : segment->get_route_width();

          int32_t ll_x = 0;
          int32_t ll_y = 0;
          int32_t ur_x = 0;
          int32_t ur_y = 0;

          if (point_1->get_y() == point_2->get_y()) {
            ll_x = std::min(point_1->get_x(), point_2->get_x());
            ll_y = std::min(point_1->get_y(), point_2->get_y()) - routing_width / 2;
            ur_x = std::max(point_1->get_x(), point_2->get_x());
            ur_y = ll_y + routing_width;
          } else if (point_1->get_x() == point_2->get_x()) {
            ll_x = std::min(point_1->get_x(), point_2->get_x()) - routing_width / 2;
            ll_y = std::min(point_1->get_y(), point_2->get_y());
            ur_x = ll_x + routing_width;
            ur_y = std::max(point_1->get_y(), point_2->get_y());
          } else {
            continue;
          }

          pdn_rects.emplace_back(ll_x, ll_y, ur_x, ur_y);
        }
      }
    }

    return pdn_rects;
  };

  auto layer_pair = get_pdn_layer_order_range();
  if (layers != nullptr && layer_pair.first <= layer_pair.second) {
    auto* top_layer = layers->find_layer_by_order(layer_pair.second);
    if (top_layer != nullptr) {
      auto top_vdd = get_top_pdn_rect(top_layer, true);
      auto top_vss = get_top_pdn_rect(top_layer, false);

      for (auto& rect : top_vdd) {
        packRect(cell, &rect, top_layer);
      }

      for (auto& rect : top_vss) {
        packRect(cell, &rect, top_layer);
      }
    }
  }

  if (cell->polygon_array.count == 0 && cell->label_array.count == 0) {
    return kDbSuccess;
  }

  addReferenceDefault(cell);
  return kDbSuccess;
}

int32_t Def2GdsWrite::write_harden_macro_obs()
{
  auto* design = _def_service->get_design();
  auto* layout = _def_service->get_layout();
  auto* die = layout == nullptr ? nullptr : layout->get_die();
  auto* layers = layout == nullptr ? nullptr : layout->get_layers();
  auto* io_pins = design == nullptr ? nullptr : design->get_io_pin_list();
  auto* pdn_list = design == nullptr ? nullptr : design->get_special_net_list();
  if (design == nullptr || layout == nullptr || die == nullptr || layers == nullptr) {
    std::cout << "Write harden macro obs failed..." << std::endl;
    return kDbFail;
  }

  auto get_obs_rect = [&](IdbLayer* layer, bool is_top) -> std::vector<IdbRect> {
    std::vector<IdbRect> obs_list;
    auto* routing_layer = dynamic_cast<IdbLayerRouting*>(layer);
    auto* die_bbox = die->get_bounding_box();
    if (routing_layer == nullptr || die_bbox == nullptr) {
      return obs_list;
    }

    ieda_solver::GtlPolygon90Set polyset_die;
    ieda_solver::GtlRect die_rect(die_bbox->get_low_x(), die_bbox->get_low_y(), die_bbox->get_high_x(), die_bbox->get_high_y());
    polyset_die += die_rect;

    ieda_solver::GtlPolygon90Set polyset_data;
    if (is_top && pdn_list != nullptr) {
      for (auto* net : pdn_list->get_net_list()) {
        if (net == nullptr || net->get_wire_list() == nullptr) {
          continue;
        }

        for (auto* wire : net->get_wire_list()->get_wire_list()) {
          if (wire == nullptr) {
            continue;
          }

          for (auto* segment : wire->get_segment_list()) {
            if (segment == nullptr || (segment->is_via() && segment->get_point_num() < 2) || segment->get_layer() != layer) {
              continue;
            }

            auto* point_1 = segment->get_point_start();
            auto* point_2 = segment->get_point_second();
            if (point_1 == nullptr || point_2 == nullptr) {
              continue;
            }

            const int32_t routing_width = segment->get_route_width() == 0 ? routing_layer->get_width() : segment->get_route_width();

            int32_t ll_x = 0;
            int32_t ll_y = 0;
            int32_t ur_x = 0;
            int32_t ur_y = 0;

            if (point_1->get_y() == point_2->get_y()) {
              ll_x = std::min(point_1->get_x(), point_2->get_x());
              ll_y = std::min(point_1->get_y(), point_2->get_y()) - routing_width / 2;
              ur_x = std::max(point_1->get_x(), point_2->get_x());
              ur_y = ll_y + routing_width;
            } else if (point_1->get_x() == point_2->get_x()) {
              ll_x = std::min(point_1->get_x(), point_2->get_x()) - routing_width / 2;
              ll_y = std::min(point_1->get_y(), point_2->get_y());
              ur_x = ll_x + routing_width;
              ur_y = std::max(point_1->get_y(), point_2->get_y());
            } else {
              continue;
            }

            int32_t required_size_h = routing_layer->get_spacing(ur_x - ll_x, ur_y - ll_y);
            int32_t required_size_v = routing_layer->get_spacing(ur_y - ll_y, ur_x - ll_x);

            ieda_solver::GtlRect bloat_rect(ll_x, ll_y, ur_x, ur_y);
            gtl::bloat(bloat_rect, gtl::HORIZONTAL, required_size_h);
            gtl::bloat(bloat_rect, gtl::VERTICAL, required_size_v);
            polyset_data += bloat_rect;
          }
        }
      }
    }

    if (io_pins != nullptr) {
      for (auto* pin : io_pins->get_pin_list()) {
        if (pin == nullptr) {
          continue;
        }

        for (auto* layer_shape : pin->get_port_box_list()) {
          if (layer_shape == nullptr || layer_shape->get_layer() != layer) {
            continue;
          }

          for (auto* port_rect : layer_shape->get_rect_list()) {
            if (port_rect == nullptr) {
              continue;
            }

            int32_t required_size_h = routing_layer->get_spacing(port_rect->get_width(), port_rect->get_height());
            int32_t required_size_v = routing_layer->get_spacing(port_rect->get_height(), port_rect->get_width());

            ieda_solver::GtlRect bloat_rect(port_rect->get_low_x(), port_rect->get_low_y(), port_rect->get_high_x(),
                                            port_rect->get_high_y());
            gtl::bloat(bloat_rect, gtl::HORIZONTAL, required_size_h);
            gtl::bloat(bloat_rect, gtl::VERTICAL, required_size_v);
            polyset_data += bloat_rect;
          }
        }
      }
    }

    polyset_die.clean();
    polyset_data.clean();

    auto polyset_obs = polyset_die - polyset_data;
    auto direction = routing_layer->is_horizontal() ? gtl::HORIZONTAL : gtl::VERTICAL;

    std::vector<ieda_solver::GtlRect> obs_rects;
    gtl::get_rectangles(obs_rects, polyset_obs, direction);

    obs_list.reserve(obs_rects.size());
    for (auto& obs_rect : obs_rects) {
      obs_list.emplace_back(gtl::xl(obs_rect), gtl::yl(obs_rect), gtl::xh(obs_rect), gtl::yh(obs_rect));
    }

    return obs_list;
  };

  auto layer_pair = get_pdn_layer_order_range();
  if (layer_pair.first > layer_pair.second) {
    return kDbSuccess;
  }

  auto* cell = createCell("HARDEN_MACRO_OBS");
  for (auto layer_order = layer_pair.first; layer_order <= layer_pair.second; layer_order += 2) {
    auto* layer = layers->find_layer_by_order(layer_order);
    if (layer == nullptr) {
      continue;
    }

    auto obs_rects = get_obs_rect(layer, layer_order == layer_pair.second);
    for (auto& obs_rect : obs_rects) {
      packRect(cell, &obs_rect, layer);
    }
  }

  if (cell->polygon_array.count == 0) {
    return kDbSuccess;
  }

  addReferenceDefault(cell);
  return kDbSuccess;
}

}  // namespace idb
