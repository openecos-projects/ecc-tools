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
#include "RuleValidator.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <utility>

#include "DRCHeader.hpp"
#include "GDSPlotter.hpp"
#include "Monitor.hpp"
#include "PlanarRect.hpp"
#include "RVCluster.hpp"
#include "Utility.hpp"

namespace idrc {

namespace {

// Bins each shape into every cluster overlapped by its enlarged rect. Pass 1 counts the
// (thread, cluster) pair distribution in parallel, then pass 2 scatters shape pointers
// directly into each cluster's vector at precomputed per-thread offsets. No intermediate
// pair buffer, no sort, no merge pass; every output slot is written by exactly one thread.
template <typename GetClusterShapeList>
void binShapesToClusters(std::vector<DRCShape>& shape_list, std::vector<RVCluster>& rv_cluster_list,
                         GetClusterShapeList get_cluster_shape_list, int32_t expand_size, int32_t cluster_size, int32_t grid_x_size,
                         const PlanarRect& bounding_box)
{
  if (shape_list.empty()) {
    return;
  }
  const int32_t offset_x = bounding_box.get_ll_x();
  const int32_t offset_y = bounding_box.get_ll_y();
  const int32_t bbox_ur_x = bounding_box.get_ur_x();
  const int32_t bbox_ur_y = bounding_box.get_ur_y();
  const int32_t cluster_num = static_cast<int32_t>(rv_cluster_list.size());
  const int64_t shape_num = static_cast<int64_t>(shape_list.size());
  const int32_t thread_num = std::max(omp_get_max_threads(), 1);
  // Manual static partitioning so both passes iterate identical (thread, shape range) pairs.
  const int64_t chunk_size = (shape_num + thread_num - 1) / thread_num;

  // Visits each cluster overlapped by the shape's enlarged rect (inlined
  // DRCUTIL.getEnlargedRect + isClosedOverlap + getRegularRect against bounding_box).
  auto for_each_cluster_idx = [&](DRCShape& drc_shape, auto visit) {
    int32_t ll_x = std::max(drc_shape.get_ll_x() - expand_size, offset_x);
    int32_t ll_y = std::max(drc_shape.get_ll_y() - expand_size, offset_y);
    int32_t ur_x = std::min(drc_shape.get_ur_x() + expand_size, bbox_ur_x);
    int32_t ur_y = std::min(drc_shape.get_ur_y() + expand_size, bbox_ur_y);
    if (ll_x > ur_x || ll_y > ur_y) {
      return;
    }
    int32_t grid_ll_x = (ll_x - offset_x) / cluster_size;
    int32_t grid_ll_y = (ll_y - offset_y) / cluster_size;
    int32_t grid_ur_x = (ur_x - offset_x) / cluster_size;
    int32_t grid_ur_y = (ur_y - offset_y) / cluster_size;
    for (int32_t grid_x = grid_ll_x; grid_x <= grid_ur_x; grid_x++) {
      for (int32_t grid_y = grid_ll_y; grid_y <= grid_ur_y; grid_y++) {
        int32_t cluster_idx = grid_x + grid_y * grid_x_size;
        if (cluster_idx < 0 || cluster_num <= cluster_idx) {
          continue;
        }
        visit(cluster_idx);
      }
    }
  };

  std::vector<std::vector<int32_t>> thread_count_lists(thread_num, std::vector<int32_t>(cluster_num, 0));
#pragma omp parallel
  {
    int32_t thread_idx = omp_get_thread_num();
    std::vector<int32_t>& local_count_list = thread_count_lists[thread_idx];
    int64_t shape_begin = std::min(shape_num, chunk_size * thread_idx);
    int64_t shape_end = std::min(shape_num, shape_begin + chunk_size);
    for (int64_t shape_idx = shape_begin; shape_idx < shape_end; shape_idx++) {
      for_each_cluster_idx(shape_list[shape_idx], [&](int32_t cluster_idx) { local_count_list[cluster_idx]++; });
    }
  }

  // thread_offset_lists[t][c] = write position of thread t's first shape within cluster c.
  std::vector<std::vector<int64_t>> thread_offset_lists(thread_num, std::vector<int64_t>(cluster_num, 0));
#pragma omp parallel for schedule(static)
  for (int32_t cluster_idx = 0; cluster_idx < cluster_num; cluster_idx++) {
    int64_t offset = 0;
    for (int32_t thread_idx = 0; thread_idx < thread_num; thread_idx++) {
      thread_offset_lists[thread_idx][cluster_idx] = offset;
      offset += thread_count_lists[thread_idx][cluster_idx];
    }
    get_cluster_shape_list(rv_cluster_list[cluster_idx]).resize(offset);
  }

#pragma omp parallel
  {
    int32_t thread_idx = omp_get_thread_num();
    std::vector<int64_t>& local_offset_list = thread_offset_lists[thread_idx];
    int64_t shape_begin = std::min(shape_num, chunk_size * thread_idx);
    int64_t shape_end = std::min(shape_num, shape_begin + chunk_size);
    for (int64_t shape_idx = shape_begin; shape_idx < shape_end; shape_idx++) {
      DRCShape& drc_shape = shape_list[shape_idx];
      for_each_cluster_idx(drc_shape, [&](int32_t cluster_idx) {
        get_cluster_shape_list(rv_cluster_list[cluster_idx])[local_offset_list[cluster_idx]++] = &drc_shape;
      });
    }
  }
}
}  // namespace

// public

void RuleValidator::initInst()
{
  if (_rv_instance == nullptr) {
    _rv_instance = new RuleValidator();
  }
}

RuleValidator& RuleValidator::getInst()
{
  if (_rv_instance == nullptr) {
    DRCLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_rv_instance;
}

void RuleValidator::destroyInst()
{
  if (_rv_instance != nullptr) {
    delete _rv_instance;
    _rv_instance = nullptr;
  }
}

// function
std::vector<Violation> RuleValidator::verify(std::vector<DRCShape> drc_env_shape_list, std::vector<DRCShape> drc_result_shape_list,
                                             std::set<ViolationType> drc_check_type_set, std::vector<DRCShape> drc_check_region_list)
{
  auto monitor = Monitor::create();
  DRCLOG.info(Loc::current(), "Starting...");
  if (drc_env_shape_list.empty() && drc_result_shape_list.empty()) {
    DRCLOG.info(Loc::current(), "Completed", monitor ? monitor->getStatsInfo() : "");
    return {};
  }
  RVModel rv_model(std::move(drc_env_shape_list), std::move(drc_result_shape_list), std::move(drc_check_type_set), std::move(drc_check_region_list));
  setRVComParam(rv_model);
  auto build_cluster_monitor = Monitor::create();
  buildRVClusterList(rv_model);
  DRCLOG.info(Loc::current(), "Stage buildRVClusterList completed", build_cluster_monitor ? build_cluster_monitor->getStatsInfo() : "");

  auto verify_model_monitor = Monitor::create();
  verifyRVModel(rv_model);

  DRCLOG.info(Loc::current(), "Stage verifyRVModel completed", verify_model_monitor ? verify_model_monitor->getStatsInfo() : "");

  auto build_violation_monitor = Monitor::create();
  buildViolationList(rv_model);
  DRCLOG.info(Loc::current(), "Stage buildViolationList completed", build_violation_monitor ? build_violation_monitor->getStatsInfo() : "");
  // debugPlotRVModel(rv_model, "best");
  DRCLOG.info(Loc::current(), "Completed", monitor ? monitor->getStatsInfo() : "");
  return std::move(rv_model.get_violation_list());
}

// private

RuleValidator* RuleValidator::_rv_instance = nullptr;

void RuleValidator::setRVComParam(RVModel& rv_model)
{
  int32_t only_pitch = DRCDM.getOnlyPitch();
  int32_t expand_size = 5 * only_pitch;
  int32_t cluster_size = -1;
  // Experiment hook: ECC_RV_CLUSTER_MULT overrides the cluster size multiplier (in units of only_pitch).
  if (const char* env = std::getenv("ECC_RV_CLUSTER_MULT")) {
    int32_t mult = std::atoi(env);
    if (mult > 0) {
      cluster_size = mult * only_pitch;
    }
  }
  // Experiment hook: ECC_RV_EXPAND_MULT overrides the halo expand multiplier.
  if (const char* env = std::getenv("ECC_RV_EXPAND_MULT")) {
    int32_t mult = std::atoi(env);
    if (mult > 0) {
      expand_size = mult * only_pitch;
    }
  }
  if (cluster_size <= 0) {
    // cluster_size = chooseClusterSize(rv_model, only_pitch, expand_size);
    cluster_size = 200 * only_pitch;
  }
  /**
   * cluster_size, expand_size
   */
  // clang-format off
  RVComParam rv_com_param(cluster_size, expand_size);
  // clang-format on
  DRCLOG.info(Loc::current(), "cluster_size: ", rv_com_param.get_cluster_size());
  DRCLOG.info(Loc::current(), "expand_size: ", rv_com_param.get_expand_size());
  rv_model.set_rv_com_param(rv_com_param);
}

// Adaptive cluster size selection.
// Profiling on this PDK shows per-cluster time follows c(m) = a*m + b*m^alpha, where m is the
// number of (cluster, shape) loads: the linear term covers halo-duplicated geometry work, and
// the superlinear term (alpha ~= 1.46) comes from rule-level connected-component merging
// (EndOfLineSpacing / EnclosureEdge), which dominates once a cluster exceeds ~85k loads.
// With K(s) grid cells and exact total loads M(s), estimated wall time is
//   T(s) = [a*M(s) + b*K(s)*m_avg(s)^alpha] / p + a*phi*m_avg(s)/2 + c_assign*M(s)/p
// where the second term is the parallel tail from the largest cluster (max load ~ phi*mean).
// M(s) is computed exactly from shape bounding boxes in one O(N * candidates) pass.
int32_t RuleValidator::chooseClusterSize(RVModel& rv_model, int32_t only_pitch, int32_t expand_size)
{
  constexpr double kCostPerLoadMs = 3.854e-3;    // a: calibrated linear per-load cost
  constexpr double kSuperlinearCoef = 3.393e-6;  // b: component-merge term coefficient
  constexpr double kSuperlinearExp = 1.46;       // alpha: measured onset of superlinearity
  constexpr double kMaxLoadFactor = 1.5;         // phi: max / mean cluster load
  constexpr double kAssignCostPerLoadMs = 3.8e-4;  // shape binning cost per load
  constexpr int32_t kCandidateMultList[] = {25, 50, 100, 200, 400};

  PlanarRect bounding_box(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN);
  if (rv_model.get_drc_check_region_list().empty()) {
    int32_t bbox_ll_x = INT32_MAX, bbox_ll_y = INT32_MAX, bbox_ur_x = INT32_MIN, bbox_ur_y = INT32_MIN;
    for (auto* shape_list : {&rv_model.get_drc_env_shape_list(), &rv_model.get_drc_result_shape_list()}) {
#pragma omp parallel for schedule(static) reduction(min : bbox_ll_x, bbox_ll_y) reduction(max : bbox_ur_x, bbox_ur_y)
      for (int64_t shape_idx = 0; shape_idx < static_cast<int64_t>(shape_list->size()); shape_idx++) {
        DRCShape& drc_shape = (*shape_list)[shape_idx];
        bbox_ll_x = std::min(bbox_ll_x, drc_shape.get_ll_x());
        bbox_ll_y = std::min(bbox_ll_y, drc_shape.get_ll_y());
        bbox_ur_x = std::max(bbox_ur_x, drc_shape.get_ur_x());
        bbox_ur_y = std::max(bbox_ur_y, drc_shape.get_ur_y());
      }
    }
    bounding_box = PlanarRect(bbox_ll_x, bbox_ll_y, bbox_ur_x, bbox_ur_y);
  } else {
    for (DRCShape& check_region : rv_model.get_drc_check_region_list()) {
      PlanarRect region_rect = DRCUTIL.getEnlargedRect(check_region.get_rect(), expand_size);
      bounding_box.set_ll_x(std::min(bounding_box.get_ll_x(), region_rect.get_ll_x()));
      bounding_box.set_ll_y(std::min(bounding_box.get_ll_y(), region_rect.get_ll_y()));
      bounding_box.set_ur_x(std::max(bounding_box.get_ur_x(), region_rect.get_ur_x()));
      bounding_box.set_ur_y(std::max(bounding_box.get_ur_y(), region_rect.get_ur_y()));
    }
  }

  const int32_t offset_x = bounding_box.get_ll_x();
  const int32_t offset_y = bounding_box.get_ll_y();
  const int32_t bbox_ur_x = bounding_box.get_ur_x();
  const int32_t bbox_ur_y = bounding_box.get_ur_y();
  const int32_t thread_num = std::max(DRCDM.getConfig().thread_number, 1);
  constexpr int32_t candidate_num = sizeof(kCandidateMultList) / sizeof(kCandidateMultList[0]);

  // load_num_list[c] = M(s_c): total (cluster, shape) loads at candidate size s_c.
  int64_t load_num_list[candidate_num] = {};
  for (auto* shape_list_ptr : {&rv_model.get_drc_env_shape_list(), &rv_model.get_drc_result_shape_list()}) {
    std::vector<DRCShape>& shape_list = *shape_list_ptr;
    int64_t local_load_list[candidate_num] = {};
#pragma omp parallel for schedule(static) reduction(+ : local_load_list[:candidate_num])
    for (int64_t shape_idx = 0; shape_idx < static_cast<int64_t>(shape_list.size()); shape_idx++) {
      DRCShape& drc_shape = shape_list[shape_idx];
      int32_t ll_x = std::max(drc_shape.get_ll_x() - expand_size, offset_x);
      int32_t ll_y = std::max(drc_shape.get_ll_y() - expand_size, offset_y);
      int32_t ur_x = std::min(drc_shape.get_ur_x() + expand_size, bbox_ur_x);
      int32_t ur_y = std::min(drc_shape.get_ur_y() + expand_size, bbox_ur_y);
      if (ll_x > ur_x || ll_y > ur_y) {
        continue;
      }
      for (int32_t c = 0; c < candidate_num; c++) {
        int32_t s = kCandidateMultList[c] * only_pitch;
        local_load_list[c] += (static_cast<int64_t>(ur_x - offset_x) / s - (ll_x - offset_x) / s + 1)
                              * (static_cast<int64_t>(ur_y - offset_y) / s - (ll_y - offset_y) / s + 1);
      }
    }
    for (int32_t c = 0; c < candidate_num; c++) {
      load_num_list[c] += local_load_list[c];
    }
  }

  int32_t best_cluster_size = kCandidateMultList[0] * only_pitch;
  double best_time_ms = std::numeric_limits<double>::max();
  for (int32_t c = 0; c < candidate_num; c++) {
    int32_t s = kCandidateMultList[c] * only_pitch;
    int64_t grid_cell_num = (bounding_box.getXSpan() / s + 1) * (bounding_box.getYSpan() / s + 1);
    double load_num = static_cast<double>(load_num_list[c]);
    double mean_load = load_num / std::max<int64_t>(grid_cell_num, 1);
    double work_ms = kCostPerLoadMs * load_num
                     + kSuperlinearCoef * std::pow(mean_load, kSuperlinearExp) * static_cast<double>(grid_cell_num);
    double time_ms = work_ms / thread_num + kCostPerLoadMs * kMaxLoadFactor * mean_load / 2.0
                     + kAssignCostPerLoadMs * load_num / thread_num;
    DRCLOG.info(Loc::current(), "cluster size candidate ", s, ": loads=", load_num_list[c], ", cells=", grid_cell_num,
                ", predicted_ms=", time_ms);
    if (time_ms < best_time_ms) {
      best_time_ms = time_ms;
      best_cluster_size = s;
    }
  }
  DRCLOG.info(Loc::current(), "adaptive cluster_size: ", best_cluster_size, " (pitch=", only_pitch, ", threads=", thread_num, ")");
  return best_cluster_size;
}

void RuleValidator::buildRVClusterList(RVModel& rv_model)
{
  std::vector<RVCluster>& rv_cluster_list = rv_model.get_rv_cluster_list();
  int32_t cluster_size = rv_model.get_rv_com_param().get_cluster_size();
  int32_t expand_size = rv_model.get_rv_com_param().get_expand_size();

  PlanarRect bounding_box(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN);
  int32_t offset_x = -1;
  int32_t offset_y = -1;
  int32_t grid_x_size = -1;
  int32_t grid_y_size = -1;
  {
    if (rv_model.get_drc_check_region_list().empty()) {
      int32_t bbox_ll_x = INT32_MAX;
      int32_t bbox_ll_y = INT32_MAX;
      int32_t bbox_ur_x = INT32_MIN;
      int32_t bbox_ur_y = INT32_MIN;
      std::vector<DRCShape>& drc_env_shape_list = rv_model.get_drc_env_shape_list();
      std::vector<DRCShape>& drc_result_shape_list = rv_model.get_drc_result_shape_list();
#pragma omp parallel for schedule(static) reduction(min : bbox_ll_x, bbox_ll_y) reduction(max : bbox_ur_x, bbox_ur_y)
      for (int64_t shape_idx = 0; shape_idx < static_cast<int64_t>(drc_env_shape_list.size()); shape_idx++) {
        DRCShape& drc_env_shape = drc_env_shape_list[shape_idx];
        bbox_ll_x = std::min(bbox_ll_x, drc_env_shape.get_ll_x());
        bbox_ll_y = std::min(bbox_ll_y, drc_env_shape.get_ll_y());
        bbox_ur_x = std::max(bbox_ur_x, drc_env_shape.get_ur_x());
        bbox_ur_y = std::max(bbox_ur_y, drc_env_shape.get_ur_y());
      }
#pragma omp parallel for schedule(static) reduction(min : bbox_ll_x, bbox_ll_y) reduction(max : bbox_ur_x, bbox_ur_y)
      for (int64_t shape_idx = 0; shape_idx < static_cast<int64_t>(drc_result_shape_list.size()); shape_idx++) {
        DRCShape& drc_result_shape = drc_result_shape_list[shape_idx];
        bbox_ll_x = std::min(bbox_ll_x, drc_result_shape.get_ll_x());
        bbox_ll_y = std::min(bbox_ll_y, drc_result_shape.get_ll_y());
        bbox_ur_x = std::max(bbox_ur_x, drc_result_shape.get_ur_x());
        bbox_ur_y = std::max(bbox_ur_y, drc_result_shape.get_ur_y());
      }
      bounding_box.set_ll_x(bbox_ll_x);
      bounding_box.set_ll_y(bbox_ll_y);
      bounding_box.set_ur_x(bbox_ur_x);
      bounding_box.set_ur_y(bbox_ur_y);
    } else {
      for (DRCShape& check_region : rv_model.get_drc_check_region_list()) {
        PlanarRect region_rect = DRCUTIL.getEnlargedRect(check_region.get_rect(), expand_size);
        bounding_box.set_ll_x(std::min(bounding_box.get_ll_x(), region_rect.get_ll_x()));
        bounding_box.set_ll_y(std::min(bounding_box.get_ll_y(), region_rect.get_ll_y()));
        bounding_box.set_ur_x(std::max(bounding_box.get_ur_x(), region_rect.get_ur_x()));
        bounding_box.set_ur_y(std::max(bounding_box.get_ur_y(), region_rect.get_ur_y()));
      }
    }
    offset_x = bounding_box.get_ll_x();
    offset_y = bounding_box.get_ll_y();
    grid_x_size = bounding_box.getXSpan() / cluster_size + 1;
    grid_y_size = bounding_box.getYSpan() / cluster_size + 1;
  }
  rv_cluster_list.resize(grid_x_size * grid_y_size);
  for (int32_t grid_x = 0; grid_x < grid_x_size; grid_x++) {
    for (int32_t grid_y = 0; grid_y < grid_y_size; grid_y++) {
      RVCluster& rv_cluster = rv_cluster_list[grid_x + grid_y * grid_x_size];
      rv_cluster.set_cluster_idx(grid_x + grid_y * grid_x_size);
      rv_cluster.get_cluster_rect_list().emplace_back(grid_x * cluster_size + offset_x, grid_y * cluster_size + offset_y,
                                                      (grid_x + 1) * cluster_size + offset_x, (grid_y + 1) * cluster_size + offset_y);
      rv_cluster.set_rv_com_param(&rv_model.get_rv_com_param());
    }
  }
  binShapesToClusters(rv_model.get_drc_env_shape_list(), rv_cluster_list,
                      [](RVCluster& rv_cluster) -> std::vector<DRCShape*>& { return rv_cluster.get_drc_env_shape_list(); }, expand_size,
                      cluster_size, grid_x_size, bounding_box);
  binShapesToClusters(rv_model.get_drc_result_shape_list(), rv_cluster_list,
                      [](RVCluster& rv_cluster) -> std::vector<DRCShape*>& { return rv_cluster.get_drc_result_shape_list(); }, expand_size,
                      cluster_size, grid_x_size, bounding_box);
  for (RVCluster& rv_cluster : rv_cluster_list) {
    rv_cluster.set_drc_check_type_set(&rv_model.get_drc_check_type_set());
    rv_cluster.set_drc_check_region_list(&rv_model.get_drc_check_region_list());
  }
  bool has_negative_net_idx = false;
  std::vector<DRCShape>& drc_result_shape_list = rv_model.get_drc_result_shape_list();
#pragma omp parallel for schedule(static) reduction(|| : has_negative_net_idx)
  for (int64_t shape_idx = 0; shape_idx < static_cast<int64_t>(drc_result_shape_list.size()); shape_idx++) {
    has_negative_net_idx = has_negative_net_idx || drc_result_shape_list[shape_idx].get_net_idx() < 0;
  }
  if (has_negative_net_idx) {
    DRCLOG.error(Loc::current(), "The drc_result_shape_list exist idx < 0!");
  }
}

void RuleValidator::verifyRVModel(RVModel& rv_model)
{
  auto monitor = Monitor::create();
  DRCLOG.info(Loc::current(), "Starting...");
  std::vector<RVCluster>& rv_cluster_list = rv_model.get_rv_cluster_list();
  const char* stats_path = std::getenv("ECC_RV_STATS_PATH");
  const bool need_stats = stats_path != nullptr;
  std::vector<std::array<double, 5>> cluster_time_list(need_stats ? rv_cluster_list.size() : 0);
  constexpr int32_t kRuleNum = 26;
  std::vector<std::array<double, kRuleNum>> cluster_rule_time_list(need_stats ? rv_cluster_list.size() : 0);
#pragma omp parallel for schedule(dynamic, 1)
  for (size_t cluster_idx = 0; cluster_idx < rv_cluster_list.size(); cluster_idx++) {
    RVCluster& rv_cluster = rv_cluster_list[cluster_idx];
    double time_begin = need_stats ? omp_get_wtime() : 0.0;
    buildRVCluster(rv_cluster);
    if (needVerifying(rv_cluster)) {
      if (need_stats) {
        double prepare_begin = omp_get_wtime();
        prepareRVCluster(rv_cluster);
        double verify_begin = omp_get_wtime();
        verifyRVCluster(rv_cluster, cluster_rule_time_list[cluster_idx].data());
        double clear_begin = omp_get_wtime();
        rv_cluster.get_layer_data().clear();
        double process_begin = omp_get_wtime();
        processRVCluster(rv_cluster);
        double time_end = omp_get_wtime();
        cluster_time_list[cluster_idx] = {time_end - time_begin, verify_begin - prepare_begin, clear_begin - verify_begin,
                                          process_begin - clear_begin, time_end - process_begin};
      } else {
        buildViolationList(rv_cluster);
      }
    } else if (need_stats) {
      cluster_time_list[cluster_idx] = {omp_get_wtime() - time_begin, 0.0, 0.0, 0.0, 0.0};
    }
  }
  DRCLOG.info(Loc::current(), "Completed", monitor ? monitor->getStatsInfo() : "");
  if (need_stats) {
    double sum_prepare = 0.0, sum_verify = 0.0, sum_clear = 0.0, sum_process = 0.0;
    std::ofstream stats_file(stats_path);
    stats_file << "cluster_idx,ll_x,ll_y,ur_x,ur_y,env_shape_num,result_shape_num,core_env_num,core_result_num,violation_num,"
                  "time_ms,prepare_ms,verify_ms,clear_ms,process_ms\n";
    for (size_t cluster_idx = 0; cluster_idx < rv_cluster_list.size(); cluster_idx++) {
      RVCluster& rv_cluster = rv_cluster_list[cluster_idx];
      PlanarRect& rect = rv_cluster.get_cluster_rect_list().front();
      // Core shapes: open-overlap with the (non-enlarged) cluster rect; the rest are halo-only loads.
      int64_t core_env_num = 0, core_result_num = 0;
      for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
        core_env_num += DRCUTIL.isOpenOverlap(rect, drc_shape->get_rect()) ? 1 : 0;
      }
      for (DRCShape* drc_shape : rv_cluster.get_drc_result_shape_list()) {
        core_result_num += DRCUTIL.isOpenOverlap(rect, drc_shape->get_rect()) ? 1 : 0;
      }
      auto& times = cluster_time_list[cluster_idx];
      sum_prepare += times[1];
      sum_verify += times[2];
      sum_clear += times[3];
      sum_process += times[4];
      stats_file << cluster_idx << ',' << rect.get_ll_x() << ',' << rect.get_ll_y() << ',' << rect.get_ur_x() << ',' << rect.get_ur_y()
                 << ',' << rv_cluster.get_drc_env_shape_list().size() << ',' << rv_cluster.get_drc_result_shape_list().size() << ','
                 << core_env_num << ',' << core_result_num << ',' << rv_cluster.get_violation_list().size() << ',' << times[0] * 1000.0
                 << ',' << times[1] * 1000.0 << ',' << times[2] * 1000.0 << ',' << times[3] * 1000.0 << ',' << times[4] * 1000.0 << '\n';
    }
    DRCLOG.info(Loc::current(), "stage sums (s): prepare=", sum_prepare, ", verify=", sum_verify, ", clear=", sum_clear,
                ", process=", sum_process);
    static const char* rule_name_list[kRuleNum] = {"AdjacentCutSpacing",   "CornerFillSpacing",  "CornerSpacing",
                                                   "CutEOLSpacing",        "CutShort",           "DifferentLayerCutSpacing",
                                                   "Enclosure",            "EnclosureEdge",      "EnclosureParallel",
                                                   "EndOfLineSpacing",     "FloatingPatch",      "JogToJogSpacing",
                                                   "MaximumWidth",         "MaxViaStack",        "MetalShort",
                                                   "MinHole",              "MinimumArea",        "MinimumCut",
                                                   "MinimumWidth",         "MinStep",            "NonsufficientMetalOverlap",
                                                   "NotchSpacing",         "OffGridOrWrongWay",  "OutOfDie",
                                                   "ParallelRunLengthSpacing", "SameLayerCutSpacing"};
    std::array<double, kRuleNum> rule_sum_list = {};
    for (auto& cluster_rules : cluster_rule_time_list) {
      for (int32_t rule_idx = 0; rule_idx < kRuleNum; rule_idx++) {
        rule_sum_list[rule_idx] += cluster_rules[rule_idx];
      }
    }
    for (int32_t rule_idx = 0; rule_idx < kRuleNum; rule_idx++) {
      DRCLOG.info(Loc::current(), "rule time (s): ", rule_name_list[rule_idx], " = ", rule_sum_list[rule_idx]);
    }
  }
}

void RuleValidator::buildRVCluster(RVCluster& rv_cluster)
{
  std::vector<DRCShape>* drc_check_region_list = rv_cluster.get_drc_check_region_list();
  int32_t expand_size = rv_cluster.get_rv_com_param()->get_expand_size();

  if (!drc_check_region_list->empty()) {
    std::vector<DRCShape*> drc_env_shape_list;
    std::vector<DRCShape*> drc_result_shape_list;
    for (DRCShape& drc_check_region : *drc_check_region_list) {
      PlanarRect searched_rect = DRCUTIL.getEnlargedRect(drc_check_region.get_rect(), expand_size);
      std::map<bool, std::set<int32_t>> type_layer_idx_map;
      {
        int32_t layer_idx = drc_check_region.get_layer_idx();
        type_layer_idx_map[true].insert({layer_idx - 1, layer_idx, layer_idx + 1});
        const std::vector<int32_t>& cut_layer_idx_list = DRCDM.getAdjacentCutLayerIdxList(layer_idx);
        type_layer_idx_map[false].insert(cut_layer_idx_list.begin(), cut_layer_idx_list.end());
      }
      for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
        if (DRCUTIL.exist(type_layer_idx_map[drc_shape->get_is_routing()], drc_shape->get_layer_idx())
            && DRCUTIL.isClosedOverlap(searched_rect, drc_shape->get_rect())) {
          drc_env_shape_list.push_back(drc_shape);
        }
      }
      for (DRCShape* drc_shape : rv_cluster.get_drc_result_shape_list()) {
        if (DRCUTIL.exist(type_layer_idx_map[drc_shape->get_is_routing()], drc_shape->get_layer_idx())
            && DRCUTIL.isClosedOverlap(searched_rect, drc_shape->get_rect())) {
          drc_result_shape_list.push_back(drc_shape);
        }
      }
    }
    std::sort(drc_env_shape_list.begin(), drc_env_shape_list.end());
    drc_env_shape_list.erase(std::unique(drc_env_shape_list.begin(), drc_env_shape_list.end()), drc_env_shape_list.end());
    std::sort(drc_result_shape_list.begin(), drc_result_shape_list.end());
    drc_result_shape_list.erase(std::unique(drc_result_shape_list.begin(), drc_result_shape_list.end()), drc_result_shape_list.end());
    rv_cluster.set_drc_env_shape_list(drc_env_shape_list);
    rv_cluster.set_drc_result_shape_list(drc_result_shape_list);
  }
}

