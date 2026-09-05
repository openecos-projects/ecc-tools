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

#include "DRCInterface.hpp"

#include "DataManager.hpp"
#include "GDSPlotter.hpp"
#include "IdbBlockages.h"
#include "IdbViaMaster.h"
#include "Monitor.hpp"
#include "RuleValidator.hpp"
#include "Utility.hpp"
#include "feature_manager.h"
#include "file_drc.h"
#include "idm.h"
#include "utility/logger/Logger.hpp"
namespace idrc {

// public

DRCInterface& DRCInterface::getInst()
{
  if (_drc_interface_instance == nullptr) {
    _drc_interface_instance = new DRCInterface();
  }
  return *_drc_interface_instance;
}

void DRCInterface::destroyInst()
{
  if (_drc_interface_instance != nullptr) {
    delete _drc_interface_instance;
    _drc_interface_instance = nullptr;
  }
}

#if 1  // 外部调用DRC的API

#if 1  // iDRC

void DRCInterface::initDRC(std::map<std::string, std::any> config_map, bool enable_quiet)
{
  Logger::initInst();
  DRCLOG.setQuiet(enable_quiet);
  // clang-format off
  DRCLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  DRCLOG.info(Loc::current(), "______________________________   _____________________________________   ");
  DRCLOG.info(Loc::current(), "___(_)__  __ \\__  __ \\_  ____/   __  ___/__  __/__    |__  __ \\__  __/");
  DRCLOG.info(Loc::current(), "__  /__  / / /_  /_/ /  /        _____ \\__  /  __  /| |_  /_/ /_  /     ");
  DRCLOG.info(Loc::current(), "_  / _  /_/ /_  _, _// /___      ____/ /_  /   _  ___ |  _, _/_  /       ");
  DRCLOG.info(Loc::current(), "/_/  /_____/ /_/ |_| \\____/      /____/ /_/    /_/  |_/_/ |_| /_/       ");
  DRCLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  DRCLOG.printLogFilePath();
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  Monitor monitor;
  DRCLOG.info(Loc::current(), "Starting...");

  DataManager::initInst();
  DRCDM.input(config_map);
  GDSPlotter::initInst();
  DRCGP.init();
  RuleValidator::initInst();

  DRCLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DRCInterface::runDRC()
{
  checkDef();
  destroyDRC();
}

void DRCInterface::checkDef()
{
  bool origin_quiet = DRCLOG.isQuiet();
  DRCLOG.disableQuiet();

  std::vector<DRCShape> env_shape_list = buildEnvShapeList();
  std::vector<DRCShape> result_shape_list = buildResultShapeList();
  std::vector<ids::Violation> violation_list = getViolationList(std::move(env_shape_list), std::move(result_shape_list), {}, {});

  std::map<std::string, std::vector<ids::Violation>> type_violation_map;
  for (ids::Violation& violation : violation_list) {
    type_violation_map[violation.violation_type].push_back(std::move(violation));
  }
  printSummary(type_violation_map);
  outputViolationJson(type_violation_map);
  outputViolationFile(type_violation_map);
  outputTofeature(type_violation_map);

  DRCLOG.setQuiet(origin_quiet);
}

void DRCInterface::destroyDRC()
{
  Monitor monitor;
  DRCLOG.info(Loc::current(), "Starting...");

  RuleValidator::destroyInst();
  DRCGP.destroy();
  GDSPlotter::destroyInst();
  DataManager::destroyInst();

  DRCLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());

  DRCLOG.printLogFilePath();
  // clang-format off
  DRCLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  DRCLOG.info(Loc::current(), "______________________________   _____________________   _____________________  __  ");
  DRCLOG.info(Loc::current(), "___(_)__  __ \\__  __ \\_  ____/   ___  ____/___  _/__  | / /___  _/_  ___/__  / / /");
  DRCLOG.info(Loc::current(), "__  /__  / / /_  /_/ /  /        __  /_    __  / __   |/ / __  / _____ \\__  /_/ /  ");
  DRCLOG.info(Loc::current(), "_  / _  /_/ /_  _, _// /___      _  __/   __/ /  _  /|  / __/ /  ____/ /_  __  /    ");
  DRCLOG.info(Loc::current(), "/_/  /_____/ /_/ |_| \\____/      /_/      /___/  /_/ |_/  /___/  /____/ /_/ /_/    ");
  DRCLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  Logger::destroyInst();
}

bool DRCInterface::saveDRC(std::string path)
{
  if (path.empty()) {
    return false;
  }

  iplf::FileDrcManager file(path, static_cast<int32_t>(iplf::DrcDbId::kDrcDetailInfo));
  return file.writeFile();
}

std::vector<ids::Violation> DRCInterface::getViolationList(const std::vector<ids::Shape>& ids_env_shape_list,
                                                           const std::vector<ids::Shape>& ids_result_shape_list,
                                                           const std::set<std::string>& ids_check_type_set,
                                                           const std::vector<ids::Shape>& ids_check_region_list)
{
  std::vector<DRCShape> drc_env_shape_list;
  drc_env_shape_list.reserve(ids_env_shape_list.size());
  for (const ids::Shape& ids_env_shape : ids_env_shape_list) {
    drc_env_shape_list.push_back(convertToDRCShape(ids_env_shape));
  }
  std::vector<DRCShape> drc_result_shape_list;
  drc_result_shape_list.reserve(ids_result_shape_list.size());
  const int32_t regular_net_num = static_cast<int32_t>(dmInst->get_idb_def_service()->get_design()->get_net_list()->get_net_list().size());
  for (const ids::Shape& ids_result_shape : ids_result_shape_list) {
    drc_result_shape_list.push_back(convertToDRCShape(ids_result_shape));
    drc_result_shape_list.back().set_is_special_net(ids_result_shape.net_idx >= regular_net_num);
  }
  std::set<ViolationType> drc_check_type_set;
  for (const std::string& ids_check_type : ids_check_type_set) {
    drc_check_type_set.insert(GetViolationTypeByName()(ids_check_type));
  }
  std::vector<DRCShape> drc_check_region_list;
  drc_check_region_list.reserve(ids_check_region_list.size());
  for (const ids::Shape& ids_check_region : ids_check_region_list) {
    drc_check_region_list.push_back(convertToDRCShape(ids_check_region));
  }
  return getViolationList(std::move(drc_env_shape_list), std::move(drc_result_shape_list), std::move(drc_check_type_set),
                          std::move(drc_check_region_list));
}

std::vector<ids::Violation> DRCInterface::getViolationList(std::vector<DRCShape> drc_env_shape_list,
                                                           std::vector<DRCShape> drc_result_shape_list,
                                                           std::set<ViolationType> drc_check_type_set,
                                                           std::vector<DRCShape> drc_check_region_list)
{
  std::vector<Violation> violation_list = DRCRV.verify(std::move(drc_env_shape_list), std::move(drc_result_shape_list),
                                                       std::move(drc_check_type_set), std::move(drc_check_region_list));
  std::vector<ids::Violation> ids_violation_list;
  ids_violation_list.reserve(violation_list.size());
  for (const Violation& violation : violation_list) {
    ids::Violation ids_violation;
    ids_violation.violation_type = GetViolationTypeName()(violation.get_violation_type());
    ids_violation.ll_x = violation.get_ll_x();
    ids_violation.ll_y = violation.get_ll_y();
    ids_violation.ur_x = violation.get_ur_x();
    ids_violation.ur_y = violation.get_ur_y();
    ids_violation.layer_idx = violation.get_layer_idx();
    ids_violation.is_routing = violation.get_is_routing();
    ids_violation.violation_net_set = violation.get_violation_net_set();
    ids_violation.required_size = violation.get_required_size();
    ids_violation_list.push_back(std::move(ids_violation));
  }
  return ids_violation_list;
}

void DRCInterface::cmpViolation(std::map<std::string, std::any> config_map)
{
  Die& die = DRCDM.getDatabase().get_die();
  std::map<std::string, int32_t>& routing_layer_name_to_idx_map = DRCDM.getDatabase().get_routing_layer_name_to_idx_map();
  std::string& rv_temp_directory_path = DRCDM.getConfig().rv_temp_directory_path;

  std::map<std::string, std::string> name_dir_map;
  {
    std::string ref = DRCUTIL.getConfigValue<std::string>(config_map, "-ref", "null");
    std::istringstream iss(ref);
    std::string token;
    while (iss >> token) {
      auto pos = token.find('=');
      if (pos == std::string::npos || pos == 0 || pos == token.size() - 1) {
        continue;
      }
      std::string key = token.substr(0, pos);
      std::string value = token.substr(pos + 1);
      name_dir_map[key] = value;
    }
  }
  std::set<ViolationType> violation_type_set;
  {
    violation_type_set.insert(ViolationType::kAdjacentCutSpacing);
    violation_type_set.insert(ViolationType::kCornerFillSpacing);
    violation_type_set.insert(ViolationType::kCornerSpacing);
    violation_type_set.insert(ViolationType::kCutEOLSpacing);
    violation_type_set.insert(ViolationType::kCutShort);
    violation_type_set.insert(ViolationType::kDifferentLayerCutSpacing);
    violation_type_set.insert(ViolationType::kEndOfLineSpacing);
    violation_type_set.insert(ViolationType::kEnclosure);
    violation_type_set.insert(ViolationType::kEnclosureEdge);
    violation_type_set.insert(ViolationType::kEnclosureParallel);
    violation_type_set.insert(ViolationType::kFloatingPatch);
    violation_type_set.insert(ViolationType::kJogToJogSpacing);
    violation_type_set.insert(ViolationType::kMaximumWidth);
    violation_type_set.insert(ViolationType::kMaxViaStack);
    violation_type_set.insert(ViolationType::kMetalShort);
    violation_type_set.insert(ViolationType::kMinHole);
    violation_type_set.insert(ViolationType::kMinimumArea);
    violation_type_set.insert(ViolationType::kMinimumCut);
    violation_type_set.insert(ViolationType::kMinimumWidth);
    violation_type_set.insert(ViolationType::kMinStep);
    violation_type_set.insert(ViolationType::kNonsufficientMetalOverlap);
    violation_type_set.insert(ViolationType::kNotchSpacing);
    violation_type_set.insert(ViolationType::kOffGridOrWrongWay);
    violation_type_set.insert(ViolationType::kOutOfDie);
    violation_type_set.insert(ViolationType::kParallelRunLengthSpacing);
    violation_type_set.insert(ViolationType::kSameLayerCutSpacing);
  }

  GPGDS gp_gds;

  GPStruct base_region_struct("base_region");
  GPBoundary gp_boundary;
  gp_boundary.set_layer_idx(0);
  gp_boundary.set_data_type(0);
  gp_boundary.set_rect(die);
  base_region_struct.push(gp_boundary);
  gp_gds.addStruct(base_region_struct);

  std::vector<DRCShape> drc_env_shape_list = buildEnvShapeList();
  for (DRCShape& drc_env_shape : drc_env_shape_list) {
    GPStruct drc_env_shape_struct(DRCUTIL.getString("drc_env_shape(net_", drc_env_shape.get_net_idx(), ")"));
    GPBoundary gp_boundary;
    gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kEnvShape));
    gp_boundary.set_rect(drc_env_shape.get_rect());
    if (drc_env_shape.get_is_routing()) {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByRouting(drc_env_shape.get_layer_idx()));
    } else {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByCut(drc_env_shape.get_layer_idx()));
    }
    drc_env_shape_struct.push(gp_boundary);
    gp_gds.addStruct(drc_env_shape_struct);
  }

  std::vector<DRCShape> drc_result_shape_list = buildResultShapeList();
  for (DRCShape& drc_result_shape : drc_result_shape_list) {
    GPStruct drc_result_shape_struct(DRCUTIL.getString("drc_result_shape(net_", drc_result_shape.get_net_idx(), ")"));
    GPBoundary gp_boundary;
    gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kResultShape));
    gp_boundary.set_rect(drc_result_shape.get_rect());
    if (drc_result_shape.get_is_routing()) {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByRouting(drc_result_shape.get_layer_idx()));
    } else {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByCut(drc_result_shape.get_layer_idx()));
    }
    drc_result_shape_struct.push(gp_boundary);
    gp_gds.addStruct(drc_result_shape_struct);
  }

  for (auto& [name, dir] : name_dir_map) {
    for (ViolationType violation_type : violation_type_set) {
      GPStruct violation_struct(DRCUTIL.getString("a_", GetViolationTypeName()(violation_type), "_", name));
      std::set<LayerRect, CmpLayerRectByXASC> violation_rect_set;
      {
        std::string violation_txt = DRCUTIL.getString(dir, "/", GetViolationTypeName()(violation_type), ".txt");

        std::ifstream fin(violation_txt);
        if (fin.is_open()) {
          std::string line;
          int32_t llx, lly, urx, ury;
          std::string layer;

          while (std::getline(fin, line)) {
            if (line.empty()) {
              continue;
            }
            std::istringstream iss(line);
            if (iss >> llx >> lly >> urx >> ury >> layer) {
              auto layer_iter = routing_layer_name_to_idx_map.find(layer);
              if (layer_iter == routing_layer_name_to_idx_map.end()) {
                DRCLOG.error(Loc::current(), "The routing layer '", layer, "' is not mapped!");
              }
              LayerRect violation_rect(llx, lly, urx, ury, layer_iter->second);
              violation_rect_set.insert(violation_rect);
            } else {
              ECCLOG.warn(ecc::Loc::current(), "这一行解析失败: ", line);
            }
          }
        }
      }
      for (const LayerRect& violation_rect : violation_rect_set) {
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(DRCGP.convertGPDataType(violation_type)));
        gp_boundary.set_rect(violation_rect.get_rect());
        gp_boundary.set_layer_idx(DRCGP.getGDSIdxByRouting(violation_rect.get_layer_idx()));
        violation_struct.push(gp_boundary);
      }
      gp_gds.addStruct(violation_struct);
    }
  }

  std::string gds_file_path = DRCUTIL.getString(rv_temp_directory_path, "cmp_drc.gds");
  DRCGP.plot(gp_gds, gds_file_path);
}

