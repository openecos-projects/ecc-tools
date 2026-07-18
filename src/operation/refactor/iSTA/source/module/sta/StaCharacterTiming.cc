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
 * @file StaCharacterTiming.cc
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief extract the timing model of design.
 * @version 0.1
 * @date 2024-03-12
 *
 */
#include "StaCharacterTiming.hh"

#include "api/TimingEngine.hh"
#include "api/TimingIDBAdapter.hh"
#include "StaAnalyze.hh"
#include "StaCheck.hh"
#include "StaDataPropagation.hh"
#include "StaDelayPropagation.hh"
#include "StaSlewPropagation.hh"
#include "ThreadPool/ThreadPool.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <tuple>

namespace ista {

#include "StaCharacterTimingHelpers.inc"

uint64_t StaCharacterTiming::nextCharacterizationEpoch() {
  static std::atomic<uint64_t> next_epoch{1};
  return next_epoch.fetch_add(1, std::memory_order_relaxed);
}

/**
 * @brief propagate from the vertex.
 *
 * @param the_vertex
 * @return unsigned
 */
unsigned StaCharacterTiming::operator()(StaVertex* the_vertex) {
  if (_state == kCollectEndpoints || _state == kBackPropagateRTToPort) {
    if ((_state == kBackPropagateRTToPort) && (the_vertex->is_bwd())) {
      return 1;
    }

    if (the_vertex->is_end()) {
      if (_state == kCollectEndpoints) {
        // collect the interface logic endpoint.
        if (!the_vertex->is_port()) {
          _port_to_logic_endpoint.insert(_current_port_vertex, the_vertex);
        } else {
          _output_port_to_input_port.insert(the_vertex, _current_port_vertex);
        }

        _interface_logic_endpoints.insert(the_vertex);

      } else if (_state == kBackPropagateRTToPort) {
        // set the constrain require time.
        auto set_constain_value = [the_vertex](auto analysis_mode,
                                               auto trans_type) {
          if (!the_vertex->is_port()) {
            // get the delay value from the check arc(delay arc)
            auto* check_arc = the_vertex->getCheckArc(analysis_mode);
            auto constrain_val =
                check_arc->get_arc_delay(analysis_mode, trans_type);
            the_vertex->setConstainTime(analysis_mode, trans_type,
                                        constrain_val);
          } else {
            the_vertex->setConstainTime(analysis_mode, trans_type, 0);
          }
        };

        FOREACH_MODE_TRANS(analysis_mode, trans_type) {
          set_constain_value(analysis_mode, trans_type);
        }
      }

      return 1;
    }

    if (the_vertex->is_clock()) {
      if (_state == kCollectEndpoints) {
        LOG_FATAL_IF(!_current_port_vertex)
            << "current port vertex is null when clock vertex "
            << the_vertex->getName() << " is reached.";
        _logic_clkpoint_to_port.insert(the_vertex, _current_port_vertex);
        _port_to_logic_clkpoint.insert(_current_port_vertex, the_vertex);
        _interface_logic_endpoints.insert(the_vertex);
      }

      return 1;
    }

    // fwd
    FOREACH_SRC_ARC(the_vertex, src_arc) {
      if (src_arc->isMpwArc()) {
        continue;
      }

      (*this)(src_arc);
      (*this)(src_arc->get_snk());
    }

    if (_state == kBackPropagateRTToPort) {
      the_vertex->set_is_bwd();
    }

  } else if (_state == kPropagateSlew || _state == kPropagateDelay ||
             _state == kPropagateATFromPort) {
    const bool characterize_clock_path =
        _current_logic_endpoint && _current_logic_endpoint->is_clock();
    if ((_state == kPropagateSlew) && (the_vertex->is_slew_prop())) {
      return 1;
    } else if ((_state == kPropagateDelay) && (the_vertex->is_delay_prop())) {
      return 1;
    } else if ((_state == kPropagateATFromPort) && (the_vertex->is_fwd())) {
      return 1;
    }

    // ETM extraction should stop at the previously discovered interface
    // boundary: first sequential endpoints or top-level output ports.
    if ((the_vertex != _current_port_vertex) &&
        (the_vertex != _current_logic_endpoint) &&
        _interface_logic_endpoints.contains(the_vertex)) {
      if (_state == kPropagateSlew) {
        the_vertex->set_is_slew_prop();
      } else if (_state == kPropagateDelay) {
        the_vertex->set_is_delay_prop();
      } else if (_state == kPropagateATFromPort) {
        the_vertex->set_is_fwd();
      }
      return 1;
    }

    // Clock-to-output ETM extraction should launch from the discovered clock
    // pin itself. Walking further upstream into the clock tree mixes extra
    // clock-slew families into the local clk->q arc buckets, which then
    // contaminates the boundary delay chosen for the output arc.
    if (the_vertex->is_clock() && !characterize_clock_path) {
      if (_state == kPropagateSlew) {
        the_vertex->set_is_slew_prop();
      } else if (_state == kPropagateDelay) {
        the_vertex->set_is_delay_prop();
      } else if (_state == kPropagateATFromPort) {
        the_vertex->set_is_fwd();
      }
      return 1;
    }

    // bwd
    FOREACH_SNK_ARC(the_vertex, snk_arc) {
      if (!snk_arc->isDelayArc()) {
        if (_state == kPropagateDelay && snk_arc->isCheckArc()) {
          // ETM setup/hold export reuses local check-arc delay values. Those
          // arc buckets must be rebuilt in the characterization epoch instead
          // of staying empty after resetCharacterizationPayload().
          (*this)(snk_arc->get_src());
          (*this)(snk_arc);
        }
        continue;
      }

      (*this)(snk_arc->get_src());
      // compute the slew and delay along the arc.
      (*this)(snk_arc);
    }

    if (_state == kPropagateSlew) {
      the_vertex->set_is_slew_prop();
    } else if (_state == kPropagateDelay) {
      the_vertex->set_is_delay_prop();
    } else if (_state == kPropagateATFromPort) {
      the_vertex->set_is_fwd();
    }
  }

  return 1;
}

/**
 * @brief propagate from the arc.
 *
 * @param the_arc
 * @return unsigned
 */
unsigned StaCharacterTiming::operator()(StaArc* the_arc) {
  if (_state == kPropagateSlew) {
    // propagate the slew along the arc.
    StaSlewPropagation slew_propagation;
    slew_propagation.set_data_epoch_filter(_characterization_epoch);
    // output port need propagate slew too, for construct transition table.
    slew_propagation.set_propagate_output_port();
    slew_propagation(the_arc);
  } else if (_state == kPropagateDelay) {
    // propagate the delay along the arc.
    StaDelayPropagation delay_propagation;
    delay_propagation.set_data_epoch_filter(_characterization_epoch);
    delay_propagation(the_arc);
  } else if (_state == kPropagateATFromPort) {
    // propagate the AT along the arc.
    StaFwdPropagation fwd_propagation;
    fwd_propagation.set_data_epoch_filter(_characterization_epoch);
    fwd_propagation(the_arc);
  } else if (_state == kBackPropagateRTToPort) {
    // propagate the RT along the arc.
    StaBwdPropagation bwd_propagation;
    bwd_propagation(the_arc);
  }

  return 1;
}

/**
 * @brief character timing is to extract the timing model of the design.
 * traverse from the port vertex to the first sequential cell. When reach to
 * the endpoint, build the constrain lib arc of the timing model.
 * @param the_graph
 * @return unsigned
 */
unsigned StaCharacterTiming::operator()(StaGraph* the_graph) {
  // init the data and propgated way.
  init(the_graph);

  // first collect the interface logic endpoint.
  collectInterfaceLogicEndPoint(the_graph);

  // then check and break loop.
  checkAndBreakLoop(the_graph);

  // then propagate the slew from the port to the first sequential cell.
  propagateSlew(the_graph);

  // then propagate the delay from the port to the first sequential cell.
  propagateDelay(the_graph);

  // then propagate the AT from port to the first sequential cell.
  propagateATFromPort(the_graph);

  // Reuse the existing seq-check analyzer on the ETM epoch so exporter lookup
  // can query exact local setup/hold pairs instead of inferring from snapshots.
  analyzeLocalSeqChecks(the_graph);

  // then back propagate the RT from endpoint to port. not need propagated RT
  // now, temporary mask code below. backPropagateRTToPort(the_graph);

  // finaly gen the timing model.
  genTimingModel(the_graph, _model_path.c_str());

  return 1;
}

void StaCharacterTiming::snapshotFullStaClockPinSlew(StaGraph* the_graph) {
  _preserved_clock_pin_slew_ns.clear();
  _preserved_full_sta_pin_slew_ns.clear();

  auto snapshot_vertex = [this](StaVertex* the_vertex) {
    if (!the_vertex) {
      return;
    }

    size_t snapshot_entries = 0;
    const bool snapshot_eligible =
        the_vertex->is_clock() || the_vertex->is_sdc_clock_pin();
    for (auto analysis_mode : {AnalysisMode::kMax, AnalysisMode::kMin}) {
      for (auto trans_type : {TransType::kRise, TransType::kFall}) {
        auto* slew_data = the_vertex->getWorstSlewData(analysis_mode, trans_type);
        if (!slew_data) {
          continue;
        }

        const double slew_ns = FS_TO_NS(slew_data->get_slew());
        _preserved_full_sta_pin_slew_ns[std::make_tuple(the_vertex, analysis_mode,
                                                        trans_type)] = slew_ns;

        if (snapshot_eligible && analysis_mode == _analysis_mode) {
          _preserved_clock_pin_slew_ns[{the_vertex, trans_type}] = slew_ns;
          ++snapshot_entries;
        }
      }
    }

    traceClockSlewVertexSnapshot(the_vertex, snapshot_eligible,
                                 snapshot_entries);
  };

  StaVertex* the_vertex = nullptr;
  FOREACH_VERTEX(the_graph, the_vertex) {
    snapshot_vertex(the_vertex);
  }

  FOREACH_ASSISTANT_VERTEX(the_graph, assistant_vertex) {
    snapshot_vertex(assistant_vertex.get());
  }
}

void StaCharacterTiming::snapshotFullStaSeqCheckData(StaGraph* the_graph) {
  _preserved_seq_check_data.clear();

  auto* ista = Sta::getOrCreateSta();
  if (!ista) {
    return;
  }

  auto snapshot_endpoint = [this, ista](StaVertex* endpoint_vertex,
                                        AnalysisMode analysis_mode) {
    if (!endpoint_vertex) {
      return;
    }

    using SnapshotKey =
        std::tuple<StaArc*, StaVertex*, StaVertex*, StaVertex*, AnalysisMode,
                   TransType, TransType, TransType, int64_t, int64_t, int64_t,
                   int64_t, int64_t>;

    auto build_snapshot_key = [](const PreservedSeqCheckSnapshot& snapshot) {
      return SnapshotKey(snapshot.check_arc, snapshot.capture_clock_vertex,
                         snapshot.capture_clock_start_vertex,
                         snapshot.data_start_vertex,
                         snapshot.capture_analysis_mode,
                         snapshot.data_start_trans_type,
                         snapshot.data_end_trans_type,
                         snapshot.clock_trans_type,
                         snapshot.data_start_arrive_fs,
                         snapshot.data_end_arrive_fs,
                         snapshot.capture_clock_start_arrive_fs,
                         snapshot.capture_clock_end_arrive_fs,
                         snapshot.constrain_value_fs);
    };

    auto build_capture_edge_fs = [](StaClockData* capture_clock_data) -> int64_t {
      if (!capture_clock_data) {
        return 0;
      }

      auto* capture_clock = capture_clock_data->get_prop_clock();
      if (!capture_clock) {
        return 0;
      }

      auto& wave_form = capture_clock->get_wave_form();
      return capture_clock_data->get_clock_wave_type() == TransType::kRise
                 ? wave_form.getRisingEdge()
                 : wave_form.getFallingEdge();
    };

    auto seq_data_vec = ista->getSeqData(endpoint_vertex, analysis_mode);
    std::set<SnapshotKey> seen_snapshots;
    auto& endpoint_snapshots = _preserved_seq_check_data[endpoint_vertex];

    auto append_snapshot =
        [&](const PreservedSeqCheckSnapshot& snapshot,
            StaPathDelayData* seq_delay_data, StaClockData* capture_clock_data,
            const char* snapshot_source) {
          auto [it, inserted] =
              seen_snapshots.insert(build_snapshot_key(snapshot));
          if (!inserted) {
            return;
          }

          const bool trace_seq_snapshot =
              shouldTraceCharacterizationPin(endpoint_vertex->getName().c_str()) ||
              (snapshot.data_start_vertex && shouldTraceCharacterizationPin(
                                               snapshot.data_start_vertex->getName().c_str()));
          if (trace_seq_snapshot) {
            LOG_INFO << "[character_timing][" << snapshot_source
                     << "] endpoint=" << endpoint_vertex->getName()
                     << " analysis_mode=" << analysisModeName(analysis_mode)
                     << " check_arc="
                     << (snapshot.check_arc
                             ? snapshot.check_arc->get_src()->getName() + "->" +
                                   snapshot.check_arc->get_snk()->getName()
                             : "null")
                     << " seq_delay_trans="
                     << (seq_delay_data
                             ? transTypeName(seq_delay_data->get_trans_type())
                             : "null")
                     << " data_start_vertex="
                     << (snapshot.data_start_vertex
                             ? snapshot.data_start_vertex->getName()
                             : "null")
                     << " data_start_trans="
                     << transTypeName(snapshot.data_start_trans_type)
                     << " data_end_trans="
                     << transTypeName(snapshot.data_end_trans_type)
                     << " data_start_arrive_ns="
                     << FS_TO_NS(snapshot.data_start_arrive_fs)
                     << " data_end_arrive_ns="
                     << FS_TO_NS(snapshot.data_end_arrive_fs)
                     << " data_delay_ns="
                     << FS_TO_NS(snapshot.data_end_arrive_fs -
                                 snapshot.data_start_arrive_fs)
                     << " seq_arrive_ns="
                     << FS_TO_NS(snapshot.seq_arrive_time_fs)
                     << " capture_clock_vertex="
                     << (snapshot.capture_clock_vertex
                             ? snapshot.capture_clock_vertex->getName()
                             : "null")
                     << " capture_clk_start_vertex="
                     << (snapshot.capture_clock_start_vertex
                             ? snapshot.capture_clock_start_vertex->getName()
                             : "null")
                     << " capture_clk_start_arrive_ns="
                     << FS_TO_NS(snapshot.capture_clock_start_arrive_fs)
                     << " capture_clk_end_arrive_ns="
                     << FS_TO_NS(snapshot.capture_clock_end_arrive_fs)
                     << " capture_clk_delay_ns="
                     << FS_TO_NS(snapshot.capture_clock_end_arrive_fs -
                                 snapshot.capture_clock_start_arrive_fs)
                     << " capture_edge_ns=" << FS_TO_NS(snapshot.capture_edge_fs)
                     << " constrain_ns=" << FS_TO_NS(snapshot.constrain_value_fs)
                     << " capture_clock_path_vertex="
                     << (capture_clock_data && capture_clock_data->get_own_vertex()
                             ? capture_clock_data->get_own_vertex()->getName()
                             : "null");
          }

          endpoint_snapshots.push_back(snapshot);
        };

    if (seq_data_vec.empty()) {
      _preserved_seq_check_data.erase(endpoint_vertex);
    }

    for (auto* seq_data : seq_data_vec) {
      auto* seq_delay_data = seq_data ? seq_data->get_delay_data() : nullptr;
      auto* capture_clock_data =
          seq_data ? seq_data->get_capture_clock_data() : nullptr;
      if (!seq_data || !seq_delay_data || !capture_clock_data ||
          !seq_data->get_check_arc()) {
        continue;
      }

      auto data_terms = extractBoundaryDelayTerms(seq_data->getPathDelayData());
      auto capture_clock_terms =
          extractBoundaryDelayTerms(capture_clock_data->getPathData());
      if (!data_terms || !capture_clock_terms) {
        continue;
      }

      PreservedSeqCheckSnapshot snapshot;
      snapshot.check_arc = seq_data->get_check_arc();
      snapshot.capture_clock_vertex = capture_clock_data->get_own_vertex();
      snapshot.capture_clock_start_vertex = capture_clock_terms->start_vertex;
      snapshot.data_start_vertex = data_terms->start_vertex;
      snapshot.capture_analysis_mode = capture_clock_data->get_delay_type();
      snapshot.data_start_trans_type = data_terms->start_trans_type;
      snapshot.data_end_trans_type = data_terms->end_trans_type;
      snapshot.clock_trans_type = capture_clock_data->get_trans_type();
      snapshot.data_start_arrive_fs = data_terms->start_arrive_fs;
      snapshot.data_end_arrive_fs = data_terms->end_arrive_fs;
      snapshot.seq_arrive_time_fs = seq_data->getArriveTime();
      snapshot.required_time_fs = seq_data->getRequireTime();
      snapshot.capture_clock_start_arrive_fs =
          capture_clock_terms->start_arrive_fs;
      snapshot.capture_clock_end_arrive_fs =
          capture_clock_terms->end_arrive_fs;
      snapshot.capture_edge_fs = seq_data->getCaptureEdge();
      snapshot.constrain_value_fs = seq_data->get_constrain_value();
      snapshot.uncertainty_fs = seq_data->get_uncertainty().value_or(0);
      snapshot.cppr_fs = seq_data->get_cppr().value_or(0);
      if (auto* check_pair_binding = seq_data->get_check_pair_binding();
          check_pair_binding) {
        snapshot.check_pair_binding = *check_pair_binding;
      }
      append_snapshot(snapshot, seq_delay_data, capture_clock_data,
                      "seq-snapshot");
    }

    auto* check_arc = endpoint_vertex->getCheckArc(analysis_mode);
    if (!check_arc) {
      if (endpoint_snapshots.empty()) {
        _preserved_seq_check_data.erase(endpoint_vertex);
      }
      return;
    }

    auto* clock_vertex = check_arc->get_src();
    const auto clock_trans_type =
        check_arc->isRisingEdgeCheck() ? TransType::kRise : TransType::kFall;
    const auto capture_analysis_mode =
        analysis_mode == AnalysisMode::kMax ? AnalysisMode::kMin
                                            : AnalysisMode::kMax;

    StaData* arc_delay_data_obj = nullptr;
    FOREACH_ARC_DELAY_DATA(check_arc, arc_delay_data_obj) {
      auto* arc_delay_data = dynamic_cast<StaArcDelayData*>(arc_delay_data_obj);
      if (!arc_delay_data ||
          arc_delay_data->get_delay_type() != analysis_mode ||
          !arc_delay_data->has_check_pair_bindings()) {
        continue;
      }

      for (const auto& check_pair_binding : arc_delay_data->get_check_pair_bindings()) {
        auto* data_slew_data = check_pair_binding.data_slew_data;
        auto* clock_slew_data = check_pair_binding.clock_slew_data;
        if (!data_slew_data || !clock_slew_data ||
            check_pair_binding.analysis_mode != analysis_mode ||
            check_pair_binding.check_arc != check_arc ||
            clock_slew_data->get_trans_type() != clock_trans_type) {
          continue;
        }

        auto data_path_sig = extractPathVertexSignature(data_slew_data->getPathData());
        auto clock_path_sig =
            extractPathVertexSignature(clock_slew_data->getPathData());
        if (!data_path_sig || data_path_sig->empty() || !clock_path_sig ||
            clock_path_sig->empty()) {
          continue;
        }

        PreservedSeqCheckSnapshot snapshot;
        snapshot.check_arc = check_arc;
        snapshot.capture_clock_vertex = clock_slew_data->get_own_vertex();
        snapshot.capture_clock_start_vertex = clock_path_sig->front().first;
        snapshot.data_start_vertex = data_path_sig->front().first;
        snapshot.capture_analysis_mode = capture_analysis_mode;
        snapshot.data_start_trans_type = data_path_sig->front().second;
        snapshot.data_end_trans_type = data_path_sig->back().second;
        snapshot.clock_trans_type = clock_slew_data->get_trans_type();
        snapshot.capture_edge_fs = 0;
        snapshot.constrain_value_fs = check_pair_binding.sampled_check_margin_fs;
        snapshot.check_pair_binding = check_pair_binding;

        append_snapshot(snapshot, nullptr, nullptr,
                        "seq-snapshot-binding");
      }
    }

    StaData* clock_data = nullptr;
    FOREACH_CLOCK_DATA(clock_vertex, clock_data) {
      auto* capture_clock_data = dynamic_cast<StaClockData*>(clock_data);
      if (!capture_clock_data ||
          capture_clock_data->get_delay_type() != capture_analysis_mode ||
          capture_clock_data->get_trans_type() != clock_trans_type) {
        continue;
      }

      auto capture_clock_terms =
          extractBoundaryDelayTerms(capture_clock_data->getPathData());
      if (!capture_clock_terms) {
        continue;
      }

      StaData* delay_data = nullptr;
      FOREACH_DELAY_DATA(endpoint_vertex, delay_data) {
        auto* path_delay_data = dynamic_cast<StaPathDelayData*>(delay_data);
        if (!path_delay_data ||
            path_delay_data->get_delay_type() != analysis_mode) {
          continue;
        }

        auto data_terms = extractBoundaryDelayTerms(path_delay_data->getPathData());
        if (!data_terms || !data_terms->start_vertex ||
            !data_terms->start_vertex->is_port() ||
            !data_terms->start_vertex->get_design_obj() ||
            !data_terms->start_vertex->get_design_obj()->isInput()) {
          continue;
        }

        auto* arc_delay_data = check_arc->getArcDelayData(
            analysis_mode, path_delay_data->get_trans_type(),
            path_delay_data->get_data_epoch());
        if (!arc_delay_data) {
          arc_delay_data =
              check_arc->getArcDelayData(analysis_mode,
                                         path_delay_data->get_trans_type());
        }

        PreservedSeqCheckSnapshot snapshot;
        snapshot.check_arc = check_arc;
        snapshot.capture_clock_vertex = capture_clock_data->get_own_vertex();
        snapshot.capture_clock_start_vertex = capture_clock_terms->start_vertex;
        snapshot.data_start_vertex = data_terms->start_vertex;
        snapshot.capture_analysis_mode = capture_clock_data->get_delay_type();
        snapshot.data_start_trans_type = data_terms->start_trans_type;
        snapshot.data_end_trans_type = data_terms->end_trans_type;
        snapshot.clock_trans_type = capture_clock_data->get_trans_type();
        snapshot.data_start_arrive_fs = data_terms->start_arrive_fs;
        snapshot.data_end_arrive_fs = data_terms->end_arrive_fs;
        snapshot.seq_arrive_time_fs = path_delay_data->get_arrive_time();
        snapshot.capture_clock_start_arrive_fs =
            capture_clock_terms->start_arrive_fs;
        snapshot.capture_clock_end_arrive_fs =
            capture_clock_terms->end_arrive_fs;
        snapshot.capture_edge_fs = build_capture_edge_fs(capture_clock_data);
        snapshot.constrain_value_fs =
            arc_delay_data
                ? arc_delay_data->get_arc_delay()
                : check_arc->get_arc_delay(analysis_mode,
                                           path_delay_data->get_trans_type(),
                                           path_delay_data->get_data_epoch());
        if (arc_delay_data && arc_delay_data->has_check_pair_binding()) {
          snapshot.check_pair_binding = *arc_delay_data->get_check_pair_binding();
        }

        append_snapshot(snapshot, path_delay_data, capture_clock_data,
                        "seq-snapshot-direct");
      }
    }

    if (endpoint_snapshots.empty()) {
      _preserved_seq_check_data.erase(endpoint_vertex);
    }
  };

  StaVertex* the_vertex = nullptr;
  FOREACH_VERTEX(the_graph, the_vertex) {
    snapshot_endpoint(the_vertex, AnalysisMode::kMax);
    snapshot_endpoint(the_vertex, AnalysisMode::kMin);
  }

  FOREACH_ASSISTANT_VERTEX(the_graph, assistant_vertex) {
    snapshot_endpoint(assistant_vertex.get(), AnalysisMode::kMax);
    snapshot_endpoint(assistant_vertex.get(), AnalysisMode::kMin);
  }
}

/**
 * @brief init the data propagated way, which may be graph based or path
 * based.Then set the init slew data.
 *
 * @param the_graph
 * @return unsigned
 */
unsigned StaCharacterTiming::init(StaGraph* the_graph) {
  auto init_clock_path_delay_data = [this](StaVertex* the_vertex) {
    StaData* clock_data = nullptr;
    FOREACH_CLOCK_DATA(the_vertex, clock_data) {
      auto* launch_clock_data = dynamic_cast<StaClockData*>(clock_data);
      if (!launch_clock_data) {
        continue;
      }

      auto* path_delay_data = new StaPathDelayData(
          launch_clock_data->get_delay_type(),
          launch_clock_data->get_trans_type(), 0, launch_clock_data,
          the_vertex);
      path_delay_data->set_data_epoch(_characterization_epoch);
      path_delay_data->set_launch_delay_data(path_delay_data);
      the_vertex->addData(path_delay_data);
    }
  };

  auto seed_preserved_clock_slew = [this](StaVertex* the_vertex) {
    if (!the_vertex) {
      return;
    }

    StaData* slew_data = nullptr;
    FOREACH_SLEW_DATA(the_vertex, slew_data) {
      auto* vertex_slew_data = dynamic_cast<StaSlewData*>(slew_data);
      if (!vertex_slew_data) {
        continue;
      }

      auto snapshot_iter = _preserved_clock_pin_slew_ns.find(
          {the_vertex, vertex_slew_data->get_trans_type()});
      if (snapshot_iter == _preserved_clock_pin_slew_ns.end() ||
          snapshot_iter->second <= 0.0) {
        continue;
      }

      vertex_slew_data->set_slew(NS_TO_FS(snapshot_iter->second));
    }
  };

  auto seed_preserved_full_sta_input_slew = [this](StaVertex* the_vertex) {
    if (!the_vertex) {
      return;
    }

    StaData* slew_data = nullptr;
    FOREACH_SLEW_DATA(the_vertex, slew_data) {
      auto* vertex_slew_data = dynamic_cast<StaSlewData*>(slew_data);
      if (!vertex_slew_data) {
        continue;
      }

      if (vertex_slew_data->get_delay_type() != AnalysisMode::kMax ||
          vertex_slew_data->get_trans_type() != TransType::kFall) {
        continue;
      }

      auto snapshot_iter = _preserved_full_sta_pin_slew_ns.find(std::make_tuple(
          the_vertex, vertex_slew_data->get_delay_type(),
          vertex_slew_data->get_trans_type()));
      if (snapshot_iter == _preserved_full_sta_pin_slew_ns.end() ||
          snapshot_iter->second <= 0.0) {
        continue;
      }

      vertex_slew_data->set_slew(NS_TO_FS(snapshot_iter->second));
    }
  };

  auto init_input_port_delay_data = [this](StaVertex* the_vertex) -> bool {
    auto* ista = Sta::getOrCreateSta();
    if (!ista || !the_vertex) {
      return false;
    }

    auto io_delays = ista->getIODelayConstrain(the_vertex);
    auto synthesize_default_input_arrival = [this, ista,
                                             the_vertex]() -> bool {
      if (!the_vertex || the_vertex->is_clock() || the_vertex->is_sdc_clock_pin()) {
        return false;
      }

      auto clocks = ista->getClocks();
      if (clocks.size() != 1) {
        return false;
      }

      auto* launch_clock = clocks.front();
      if (!launch_clock) {
        return false;
      }

      auto ensure_launch_clock_data =
          [this, the_vertex, launch_clock](AnalysisMode analysis_mode)
          -> StaClockData* {
        auto clock_datas = the_vertex->getClockData(analysis_mode, TransType::kRise);
        for (auto* clock_data : clock_datas) {
          auto* candidate_clock_data = dynamic_cast<StaClockData*>(clock_data);
          if (candidate_clock_data &&
              candidate_clock_data->get_prop_clock() == launch_clock) {
            return candidate_clock_data;
          }
        }

        auto* launch_clock_data = new StaClockData(
            analysis_mode, TransType::kRise, 0, the_vertex, launch_clock);
        launch_clock_data->set_data_epoch(_characterization_epoch);
        launch_clock_data->set_clock_wave_type(TransType::kRise);
        the_vertex->addData(launch_clock_data);
        return launch_clock_data;
      };

      auto add_default_delay_data =
          [this, the_vertex, &ensure_launch_clock_data](AnalysisMode analysis_mode) {
        auto* launch_clock_data = ensure_launch_clock_data(analysis_mode);
        if (!launch_clock_data) {
          return false;
        }

        for (auto data_trans_type : {TransType::kRise, TransType::kFall}) {
          auto* path_delay_data = new StaPathDelayData(
              analysis_mode, data_trans_type, 0, launch_clock_data, the_vertex);
          path_delay_data->set_data_epoch(_characterization_epoch);
          path_delay_data->set_launch_delay_data(path_delay_data);
          the_vertex->addData(path_delay_data);
        }

        return true;
      };

      return add_default_delay_data(AnalysisMode::kMax) |
             add_default_delay_data(AnalysisMode::kMin);
    };

    if (io_delays.empty()) {
      return synthesize_default_input_arrival();
    }

    bool created = false;
    for (auto* io_delay : io_delays) {
      if (!io_delay) {
        continue;
      }

      auto construct_delay_data = [&](AnalysisMode analysis_mode) {
        auto clock_datas =
            !io_delay->isClockFall()
                ? the_vertex->getClockData(analysis_mode, TransType::kRise)
                : the_vertex->getClockData(analysis_mode, TransType::kFall);
        if (clock_datas.empty()) {
          return;
        }

        StaClockData* launch_clock_data = nullptr;
        for (auto* clock_data : clock_datas) {
          auto* candidate_clock_data = dynamic_cast<StaClockData*>(clock_data);
          if (!candidate_clock_data || !candidate_clock_data->get_prop_clock()) {
            continue;
          }

          if (candidate_clock_data->get_prop_clock()->get_clock_name() ==
                  io_delay->get_clock_name() ||
              !launch_clock_data) {
            launch_clock_data = candidate_clock_data;
          }

          if (candidate_clock_data->get_prop_clock()->get_clock_name() ==
              io_delay->get_clock_name()) {
            break;
          }
        }

        if (!launch_clock_data) {
          return;
        }

        auto add_delay_data = [&](TransType data_trans_type) {
          auto* path_delay_data = new StaPathDelayData(
              analysis_mode, data_trans_type, 0, launch_clock_data, the_vertex);
          path_delay_data->set_data_epoch(_characterization_epoch);
          path_delay_data->set_launch_delay_data(path_delay_data);
          the_vertex->addData(path_delay_data);
          created = true;
        };

        if (io_delay->isRise()) {
          add_delay_data(TransType::kRise);
        }
        if (io_delay->isFall()) {
          add_delay_data(TransType::kFall);
        }
      };

      if (io_delay->isMax()) {
        construct_delay_data(AnalysisMode::kMax);
      }
      if (io_delay->isMin()) {
        construct_delay_data(AnalysisMode::kMin);
      }
    }

    return created;
  };

  snapshotFullStaClockPinSlew(the_graph);
  snapshotFullStaSeqCheckData(the_graph);
  resetCharacterizationPayload(the_graph);

  StaVertex* the_vertex;
  FOREACH_VERTEX(the_graph, the_vertex) {
    // the_vertex->setPathBasedPropagated(); // not use path base for run time.

    if (the_vertex->is_port()) {
      // set init slew or AT.
      auto* the_port = the_vertex->get_design_obj();
      if (the_port->isInput()) {
        the_vertex->initSlewData(
            0, true, true,
            _characterization_epoch);  // TODO(to taosimin) need decide the
        // discrete init slew data point.
        seed_preserved_full_sta_input_slew(the_vertex);
        if (!init_input_port_delay_data(the_vertex)) {
          the_vertex->initPathDelayData(0, true, _characterization_epoch);
        }
      }
    } else if (the_vertex->is_clock()) {
      // Clock-to-output characterization needs fresh epoch-local launch data,
      // but it must preserve the full-STA clock-pin transition that drove the
      // original arc delay.
      the_vertex->initSlewData(0, true, true, _characterization_epoch);
      seed_preserved_clock_slew(the_vertex);
      init_clock_path_delay_data(the_vertex);
    }
  }

  return 1;
}

/**
 * @brief collect the interface logic endpoint, so then we can propagate slew
 * delay AT from the endpoint.
 *
 * @return unsigned
 */
unsigned StaCharacterTiming::collectInterfaceLogicEndPoint(
    StaGraph* the_graph) {
  // capture the interface logic endpoint.
  // traverse from the port vertex to the first sequential cell, collect the
  // endpoints.
  _state = kCollectEndpoints;
  StaVertex* port_vertex;
  FOREACH_PORT_VERTEX(the_graph, port_vertex) {
    auto* the_port = port_vertex->get_design_obj();
    if (the_port->isInput()) {
      _current_port_vertex = port_vertex;
      (*this)(port_vertex);
    } else {
      _interface_logic_endpoints.insert(port_vertex);
    }
  }

  return 1;
}

/**
 * @brief check and break loop before propagated.
 *
 * @param the_graph
 * @return unsigned
 */
unsigned StaCharacterTiming::checkAndBreakLoop(StaGraph* the_graph) {
  StaGraph logic_graph(the_graph->get_nl());

  for (auto* the_end_point : _interface_logic_endpoints) {
    logic_graph.addEndVertex(the_end_point);
  }

  StaVertex* port_vertex;
  FOREACH_PORT_VERTEX(the_graph, port_vertex) {
    if (port_vertex->is_start()) {
      logic_graph.addStartVertex(port_vertex);
    }
  }

  StaCombLoopCheck comb_loop_check;
  comb_loop_check(&logic_graph);

  return 1;
}

/**
 * @brief propagate slew from the port to the first sequential cell
 * endpoint.
 *
 * @param the_graph
 * @return unsigned
 */
unsigned StaCharacterTiming::propagateSlew(StaGraph* the_graph) {
  LOG_INFO << "character timing propagate slew start.";
  _state = kPropagateSlew;
  for (auto* the_end_point : _interface_logic_endpoints) {
    _current_logic_endpoint = the_end_point;
    (*this)(the_end_point);
  }
  _current_logic_endpoint = nullptr;

  LOG_INFO << "character timing propagate slew end.";

  return 1;
}

/**
 * @brief propagate delay from the port to the first sequential cell
 * endpoint.
 *
 * @param the_graph
 * @return unsigned
 */
unsigned StaCharacterTiming::propagateDelay(StaGraph* the_graph) {
  LOG_INFO << "character timing propagate delay start.";
  _state = kPropagateDelay;
  for (auto* the_end_point : _interface_logic_endpoints) {
    _current_logic_endpoint = the_end_point;
    (*this)(the_end_point);
  }
  _current_logic_endpoint = nullptr;

  LOG_INFO << "character timing propagate delay end.";

  return 1;
}

/**
 * @brief propagate AT from port to the first sequential cell..
 *
 * @param port_vertex
 * @return unsigned
 */
unsigned StaCharacterTiming::propagateATFromPort(StaGraph* the_graph) {
  LOG_INFO << "character timing propagate AT start.";
  _state = kPropagateATFromPort;
  for (auto* the_end_point : _interface_logic_endpoints) {
    _current_logic_endpoint = the_end_point;
    (*this)(the_end_point);
  }
  _current_logic_endpoint = nullptr;

  LOG_INFO << "character timing propagate AT end.";
  return 1;
}

/**
 * @brief propagate RT from endpoint to port, if reach to the clock pin, need
 * mark the propagated port as clock port.
 *
 * @param the_graph
 * @return unsigned
 */
unsigned StaCharacterTiming::backPropagateRTToPort(StaGraph* the_graph) {
  LOG_INFO << "character timing propagate RT start.";

  {
    _state = kBackPropagateRTToPort;

    unsigned num_threads = getNumThreads();
    ThreadPool pool(num_threads);

    StaVertex* port_vertex;
    FOREACH_PORT_VERTEX(the_graph, port_vertex) {
      if (port_vertex->is_start()) {
#if 0
      (*this)(port_vertex);
#else
        // enqueue and store future
        pool.enqueue([this](StaVertex* port_vertex) { (*this)(port_vertex); },
                     port_vertex);

#endif
      }
    }
  }

  LOG_INFO << "character timing propagate RT end.";
  return 1;
}

unsigned StaCharacterTiming::analyzeLocalSeqChecks(StaGraph* the_graph) {
  auto* ista = Sta::getOrCreateSta();
  if (!ista) {
    return 0;
  }

  StaGraph logic_graph(the_graph->get_nl());
  size_t endpoint_count = 0;
  for (auto* endpoint_vertex : _interface_logic_endpoints) {
    if (!endpoint_vertex ||
        (!endpoint_vertex->is_port() && !endpoint_vertex->is_end())) {
      continue;
    }

    logic_graph.addEndVertex(endpoint_vertex);
    ++endpoint_count;
  }

  if (endpoint_count == 0) {
    return 1;
  }

  LOG_INFO << "character timing local seq analyze start.";
  const auto saved_analysis_mode = ista->get_analysis_mode();
  ista->set_analysis_mode(AnalysisMode::kMaxMin);

  StaAnalyze analyze;
  const unsigned is_ok = analyze(&logic_graph);

  ista->set_analysis_mode(saved_analysis_mode);

  LOG_INFO << "character timing local seq analyze end. endpoints="
           << endpoint_count << " status=" << is_ok;

  return is_ok;
}

/**
 * @brief generate the timing model as lib format.
 *
 * @param model_path
 * @return unsigned
 */


}  // namespace ista
