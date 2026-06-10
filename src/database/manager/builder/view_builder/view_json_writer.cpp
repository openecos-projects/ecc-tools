// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************

#include "view_json_writer.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>

#include "IdbBlockages.h"
#include "IdbCellMaster.h"
#include "IdbCore.h"
#include "IdbDesign.h"
#include "IdbDie.h"
#include "IdbEnum.h"
#include "IdbFill.h"
#include "IdbInstance.h"
#include "IdbLayer.h"
#include "IdbLayerShape.h"
#include "IdbNet.h"
#include "IdbObs.h"
#include "IdbPins.h"
#include "IdbRegularWire.h"
#include "IdbRow.h"
#include "IdbSite.h"
#include "IdbSpecialNet.h"
#include "IdbSpecialWire.h"
#include "IdbTerm.h"
#include "IdbUnits.h"
#include "IdbVias.h"
#include "IdbViaMaster.h"
#include "def_service.h"

namespace idb {
namespace {

constexpr const char* kSchema = "ieda.view.v1";
constexpr int32_t kDefaultTileSize = 200000;
constexpr int32_t kInvalidLow = std::numeric_limits<int32_t>::max();
constexpr int32_t kInvalidHigh = std::numeric_limits<int32_t>::min();

ViewRect invalidBBox()
{
  return {kInvalidLow, kInvalidLow, kInvalidHigh, kInvalidHigh};
}

int32_t coordX(IdbCoordinate<int32_t>* coord)
{
  return coord == nullptr ? 0 : coord->get_x();
}

int32_t coordY(IdbCoordinate<int32_t>* coord)
{
  return coord == nullptr ? 0 : coord->get_y();
}

}  // namespace

ViewJsonWriter::ViewJsonWriter(IdbDefService* def_service)
    : _def_service(def_service),
      _layout(def_service == nullptr ? nullptr : def_service->get_layout()),
      _design(def_service == nullptr ? nullptr : def_service->get_design())
{
}

bool ViewJsonWriter::write(const std::string& output_dir)
{
  if (_def_service == nullptr || _layout == nullptr || _design == nullptr) {
    std::cout << "Write view json failed: def service, layout or design is null." << std::endl;
    return false;
  }

  _output_dir = output_dir;
  _layer_parts.clear();
  _spatial_entries.clear();

  if (!prepareOutputDir(_output_dir) || !buildDictionaries()) {
    return false;
  }

  return writeManifest() && writeMeta() && writeLayers() && writeVias() && writeCellMasters() && writeSites() && writeDie()
         && writeRows() && writeTracks() && writeGCellGrids() && writeInstances() && writeIoPins() && writeRegularNets()
         && writeRegularWires() && writeSpecialNets() && writeSpecialWires() && writeBlockages() && writeFills() && writeRegions()
         && writeObjectIndexes() && writeLayerIndex() && writeSpatialIndex() && writeEditOverlay();
}

bool ViewJsonWriter::prepareOutputDir(const std::filesystem::path& output_dir) const
{
  std::error_code ec;
  std::filesystem::create_directories(output_dir / "tech", ec);
  if (ec) {
    std::cout << "Create view json tech directory failed: " << ec.message() << std::endl;
    return false;
  }

  std::filesystem::create_directories(output_dir / "design", ec);
  if (ec) {
    std::cout << "Create view json design directory failed: " << ec.message() << std::endl;
    return false;
  }

  std::filesystem::create_directories(output_dir / "edits", ec);
  if (ec) {
    std::cout << "Create view json edits directory failed: " << ec.message() << std::endl;
    return false;
  }

  return true;
}

bool ViewJsonWriter::buildDictionaries()
{
  _layer_id_map.clear();
  _via_master_id_map.clear();
  _cell_master_id_map.clear();
  _instance_id_map.clear();
  _io_pin_id_map.clear();
  _regular_net_id_map.clear();
  _special_net_id_map.clear();

  _layer_name_to_id.clear();
  _via_master_name_to_id.clear();
  _cell_master_name_to_id.clear();
  _instance_name_to_id.clear();
  _io_pin_name_to_id.clear();
  _regular_net_name_to_id.clear();
  _special_net_name_to_id.clear();

  int id = 0;
  if (_layout->get_layers() != nullptr) {
    for (auto* layer : _layout->get_layers()->get_layers()) {
      if (layer == nullptr) {
        continue;
      }
      _layer_id_map[layer] = id;
      _layer_name_to_id[layer->get_name()] = id;
      ++id;
    }
  }

  id = 0;
  std::set<IdbViaMaster*> via_masters;
  auto addViaMaster = [&](IdbViaMaster* via_master) {
    if (via_master == nullptr || via_masters.contains(via_master)) {
      return;
    }
    via_masters.insert(via_master);
    _via_master_id_map[via_master] = id;
    _via_master_name_to_id[via_master->get_name()] = id;
    ++id;
  };
  if (_layout->get_via_list() != nullptr) {
    for (auto* via : _layout->get_via_list()->get_via_list()) {
      addViaMaster(via == nullptr ? nullptr : via->get_instance());
    }
  }
  if (_design->get_via_list() != nullptr) {
    for (auto* via : _design->get_via_list()->get_via_list()) {
      addViaMaster(via == nullptr ? nullptr : via->get_instance());
    }
  }
  if (_design->get_io_pin_list() != nullptr) {
    for (auto* pin : _design->get_io_pin_list()->get_pin_list()) {
      if (pin == nullptr) {
        continue;
      }
      for (auto* via : pin->get_via_list()) {
        addViaMaster(via == nullptr ? nullptr : via->get_instance());
      }
    }
  }
  if (_design->get_net_list() != nullptr) {
    for (auto* net : _design->get_net_list()->get_net_list()) {
      if (net == nullptr || net->get_wire_list() == nullptr) {
        continue;
      }
      for (auto* wire : net->get_wire_list()->get_wire_list()) {
        if (wire == nullptr) {
          continue;
        }
        for (auto* segment : wire->get_segment_list()) {
          if (segment == nullptr || !segment->is_via()) {
            continue;
          }
          for (auto* via : segment->get_via_list()) {
            addViaMaster(via == nullptr ? nullptr : via->get_instance());
          }
        }
      }
    }
  }
  if (_design->get_special_net_list() != nullptr) {
    for (auto* net : _design->get_special_net_list()->get_net_list()) {
      if (net == nullptr || net->get_wire_list() == nullptr) {
        continue;
      }
      for (auto* wire : net->get_wire_list()->get_wire_list()) {
        if (wire == nullptr) {
          continue;
        }
        for (auto* segment : wire->get_segment_list()) {
          if (segment != nullptr && segment->is_via()) {
            addViaMaster(segment->get_via() == nullptr ? nullptr : segment->get_via()->get_instance());
          }
        }
      }
    }
  }
  if (_design->get_fill_list() != nullptr) {
    for (auto* fill : _design->get_fill_list()->get_fill_list()) {
      if (fill != nullptr && fill->get_type() == IdbFill::kVia && fill->get_via() != nullptr && fill->get_via()->get_via() != nullptr) {
        addViaMaster(fill->get_via()->get_via()->get_instance());
      }
    }
  }

  id = 0;
  if (_layout->get_cell_master_list() != nullptr) {
    for (auto* master : _layout->get_cell_master_list()->get_cell_master()) {
      if (master == nullptr) {
        continue;
      }
      _cell_master_id_map[master] = id;
      _cell_master_name_to_id[master->get_name()] = id;
      ++id;
    }
  }

  id = 0;
  if (_design->get_instance_list() != nullptr) {
    for (auto* inst : _design->get_instance_list()->get_instance_list()) {
      if (inst == nullptr) {
        continue;
      }
      _instance_id_map[inst] = id;
      _instance_name_to_id[inst->get_name()] = id;
      ++id;
    }
  }

  id = 0;
  if (_design->get_io_pin_list() != nullptr) {
    for (auto* pin : _design->get_io_pin_list()->get_pin_list()) {
      if (pin == nullptr) {
        continue;
      }
      _io_pin_id_map[pin] = id;
      _io_pin_name_to_id[pin->get_pin_name()] = id;
      ++id;
    }
  }

  id = 0;
  if (_design->get_net_list() != nullptr) {
    for (auto* net : _design->get_net_list()->get_net_list()) {
      if (net == nullptr) {
        continue;
      }
      _regular_net_id_map[net] = id;
      _regular_net_name_to_id[net->get_net_name()] = id;
      ++id;
    }
  }

  id = 0;
  if (_design->get_special_net_list() != nullptr) {
    for (auto* net : _design->get_special_net_list()->get_net_list()) {
      if (net == nullptr) {
        continue;
      }
      _special_net_id_map[net] = id;
      _special_net_name_to_id[net->get_net_name()] = id;
      ++id;
    }
  }

  return true;
}

bool ViewJsonWriter::writeManifest()
{
  ViewJson json;
  json["schema"] = kSchema;
  json["format"] = "layout_view_package";
  json["version"] = 1;
  json["design_name"] = _design->get_design_name();
  json["unit"] = ViewJson::object();
  json["unit"]["coord"] = "dbu";
  const int dbu_per_micron = _design->get_units() == nullptr ? 0 : _design->get_units()->get_micron_dbu();
  json["unit"]["dbu_per_micron"] = dbu_per_micron;
  json["unit"]["micron_per_dbu"] = dbu_per_micron <= 0 ? 0.0 : 1.0 / dbu_per_micron;

  IdbRect* die_bbox = _layout->get_die() == nullptr ? nullptr : _layout->get_die()->get_bounding_box();
  json["bbox"] = toRectJson(die_bbox);
  json["files"] = {
      {"meta", "meta.json"},
      {"layers", "tech/layers.json"},
      {"vias", "tech/vias.json"},
      {"cell_masters", "tech/cell_masters.json"},
      {"sites", "tech/sites.json"},
      {"die", "design/die.json"},
      {"rows", "design/rows.json"},
      {"tracks", "design/tracks.json"},
      {"gcell_grids", "design/gcell_grids.json"},
      {"instances", "design/instances.json"},
      {"instances_name_index", "design/instances.name_index.json"},
      {"io_pins", "design/io_pins.json"},
      {"io_pins_name_index", "design/io_pins.name_index.json"},
      {"regular_nets", "design/regular_nets.json"},
      {"regular_nets_name_index", "design/regular_nets.name_index.json"},
      {"regular_wires", "design/regular_wires.json"},
      {"regular_wires_index", "design/regular_wires.index.json"},
      {"layer_index", "design/layer_index.json"},
      {"spatial_index", "design/spatial_index.json"},
      {"special_nets", "design/special_nets.json"},
      {"special_nets_name_index", "design/special_nets.name_index.json"},
      {"special_wires", "design/special_wires.json"},
      {"special_wires_index", "design/special_wires.index.json"},
      {"blockages", "design/blockages.json"},
      {"blockages_index", "design/blockages.index.json"},
      {"fills", "design/fills.json"},
      {"fills_index", "design/fills.index.json"},
      {"regions", "design/regions.json"},
      {"layout_edits", "edits/layout_edits.json"},
  };

  json["capabilities"] = {
      {"stable_id_lookup", true},
      {"name_indexes", true},
      {"layer_index", true},
      {"spatial_index", true},
      {"object_part_indexes", true},
      {"edit_overlay", true},
      {"editable_kinds", {"instances", "io_pins"}},
  };

  json["counts"] = {
      {"layers", static_cast<int>(_layer_id_map.size())},
      {"via_masters", static_cast<int>(_via_master_id_map.size())},
      {"cell_masters", static_cast<int>(_cell_master_id_map.size())},
      {"instances", static_cast<int>(_instance_id_map.size())},
      {"io_pins", static_cast<int>(_io_pin_id_map.size())},
      {"regular_nets", static_cast<int>(_regular_net_id_map.size())},
      {"regular_wire_segments", _design->get_net_list() == nullptr ? 0 : static_cast<int>(_design->get_net_list()->get_segment_num())},
      {"special_nets", static_cast<int>(_special_net_id_map.size())},
      {"special_wire_segments",
       _design->get_special_net_list() == nullptr ? 0 : static_cast<int>(_design->get_special_net_list()->get_segment_num())},
  };

  return writeJsonFile("manifest.json", json);
}

bool ViewJsonWriter::writeMeta()
{
  ViewJson json = makeFileHeader("meta", 1);
  ViewJson data;
  data["design_name"] = _design->get_design_name();
  data["def_version"] = _design->get_version();
  data["unit"] = {{"dbu_per_micron", _design->get_units() == nullptr ? 0 : _design->get_units()->get_micron_dbu()}};
  data["manufacture_grid"] = _layout->get_munufacture_grid();
  data["source"] = {{"exporter", "view_builder"}, {"created_by", "ecc_tools"}};
  json["data"] = data;
  return writeJsonFile("meta.json", json);
}

bool ViewJsonWriter::writeLayers()
{
  ViewJson json = makeFileHeader("layers", static_cast<int>(_layer_id_map.size()));
  json["data"] = ViewJson::array();
  for (auto* layer : _layout->get_layers()->get_layers()) {
    if (layer == nullptr) {
      continue;
    }
    ViewJson item;
    item["id"] = layerId(layer);
    item["name"] = layer->get_name();
    item["type"] = layerTypeName(layer);
    item["order"] = layer->get_order();
    item["direction"] = layerDirectionName(layer);

    if (auto* routing = dynamic_cast<IdbLayerRouting*>(layer)) {
      item["width"] = routing->get_width();
      item["min_width"] = routing->get_min_width();
      item["max_width"] = routing->get_max_width();
      item["pitch"] = {routing->get_pitch_x(), routing->get_pitch_y()};
      item["offset"] = {routing->get_offset_x(), routing->get_offset_y()};
    }

    json["data"].push_back(item);
  }
  json["name_to_id"] = makeNameToIdJson(_layer_name_to_id);
  return writeJsonFile("tech/layers.json", json);
}

bool ViewJsonWriter::writeVias()
{
  ViewJson json = makeFileHeader("via_masters", static_cast<int>(_via_master_id_map.size()));
  json["data"] = ViewJson::array();

  std::vector<std::pair<int, IdbViaMaster*>> masters;
  masters.reserve(_via_master_id_map.size());
  for (const auto& [master, id] : _via_master_id_map) {
    masters.push_back({id, const_cast<IdbViaMaster*>(master)});
  }
  std::sort(masters.begin(), masters.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

  for (auto [id, master] : masters) {
    if (master == nullptr) {
      continue;
    }
    ViewJson item;
    item["id"] = id;
    item["name"] = master->get_name();
    item["type"] = master->is_generate() ? "GENERATED" : "FIXED";
    item["is_default"] = master->is_default();
    item["cut_rows"] = master->get_cut_rows();
    item["cut_cols"] = master->get_cut_cols();
    item["shapes"] = ViewJson::array();

    for (auto* shape : {master->get_bottom_layer_shape(), master->get_cut_layer_shape(), master->get_top_layer_shape()}) {
      if (shape != nullptr && shape->get_layer() != nullptr && shape->get_rect_list_num() > 0) {
        item["shapes"].push_back(toLayerShapeJson(shape));
      }
    }

    if (master->is_generate() && master->get_master_generate() != nullptr) {
      auto* gen = master->get_master_generate();
      item["generate"] = {
          {"rule", gen->get_rule_name()},
          {"cut_size", {gen->get_cut_size_x(), gen->get_cut_size_y()}},
          {"cut_spacing", {gen->get_cut_spcing_x(), gen->get_cut_spcing_y()}},
          {"layers",
           {{"bottom", layerId(gen->get_layer_bottom())}, {"cut", layerId(gen->get_layer_cut())}, {"top", layerId(gen->get_layer_top())}}},
          {"enclosure_bottom", {gen->get_enclosure_bottom_x(), gen->get_enclosure_bottom_y()}},
          {"enclosure_top", {gen->get_enclosure_top_x(), gen->get_enclosure_top_y()}},
          {"origin", {gen->get_original_offset_x(), gen->get_original_offset_y()}},
          {"offset_bottom", {gen->get_offset_bottom_x(), gen->get_offset_bottom_y()}},
          {"offset_top", {gen->get_offset_top_x(), gen->get_offset_top_y()}},
      };
      if (gen->get_patttern() != nullptr) {
        item["generate"]["pattern"] = gen->get_patttern()->get_pattern_string();
      }
    }

    json["data"].push_back(item);
  }

  json["name_to_id"] = makeNameToIdJson(_via_master_name_to_id);
  return writeJsonFile("tech/vias.json", json);
}

bool ViewJsonWriter::writeCellMasters()
{
  ViewJson json = makeFileHeader("cell_masters", static_cast<int>(_cell_master_id_map.size()));
  json["data"] = ViewJson::array();

  for (auto* master : _layout->get_cell_master_list()->get_cell_master()) {
    if (master == nullptr) {
      continue;
    }
    ViewJson item;
    item["id"] = cellMasterId(master);
    item["name"] = master->get_name();
    item["type"] = cellMasterTypeName(master);
    item["origin"] = {master->get_origin_x(), master->get_origin_y()};
    item["size"] = {master->get_width(), master->get_height()};
    item["site"] = master->get_site() == nullptr ? "" : master->get_site()->get_name();
    item["symmetry"] = ViewJson::array();
    if (master->is_symmetry_x()) {
      item["symmetry"].push_back("X");
    }
    if (master->is_symmetry_y()) {
      item["symmetry"].push_back("Y");
    }
    if (master->is_symmetry_R90()) {
      item["symmetry"].push_back("R90");
    }

    item["pins"] = ViewJson::array();
    for (auto* term : master->get_term_list()) {
      if (term == nullptr) {
        continue;
      }
      ViewJson pin;
      pin["name"] = term->get_name();
      pin["direction"] = connectDirectionName(term);
      pin["use"] = connectTypeName(term->get_type());
      pin["ports"] = ViewJson::array();
      for (auto* port : term->get_port_list()) {
        if (port == nullptr) {
          continue;
        }
        for (auto* shape : port->get_layer_shape()) {
          if (shape != nullptr) {
            pin["ports"].push_back(toLayerShapeJson(shape));
          }
        }
      }
      item["pins"].push_back(pin);
    }

    item["obs"] = ViewJson::array();
    for (auto* obs : master->get_obs_list()) {
      if (obs == nullptr) {
        continue;
      }
      for (auto* obs_layer : obs->get_obs_layer_list()) {
        if (obs_layer != nullptr && obs_layer->get_shape() != nullptr) {
          item["obs"].push_back(toLayerShapeJson(obs_layer->get_shape()));
        }
      }
    }

    json["data"].push_back(item);
  }
  json["name_to_id"] = makeNameToIdJson(_cell_master_name_to_id);
  return writeJsonFile("tech/cell_masters.json", json);
}

bool ViewJsonWriter::writeSites()
{
  const int count = _layout->get_sites() == nullptr ? 0 : static_cast<int>(_layout->get_sites()->get_site_list().size());
  ViewJson json = makeFileHeader("sites", count);
  json["data"] = ViewJson::array();
  ViewJson name_to_id = ViewJson::object();
  int id = 0;
  if (_layout->get_sites() != nullptr) {
    for (auto* site : _layout->get_sites()->get_site_list()) {
      if (site == nullptr) {
        continue;
      }
      ViewJson item;
      item["id"] = id;
      item["name"] = site->get_name();
      item["class"] = IdbEnum::GetInstance()->get_site_property()->get_class_name(site->get_site_class());
      item["size"] = {site->get_width(), site->get_height()};
      item["orient"] = orientName(site->get_orient());
      item["symmetry"] = ViewJson::array();
      name_to_id[site->get_name()] = id;
      json["data"].push_back(item);
      ++id;
    }
  }
  json["name_to_id"] = name_to_id;
  json["count"] = id;
  return writeJsonFile("tech/sites.json", json);
}

bool ViewJsonWriter::writeDie()
{
  ViewJson json = makeFileHeader("die", 1);
  ViewJson data;
  data["die_area"] = toRectJson(_layout->get_die() == nullptr ? nullptr : _layout->get_die()->get_bounding_box());
  data["core_area"] = toRectJson(_layout->get_core() == nullptr ? nullptr : _layout->get_core()->get_bounding_box());
  json["data"] = data;
  return writeJsonFile("design/die.json", json);
}

bool ViewJsonWriter::writeRows()
{
  const int count = _layout->get_rows() == nullptr ? 0 : static_cast<int>(_layout->get_rows()->get_row_list().size());
  ViewJson json = makeFileHeader("rows", count);
  json["data"] = ViewJson::array();
  int id = 0;
  if (_layout->get_rows() != nullptr) {
    for (auto* row : _layout->get_rows()->get_row_list()) {
      if (row == nullptr) {
        continue;
      }
      ViewJson item;
      item["id"] = id++;
      item["name"] = row->get_name();
      item["site"] = row->get_site() == nullptr ? "" : row->get_site()->get_name();
      item["origin"] = toPointJson(row->get_original_coordinate());
      item["orient"] = orientName(row->get_orient());
      item["num"] = {row->get_row_num_x(), row->get_row_num_y()};
      item["step"] = {row->get_step_x(), row->get_step_y()};
      item["bbox"] = toRectJson(row->get_bounding_box());
      json["data"].push_back(item);
    }
  }
  json["count"] = id;
  return writeJsonFile("design/rows.json", json);
}

bool ViewJsonWriter::writeTracks()
{
  const int count = _layout->get_track_grid_list() == nullptr ? 0 : static_cast<int>(_layout->get_track_grid_list()->get_track_grid_num());
  ViewJson json = makeFileHeader("tracks", count);
  json["data"] = ViewJson::array();

  int id = 0;
  if (_layout->get_track_grid_list() != nullptr) {
    for (auto* track_grid : _layout->get_track_grid_list()->get_track_grid_list()) {
      auto* track = track_grid == nullptr ? nullptr : track_grid->get_track();
      if (track == nullptr) {
        continue;
      }
      ViewJson item;
      item["id"] = id++;
      item["direction"] = trackDirectionName(track->get_direction());
      item["start"] = track->get_start();
      item["count"] = track_grid->get_track_num();
      item["step"] = track->get_pitch();
      item["width"] = track->get_width();
      item["layer_ids"] = ViewJson::array();
      auto layers = track_grid->get_layer_list();
      for (auto* layer : layers) {
        item["layer_ids"].push_back(layerId(layer));
      }
      item["layer_id"] = layers.empty() ? -1 : layerId(layers.front());
      json["data"].push_back(item);
    }
  }

  json["count"] = id;
  return writeJsonFile("design/tracks.json", json);
}

bool ViewJsonWriter::writeGCellGrids()
{
  const int count
      = _layout->get_gcell_grid_list() == nullptr ? 0 : static_cast<int>(_layout->get_gcell_grid_list()->get_gcell_grid_num());
  ViewJson json = makeFileHeader("gcell_grids", count);
  json["data"] = ViewJson::array();

  int id = 0;
  if (_layout->get_gcell_grid_list() != nullptr) {
    for (auto* gcell_grid : _layout->get_gcell_grid_list()->get_gcell_grid_list()) {
      if (gcell_grid == nullptr) {
        continue;
      }
      ViewJson item;
      item["id"] = id++;
      item["direction"] = trackDirectionName(gcell_grid->get_direction());
      item["start"] = gcell_grid->get_start();
      item["count"] = gcell_grid->get_num();
      item["step"] = gcell_grid->get_space();
      json["data"].push_back(item);
    }
  }

  json["count"] = id;
  return writeJsonFile("design/gcell_grids.json", json);
}

bool ViewJsonWriter::writeInstances()
{
  ViewJson json = makeFileHeader("instances", static_cast<int>(_instance_id_map.size()));
  json["data"] = ViewJson::array();
  for (auto* inst : _design->get_instance_list()->get_instance_list()) {
    if (inst == nullptr) {
      continue;
    }
    ViewJson item;
    item["id"] = instanceId(inst);
    item["name"] = inst->get_name();
    item["master_id"] = cellMasterId(inst->get_cell_master());
    item["origin"] = toPointJson(inst->get_coordinate());
    item["orient"] = orientName(inst->get_orient());
    item["status"] = placementStatusName(inst->get_status());
    item["type"] = instanceTypeName(inst);
    ViewRect bbox = instanceBBox(inst);
    item["bbox"] = toRectJson(bbox);
    item["region"] = inst->get_region() == nullptr ? "" : inst->get_region()->get_name();
    json["data"].push_back(item);
    registerSpatialEntry("instances", "instances.json", instanceId(inst), bbox, {});
  }
  if (!writeJsonFile("design/instances.json", json)) {
    return false;
  }
  return writeNameIndex("design/instances.name_index.json", "instances_name_index", _instance_name_to_id);
}

bool ViewJsonWriter::writeIoPins()
{
  ViewJson json = makeFileHeader("io_pins", static_cast<int>(_io_pin_id_map.size()));
  json["data"] = ViewJson::array();
  for (auto* pin : _design->get_io_pin_list()->get_pin_list()) {
    if (pin == nullptr) {
      continue;
    }
    ViewJson item;
    item["id"] = ioPinId(pin);
    item["name"] = pin->get_pin_name();
    const int regular_net_id = regularNetId(pin->get_net());
    item["net_id"] = regular_net_id < 0 ? ViewJson(nullptr) : ViewJson(regular_net_id);
    const int special_net_id = specialNetId(pin->get_special_net());
    item["special_net_id"] = special_net_id < 0 ? ViewJson(nullptr) : ViewJson(special_net_id);
    item["location"] = toPointJson(pin->get_location());
    item["orient"] = orientName(pin->get_orient());
    ViewRect bbox = toViewRect(pin->get_bounding_box());
    item["ports"] = ViewJson::array();
    std::vector<int> layers;
    for (auto* shape : pin->get_port_box_list()) {
      if (shape != nullptr) {
        item["ports"].push_back(toLayerShapeJson(shape));
        if (shape->get_layer() != nullptr) {
          layers.push_back(layerId(shape->get_layer()));
        }
      }
    }
    item["vias"] = ViewJson::array();
    for (auto* via : pin->get_via_list()) {
      item["vias"].push_back(toViaPlacementJson(via));
      expandBBox(bbox, viaBBox(via));
      for (const int layer_id : viaLayerIds(via)) {
        layers.push_back(layer_id);
      }
    }
    std::sort(layers.begin(), layers.end());
    layers.erase(std::unique(layers.begin(), layers.end()), layers.end());
    item["bbox"] = toRectJson(bbox);
    item["layers"] = layers;
    json["data"].push_back(item);
    for (const int layer_id : layers) {
      PartSummary summary{"io_pins.json", bbox, 1, {layer_id}};
      addLayerPart(layer_id, "io_pins", summary);
    }
    registerSpatialEntry("io_pins", "io_pins.json", ioPinId(pin), bbox, layers);
  }
  if (!writeJsonFile("design/io_pins.json", json)) {
    return false;
  }
  return writeNameIndex("design/io_pins.name_index.json", "io_pins_name_index", _io_pin_name_to_id);
}

bool ViewJsonWriter::writeRegularNets()
{
  ViewJson json = makeFileHeader("regular_nets", static_cast<int>(_regular_net_id_map.size()));
  json["data"] = ViewJson::array();
  for (auto* net : _design->get_net_list()->get_net_list()) {
    if (net == nullptr) {
      continue;
    }
    ViewJson item;
    item["id"] = regularNetId(net);
    item["name"] = net->get_net_name();
    item["use"] = connectTypeName(net->get_connect_type());
    item["source"] = IdbEnum::GetInstance()->get_instance_property()->get_type_str(net->get_source_type());
    item["weight"] = net->get_weight();
    item["bbox"] = toRectJson(net->get_bounding_box());
    item["pins"] = ViewJson::array();
    if (net->get_instance_pin_list() != nullptr) {
      for (auto* pin : net->get_instance_pin_list()->get_pin_list()) {
        if (pin == nullptr || pin->get_instance() == nullptr) {
          continue;
        }
        item["pins"].push_back({{"type", "instance"}, {"inst_id", instanceId(pin->get_instance())}, {"pin", pin->get_pin_name()}});
      }
    }
    if (net->get_io_pins() != nullptr) {
      for (auto* pin : net->get_io_pins()->get_pin_list()) {
        if (pin == nullptr) {
          continue;
        }
        item["pins"].push_back({{"type", "io"}, {"pin_id", ioPinId(pin)}});
      }
    }
    item["wire_segment_count"] = static_cast<int>(net->get_segment_num());
    item["via_count"] = static_cast<int>(net->get_via_num());
    json["data"].push_back(item);
  }
  if (!writeJsonFile("design/regular_nets.json", json)) {
    return false;
  }
  return writeNameIndex("design/regular_nets.name_index.json", "regular_nets_name_index", _regular_net_name_to_id);
}

bool ViewJsonWriter::writeRegularWires()
{
  ViewJson json = makeFileHeader("regular_wires", _design->get_net_list() == nullptr ? 0 : static_cast<int>(_design->get_net_list()->get_segment_num()));
  json["data"] = ViewJson::array();
  int id = 0;
  int wire_index = 0;
  for (auto* net : _design->get_net_list()->get_net_list()) {
    if (net == nullptr || net->get_wire_list() == nullptr) {
      continue;
    }
    wire_index = 0;
    for (auto* wire : net->get_wire_list()->get_wire_list()) {
      if (wire == nullptr) {
        continue;
      }
      int segment_index = 0;
      for (auto* segment : wire->get_segment_list()) {
        if (segment == nullptr) {
          ++segment_index;
          continue;
        }
        if (segment->is_wire()) {
          const int layer_id = layerId(segment->get_layer());
          ViewJson item;
          item["id"] = id++;
          item["net_id"] = regularNetId(net);
          item["wire_index"] = wire_index;
          item["segment_index"] = segment_index;
          item["wire_state"] = wireStateName(wire->get_wire_statement());
          item["kind"] = "path";
          item["layer_id"] = layer_id;
          int32_t width = 0;
          if (auto* routing = dynamic_cast<IdbLayerRouting*>(segment->get_layer())) {
            width = routing->get_width();
          }
          item["width"] = width;
          item["points"] = ViewJson::array();
          std::vector<ViewPoint> points;
          for (auto* point : segment->get_point_list()) {
            points.push_back({coordX(point), coordY(point)});
            item["points"].push_back(toPointJson(points.back()));
          }
          ViewRect bbox = wireBBox(points, width);
          item["bbox"] = toRectJson(bbox);
          item["layers"] = ViewJson::array({layer_id});
          json["data"].push_back(item);
          PartSummary summary{"regular_wires.json", bbox, 1, {layer_id}};
          addLayerPart(layer_id, "regular_wires", summary);
          registerSpatialEntry("regular_wires", "regular_wires.json", id - 1, bbox, {layer_id});
        }
        if (segment->is_via()) {
          for (auto* via : segment->get_via_list()) {
            const int entry_id = id++;
            const ViewRect bbox = viaBBox(via);
            const std::vector<int> layers = viaLayerIds(via);
            ViewJson item;
            item["id"] = entry_id;
            item["net_id"] = regularNetId(net);
            item["wire_index"] = wire_index;
            item["segment_index"] = segment_index;
            item["kind"] = "via";
            item.update(toViaPlacementJson(via));
            item["bbox"] = toRectJson(bbox);
            item["layers"] = layers;
            json["data"].push_back(item);
            registerSpatialEntry("regular_wires", "regular_wires.json", entry_id, bbox, layers);
          }
        }
        if (segment->is_rect()) {
          const int layer_id = layerId(segment->get_layer());
          const ViewRect bbox = toViewRect(segment->get_delta_rect());
          ViewJson item;
          item["id"] = id++;
          item["net_id"] = regularNetId(net);
          item["wire_index"] = wire_index;
          item["segment_index"] = segment_index;
          item["kind"] = "patch";
          item["layer_id"] = layer_id;
          item["rect"] = toRectJson(segment->get_delta_rect());
          item["bbox"] = toRectJson(bbox);
          item["layers"] = ViewJson::array({layer_id});
          json["data"].push_back(item);
          PartSummary summary{"regular_wires.json", bbox, 1, {layer_id}};
          addLayerPart(layer_id, "regular_wires", summary);
          registerSpatialEntry("regular_wires", "regular_wires.json", id - 1, bbox, {layer_id});
        }
        ++segment_index;
      }
      ++wire_index;
    }
  }
  json["count"] = id;
  return writeJsonFile("design/regular_wires.json", json);
}

bool ViewJsonWriter::writeSpecialNets()
{
  ViewJson json = makeFileHeader("special_nets", static_cast<int>(_special_net_id_map.size()));
  json["data"] = ViewJson::array();
  for (auto* net : _design->get_special_net_list()->get_net_list()) {
    if (net == nullptr) {
      continue;
    }
    ViewJson item;
    item["id"] = specialNetId(net);
    item["name"] = net->get_net_name();
    item["use"] = connectTypeName(net->get_connect_type());
    item["source"] = IdbEnum::GetInstance()->get_instance_property()->get_type_str(net->get_source_type());
    item["weight"] = net->get_weight();
    item["bbox"] = toRectJson(_layout->get_die() == nullptr ? nullptr : _layout->get_die()->get_bounding_box());
    item["pins"] = ViewJson::array();
    for (const auto& pin_name : net->get_pin_string_list()) {
      item["pins"].push_back({{"type", "wildcard_instance_pin"}, {"pin", pin_name}});
    }
    if (net->get_instance_pin_list() != nullptr) {
      for (auto* pin : net->get_instance_pin_list()->get_pin_list()) {
        if (pin == nullptr || pin->get_instance() == nullptr) {
          continue;
        }
        item["pins"].push_back({{"type", "instance"}, {"inst_id", instanceId(pin->get_instance())}, {"pin", pin->get_pin_name()}});
      }
    }
    if (net->get_io_pin_list() != nullptr) {
      for (auto* pin : net->get_io_pin_list()->get_pin_list()) {
        if (pin == nullptr) {
          continue;
        }
        item["pins"].push_back({{"type", "io"}, {"pin_id", ioPinId(pin)}});
      }
    }
    item["wire_segment_count"] = net->get_segment_num();
    item["via_count"] = net->get_via_num();
    json["data"].push_back(item);
  }
  if (!writeJsonFile("design/special_nets.json", json)) {
    return false;
  }
  return writeNameIndex("design/special_nets.name_index.json", "special_nets_name_index", _special_net_name_to_id);
}

bool ViewJsonWriter::writeSpecialWires()
{
  const int segment_count
      = _design->get_special_net_list() == nullptr ? 0 : static_cast<int>(_design->get_special_net_list()->get_segment_num());
  ViewJson json = makeFileHeader("special_wires", segment_count);
  json["data"] = ViewJson::array();
  int id = 0;
  for (auto* net : _design->get_special_net_list()->get_net_list()) {
    if (net == nullptr || net->get_wire_list() == nullptr) {
      continue;
    }
    int wire_index = 0;
    for (auto* wire : net->get_wire_list()->get_wire_list()) {
      if (wire == nullptr) {
        continue;
      }
      int segment_index = 0;
      for (auto* segment : wire->get_segment_list()) {
        if (segment == nullptr) {
          ++segment_index;
          continue;
        }
        if ((segment->is_line() || segment->get_point_num() >= 2) && !segment->is_via()) {
          const int layer_id = layerId(segment->get_layer());
          ViewJson item;
          item["id"] = id++;
          item["special_net_id"] = specialNetId(net);
          item["wire_index"] = wire_index;
          item["segment_index"] = segment_index;
          item["wire_state"] = wireStateName(wire->get_wire_state());
          item["kind"] = "path";
          item["shape"] = wireShapeName(segment->get_shape_type());
          item["layer_id"] = layer_id;
          item["width"] = segment->get_route_width();
          item["style"] = segment->get_style();
          item["points"] = ViewJson::array();
          std::vector<ViewPoint> points;
          for (auto* point : segment->get_point_list()) {
            points.push_back({coordX(point), coordY(point)});
            item["points"].push_back(toPointJson(points.back()));
          }
          ViewRect bbox = wireBBox(points, segment->get_route_width());
          item["bbox"] = toRectJson(bbox);
          item["layers"] = ViewJson::array({layer_id});
          json["data"].push_back(item);
          PartSummary summary{"special_wires.json", bbox, 1, {layer_id}};
          addLayerPart(layer_id, "special_wires", summary);
          registerSpatialEntry("special_wires", "special_wires.json", id - 1, bbox, {layer_id});
        }
        if (segment->is_via()) {
          const int entry_id = id++;
          const ViewRect bbox = viaBBox(segment->get_via());
          const std::vector<int> layers = viaLayerIds(segment->get_via());
          ViewJson item;
          item["id"] = entry_id;
          item["special_net_id"] = specialNetId(net);
          item["wire_index"] = wire_index;
          item["segment_index"] = segment_index;
          item["kind"] = "via";
          item.update(toViaPlacementJson(segment->get_via()));
          item["bbox"] = toRectJson(bbox);
          item["layers"] = layers;
          json["data"].push_back(item);
          registerSpatialEntry("special_wires", "special_wires.json", entry_id, bbox, layers);
        }
        if (segment->is_rect()) {
          const int layer_id = layerId(segment->get_layer());
          const ViewRect bbox = toViewRect(segment->get_delta_rect());
          ViewJson item;
          item["id"] = id++;
          item["special_net_id"] = specialNetId(net);
          item["wire_index"] = wire_index;
          item["segment_index"] = segment_index;
          item["kind"] = "patch";
          item["layer_id"] = layer_id;
          item["rect"] = toRectJson(segment->get_delta_rect());
          item["bbox"] = toRectJson(bbox);
          item["layers"] = ViewJson::array({layer_id});
          json["data"].push_back(item);
          PartSummary summary{"special_wires.json", bbox, 1, {layer_id}};
          addLayerPart(layer_id, "special_wires", summary);
          registerSpatialEntry("special_wires", "special_wires.json", id - 1, bbox, {layer_id});
        }
        ++segment_index;
      }
      ++wire_index;
    }
  }
  json["count"] = id;
  return writeJsonFile("design/special_wires.json", json);
}

bool ViewJsonWriter::writeBlockages()
{
  const int count = _design->get_blockage_list() == nullptr ? 0 : _design->get_blockage_list()->get_num();
  ViewJson json = makeFileHeader("blockages", count);
  json["data"] = ViewJson::array();
  int id = 0;
  if (_design->get_blockage_list() != nullptr) {
    for (auto* blockage : _design->get_blockage_list()->get_blockage_list()) {
      if (blockage == nullptr) {
        continue;
      }
      ViewJson item;
      item["id"] = id++;
      item["type"] = blockage->is_routing_blockage() ? "ROUTING" : "PLACEMENT";
      item["rects"] = ViewJson::array();
      ViewRect bbox = invalidBBox();
      for (auto* rect : blockage->get_rect_list()) {
        item["rects"].push_back(toRectJson(rect));
        expandBBox(bbox, toViewRect(rect));
      }
      std::vector<int> layers;
      if (auto* routing = dynamic_cast<IdbRoutingBlockage*>(blockage)) {
        const int layer_id = layerId(routing->get_layer());
        item["layer_id"] = layer_id;
        item["except_pgnet"] = routing->is_except_pgnet();
        layers.push_back(layer_id);
        PartSummary summary{"blockages.json", bbox, 1, {layer_id}};
        addLayerPart(layer_id, "blockages", summary);
      }
      item["bbox"] = toRectJson(isBBoxValid(bbox) ? bbox : ViewRect{0, 0, 0, 0});
      item["layers"] = layers;
      json["data"].push_back(item);
      registerSpatialEntry("blockages", "blockages.json", id - 1, bbox, layers);
    }
  }
  json["count"] = id;
  return writeJsonFile("design/blockages.json", json);
}

bool ViewJsonWriter::writeFills()
{
  const int count = _design->get_fill_list() == nullptr ? 0 : _design->get_fill_list()->get_num_fill();
  ViewJson json = makeFileHeader("fills", count);
  json["data"] = ViewJson::array();
  int id = 0;
  if (_design->get_fill_list() != nullptr) {
    for (auto* fill : _design->get_fill_list()->get_fill_list()) {
      if (fill == nullptr) {
        continue;
      }
      if (fill->get_type() == IdbFill::kLayer && fill->get_layer() != nullptr) {
        for (auto* rect : fill->get_layer()->get_rect_list()) {
          const int entry_id = id++;
          const int layer_id = layerId(fill->get_layer()->get_layer());
          const ViewRect bbox = toViewRect(rect);
          ViewJson item;
          item["id"] = entry_id;
          item["kind"] = "rect";
          item["layer_id"] = layer_id;
          item["rect"] = toRectJson(rect);
          item["bbox"] = toRectJson(bbox);
          item["layers"] = ViewJson::array({layer_id});
          json["data"].push_back(item);
          PartSummary summary{"fills.json", bbox, 1, {layer_id}};
          addLayerPart(layer_id, "fills", summary);
          registerSpatialEntry("fills", "fills.json", entry_id, bbox, {layer_id});
        }
      } else if (fill->get_type() == IdbFill::kVia && fill->get_via() != nullptr && fill->get_via()->get_via() != nullptr) {
        auto* via_master = fill->get_via()->get_via()->get_instance();
        for (auto* coord : fill->get_via()->get_coordinate_list()) {
          const int entry_id = id++;
          const ViewPoint origin{coordX(coord), coordY(coord)};
          const ViewRect bbox = viaBBox(via_master, origin);
          const std::vector<int> layers = viaLayerIds(via_master);
          ViewJson item;
          item["id"] = entry_id;
          item["kind"] = "via";
          item["via_master_id"] = viaMasterId(via_master);
          item["origin"] = toPointJson(coord);
          item["bbox"] = toRectJson(bbox);
          item["layers"] = layers;
          json["data"].push_back(item);
          for (const int layer_id : layers) {
            PartSummary summary{"fills.json", bbox, 1, {layer_id}};
            addLayerPart(layer_id, "fills", summary);
          }
          registerSpatialEntry("fills", "fills.json", entry_id, bbox, layers);
        }
      }
    }
  }
  json["count"] = id;
  return writeJsonFile("design/fills.json", json);
}

bool ViewJsonWriter::writeRegions()
{
  const int count = _design->get_region_list() == nullptr ? 0 : static_cast<int>(_design->get_region_list()->get_num());
  ViewJson json = makeFileHeader("regions", count);
  json["data"] = ViewJson::array();
  int id = 0;
  if (_design->get_region_list() != nullptr) {
    for (auto* region : _design->get_region_list()->get_region_list()) {
      if (region == nullptr) {
        continue;
      }
      ViewJson item;
      item["id"] = id++;
      item["name"] = region->get_name();
      item["type"] = IdbEnum::GetInstance()->get_region_property()->get_name(region->get_type());
      item["rects"] = ViewJson::array();
      for (auto* rect : region->get_boundary()) {
        item["rects"].push_back(toRectJson(rect));
      }
      item["instances"] = ViewJson::array();
      for (auto* inst : region->get_instance_list()) {
        const int inst_id = instanceId(inst);
        if (inst_id >= 0) {
          item["instances"].push_back(inst_id);
        }
      }
      json["data"].push_back(item);
    }
  }
  json["count"] = id;
  return writeJsonFile("design/regions.json", json);
}

bool ViewJsonWriter::writeObjectIndexes()
{
  return writeJsonFile("design/regular_wires.index.json", makeObjectIndexJson("regular_wires", "regular_wires.json"))
         && writeJsonFile("design/special_wires.index.json", makeObjectIndexJson("special_wires", "special_wires.json"))
         && writeJsonFile("design/blockages.index.json", makeObjectIndexJson("blockages", "blockages.json"))
         && writeJsonFile("design/fills.index.json", makeObjectIndexJson("fills", "fills.json"));
}

bool ViewJsonWriter::writeLayerIndex()
{
  ViewJson json;
  json["schema"] = kSchema;
  json["kind"] = "layer_index";
  json["version"] = 1;
  json["layers"] = ViewJson::array();

  for (auto* layer : _layout->get_layers()->get_layers()) {
    if (layer == nullptr) {
      continue;
    }
    const int id = layerId(layer);
    ViewJson item;
    item["layer_id"] = id;
    for (const std::string& object_kind : {"regular_wires", "special_wires", "io_pins", "blockages", "fills"}) {
      item[object_kind] = ViewJson::array();
      auto layer_iter = _layer_parts.find(id);
      if (layer_iter != _layer_parts.end()) {
        auto kind_iter = layer_iter->second.find(object_kind);
        if (kind_iter != layer_iter->second.end()) {
          for (const auto& summary : kind_iter->second) {
            item[object_kind].push_back(makeLayerPartJson(summary));
          }
        }
      }
    }
    json["layers"].push_back(item);
  }

  return writeJsonFile("design/layer_index.json", json);
}

bool ViewJsonWriter::writeSpatialIndex()
{
  ViewJson json;
  json["schema"] = kSchema;
  json["kind"] = "spatial_index";
  json["version"] = 1;

  ViewRect bbox = invalidBBox();
  if (_layout->get_die() != nullptr) {
    bbox = toViewRect(_layout->get_die()->get_bounding_box());
  }
  if (!isBBoxValid(bbox)) {
    bbox = {0, 0, 0, 0};
  }

  const int32_t tile_size = std::max<int32_t>(1, kDefaultTileSize);
  json["origin"] = {bbox.lx, bbox.ly};
  json["tile_size"] = {tile_size, tile_size};
  json["bbox"] = toRectJson(bbox);
  json["tiles"] = ViewJson::array();

  struct TileData
  {
    ViewRect bbox;
    std::set<int> layers;
    std::map<std::string, std::vector<const SpatialEntry*>> objects;
  };

  auto tileCoord = [&](int32_t coord, int32_t origin) {
    const int64_t delta = static_cast<int64_t>(coord) - static_cast<int64_t>(origin);
    if (delta >= 0) {
      return static_cast<int>(delta / tile_size);
    }
    return static_cast<int>((delta - tile_size + 1) / tile_size);
  };

  std::map<std::pair<int, int>, TileData> tiles;
  for (const auto& entry : _spatial_entries) {
    if (entry.id < 0 || !isBBoxValid(entry.bbox)) {
      continue;
    }

    const int tx0 = tileCoord(entry.bbox.lx, bbox.lx);
    const int tx1 = tileCoord(entry.bbox.ux, bbox.lx);
    const int ty0 = tileCoord(entry.bbox.ly, bbox.ly);
    const int ty1 = tileCoord(entry.bbox.uy, bbox.ly);
    for (int tx = std::min(tx0, tx1); tx <= std::max(tx0, tx1); ++tx) {
      for (int ty = std::min(ty0, ty1); ty <= std::max(ty0, ty1); ++ty) {
        auto& tile = tiles[{tx, ty}];
        tile.bbox = {bbox.lx + tx * tile_size, bbox.ly + ty * tile_size, bbox.lx + (tx + 1) * tile_size,
                     bbox.ly + (ty + 1) * tile_size};
        for (const int layer_id : entry.layers) {
          if (layer_id >= 0) {
            tile.layers.insert(layer_id);
          }
        }
        tile.objects[entry.object_kind].push_back(&entry);
      }
    }
  }

  for (const auto& [coord, data] : tiles) {
    ViewJson tile;
    tile["tile"] = {coord.first, coord.second};
    tile["bbox"] = toRectJson(data.bbox);
    tile["layers"] = ViewJson::array();
    for (const int layer_id : data.layers) {
      tile["layers"].push_back(layer_id);
    }
    tile["objects"] = ViewJson::object();
    for (const auto& [object_kind, entries] : data.objects) {
      tile["objects"][object_kind] = ViewJson::array();
      for (const auto* entry : entries) {
        ViewJson object;
        object["file"] = entry->file;
        object["id"] = entry->id;
        object["bbox"] = toRectJson(entry->bbox);
        object["layers"] = entry->layers;
        tile["objects"][object_kind].push_back(object);
      }
    }
    json["tiles"].push_back(tile);
  }

  json["count"] = static_cast<int>(tiles.size());
  return writeJsonFile("design/spatial_index.json", json);
}

bool ViewJsonWriter::writeEditOverlay()
{
  ViewJson json;
  json["schema"] = "ieda.view.edit.v1";
  json["kind"] = "layout_edits";
  json["version"] = 1;
  json["base_manifest"] = "../manifest.json";
  json["data"] = ViewJson::array();
  json["dirty"] = {{"instances", ViewJson::array()},
                   {"io_pins", ViewJson::array()},
                   {"regular_nets", ViewJson::array()},
                   {"special_nets", ViewJson::array()}};
  json["ops"] = {"move_instance", "orient_instance", "move_io_pin", "orient_io_pin", "set_status", "delete_edit"};
  return writeJsonFile("edits/layout_edits.json", json);
}

bool ViewJsonWriter::writeNameIndex(const std::string& relative_path, const std::string& kind,
                                    const std::unordered_map<std::string, int>& name_to_id) const
{
  ViewJson json;
  json["schema"] = kSchema;
  json["kind"] = kind;
  json["version"] = 1;
  json["count"] = static_cast<int>(name_to_id.size());
  json["name_to_id"] = makeNameToIdJson(name_to_id);
  return writeJsonFile(relative_path, json);
}

bool ViewJsonWriter::writeJsonFile(const std::string& relative_path, const ViewJson& json) const
{
  if (!validateDenseData(relative_path, json)) {
    return false;
  }

  const auto path = _output_dir / relative_path;
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    std::cout << "Create view json directory failed: " << path.parent_path() << " " << ec.message() << std::endl;
    return false;
  }

  std::ofstream out(path);
  if (!out.is_open()) {
    std::cout << "Open view json file failed: " << path << std::endl;
    return false;
  }

  out << json.dump(2);
  return true;
}

bool ViewJsonWriter::validateDenseData(const std::string& relative_path, const ViewJson& json) const
{
  if (!json.contains("data") || !json["data"].is_array()) {
    return true;
  }

  const auto& data = json["data"];
  for (size_t index = 0; index < data.size(); ++index) {
    const auto& item = data[index];
    if (!item.is_object() || !item.contains("id")) {
      continue;
    }
    if (!item["id"].is_number_integer() || item["id"].get<int>() != static_cast<int>(index)) {
      std::cout << "Write view json failed: " << relative_path << " data[" << index << "].id is not dense." << std::endl;
      return false;
    }
  }
  return true;
}

ViewJson ViewJsonWriter::makeFileHeader(const std::string& kind, int count) const
{
  ViewJson json;
  json["schema"] = kSchema;
  json["kind"] = kind;
  json["version"] = 1;
  json["count"] = count;
  json["data"] = ViewJson::array();
  return json;
}

ViewJson ViewJsonWriter::makeNameToIdJson(const std::unordered_map<std::string, int>& name_to_id) const
{
  std::vector<std::pair<std::string, int>> sorted(name_to_id.begin(), name_to_id.end());
  std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

  ViewJson json = ViewJson::object();
  for (const auto& [name, id] : sorted) {
    json[name] = id;
  }
  return json;
}

ViewJson ViewJsonWriter::makeObjectIndexJson(const std::string& object_kind, const std::string& file) const
{
  ViewJson json;
  json["schema"] = kSchema;
  json["kind"] = object_kind + "_index";
  json["version"] = 1;
  json["parts"] = ViewJson::array();

  ViewRect bbox = invalidBBox();
  std::set<int> layers;
  int min_id = std::numeric_limits<int>::max();
  int max_id = std::numeric_limits<int>::min();
  int count = 0;

  for (const auto& entry : _spatial_entries) {
    if (entry.object_kind != object_kind || entry.file != file || entry.id < 0 || !isBBoxValid(entry.bbox)) {
      continue;
    }
    expandBBox(bbox, entry.bbox);
    min_id = std::min(min_id, entry.id);
    max_id = std::max(max_id, entry.id);
    ++count;
    for (const int layer_id : entry.layers) {
      if (layer_id >= 0) {
        layers.insert(layer_id);
      }
    }
  }

  json["count"] = count;
  if (count > 0) {
    ViewJson part;
    part["file"] = file;
    part["count"] = count;
    part["id_range"] = {min_id, max_id};
    part["bbox"] = toRectJson(bbox);
    part["layers"] = ViewJson::array();
    for (const int layer_id : layers) {
      part["layers"].push_back(layer_id);
    }
    json["parts"].push_back(part);
  }

  return json;
}

ViewJson ViewJsonWriter::toPointJson(const ViewPoint& point) const
{
  return ViewJson::array({point.x, point.y});
}

ViewJson ViewJsonWriter::toPointJson(const IdbCoordinate<int32_t>* coord) const
{
  return ViewJson::array({coord == nullptr ? 0 : const_cast<IdbCoordinate<int32_t>*>(coord)->get_x(),
                          coord == nullptr ? 0 : const_cast<IdbCoordinate<int32_t>*>(coord)->get_y()});
}

ViewJson ViewJsonWriter::toRectJson(const ViewRect& rect) const
{
  if (!isBBoxValid(rect)) {
    return ViewJson::array({0, 0, 0, 0});
  }
  return ViewJson::array({rect.lx, rect.ly, rect.ux, rect.uy});
}

ViewJson ViewJsonWriter::toRectJson(const IdbRect* rect) const
{
  if (rect == nullptr) {
    return ViewJson::array({0, 0, 0, 0});
  }
  auto* mutable_rect = const_cast<IdbRect*>(rect);
  return ViewJson::array({mutable_rect->get_low_x(), mutable_rect->get_low_y(), mutable_rect->get_high_x(), mutable_rect->get_high_y()});
}

ViewJson ViewJsonWriter::toRectJson(const IdbRect& rect) const
{
  auto mutable_rect = rect;
  return ViewJson::array({mutable_rect.get_low_x(), mutable_rect.get_low_y(), mutable_rect.get_high_x(), mutable_rect.get_high_y()});
}

ViewLayerShape ViewJsonWriter::toViewLayerShape(const IdbLayerShape* shape) const
{
  ViewLayerShape view_shape;
  if (shape == nullptr) {
    return view_shape;
  }
  auto* mutable_shape = const_cast<IdbLayerShape*>(shape);
  view_shape.layer_id = layerId(mutable_shape->get_layer());
  for (auto* rect : mutable_shape->get_rect_list()) {
    view_shape.rects.push_back(toViewRect(rect));
  }
  return view_shape;
}

ViewJson ViewJsonWriter::toLayerShapeJson(const ViewLayerShape& shape) const
{
  ViewJson json;
  json["layer_id"] = shape.layer_id;
  json["rects"] = ViewJson::array();
  for (const auto& rect : shape.rects) {
    json["rects"].push_back(toRectJson(rect));
  }
  return json;
}

ViewJson ViewJsonWriter::toLayerShapeJson(const IdbLayerShape* shape) const
{
  return toLayerShapeJson(toViewLayerShape(shape));
}

ViewJson ViewJsonWriter::toPlacedLayerShapeJson(const IdbLayerShape* local_shape, const PlacedTransform& transform) const
{
  return toLayerShapeJson(ViewGeometryTransform::transformLayerShape(toViewLayerShape(local_shape), transform));
}

ViewRect ViewJsonWriter::toViewRect(const IdbRect* rect) const
{
  if (rect == nullptr) {
    return {0, 0, 0, 0};
  }
  auto* mutable_rect = const_cast<IdbRect*>(rect);
  return {mutable_rect->get_low_x(), mutable_rect->get_low_y(), mutable_rect->get_high_x(), mutable_rect->get_high_y()};
}

ViewRect ViewJsonWriter::toViewRect(const IdbRect& rect) const
{
  auto mutable_rect = rect;
  return {mutable_rect.get_low_x(), mutable_rect.get_low_y(), mutable_rect.get_high_x(), mutable_rect.get_high_y()};
}

ViewRect ViewJsonWriter::instanceBBox(const IdbInstance* inst) const
{
  auto* mutable_inst = const_cast<IdbInstance*>(inst);
  if (mutable_inst == nullptr || mutable_inst->get_cell_master() == nullptr) {
    return {0, 0, 0, 0};
  }
  ViewRect local{0, 0, static_cast<int32_t>(mutable_inst->get_cell_master()->get_width()),
                 static_cast<int32_t>(mutable_inst->get_cell_master()->get_height())};
  return ViewGeometryTransform::transformRect(local, ViewGeometryTransform::fromInstance(inst));
}

ViewRect ViewJsonWriter::wireBBox(const std::vector<ViewPoint>& points, int32_t width) const
{
  if (points.empty()) {
    return {0, 0, 0, 0};
  }
  ViewRect bbox = invalidBBox();
  const int32_t half = width / 2;
  for (const auto& point : points) {
    expandBBox(bbox, {point.x - half, point.y - half, point.x + half, point.y + half});
  }
  return bbox;
}

ViewRect ViewJsonWriter::viaBBox(const IdbVia* via) const
{
  auto* mutable_via = const_cast<IdbVia*>(via);
  if (mutable_via == nullptr || mutable_via->get_instance() == nullptr) {
    return {0, 0, 0, 0};
  }

  ViewRect bbox = invalidBBox();
  for (auto shape : {mutable_via->get_bottom_layer_shape(), mutable_via->get_cut_layer_shape(), mutable_via->get_top_layer_shape()}) {
    expandBBox(bbox, toViewRect(shape.get_bounding_box()));
  }
  return isBBoxValid(bbox) ? bbox : ViewRect{0, 0, 0, 0};
}

ViewRect ViewJsonWriter::viaBBox(const IdbViaMaster* via_master, const ViewPoint& origin) const
{
  auto* mutable_master = const_cast<IdbViaMaster*>(via_master);
  if (mutable_master == nullptr) {
    return {origin.x, origin.y, origin.x, origin.y};
  }

  ViewRect bbox = invalidBBox();
  for (auto* shape : {mutable_master->get_bottom_layer_shape(), mutable_master->get_cut_layer_shape(), mutable_master->get_top_layer_shape()}) {
    if (shape == nullptr) {
      continue;
    }
    for (auto* rect : shape->get_rect_list()) {
      ViewRect placed = toViewRect(rect);
      placed.lx += origin.x;
      placed.ux += origin.x;
      placed.ly += origin.y;
      placed.uy += origin.y;
      expandBBox(bbox, placed);
    }
  }
  return isBBoxValid(bbox) ? bbox : ViewRect{origin.x, origin.y, origin.x, origin.y};
}

std::vector<int> ViewJsonWriter::viaLayerIds(const IdbVia* via) const
{
  auto* mutable_via = const_cast<IdbVia*>(via);
  return mutable_via == nullptr ? std::vector<int>{} : viaLayerIds(mutable_via->get_instance());
}

std::vector<int> ViewJsonWriter::viaLayerIds(const IdbViaMaster* via_master) const
{
  auto* mutable_master = const_cast<IdbViaMaster*>(via_master);
  if (mutable_master == nullptr) {
    return {};
  }

  std::vector<int> layers;
  for (auto* shape : {mutable_master->get_bottom_layer_shape(), mutable_master->get_cut_layer_shape(), mutable_master->get_top_layer_shape()}) {
    if (shape != nullptr && shape->get_layer() != nullptr) {
      layers.push_back(layerId(shape->get_layer()));
    }
  }
  std::sort(layers.begin(), layers.end());
  layers.erase(std::unique(layers.begin(), layers.end()), layers.end());
  return layers;
}

ViewJson ViewJsonWriter::toViaPlacementJson(const IdbVia* via) const
{
  auto* mutable_via = const_cast<IdbVia*>(via);
  ViewJson json;
  json["via_master_id"] = mutable_via == nullptr ? -1 : viaMasterId(mutable_via->get_instance());
  json["origin"] = mutable_via == nullptr ? ViewJson::array({0, 0}) : toPointJson(mutable_via->get_coordinate());
  return json;
}

ViewJson ViewJsonWriter::makeLayerPartJson(const PartSummary& summary) const
{
  return {{"file", summary.file}, {"bbox", toRectJson(summary.bbox)}, {"count", summary.count}};
}

void ViewJsonWriter::addLayerPart(int layer_id, const std::string& object_kind, const PartSummary& summary)
{
  if (layer_id < 0 || summary.count <= 0) {
    return;
  }
  auto& summaries = _layer_parts[layer_id][object_kind];
  if (summaries.empty()) {
    summaries.push_back(summary);
    return;
  }
  auto& aggregate = summaries.front();
  expandBBox(aggregate.bbox, summary.bbox);
  aggregate.count += summary.count;
}

void ViewJsonWriter::registerSpatialEntry(const std::string& object_kind, const std::string& file, int id, const ViewRect& bbox,
                                          const std::vector<int>& layers)
{
  if (id < 0 || object_kind.empty() || file.empty() || !isBBoxValid(bbox)) {
    return;
  }

  SpatialEntry entry;
  entry.object_kind = object_kind;
  entry.file = file;
  entry.id = id;
  entry.bbox = bbox;
  entry.layers = layers;
  std::sort(entry.layers.begin(), entry.layers.end());
  entry.layers.erase(std::unique(entry.layers.begin(), entry.layers.end()), entry.layers.end());
  _spatial_entries.push_back(std::move(entry));
}

void ViewJsonWriter::expandBBox(ViewRect& bbox, const ViewRect& rect) const
{
  if (!isBBoxValid(rect)) {
    return;
  }
  if (!isBBoxValid(bbox)) {
    bbox = rect;
    return;
  }
  bbox.lx = std::min(bbox.lx, rect.lx);
  bbox.ly = std::min(bbox.ly, rect.ly);
  bbox.ux = std::max(bbox.ux, rect.ux);
  bbox.uy = std::max(bbox.uy, rect.uy);
}

bool ViewJsonWriter::isBBoxValid(const ViewRect& bbox) const
{
  return bbox.lx <= bbox.ux && bbox.ly <= bbox.uy && bbox.lx != kInvalidLow && bbox.ux != kInvalidHigh;
}

std::string ViewJsonWriter::orientName(IdbOrient orient) const
{
  switch (orient) {
    case IdbOrient::kN_R0:
      return "N_R0";
    case IdbOrient::kW_R90:
      return "W_R90";
    case IdbOrient::kS_R180:
      return "S_R180";
    case IdbOrient::kE_R270:
      return "E_R270";
    case IdbOrient::kFN_MY:
      return "FN_MY";
    case IdbOrient::kFE_MY90:
      return "FE_MY90";
    case IdbOrient::kFS_MX:
      return "FS_MX";
    case IdbOrient::kFW_MX90:
      return "FW_MX90";
    case IdbOrient::kNone:
    case IdbOrient::kMax:
    default:
      return "N_R0";
  }
}

std::string ViewJsonWriter::layerTypeName(IdbLayer* layer) const
{
  if (layer == nullptr) {
    return "";
  }
  return IdbEnum::GetInstance()->get_layer_property()->get_name(layer->get_type());
}

std::string ViewJsonWriter::layerDirectionName(IdbLayer* layer) const
{
  if (auto* routing = dynamic_cast<IdbLayerRouting*>(layer)) {
    return IdbEnum::GetInstance()->get_layer_property()->get_direction_str(routing->get_direction());
  }
  return "";
}

std::string ViewJsonWriter::cellMasterTypeName(IdbCellMaster* master) const
{
  if (master == nullptr) {
    return "";
  }
  return IdbEnum::GetInstance()->get_cell_property()->get_name(master->get_type());
}

std::string ViewJsonWriter::instanceTypeName(IdbInstance* inst) const
{
  if (inst == nullptr) {
    return "";
  }
  return IdbEnum::GetInstance()->get_instance_property()->get_type_str(inst->get_type());
}

std::string ViewJsonWriter::placementStatusName(IdbPlacementStatus status) const
{
  return IdbEnum::GetInstance()->get_instance_property()->get_status_str(status);
}

std::string ViewJsonWriter::connectDirectionName(IdbTerm* term) const
{
  if (term == nullptr) {
    return "";
  }
  return IdbEnum::GetInstance()->get_connect_property()->get_direction_name(term->get_direction());
}

std::string ViewJsonWriter::connectTypeName(IdbConnectType type) const
{
  return IdbEnum::GetInstance()->get_connect_property()->get_type_name(type);
}

std::string ViewJsonWriter::wireStateName(IdbWiringStatement state) const
{
  return IdbEnum::GetInstance()->get_connect_property()->get_wiring_state_name(state);
}

std::string ViewJsonWriter::wireShapeName(IdbWireShapeType shape) const
{
  return IdbEnum::GetInstance()->get_connect_property()->get_wire_shape_name(shape);
}

std::string ViewJsonWriter::trackDirectionName(IdbTrackDirection direction) const
{
  return direction == IdbTrackDirection::kDirectionX ? "X" : "Y";
}

int ViewJsonWriter::layerId(const IdbLayer* layer) const
{
  auto iter = _layer_id_map.find(layer);
  return iter == _layer_id_map.end() ? -1 : iter->second;
}

int ViewJsonWriter::viaMasterId(const IdbViaMaster* via_master) const
{
  auto iter = _via_master_id_map.find(via_master);
  return iter == _via_master_id_map.end() ? -1 : iter->second;
}

int ViewJsonWriter::cellMasterId(const IdbCellMaster* master) const
{
  auto iter = _cell_master_id_map.find(master);
  return iter == _cell_master_id_map.end() ? -1 : iter->second;
}

int ViewJsonWriter::instanceId(const IdbInstance* inst) const
{
  auto iter = _instance_id_map.find(inst);
  return iter == _instance_id_map.end() ? -1 : iter->second;
}

int ViewJsonWriter::ioPinId(const IdbPin* pin) const
{
  auto iter = _io_pin_id_map.find(pin);
  return iter == _io_pin_id_map.end() ? -1 : iter->second;
}

int ViewJsonWriter::regularNetId(const IdbNet* net) const
{
  auto iter = _regular_net_id_map.find(net);
  return iter == _regular_net_id_map.end() ? -1 : iter->second;
}

int ViewJsonWriter::specialNetId(const IdbSpecialNet* net) const
{
  auto iter = _special_net_id_map.find(net);
  return iter == _special_net_id_map.end() ? -1 : iter->second;
}

}  // namespace idb
