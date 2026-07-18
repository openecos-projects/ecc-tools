unsigned StaCharacterTiming::genTimingModel(StaGraph* the_graph,
                                            const char* model_path) {
  LOG_INFO << "gen timing model start.";
  _state = kGenTimingModel;

  _design_timing_model =
      std::make_unique<LibLibrary>(the_graph->get_nl()->get_name());
  if (auto* ista = Sta::getOrCreateSta(); ista) {
    auto* reference_lib = [&]() -> LibLibrary* {
      std::map<LibLibrary*, std::pair<int, int>> lib_usage;
      Instance* inst = nullptr;
      FOREACH_INSTANCE(the_graph->get_nl(), inst) {
        auto* inst_cell = inst->get_inst_cell();
        auto* owner_lib = inst_cell ? inst_cell->get_owner_lib() : nullptr;
        if (!owner_lib) {
          continue;
        }

        auto& [total_count, stdcell_count] = lib_usage[owner_lib];
        ++total_count;
        if (!inst_cell->isMacroCell()) {
          ++stdcell_count;
        }
      }

      LibLibrary* selected_lib = nullptr;
      auto selected_key = std::make_tuple(-1, -1, std::string());
      for (const auto& [lib, usage] : lib_usage) {
        auto current_key =
            std::make_tuple(usage.second, usage.first, lib->get_lib_name());
        if (!selected_lib || current_key > selected_key) {
          selected_lib = lib;
          selected_key = current_key;
        }
      }

      if (selected_lib) {
        return selected_lib;
      }

      auto& source_libs = ista->getAllLib();
      return source_libs.empty() ? nullptr : source_libs.front().get();
    }();

    if (reference_lib) {
      if (const auto& comment = reference_lib->get_comment(); comment) {
        _design_timing_model->set_comment(*comment);
      }
      if (const auto& simulation = reference_lib->get_simulation();
          simulation) {
        _design_timing_model->set_simulation(*simulation);
      }
      for (const auto& feature : reference_lib->get_library_features()) {
        _design_timing_model->add_library_feature(feature);
      }
      if (const auto& leakage_power_unit =
              reference_lib->get_leakage_power_unit();
          leakage_power_unit) {
        _design_timing_model->set_leakage_power_unit(*leakage_power_unit);
      }
      if (const auto& current_unit = reference_lib->get_current_unit_name();
          current_unit) {
        _design_timing_model->set_current_unit_name(*current_unit);
      }
      if (const auto& voltage_unit = reference_lib->get_voltage_unit_name();
          voltage_unit) {
        _design_timing_model->set_voltage_unit_name(*voltage_unit);
      }
      _design_timing_model->set_cap_unit(reference_lib->get_cap_unit());
      _design_timing_model->set_resistance_unit(
          reference_lib->get_resistance_unit());
      _design_timing_model->set_time_unit(reference_lib->get_time_unit());
      if (const auto& nom_process = reference_lib->get_nom_process();
          nom_process) {
        _design_timing_model->set_nom_process(*nom_process);
      }
      _design_timing_model->set_nom_voltage(reference_lib->get_nom_voltage());
      if (const auto& nom_temperature = reference_lib->get_nom_temperature();
          nom_temperature) {
        _design_timing_model->set_nom_temperature(*nom_temperature);
      }
      _design_timing_model->set_slew_lower_threshold_pct_rise(
          reference_lib->get_slew_lower_threshold_pct_rise() * 100.0);
      _design_timing_model->set_slew_upper_threshold_pct_rise(
          reference_lib->get_slew_upper_threshold_pct_rise() * 100.0);
      _design_timing_model->set_slew_lower_threshold_pct_fall(
          reference_lib->get_slew_lower_threshold_pct_fall() * 100.0);
      _design_timing_model->set_slew_upper_threshold_pct_fall(
          reference_lib->get_slew_upper_threshold_pct_fall() * 100.0);
      _design_timing_model->set_input_threshold_pct_rise(
          reference_lib->get_input_threshold_pct_rise() * 100.0);
      _design_timing_model->set_input_threshold_pct_fall(
          reference_lib->get_input_threshold_pct_fall() * 100.0);
      _design_timing_model->set_output_threshold_pct_rise(
          reference_lib->get_output_threshold_pct_rise() * 100.0);
      _design_timing_model->set_output_threshold_pct_fall(
          reference_lib->get_output_threshold_pct_fall() * 100.0);
      _design_timing_model->set_slew_derate_from_library(
          reference_lib->get_slew_derate_from_library());

      if (const auto& default_max_transition =
              reference_lib->get_default_max_transition();
          default_max_transition) {
        _design_timing_model->set_default_max_transition(
            *default_max_transition);
      }
      if (const auto& default_max_fanout =
              reference_lib->get_default_max_fanout();
          default_max_fanout) {
        _design_timing_model->set_default_max_fanout(*default_max_fanout);
      }
      if (const auto& default_fanout_load =
              reference_lib->get_default_fanout_load();
          default_fanout_load) {
        _design_timing_model->set_default_fanout_load(*default_fanout_load);
      }
    }
  }
  auto design_timing_cell = std::make_unique<LibCell>(
      the_graph->get_nl()->get_name(), _design_timing_model.get());
  std::vector<std::string> check_source_rows;
  check_source_rows.emplace_back(
      "port\trelated_pin\ttiming_type\tinput_trans\tconstraint_ns\t"
      "source_tag\tcheck_margin_source\tlocal_seq_match_status\t"
      "local_seq_binding_status\tpreserved_seq_match_status\t"
      "exported_check_pair_binding_status");
  design_timing_cell->set_is_macro();

  double design_cell_area = 0.0;
  Instance* inst = nullptr;
  FOREACH_INSTANCE(the_graph->get_nl(), inst) {
    if (auto* inst_cell = inst->get_inst_cell(); inst_cell) {
      design_cell_area += inst_cell->get_cell_area();
    }
  }
  design_timing_cell->set_cell_area(design_cell_area);

  auto template_variable_name = [](LibLutTableTemplate::Variable variable) {
    switch (variable) {
      case LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE:
        return "total_output_net_capacitance";
      case LibLutTableTemplate::Variable::INPUT_NET_TRANSITION:
        return "input_net_transition";
      case LibLutTableTemplate::Variable::CONSTRAINED_PIN_TRANSITION:
        return "constrained_pin_transition";
      case LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION:
        return "related_pin_transition";
      case LibLutTableTemplate::Variable::INPUT_TRANSITION_TIME:
        return "input_transition_time";
      case LibLutTableTemplate::Variable::TIME:
        return "time";
      case LibLutTableTemplate::Variable::INPUT_VOLTAGE:
        return "input_voltage";
      case LibLutTableTemplate::Variable::OUTPUT_VOLTAGE:
        return "output_voltage";
      case LibLutTableTemplate::Variable::INPUT_NOISE_HEIGHT:
        return "input_noise_height";
      case LibLutTableTemplate::Variable::INPUT_NOISE_WIDTH:
        return "input_noise_width";
      case LibLutTableTemplate::Variable::NORMALIZED_VOLTAGE:
        return "normalized_voltage";
    }
    return "total_output_net_capacitance";
  };

  struct LoadAxisInfo {
    LibLutTableTemplate::Variable variable;
    std::vector<double> values;
  };

  std::map<std::string, LibLutTableTemplate*> load_template_cache;
  auto clone_axis = [](const std::vector<double>& axis_values) {
    auto axis = std::make_unique<LibAxis>("index");
    std::vector<std::unique_ptr<LibAttrValue>> values;
    values.reserve(axis_values.size());
    for (double axis_value : axis_values) {
      values.emplace_back(std::make_unique<LibFloatValue>(axis_value));
    }
    axis->set_axis_values(std::move(values));
    return axis;
  };

  auto get_or_create_load_template =
      [this, &clone_axis, &load_template_cache, &template_variable_name](
          const LoadAxisInfo& load_axis_info) -> LibLutTableTemplate* {
    std::string cache_key =
        std::to_string(static_cast<int>(load_axis_info.variable));
    for (double axis_value : load_axis_info.values) {
      cache_key.append("|");
      cache_key.append(std::to_string(axis_value));
    }

    if (auto iter = load_template_cache.find(cache_key);
        iter != load_template_cache.end()) {
      return iter->second;
    }

    std::string template_name =
        "template_" + std::to_string(load_template_cache.size() + 1);
    auto lut_template =
        std::make_unique<LibLutTableTemplate>(template_name.c_str());
    lut_template->set_template_variable1(
        template_variable_name(load_axis_info.variable));
    lut_template->addAxis(clone_axis(load_axis_info.values));
    auto* lut_template_ptr = lut_template.get();
    _design_timing_model->addLutTemplate(std::move(lut_template));
    load_template_cache.emplace(std::move(cache_key), lut_template_ptr);
    return lut_template_ptr;
  };

  auto extract_load_axis = [](LibTable* reference_table)
      -> std::optional<LoadAxisInfo> {
    if (!reference_table) {
      return std::nullopt;
    }

    auto* table_template = reference_table->get_table_template();
    if (!table_template) {
      return std::nullopt;
    }

    auto& axes = reference_table->get_axes();
    if (axes.empty()) {
      return std::nullopt;
    }

    auto build_axis_info = [&axes](int axis_index,
                                   LibLutTableTemplate::Variable variable)
        -> std::optional<LoadAxisInfo> {
      if (axis_index >= axes.size()) {
        return std::nullopt;
      }

      std::vector<double> axis_values;
      auto& raw_axis_values = axes[axis_index]->get_axis_values();
      axis_values.reserve(raw_axis_values.size());
      for (const auto& axis_value : raw_axis_values) {
        axis_values.emplace_back(
            dynamic_cast<LibFloatValue*>(axis_value.get())->getFloatValue());
      }

      if (axis_values.empty()) {
        return std::nullopt;
      }

      return LoadAxisInfo{variable, std::move(axis_values)};
    };

    if (auto variable1 = table_template->get_template_variable1();
        variable1
        && *variable1
               == LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE) {
      return build_axis_info(0, *variable1);
    }

    if (auto variable2 = table_template->get_template_variable2();
        variable2
        && *variable2
               == LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE) {
      return build_axis_info(1, *variable2);
    }

    return std::nullopt;
  };

  auto convert_check_constrained_slew_arg =
      [](LibArc* check_arc, double constrained_slew_ns) -> double {
    if (!check_arc || !check_arc->get_owner_cell() ||
        !check_arc->get_owner_cell()->get_owner_lib()) {
      return constrained_slew_ns;
    }

    switch (check_arc->get_owner_cell()->get_owner_lib()->get_time_unit()) {
      case TimeUnit::kPS:
        return constrained_slew_ns * 1e3;
      case TimeUnit::kFS:
        return constrained_slew_ns * 1e6;
      case TimeUnit::kNS:
      default:
        return constrained_slew_ns;
    }
  };

  auto lookup_check_margin_ns =
      [&convert_check_constrained_slew_arg](
          LibArc* check_arc, TransType trans_type, double related_slew_ns,
          double constrained_slew_ns) -> double {
    if (!check_arc) {
      return 0.0;
    }

    return check_arc->getDelayOrConstrainCheckNs(
        trans_type, related_slew_ns,
        convert_check_constrained_slew_arg(check_arc, constrained_slew_ns));
  };

  auto build_scalar_table = [](LibTable::TableType table_type, double table_value) {
    auto table = std::make_unique<LibTable>(table_type, nullptr);
    table->addTableValue(std::make_unique<LibFloatValue>(table_value));
    return table;
  };

  auto convert_lib_cap_to_internal_pf = [this](double raw_cap) {
    return ConvertCapUnit(_design_timing_model->get_cap_unit(),
                          CapacitiveUnit::kPF, raw_cap);
  };

  auto resolve_export_port_type = [](DesignObject* design_port) {
    if (auto* sta_port = dynamic_cast<Port*>(design_port); sta_port) {
      auto* timing_engine = TimingEngine::getOrCreateTimingEngine();
      auto* db_adapter = timing_engine
                             ? dynamic_cast<TimingIDBAdapter*>(
                                   timing_engine->get_db_adapter())
                             : nullptr;
      auto* db_port = db_adapter ? db_adapter->staToDb(sta_port) : nullptr;
      auto* db_term = db_port ? db_port->get_term() : nullptr;
      if (db_term && (db_term->is_power() || db_term->is_ground())) {
        return LibPort::LibertyPortType::kInput;
      }
    }

    if (design_port->isInput()) {
      return LibPort::LibertyPortType::kInput;
    }
    if (design_port->isOutput()) {
      return LibPort::LibertyPortType::kOutput;
    }
    if (design_port->isInout()) {
      return LibPort::LibertyPortType::kInOut;
    }
    return LibPort::LibertyPortType::kDefault;
  };

  // construct the data port to clock port check arc.
  auto construct_port_check_arc =
      [&design_timing_cell, &check_source_rows, &lookup_check_margin_ns, this](
                                      auto* port_vertex,
                                      AnalysisMode analysis_mode) {
    if (!_port_to_logic_endpoint.contains(port_vertex)) {
      return;
    }

    struct CheckArcCandidate {
      std::string related_pin;
      std::string timing_type;
      double rise_constraint = 0.0;
      double fall_constraint = 0.0;
      bool has_rise_constraint = false;
      bool has_fall_constraint = false;
      bool is_valid = false;
    };

    std::map<std::pair<std::string, std::string>, CheckArcCandidate>
        aggregated_candidates;
    auto logic_endpoints = _port_to_logic_endpoint.values(port_vertex);
    const bool trace_port =
        shouldTraceCharacterizationPin(port_vertex->getName().c_str());

    auto collect_check_arc_candidate = [&](auto* logic_endpoint,
                                           AnalysisMode constraint_mode,
                                           const char* rising_timing_type,
                                           const char* falling_timing_type) {
      for (TransType input_trans_type : {TransType::kRise, TransType::kFall}) {
        auto local_data_candidates = collectBoundaryDelayCandidatesForEpoch(
            logic_endpoint, constraint_mode, _characterization_epoch,
            port_vertex, input_trans_type);
        if (local_data_candidates.empty()) {
          if (trace_port) {
            LOG_INFO << "[character_timing][check-skip-data] port="
                     << port_vertex->getName() << " endpoint="
                     << logic_endpoint->getName() << " mode="
                     << analysisModeName(constraint_mode) << " input_trans="
                     << transTypeName(input_trans_type);
          }
          continue;
        }

        std::vector<StaArc*> endpoint_check_arcs;
        for (auto* endpoint_arc : logic_endpoint->get_snk_arcs()) {
          const bool mode_matches =
              (constraint_mode == AnalysisMode::kMax &&
               (endpoint_arc->isSetupArc() || endpoint_arc->isRecoveryArc())) ||
              (constraint_mode == AnalysisMode::kMin &&
               (endpoint_arc->isHoldArc() || endpoint_arc->isRemovalArc()));
          if (mode_matches) {
            endpoint_check_arcs.push_back(endpoint_arc);
          }
        }
        if (endpoint_check_arcs.empty()) {
          if (trace_port) {
            LOG_INFO << "[character_timing][check-skip-arc] port="
                     << port_vertex->getName() << " endpoint="
                     << logic_endpoint->getName() << " mode="
                     << analysisModeName(constraint_mode) << " input_trans="
                     << transTypeName(input_trans_type);
          }
          continue;
        }

        for (auto* endpoint_check_arc : endpoint_check_arcs) {
          auto* clk_point = endpoint_check_arc->get_src();
          auto clock_port_vertexes = _logic_clkpoint_to_port.values(clk_point);
          if (clock_port_vertexes.empty()) {
            LOG_INFO << clk_point->getName() << " clock port vertex is empty.";
            continue;
          }

          auto* clock_port_vertex = clock_port_vertexes.front();
          std::string timing_type = endpoint_check_arc->isRisingEdgeCheck()
                                        ? rising_timing_type
                                        : falling_timing_type;
          auto candidate_key =
              std::make_pair(clock_port_vertex->getName(), timing_type);
          auto& candidate = aggregated_candidates[candidate_key];
          if (!candidate.is_valid) {
            candidate.related_pin = candidate_key.first;
            candidate.timing_type = candidate_key.second;
            candidate.is_valid = true;
          }

          auto clock_trans_type = endpoint_check_arc->isRisingEdgeCheck()
                                      ? TransType::kRise
                                      : TransType::kFall;
          auto capture_analysis_mode =
              constraint_mode == AnalysisMode::kMax ? AnalysisMode::kMin
                                                    : AnalysisMode::kMax;
          auto local_clock_candidates = collectBoundaryDelayCandidatesForEpoch(
              clk_point, capture_analysis_mode, _characterization_epoch,
              clock_port_vertex, std::nullopt, clock_trans_type);

          auto* inst_check_arc = dynamic_cast<StaInstArc*>(endpoint_check_arc);
          auto* reference_check_arc =
              inst_check_arc ? inst_check_arc->get_lib_arc() : nullptr;
          auto accumulate_constraint = [&](double data_arrival_delta_ns,
                                           double clock_arrival_delta_ns,
                                           double check_margin_ns,
                                           double capture_edge_ns,
                                           const char* source_tag,
                                           const char* check_margin_source,
                                           const char* local_seq_match_status,
                                           const char* local_seq_binding_status,
                                           const char* preserved_seq_match_status,
                                           const char* exported_check_pair_binding_status) {
                double constraint_ns =
                    constraint_mode == AnalysisMode::kMax
                        ? data_arrival_delta_ns - clock_arrival_delta_ns +
                             check_margin_ns
                        : clock_arrival_delta_ns - data_arrival_delta_ns +
                              check_margin_ns;

                if (trace_port) {
                  LOG_INFO << "[character_timing][check] port="
                           << port_vertex->getName() << " endpoint="
                           << logic_endpoint->getName() << " mode="
                           << (constraint_mode == AnalysisMode::kMax ? "max"
                                                                      : "min")
                           << " input_trans="
                           << (input_trans_type == TransType::kRise ? "rise"
                                                                    : "fall")
                           << " timing_type=" << timing_type
                           << " related_clk=" << clock_port_vertex->getName()
                           << " source=" << source_tag
                           << " data_arrival_delta_ns="
                           << data_arrival_delta_ns
                           << " clock_arrival_delta_ns="
                           << clock_arrival_delta_ns
                           << " capture_edge_ns=" << capture_edge_ns
                           << " check_margin_ns=" << check_margin_ns
                           << " formula=data-clock+margin"
                           << " constraint_ps=" << constraint_ns;
                }

                if (input_trans_type == TransType::kRise) {
                  candidate.rise_constraint =
                      candidate.has_rise_constraint
                          ? std::max(candidate.rise_constraint, constraint_ns)
                          : constraint_ns;
                  candidate.has_rise_constraint = true;
                } else if (input_trans_type == TransType::kFall) {
                  candidate.fall_constraint =
                      candidate.has_fall_constraint
                          ? std::max(candidate.fall_constraint, constraint_ns)
                          : constraint_ns;
                  candidate.has_fall_constraint = true;
                }

                std::ostringstream row;
                row << port_vertex->getName() << '\t'
                    << clock_port_vertex->getName() << '\t' << timing_type
                    << '\t' << transTypeName(input_trans_type) << '\t'
                    << constraint_ns << '\t' << source_tag << '\t'
                    << check_margin_source << '\t' << local_seq_match_status
                    << '\t' << local_seq_binding_status << '\t'
                    << preserved_seq_match_status << '\t'
                    << exported_check_pair_binding_status;
                check_source_rows.push_back(row.str());

                return true;
              };

          auto clock_slew_ns = findVertexSlewNsForEpochWithStart(
              clk_point, capture_analysis_mode, clock_trans_type,
              _characterization_epoch, clock_port_vertex);
          if (!clock_slew_ns) {
            clock_slew_ns = findVertexSlewNsForEpoch(
                clk_point, capture_analysis_mode, clock_trans_type,
                _characterization_epoch);
          }
          if (!clock_slew_ns) {
            auto preserved_clock_iter =
                _preserved_clock_pin_slew_ns.find({clk_point, clock_trans_type});
            if (preserved_clock_iter != _preserved_clock_pin_slew_ns.end()) {
              clock_slew_ns = preserved_clock_iter->second;
            }
          }

          if (trace_port) {
            std::ostringstream local_data_arrival_deltas;
            local_data_arrival_deltas << "[";
            for (size_t i = 0; i < local_data_candidates.size(); ++i) {
              if (i != 0) {
                local_data_arrival_deltas << ",";
              }
              local_data_arrival_deltas
                  << FS_TO_NS(local_data_candidates[i].terms.delay_fs)
                                << ":" << transTypeName(
                                               local_data_candidates[i]
                                                   .terms.end_trans_type);
            }
            local_data_arrival_deltas << "]";

            std::ostringstream local_clock_arrival_deltas;
            local_clock_arrival_deltas << "[";
            for (size_t i = 0; i < local_clock_candidates.size(); ++i) {
              if (i != 0) {
                local_clock_arrival_deltas << ",";
              }
              auto clock_breakdown = rebuildPathDelayBreakdownNsForEpoch(
                  local_clock_candidates[i].delay_data, _characterization_epoch);
              local_clock_arrival_deltas
                  << FS_TO_NS(local_clock_candidates[i].terms.delay_fs)
                  << "(total)/"
                  << (clock_breakdown ? clock_breakdown->cell_ns : 0.0)
                  << "(cell)/"
                  << (clock_breakdown ? clock_breakdown->net_ns : 0.0)
                  << "(net):" << transTypeName(
                                     local_clock_candidates[i]
                                         .terms.start_trans_type);
            }
            local_clock_arrival_deltas << "]";

            std::ostringstream all_clock_candidates;
            all_clock_candidates << "[";
            auto all_clock_delay_candidates = collectBoundaryDelayCandidatesForEpoch(
                clk_point, capture_analysis_mode, _characterization_epoch,
                nullptr, std::nullopt, clock_trans_type);
            for (size_t i = 0; i < all_clock_delay_candidates.size(); ++i) {
              if (i != 0) {
                all_clock_candidates << ",";
              }
              const auto& clock_candidate = all_clock_delay_candidates[i];
              auto rebuilt_candidate_total_ns = rebuildPathDelayNsForEpoch(
                  clock_candidate.delay_data, _characterization_epoch);
              auto* candidate_launch_clock_data =
                  clock_candidate.delay_data
                      ? clock_candidate.delay_data->get_launch_clock_data()
                      : nullptr;
              auto* candidate_launch_delay_data =
                  clock_candidate.delay_data
                      ? clock_candidate.delay_data->get_launch_delay_data()
                      : nullptr;
              all_clock_candidates
                  << (clock_candidate.terms.start_vertex
                          ? clock_candidate.terms.start_vertex->getName()
                          : "null")
                  << "->"
                  << (clock_candidate.terms.end_vertex
                          ? clock_candidate.terms.end_vertex->getName()
                          : "null")
                  << ":start="
                  << FS_TO_NS(clock_candidate.terms.start_arrive_fs)
                  << ":end="
                  << FS_TO_NS(clock_candidate.terms.end_arrive_fs)
                  << ":delta="
                  << FS_TO_NS(clock_candidate.terms.delay_fs)
                  << ":stored="
                  << (clock_candidate.delay_data
                          ? FS_TO_NS(clock_candidate.delay_data->get_arrive_time())
                          : -1.0)
                  << ":rebuilt="
                  << (rebuilt_candidate_total_ns ? *rebuilt_candidate_total_ns
                                                 : -1.0)
                  << ":launch_clk="
                  << (candidate_launch_clock_data
                          ? FS_TO_NS(candidate_launch_clock_data->get_arrive_time())
                          : -1.0)
                  << ":launch_seed="
                  << (candidate_launch_delay_data &&
                              candidate_launch_delay_data->get_own_vertex()
                          ? candidate_launch_delay_data->get_own_vertex()->getName()
                          : "null")
                  << ":launch_seed_arrive="
                  << (candidate_launch_delay_data
                          ? FS_TO_NS(candidate_launch_delay_data->get_arrive_time())
                          : -1.0);
            }
            all_clock_candidates << "]";

            LOG_INFO << "[character_timing][check-local-candidates] port="
                     << port_vertex->getName() << " endpoint="
                     << logic_endpoint->getName() << " mode="
                     << analysisModeName(constraint_mode) << " input_trans="
                     << transTypeName(input_trans_type)
                     << " timing_type=" << timing_type
                     << " data_arrival_deltas_ns="
                     << local_data_arrival_deltas.str()
                     << " clock_arrival_deltas_ns="
                     << local_clock_arrival_deltas.str()
                     << " all_clock_candidates_ns="
                     << all_clock_candidates.str()
                     << " clock_slew_ns="
                     << (clock_slew_ns ? *clock_slew_ns : 0.0)
                     << " margin_source="
                     << (reference_check_arc ? "lib_arc" : "missing_lib_arc");
          }

          for (const auto& local_data_candidate : local_data_candidates) {
            const auto preserved_seq_iter =
                _preserved_seq_check_data.find(logic_endpoint);
            const std::vector<PreservedSeqCheckSnapshot> empty_seq_snapshots;
            const auto& preserved_seq_snapshots =
                preserved_seq_iter == _preserved_seq_check_data.end()
                    ? empty_seq_snapshots
                    : preserved_seq_iter->second;
            auto data_slew_ns = findVertexSlewNsForEpochWithStart(
                logic_endpoint, constraint_mode,
                local_data_candidate.terms.end_trans_type,
                _characterization_epoch,
                port_vertex, input_trans_type);
            if (!data_slew_ns) {
              data_slew_ns = findVertexSlewNsForEpoch(
                  logic_endpoint, constraint_mode,
                  local_data_candidate.terms.end_trans_type,
                  _characterization_epoch);
            }

            double check_margin_ns = 0.0;
            double check_margin_with_local_clock_slew_ns = 0.0;
            const double full_sta_check_margin_ns = FS_TO_NS(
                endpoint_check_arc->get_arc_delay(
                    constraint_mode, local_data_candidate.terms.end_trans_type));
            const auto preserved_full_sta_clock_slew =
                _preserved_full_sta_pin_slew_ns.find(std::make_tuple(
                    clk_point, capture_analysis_mode, clock_trans_type));
            const double check_clock_slew_ns =
                preserved_full_sta_clock_slew != _preserved_full_sta_pin_slew_ns.end()
                    ? preserved_full_sta_clock_slew->second
                    : clock_slew_ns.value_or(0.0);
            const double check_data_slew_ns = data_slew_ns.value_or(0.0);
            std::vector<StaSeqPathData*> local_seq_candidates;
            if (auto* ista = Sta::getOrCreateSta();
                ista && local_data_candidate.delay_data) {
              local_seq_candidates =
                  ista->getSeqData(logic_endpoint, local_data_candidate.delay_data);
            }
            if (trace_port) {
              LOG_INFO << "[character_timing][check-local-seq-count] port="
                       << port_vertex->getName() << " endpoint="
                       << logic_endpoint->getName() << " mode="
                       << analysisModeName(constraint_mode) << " input_trans="
                       << transTypeName(input_trans_type)
                       << " timing_type=" << timing_type
                       << " local_seq_candidates="
                       << local_seq_candidates.size();
            }
            const auto local_seq_match = findExactLocalSeqDataForExport(
                local_seq_candidates, local_data_candidate, clk_point,
                endpoint_check_arc, capture_analysis_mode, clock_trans_type,
                _characterization_epoch);
            auto* local_seq_data = local_seq_match.seq_data;
            const auto local_seq_capture_clock_terms =
                (local_seq_data && local_seq_data->get_capture_clock_data())
                    ? extractBoundaryDelayTerms(local_seq_data->get_capture_clock_data()
                                                    ->getPathData())
                    : std::nullopt;
            const auto local_seq_target_clock_delay_ns =
                local_seq_capture_clock_terms
                    ? std::optional<double>(
                          FS_TO_NS(local_seq_capture_clock_terms->delay_fs))
                    : std::nullopt;
            const auto local_seq_raw_capture_clock_arrive_ns =
                (local_seq_data && local_seq_data->get_capture_clock_data())
                    ? std::optional<double>(FS_TO_NS(
                          local_seq_data->get_capture_clock_data()
                              ->get_arrive_time()))
                    : std::nullopt;
            const auto local_seq_check_margin_ns =
                local_seq_data
                    ? std::optional<double>(
                          FS_TO_NS(local_seq_data->get_constrain_value()))
                    : std::nullopt;
            const auto local_seq_check_pair_binding =
                local_seq_data ? local_seq_data->get_check_pair_binding()
                               : nullptr;
            const auto local_seq_binding_check_margin_ns =
                bindingCheckMarginNs(local_seq_check_pair_binding);
            const auto local_seq_binding_clock_slew_ns =
                bindingSlewNs(local_seq_check_pair_binding, true);
            const auto local_seq_binding_data_slew_ns =
                bindingSlewNs(local_seq_check_pair_binding, false);
            const char* local_seq_binding_status = checkPairBindingStatus(
                local_seq_check_pair_binding, _characterization_epoch,
                endpoint_check_arc, constraint_mode, clock_trans_type,
                local_data_candidate.terms.end_trans_type);
            const bool local_seq_clock_proven =
                local_seq_data && local_seq_capture_clock_terms.has_value();
            bool local_seq_binding_exact = checkPairBindingIsExact(
                local_seq_check_pair_binding, _characterization_epoch,
                endpoint_check_arc, constraint_mode, clock_trans_type,
                local_data_candidate.terms.end_trans_type);
            const char* local_seq_match_status = "missing";

            if (local_seq_data) {
              const auto local_seq_data_path_sig =
                  extractPathVertexSignature(
                      local_seq_data->getPathDelayData());
              const auto local_seq_clock_path_sig =
                  local_seq_data->get_capture_clock_data()
                      ? extractPathVertexSignature(
                            local_seq_data->get_capture_clock_data()
                                ->getPathData())
                      : std::nullopt;
              const auto exact_data_slew_match =
                  local_seq_data_path_sig
                      ? findExactSlewDataForPath(
                            logic_endpoint, constraint_mode,
                            local_data_candidate.terms.end_trans_type,
                            _characterization_epoch,
                            *local_seq_data_path_sig)
                      : ExactSlewDataMatch{};
              const auto exact_clock_slew_match =
                  local_seq_clock_path_sig
                      ? findExactSlewDataForPath(
                            clk_point, capture_analysis_mode, clock_trans_type,
                            _characterization_epoch,
                            *local_seq_clock_path_sig)
                      : ExactSlewDataMatch{};
              if (local_seq_binding_exact && local_seq_check_pair_binding) {
                if (exact_data_slew_match.slew_data &&
                    local_seq_check_pair_binding->data_slew_data !=
                        exact_data_slew_match.slew_data) {
                  local_seq_binding_exact = false;
                  local_seq_binding_status = "binding_data_path_mismatch";
                }
                if (local_seq_binding_exact && exact_clock_slew_match.slew_data &&
                    local_seq_check_pair_binding->clock_slew_data !=
                        exact_clock_slew_match.slew_data) {
                  local_seq_binding_exact = false;
                  local_seq_binding_status = "binding_clock_path_mismatch";
                }
              }
              const auto exact_data_slew_ns =
                  local_seq_binding_data_slew_ns
                      ? (local_seq_binding_exact ? local_seq_binding_data_slew_ns
                                                 : (exact_data_slew_match.slew_data
                                                        ? std::optional<double>(FS_TO_NS(
                                                              exact_data_slew_match.slew_data->get_slew()))
                                                        : local_seq_binding_data_slew_ns))
                      : (exact_data_slew_match.slew_data
                      ? std::optional<double>(FS_TO_NS(
                            exact_data_slew_match.slew_data->get_slew()))
                      : std::nullopt);
              const auto exact_clock_path_slew_ns =
                  local_seq_binding_clock_slew_ns
                      ? (local_seq_binding_exact ? local_seq_binding_clock_slew_ns
                                                 : (exact_clock_slew_match.slew_data
                                                        ? std::optional<double>(FS_TO_NS(
                                                              exact_clock_slew_match.slew_data->get_slew()))
                                                        : local_seq_binding_clock_slew_ns))
                      : (exact_clock_slew_match.slew_data
                      ? std::optional<double>(FS_TO_NS(
                            exact_clock_slew_match.slew_data->get_slew()))
                      : std::nullopt);
              const char* exact_data_slew_status =
                  local_seq_binding_exact
                      ? "exact"
                      : (local_seq_check_pair_binding
                             ? local_seq_binding_status
                             : (exact_data_slew_match.match_count == 1
                                    ? "exact"
                                    : (exact_data_slew_match.match_count == 0
                                           ? "missing"
                                           : "ambiguous")));
              const char* exact_clock_slew_status =
                  local_seq_binding_exact
                      ? "exact"
                      : (local_seq_check_pair_binding
                             ? local_seq_binding_status
                             : (exact_clock_slew_match.match_count == 1
                                    ? "exact"
                                    : (exact_clock_slew_match.match_count == 0
                                           ? "missing"
                                           : "ambiguous")));
              local_seq_match_status =
                  local_seq_match.match_count == 0
                      ? "missing"
                      : (local_seq_match.match_count == 1
                             ? (local_seq_binding_exact
                                    ? "exact"
                                    : (local_seq_binding_status == std::string("missing")
                                           ? (local_seq_clock_proven
                                                  ? "clock_unproven"
                                                  : "missing")
                                           : local_seq_binding_status))
                             : "ambiguous");
              const double selected_check_clock_slew_ns = check_clock_slew_ns;
              const double data_arrival_delta_ns =
                  FS_TO_NS(local_data_candidate.terms.delay_fs);
              const double target_clock_delay_ns =
                  local_seq_target_clock_delay_ns.value_or(0.0);
              double check_margin_ns = 0.0;
              const char* local_seq_margin_source = "missing";
              if (reference_check_arc && exact_data_slew_ns) {
                check_margin_ns = lookup_check_margin_ns(
                    reference_check_arc,
                    local_data_candidate.terms.end_trans_type,
                    selected_check_clock_slew_ns, *exact_data_slew_ns);
                local_seq_margin_source = "exact_data_plus_check_clock_slew";
              } else if (reference_check_arc) {
                check_margin_ns = lookup_check_margin_ns(
                    reference_check_arc,
                    local_data_candidate.terms.end_trans_type,
                    selected_check_clock_slew_ns, check_data_slew_ns);
                local_seq_margin_source = "fallback_lib_arc";
              } else if (local_seq_binding_exact &&
                         local_seq_binding_check_margin_ns) {
                check_margin_ns = *local_seq_binding_check_margin_ns;
                local_seq_margin_source = "binding_sampled_margin";
              } else if (local_seq_check_margin_ns) {
                check_margin_ns = *local_seq_check_margin_ns;
                local_seq_margin_source = "bucketed_seq_margin";
              }

              if (trace_port) {
                LOG_INFO << "[character_timing][check-local-seq] port="
                         << port_vertex->getName() << " endpoint="
                         << logic_endpoint->getName() << " mode="
                         << analysisModeName(constraint_mode) << " input_trans="
                         << transTypeName(input_trans_type)
                         << " timing_type=" << timing_type
                         << " related_clk=" << clock_port_vertex->getName()
                         << " local_seq_match_status="
                         << local_seq_match_status
                         << " local_seq_match_count="
                         << local_seq_match.match_count
                         << " local_seq_binding_status="
                         << local_seq_binding_status
                         << " local_seq_binding_clock_slew_ns="
                         << (local_seq_binding_clock_slew_ns
                                 ? *local_seq_binding_clock_slew_ns
                                 : -1.0)
                         << " local_seq_binding_data_slew_ns="
                         << (local_seq_binding_data_slew_ns
                                 ? *local_seq_binding_data_slew_ns
                                 : -1.0)
                         << " local_seq_binding_check_margin_ns="
                         << (local_seq_binding_check_margin_ns
                                 ? *local_seq_binding_check_margin_ns
                                 : -1.0)
                         << " launch_clk_arrive_ns="
                         << FS_TO_NS(
                                local_seq_data->get_launch_clock_data()
                                    ->get_arrive_time())
                         << " capture_clk_arrive_ns="
                         << FS_TO_NS(
                                local_seq_data->get_capture_clock_data()
                                    ->get_arrive_time())
                         << " launch_edge_ns="
                         << FS_TO_NS(local_seq_data->getLaunchEdge())
                         << " capture_edge_ns="
                         << FS_TO_NS(local_seq_data->getCaptureEdge())
                         << " raw_capture_clk_arrive_ns="
                         << (local_seq_raw_capture_clock_arrive_ns
                                 ? *local_seq_raw_capture_clock_arrive_ns
                                 : -1.0)
                         << " capture_clk_boundary_delay_ns="
                         << (local_seq_target_clock_delay_ns
                                 ? *local_seq_target_clock_delay_ns
                                 : -1.0)
                         << " data_arrival_delta_ns="
                         << data_arrival_delta_ns
                         << " target_clock_delay_ns="
                         << target_clock_delay_ns
                         << " local_seq_check_margin_ns="
                         << (local_seq_check_margin_ns
                                 ? *local_seq_check_margin_ns
                                 : -1.0)
                         << " exact_data_slew_status="
                         << exact_data_slew_status
                         << " exact_data_slew_count="
                         << exact_data_slew_match.match_count
                         << " exact_data_slew_ns="
                         << (exact_data_slew_ns ? *exact_data_slew_ns : -1.0)
                         << " exact_clock_slew_status="
                         << exact_clock_slew_status
                         << " exact_clock_slew_count="
                         << exact_clock_slew_match.match_count
                         << " exact_clock_path_slew_ns="
                         << (exact_clock_path_slew_ns
                                 ? *exact_clock_path_slew_ns
                                 : -1.0)
                         << " selected_check_clock_slew_ns="
                         << selected_check_clock_slew_ns
                         << " local_seq_export_status="
                         << "diagnostic_only"
                         << " local_seq_margin_source="
                         << local_seq_margin_source
                         << " check_margin_ns=" << check_margin_ns;
              }
            }

            if (local_clock_candidates.empty()) {
              if (trace_port) {
                LOG_INFO << "[character_timing][check-clock-miss] port="
                         << port_vertex->getName() << " endpoint="
                         << logic_endpoint->getName() << " mode="
                         << analysisModeName(constraint_mode)
                         << " input_trans=" << transTypeName(input_trans_type)
                         << " timing_type=" << timing_type << " clock_port="
                         << clock_port_vertex->getName() << " clk_point="
                         << clk_point->getName() << " clock_trans="
                         << transTypeName(clock_trans_type)
                         << " local_seq_match_status="
                         << local_seq_match_status
                         << " local_seq_match_count="
                         << local_seq_match.match_count;
              }
              continue;
            }

            for (const auto& local_clock_candidate : local_clock_candidates) {
              auto data_breakdown = rebuildPathDelayBreakdownNsForEpoch(
                  local_data_candidate.delay_data, _characterization_epoch);
              auto clock_breakdown = rebuildPathDelayBreakdownNsForEpoch(
                  local_clock_candidate.delay_data, _characterization_epoch);
              auto clock_rebuilt_total_ns = rebuildPathDelayNsForEpoch(
                  local_clock_candidate.delay_data, _characterization_epoch);
              auto* clock_launch_clock_data =
                  local_clock_candidate.delay_data
                      ? local_clock_candidate.delay_data->get_launch_clock_data()
                      : nullptr;
              const auto preserved_seq_match =
                  findExactSeqCheckSnapshotForExport(
                      preserved_seq_snapshots, local_data_candidate,
                      local_clock_candidate, clk_point, clock_port_vertex,
                      endpoint_check_arc, capture_analysis_mode,
                      clock_trans_type);
              const auto relaxed_preserved_seq_match =
                  preserved_seq_match.snapshot
                      ? ExactSeqCheckSnapshotMatch{}
                      : findRelaxedSeqCheckSnapshotForExport(
                            preserved_seq_snapshots, local_data_candidate,
                            clk_point, clock_port_vertex, endpoint_check_arc,
                            capture_analysis_mode, clock_trans_type);
              const auto* preserved_seq_snapshot =
                  preserved_seq_match.snapshot
                      ? preserved_seq_match.snapshot
                      : relaxed_preserved_seq_match.snapshot;
              const auto* local_seq_check_pair_binding_for_export =
                  local_seq_data ? local_seq_data->get_check_pair_binding()
                                 : nullptr;
              const char* preserved_seq_match_status =
                  preserved_seq_match.match_count == 1
                      ? "exact"
                      : (relaxed_preserved_seq_match.match_count == 1
                             ? "relaxed"
                             : ((preserved_seq_match.match_count == 0 &&
                                 relaxed_preserved_seq_match.match_count == 0)
                                    ? "missing"
                                    : "ambiguous"));
              std::optional<PreservedSeqCheckSnapshot>
                  local_seq_snapshot_fallback;
              if (!preserved_seq_snapshot && local_seq_data &&
                  local_seq_check_pair_binding_for_export &&
                  local_seq_capture_clock_terms) {
                PreservedSeqCheckSnapshot fallback_snapshot;
                fallback_snapshot.check_arc = endpoint_check_arc;
                fallback_snapshot.capture_clock_vertex = clk_point;
                fallback_snapshot.capture_clock_start_vertex =
                    local_seq_capture_clock_terms->start_vertex;
                fallback_snapshot.data_start_vertex =
                    local_data_candidate.terms.start_vertex;
                fallback_snapshot.capture_analysis_mode =
                    capture_analysis_mode;
                fallback_snapshot.data_start_trans_type =
                    local_data_candidate.terms.start_trans_type;
                fallback_snapshot.data_end_trans_type =
                    local_data_candidate.terms.end_trans_type;
                fallback_snapshot.clock_trans_type = clock_trans_type;
                fallback_snapshot.data_start_arrive_fs =
                    local_data_candidate.terms.start_arrive_fs;
                fallback_snapshot.data_end_arrive_fs =
                    local_data_candidate.terms.end_arrive_fs;
                fallback_snapshot.seq_arrive_time_fs =
                    local_data_candidate.delay_data
                        ? local_data_candidate.delay_data->get_arrive_time()
                        : 0;
                fallback_snapshot.capture_clock_start_arrive_fs =
                    local_seq_capture_clock_terms->start_arrive_fs;
                fallback_snapshot.capture_clock_end_arrive_fs =
                    local_seq_capture_clock_terms->end_arrive_fs;
                fallback_snapshot.capture_edge_fs =
                    local_seq_data->getCaptureEdge();
                fallback_snapshot.constrain_value_fs =
                    local_seq_data->get_constrain_value();
                fallback_snapshot.check_pair_binding =
                    *local_seq_check_pair_binding_for_export;
                local_seq_snapshot_fallback = fallback_snapshot;
                preserved_seq_snapshot = &*local_seq_snapshot_fallback;
                preserved_seq_match_status = "local_binding";
              }
              const double data_arrival_delta_ns =
                  FS_TO_NS(local_data_candidate.terms.delay_fs);
              const double local_clock_arrival_delta_ns =
                  FS_TO_NS(local_clock_candidate.terms.delay_fs);
              const auto full_sta_clock_arrive_fs =
                  clk_point->getClockArriveTime(capture_analysis_mode,
                                                clock_trans_type);
              const auto full_sta_clock_port_arrive_fs =
                  clock_port_vertex->getClockArriveTime(capture_analysis_mode,
                                                        clock_trans_type);
              const auto preserved_clock_arrival_delta_ns =
                  preserved_seq_snapshot
                      ? std::optional<double>(FS_TO_NS(
                            preserved_seq_snapshot->capture_clock_end_arrive_fs -
                            preserved_seq_snapshot
                                ->capture_clock_start_arrive_fs))
                      : std::nullopt;
              const double preserved_check_margin_ns =
                  preserved_seq_snapshot
                      ? FS_TO_NS(preserved_seq_snapshot->constrain_value_fs)
                      : 0.0;
              const auto* preserved_seq_check_pair_binding =
                  preserved_seq_snapshot &&
                          preserved_seq_snapshot->check_pair_binding
                      ? &*preserved_seq_snapshot->check_pair_binding
                      : nullptr;
              const bool preserved_seq_binding_exact =
                  checkPairBindingIsExact(
                      preserved_seq_check_pair_binding,
                      _characterization_epoch, endpoint_check_arc,
                      constraint_mode, clock_trans_type,
                      local_data_candidate.terms.end_trans_type);
              const auto* exported_check_pair_binding =
                  preserved_seq_binding_exact
                      ? preserved_seq_check_pair_binding
                      : (local_seq_binding_exact &&
                                 local_seq_check_pair_binding_for_export
                             ? local_seq_check_pair_binding_for_export
                             : (preserved_seq_check_pair_binding
                                    ? preserved_seq_check_pair_binding
                                    : local_seq_check_pair_binding_for_export));
              const auto preserved_exported_check_pair_binding_status =
                  checkPairBindingStatus(
                      exported_check_pair_binding, _characterization_epoch,
                      endpoint_check_arc, constraint_mode,
                      clock_trans_type,
                      local_data_candidate.terms.end_trans_type);
              const char* exported_check_pair_binding_status =
                  exported_check_pair_binding ==
                          preserved_seq_check_pair_binding
                      ? preserved_exported_check_pair_binding_status
                      : local_seq_binding_status;
              const bool exported_binding_exact =
                  exported_check_pair_binding ==
                          preserved_seq_check_pair_binding
                      ? checkPairBindingIsExact(
                            exported_check_pair_binding, _characterization_epoch,
                            endpoint_check_arc, constraint_mode,
                            clock_trans_type,
                            local_data_candidate.terms.end_trans_type)
                      : local_seq_binding_exact;
              const auto exported_binding_check_margin_ns =
                  bindingCheckMarginNs(exported_check_pair_binding);
              const auto exported_binding_clock_slew_ns =
                  bindingSlewNs(exported_check_pair_binding, true);
              const auto exported_binding_data_slew_ns =
                  bindingSlewNs(exported_check_pair_binding, false);
              const bool preserved_seq_is_local_binding =
                  std::string_view(preserved_seq_match_status) ==
                  "local_binding";
              const bool preserved_seq_is_relaxed =
                  std::string_view(preserved_seq_match_status) == "relaxed";
              double clock_arrival_delta_ns = 0.0;
              const char* clock_arrival_source = "missing";
              if (preserved_clock_arrival_delta_ns) {
                clock_arrival_delta_ns = *preserved_clock_arrival_delta_ns;
                clock_arrival_source = "preserved_seq_clock_path";
              } else if (local_seq_target_clock_delay_ns) {
                clock_arrival_delta_ns = *local_seq_target_clock_delay_ns;
                clock_arrival_source = "local_seq_capture_clock_path";
              } else if (full_sta_clock_arrive_fs &&
                         full_sta_clock_port_arrive_fs) {
                clock_arrival_delta_ns =
                    FS_TO_NS(*full_sta_clock_arrive_fs -
                             *full_sta_clock_port_arrive_fs);
                clock_arrival_source = "full_sta_clock_arrival_subtraction";
              }
              check_margin_ns = 0.0;
              check_margin_with_local_clock_slew_ns = 0.0;
              const char* check_margin_source = "missing";
              if (constraint_mode == AnalysisMode::kMin &&
                  preserved_seq_is_relaxed && !exported_binding_exact &&
                  local_seq_check_margin_ns) {
                check_margin_ns = *local_seq_check_margin_ns;
                check_margin_with_local_clock_slew_ns = check_margin_ns;
                check_margin_source =
                    "local_seq_margin_after_relaxed_preserved";
              } else if (preserved_seq_snapshot &&
                         !preserved_seq_is_local_binding) {
                check_margin_ns = preserved_check_margin_ns;
                check_margin_with_local_clock_slew_ns = preserved_check_margin_ns;
                check_margin_source = "preserved_seq_snapshot";
              } else if (reference_check_arc) {
                check_margin_ns = lookup_check_margin_ns(
                    reference_check_arc, input_trans_type, check_clock_slew_ns,
                    check_data_slew_ns);
                check_margin_with_local_clock_slew_ns = lookup_check_margin_ns(
                    reference_check_arc, input_trans_type,
                    clock_slew_ns.value_or(0.0), check_data_slew_ns);
                check_margin_source = preserved_seq_is_local_binding
                                          ? "reference_check_arc_after_local_binding"
                                          : "reference_check_arc";
              } else if (exported_binding_exact && exported_binding_check_margin_ns) {
                check_margin_ns = *exported_binding_check_margin_ns;
                check_margin_with_local_clock_slew_ns = check_margin_ns;
                check_margin_source = "binding_sampled_margin";
              } else if (full_sta_check_margin_ns != 0.0) {
                check_margin_ns = full_sta_check_margin_ns;
                check_margin_with_local_clock_slew_ns = full_sta_check_margin_ns;
                check_margin_source = "endpoint_check_arc_delay";
              }
              auto data_rebuilt_total_ns = rebuildPathDelayNsForEpoch(
                  local_data_candidate.delay_data, _characterization_epoch);
              auto* data_launch_clock_data =
                  local_data_candidate.delay_data
                      ? local_data_candidate.delay_data->get_launch_clock_data()
                      : nullptr;
              if (trace_port) {
                const auto data_path_req_ns =
                    (local_data_candidate.delay_data &&
                     local_data_candidate.delay_data->get_req_time())
                        ? FS_TO_NS(
                              *local_data_candidate.delay_data->get_req_time())
                        : -1.0;
                const auto endpoint_req_ns =
                    logic_endpoint->getReqTime(
                        constraint_mode,
                        local_data_candidate.terms.end_trans_type)
                        ? FS_TO_NS(*logic_endpoint->getReqTime(
                              constraint_mode,
                              local_data_candidate.terms.end_trans_type))
                        : -1.0;
                const auto port_req_ns =
                    port_vertex->getReqTime(constraint_mode, input_trans_type)
                        ? FS_TO_NS(
                              *port_vertex->getReqTime(constraint_mode,
                                                       input_trans_type))
                        : -1.0;
                const auto clk_point_req_ns =
                    clk_point->getReqTime(capture_analysis_mode,
                                          clock_trans_type)
                        ? FS_TO_NS(*clk_point->getReqTime(
                              capture_analysis_mode, clock_trans_type))
                        : -1.0;
                const auto endpoint_arrive_ns =
                    logic_endpoint->getArriveTime(
                        constraint_mode,
                        local_data_candidate.terms.end_trans_type)
                        ? FS_TO_NS(*logic_endpoint->getArriveTime(
                              constraint_mode,
                              local_data_candidate.terms.end_trans_type))
                        : -1.0;
                const auto port_arrive_ns =
                    port_vertex->getArriveTime(constraint_mode, input_trans_type)
                        ? FS_TO_NS(
                              *port_vertex->getArriveTime(constraint_mode,
                                                          input_trans_type))
                        : -1.0;
                const auto clk_point_arrive_ns =
                    clk_point->getArriveTime(capture_analysis_mode,
                                             clock_trans_type)
                        ? FS_TO_NS(*clk_point->getArriveTime(
                              capture_analysis_mode, clock_trans_type))
                        : -1.0;
                LOG_INFO << "[character_timing][check-local] port="
                         << port_vertex->getName() << " endpoint="
                         << logic_endpoint->getName() << " mode="
                         << analysisModeName(constraint_mode)
                         << " input_trans=" << transTypeName(input_trans_type)
                         << " timing_type=" << timing_type
                         << " data_end_trans="
                         << transTypeName(
                                local_data_candidate.terms.end_trans_type)
                         << " data_arrival_delta_ns="
                         << data_arrival_delta_ns
                         << " data_cell_delay_ns="
                         << (data_breakdown ? data_breakdown->cell_ns : -1.0)
                         << " data_net_delay_ns="
                         << (data_breakdown ? data_breakdown->net_ns : -1.0)
                         << " data_stored_arrival_ns="
                         << (local_data_candidate.delay_data
                                 ? FS_TO_NS(local_data_candidate.delay_data
                                                ->get_arrive_time())
                                 : -1.0)
                         << " data_path_req_ns=" << data_path_req_ns
                         << " data_rebuilt_total_ns="
                         << (data_rebuilt_total_ns ? *data_rebuilt_total_ns
                                                   : -1.0)
                         << " endpoint_arrive_ns=" << endpoint_arrive_ns
                         << " endpoint_req_ns=" << endpoint_req_ns
                         << " port_arrive_ns=" << port_arrive_ns
                         << " port_req_ns=" << port_req_ns
                         << " data_launch_clk_arrive_ns="
                         << (data_launch_clock_data
                                 ? FS_TO_NS(
                                       data_launch_clock_data->get_arrive_time())
                                 : -1.0)
                         << " local_clock_arrival_delta_ns="
                         << local_clock_arrival_delta_ns
                         << " local_seq_target_clock_delay_ns="
                         << (local_seq_target_clock_delay_ns
                                 ? *local_seq_target_clock_delay_ns
                                 : -1.0)
                         << " clock_arrival_delta_ns="
                         << clock_arrival_delta_ns
                         << " clock_stored_arrival_ns="
                         << (local_clock_candidate.delay_data
                                 ? FS_TO_NS(local_clock_candidate.delay_data
                                                ->get_arrive_time())
                                 : -1.0)
                         << " clock_rebuilt_total_ns="
                         << (clock_rebuilt_total_ns ? *clock_rebuilt_total_ns
                                                    : -1.0)
                         << " full_sta_clock_port_arrive_ns="
                         << ([&]() -> double {
                              return full_sta_clock_port_arrive_fs
                                         ? FS_TO_NS(
                                               *full_sta_clock_port_arrive_fs)
                                         : -1.0;
                            })()
                         << " clock_launch_clk_arrive_ns="
                         << (clock_launch_clock_data
                                 ? FS_TO_NS(clock_launch_clock_data
                                                ->get_arrive_time())
                                 : -1.0)
                         << " clock_net_delay_ns="
                         << (clock_breakdown ? clock_breakdown->net_ns : 0.0)
                         << " full_sta_clock_arrive_ns="
                         << (full_sta_clock_arrive_fs
                                 ? FS_TO_NS(*full_sta_clock_arrive_fs)
                                 : -1.0)
                         << " preserved_clock_arrival_delta_ns="
                         << (preserved_clock_arrival_delta_ns
                                 ? *preserved_clock_arrival_delta_ns
                                 : -1.0)
                         << " clk_point_arrive_ns=" << clk_point_arrive_ns
                         << " clk_point_req_ns=" << clk_point_req_ns
                         << " clock_start_trans="
                         << transTypeName(
                                local_clock_candidate.terms.start_trans_type)
                         << " local_seq_match_status="
                         << local_seq_match_status
                         << " local_seq_match_count="
                         << local_seq_match.match_count
                         << " local_seq_launch_clk_arrive_ns="
                         << (local_seq_data && local_seq_data->get_launch_clock_data()
                                 ? FS_TO_NS(local_seq_data->get_launch_clock_data()
                                                ->get_arrive_time())
                                 : -1.0)
                         << " local_seq_capture_clk_arrive_ns="
                         << (local_seq_data && local_seq_data->get_capture_clock_data()
                                 ? FS_TO_NS(local_seq_data->get_capture_clock_data()
                                                ->get_arrive_time())
                                 : -1.0)
                         << " local_seq_launch_edge_ns="
                         << (local_seq_data ? FS_TO_NS(local_seq_data->getLaunchEdge())
                                            : -1.0)
                         << " local_seq_capture_edge_ns="
                         << (local_seq_data ? FS_TO_NS(local_seq_data->getCaptureEdge())
                                            : -1.0)
                         << " clock_arrival_source="
                         << clock_arrival_source
                         << " check_clock_slew_ns="
                         << check_clock_slew_ns
                         << " local_clock_slew_ns="
                         << clock_slew_ns.value_or(0.0)
                         << " check_data_slew_ns="
                         << check_data_slew_ns
                         << " local_data_slew_ns="
                         << (data_slew_ns ? *data_slew_ns : 0.0)
                         << " preserved_seq_match_status="
                         << preserved_seq_match_status
                         << " preserved_seq_match_count="
                         << preserved_seq_match.match_count
                         << " exported_check_pair_binding_status="
                         << exported_check_pair_binding_status
                         << " exported_binding_clock_slew_ns="
                         << (exported_binding_clock_slew_ns
                                 ? *exported_binding_clock_slew_ns
                                 : -1.0)
                         << " exported_binding_data_slew_ns="
                         << (exported_binding_data_slew_ns
                                 ? *exported_binding_data_slew_ns
                                 : -1.0)
                         << " exported_binding_check_margin_ns="
                         << (exported_binding_check_margin_ns
                                 ? *exported_binding_check_margin_ns
                                 : -1.0)
                         << " preserved_check_margin_ns="
                         << preserved_check_margin_ns
                         << " check_margin_ns=" << check_margin_ns
                         << " check_margin_source=" << check_margin_source
                         << " check_margin_with_local_clock_slew_ns="
                         << check_margin_with_local_clock_slew_ns
                         << " preserved_data_start_vertex="
                         << (preserved_seq_snapshot &&
                                     preserved_seq_snapshot->data_start_vertex
                                 ? preserved_seq_snapshot->data_start_vertex
                                       ->getName()
                                 : "null")
                         << " preserved_clock_start_vertex="
                         << (preserved_seq_snapshot &&
                                     preserved_seq_snapshot
                                         ->capture_clock_start_vertex
                                 ? preserved_seq_snapshot
                                       ->capture_clock_start_vertex->getName()
                                 : "null");
              }

              accumulate_constraint(
                  data_arrival_delta_ns,
                  clock_arrival_delta_ns,
                  check_margin_ns, 0.0, "local_arrival_subtraction",
                  check_margin_source, local_seq_match_status,
                  local_seq_binding_status, preserved_seq_match_status,
                  exported_check_pair_binding_status);
            }
          }
        }
      }
    };

    for (auto* logic_endpoint : logic_endpoints) {
      collect_check_arc_candidate(logic_endpoint, AnalysisMode::kMax,
                                  "setup_rising", "setup_falling");
      collect_check_arc_candidate(logic_endpoint, AnalysisMode::kMin,
                                  "hold_rising", "hold_falling");
    }

    for (const auto& [candidate_key, candidate] : aggregated_candidates) {
      if (trace_port) {
        LOG_INFO << "[character_timing][check-aggregate] port="
                 << port_vertex->getName() << " related_pin="
                 << candidate_key.first << " timing_type="
                 << candidate_key.second << " has_rise="
                 << candidate.has_rise_constraint << " rise="
                 << candidate.rise_constraint << " has_fall="
                 << candidate.has_fall_constraint << " fall="
                 << candidate.fall_constraint;
      }
      if (!candidate.is_valid) {
        continue;
      }
      auto lib_arc = std::make_unique<LibArc>();
      lib_arc->set_snk_port(port_vertex->getName().c_str());
      lib_arc->set_src_port(candidate.related_pin.c_str());
      lib_arc->set_timing_type(candidate.timing_type.c_str());

      auto check_model = std::make_unique<LibCheckTableModel>();
      if (candidate.has_rise_constraint) {
        auto lib_rise_table = std::make_unique<LibTable>(
            LibTable::TableType::kRiseConstrain, nullptr);
        lib_rise_table->addTableValue(
            std::make_unique<LibFloatValue>(candidate.rise_constraint));
        check_model->addTable(std::move(lib_rise_table));
      }
      if (candidate.has_fall_constraint) {
        auto lib_fall_table = std::make_unique<LibTable>(
            LibTable::TableType::kFallConstrain, nullptr);
        lib_fall_table->addTableValue(
            std::make_unique<LibFloatValue>(candidate.fall_constraint));
        check_model->addTable(std::move(lib_fall_table));
      }
      if (!candidate.has_rise_constraint && !candidate.has_fall_constraint) {
        continue;
      }

      lib_arc->set_table_model(std::move(check_model));
      lib_arc->set_owner_cell(design_timing_cell.get());
      design_timing_cell->addLibertyArc(std::move(lib_arc));
    }
  };

  // construct the data input port to output port delay arc.
  auto construct_port_delay_arc = [&design_timing_cell, this, &build_scalar_table,
                                   &clone_axis, &extract_load_axis,
                                   &get_or_create_load_template](
                                      auto* port_vertex,
                                      AnalysisMode analysis_mode) {
    auto resolve_clock_related_pin_name =
        [](StaClockData* launch_clock_data) -> std::string {
      if (!launch_clock_data) {
        return {};
      }

      auto* launch_clock = launch_clock_data->get_prop_clock();
      if (!launch_clock) {
        return {};
      }

      auto* ista = Sta::getOrCreateSta();
      auto* constrain = ista ? ista->getConstrain() : nullptr;
      auto* sdc_clock = constrain
                            ? constrain->findClock(launch_clock->get_clock_name())
                            : nullptr;
      if (sdc_clock) {
        auto& clock_objs = sdc_clock->get_objs();
        auto port_iter = std::find_if(clock_objs.begin(), clock_objs.end(),
                                      [](DesignObject* obj) {
                                        return obj && obj->isPort();
                                      });
        if (port_iter != clock_objs.end()) {
          return (*port_iter)->get_name();
        }

        auto obj_iter = std::find_if(clock_objs.begin(), clock_objs.end(),
                                     [](DesignObject* obj) {
                                       return obj != nullptr;
                                     });
        if (obj_iter != clock_objs.end()) {
          return (*obj_iter)->get_name();
        }
      }

      return launch_clock->get_clock_name();
    };

    auto build_table_from_reference =
        [this, &build_scalar_table, &clone_axis, &extract_load_axis,
         &get_or_create_load_template, analysis_mode](
            StaPathDelayData* delay_data, StaSlewData* slew_data,
            double path_delay_ns, LibTable::TableType delay_table_type,
            LibTable::TableType slew_table_type,
            TransType block_offset_trans_type,
            TransType sampled_output_trans_type)
        -> std::pair<std::unique_ptr<LibTable>, std::unique_ptr<LibTable>> {
      double output_slew_ns =
          slew_data ? FS_TO_NS(slew_data->get_slew()) : 0.0;
      auto scalar_delay_table =
          build_scalar_table(delay_table_type, path_delay_ns);
      auto scalar_slew_table =
          build_scalar_table(slew_table_type, output_slew_ns);
      if (!delay_data) {
        LOG_INFO_FIRST_N(5)
            << "liberty alignment fallback: missing delay data.";
        return {std::move(scalar_delay_table), std::move(scalar_slew_table)};
      }

      StaInstArc* final_inst_arc = nullptr;
      auto* driver_path_data =
          dynamic_cast<StaPathDelayData*>(delay_data->get_bwd());
      auto* driver_input_path_data = driver_path_data
                                         ? dynamic_cast<StaPathDelayData*>(
                                               driver_path_data->get_bwd())
                                         : nullptr;
      auto* driver_vertex =
          driver_path_data ? driver_path_data->get_own_vertex() : nullptr;
      auto* driver_input_vertex = driver_input_path_data
                                      ? driver_input_path_data->get_own_vertex()
                                      : nullptr;

      if (driver_vertex) {
        FOREACH_SNK_ARC(driver_vertex, snk_arc) {
          auto* inst_arc = dynamic_cast<StaInstArc*>(snk_arc);
          if (!inst_arc || !inst_arc->isDelayArc()) {
            continue;
          }

          if (!driver_input_vertex || snk_arc->get_src() == driver_input_vertex) {
            final_inst_arc = inst_arc;
            break;
          }
        }
      }

      if (!final_inst_arc) {
        LOG_INFO_FIRST_N(10) << "liberty alignment fallback: final inst arc not found for sink "
                             << delay_data->get_own_vertex()->getName();
        return {std::move(scalar_delay_table), std::move(scalar_slew_table)};
      }

      auto* reference_arc = final_inst_arc->get_lib_arc();
      auto* reference_delay_model =
          dynamic_cast<LibDelayTableModel*>(reference_arc->get_table_model());
      if (!reference_delay_model) {
        LOG_INFO_FIRST_N(10) << "liberty alignment fallback: reference delay model missing on arc "
                             << final_inst_arc->get_src()->getName() << " -> "
                             << final_inst_arc->get_snk()->getName();
        return {std::move(scalar_delay_table), std::move(scalar_slew_table)};
      }

      auto* reference_delay_table =
          reference_delay_model->getTable(static_cast<int>(delay_table_type));
      auto* reference_slew_table =
          reference_delay_model->getTable(static_cast<int>(slew_table_type));
      auto load_axis_info = extract_load_axis(reference_delay_table);
      if (!load_axis_info) {
        load_axis_info = extract_load_axis(reference_slew_table);
      }
      if (!load_axis_info) {
        LOG_INFO_FIRST_N(10) << "liberty alignment fallback: load axis missing on arc "
                             << final_inst_arc->get_src()->getName() << " -> "
                             << final_inst_arc->get_snk()->getName();
        return {std::move(scalar_delay_table), std::move(scalar_slew_table)};
      }

      auto pick_available_slew =
          [analysis_mode](StaVertex* vertex, TransType preferred_trans_type,
                          uint64_t data_epoch) -> std::optional<double> {
        if (!vertex) {
          return std::nullopt;
        }

        if (auto preferred_slew = findVertexSlewNsForEpoch(
                vertex, analysis_mode, preferred_trans_type, data_epoch)) {
          return preferred_slew;
        }

        if (auto opposite_slew = findVertexSlewNsForEpoch(
                vertex, analysis_mode, FLIP_TRANS(preferred_trans_type),
                data_epoch)) {
          return opposite_slew;
        }

        auto rise_slew = findVertexSlewNsForEpoch(vertex, analysis_mode,
                                                  TransType::kRise, data_epoch);
        auto fall_slew = findVertexSlewNsForEpoch(vertex, analysis_mode,
                                                  TransType::kFall, data_epoch);
        if (rise_slew && fall_slew) {
          if (analysis_mode == AnalysisMode::kMax) {
            return (*rise_slew > *fall_slew) ? rise_slew : fall_slew;
          }
          return (*rise_slew < *fall_slew) ? rise_slew : fall_slew;
        }

        return rise_slew ? rise_slew : fall_slew;
      };

      auto pick_preserved_clock_pin_slew =
          [this, analysis_mode](StaVertex* vertex,
                                TransType preferred_trans_type)
          -> std::optional<double> {
        if (!vertex) {
          return std::nullopt;
        }

        auto lookup = [this, vertex](TransType trans_type)
            -> std::optional<double> {
          auto snapshot_iter =
              _preserved_clock_pin_slew_ns.find({vertex, trans_type});
          return snapshot_iter == _preserved_clock_pin_slew_ns.end()
                     ? std::nullopt
                     : std::optional<double>(snapshot_iter->second);
        };

        if (auto preferred_slew = lookup(preferred_trans_type)) {
          return preferred_slew;
        }

        if (auto opposite_slew = lookup(FLIP_TRANS(preferred_trans_type))) {
          return opposite_slew;
        }

        auto rise_slew = lookup(TransType::kRise);
        auto fall_slew = lookup(TransType::kFall);
        if (rise_slew && fall_slew) {
          if (analysis_mode == AnalysisMode::kMax) {
            return (*rise_slew > *fall_slew) ? rise_slew : fall_slew;
          }
          return (*rise_slew < *fall_slew) ? rise_slew : fall_slew;
        }

        return rise_slew ? rise_slew : fall_slew;
      };

      auto pick_sampling_clock_slew =
          [this, &pick_preserved_clock_pin_slew](StaVertex* vertex,
                                                 TransType preferred_trans_type)
          -> std::optional<double> {
        if (!vertex || !vertex->is_clock()) {
          return std::nullopt;
        }

        auto direct_slew =
            pick_preserved_clock_pin_slew(vertex, preferred_trans_type);
        if (direct_slew && *direct_slew > 0.0) {
          return direct_slew;
        }

        auto mapped_ports = _logic_clkpoint_to_port.values(vertex);
        for (auto* port_vertex : mapped_ports) {
          auto port_slew =
              pick_preserved_clock_pin_slew(port_vertex, preferred_trans_type);
          if (port_slew && *port_slew > 0.0) {
            LOG_INFO_FIRST_N(10)
                << "liberty alignment fallback: reuse source clock port slew "
                << port_vertex->getName() << " -> " << vertex->getName()
                << " slew_ns=" << *port_slew;
            return port_slew;
          }
        }

        return direct_slew;
      };

      auto resolve_input_trans_type =
          [driver_input_path_data, sampled_output_trans_type](StaInstArc* inst_arc) {
            auto output_trans_type = sampled_output_trans_type;
            if (!inst_arc) {
              return driver_input_path_data
                         ? driver_input_path_data->get_trans_type()
                         : output_trans_type;
            }

            // Match OpenROAD's use of the reference arc's source edge when
            // picking the input slew that drives table sampling.
            if (inst_arc->isRisingTriggerArc()) {
              return TransType::kRise;
            }
            if (inst_arc->isFallingTriggerArc()) {
              return TransType::kFall;
            }
            if (inst_arc->isPositiveArc()) {
              return output_trans_type;
            }
            if (inst_arc->isNegativeArc()) {
              return FLIP_TRANS(output_trans_type);
            }
            return driver_input_path_data
                       ? driver_input_path_data->get_trans_type()
                       : output_trans_type;
          };

      auto input_trans_type = resolve_input_trans_type(final_inst_arc);
      auto* reference_input_vertex = final_inst_arc->get_src();
      auto working_slew =
          (delay_data->get_launch_clock_data() && reference_input_vertex &&
           reference_input_vertex->is_clock())
              ? pick_sampling_clock_slew(reference_input_vertex,
                                         input_trans_type)
              : std::nullopt;
      if (!working_slew) {
        working_slew = pick_available_slew(reference_input_vertex,
                                           input_trans_type,
                                           delay_data->get_data_epoch());
      }
      if (!working_slew) {
        LOG_INFO_FIRST_N(10) << "liberty alignment fallback: working slew missing on arc "
                             << final_inst_arc->get_src()->getName() << " -> "
                             << final_inst_arc->get_snk()->getName();
        return {std::move(scalar_delay_table), std::move(scalar_slew_table)};
      }

      double actual_load = convertPfToReferenceCapUnit(
          reference_arc, final_inst_arc->get_snk()->getLoad(
                             analysis_mode, block_offset_trans_type));
      double reference_working_delay =
          reference_arc->getDelayOrConstrainCheckNs(block_offset_trans_type,
                                                    *working_slew, actual_load);
      double block_offset_ns = path_delay_ns - reference_working_delay;
      const bool trace_sink =
          delay_data && delay_data->get_own_vertex() &&
          shouldTraceCharacterizationPin(
              delay_data->get_own_vertex()->getName().c_str());
      if (trace_sink) {
        LOG_INFO << "[character_timing][table-build] sink="
                 << delay_data->get_own_vertex()->getName()
                 << " ref_arc=" << final_inst_arc->get_src()->getName() << "->"
                 << final_inst_arc->get_snk()->getName()
                 << " block_offset_trans="
                 << (block_offset_trans_type == TransType::kRise ? "rise"
                                                                 : "fall")
                 << " sampled_output_trans="
                 << (sampled_output_trans_type == TransType::kRise ? "rise"
                                                                   : "fall")
                 << " input_trans="
                 << (input_trans_type == TransType::kRise ? "rise" : "fall")
                 << " working_slew_ns=" << *working_slew
                 << " actual_load=" << actual_load
                 << " path_delay_ns=" << path_delay_ns
                 << " reference_working_delay_ns=" << reference_working_delay
                 << " block_offset_ns=" << block_offset_ns;
      }

      auto* load_template = get_or_create_load_template(*load_axis_info);
      auto delay_table =
          std::make_unique<LibTable>(delay_table_type, load_template);
      delay_table->addAxis(clone_axis(load_axis_info->values));
      auto slew_table = std::make_unique<LibTable>(slew_table_type, load_template);
      slew_table->addAxis(clone_axis(load_axis_info->values));

      for (double sampled_load : load_axis_info->values) {
        double sampled_delay = reference_arc->getDelayOrConstrainCheckNs(
            sampled_output_trans_type, *working_slew, sampled_load);
        double sampled_slew =
            reference_arc->getSlewNs(sampled_output_trans_type, *working_slew,
                                     sampled_load);

        delay_table->addTableValue(
            std::make_unique<LibFloatValue>(block_offset_ns + sampled_delay));
        slew_table->addTableValue(
            std::make_unique<LibFloatValue>(sampled_slew));
      }

      return {std::move(delay_table), std::move(slew_table)};
    };

    auto* ista = Sta::getOrCreateSta();
    auto fall_start_vertex_to_delay_data = getDifferentStartPathDelayDataForEpoch(
        port_vertex, analysis_mode, TransType::kFall, _characterization_epoch);
    auto rise_start_vertex_to_delay_data = getDifferentStartPathDelayDataForEpoch(
        port_vertex, analysis_mode, TransType::kRise, _characterization_epoch);

    struct ClockedOutputArcCandidate {
      std::string related_pin;
      std::string timing_type;
      StaPathDelayData* rise_delay_data = nullptr;
      StaPathDelayData* fall_delay_data = nullptr;
      StaVertex* rise_start_vertex = nullptr;
      StaVertex* fall_start_vertex = nullptr;
      double rise_delay_ns = 0.0;
      double fall_delay_ns = 0.0;
      bool has_rise_delay = false;
      bool has_fall_delay = false;
    };

    struct ClockedOutputTableRequest {
      StaPathDelayData* source_delay_data = nullptr;
      StaVertex* source_start_vertex = nullptr;
      double boundary_delay_ns = 0.0;
      TransType block_offset_trans_type = TransType::kRise;
      TransType sampled_output_trans_type = TransType::kRise;
    };

    std::map<std::pair<std::string, std::string>, ClockedOutputArcCandidate>
        clocked_output_candidates;
    const bool trace_port =
        shouldTraceCharacterizationPin(port_vertex->getName().c_str());
    auto choose_worse_delay = [analysis_mode](double lhs, double rhs) {
      return analysis_mode == AnalysisMode::kMax ? (lhs > rhs) : (lhs < rhs);
    };
    if (ista) {
      auto collect_clocked_output_candidates =
          [&](const auto& start_vertex_to_delay_data,
              TransType output_trans_type) {
            for (const auto& [path_start_vertex, delay_data] :
                 start_vertex_to_delay_data) {
              if (!path_start_vertex || path_start_vertex->is_port() ||
                  !delay_data) {
                continue;
              }

              auto* launch_clock_data = delay_data->get_launch_clock_data();
              if (!launch_clock_data) {
                continue;
              }

              auto related_pin = resolve_clock_related_pin_name(launch_clock_data);
              if (related_pin.empty()) {
                continue;
              }

              auto timing_type =
                  launch_clock_data->get_clock_wave_type() == TransType::kRise
                      ? std::string("rising_edge")
                      : std::string("falling_edge");
              auto candidate_key = std::make_pair(related_pin, timing_type);
              auto& candidate = clocked_output_candidates[candidate_key];
              if (candidate.related_pin.empty()) {
                candidate.related_pin = candidate_key.first;
                candidate.timing_type = candidate_key.second;
              }

              // Match OpenROAD's clocked-output model collection: use the
              // output path arrival relative to the driving clock edge, which
              // is equivalent to data path delay plus launch clock network
              // latency.
              double stored_delay_ns =
                  FS_TO_NS(delay_data->get_arrive_time()) +
                  FS_TO_NS(launch_clock_data->get_arrive_time());
              double raw_delay_ns = rebuildPathDelayNsForEpoch(
                                        delay_data, _characterization_epoch)
                                        .value_or(stored_delay_ns);
              if (trace_port) {
                auto* launch_clock = launch_clock_data->get_prop_clock();
                auto path_nodes = delay_data->getPathData();
                auto* launch_delay_data = delay_data->get_launch_delay_data();
                std::vector<std::string> path_trace;
                while (!path_nodes.empty()) {
                  auto* node = dynamic_cast<StaPathDelayData*>(path_nodes.top());
                  path_nodes.pop();
                  if (!node) {
                    continue;
                  }
                  path_trace.emplace_back(
                      node->get_own_vertex()->getName() + "@" +
                      std::to_string(FS_TO_NS(node->get_arrive_time())));
                }
                LOG_INFO << "[character_timing][clk2out-candidate] port="
                         << port_vertex->getName() << " mode="
                         << (analysis_mode == AnalysisMode::kMax ? "max"
                                                                 : "min")
                         << " related_pin=" << related_pin << " output_trans="
                         << (output_trans_type == TransType::kRise ? "rise"
                                                                   : "fall")
                         << " start_vertex="
                         << (path_start_vertex ? path_start_vertex->getName()
                                               : "null")
                         << " launch_seed_vertex="
                         << (launch_delay_data && launch_delay_data->get_own_vertex()
                                 ? launch_delay_data->get_own_vertex()->getName()
                                 : "null")
                         << " launch_seed_arrive_ns="
                         << (launch_delay_data
                                 ? FS_TO_NS(launch_delay_data->get_arrive_time())
                                 : -1.0)
                         << " path_depth=" << delay_data->getPathData().size()
                         << " stored_arrival_ns="
                         << FS_TO_NS(delay_data->get_arrive_time())
                         << " launch_clk_ns="
                         << FS_TO_NS(launch_clock_data->get_arrive_time())
                         << " rebuilt_boundary_ns=" << raw_delay_ns
                         << " stored_boundary_ns=" << stored_delay_ns
                         << " clock_period_ns="
                         << (launch_clock ? launch_clock->getPeriodNs() : -1.0)
                         << " clock_edge_ns="
                         << (launch_clock_data->get_clock_wave_type() ==
                                     TransType::kRise
                                 ? (launch_clock
                                        ? PS_TO_NS(launch_clock->getRisingEdge())
                                        : -1.0)
                                 : (launch_clock
                                        ? PS_TO_NS(launch_clock->getFallingEdge())
                                        : -1.0))
                         << " path_trace="
                         << [&path_trace]() {
                              std::string joined;
                              for (size_t i = 0; i < path_trace.size(); ++i) {
                                if (i != 0) {
                                  joined.append(" -> ");
                                }
                                joined.append(path_trace[i]);
                              }
                              return joined;
                            }();
              }

              if (output_trans_type == TransType::kRise &&
                  (!candidate.has_rise_delay ||
                   choose_worse_delay(raw_delay_ns, candidate.rise_delay_ns))) {
                candidate.rise_delay_ns = raw_delay_ns;
                candidate.rise_delay_data = delay_data;
                candidate.rise_start_vertex = path_start_vertex;
                candidate.has_rise_delay = true;
              } else if (output_trans_type == TransType::kFall &&
                         (!candidate.has_fall_delay || choose_worse_delay(
                                                          raw_delay_ns,
                                                          candidate.fall_delay_ns))) {
                candidate.fall_delay_ns = raw_delay_ns;
                candidate.fall_delay_data = delay_data;
                candidate.fall_start_vertex = path_start_vertex;
                candidate.has_fall_delay = true;
              }
            }
          };

      collect_clocked_output_candidates(rise_start_vertex_to_delay_data,
                                        TransType::kRise);
      collect_clocked_output_candidates(fall_start_vertex_to_delay_data,
                                        TransType::kFall);
    }

    if (!clocked_output_candidates.empty()) {
      auto choose_table_request = [&](const ClockedOutputArcCandidate& candidate,
                                      TransType table_trans)
          -> std::optional<ClockedOutputTableRequest> {
        if (table_trans == TransType::kRise) {
          if (candidate.has_rise_delay) {
            return ClockedOutputTableRequest{
                .source_delay_data = candidate.rise_delay_data,
                .source_start_vertex = candidate.rise_start_vertex,
                .boundary_delay_ns = candidate.rise_delay_ns,
                .block_offset_trans_type = TransType::kRise,
                .sampled_output_trans_type = TransType::kRise,
            };
          }
          if (candidate.has_fall_delay) {
            return ClockedOutputTableRequest{
                .source_delay_data = candidate.fall_delay_data,
                .source_start_vertex = candidate.fall_start_vertex,
                .boundary_delay_ns = candidate.fall_delay_ns,
                .block_offset_trans_type = TransType::kFall,
                .sampled_output_trans_type = TransType::kRise,
            };
          }
        } else {
          if (candidate.has_fall_delay) {
            return ClockedOutputTableRequest{
                .source_delay_data = candidate.fall_delay_data,
                .source_start_vertex = candidate.fall_start_vertex,
                .boundary_delay_ns = candidate.fall_delay_ns,
                .block_offset_trans_type = TransType::kFall,
                .sampled_output_trans_type = TransType::kFall,
            };
          }
          if (candidate.has_rise_delay) {
            return ClockedOutputTableRequest{
                .source_delay_data = candidate.rise_delay_data,
                .source_start_vertex = candidate.rise_start_vertex,
                .boundary_delay_ns = candidate.rise_delay_ns,
                .block_offset_trans_type = TransType::kRise,
                .sampled_output_trans_type = TransType::kFall,
            };
          }
        }

        return std::nullopt;
      };

      for (auto& [candidate_key, candidate] : clocked_output_candidates) {
        auto lib_arc = std::make_unique<LibArc>();
        auto delay_model = std::make_unique<LibDelayTableModel>();
        lib_arc->set_snk_port(port_vertex->getName().c_str());
        lib_arc->set_src_port(candidate.related_pin.c_str());
        lib_arc->set_timing_type(candidate.timing_type.c_str());
        lib_arc->set_timing_sense("positive_unate");

        bool has_delay_table = false;
        for (auto table_trans : {TransType::kRise, TransType::kFall}) {
          auto table_request = choose_table_request(candidate, table_trans);
          if (!table_request || !table_request->source_delay_data) {
            continue;
          }

          auto* delay_data = table_request->source_delay_data;
          auto* selected_start_vertex = table_request->source_start_vertex;
          auto* slew_data = findWorstVertexSlewDataForEpoch(
              port_vertex, analysis_mode, table_trans,
              delay_data->get_data_epoch(), selected_start_vertex);
          if (!slew_data) {
            slew_data = findWorstVertexSlewDataForEpoch(
                port_vertex, analysis_mode,
                table_request->block_offset_trans_type,
                delay_data->get_data_epoch(), selected_start_vertex);
          }
          double boundary_delay_ns = table_request->boundary_delay_ns;
          if (trace_port) {
            LOG_INFO << "[character_timing][clk2out-final] port="
                     << port_vertex->getName() << " related_pin="
                     << candidate.related_pin << " output_trans="
                     << (table_trans == TransType::kRise ? "rise" : "fall")
                     << " mode="
                     << (analysis_mode == AnalysisMode::kMax ? "max" : "min")
                     << " start_vertex="
                     << (selected_start_vertex
                             ? selected_start_vertex->getName()
                             : "null")
                     << " boundary_delay_ns=" << boundary_delay_ns
                     << " slew_ns="
                     << (slew_data ? FS_TO_NS(slew_data->get_slew()) : -1.0);
          }
          auto delay_table_type =
              table_trans == TransType::kRise
                  ? LibTable::TableType::kCellRise
                  : LibTable::TableType::kCellFall;
          auto slew_table_type =
              table_trans == TransType::kRise
                  ? LibTable::TableType::kRiseTransition
                  : LibTable::TableType::kFallTransition;
          auto [delay_table, slew_table] = build_table_from_reference(
              delay_data, slew_data, boundary_delay_ns, delay_table_type,
              slew_table_type, table_request->block_offset_trans_type,
              table_request->sampled_output_trans_type);
          delay_model->addTable(std::move(delay_table));
          delay_model->addTable(std::move(slew_table));
          has_delay_table = true;
        }

        if (!has_delay_table) {
          continue;
        }

        lib_arc->set_table_model(std::move(delay_model));
        lib_arc->set_owner_cell(design_timing_cell.get());
        design_timing_cell->addLibertyArc(std::move(lib_arc));
      }
      return;
    }

    std::vector<StaVertex*> all_start_vertexes;
    for (const auto& pair : rise_start_vertex_to_delay_data) {
      all_start_vertexes.emplace_back(pair.first);
    }
    for (const auto& pair : fall_start_vertex_to_delay_data) {
      if (std::find(all_start_vertexes.begin(), all_start_vertexes.end(),
                    pair.first) == all_start_vertexes.end()) {
        all_start_vertexes.emplace_back(pair.first);
      }
    }

    if (all_start_vertexes.empty()) {
      return;
    }

    for (auto* start_vertex : all_start_vertexes) {
      auto lib_arc = std::make_unique<LibArc>();
      auto delay_model = std::make_unique<LibDelayTableModel>();
      auto* representative_delay_data =
          rise_start_vertex_to_delay_data[start_vertex]
              ? rise_start_vertex_to_delay_data[start_vertex]
              : fall_start_vertex_to_delay_data[start_vertex];
      auto related_pin = start_vertex->getName();
      auto timing_type = std::string("combinational");
      if (representative_delay_data) {
        if (auto* launch_clock_data =
                representative_delay_data->get_launch_clock_data();
            launch_clock_data) {
          if (auto clock_related_pin =
                  resolve_clock_related_pin_name(launch_clock_data);
              !clock_related_pin.empty()) {
            related_pin = std::move(clock_related_pin);
          }
          timing_type =
              launch_clock_data->get_clock_wave_type() == TransType::kRise
                  ? "rising_edge"
                  : "falling_edge";
        }
      }

      lib_arc->set_snk_port(port_vertex->getName().c_str());
      lib_arc->set_src_port(related_pin.c_str());
      lib_arc->set_timing_type(timing_type.c_str());
      bool has_delay_table = false;
      FOREACH_TRANS(trans) {
        auto* delay_data = trans == TransType::kRise
                               ? rise_start_vertex_to_delay_data[start_vertex]
                               : fall_start_vertex_to_delay_data[start_vertex];
        if (!delay_data) {
          continue;
        }

        auto* slew_data = findWorstVertexSlewDataForEpoch(
            port_vertex, analysis_mode, trans, delay_data->get_data_epoch(),
            start_vertex);
        if (!slew_data) {
          continue;
        }

        auto path_data = delay_data->getPathData();
        const char* timing_sense =
            (trans == path_data.top()->get_trans_type())
                ? "positive_unate"
                : "negative_unate";  // TODO(to taosimin), non-unate should
                                     // consider.
        lib_arc->set_timing_sense(timing_sense);

        auto delay_table_type =
            delay_data->get_trans_type() == TransType::kRise
                ? LibTable::TableType::kCellRise
                : LibTable::TableType::kCellFall;
        auto slew_table_type = slew_data->get_trans_type() == TransType::kRise
                                   ? LibTable::TableType::kRiseTransition
                                   : LibTable::TableType::kFallTransition;
        auto [delay_table, slew_table] = build_table_from_reference(
            delay_data, slew_data, FS_TO_NS(delay_data->get_arrive_time()),
            delay_table_type, slew_table_type, delay_data->get_trans_type(),
            delay_data->get_trans_type());

        delay_model->addTable(std::move(delay_table));
        delay_model->addTable(std::move(slew_table));
        has_delay_table = true;
      }

      if (!has_delay_table) {
        continue;
      }
      lib_arc->set_table_model(std::move(delay_model));
      lib_arc->set_owner_cell(design_timing_cell.get());
      design_timing_cell->addLibertyArc(std::move(lib_arc));
    }
  };

  auto construct_clock_path_arc = [&design_timing_cell, &build_scalar_table](
                                      auto* port_vertex) {
    auto append_clock_arc = [&](const char* timing_type) {
      auto lib_arc = std::make_unique<LibArc>();
      lib_arc->set_snk_port(port_vertex->getName().c_str());
      lib_arc->set_timing_sense("positive_unate");
      lib_arc->set_timing_type(timing_type);

      auto delay_model = std::make_unique<LibDelayTableModel>();
      delay_model->addTable(
          build_scalar_table(LibTable::TableType::kCellRise, 0.0));
      delay_model->addTable(
          build_scalar_table(LibTable::TableType::kCellFall, 0.0));
      lib_arc->set_table_model(std::move(delay_model));
      lib_arc->set_owner_cell(design_timing_cell.get());
      design_timing_cell->addLibertyArc(std::move(lib_arc));
    };

    append_clock_arc("min_clock_tree_path");
    append_clock_arc("max_clock_tree_path");
  };

  StaVertex* port_vertex;
  FOREACH_PORT_VERTEX(the_graph, port_vertex) {
    auto* the_port = port_vertex->get_design_obj();

    auto lib_port = std::make_unique<LibPort>(the_port->get_name());
    lib_port->set_port_type(resolve_export_port_type(the_port));
    bool is_clock_port =
        port_vertex->is_clock() || _port_to_logic_clkpoint.contains(port_vertex);
    lib_port->set_is_clock(is_clock_port);
    double exported_port_cap_pf = 0.0;
    auto* design_port = dynamic_cast<Port*>(port_vertex->get_design_obj());
    auto* driven_net = design_port ? design_port->get_net() : nullptr;
    const bool use_connected_sink_cap_only =
        design_port && design_port->isInput() && driven_net;
    for (auto mode : {AnalysisMode::kMax, AnalysisMode::kMin}) {
      for (auto trans : {TransType::kRise, TransType::kFall}) {
        const double port_cap_pf =
            use_connected_sink_cap_only ? driven_net->getLoad(mode, trans)
                                        : port_vertex->getLoad(mode, trans);
        exported_port_cap_pf = std::max(exported_port_cap_pf, port_cap_pf);
      }
    }
    lib_port->set_port_cap(exported_port_cap_pf);
    design_timing_cell->addLibertyPort(std::move(lib_port));

    if (is_clock_port) {
      construct_clock_path_arc(port_vertex);
    }

    if (the_port->isInput() && !_port_to_logic_clkpoint.contains(port_vertex)) {
      // construct the constrain arc, need know the constrained arc, and the
      // constrain port.
      construct_port_check_arc(port_vertex, _analysis_mode);

    } else if (the_port->isOutput()) {
      // construct the delay arc.
      construct_port_delay_arc(port_vertex, _analysis_mode);
    }
  }

  _design_timing_model->addLibertyCell(std::move(design_timing_cell));

  if (model_path && *model_path) {
    std::ofstream check_source_file(std::string(model_path) + ".check_sources.tsv",
                                    std::ios::trunc);
    if (check_source_file.is_open()) {
      for (const auto& row : check_source_rows) {
        check_source_file << row << '\n';
      }
    }
  }

  LOG_INFO << "gen timing model end.";
  return 1;
}
