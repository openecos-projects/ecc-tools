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
#pragma once

#include "DFModel.hpp"
#include "DFNetBinding.hpp"
#include "DFRegionNameMap.hpp"
#include "DFTransform.hpp"
#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbRegularWire.h"
#include "IdbSpecialNet.h"
#include "IdbSpecialWire.h"
#include "IdbVias.h"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "MJHeader.hpp"

namespace imj {

#define MJDF (imj::DefFlattener::getInst())

class DefFlattener
{
 public:
  static void initInst();
  static DefFlattener& getInst();
  static void destroyInst();
  // function
  void flatten(std::map<std::string, std::any> config_map);

 private:
  static DefFlattener* _df_instance;

  DefFlattener() = default;
  DefFlattener(const DefFlattener& other) = delete;
  DefFlattener(DefFlattener&& other) = delete;
  ~DefFlattener() = default;
  DefFlattener& operator=(const DefFlattener& other) = delete;
  DefFlattener& operator=(DefFlattener&& other) = delete;
  // function

  bool connectSpecialPinList(DFModel& df_model, idb::IdbDesign* output_design);

#if 1  // build

  bool buildDFModel(DFModel& df_model, std::map<std::string, std::any>& config_map);
  bool buildDFConfig(DFModel& df_model, std::map<std::string, std::any>& config_map);
  bool buildDFPGConnectList(DFConfig& df_config, std::string pg_connect_list_string);
  bool buildDFSourceMap(DFModel& df_model);
  bool buildDFSourceMasterList(DFModel& df_model, idb::IdbLayout* layout);
  bool buildDFSourceViaList(DFModel& df_model);
  bool buildDFVia(idb::IdbVia* source_via);
  bool buildDFViaMaster(idb::IdbViaMaster* source_via_master, idb::IdbViaMaster* output_via_master);
  bool buildDFHierarchy(DFModel& df_model);
  bool buildDFHierarchyNode(DFModel& df_model, idb::IdbDesign* parent_design, std::string parent_master_name,
                            std::vector<std::string>& master_name_stack, std::set<std::string>& visited_master_name_set);

#endif

#if 1  // check

  bool validateDFModel(DFModel& df_model);
  bool validateDFPGConnectList(DFModel& df_model);
  bool validateDFSource(DFModel& df_model, std::string master_name);
  bool validateDFDesignData(DFModel& df_model, idb::IdbDesign* source_design);
  bool validateDFInstance(DFModel& df_model, idb::IdbDesign* source_design, idb::IdbInstance* source_instance);
  bool validateDFRegularWire(idb::IdbRegularWire* source_wire);
  bool validateDFSpecialWire(idb::IdbSpecialWire* source_wire);
  bool validateDFVia(idb::IdbVia* source_via);

#endif

#if 1  // flatten

  void buildRootNetBinding(idb::IdbDesign* output_design, DFNetBinding& root_net_binding);
  void flattenInstance(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbDesign* parent_design,
                       idb::IdbInstance* parent_instance, std::string parent_hierarchy_name, DFTransform parent_transform,
                       DFNetBinding& parent_net_binding, bool remove_parent_instance);
  bool buildChildNetBinding(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbDesign* parent_design,
                            idb::IdbInstance* parent_instance, idb::IdbDesign* child_design,
                            std::string parent_hierarchy_name, DFNetBinding& parent_net_binding, DFNetBinding& child_net_binding);
  bool bindChildRegularNet(DFModel& df_model, idb::IdbDesign* output_design, DFNetBinding& child_net_binding,
                           idb::IdbNet* child_net, std::string output_net_name);
  bool bindChildSpecialNet(DFModel& df_model, idb::IdbDesign* output_design, DFNetBinding& child_net_binding,
                           idb::IdbSpecialNet* child_net, std::string output_net_name);
  idb::IdbSpecialNet* getOutputPGNet(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbNet* source_net);
  void flattenDesign(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbDesign* source_design,
                     std::string hierarchy_name, DFTransform transform, DFNetBinding& net_binding,
                     DFRegionNameMap& region_name_map);

#endif

#if 1  // merge

