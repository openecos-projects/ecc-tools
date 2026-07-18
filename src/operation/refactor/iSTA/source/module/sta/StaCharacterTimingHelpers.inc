namespace {

bool isCharacterizationData(const StaData* data, uint64_t epoch) {
  return data && data->get_data_epoch() == epoch;
}

bool checkArcMatchesForTimingModel(const StaArc* lhs, const StaArc* rhs) {
  if (lhs == rhs) {
    return true;
  }
  if (!lhs || !rhs) {
    return false;
  }

  const bool lhs_setup_like = lhs->isSetupArc() || lhs->isRecoveryArc();
  const bool rhs_setup_like = rhs->isSetupArc() || rhs->isRecoveryArc();
  const bool lhs_hold_like = lhs->isHoldArc() || lhs->isRemovalArc();
  const bool rhs_hold_like = rhs->isHoldArc() || rhs->isRemovalArc();

  return lhs_setup_like == rhs_setup_like &&
         lhs_hold_like == rhs_hold_like &&
         lhs->isRisingEdgeCheck() == rhs->isRisingEdgeCheck();
}

struct BoundaryDelayTerms {
  StaVertex* start_vertex = nullptr;
  StaVertex* end_vertex = nullptr;
  TransType start_trans_type = TransType::kRise;
  TransType end_trans_type = TransType::kRise;
  int64_t start_arrive_fs = 0;
  int64_t end_arrive_fs = 0;
  int64_t delay_fs = 0;
};

struct BoundaryDelayCandidate {
  StaPathDelayData* delay_data = nullptr;
  BoundaryDelayTerms terms;
};

struct PathDelayBreakdownNs {
  double total_ns = 0.0;
  double cell_ns = 0.0;
  double net_ns = 0.0;
};

template <typename T>
std::optional<BoundaryDelayTerms> extractBoundaryDelayTerms(
    std::stack<T*> path_stack) {
  std::vector<T*> path_nodes;
  while (!path_stack.empty()) {
    auto* path_node = path_stack.top();
    path_stack.pop();
    if (!path_node || !path_node->get_own_vertex()) {
      return std::nullopt;
    }
    path_nodes.push_back(path_node);
  }

  if (path_nodes.empty()) {
    return std::nullopt;
  }

  auto* start_node = path_nodes.front();
  auto* end_node = path_nodes.back();
  BoundaryDelayTerms terms;
  terms.start_vertex = start_node->get_own_vertex();
  terms.end_vertex = end_node->get_own_vertex();
  terms.start_trans_type = start_node->get_trans_type();
  terms.end_trans_type = end_node->get_trans_type();
  terms.start_arrive_fs = start_node->get_arrive_time();
  terms.end_arrive_fs = end_node->get_arrive_time();
  terms.delay_fs = terms.end_arrive_fs - terms.start_arrive_fs;
  return terms;
}

std::optional<PathDelayBreakdownNs> rebuildPathDelayBreakdownNsForEpoch(
    StaPathDelayData* delay_data, uint64_t epoch) {
  if (!delay_data) {
    return std::nullopt;
  }

  auto path_stack = delay_data->getPathData();
  std::vector<StaPathDelayData*> path_nodes;
  while (!path_stack.empty()) {
    auto* path_node = dynamic_cast<StaPathDelayData*>(path_stack.top());
    path_stack.pop();
    if (!path_node || !path_node->get_own_vertex()) {
      return std::nullopt;
    }
    path_nodes.push_back(path_node);
  }

  if (path_nodes.empty()) {
    return std::nullopt;
  }

  PathDelayBreakdownNs breakdown;
  for (size_t i = 1; i < path_nodes.size(); ++i) {
    auto* src_vertex = path_nodes[i - 1]->get_own_vertex();
    auto* snk_node = path_nodes[i];
    auto* snk_vertex = snk_node->get_own_vertex();
    if (!src_vertex || !snk_vertex) {
      return std::nullopt;
    }

    auto arcs = snk_vertex->getSrcArc(src_vertex);
    StaArc* matched_arc = nullptr;
    for (auto* arc : arcs) {
      if (arc && arc->isDelayArc()) {
        matched_arc = arc;
        break;
      }
    }

    if (!matched_arc) {
      return std::nullopt;
    }

    const double arc_delay_ns =
        FS_TO_NS(matched_arc->get_arc_delay(snk_node->get_delay_type(),
                                            snk_node->get_trans_type(), epoch));
    breakdown.total_ns += arc_delay_ns;
    if (matched_arc->isNetArc()) {
      breakdown.net_ns += arc_delay_ns;
    } else {
      breakdown.cell_ns += arc_delay_ns;
    }
  }

  return breakdown;
}

std::vector<BoundaryDelayCandidate> collectBoundaryDelayCandidatesForEpoch(
    StaVertex* vertex, AnalysisMode analysis_mode, uint64_t epoch,
    StaVertex* start_vertex = nullptr,
    std::optional<TransType> start_trans_type = std::nullopt,
    std::optional<TransType> end_trans_type = std::nullopt) {
  std::vector<BoundaryDelayCandidate> collected_terms;
  if (!vertex) {
    return collected_terms;
  }

  StaData* data = nullptr;
  FOREACH_DELAY_DATA(vertex, data) {
    if ((data->get_delay_type() != analysis_mode) ||
        !isCharacterizationData(data, epoch)) {
      continue;
    }

    auto* path_delay_data = dynamic_cast<StaPathDelayData*>(data);
    if (!path_delay_data) {
      continue;
    }

    auto delay_terms = extractBoundaryDelayTerms(path_delay_data->getPathData());
    if (!delay_terms) {
      continue;
    }

    if (start_vertex && delay_terms->start_vertex != start_vertex) {
      continue;
    }
    if (start_trans_type &&
        delay_terms->start_trans_type != *start_trans_type) {
      continue;
    }
    if (end_trans_type && delay_terms->end_trans_type != *end_trans_type) {
      continue;
    }

    collected_terms.push_back({path_delay_data, *delay_terms});
  }

  return collected_terms;
}

std::vector<BoundaryDelayTerms> collectBoundaryDelayTermsForEpoch(
    StaVertex* vertex, AnalysisMode analysis_mode, uint64_t epoch,
    StaVertex* start_vertex = nullptr,
    std::optional<TransType> start_trans_type = std::nullopt,
    std::optional<TransType> end_trans_type = std::nullopt) {
  std::vector<BoundaryDelayTerms> collected_terms;
  for (const auto& candidate : collectBoundaryDelayCandidatesForEpoch(
           vertex, analysis_mode, epoch, start_vertex, start_trans_type,
           end_trans_type)) {
    collected_terms.push_back(candidate.terms);
  }
  return collected_terms;
}

std::map<StaVertex*, StaPathDelayData*> getDifferentStartPathDelayDataForEpoch(
    StaVertex* vertex, AnalysisMode analysis_mode, TransType trans_type,
    uint64_t epoch) {
  using PathDataQueue = std::priority_queue<StaData*, std::vector<StaData*>,
                                            decltype(sta_data_cmp)>;
  std::map<StaVertex*, PathDataQueue> start_vertex_path_data_queue;

  StaData* data = nullptr;
  FOREACH_DELAY_DATA(vertex, data) {
    if ((data->get_delay_type() != analysis_mode) ||
        (data->get_trans_type() != trans_type) ||
        !isCharacterizationData(data, epoch)) {
      continue;
    }

    auto* path_delay_data = dynamic_cast<StaPathDelayData*>(data);
    if (!path_delay_data) {
      continue;
    }

    auto path_data = path_delay_data->getPathData();
    if (path_data.empty()) {
      continue;
    }

    auto* path_start_vertex = path_data.top()->get_own_vertex();
    auto [queue_iter, inserted] = start_vertex_path_data_queue.emplace(
        path_start_vertex, PathDataQueue(sta_data_cmp));
    queue_iter->second.push(data);
  }

  std::map<StaVertex*, StaPathDelayData*> start_vertex_to_worst_data;
  for (auto& [start_vertex, data_queue] : start_vertex_path_data_queue) {
    if (!data_queue.empty()) {
      start_vertex_to_worst_data[start_vertex] =
          dynamic_cast<StaPathDelayData*>(data_queue.top());
    }
  }

  return start_vertex_to_worst_data;
}

std::optional<double> findVertexSlewNsForEpoch(StaVertex* vertex,
                                               AnalysisMode analysis_mode,
                                               TransType trans_type,
                                               uint64_t epoch) {
  if (!vertex) {
    return std::nullopt;
  }

  StaSlewData* selected = nullptr;
  StaData* data = nullptr;
  FOREACH_SLEW_DATA(vertex, data) {
    if ((data->get_delay_type() != analysis_mode) ||
        (data->get_trans_type() != trans_type) ||
        !isCharacterizationData(data, epoch)) {
      continue;
    }

    auto* slew_data = dynamic_cast<StaSlewData*>(data);
    if (!slew_data) {
      continue;
    }

    if (!selected) {
      selected = slew_data;
      continue;
    }

    if ((analysis_mode == AnalysisMode::kMax &&
         slew_data->get_slew() > selected->get_slew()) ||
        (analysis_mode == AnalysisMode::kMin &&
         slew_data->get_slew() < selected->get_slew())) {
      selected = slew_data;
    }
  }

  return selected ? std::optional<double>(FS_TO_NS(selected->get_slew()))
                  : std::nullopt;
}

std::optional<double> findVertexSlewNsForEpochWithStart(
    StaVertex* vertex, AnalysisMode analysis_mode, TransType trans_type,
    uint64_t epoch, StaVertex* start_vertex = nullptr,
    std::optional<TransType> start_trans_type = std::nullopt) {
  if (!vertex) {
    return std::nullopt;
  }

  StaSlewData* selected = nullptr;
  StaData* data = nullptr;
  FOREACH_SLEW_DATA(vertex, data) {
    if ((data->get_delay_type() != analysis_mode) ||
        (data->get_trans_type() != trans_type) ||
        !isCharacterizationData(data, epoch)) {
      continue;
    }

    auto* slew_data = dynamic_cast<StaSlewData*>(data);
    if (!slew_data) {
      continue;
    }

    if (start_vertex || start_trans_type) {
      auto path_data = slew_data->getPathData();
      if (path_data.empty()) {
        continue;
      }

      auto* path_start_data = path_data.top();
      if (!path_start_data) {
        continue;
      }

      if (start_vertex && path_start_data->get_own_vertex() != start_vertex) {
        continue;
      }
      if (start_trans_type &&
          path_start_data->get_trans_type() != *start_trans_type) {
        continue;
      }
    }

    if (!selected) {
      selected = slew_data;
      continue;
    }

    if ((analysis_mode == AnalysisMode::kMax &&
         slew_data->get_slew() > selected->get_slew()) ||
        (analysis_mode == AnalysisMode::kMin &&
         slew_data->get_slew() < selected->get_slew())) {
      selected = slew_data;
    }
  }

  return selected ? std::optional<double>(FS_TO_NS(selected->get_slew()))
                  : std::nullopt;
}

StaSlewData* findWorstVertexSlewDataForEpoch(StaVertex* vertex,
                                             AnalysisMode analysis_mode,
                                             TransType trans_type,
                                             uint64_t epoch,
                                             StaVertex* start_vertex =
                                                 nullptr) {
  if (!vertex) {
    return nullptr;
  }

  std::priority_queue<StaData*, std::vector<StaData*>, decltype(sta_data_cmp)>
      data_queue(sta_data_cmp);

  StaData* data = nullptr;
  FOREACH_SLEW_DATA(vertex, data) {
    if ((data->get_delay_type() != analysis_mode) ||
        (data->get_trans_type() != trans_type) ||
        !isCharacterizationData(data, epoch)) {
      continue;
    }

    if (start_vertex) {
      auto path_data = data->getPathData();
      if (path_data.empty() || path_data.top()->get_own_vertex() != start_vertex) {
        continue;
      }
    }

    data_queue.push(data);
  }

  if (data_queue.empty()) {
    return nullptr;
  }

  return dynamic_cast<StaSlewData*>(data_queue.top());
}

struct ExactSeqCheckSnapshotMatch {
  const PreservedSeqCheckSnapshot* snapshot = nullptr;
  size_t match_count = 0;
};

using PathVertexSignature = std::vector<std::pair<StaVertex*, TransType>>;

template <typename T>
std::optional<PathVertexSignature> extractPathVertexSignature(
    std::stack<T*> path_stack) {
  PathVertexSignature signature;
  while (!path_stack.empty()) {
    auto* path_node = path_stack.top();
    path_stack.pop();
    if (!path_node || !path_node->get_own_vertex()) {
      return std::nullopt;
    }
    signature.emplace_back(path_node->get_own_vertex(),
                           path_node->get_trans_type());
  }

  if (signature.empty()) {
    return std::nullopt;
  }

  return signature;
}

bool pathVertexSignatureMatches(const PathVertexSignature& lhs,
                                const PathVertexSignature& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (size_t i = 0; i < lhs.size(); ++i) {
    if (lhs[i].first != rhs[i].first || lhs[i].second != rhs[i].second) {
      return false;
    }
  }

  return true;
}

bool isCharacterizationSeqData(StaSeqPathData* seq_data, uint64_t epoch) {
  auto* delay_data = seq_data ? seq_data->get_delay_data() : nullptr;
  return delay_data && delay_data->get_data_epoch() == epoch;
}

std::optional<double> bindingCheckMarginNs(
    const StaCheckPairBinding* check_pair_binding) {
  return check_pair_binding
             ? std::optional<double>(
                   FS_TO_NS(check_pair_binding->sampled_check_margin_fs))
             : std::nullopt;
}

std::optional<double> bindingSlewNs(const StaCheckPairBinding* check_pair_binding,
                                    bool is_clock_slew) {
  if (!check_pair_binding) {
    return std::nullopt;
  }
  return std::optional<double>(FS_TO_NS(is_clock_slew
                                            ? check_pair_binding->clock_slew_fs
                                            : check_pair_binding->data_slew_fs));
}

const char* checkPairBindingStatus(const StaCheckPairBinding* check_pair_binding,
                                   uint64_t epoch, StaArc* check_arc,
                                   AnalysisMode analysis_mode,
                                   TransType clock_trans_type,
                                   TransType data_trans_type) {
  if (!check_pair_binding) {
    return "missing";
  }
  if (!check_pair_binding->clock_slew_data ||
      !check_pair_binding->data_slew_data) {
    return "missing_provenance";
  }
  if (check_pair_binding->data_epoch != epoch ||
      check_pair_binding->clock_slew_data->get_data_epoch() != epoch ||
      check_pair_binding->data_slew_data->get_data_epoch() != epoch) {
    return "epoch_mismatch";
  }
  if (check_pair_binding->check_arc != check_arc) {
    return "arc_mismatch";
  }
  if (check_pair_binding->analysis_mode != analysis_mode) {
    return "mode_mismatch";
  }
  if (check_pair_binding->clock_trans_type != clock_trans_type) {
    return "clock_trans_mismatch";
  }
  if (check_pair_binding->check_trans_type != data_trans_type) {
    return "check_trans_mismatch";
  }
  if (check_pair_binding->data_trans_type != data_trans_type) {
    return "data_trans_mismatch";
  }
  return "exact";
}

bool checkPairBindingIsExact(const StaCheckPairBinding* check_pair_binding,
                             uint64_t epoch, StaArc* check_arc,
                             AnalysisMode analysis_mode,
                             TransType clock_trans_type,
                             TransType data_trans_type) {
  return checkPairBindingStatus(check_pair_binding, epoch, check_arc,
                                analysis_mode, clock_trans_type,
                                data_trans_type) == std::string("exact");
}

ExactSeqCheckSnapshotMatch findExactSeqCheckSnapshotForExport(
    const std::vector<PreservedSeqCheckSnapshot>& seq_check_snapshots,
    const BoundaryDelayCandidate& data_candidate,
    const BoundaryDelayCandidate& clock_candidate, StaVertex* clk_point,
    StaVertex* clock_start_vertex, StaArc* check_arc,
    AnalysisMode capture_analysis_mode, TransType clock_trans_type) {
  ExactSeqCheckSnapshotMatch result;
  if (!clk_point) {
    return result;
  }

  for (const auto& seq_snapshot : seq_check_snapshots) {
    if (check_arc &&
        !checkArcMatchesForTimingModel(seq_snapshot.check_arc, check_arc)) {
      continue;
    }

    if (!seq_snapshot.capture_clock_vertex ||
        seq_snapshot.capture_clock_vertex != clk_point ||
        seq_snapshot.capture_analysis_mode != capture_analysis_mode ||
        seq_snapshot.clock_trans_type != clock_trans_type) {
      continue;
    }

    if (clock_start_vertex &&
        seq_snapshot.capture_clock_start_vertex != clock_start_vertex) {
      continue;
    }

    if (!seq_snapshot.data_start_vertex ||
        seq_snapshot.data_start_vertex != data_candidate.terms.start_vertex ||
        seq_snapshot.data_start_trans_type !=
            data_candidate.terms.start_trans_type ||
        seq_snapshot.data_end_trans_type !=
            data_candidate.terms.end_trans_type) {
      continue;
    }

    if (seq_snapshot.data_start_arrive_fs !=
            data_candidate.terms.start_arrive_fs ||
        seq_snapshot.data_end_arrive_fs !=
            data_candidate.terms.end_arrive_fs ||
        seq_snapshot.capture_clock_start_arrive_fs !=
            clock_candidate.terms.start_arrive_fs ||
        seq_snapshot.capture_clock_end_arrive_fs !=
            clock_candidate.terms.end_arrive_fs) {
      continue;
    }

    ++result.match_count;
    result.snapshot = &seq_snapshot;
  }

  if (result.match_count != 1) {
    result.snapshot = nullptr;
  }

  return result;
}

ExactSeqCheckSnapshotMatch findRelaxedSeqCheckSnapshotForExport(
    const std::vector<PreservedSeqCheckSnapshot>& seq_check_snapshots,
    const BoundaryDelayCandidate& data_candidate, StaVertex* clk_point,
    StaVertex* clock_start_vertex, StaArc* check_arc,
    AnalysisMode capture_analysis_mode, TransType clock_trans_type) {
  ExactSeqCheckSnapshotMatch result;
  if (!clk_point) {
    return result;
  }

  for (const auto& seq_snapshot : seq_check_snapshots) {
    if (check_arc &&
        !checkArcMatchesForTimingModel(seq_snapshot.check_arc, check_arc)) {
      continue;
    }

    if (!seq_snapshot.capture_clock_vertex ||
        seq_snapshot.capture_clock_vertex != clk_point ||
        seq_snapshot.capture_analysis_mode != capture_analysis_mode ||
        seq_snapshot.clock_trans_type != clock_trans_type) {
      continue;
    }

    if (clock_start_vertex &&
        seq_snapshot.capture_clock_start_vertex != clock_start_vertex) {
      continue;
    }

    if (!seq_snapshot.data_start_vertex ||
        seq_snapshot.data_start_vertex != data_candidate.terms.start_vertex ||
        seq_snapshot.data_start_trans_type !=
            data_candidate.terms.start_trans_type ||
        seq_snapshot.data_end_trans_type !=
            data_candidate.terms.end_trans_type) {
      continue;
    }

    ++result.match_count;
    result.snapshot = &seq_snapshot;
  }

  if (result.match_count != 1) {
    result.snapshot = nullptr;
  }

  return result;
}

struct ExactLocalSeqDataMatch {
  StaSeqPathData* seq_data = nullptr;
  size_t match_count = 0;
};

struct ExactSlewDataMatch {
  StaSlewData* slew_data = nullptr;
  size_t match_count = 0;
};

ExactSlewDataMatch findExactSlewDataForPath(StaVertex* vertex,
                                            AnalysisMode analysis_mode,
                                            TransType trans_type,
                                            uint64_t epoch,
                                            const PathVertexSignature& path_sig) {
  ExactSlewDataMatch result;
  if (!vertex) {
    return result;
  }

  StaData* data = nullptr;
  FOREACH_SLEW_DATA(vertex, data) {
    if ((data->get_delay_type() != analysis_mode) ||
        (data->get_trans_type() != trans_type) ||
        !isCharacterizationData(data, epoch)) {
      continue;
    }

    auto* slew_data = dynamic_cast<StaSlewData*>(data);
    if (!slew_data) {
      continue;
    }

    auto slew_path_sig = extractPathVertexSignature(slew_data->getPathData());
    if (!slew_path_sig ||
        !pathVertexSignatureMatches(*slew_path_sig, path_sig)) {
      continue;
    }

    ++result.match_count;
    result.slew_data = slew_data;
  }

  if (result.match_count != 1) {
    result.slew_data = nullptr;
  }

  return result;
}

ExactLocalSeqDataMatch findExactLocalSeqDataForExport(
    const std::vector<StaSeqPathData*>& seq_data_vec,
    const BoundaryDelayCandidate& data_candidate, StaVertex* clk_point,
    StaArc* check_arc, AnalysisMode capture_analysis_mode,
    TransType clock_trans_type, uint64_t epoch) {
  ExactLocalSeqDataMatch result;
  if (!clk_point || !data_candidate.delay_data) {
    return result;
  }

  for (auto* seq_data : seq_data_vec) {
    if (!isCharacterizationSeqData(seq_data, epoch)) {
      continue;
    }

    auto* delay_data = seq_data->get_delay_data();
    auto* capture_clock_data = seq_data->get_capture_clock_data();
    if (!delay_data || delay_data != data_candidate.delay_data ||
        !capture_clock_data || capture_clock_data->get_own_vertex() != clk_point ||
        capture_clock_data->get_delay_type() != capture_analysis_mode ||
        capture_clock_data->get_trans_type() != clock_trans_type) {
      continue;
    }

    if (check_arc &&
        !checkArcMatchesForTimingModel(seq_data->get_check_arc(), check_arc)) {
      continue;
    }

    auto data_terms = extractBoundaryDelayTerms(seq_data->getPathDelayData());
    if (!data_terms ||
        data_terms->start_vertex != data_candidate.terms.start_vertex ||
        data_terms->start_trans_type != data_candidate.terms.start_trans_type ||
        data_terms->end_trans_type != data_candidate.terms.end_trans_type ||
        data_terms->start_arrive_fs != data_candidate.terms.start_arrive_fs ||
        data_terms->end_arrive_fs != data_candidate.terms.end_arrive_fs) {
      continue;
    }

    ++result.match_count;
    result.seq_data = seq_data;
  }

  if (result.match_count != 1) {
    result.seq_data = nullptr;
  }

  return result;
}

double convertPfToReferenceCapUnit(LibArc* reference_arc, double cap_pf) {
  auto* owner_cell = reference_arc ? reference_arc->get_owner_cell() : nullptr;
  auto* owner_lib = owner_cell ? owner_cell->get_owner_lib() : nullptr;
  return owner_lib
             ? ConvertCapUnit(CapacitiveUnit::kPF, owner_lib->get_cap_unit(),
                              cap_pf)
             : cap_pf;
}

double convertNsToReferenceTimeAxisUnit(LibArc* reference_arc,
                                        double time_ns) {
  auto* owner_cell = reference_arc ? reference_arc->get_owner_cell() : nullptr;
  auto* owner_lib = owner_cell ? owner_cell->get_owner_lib() : nullptr;
  if (!owner_lib) {
    return time_ns;
  }

  switch (owner_lib->get_time_unit()) {
    case TimeUnit::kPS:
      return time_ns * 1e3;
    case TimeUnit::kFS:
      return time_ns * 1e6;
    case TimeUnit::kNS:
    default:
      return time_ns;
  }
}

bool shouldTraceCharacterizationPin(const char* pin_name) {
  if (!pin_name) {
    return false;
  }

  const char* trace_env = std::getenv("IEDA_CHARACTER_TIMING_TRACE_PINS");
  if (!trace_env || !*trace_env) {
    return false;
  }

  std::stringstream ss(trace_env);
  std::string item;
  while (std::getline(ss, item, ',')) {
    item.erase(std::remove_if(item.begin(), item.end(), ::isspace),
               item.end());
    if (!item.empty() && item == pin_name) {
      return true;
    }
  }

  return false;
}

bool shouldTraceClockSlewVertex(const std::string& vertex_name) {
  const char* trace_env = std::getenv("IEDA_TRACE_CLOCK_SLEW_VERTICES");
  if (!trace_env || !*trace_env) {
    return false;
  }

  std::stringstream ss(trace_env);
  std::string item;
  while (std::getline(ss, item, ',')) {
    item.erase(std::remove_if(item.begin(), item.end(), ::isspace),
               item.end());
    if (!item.empty() && vertex_name.find(item) != std::string::npos) {
      return true;
    }
  }

  return false;
}

const char* analysisModeName(AnalysisMode analysis_mode) {
  switch (analysis_mode) {
    case AnalysisMode::kMax:
      return "max";
    case AnalysisMode::kMin:
      return "min";
    case AnalysisMode::kMaxMin:
      return "maxmin";
    default:
      return "unknown";
  }
}

const char* transTypeName(TransType trans_type) {
  switch (trans_type) {
    case TransType::kRise:
      return "rise";
    case TransType::kFall:
      return "fall";
    default:
      return "unknown";
  }
}

std::string formatOptionalNs(std::optional<double> value) {
  if (!value) {
    return "na";
  }

  std::ostringstream os;
  os << *value;
  return os.str();
}

std::string buildSlewCandidateTrace(StaVertex* the_vertex,
                                    AnalysisMode analysis_mode,
                                    TransType trans_type) {
  std::vector<StaSlewData*> candidates;
  StaData* data = nullptr;
  FOREACH_SLEW_DATA(the_vertex, data) {
    if ((data->get_delay_type() != analysis_mode) ||
        (data->get_trans_type() != trans_type)) {
      continue;
    }

    auto* slew_data = dynamic_cast<StaSlewData*>(data);
    if (slew_data) {
      candidates.push_back(slew_data);
    }
  }

  std::ostringstream os;
  const auto field_prefix =
      std::string(analysisModeName(analysis_mode)) + "_" +
      transTypeName(trans_type);
  os << " " << field_prefix << "_candidate_count=" << candidates.size();

  auto* api_selected = the_vertex->getWorstSlewData(analysis_mode, trans_type);
  os << " " << field_prefix
     << "_api_selected_slew_ns="
     << formatOptionalNs(api_selected
                             ? std::optional<double>(
                                   FS_TO_NS(api_selected->get_slew()))
                             : std::nullopt);

  StaSlewData* manual_selected = nullptr;
  for (auto* candidate : candidates) {
    if (!manual_selected) {
      manual_selected = candidate;
      continue;
    }

    if ((analysis_mode == AnalysisMode::kMax &&
         candidate->get_slew() > manual_selected->get_slew()) ||
        (analysis_mode == AnalysisMode::kMin &&
         candidate->get_slew() < manual_selected->get_slew())) {
      manual_selected = candidate;
    }
  }

  os << " " << field_prefix
     << "_manual_selected_slew_ns="
     << formatOptionalNs(manual_selected
                             ? std::optional<double>(
                                   FS_TO_NS(manual_selected->get_slew()))
                             : std::nullopt);

  std::stable_sort(candidates.begin(), candidates.end(),
                   [](StaSlewData* left, StaSlewData* right) {
                     if (left->get_slew() != right->get_slew()) {
                       return left->get_slew() < right->get_slew();
                     }
                     if (left->get_data_epoch() != right->get_data_epoch()) {
                       return left->get_data_epoch() < right->get_data_epoch();
                     }

                     auto* left_bwd_vertex =
                         left->get_bwd() ? left->get_bwd()->get_own_vertex()
                                         : nullptr;
                     auto* right_bwd_vertex =
                         right->get_bwd() ? right->get_bwd()->get_own_vertex()
                                          : nullptr;
                     const auto left_name =
                         left_bwd_vertex ? left_bwd_vertex->getName() : "";
                     const auto right_name =
                         right_bwd_vertex ? right_bwd_vertex->getName() : "";
                     return left_name < right_name;
                   });

  os << " " << field_prefix << "_candidates=[";
  for (size_t i = 0; i < candidates.size(); ++i) {
    auto* candidate = candidates[i];
    auto* bwd_vertex =
        candidate->get_bwd() ? candidate->get_bwd()->get_own_vertex() : nullptr;
    if (i != 0) {
      os << ";";
    }
    os << "{slew_ns=" << FS_TO_NS(candidate->get_slew())
       << ",epoch=" << candidate->get_data_epoch()
       << ",bwd=" << (bwd_vertex ? bwd_vertex->getName() : "null") << "}";
  }
  os << "]";

  return os.str();
}

void traceClockSlewVertexSnapshot(StaVertex* the_vertex, bool snapshot_eligible,
                                  size_t snapshot_entries) {
  if (!the_vertex ||
      !shouldTraceClockSlewVertex(the_vertex->getName())) {
    return;
  }

  std::ostringstream os;
  os << "[character_timing][full-sta-clock-snapshot] vertex="
     << the_vertex->getName() << " is_port=" << the_vertex->is_port()
     << " is_start=" << the_vertex->is_start()
     << " is_clock=" << the_vertex->is_clock()
     << " is_sdc_clock_pin=" << the_vertex->is_sdc_clock_pin()
     << " snapshot_eligible=" << snapshot_eligible
     << " snapshot_entries=" << snapshot_entries;

  for (auto analysis_mode : {AnalysisMode::kMax, AnalysisMode::kMin}) {
    for (auto trans_type : {TransType::kRise, TransType::kFall}) {
      auto* slew_data = the_vertex->getWorstSlewData(analysis_mode, trans_type);
      auto slew_ns =
          slew_data ? std::optional<double>(FS_TO_NS(slew_data->get_slew()))
                    : std::nullopt;
      auto clock_datas = the_vertex->getClockData(analysis_mode, trans_type);
      auto clock_arrive_ns =
          the_vertex->getClockArriveTime(analysis_mode, trans_type);

      os << " " << analysisModeName(analysis_mode) << "_"
         << transTypeName(trans_type) << "_slew_ns=";
      if (slew_ns) {
        os << *slew_ns;
      } else {
        os << "na";
      }

      os << " " << analysisModeName(analysis_mode) << "_"
         << transTypeName(trans_type) << "_clock_data="
         << clock_datas.size();
      os << " " << analysisModeName(analysis_mode) << "_"
         << transTypeName(trans_type) << "_clock_arrive_ns=";
      if (clock_arrive_ns) {
        os << FS_TO_NS(*clock_arrive_ns);
      } else {
        os << "na";
      }

      os << buildSlewCandidateTrace(the_vertex, analysis_mode, trans_type);
    }
  }

  LOG_INFO << os.str();
}

std::optional<double> rebuildPathDelayNsForEpoch(StaPathDelayData* delay_data,
                                                 uint64_t epoch) {
  if (!delay_data) {
    return std::nullopt;
  }

  auto path_stack = delay_data->getPathData();
  std::vector<StaPathDelayData*> path_nodes;
  while (!path_stack.empty()) {
    auto* path_node = dynamic_cast<StaPathDelayData*>(path_stack.top());
    path_stack.pop();
    if (!path_node || !path_node->get_own_vertex()) {
      return std::nullopt;
    }
    path_nodes.push_back(path_node);
  }

  if (path_nodes.empty()) {
    return std::nullopt;
  }

  int64_t rebuilt_delay_fs = 0;
  for (size_t i = 1; i < path_nodes.size(); ++i) {
    auto* src_vertex = path_nodes[i - 1]->get_own_vertex();
    auto* snk_node = path_nodes[i];
    auto* snk_vertex = snk_node->get_own_vertex();
    if (!src_vertex || !snk_vertex) {
      return std::nullopt;
    }

    auto arcs = snk_vertex->getSrcArc(src_vertex);
    StaArc* matched_arc = nullptr;
    for (auto* arc : arcs) {
      if (arc && arc->isDelayArc()) {
        matched_arc = arc;
        break;
      }
    }

    if (!matched_arc) {
      return std::nullopt;
    }

    rebuilt_delay_fs += matched_arc->get_arc_delay(
        snk_node->get_delay_type(), snk_node->get_trans_type(), epoch);
  }

  if (auto* launch_clock_data = delay_data->get_launch_clock_data();
      launch_clock_data) {
    rebuilt_delay_fs += launch_clock_data->get_arrive_time();
  }

  return FS_TO_NS(rebuilt_delay_fs);
}

void resetCharacterizationPayload(StaGraph* the_graph) {
  auto reset_vertex_payload = [](StaVertex* the_vertex) {
    if (!the_vertex) {
      return;
    }

    // Preserve clock/context buckets from the propagated-clock refresh, but
    // drop full-STA local payload so ETM extraction seeds a clean epoch-local
    // graph.
    the_vertex->resetSlewBucket();
    the_vertex->resetPathDelayBucket();
    the_vertex->resetColor();
    the_vertex->resetLevel();
    the_vertex->reset_is_slew_prop();
    the_vertex->reset_is_delay_prop();
    the_vertex->reset_is_fwd();
    the_vertex->reset_is_bwd();
  };

  StaVertex* the_vertex = nullptr;
  FOREACH_VERTEX(the_graph, the_vertex) {
    reset_vertex_payload(the_vertex);
  }

  FOREACH_ASSISTANT_VERTEX(the_graph, assistant_vertex) {
    reset_vertex_payload(assistant_vertex.get());
  }

  StaArc* the_arc = nullptr;
  FOREACH_ARC(the_graph, the_arc) {
    the_arc->resetArcDelayBucket();
    if (auto* net_arc = dynamic_cast<StaNetArc*>(the_arc)) {
      net_arc->getWaveformBucket().freeData();
    }
  }
}

}  // namespace