#endif

#endif

#if 1  // DRC调用外部的API

#if 1  // TopData

#if 1  // input

void DRCInterface::input(std::map<std::string, std::any>& config_map)
{
  DRCDM.input(config_map);
}

void DRCInterface::wrapConfig(std::map<std::string, std::any>& config_map)
{
  DRCDM.wrapConfig(config_map);
}

void DRCInterface::wrapDatabase()
{
  DRCDM.wrapDatabase();
}

void DRCInterface::wrapDBInfo()
{
  DRCDM.wrapDBInfo();
}

void DRCInterface::wrapMicronDBU()
{
  DRCDM.wrapMicronDBU();
}

void DRCInterface::wrapManufactureGrid()
{
  DRCDM.wrapManufactureGrid();
}

void DRCInterface::wrapDie()
{
  DRCDM.wrapDie();
}

void DRCInterface::wrapDesignRule()
{
  DRCDM.wrapDesignRule();
}

void DRCInterface::wrapLayerList()
{
  DRCDM.wrapLayerList();
}

void DRCInterface::wrapTrackAxis(RoutingLayer& routing_layer, idb::IdbLayerRouting* idb_layer)
{
  DRCDM.wrapTrackAxis(routing_layer, idb_layer);
}

void DRCInterface::wrapRoutingDesignRule(RoutingLayer& routing_layer, idb::IdbLayerRouting* idb_layer)
{
  DRCDM.wrapRoutingDesignRule(routing_layer, idb_layer);
}