bool RuleValidator::needVerifying(RVCluster& rv_cluster)
{
  if (rv_cluster.get_drc_result_shape_list().empty()) {
    return false;
  }
  for (DRCShape* drc_result_shape : rv_cluster.get_drc_result_shape_list()) {
    for (PlanarRect& cluster_rect : rv_cluster.get_cluster_rect_list()) {
      if (DRCUTIL.isOpenOverlap(cluster_rect, drc_result_shape->get_rect())) {
        return true;
      }
    }
  }
  return false;
}

void RuleValidator::buildViolationList(RVCluster& rv_cluster)
{
  prepareRVCluster(rv_cluster);
  verifyRVCluster(rv_cluster);

  // destroy cluster cache after verify
  rv_cluster.get_layer_data().clear();

  processRVCluster(rv_cluster);
}

namespace {

using MetalShortObsPolysetMap = std::map<int32_t, GTLPolySetInt>;
using MetalShortObsRectMap = std::map<int32_t, std::vector<GTLRectInt>>;

void addShapeToLayerData(std::map<int32_t, RVLayerData>& layer_data, DRCShape* drc_shape, bool is_env_shape);
void collectMetalShortObsRect(MetalShortObsRectMap& obs_rects, MetalShortObsRectMap& netless_rects, DRCShape* drc_shape);
void buildMetalShortObsPolysets(MetalShortObsPolysetMap& obs_polysets, MetalShortObsRectMap& obs_rects);
void prepareRoutingNet(int32_t net_idx, RVRoutingNet& routing_net, RVLayerData& rv_layer_data,
                       std::vector<std::pair<GTLRectInt, int32_t>>& env_rect_rtree_inputs, bool need_polygon_only);
void buildLayerSpatialIndexes(RVLayerData& rv_layer_data, const std::vector<std::pair<GTLRectInt, int32_t>>& env_rect_rtree_inputs);
void buildMetalShortSpatialIndexes(int32_t layer_idx, RVLayerData& rv_layer_data, MetalShortObsPolysetMap& obs_polysets,
                                   MetalShortObsRectMap& netless_rects);

// Phase-level profiling for prepareRVCluster, printed at exit when ECC_PREP_PROF is set.
struct PrepProf
{
  std::atomic<int64_t> net_calls{0};
  std::atomic<int64_t> in_rects{0};
  std::atomic<int64_t> out_polygons{0};
  std::atomic<int64_t> out_max_rects{0};
  std::atomic<int64_t> out_boundaries{0};
  std::atomic<int64_t> t_insert_ns{0};   // polyset insert of env+result rects
  std::atomic<int64_t> t_delta_ns{0};    // env polyset, env maxrects, delta subtract and delta rtree
  std::atomic<int64_t> t_get_ns{0};      // polyset.get polygon extraction
  std::atomic<int64_t> t_maxrect_ns{0};  // per-polygon maxrect decomposition + env classification
  std::atomic<int64_t> t_boundary_ns{0}; // per-polygon boundary edge collection
  std::atomic<int64_t> t_index_ns{0};    // buildLayerSpatialIndexes
  std::atomic<int64_t> calls_r1{0};       // net calls with 1 input rect
  std::atomic<int64_t> calls_r2_5{0};     // 2..5 rects
  std::atomic<int64_t> calls_r6_20{0};    // 6..20 rects
  std::atomic<int64_t> calls_r21p{0};     // >20 rects
  std::atomic<int64_t> calls_delta{0};    // calls taking the env/result delta path
  std::atomic<int64_t> t_bin_ns{0};       // shape -> layer/net binning loops
  std::atomic<int64_t> t_msindex_ns{0};   // buildMetalShortSpatialIndexes
};

PrepProf& prepProf()
{
  static PrepProf prof;
  return prof;
}

void dumpPrepProf()
{
  const PrepProf& p = prepProf();
  auto ms = [](std::atomic<int64_t> const& v) { return v.load(std::memory_order_relaxed) / 1e6; };
  std::fprintf(stderr,
               "[ECC_PREP_PROF] net_calls=%ld in_rects=%ld out_polygons=%ld out_max_rects=%ld out_boundaries=%ld\n"
               "[ECC_PREP_PROF] ms: insert=%.0f delta=%.0f get=%.0f maxrect=%.0f boundary=%.0f index=%.0f total=%.0f\n",
               p.net_calls.load(), p.in_rects.load(), p.out_polygons.load(), p.out_max_rects.load(), p.out_boundaries.load(),
               ms(p.t_insert_ns), ms(p.t_delta_ns), ms(p.t_get_ns), ms(p.t_maxrect_ns), ms(p.t_boundary_ns), ms(p.t_index_ns),
               ms(p.t_insert_ns) + ms(p.t_delta_ns) + ms(p.t_get_ns) + ms(p.t_maxrect_ns) + ms(p.t_boundary_ns) + ms(p.t_index_ns));
  std::fprintf(stderr, "[ECC_PREP_PROF] call buckets by in_rects: r1=%ld r2_5=%ld r6_20=%ld r21+=%ld delta_path=%ld\n",
               p.calls_r1.load(), p.calls_r2_5.load(), p.calls_r6_20.load(), p.calls_r21p.load(), p.calls_delta.load());
  std::fprintf(stderr, "[ECC_PREP_PROF] ms: binning=%.0f ms_index=%.0f\n", p.t_bin_ns.load() / 1e6, p.t_msindex_ns.load() / 1e6);
}

void prepProfAdd(std::atomic<int64_t>& counter, std::chrono::steady_clock::time_point begin)
{
  auto end = std::chrono::steady_clock::now();
  counter.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count(), std::memory_order_relaxed);
}

