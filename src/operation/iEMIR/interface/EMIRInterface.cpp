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
#include "EMIRInterface.hpp"

#include "DataManager.hpp"
#include "EMAnalyzer.hpp"
#include "EMIRReporter.hpp"
#include "GraphBuilder.hpp"
#include "IRAnalyzer.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "PowerNet.hpp"
#include "PowerNetType.hpp"
#include "PowerPin.hpp"
#include "PowerVia.hpp"
#include "PowerWireSegment.hpp"
#include "RedHawkResNetworkReader.hpp"
#include "Utility.hpp"
#include "idm.h"

namespace iemir {

namespace {

int32_t micronToDBU(double value, int32_t micron_dbu)
{
  return static_cast<int32_t>(std::llround(value * micron_dbu));
}

std::pair<idb::IdbLayer*, idb::IdbLayer*> getAdjacentRoutingLayers(idb::IdbLayers* layers, idb::IdbLayer* cut_layer)
{
  idb::IdbLayer* bottom_layer = nullptr;
  idb::IdbLayer* top_layer = nullptr;
  for (idb::IdbLayer* routing_layer : layers->get_routing_layers()) {
    if (routing_layer->get_order() < cut_layer->get_order()
        && (bottom_layer == nullptr || routing_layer->get_order() > bottom_layer->get_order())) {
      bottom_layer = routing_layer;
    }
    if (routing_layer->get_order() > cut_layer->get_order()
        && (top_layer == nullptr || routing_layer->get_order() < top_layer->get_order())) {
      top_layer = routing_layer;
    }
  }
  return {bottom_layer, top_layer};
}

}  // namespace

EMIRInterface* EMIRInterface::_emir_interface_instance = nullptr;

// public

EMIRInterface& EMIRInterface::getInst()
{
  if (_emir_interface_instance == nullptr) {
    _emir_interface_instance = new EMIRInterface();
  }
  return *_emir_interface_instance;
}

void EMIRInterface::destroyInst()
{
  if (_emir_interface_instance != nullptr) {
    delete _emir_interface_instance;
    _emir_interface_instance = nullptr;
  }
}

#if 1  // 外部调用EMIR的API

#if 1  // iEMIR

void EMIRInterface::initEMIR(std::map<std::string, std::any> config_map)
{
  Logger::initInst();
  // clang-format off
  EMIRLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  EMIRLOG.info(Loc::current(), "____________________  _________________     _____________________________________  ");
  EMIRLOG.info(Loc::current(), "___(_)__  ____/__   |/  /___  _/__  __ \\    __  ___/__  __/__    |__  __ \\__  __/");
  EMIRLOG.info(Loc::current(), "__  /__  __/  __  /|_/ / __  / __  /_/ /    _____ \\__  /  __  /| |_  /_/ /_  /    ");
  EMIRLOG.info(Loc::current(), "_  / _  /___  _  /  / / __/ /  _  _, _/     ____/ /_  /   _  ___ |  _, _/_  /      ");
  EMIRLOG.info(Loc::current(), "/_/  /_____/  /_/  /_/  /___/  /_/ |_|      /____/ /_/    /_/  |_/_/ |_| /_/       ");
  EMIRLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  EMIRLOG.printLogFilePath();
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  Monitor monitor;
  EMIRLOG.info(Loc::current(), "Starting...");

  DataManager::initInst();
  EMIRDM.input(config_map);

  EMIRLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EMIRInterface::runEMIR()
{
  Monitor monitor;
  EMIRLOG.info(Loc::current(), "Starting...");

  GraphBuilder::initInst();
  EMIRGB.build();
  GraphBuilder::destroyInst();

  IRAnalyzer::initInst();
  EMIRIA.analyze();
  IRAnalyzer::destroyInst();

  EMAnalyzer::initInst();
  EMIREA.analyze();
  EMAnalyzer::destroyInst();

  EMIRReporter::initInst();
  EMIRER.report();
  EMIRReporter::destroyInst();

  EMIRLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EMIRInterface::destroyEMIR()
{
  Monitor monitor;
  EMIRLOG.info(Loc::current(), "Starting...");

  EMIRDM.output();
  DataManager::destroyInst();

  EMIRLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());

  EMIRLOG.printLogFilePath();
  // clang-format off
  EMIRLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  EMIRLOG.info(Loc::current(), "____________________  _________________     _____________________   _____________________  __ ");
  EMIRLOG.info(Loc::current(), "___(_)__  ____/__   |/  /___  _/__  __ \\    ___  ____/___  _/__  | / /___  _/_  ___/__  / / /");
  EMIRLOG.info(Loc::current(), "__  /__  __/  __  /|_/ / __  / __  /_/ /    __  /_    __  / __   |/ / __  / _____ \\__  /_/ / ");
  EMIRLOG.info(Loc::current(), "_  / _  /___  _  /  / / __/ /  _  _, _/     _  __/   __/ /  _  /|  / __/ /  ____/ /_  __  /   ");
  EMIRLOG.info(Loc::current(), "/_/  /_____/  /_/  /_/  /___/  /_/ |_|      /_/      /___/  /_/ |_/  /___/  /____/ /_/ /_/    ");
  EMIRLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  Logger::destroyInst();
}

#endif

#endif

#if 1  // EMIR调用外部的API

#if 1  // TopData

#if 1  // input

void EMIRInterface::input(std::map<std::string, std::any>& config_map)
{
  wrapConfig(config_map);
  wrapDatabase();
}

void EMIRInterface::wrapConfig(std::map<std::string, std::any>& config_map)
{
  /////////////////////////////////////////////
  EMIRDM.getConfig().temp_directory_path = EMIRUTIL.getConfigValue<std::string>(config_map, "-temp_directory_path", "./emir_temp_directory");
  EMIRDM.getConfig().ptpx_instance_power_file_path
      = EMIRUTIL.getConfigValue<std::string>(config_map, "-ptpx_instance_power_file_path", "");
  EMIRDM.getConfig().redhawk_res_network_file_path
      = EMIRUTIL.getConfigValue<std::string>(config_map, "-redhawk_res_network_file_path", "");
  EMIRDM.getConfig().ploc_file_path = EMIRUTIL.getConfigValue<std::string>(config_map, "-ploc_file_path", "");
  EMIRDM.getConfig().redhawk_tech_file_path = EMIRUTIL.getConfigValue<std::string>(config_map, "-redhawk_tech_file_path", "");
  EMIRDM.getConfig().em_limit_file_path = EMIRUTIL.getConfigValue<std::string>(config_map, "-em_limit_file_path", "");
  EMIRDM.getConfig().em_violation_threshold_percent
      = EMIRUTIL.getConfigValue<double>(config_map, "-em_violation_threshold_percent", 100.0);
  EMIRDM.getConfig().thread_number = EMIRUTIL.getConfigValue<int32_t>(config_map, "-thread_number", 128);
  omp_set_num_threads(std::max(EMIRDM.getConfig().thread_number, 1));
  /////////////////////////////////////////////
}

void EMIRInterface::wrapDatabase()
{
  wrapDBInfo();
  wrapInstanceIdSet();
  wrapPowerNetList();
  if (!EMIRDM.getConfig().redhawk_res_network_file_path.empty()) {
    wrapRedHawkResNetwork();
  }
}

void EMIRInterface::wrapDBInfo()
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  EMIRDM.getDatabase().set_design_name(idb_design->get_design_name());
  EMIRDM.getDatabase().set_micron_dbu(idb_design->get_units()->get_micron_dbu());
}

void EMIRInterface::wrapInstanceIdSet()
{
  std::set<uint64_t>& instance_id_set = EMIRDM.getDatabase().get_instance_id_set();
  std::map<std::string, uint64_t>& instance_name_to_id_map = EMIRDM.getDatabase().get_instance_name_to_id_map();
  instance_id_set.clear();
  instance_name_to_id_map.clear();
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (idb::IdbInstance* idb_instance : idb_design->get_instance_list()->get_instance_list()) {
    instance_id_set.insert(idb_instance->get_id());
    instance_name_to_id_map[idb_instance->get_name()] = idb_instance->get_id();
  }
}

void EMIRInterface::wrapPowerNetList()
{
  std::map<std::string, PowerNet>& power_net_map = EMIRDM.getDatabase().get_power_net_map();
  power_net_map.clear();
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (idb::IdbSpecialNet* idb_power_net : idb_design->get_special_net_list()->get_net_list()) {
    PowerNetType power_net_type = wrapPowerNetType(idb_power_net->get_connect_type());
    if (power_net_type == PowerNetType::kNone) {
      continue;
    }
    wrapPowerNet(idb_power_net);
  }
}

void EMIRInterface::wrapRedHawkResNetwork()
{
  RedHawkResNetwork network = RedHawkResNetworkReader::read(EMIRDM.getConfig().redhawk_res_network_file_path);
  idb::IdbLayers* layers = dmInst->get_idb_layout()->get_layers();
  int32_t micron_dbu = EMIRDM.getDatabase().get_micron_dbu();
  std::unordered_map<std::string, std::vector<PowerWireSegment>> wire_lists;
  std::unordered_map<std::string, std::vector<PowerVia>> via_lists;
  std::unordered_map<std::string, const RedHawkWireSegmentRecord*> wire_segment_map;

  for (const RedHawkWireSegmentRecord& record : network.wire_segments) {
    wire_segment_map[record.id] = &record;
    idb::IdbLayer* layer = layers->find_layer(record.layer_name);
    if (layer == nullptr || !layer->is_routing()) {
      EMIRLOG.error(Loc::current(), "RedHawk res_network uses an unmapped routing layer: ", record.layer_name);
    }
    PowerWireSegment wire;
    wire.set_layer_idx(layer->get_id());
    wire.set_layer_name(layer->get_name());
    wire.set_first_x(micronToDBU(record.first_x_um, micron_dbu));
    wire.set_first_y(micronToDBU(record.first_y_um, micron_dbu));
    wire.set_second_x(micronToDBU(record.second_x_um, micron_dbu));
    wire.set_second_y(micronToDBU(record.second_y_um, micron_dbu));
    wire.set_width(std::max(micronToDBU(record.width_um, micron_dbu), 1));
    wire.set_resistance(record.resistance_ohm);
    wire_lists[record.net_name].push_back(std::move(wire));
  }

  for (const RedHawkViaRecord& record : network.vias) {
    idb::IdbLayer* cut_layer = layers->find_layer(record.layer_name);
    if (cut_layer == nullptr || !cut_layer->is_cut()) {
      EMIRLOG.error(Loc::current(), "RedHawk res_network uses an unmapped cut layer: ", record.layer_name);
    }
    auto [bottom_layer, top_layer] = getAdjacentRoutingLayers(layers, cut_layer);
    if (bottom_layer == nullptr || top_layer == nullptr) {
      EMIRLOG.error(Loc::current(), "Cannot map adjacent routing layers for RedHawk cut layer: ", record.layer_name);
    }

    PowerVia via;
    via.set_via_name(record.via_name);
    via.set_bottom_layer_name(bottom_layer->get_name());
    via.set_top_layer_name(top_layer->get_name());
    via.set_bottom_layer_idx(bottom_layer->get_id());
    via.set_top_layer_idx(top_layer->get_id());
    via.set_x(micronToDBU(record.x_um, micron_dbu));
    via.set_y(micronToDBU(record.y_um, micron_dbu));
    std::optional<std::pair<double, double>> bottom_coordinate
        = RedHawkResNetworkReader::connectionCoordinate(record, wire_segment_map, bottom_layer->get_name());
    std::optional<std::pair<double, double>> top_coordinate = RedHawkResNetworkReader::connectionCoordinate(record, wire_segment_map, top_layer->get_name());
    if (bottom_coordinate.has_value()) {
      via.set_bottom_x(micronToDBU(bottom_coordinate->first, micron_dbu));
      via.set_bottom_y(micronToDBU(bottom_coordinate->second, micron_dbu));
    }
    if (top_coordinate.has_value()) {
      via.set_top_x(micronToDBU(top_coordinate->first, micron_dbu));
      via.set_top_y(micronToDBU(top_coordinate->second, micron_dbu));
    }
    via.set_cut_num(record.cut_num);
    via.set_cut_area_um2(static_cast<double>(record.cut_num) * record.cut_width_um * record.cut_height_um);
    int32_t half_cut_width = std::max(micronToDBU(record.cut_width_um / 2.0, micron_dbu), 1);
    int32_t half_cut_height = std::max(micronToDBU(record.cut_height_um / 2.0, micron_dbu), 1);
    via.set_cut_low_x(-half_cut_width);
    via.set_cut_low_y(-half_cut_height);
    via.set_cut_high_x(half_cut_width);
    via.set_cut_high_y(half_cut_height);
    via.set_resistance(record.resistance_ohm);
    via_lists[record.net_name].push_back(std::move(via));
  }

  std::map<std::string, PowerNet>& power_net_map = EMIRDM.getDatabase().get_power_net_map();
  for (const auto& [net_name, wires] : wire_lists) {
    if (power_net_map.count(net_name) == 0) {
      EMIRLOG.error(Loc::current(), "RedHawk res_network net is not a DEF POWER/GROUND net: ", net_name);
    }
  }
  for (const auto& [net_name, vias] : via_lists) {
    if (power_net_map.count(net_name) == 0) {
      EMIRLOG.error(Loc::current(), "RedHawk res_network net is not a DEF POWER/GROUND net: ", net_name);
    }
  }

  std::size_t imported_net_num = 0;
  for (auto& [net_name, power_net] : power_net_map) {
    auto wire_iter = wire_lists.find(net_name);
    if (wire_iter == wire_lists.end()) {
      continue;
    }
    power_net.get_wire_segment_list() = std::move(wire_iter->second);
    auto via_iter = via_lists.find(net_name);
    if (via_iter == via_lists.end()) {
      power_net.get_via_list().clear();
    } else {
      power_net.get_via_list() = std::move(via_iter->second);
    }
    power_net.set_has_explicit_parasitics(true);
    imported_net_num++;
  }
  if (imported_net_num == 0) {
    EMIRLOG.error(Loc::current(), "The RedHawk res_network contains no DEF POWER/GROUND net: ",
                  EMIRDM.getConfig().redhawk_res_network_file_path);
  }
  EMIRLOG.info(Loc::current(), "Imported ", network.wire_segments.size(), " RedHawk PG wire resistors and ", network.vias.size(),
               " via resistors from ", imported_net_num, " nets in ", EMIRDM.getConfig().redhawk_res_network_file_path);
}

void EMIRInterface::wrapPowerNet(idb::IdbSpecialNet* idb_power_net)
{
  PowerNet power_net;
  power_net.set_net_name(idb_power_net->get_net_name());
  power_net.set_type(wrapPowerNetType(idb_power_net->get_connect_type()));
  wrapPowerWireSegmentList(power_net, idb_power_net);
  wrapPowerPinList(power_net, idb_power_net);
  if (power_net.get_wire_segment_list().empty() && power_net.get_via_list().empty() && power_net.get_pin_list().empty()) {
    return;
  }
  EMIRDM.getDatabase().get_power_net_map()[power_net.get_net_name()] = power_net;
}

PowerNetType EMIRInterface::wrapPowerNetType(idb::IdbConnectType connect_type)
{
  if (connect_type == idb::IdbConnectType::kPower) {
    return PowerNetType::kPower;
  }
  if (connect_type == idb::IdbConnectType::kGround) {
    return PowerNetType::kGround;
  }
  return PowerNetType::kNone;
}

void EMIRInterface::wrapPowerWireSegmentList(PowerNet& power_net, idb::IdbSpecialNet* idb_power_net)
{
  for (idb::IdbSpecialWire* idb_wire : idb_power_net->get_wire_list()->get_wire_list()) {
    for (idb::IdbSpecialWireSegment* idb_segment : idb_wire->get_segment_list()) {
      if (idb_segment->get_point_num() >= 2) {
        wrapPowerWireSegment(power_net, idb_segment);
      }
      if (idb_segment->is_via()) {
        wrapPowerVia(power_net, idb_segment->get_via());
      }
    }
  }
}

void EMIRInterface::wrapPowerWireSegment(PowerNet& power_net, idb::IdbSpecialWireSegment* idb_segment)
{
  idb::IdbLayerRouting* idb_layer = dynamic_cast<idb::IdbLayerRouting*>(idb_segment->get_layer());
  if (idb_layer == nullptr) {
    EMIRLOG.error(Loc::current(), "The power wire segment layer is invalid!");
  }
  idb::IdbCoordinate<int32_t>* first_coordinate = idb_segment->get_point_start();
  idb::IdbCoordinate<int32_t>* second_coordinate = idb_segment->get_point_second();
  if (first_coordinate == nullptr || second_coordinate == nullptr) {
    EMIRLOG.error(Loc::current(), "The power wire segment coordinate is invalid!");
  }
  PowerWireSegment power_wire_segment;
  power_wire_segment.set_layer_idx(idb_layer->get_id());
  power_wire_segment.set_layer_name(idb_layer->get_name());
  power_wire_segment.set_first_x(first_coordinate->get_x());
  power_wire_segment.set_first_y(first_coordinate->get_y());
  power_wire_segment.set_second_x(second_coordinate->get_x());
  power_wire_segment.set_second_y(second_coordinate->get_y());
  power_wire_segment.set_width(idb_segment->get_route_width());
  power_wire_segment.set_resistance_per_square(idb_layer->get_resistance());
  power_net.get_wire_segment_list().push_back(power_wire_segment);
}

void EMIRInterface::wrapPowerVia(PowerNet& power_net, idb::IdbVia* idb_via)
{
  idb::IdbLayerShape bottom_layer_shape = idb_via->get_bottom_layer_shape();
  idb::IdbLayerShape top_layer_shape = idb_via->get_top_layer_shape();
  idb::IdbLayerShape cut_layer_shape = idb_via->get_cut_layer_shape();
  idb::IdbCoordinate<int32_t>* coordinate = idb_via->get_coordinate();
  idb::IdbViaMaster* idb_via_master = idb_via->get_instance();
  PowerVia power_via;
  std::string via_name = idb_via->get_name();
  if (via_name.empty() && idb_via_master != nullptr) {
    via_name = idb_via_master->get_name();
  }
  power_via.set_via_name(via_name);
  power_via.set_bottom_layer_name(bottom_layer_shape.get_layer()->get_name());
  power_via.set_top_layer_name(top_layer_shape.get_layer()->get_name());
  power_via.set_bottom_layer_idx(bottom_layer_shape.get_layer()->get_id());
  power_via.set_top_layer_idx(top_layer_shape.get_layer()->get_id());
  power_via.set_x(coordinate->get_x());
  power_via.set_y(coordinate->get_y());
  power_via.set_cut_num(static_cast<int32_t>(cut_layer_shape.get_rect_list().size()));
  int32_t micron_dbu = EMIRDM.getDatabase().get_micron_dbu();
  double cut_area_um2 = 0.0;
  for (idb::IdbRect* cut_rect : cut_layer_shape.get_rect_list()) {
    if (cut_rect == nullptr || micron_dbu <= 0) {
      continue;
    }
    cut_area_um2 += static_cast<double>(cut_rect->get_width()) * static_cast<double>(cut_rect->get_height())
                    / static_cast<double>(micron_dbu) / static_cast<double>(micron_dbu);
  }
  power_via.set_cut_area_um2(cut_area_um2);
  idb::IdbRect cut_bounding_box = idb_via->get_cut_bounding_box();
  power_via.set_cut_low_x(cut_bounding_box.get_low_x() - coordinate->get_x());
  power_via.set_cut_low_y(cut_bounding_box.get_low_y() - coordinate->get_y());
  power_via.set_cut_high_x(cut_bounding_box.get_high_x() - coordinate->get_x());
  power_via.set_cut_high_y(cut_bounding_box.get_high_y() - coordinate->get_y());
  if (idb_via_master->get_resistance() > 0.0) {
    power_via.set_resistance(idb_via_master->get_resistance());
  } else if (idb_via_master->get_master_generate()->get_rule_generate() != nullptr
             && idb_via_master->get_master_generate()->get_rule_generate()->get_resistance_per_cut() > 0.0 && power_via.get_cut_num() > 0) {
    power_via.set_resistance(idb_via_master->get_master_generate()->get_rule_generate()->get_resistance_per_cut() / power_via.get_cut_num());
  } else {
    power_via.set_resistance(getGeneratedViaResistance(idb_via_master, power_via.get_cut_num()));
  }
  power_net.get_via_list().push_back(power_via);
}

double EMIRInterface::getGeneratedViaResistance(idb::IdbViaMaster* idb_via_master, int32_t cut_num)
{
  if (idb_via_master->get_master_generate() == nullptr || cut_num <= 0) {
    return 0.0;
  }
  idb::IdbViaMasterGenerate* generated_master = idb_via_master->get_master_generate();
  idb::IdbLayerRouting* bottom_layer = generated_master->get_layer_bottom();
  idb::IdbLayerCut* cut_layer = generated_master->get_layer_cut();
  idb::IdbLayerRouting* top_layer = generated_master->get_layer_top();
  double resistance_per_cut = 0.0;
  for (idb::IdbVia* technology_via : dmInst->get_idb_layout()->get_via_list()->get_via_list()) {
    idb::IdbViaMaster* technology_via_master = technology_via->get_instance();
    if (!technology_via_master->is_fix() || !technology_via_master->is_default() || !technology_via_master->isOneCut()
        || technology_via_master->get_resistance() <= 0.0) {
      continue;
    }
    idb::IdbLayerShape* bottom_layer_shape = technology_via_master->get_bottom_layer_shape();
    idb::IdbLayerShape* cut_layer_shape = technology_via_master->get_cut_layer_shape();
    idb::IdbLayerShape* top_layer_shape = technology_via_master->get_top_layer_shape();
    if (bottom_layer_shape->get_layer() != bottom_layer || cut_layer_shape->get_layer() != cut_layer || top_layer_shape->get_layer() != top_layer) {
      continue;
    }
    if (resistance_per_cut == 0.0) {
      resistance_per_cut = technology_via_master->get_resistance();
    } else if (std::abs(resistance_per_cut - technology_via_master->get_resistance()) > EMIR_ERROR) {
      return 0.0;
    }
  }
  return resistance_per_cut / cut_num;
}

void EMIRInterface::wrapPowerPinList(PowerNet& power_net, idb::IdbSpecialNet* idb_power_net)
{
  std::unordered_set<idb::IdbPin*> wrapped_pins;
  for (idb::IdbPin* idb_pin : idb_power_net->get_instance_pin_list()->get_pin_list()) {
    if (idb_pin != nullptr && wrapped_pins.insert(idb_pin).second) {
      wrapPowerPin(power_net, idb_pin, false);
    }
  }
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  if (idb_power_net->has_wildcard_instance_pins()) {
    for (idb::IdbInstance* idb_instance : idb_design->get_instance_list()->get_instance_list()) {
      if (idb_instance == nullptr || idb_instance->get_pin_list() == nullptr) {
        continue;
      }
      for (idb::IdbPin* idb_pin : idb_instance->get_pin_list()->get_pin_list()) {
        if (idb_pin != nullptr && idb_design->findSpecialNetForInstancePin(idb_pin) == idb_power_net
            && wrapped_pins.insert(idb_pin).second) {
          wrapPowerPin(power_net, idb_pin, false);
        }
      }
    }
  }
  for (idb::IdbPin* idb_pin : idb_power_net->get_io_pin_list()->get_pin_list()) {
    if (idb_pin != nullptr && wrapped_pins.insert(idb_pin).second) {
      wrapPowerPin(power_net, idb_pin, true);
    }
  }
}

void EMIRInterface::wrapPowerPin(PowerNet& power_net, idb::IdbPin* idb_pin, bool is_source)
{
  for (idb::IdbLayerShape* idb_layer_shape : idb_pin->get_port_box_list()) {
    wrapPowerPinShape(power_net, idb_pin, idb_layer_shape, is_source);
  }
}

void EMIRInterface::wrapPowerPinShape(PowerNet& power_net, idb::IdbPin* idb_pin, idb::IdbLayerShape* idb_layer_shape, bool is_source)
{
  idb::IdbRect idb_bounding_box = idb_layer_shape->get_bounding_box();
  PowerPin power_pin;
  if (!idb_pin->is_io_pin()) {
    power_pin.set_instance_id(idb_pin->get_instance()->get_id());
    power_pin.set_pin_name(idb_pin->get_instance()->get_name() + ":" + idb_pin->get_pin_name());
  } else {
    power_pin.set_pin_name(idb_pin->get_pin_name());
  }
  power_pin.set_layer_idx(idb_layer_shape->get_layer()->get_id());
  power_pin.set_x((idb_bounding_box.get_low_x() + idb_bounding_box.get_high_x()) / 2);
  power_pin.set_y((idb_bounding_box.get_low_y() + idb_bounding_box.get_high_y()) / 2);
  power_pin.set_low_x(idb_bounding_box.get_low_x());
  power_pin.set_low_y(idb_bounding_box.get_low_y());
  power_pin.set_high_x(idb_bounding_box.get_high_x());
  power_pin.set_high_y(idb_bounding_box.get_high_y());
  power_pin.set_is_source(is_source);
  power_net.get_pin_list().push_back(power_pin);
}

#endif

#if 1  // output

void EMIRInterface::output()
{
}

#endif

#endif

#endif

}  // namespace iemir