void DRCInterface::wrapCutDesignRule(CutLayer& cut_layer, idb::IdbLayerCut* idb_layer)
{
  DRCDM.wrapCutDesignRule(cut_layer, idb_layer);
}

void DRCInterface::wrapLayerInfo()
{
  DRCDM.wrapLayerInfo();
}

Direction DRCInterface::getDRCDirectionByDB(idb::IdbLayerDirection idb_direction)
{
  return DRCDM.getDRCDirectionByDB(idb_direction);
}

#endif

#if 1  // output

void DRCInterface::output()
{
}

#endif

#endif

#if 1  // check

namespace {

size_t getViaRectCount(idb::IdbVia* idb_via);
void appendPinViaShapeList(std::vector<DRCShape>& shape_list, idb::IdbVia* idb_via, int32_t net_idx);
void appendWireViaShapeList(std::vector<DRCShape>& shape_list, idb::IdbVia* idb_via, int32_t net_idx,
                            ids::Shape::SourceType source_type);

}  // namespace

std::vector<DRCShape> DRCInterface::buildEnvShapeList()
{
  std::vector<DRCShape> env_shape_list;
  Monitor monitor;
  DRCLOG.info(Loc::current(), "Starting...");

  auto* idb_design = dmInst->get_idb_def_service()->get_design();
  std::vector<idb::IdbInstance*>& idb_instance_list = idb_design->get_instance_list()->get_instance_list();
  std::vector<idb::IdbNet*>& idb_net_list = idb_design->get_net_list()->get_net_list();
  std::vector<idb::IdbSpecialNet*>& idb_special_net_list = idb_design->get_special_net_list()->get_net_list();
  std::vector<idb::IdbPin*>& idb_io_pin_list = idb_design->get_io_pin_list()->get_pin_list();
  std::vector<idb::IdbBlockage*> idb_blockage_list = idb_design->get_blockage_list()->get_blockage_list();
  std::map<idb::IdbSpecialNet*, int32_t> special_net_idx_map;
  int32_t regular_net_num = static_cast<int32_t>(idb_net_list.size());
  for (size_t i = 0; i < idb_special_net_list.size(); ++i) {
    special_net_idx_map[idb_special_net_list[i]] = regular_net_num + static_cast<int32_t>(i);
  }
  auto get_pin_net_idx = [&](idb::IdbPin* idb_pin) {
    if (idb_pin == nullptr) {
      return -1;
    }
    idb::IdbSpecialNet* special_net = idb_pin->is_io_pin() ? idb_pin->get_special_net()
                                                            : idb_design->findSpecialNetForInstancePin(idb_pin);
    auto special_net_it = special_net_idx_map.find(special_net);
    if (special_net_it != special_net_idx_map.end()) {
      return special_net_it->second;
    }

    if (!isSkipping(idb_pin->get_net())) {
      return static_cast<int32_t>(idb_pin->get_net()->get_id());
    }
    return -1;
  };
  auto should_skip_instance_shape
      = [](idb::IdbInstance* idb_instance) { return idb_instance->is_unplaced() || idb_instance->get_status() == idb::IdbPlacementStatus::kNone; };

  size_t total_env_shape_num = 0;
  {
    // instance
    for (idb::IdbInstance* idb_instance : idb_instance_list) {
      if (should_skip_instance_shape(idb_instance)) {
        continue;
      }
      // instance obs
      for (idb::IdbLayerShape* obs_box : idb_instance->get_obs_box_list()) {
        total_env_shape_num += obs_box->get_rect_list().size();
      }
      // instance pin without net
      for (idb::IdbPin* idb_pin : idb_instance->get_pin_list()->get_pin_list()) {
        for (idb::IdbLayerShape* port_box : idb_pin->get_port_box_list()) {
          total_env_shape_num += port_box->get_rect_list().size();
        }
        for (idb::IdbVia* idb_via : idb_pin->get_via_list()) {
          total_env_shape_num += 2;
          total_env_shape_num += idb_via->get_instance()->get_cut_layer_shape()->get_rect_list_num();
        }
      }
    }

    // io pin without net
    for (idb::IdbPin* idb_io_pin : idb_io_pin_list) {
      for (idb::IdbLayerShape* port_box : idb_io_pin->get_port_box_list()) {
        total_env_shape_num += port_box->get_rect_list().size();
      }
    }

    // routing blockage
    for (idb::IdbBlockage* idb_blockage : idb_blockage_list) {
      if (!idb_blockage->is_routing_blockage()) {
        continue;
      }
      idb::IdbRoutingBlockage* routing_blockage = static_cast<idb::IdbRoutingBlockage*>(idb_blockage);
      if (routing_blockage->get_layer() != nullptr) {
        total_env_shape_num += routing_blockage->get_rect_num();
      }
    }
  }
  env_shape_list.reserve(total_env_shape_num);
  {
    // instance
    for (idb::IdbInstance* idb_instance : idb_instance_list) {
      if (should_skip_instance_shape(idb_instance)) {
        continue;
      }
      // instance obs
      for (idb::IdbLayerShape* obs_box : idb_instance->get_obs_box_list()) {
        for (idb::IdbRect* rect : obs_box->get_rect_list()) {
          if (obs_box->get_layer() == nullptr) {
            // DRCLOG.warn(Loc::current(), "The obs box layer is empty for instance ", idb_instance->get_name());
            continue;
          }
          DRCShape drc_shape(-1,
                             LayerRect(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y(),
                                       obs_box->get_layer()->get_id()),
                             obs_box->get_layer()->is_routing());
          drc_shape.set_source_type(ids::Shape::SourceType::kInstanceObs);
          env_shape_list.push_back(std::move(drc_shape));
        }
      }
      // instance pin without net
      for (idb::IdbPin* idb_pin : idb_instance->get_pin_list()->get_pin_list()) {
        int32_t net_idx = get_pin_net_idx(idb_pin);
        for (idb::IdbLayerShape* port_box : idb_pin->get_port_box_list()) {
          for (idb::IdbRect* rect : port_box->get_rect_list()) {
            DRCShape drc_shape(net_idx,
                               LayerRect(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y(),
                                         port_box->get_layer()->get_id()),
                               port_box->get_layer()->is_routing());
            drc_shape.set_source_type(ids::Shape::SourceType::kInstancePin);
            env_shape_list.push_back(std::move(drc_shape));
          }
        }
        for (idb::IdbVia* idb_via : idb_pin->get_via_list()) {
          appendPinViaShapeList(env_shape_list, idb_via, net_idx);
        }
      }
    }

    // io pin without net
    for (idb::IdbPin* idb_io_pin : idb_io_pin_list) {
      int32_t net_idx = get_pin_net_idx(idb_io_pin);
      for (idb::IdbLayerShape* port_box : idb_io_pin->get_port_box_list()) {
        for (idb::IdbRect* rect : port_box->get_rect_list()) {
          DRCShape drc_shape(net_idx,
                             LayerRect(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y(),
                                       port_box->get_layer()->get_id()),
                             port_box->get_layer()->is_routing());
          drc_shape.set_source_type(ids::Shape::SourceType::kIOPin);
          env_shape_list.push_back(std::move(drc_shape));
        }
      }
    }

    // routing blockage
    for (idb::IdbBlockage* idb_blockage : idb_blockage_list) {
      if (!idb_blockage->is_routing_blockage()) {
        continue;
      }
      idb::IdbRoutingBlockage* routing_blockage = static_cast<idb::IdbRoutingBlockage*>(idb_blockage);
      idb::IdbLayer* layer = routing_blockage->get_layer();
      if (layer == nullptr) {
        continue;
      }
      GTLPolySetInt blockage_polyset;
      for (idb::IdbRect* rect : routing_blockage->get_rect_list()) {
        blockage_polyset += GTLRectInt(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
      }
      idb::IdbInstance* idb_instance = routing_blockage->get_instance();
      if (idb_instance != nullptr) {
        GTLPolySetInt pin_polyset;
        for (idb::IdbPin* idb_pin : idb_instance->get_pin_list()->get_pin_list()) {
          for (idb::IdbLayerShape* port_box : idb_pin->get_port_box_list()) {
            if (port_box->get_layer() != layer) {
              continue;
            }
            for (idb::IdbRect* rect : port_box->get_rect_list()) {
              pin_polyset += GTLRectInt(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
            }
          }
          for (idb::IdbVia* idb_via : idb_pin->get_via_list()) {
            for (idb::IdbLayerShape via_shape : {idb_via->get_top_layer_shape(), idb_via->get_bottom_layer_shape()}) {
              if (via_shape.get_layer() == layer) {
                idb::IdbRect via_box = via_shape.get_bounding_box();
                pin_polyset += GTLRectInt(via_box.get_low_x(), via_box.get_low_y(), via_box.get_high_x(), via_box.get_high_y());
              }
            }
          }
        }
        blockage_polyset -= pin_polyset;
      }
      std::vector<GTLRectInt> blockage_rect_list;
      gtl::get_max_rectangles(blockage_rect_list, blockage_polyset);
      for (const GTLRectInt& rect : blockage_rect_list) {
        DRCShape drc_shape(-1, LayerRect(gtl::xl(rect), gtl::yl(rect), gtl::xh(rect), gtl::yh(rect), layer->get_id()), layer->is_routing());
        drc_shape.set_is_obs(true);
        env_shape_list.push_back(std::move(drc_shape));
      }
    }
  }
  DRCLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
  return env_shape_list;
}

bool DRCInterface::isSkipping(idb::IdbNet* idb_net)
{
  if (idb_net == nullptr) {
    return true;
  }
  bool has_io_pin = false;
  if (idb_net->has_io_pins() && idb_net->get_io_pins()->get_pin_num() == 1) {
    has_io_pin = true;
  }
  bool has_io_cell = false;
  std::vector<idb::IdbInstance*>& instance_list = idb_net->get_instance_list()->get_instance_list();
  if (instance_list.size() == 1 && instance_list.front()->get_cell_master()->is_pad()) {
    has_io_cell = true;
  }
  if (has_io_pin && has_io_cell) {
    return true;
  }

  int32_t pin_num = 0;
  for (idb::IdbPin* idb_pin : idb_net->get_instance_pin_list()->get_pin_list()) {
    if (idb_pin->get_term()->get_port_number() <= 0) {
      continue;
    }
    pin_num++;
  }
  for (idb::IdbPin* idb_pin : idb_net->get_io_pins()->get_pin_list()) {
    if (idb_pin->get_term()->get_port_number() <= 0) {
      continue;
    }
    pin_num++;
  }
  if (pin_num <= 1) {
    return true;
  }
  return false;
}

std::vector<DRCShape> DRCInterface::buildResultShapeList()
{
  std::vector<DRCShape> result_shape_list;
  Monitor monitor;
  DRCLOG.info(Loc::current(), "Starting...");

  std::vector<idb::IdbNet*>& idb_net_list = dmInst->get_idb_def_service()->get_design()->get_net_list()->get_net_list();
  std::vector<idb::IdbSpecialNet*>& idb_special_net_list = dmInst->get_idb_def_service()->get_design()->get_special_net_list()->get_net_list();

  size_t total_result_shape_num = 0;
  {
    // regular net
    for (idb::IdbNet* idb_net : idb_net_list) {
      for (idb::IdbRegularWire* idb_wire : idb_net->get_wire_list()->get_wire_list()) {
        for (idb::IdbRegularWireSegment* idb_segment : idb_wire->get_segment_list()) {
          if (idb_segment->get_point_number() >= 2) {
            total_result_shape_num += 1;
          }
          if (idb_segment->is_via()) {
            for (idb::IdbVia* idb_via : idb_segment->get_via_list()) {
              total_result_shape_num += getViaRectCount(idb_via);
            }
          }
          if (idb_segment->is_rect()) {
            total_result_shape_num += 1;
          }
        }
      }
    }
    // special net
    for (idb::IdbSpecialNet* idb_net : idb_special_net_list) {
      for (idb::IdbSpecialWire* idb_wire : idb_net->get_wire_list()->get_wire_list()) {
        for (idb::IdbSpecialWireSegment* idb_segment : idb_wire->get_segment_list()) {
          if (idb_segment->is_via()) {
            total_result_shape_num += getViaRectCount(idb_segment->get_via());
          } else {
            total_result_shape_num += 1;
          }
        }
      }
    }
  }
  result_shape_list.reserve(total_result_shape_num);
  // net
  for (idb::IdbNet* idb_net : idb_net_list) {
    for (idb::IdbRegularWire* idb_wire : idb_net->get_wire_list()->get_wire_list()) {
      for (idb::IdbRegularWireSegment* idb_segment : idb_wire->get_segment_list()) {
        if (idb_segment->get_point_number() >= 2) {
          PlanarCoord first_coord(idb_segment->get_point_start()->get_x(), idb_segment->get_point_start()->get_y());
          PlanarCoord second_coord(idb_segment->get_point_second()->get_x(), idb_segment->get_point_second()->get_y());
          int32_t half_width = dynamic_cast<IdbLayerRouting*>(idb_segment->get_layer())->get_width() / 2;
          PlanarRect rect = DRCUTIL.getEnlargedRect(first_coord, second_coord, half_width);
          DRCShape drc_shape(static_cast<int32_t>(idb_net->get_id()), LayerRect(rect, idb_segment->get_layer()->get_id()), true);
          drc_shape.set_source_type(ids::Shape::SourceType::kRegularWire);
          result_shape_list.push_back(std::move(drc_shape));
        }
        if (idb_segment->is_via()) {
          for (idb::IdbVia* idb_via : idb_segment->get_via_list()) {
            appendWireViaShapeList(result_shape_list, idb_via, static_cast<int32_t>(idb_net->get_id()),
                                   ids::Shape::SourceType::kRegularWire);
          }
        }
        if (idb_segment->is_rect()) {
          PlanarCoord offset_coord(idb_segment->get_point_start()->get_x(), idb_segment->get_point_start()->get_y());
          PlanarRect delta_rect(idb_segment->get_delta_rect()->get_low_x(), idb_segment->get_delta_rect()->get_low_y(),
                                idb_segment->get_delta_rect()->get_high_x(), idb_segment->get_delta_rect()->get_high_y());
          PlanarRect rect = DRCUTIL.getOffsetRect(delta_rect, offset_coord);
          DRCShape drc_shape(static_cast<int32_t>(idb_net->get_id()), LayerRect(rect, idb_segment->get_layer()->get_id()), true);
          drc_shape.set_source_type(ids::Shape::SourceType::kRegularWire);
          result_shape_list.push_back(std::move(drc_shape));
        }
      }
    }
  }
  // special net
  int32_t regular_net_num = static_cast<int32_t>(idb_net_list.size());
  for (size_t i = 0; i < idb_special_net_list.size(); ++i) {
    idb::IdbSpecialNet* idb_net = idb_special_net_list[i];
    int32_t special_net_id = regular_net_num + static_cast<int32_t>(i);
    for (idb::IdbSpecialWire* idb_wire : idb_net->get_wire_list()->get_wire_list()) {
      for (idb::IdbSpecialWireSegment* idb_segment : idb_wire->get_segment_list()) {
        if (idb_segment->is_via()) {
          appendWireViaShapeList(result_shape_list, idb_segment->get_via(), special_net_id, ids::Shape::SourceType::kSpecialWire);
        } else {
          idb::IdbRect* idb_rect = idb_segment->get_bounding_box();
          DRCShape drc_shape(special_net_id,
                             LayerRect(idb_rect->get_low_x(), idb_rect->get_low_y(), idb_rect->get_high_x(), idb_rect->get_high_y(),
                                       idb_segment->get_layer()->get_id()),
                             true);
          drc_shape.set_source_type(ids::Shape::SourceType::kSpecialWire);
          result_shape_list.push_back(std::move(drc_shape));
        }
      }
    }
  }
  DRCLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
  return result_shape_list;
}

void DRCInterface::printSummary(std::map<std::string, std::vector<ids::Violation>>& type_violation_map)
{
  int32_t total_violation_num = 0;
  for (auto& [type, violation_list] : type_violation_map) {
    total_violation_num += static_cast<int32_t>(violation_list.size());
  }
  fort::char_table type_violation_map_table;
  {
    type_violation_map_table.set_cell_text_align(fort::text_align::right);
    type_violation_map_table << fort::header << "violation_type"
                             << "violation_num" << "prop" << fort::endr;
    for (auto& [type, violation_list] : type_violation_map) {
      type_violation_map_table << type << violation_list.size() << DRCUTIL.getPercentage(violation_list.size(), total_violation_num) << fort::endr;
    }
    type_violation_map_table << fort::header << "Total" << total_violation_num << DRCUTIL.getPercentage(total_violation_num, total_violation_num) << fort::endr;
  }
  DRCUTIL.printTableList({type_violation_map_table});
}

namespace {

std::string getNetName(int32_t net_idx, const std::vector<idb::IdbNet*>& net_list,
                       const std::vector<idb::IdbSpecialNet*>& special_net_list, const std::string& fallback_name);

}  // namespace

void DRCInterface::outputViolationJson(std::map<std::string, std::vector<ids::Violation>>& type_violation_map)
{
  std::vector<RoutingLayer>& routing_layer_list = DRCDM.getDatabase().get_routing_layer_list();
  std::string& temp_directory_path = DRCDM.getConfig().temp_directory_path;

  std::vector<idb::IdbNet*>& idb_net_list = dmInst->get_idb_def_service()->get_design()->get_net_list()->get_net_list();
  std::vector<idb::IdbSpecialNet*>& idb_special_net_list = dmInst->get_idb_def_service()->get_design()->get_special_net_list()->get_net_list();
  std::string violation_json_file_path = DRCUTIL.getString(temp_directory_path, "violation_map.json");
  std::ofstream* violation_json_file = DRCUTIL.getOutputFileStream(violation_json_file_path);
  *violation_json_file << '[';
  bool is_first_violation = true;
  for (auto& [type, violation_list] : type_violation_map) {
    for (ids::Violation& violation : violation_list) {
      nlohmann::json violation_json;
      violation_json["type"] = violation.violation_type;

      int32_t layer_idx = violation.layer_idx;
      if (!violation.is_routing) {
        const std::vector<int32_t>& routing_layer_idx_list = DRCDM.getAdjacentRoutingLayerIdxList(layer_idx);
        layer_idx = *std::min_element(routing_layer_idx_list.begin(), routing_layer_idx_list.end());
      }
      violation_json["shape"] = {violation.ll_x, violation.ll_y, violation.ur_x, violation.ur_y, routing_layer_list[layer_idx].get_layer_name()};
      for (int32_t net_idx : violation.violation_net_set) {
        violation_json["net"].push_back(getNetName(net_idx, idb_net_list, idb_special_net_list, "obs"));
      }
      if (!is_first_violation) {
        *violation_json_file << ',';
      }
      *violation_json_file << violation_json;
      is_first_violation = false;
    }
  }
  *violation_json_file << ']';
  DRCUTIL.closeFileStream(violation_json_file);
}

void DRCInterface::outputViolationFile(std::map<std::string, std::vector<ids::Violation>>& type_violation_map)
{
  Monitor monitor;
  DRCLOG.info(Loc::current(), "Starting...");

  std::vector<RoutingLayer>& routing_layer_list = DRCDM.getDatabase().get_routing_layer_list();
  std::vector<CutLayer>& cut_layer_list = DRCDM.getDatabase().get_cut_layer_list();
  std::string& temp_directory_path = DRCDM.getConfig().temp_directory_path;

  std::vector<idb::IdbNet*>& idb_net_list = dmInst->get_idb_def_service()->get_design()->get_net_list()->get_net_list();
  std::vector<idb::IdbSpecialNet*>& idb_special_net_list = dmInst->get_idb_def_service()->get_design()->get_special_net_list()->get_net_list();
  for (auto& [type, violation_list] : type_violation_map) {
    std::ofstream* violation_file = DRCUTIL.getOutputFileStream(DRCUTIL.getString(temp_directory_path, type, ".txt"));
    for (ids::Violation& violation : violation_list) {
      DRCUTIL.pushStream(violation_file, violation.ll_x, " ", violation.ll_y, " ", violation.ur_x, " ", violation.ur_y, " ");
      if (violation.is_routing) {
        DRCUTIL.pushStream(violation_file, routing_layer_list[violation.layer_idx].get_layer_name(), " ");
      } else {
        DRCUTIL.pushStream(violation_file, cut_layer_list[violation.layer_idx].get_layer_name(), " ");
      }
      DRCUTIL.pushStream(violation_file, violation.is_routing ? "true" : "false", " ");

      DRCUTIL.pushStream(violation_file, "{ ");
      for (int32_t net_idx : violation.violation_net_set) {
        DRCUTIL.pushStream(violation_file, getNetName(net_idx, idb_net_list, idb_special_net_list, "-1"), " ");
      }
      DRCUTIL.pushStream(violation_file, "}", " ");

      DRCUTIL.pushStream(violation_file, violation.required_size, " ");
      DRCUTIL.pushStream(violation_file, "\n");
    }
    DRCUTIL.closeFileStream(violation_file);
  }

  DRCLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DRCInterface::outputTofeature(std::map<std::string, std::vector<ids::Violation>>& type_violation_map)
{
  std::vector<RoutingLayer>& routing_layer_list = DRCDM.getDatabase().get_routing_layer_list();
  std::vector<CutLayer>& cut_layer_list = DRCDM.getDatabase().get_cut_layer_list();

  featureInst->get_type_layer_violation_map().clear();
  for (auto& [type, violation_list] : type_violation_map) {
    for (ids::Violation& violation : violation_list) {
      std::string layer_name;
      if (violation.is_routing) {
        layer_name = routing_layer_list[violation.layer_idx].get_layer_name();
      } else {
        layer_name = cut_layer_list[violation.layer_idx].get_layer_name();
      }
      featureInst->get_type_layer_violation_map()[type][layer_name].push_back(violation);
    }
  }
}

DRCShape DRCInterface::convertToDRCShape(const ids::Shape& ids_shape)
{
  DRCShape drc_shape;
  drc_shape.set_net_idx(ids_shape.net_idx);
  drc_shape.set_ll(ids_shape.ll_x, ids_shape.ll_y);
  drc_shape.set_ur(ids_shape.ur_x, ids_shape.ur_y);
  drc_shape.set_layer_idx(ids_shape.layer_idx);
  drc_shape.set_is_routing(ids_shape.is_routing);
  drc_shape.set_source_type(ids_shape.source_type);
  return drc_shape;
}

namespace {

std::string getNetName(int32_t net_idx, const std::vector<idb::IdbNet*>& net_list,
                       const std::vector<idb::IdbSpecialNet*>& special_net_list, const std::string& fallback_name)
{
  if (0 <= net_idx && net_idx < static_cast<int32_t>(net_list.size())) {
    return net_list[net_idx]->get_net_name();
  }
  int32_t special_net_idx = net_idx - static_cast<int32_t>(net_list.size());
  if (0 <= special_net_idx && special_net_idx < static_cast<int32_t>(special_net_list.size())) {
    return special_net_list[special_net_idx]->get_net_name();
  }
  return fallback_name;
}

void appendLayerShapeRects(std::vector<DRCShape>& shape_list, idb::IdbLayerShape* layer_shape, int32_t net_idx, int32_t offset_x,
                           int32_t offset_y, bool is_routing, ids::Shape::SourceType source_type)
{
  for (idb::IdbRect* rect : layer_shape->get_rect_list()) {
    DRCShape& drc_shape = shape_list.emplace_back(
        net_idx,
        LayerRect(rect->get_low_x() + offset_x, rect->get_low_y() + offset_y, rect->get_high_x() + offset_x,
                  rect->get_high_y() + offset_y, layer_shape->get_layer()->get_id()),
        is_routing);
    drc_shape.set_source_type(source_type);
  }
}

void appendLayerShapeBoundingBox(std::vector<DRCShape>& shape_list, idb::IdbLayerShape* layer_shape, int32_t net_idx, int32_t offset_x,
                                 int32_t offset_y, ids::Shape::SourceType source_type)
{
  idb::IdbRect rect = layer_shape->get_bounding_box();
  DRCShape& drc_shape = shape_list.emplace_back(
      net_idx,
      LayerRect(rect.get_low_x() + offset_x, rect.get_low_y() + offset_y, rect.get_high_x() + offset_x, rect.get_high_y() + offset_y,
                layer_shape->get_layer()->get_id()),
      true);
  drc_shape.set_source_type(source_type);
}

size_t getViaRectCount(idb::IdbVia* idb_via)
{
  idb::IdbViaMaster* via_master = idb_via->get_instance();
  return via_master->get_top_layer_shape()->get_rect_list_num() + via_master->get_bottom_layer_shape()->get_rect_list_num()
         + via_master->get_cut_layer_shape()->get_rect_list_num();
}

void appendPinViaShapeList(std::vector<DRCShape>& shape_list, idb::IdbVia* idb_via, int32_t net_idx)
{
  idb::IdbViaMaster* via_master = idb_via->get_instance();
  idb::IdbCoordinate<int32_t>* offset = idb_via->get_coordinate();
  appendLayerShapeBoundingBox(shape_list, via_master->get_top_layer_shape(), net_idx, offset->get_x(), offset->get_y(),
                              ids::Shape::SourceType::kInstancePin);
  appendLayerShapeBoundingBox(shape_list, via_master->get_bottom_layer_shape(), net_idx, offset->get_x(), offset->get_y(),
                              ids::Shape::SourceType::kInstancePin);
  appendLayerShapeRects(shape_list, via_master->get_cut_layer_shape(), net_idx, offset->get_x(), offset->get_y(), false,
                        ids::Shape::SourceType::kInstancePin);
}

void appendWireViaShapeList(std::vector<DRCShape>& shape_list, idb::IdbVia* idb_via, int32_t net_idx,
                            ids::Shape::SourceType source_type)
{
  idb::IdbViaMaster* via_master = idb_via->get_instance();
  idb::IdbCoordinate<int32_t>* offset = idb_via->get_coordinate();
  appendLayerShapeRects(shape_list, via_master->get_top_layer_shape(), net_idx, offset->get_x(), offset->get_y(), true, source_type);
  appendLayerShapeRects(shape_list, via_master->get_bottom_layer_shape(), net_idx, offset->get_x(), offset->get_y(), true, source_type);
  appendLayerShapeRects(shape_list, via_master->get_cut_layer_shape(), net_idx, offset->get_x(), offset->get_y(), false, source_type);
}

}  // namespace

#endif

#endif

// private

DRCInterface* DRCInterface::_drc_interface_instance = nullptr;

}  // namespace idrc