struct PrepProfReg
{
  PrepProfReg()
  {
    if (std::getenv("ECC_PREP_PROF") != nullptr) {
      std::atexit(dumpPrepProf);
    }
  }
};

}  // namespace

void RuleValidator::prepareRVCluster(RVCluster& rv_cluster)
{
  const std::set<ViolationType>& check_type_set = *rv_cluster.get_drc_check_type_set();
  const bool need_polygon_only = check_type_set.size() == 1 && check_type_set.contains(ViolationType::kMinimumArea);
  const bool need_metal_short = needVerifying(rv_cluster, ViolationType::kMetalShort);
  std::map<int32_t, RVLayerData>& layer_data = rv_cluster.get_layer_data();
  layer_data.clear();
  MetalShortObsPolysetMap metal_short_obs_polysets;
  MetalShortObsRectMap metal_short_obs_rects;
  MetalShortObsRectMap metal_short_netless_rects;
  auto t_bin = std::chrono::steady_clock::now();
  for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
    addShapeToLayerData(layer_data, drc_shape, true);
    if (need_metal_short) {
      collectMetalShortObsRect(metal_short_obs_rects, metal_short_netless_rects, drc_shape);
    }
  }
  for (DRCShape* drc_shape : rv_cluster.get_drc_result_shape_list()) {
    addShapeToLayerData(layer_data, drc_shape, false);
    if (need_metal_short) {
      collectMetalShortObsRect(metal_short_obs_rects, metal_short_netless_rects, drc_shape);
    }
  }
  prepProfAdd(prepProf().t_bin_ns, t_bin);
  if (need_metal_short) {
    // One sweepline per layer instead of one boolean union per obs shape.
    buildMetalShortObsPolysets(metal_short_obs_polysets, metal_short_obs_rects);
  }

  // Each layer owns flat geometry pools and the indexes that refer to them.
  for (auto& layer_entry : layer_data) {
    RVLayerData& rv_layer_data = layer_entry.second;
    size_t env_rect_count = 0;
    for (const auto& [net_idx, routing_net] : rv_layer_data.nets) {
      (void) net_idx;
      env_rect_count += routing_net.env_rect_list.size();
    }
    std::vector<std::pair<GTLRectInt, int32_t>> env_rect_rtree_inputs;
    env_rect_rtree_inputs.reserve(env_rect_count);
    for (auto& [net_idx, routing_net] : rv_layer_data.nets) {
      prepareRoutingNet(net_idx, routing_net, rv_layer_data, env_rect_rtree_inputs, need_polygon_only);
    }
    if (!need_polygon_only) {
      buildLayerSpatialIndexes(rv_layer_data, env_rect_rtree_inputs);
    }
    if (need_metal_short) {
      auto t_msi = std::chrono::steady_clock::now();
      buildMetalShortSpatialIndexes(layer_entry.first, rv_layer_data, metal_short_obs_polysets, metal_short_netless_rects);
      prepProfAdd(prepProf().t_msindex_ns, t_msi);
    }
  }
}

