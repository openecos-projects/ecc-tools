// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file StaCharacterTiming.hh
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief extract the timing model of design.
 * @version 0.1
 * @date 2024-03-12
 *
 */
#pragma once

#include <atomic>
#include <map>
#include <optional>
#include <vector>

#include "BTreeMap.hh"
#include "StaData.hh"
#include "StaFunc.hh"
#include "StaGraph.hh"
#include "liberty/Lib.hh"

namespace ista {

struct PreservedSeqCheckSnapshot {
  StaArc* check_arc = nullptr;
  StaVertex* capture_clock_vertex = nullptr;
  StaVertex* capture_clock_start_vertex = nullptr;
  StaVertex* data_start_vertex = nullptr;
  AnalysisMode capture_analysis_mode = AnalysisMode::kMax;
  TransType data_start_trans_type = TransType::kRise;
  TransType data_end_trans_type = TransType::kRise;
  TransType clock_trans_type = TransType::kRise;
  int64_t data_start_arrive_fs = 0;
  int64_t data_end_arrive_fs = 0;
  int64_t seq_arrive_time_fs = 0;
  int64_t required_time_fs = 0;
  int64_t capture_clock_start_arrive_fs = 0;
  int64_t capture_clock_end_arrive_fs = 0;
  int64_t capture_edge_fs = 0;
  int64_t constrain_value_fs = 0;
  int64_t uncertainty_fs = 0;
  int64_t cppr_fs = 0;
  std::optional<StaCheckPairBinding> check_pair_binding;
};

/**
 * @brief extract the timing model of the design.
 *
 */
class StaCharacterTiming : public StaFunc {
 public:
  enum CharacterState {
    kCollectEndpoints,
    kPropagateSlew,
    kPropagateDelay,
    kPropagateATFromPort,
    kBackPropagateRTToPort,
    kGenTimingModel
  };

  StaCharacterTiming(AnalysisMode analysis_mode, const char* model_path)
      : _model_path(model_path), _analysis_mode(analysis_mode) {}
  ~StaCharacterTiming() = default;

  unsigned operator()(StaGraph* the_graph) override;
  unsigned operator()(StaVertex* the_vertex) override;
  unsigned operator()(StaArc* the_arc) override;
  LibLibrary* get_design_timing_model() { return _design_timing_model.get(); }

 private:
  unsigned init(StaGraph* the_graph);
  void snapshotFullStaClockPinSlew(StaGraph* the_graph);
  void snapshotFullStaSeqCheckData(StaGraph* the_graph);
  unsigned collectInterfaceLogicEndPoint(StaGraph* the_graph);
  unsigned checkAndBreakLoop(StaGraph* the_graph);
  unsigned propagateSlew(StaGraph* the_graph);
  unsigned propagateDelay(StaGraph* the_graph);
  unsigned propagateATFromPort(StaGraph* the_graph);
  unsigned analyzeLocalSeqChecks(StaGraph* the_graph);
  unsigned backPropagateRTToPort(StaGraph* the_graph);
  unsigned genTimingModel(StaGraph* the_graph, const char* model_path);
  static uint64_t nextCharacterizationEpoch();

  std::unique_ptr<LibLibrary>
      _design_timing_model;     //!< The design timing model as lib format.
  std::string _model_path;      //!< The design timing model path.
  AnalysisMode _analysis_mode;  //!< The analysis mode.

  std::set<StaVertex*>
      _interface_logic_endpoints;  //!< The collected design interface logic
                                   //!< sequential endpoint and output port.
  ieda::Multimap<StaVertex*, StaVertex*>
      _port_to_logic_endpoint;  //!< The map from port to logic endpoint.
  ieda::Multimap<StaVertex*, StaVertex*>
      _output_port_to_input_port;  //!< The map from input port to output port.
  ieda::Multimap<StaVertex*, StaVertex*>
      _logic_clkpoint_to_port;  //!< The map from logic clkpoint to port.
  ieda::Multimap<StaVertex*, StaVertex*>
      _port_to_logic_clkpoint;  //!< The map from port to logic clkpoint.

  CharacterState _state =
      kCollectEndpoints;  // as the timing model compose of many points, we need
                          // to track the state.
  StaVertex* _current_port_vertex = nullptr;  // track the current port vertex.
  StaVertex* _current_logic_endpoint =
      nullptr;  // track the boundary endpoint currently being characterized.
  std::map<std::pair<StaVertex*, TransType>, double>
      _preserved_clock_pin_slew_ns;  //!< Snapshot of full-STA clock-pin slew
                                     //!< used only for ETM table sampling.
  std::map<std::tuple<StaVertex*, AnalysisMode, TransType>, double>
      _preserved_full_sta_pin_slew_ns;  //!< Full-STA pin slew snapshot keyed by
                                        //!< vertex/mode/transition for ETM
                                        //!< check-table sampling.
  std::map<StaVertex*, std::vector<PreservedSeqCheckSnapshot>>
      _preserved_seq_check_data;  //!< Snapshot of full-STA setup/hold data used
                                  //!< after ETM clears vertex delay buckets.
  uint64_t _characterization_epoch = nextCharacterizationEpoch();
};

}  // namespace ista