  bool mergeOutputRegularNet(DFModel& df_model, idb::IdbDesign* output_design, std::string target_net_name,
                             std::string source_net_name);
  bool mergeOutputSpecialNet(DFModel& df_model, idb::IdbDesign* output_design, std::string target_net_name,
                             std::string source_net_name);

#endif

#if 1  // copy

  void copyRegionList(idb::IdbDesign* output_design, idb::IdbDesign* source_design, std::string hierarchy_name,
                      DFTransform transform, DFRegionNameMap& region_name_map);
  void copyBlockageList(idb::IdbDesign* output_design, idb::IdbDesign* source_design, std::string hierarchy_name,
                        DFTransform transform);
  void copyRegularNetWireList(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbDesign* source_design,
                              std::string hierarchy_name, DFTransform transform, DFNetBinding& net_binding);
  void copyRegularWire(idb::IdbDesign* output_design, idb::IdbNet* output_net, idb::IdbRegularWire* source_wire,
                       DFTransform transform);
  void copyRegularWire(idb::IdbDesign* output_design, idb::IdbSpecialNet* output_net, idb::IdbRegularWire* source_wire,
                       DFTransform transform);
  void copyRegularWireSegment(idb::IdbDesign* output_design, idb::IdbRegularWireSegment* output_segment,
                              idb::IdbRegularWireSegment* source_segment, DFTransform transform);
  void copyRegularWireSegment(idb::IdbDesign* output_design, idb::IdbSpecialWireSegment* output_segment,
                              idb::IdbRegularWireSegment* source_segment, DFTransform transform);
  void copySpecialNetWireList(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbDesign* source_design,
                              std::string hierarchy_name, DFTransform transform, DFNetBinding& net_binding);
  void copySpecialWire(idb::IdbDesign* output_design, idb::IdbSpecialNet* output_net, idb::IdbSpecialWire* source_wire,
                       DFTransform transform);
  void copySpecialWireSegment(idb::IdbDesign* output_design, idb::IdbSpecialWireSegment* output_segment,
                              idb::IdbSpecialWireSegment* source_segment, DFTransform transform);
  void copyBoundaryPinGeometry(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbDesign* source_design,
                               std::string hierarchy_name, DFTransform transform, DFNetBinding& net_binding);
  void copyBoundaryPinShape(idb::IdbDesign* output_design, idb::IdbPin* source_pin, idb::IdbNet* output_net,
                            DFTransform transform);
  void copyBoundaryPinShape(idb::IdbDesign* output_design, idb::IdbPin* source_pin, idb::IdbSpecialNet* output_net,
                            DFTransform transform);
  void copyBoundaryPinVia(idb::IdbDesign* output_design, idb::IdbPin* source_pin, idb::IdbNet* output_net,
                          DFTransform transform);
  void copyBoundaryPinVia(idb::IdbDesign* output_design, idb::IdbPin* source_pin, idb::IdbSpecialNet* output_net,
                          DFTransform transform);
  void copyLeafInstance(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbInstance* source_instance,
                        std::string hierarchy_name, DFTransform transform, DFNetBinding& net_binding,
                        DFRegionNameMap& region_name_map);

#endif

#if 1  // get

  idb::IdbNet* getOutputRegularNet(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbNet* source_net,
                                    std::string hierarchy_name, DFNetBinding& net_binding);
  idb::IdbSpecialNet* getOutputSpecialNet(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbSpecialNet* source_net,
                                           std::string hierarchy_name, DFNetBinding& net_binding);
  idb::IdbSpecialNet* getSpecialNet(idb::IdbDesign* design, idb::IdbPin* pin);
  idb::IdbSpecialNet* getRelatedSpecialNet(idb::IdbDesign* design, idb::IdbNet* regular_net);
  idb::IdbVia* getOutputVia(idb::IdbDesign* output_design, idb::IdbVia* source_via);
  std::string getHierarchyName(std::string hierarchy_name, std::string name);
  std::string getUniqueRegionName(idb::IdbDesign* output_design, std::string name);

#endif
};

}  // namespace imj