void RuleValidator::verifyRVCluster(RVCluster& rv_cluster, double* rule_time_list)
{
#define RV_TIMED_RULE(rule_idx, call)                                \
  if (needVerifying(rv_cluster, ViolationType::k##call)) {            \
    if (rule_time_list != nullptr) {                                  \
      double rule_begin = omp_get_wtime();                            \
      verify##call(rv_cluster);                                       \
      rule_time_list[rule_idx] = omp_get_wtime() - rule_begin;        \
    } else {                                                          \
      verify##call(rv_cluster);                                       \
    }                                                                 \
  }
  RV_TIMED_RULE(0, AdjacentCutSpacing)
  RV_TIMED_RULE(1, CornerFillSpacing)
  RV_TIMED_RULE(2, CornerSpacing)
  RV_TIMED_RULE(3, CutEOLSpacing)
  RV_TIMED_RULE(4, CutShort)
  RV_TIMED_RULE(5, DifferentLayerCutSpacing)
  RV_TIMED_RULE(6, Enclosure)
  RV_TIMED_RULE(7, EnclosureEdge)
  RV_TIMED_RULE(8, EnclosureParallel)
  RV_TIMED_RULE(9, EndOfLineSpacing)
  RV_TIMED_RULE(10, FloatingPatch)
  RV_TIMED_RULE(11, JogToJogSpacing)
  RV_TIMED_RULE(12, MaximumWidth)
  RV_TIMED_RULE(13, MaxViaStack)
  RV_TIMED_RULE(14, MetalShort)
  RV_TIMED_RULE(15, MinHole)
  RV_TIMED_RULE(16, MinimumArea)
  RV_TIMED_RULE(17, MinimumCut)
  RV_TIMED_RULE(18, MinimumWidth)
  RV_TIMED_RULE(19, MinStep)
  RV_TIMED_RULE(20, NonsufficientMetalOverlap)
  RV_TIMED_RULE(21, NotchSpacing)
  RV_TIMED_RULE(22, OffGridOrWrongWay)
  RV_TIMED_RULE(23, OutOfDie)
  RV_TIMED_RULE(24, ParallelRunLengthSpacing)
  RV_TIMED_RULE(25, SameLayerCutSpacing)
#undef RV_TIMED_RULE
}

bool RuleValidator::needVerifying(RVCluster& rv_cluster, ViolationType violation_type)
{
  std::set<ViolationType>& exist_rule_set = DRCDM.getDatabase().get_exist_rule_set();

  std::set<ViolationType>* drc_check_type_set = rv_cluster.get_drc_check_type_set();

  if (drc_check_type_set->empty()) {
    return DRCUTIL.exist(exist_rule_set, violation_type);
  } else {
    return (DRCUTIL.exist(*drc_check_type_set, violation_type) && DRCUTIL.exist(exist_rule_set, violation_type));
  }
}

void RuleValidator::processRVCluster(RVCluster& rv_cluster)
{
  std::vector<Violation> new_violation_list;
  new_violation_list.reserve(rv_cluster.get_violation_list().size());
  for (Violation& violation : rv_cluster.get_violation_list()) {
    bool has_overlap = false;
    for (PlanarRect& cluster_rect : rv_cluster.get_cluster_rect_list()) {
      if (DRCUTIL.isOpenOverlap(cluster_rect, violation.get_rect())) {
        has_overlap = true;
        break;
      }
    }
    if (!has_overlap) {
      continue;
    }
    new_violation_list.push_back(std::move(violation));
  }
  std::sort(new_violation_list.begin(), new_violation_list.end(), CmpViolation());
  new_violation_list.erase(std::unique(new_violation_list.begin(), new_violation_list.end()), new_violation_list.end());
  rv_cluster.get_violation_list() = std::move(new_violation_list);
}

void RuleValidator::buildViolationList(RVModel& rv_model)
{
  std::vector<Violation>& violation_list = rv_model.get_violation_list();
  std::vector<RVCluster>& cluster_list = rv_model.get_rv_cluster_list();
  const int64_t cluster_num = static_cast<int64_t>(cluster_list.size());

  // Gather: move each cluster's violations into one global slot range, no copies.
  std::vector<int64_t> cluster_offset_list(cluster_num + 1, 0);
  for (int64_t cluster_idx = 0; cluster_idx < cluster_num; cluster_idx++) {
    cluster_offset_list[cluster_idx + 1] =
        cluster_offset_list[cluster_idx] + static_cast<int64_t>(cluster_list[cluster_idx].get_violation_list().size());
  }
  const int64_t total_violation_num = cluster_offset_list[cluster_num];
  violation_list.resize(total_violation_num);
#pragma omp parallel for schedule(static)
  for (int64_t cluster_idx = 0; cluster_idx < cluster_num; cluster_idx++) {
    std::vector<Violation>& cluster_violation_list = cluster_list[cluster_idx].get_violation_list();
    std::move(cluster_violation_list.begin(), cluster_violation_list.end(), violation_list.begin() + cluster_offset_list[cluster_idx]);
  }

  // Sort: parallel chunk sort + merge tree, moving violations instead of copying them.
  const int32_t thread_num = std::max(omp_get_max_threads(), 1);
  const int64_t chunk_size = std::max<int64_t>(1, (total_violation_num + thread_num - 1) / thread_num);
#pragma omp parallel for schedule(static)
  for (int64_t chunk_begin = 0; chunk_begin < total_violation_num; chunk_begin += chunk_size) {
    std::sort(violation_list.begin() + chunk_begin, violation_list.begin() + std::min(total_violation_num, chunk_begin + chunk_size),
              CmpViolation());
  }
  std::vector<Violation> merge_buffer(total_violation_num);
  std::vector<Violation>* merge_src = &violation_list;
  std::vector<Violation>* merge_dst = &merge_buffer;
  for (int64_t width = chunk_size; width < total_violation_num; width *= 2) {
    int64_t merge_num = (total_violation_num + 2 * width - 1) / (2 * width);
#pragma omp parallel for schedule(static)
    for (int64_t merge_idx = 0; merge_idx < merge_num; merge_idx++) {
      int64_t range_begin = merge_idx * 2 * width;
      int64_t range_mid = std::min(total_violation_num, range_begin + width);
      int64_t range_end = std::min(total_violation_num, range_mid + width);
      std::merge(std::make_move_iterator(merge_src->begin() + range_begin), std::make_move_iterator(merge_src->begin() + range_mid),
                 std::make_move_iterator(merge_src->begin() + range_mid), std::make_move_iterator(merge_src->begin() + range_end),
                 merge_dst->begin() + range_begin, CmpViolation());
    }
    std::swap(merge_src, merge_dst);
  }
  if (merge_src != &violation_list) {
    violation_list.swap(*merge_src);
  }
  violation_list.erase(std::unique(violation_list.begin(), violation_list.end()), violation_list.end());
}

namespace {

using IndexedRect = std::pair<GTLRectInt, int32_t>;
using RectRTree = bgi::rtree<GTLRectInt, bgi::quadratic<16>>;

// Temporary geometry used only while materializing one routing net.
struct NetPrepareContext
{
  bool has_delta_geometry = false;
  RectRTree delta_rect_rtree;
  std::vector<GTLRectInt> delta_overlap_list;
};

Orientation getBoundaryOrient(Rotation rotation, bool is_hole, const PlanarCoord& begin_coord, const PlanarCoord& end_coord)
{
  Orientation travel_orient = DRCUTIL.getOrientation(begin_coord, end_coord);
  bool metal_on_left = (rotation == Rotation::kCounterclockwise);
  if (is_hole) {
    metal_on_left = !metal_on_left;
  }
  switch (travel_orient) {
    case Orientation::kEast:
      return metal_on_left ? Orientation::kSouth : Orientation::kNorth;
    case Orientation::kNorth:
      return metal_on_left ? Orientation::kEast : Orientation::kWest;
    case Orientation::kWest:
      return metal_on_left ? Orientation::kNorth : Orientation::kSouth;
    case Orientation::kSouth:
      return metal_on_left ? Orientation::kWest : Orientation::kEast;
    default:
      return Orientation::kNone;
  }
}

void collectBoundaryEdges(GTLHolePolyInt& check_hole_poly, bool is_hole, int32_t polygon_id, std::vector<BoundaryData>& boundary_pool)
{
  int32_t boundary_begin = static_cast<int32_t>(boundary_pool.size());
  int32_t coord_size = static_cast<int32_t>(check_hole_poly.size());
  if (coord_size < 2) {
    return;
  }

  std::vector<PlanarCoord> coord_list;
  coord_list.reserve(coord_size);
  for (auto iter = check_hole_poly.begin(); iter != check_hole_poly.end(); iter++) {
    coord_list.push_back(DRCUTIL.convertToPlanarCoord(*iter));
  }
  if (coord_list.size() < 2) {
    return;
  }

  Rotation rotation = DRCUTIL.getRotation(check_hole_poly);
  for (int32_t i = 0; i < coord_size; i++) {
    PlanarCoord& pre_coord = coord_list[(i - 1 + coord_size) % coord_size];
    PlanarCoord& curr_coord = coord_list[i];
    if (pre_coord == curr_coord) {
      continue;
    }

    BoundaryData boundary_data;
    boundary_data.edge = DRCUTIL.convertToGTLRectInt(DRCUTIL.getRect(pre_coord, curr_coord));
    boundary_data.begin_coord = pre_coord;
    boundary_data.end_coord = curr_coord;
    boundary_data.orient = getBoundaryOrient(rotation, is_hole, pre_coord, curr_coord);
    boundary_data.polygon_id = polygon_id;
    boundary_data.edge_length = DRCUTIL.getManhattanDistance(pre_coord, curr_coord);
    boundary_data.isHole = is_hole;
    if (coord_size >= 3) {
      PlanarCoord& post_coord = coord_list[(i + 1) % coord_size];
      boundary_data.isConvex = is_hole ? DRCUTIL.isConcaveCorner(rotation, pre_coord, curr_coord, post_coord)
                                       : DRCUTIL.isConvexCorner(rotation, pre_coord, curr_coord, post_coord);
    }

    boundary_pool.push_back(boundary_data);
  }

  int32_t boundary_count = static_cast<int32_t>(boundary_pool.size()) - boundary_begin;
  if (boundary_count < 2) {
    return;
  }
  for (int32_t i = 0; i < boundary_count; i++) {
    BoundaryData& boundary_data = boundary_pool[boundary_begin + i];
    boundary_data.prev_boundary_id = boundary_begin + (i - 1 + boundary_count) % boundary_count;
    boundary_data.next_boundary_id = boundary_begin + (i + 1) % boundary_count;
  }
}

void addShapeToLayerData(std::map<int32_t, RVLayerData>& layer_data, DRCShape* drc_shape, bool is_env_shape)
{
  GTLRectInt gtl_rect = DRCUTIL.convertToGTLRectInt(drc_shape->get_rect());
  RVLayerData& rv_layer_data = layer_data[drc_shape->get_layer_idx()];
  if (!drc_shape->get_is_routing()) {
    rv_layer_data.cut_pool.push_back({gtl_rect, drc_shape->get_net_idx(), is_env_shape, drc_shape->get_source_type()});
    return;
  }

  RVRoutingNet& routing_net = rv_layer_data.nets[drc_shape->get_net_idx()];
  if (is_env_shape) {
    routing_net.env_rect_list.push_back(gtl_rect);
  } else {
    routing_net.result_rect_list.push_back(gtl_rect);
  }
}

void collectMetalShortObsRect(MetalShortObsRectMap& obs_rects, MetalShortObsRectMap& netless_rects, DRCShape* drc_shape)
{
  if (!drc_shape->get_is_routing()) {
    return;
  }
  if (drc_shape->get_is_obs()) {
    obs_rects[drc_shape->get_layer_idx()].push_back(DRCUTIL.convertToGTLRectInt(drc_shape->get_rect()));
  } else if (drc_shape->get_net_idx() == -1) {
    // Non-obs netless shapes belong to the metal index (net -1) in the original semantics,
    // but are mixed with obs in nets[-1], so they are collected separately.
    netless_rects[drc_shape->get_layer_idx()].push_back(DRCUTIL.convertToGTLRectInt(drc_shape->get_rect()));
  }
}

void buildMetalShortObsPolysets(MetalShortObsPolysetMap& obs_polysets, MetalShortObsRectMap& obs_rects)
{
  // Bulk rectangle insert produces the same merged polyset as sequential `+=`,
  // but pays the sweepline setup once per layer instead of once per shape.
  for (auto& [layer_idx, rect_list] : obs_rects) {
    obs_polysets[layer_idx].insert(rect_list.begin(), rect_list.end());
  }
}

void prepareRoutingNet(int32_t net_idx, RVRoutingNet& routing_net, RVLayerData& rv_layer_data,
                       std::vector<std::pair<GTLRectInt, int32_t>>& env_rect_rtree_inputs, bool need_polygon_only)
{
  static PrepProfReg prof_reg;
  PrepProf& prof = prepProf();
  auto t_begin = std::chrono::steady_clock::now();

  NetPrepareContext prepare_context;
  std::vector<GTLRectInt> env_rect_list = std::move(routing_net.env_rect_list);
  std::vector<GTLRectInt> result_rect_list = std::move(routing_net.result_rect_list);
  bool has_env = !env_rect_list.empty();
  bool has_result = !result_rect_list.empty();
  prof.net_calls.fetch_add(1, std::memory_order_relaxed);
  const int64_t in_rect_num = static_cast<int64_t>(env_rect_list.size() + result_rect_list.size());
  prof.in_rects.fetch_add(in_rect_num, std::memory_order_relaxed);
  if (in_rect_num == 1) {
    prof.calls_r1.fetch_add(1, std::memory_order_relaxed);
  } else if (in_rect_num <= 5) {
    prof.calls_r2_5.fetch_add(1, std::memory_order_relaxed);
  } else if (in_rect_num <= 20) {
    prof.calls_r6_20.fetch_add(1, std::memory_order_relaxed);
  } else {
    prof.calls_r21p.fetch_add(1, std::memory_order_relaxed);
  }
  if (has_env && has_result && !need_polygon_only) {
    prof.calls_delta.fetch_add(1, std::memory_order_relaxed);
  }

  routing_net.polyset.insert(env_rect_list.begin(), env_rect_list.end());
  routing_net.polyset.insert(result_rect_list.begin(), result_rect_list.end());
  prepProfAdd(prof.t_insert_ns, t_begin);

  auto t_delta = std::chrono::steady_clock::now();
  GTLPolySetInt env_polyset;
  if (has_env && has_result) {
    env_polyset.insert(env_rect_list.begin(), env_rect_list.end());
  }
  if (has_env && has_result && !need_polygon_only) {
    std::vector<GTLRectInt> env_max_rect_list;
    gtl::get_max_rectangles(env_max_rect_list, env_polyset);
    for (const GTLRectInt& env_max_rect : env_max_rect_list) {
      env_rect_rtree_inputs.emplace_back(env_max_rect, net_idx);
    }
  }

  // result - env equals (env union result) - env without rebuilding result.
  if (has_env && has_result && !need_polygon_only) {
    GTLPolySetInt delta_polyset = routing_net.polyset - env_polyset;
    prepare_context.has_delta_geometry = !gtl::empty(delta_polyset);
    if (prepare_context.has_delta_geometry) {
      std::vector<GTLRectInt> delta_rect_list;
      gtl::get_max_rectangles(delta_rect_list, delta_polyset);
      prepare_context.delta_rect_rtree = RectRTree(delta_rect_list);
    }
  }
  prepProfAdd(prof.t_delta_ns, t_delta);

  // Materialize combined geometry into contiguous layer pools.
  routing_net.polygon_begin = static_cast<int32_t>(rv_layer_data.polygon_pool.size());
  routing_net.max_rect_begin = static_cast<int32_t>(rv_layer_data.max_rect_pool.size());
  routing_net.boundary_begin = static_cast<int32_t>(rv_layer_data.boundary_pool.size());

  auto t_get = std::chrono::steady_clock::now();
  std::vector<GTLHolePolyInt> hole_poly_list;
  routing_net.polyset.get(hole_poly_list);
  prepProfAdd(prof.t_get_ns, t_get);
  prof.out_polygons.fetch_add(static_cast<int64_t>(hole_poly_list.size()), std::memory_order_relaxed);
  for (GTLHolePolyInt& hole_poly : hole_poly_list) {
    int32_t polygon_id = static_cast<int32_t>(rv_layer_data.polygon_pool.size());
    rv_layer_data.polygon_pool.push_back(
        {net_idx, static_cast<int32_t>(rv_layer_data.max_rect_pool.size()), 0, static_cast<int32_t>(rv_layer_data.boundary_pool.size()), 0});
    PolygonData& polygon_data = rv_layer_data.polygon_pool.back();
    polygon_data.hole_poly = std::move(hole_poly);
    GTLHolePolyInt& polygon_hole_poly = polygon_data.hole_poly;
    if (need_polygon_only) {
      // MinimumArea needs whole-polygon provenance, not rectangle or boundary indexes.
      polygon_data.isEnv = has_env;
      if (has_env && has_result) {
        GTLPolySetInt delta_polyset = polygon_hole_poly - env_polyset;
        polygon_data.isEnv = gtl::empty(delta_polyset);
      }
      continue;
    }
    auto t_maxrect = std::chrono::steady_clock::now();
    std::vector<GTLRectInt> rect_list;
    if (polygon_hole_poly.size() == 4 && polygon_hole_poly.begin_holes() == polygon_hole_poly.end_holes()) {
      rect_list.emplace_back();
      gtl::extents(rect_list.back(), polygon_hole_poly);
    } else {
      gtl::get_max_rectangles(rect_list, polygon_hole_poly);
    }
    // A polygon is env only when it is nonempty and every max rectangle decomposed from it is env.
    bool is_polygon_env = has_env && !rect_list.empty();
    for (const GTLRectInt& gtl_rect : rect_list) {
      // A max rectangle is env unless it has an open-area overlap with result-only geometry (result - env).
      bool is_env = has_env;
      if (is_env && prepare_context.has_delta_geometry) {
        prepare_context.delta_overlap_list.clear();
        prepare_context.delta_rect_rtree.query(bgi::intersects(gtl_rect), std::back_inserter(prepare_context.delta_overlap_list));
        PlanarRect max_rect = DRCUTIL.convertToPlanarRect(gtl_rect);
        for (const GTLRectInt& delta_rect : prepare_context.delta_overlap_list) {
          if (DRCUTIL.isOpenOverlap(DRCUTIL.convertToPlanarRect(delta_rect), max_rect)) {
            is_env = false;
            break;
          }
        }
      }
      rv_layer_data.max_rect_pool.push_back({gtl_rect, polygon_id, is_env});
      if (has_env && !has_result) {
        env_rect_rtree_inputs.emplace_back(gtl_rect, net_idx);
      }
      is_polygon_env = is_polygon_env && is_env;
    }
    polygon_data.max_rect_count = static_cast<int32_t>(rv_layer_data.max_rect_pool.size()) - polygon_data.max_rect_begin;
    polygon_data.isEnv = is_polygon_env;
    prepProfAdd(prof.t_maxrect_ns, t_maxrect);
    prof.out_max_rects.fetch_add(polygon_data.max_rect_count, std::memory_order_relaxed);

    auto t_boundary = std::chrono::steady_clock::now();
    collectBoundaryEdges(polygon_hole_poly, false, polygon_id, rv_layer_data.boundary_pool);
    for (auto iter = polygon_hole_poly.begin_holes(); iter != polygon_hole_poly.end_holes(); iter++) {
      GTLPolyInt gtl_poly = *iter;
      GTLHolePolyInt check_hole_poly;
      check_hole_poly.set(gtl_poly.begin(), gtl_poly.end());
      collectBoundaryEdges(check_hole_poly, true, polygon_id, rv_layer_data.boundary_pool);
    }
    polygon_data.boundary_count = static_cast<int32_t>(rv_layer_data.boundary_pool.size()) - polygon_data.boundary_begin;
    prepProfAdd(prof.t_boundary_ns, t_boundary);
    prof.out_boundaries.fetch_add(polygon_data.boundary_count, std::memory_order_relaxed);
  }

  routing_net.polygon_count = static_cast<int32_t>(rv_layer_data.polygon_pool.size()) - routing_net.polygon_begin;
  routing_net.max_rect_count = static_cast<int32_t>(rv_layer_data.max_rect_pool.size()) - routing_net.max_rect_begin;
  routing_net.boundary_count = static_cast<int32_t>(rv_layer_data.boundary_pool.size()) - routing_net.boundary_begin;
}

void buildLayerSpatialIndexes(RVLayerData& rv_layer_data, const std::vector<std::pair<GTLRectInt, int32_t>>& env_rect_rtree_inputs)
{
  auto t_begin = std::chrono::steady_clock::now();
  // Pool IDs are final here, so index inputs can be allocated exactly once.
  std::vector<IndexedRect> rect_inputs;
  rect_inputs.reserve(rv_layer_data.max_rect_pool.size());
  for (size_t i = 0; i < rv_layer_data.max_rect_pool.size(); i++) {
    rect_inputs.emplace_back(rv_layer_data.max_rect_pool[i].rect, static_cast<int32_t>(i));
  }

  std::vector<IndexedRect> boundary_inputs;
  boundary_inputs.reserve(rv_layer_data.boundary_pool.size());
  for (size_t i = 0; i < rv_layer_data.boundary_pool.size(); i++) {
    boundary_inputs.emplace_back(rv_layer_data.boundary_pool[i].edge, static_cast<int32_t>(i));
  }

  rv_layer_data.rect_rtrees = decltype(rv_layer_data.rect_rtrees)(rect_inputs);
  rv_layer_data.env_rect_rtree = decltype(rv_layer_data.env_rect_rtree)(env_rect_rtree_inputs);
  rv_layer_data.boundary_rtrees = decltype(rv_layer_data.boundary_rtrees)(boundary_inputs);
  rv_layer_data.cut_rtrees = decltype(rv_layer_data.cut_rtrees)(rv_layer_data.cut_pool);
  prepProfAdd(prepProf().t_index_ns, t_begin);
}

void buildMetalShortSpatialIndexes(int32_t layer_idx, RVLayerData& rv_layer_data, MetalShortObsPolysetMap& obs_polysets,
                                   MetalShortObsRectMap& netless_rects)
{
  // The metal target index equals the per-net max rectangles already materialized by
  // prepareRoutingNet (same env+result polyset per net); obs (net -1) stays on the obs path.
  std::vector<IndexedRect> metal_rtree_inputs;
  metal_rtree_inputs.reserve(rv_layer_data.max_rect_pool.size());
  for (const auto& [net_idx, routing_net] : rv_layer_data.nets) {
    if (net_idx == -1) {
      continue;
    }
    for (const auto& polygon : rv_layer_data.getPolygons(routing_net)) {
      for (const MaxRectData& max_rect : rv_layer_data.getMaxRects(polygon)) {
        metal_rtree_inputs.emplace_back(max_rect.rect, net_idx);
      }
    }
  }
  // Non-obs netless shapes keep the original merged-polyset semantics under net -1.
  auto netless_it = netless_rects.find(layer_idx);
  if (netless_it != netless_rects.end()) {
    GTLPolySetInt netless_polyset;
    netless_polyset.insert(netless_it->second.begin(), netless_it->second.end());
    std::vector<GTLRectInt> netless_max_rect_list;
    gtl::get_max_rectangles(netless_max_rect_list, netless_polyset);
    for (const GTLRectInt& max_rect : netless_max_rect_list) {
      metal_rtree_inputs.emplace_back(max_rect, -1);
    }
  }
  rv_layer_data.metal_short_metal_rtree = decltype(rv_layer_data.metal_short_metal_rtree)(metal_rtree_inputs);

  std::vector<GTLRectInt> obs_rtree_inputs;
  auto layer_obs_it = obs_polysets.find(layer_idx);
  if (layer_obs_it != obs_polysets.end()) {
    gtl::get_max_rectangles(obs_rtree_inputs, layer_obs_it->second);
  }
  rv_layer_data.metal_short_obs_rtree = decltype(rv_layer_data.metal_short_obs_rtree)(obs_rtree_inputs);
}

}  // namespace

#if 1  // aux

int32_t RuleValidator::getIdx(int32_t idx, int32_t coord_size)
{
  return (idx + coord_size) % coord_size;
}

#endif

#if 1  // debug

void RuleValidator::debugPlotRVModel(RVModel& rv_model, std::string flag)
{
  Die& die = DRCDM.getDatabase().get_die();
  std::string& rv_temp_directory_path = DRCDM.getConfig().rv_temp_directory_path;

  GPGDS gp_gds;

  GPStruct base_region_struct("base_region");
  GPBoundary gp_boundary;
  gp_boundary.set_layer_idx(0);
  gp_boundary.set_data_type(0);
  gp_boundary.set_rect(die);
  base_region_struct.push(gp_boundary);
  gp_gds.addStruct(base_region_struct);

  for (DRCShape& drc_env_shape : rv_model.get_drc_env_shape_list()) {
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

  for (DRCShape& drc_result_shape : rv_model.get_drc_result_shape_list()) {
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

  for (Violation& violation : rv_model.get_violation_list()) {
    std::string net_idx_name = DRCUTIL.getString("net");
    for (int32_t violation_net_idx : violation.get_violation_net_set()) {
      net_idx_name = DRCUTIL.getString(net_idx_name, ",", violation_net_idx);
    }
    GPStruct violation_struct(DRCUTIL.getString("violation(", net_idx_name, ")(rs,", violation.get_required_size(), ")"));
    GPBoundary gp_boundary;
    gp_boundary.set_data_type(static_cast<int32_t>(DRCGP.convertGPDataType(violation.get_violation_type())));
    gp_boundary.set_rect(violation.get_rect());
    if (violation.get_is_routing()) {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByRouting(violation.get_layer_idx()));
    } else {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByCut(violation.get_layer_idx()));
    }
    violation_struct.push(gp_boundary);
    gp_gds.addStruct(violation_struct);
  }

  std::string gds_file_path = DRCUTIL.getString(rv_temp_directory_path, flag, "_rv_model.gds");
  DRCGP.plot(gp_gds, gds_file_path);
}

void RuleValidator::debugPlotRVCluster(RVCluster& rv_cluster, std::string flag)
{
  std::string& rv_temp_directory_path = DRCDM.getConfig().rv_temp_directory_path;

  GPGDS gp_gds;

  GPStruct base_region_struct("base_region");
  for (PlanarRect& cluster_rect : rv_cluster.get_cluster_rect_list()) {
    GPBoundary gp_boundary;
    gp_boundary.set_layer_idx(0);
    gp_boundary.set_data_type(0);
    gp_boundary.set_rect(cluster_rect);
    base_region_struct.push(gp_boundary);
  }
  gp_gds.addStruct(base_region_struct);

  for (DRCShape* drc_env_shape : rv_cluster.get_drc_env_shape_list()) {
    GPStruct drc_env_shape_struct(DRCUTIL.getString("drc_env_shape(net_", drc_env_shape->get_net_idx(), ")"));
    GPBoundary gp_boundary;
    gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kEnvShape));
    gp_boundary.set_rect(drc_env_shape->get_rect());
    if (drc_env_shape->get_is_routing()) {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByRouting(drc_env_shape->get_layer_idx()));
    } else {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByCut(drc_env_shape->get_layer_idx()));
    }
    drc_env_shape_struct.push(gp_boundary);
    gp_gds.addStruct(drc_env_shape_struct);
  }

  for (DRCShape* drc_result_shape : rv_cluster.get_drc_result_shape_list()) {
    GPStruct drc_result_shape_struct(DRCUTIL.getString("drc_result_shape(net_", drc_result_shape->get_net_idx(), ")"));
    GPBoundary gp_boundary;
    gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kResultShape));
    gp_boundary.set_rect(drc_result_shape->get_rect());
    if (drc_result_shape->get_is_routing()) {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByRouting(drc_result_shape->get_layer_idx()));
    } else {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByCut(drc_result_shape->get_layer_idx()));
    }
    drc_result_shape_struct.push(gp_boundary);
    gp_gds.addStruct(drc_result_shape_struct);
  }

  for (Violation& violation : rv_cluster.get_violation_list()) {
    std::string net_idx_name = DRCUTIL.getString("net");
    for (int32_t violation_net_idx : violation.get_violation_net_set()) {
      net_idx_name = DRCUTIL.getString(net_idx_name, ",", violation_net_idx);
    }
    GPStruct violation_struct(DRCUTIL.getString("violation(", net_idx_name, ")(rs,", violation.get_required_size(), ")"));
    GPBoundary gp_boundary;
    gp_boundary.set_data_type(static_cast<int32_t>(DRCGP.convertGPDataType(violation.get_violation_type())));
    gp_boundary.set_rect(violation.get_rect());
    if (violation.get_is_routing()) {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByRouting(violation.get_layer_idx()));
    } else {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByCut(violation.get_layer_idx()));
    }
    violation_struct.push(gp_boundary);
    gp_gds.addStruct(violation_struct);
  }

  std::string gds_file_path = DRCUTIL.getString(rv_temp_directory_path, flag, "_rv_cluster_", rv_cluster.get_cluster_idx(), ".gds");

  DRCGP.plot(gp_gds, gds_file_path);
}

#endif

}  // namespace idrc
