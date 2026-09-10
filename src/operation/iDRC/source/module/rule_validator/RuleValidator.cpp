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

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <string>

#include "DRCHeader.hpp"
#include "GDSPlotter.hpp"
#include "Monitor.hpp"
#include "PlanarRect.hpp"
#include "RVCluster.hpp"
#include "Utility.hpp"

namespace idrc {

namespace {

bool isDisabledEnvValue(const char* env_value)
{
  if (env_value == nullptr) {
    return false;
  }
  std::string value(env_value);
  return value == "0" || value == "false" || value == "FALSE" || value == "off" || value == "OFF" || value == "no" || value == "NO";
}

bool isLoadBalanceEnabled()
{
  return !isDisabledEnvValue(std::getenv("IDRC_ENABLE_LOAD_BALANCE"));
}

bool isLoadBalanceProfileEnabled()
{
  return !isDisabledEnvValue(std::getenv("IDRC_ENABLE_LOAD_BALANCE_PROFILE"));
}

int32_t getLoadBalanceProfileThread()
{
  const char* env_value = std::getenv("IDRC_LOAD_BALANCE_PROFILE_THREAD");
  if (env_value == nullptr) {
    env_value = std::getenv("IDRC_PROFILE_THREAD");
  }
  if (env_value == nullptr) {
    return 8;
  }
  int32_t profile_thread = std::atoi(env_value);
  return profile_thread > 0 ? profile_thread : 8;
}

bool shouldOutputLoadBalanceProfile()
{
  return isLoadBalanceProfileEnabled() && DRCDM.getConfig().thread_number == getLoadBalanceProfileThread();
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
  buildRVClusterList(rv_model);
  if (isLoadBalanceEnabled()) {
    loadBalance(rv_model, rv_model.get_grid_col_num(), rv_model.get_grid_row_num());
    if (shouldOutputLoadBalanceProfile()) {
      exportClusterProfileData(rv_model);
      reportGroupStatistics(rv_model);
    }
  } else {
    rv_model.set_rv_cluster_group_list({});
    DRCLOG.info(Loc::current(), "loadBalance disabled by IDRC_ENABLE_LOAD_BALANCE");
  }
  verifyRVModel(rv_model);
  buildViolationList(rv_model);
  // debugPlotRVModel(rv_model, "best");
  DRCLOG.info(Loc::current(), "Completed", monitor ? monitor->getStatsInfo() : "");
  return std::move(rv_model.get_violation_list());
}

// private

RuleValidator* RuleValidator::_rv_instance = nullptr;

void RuleValidator::setRVComParam(RVModel& rv_model)
{
  int32_t only_pitch = DRCDM.getOnlyPitch();
  int32_t cluster_size = 100 * only_pitch;
  int32_t expand_size = 5 * only_pitch;
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
      for (DRCShape& drc_env_shape : rv_model.get_drc_env_shape_list()) {
        bounding_box.set_ll_x(std::min(bounding_box.get_ll_x(), drc_env_shape.get_ll_x()));
        bounding_box.set_ll_y(std::min(bounding_box.get_ll_y(), drc_env_shape.get_ll_y()));
        bounding_box.set_ur_x(std::max(bounding_box.get_ur_x(), drc_env_shape.get_ur_x()));
        bounding_box.set_ur_y(std::max(bounding_box.get_ur_y(), drc_env_shape.get_ur_y()));
      }
      for (DRCShape& drc_result_shape : rv_model.get_drc_result_shape_list()) {
        bounding_box.set_ll_x(std::min(bounding_box.get_ll_x(), drc_result_shape.get_ll_x()));
        bounding_box.set_ll_y(std::min(bounding_box.get_ll_y(), drc_result_shape.get_ll_y()));
        bounding_box.set_ur_x(std::max(bounding_box.get_ur_x(), drc_result_shape.get_ur_x()));
        bounding_box.set_ur_y(std::max(bounding_box.get_ur_y(), drc_result_shape.get_ur_y()));
      }
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
    rv_model.set_grid_col_num(grid_x_size);
    rv_model.set_grid_row_num(grid_y_size);
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
  for (DRCShape& drc_env_shape : rv_model.get_drc_env_shape_list()) {
    PlanarRect searched_rect = DRCUTIL.getEnlargedRect(drc_env_shape.get_rect(), expand_size);
    if (!DRCUTIL.isClosedOverlap(searched_rect, bounding_box)) {
      continue;
    }
    searched_rect = DRCUTIL.getRegularRect(searched_rect, bounding_box);
    int32_t grid_ll_x = (searched_rect.get_ll_x() - offset_x) / cluster_size;
    int32_t grid_ll_y = (searched_rect.get_ll_y() - offset_y) / cluster_size;
    int32_t grid_ur_x = (searched_rect.get_ur_x() - offset_x) / cluster_size;
    int32_t grid_ur_y = (searched_rect.get_ur_y() - offset_y) / cluster_size;
    for (int32_t grid_x = grid_ll_x; grid_x <= grid_ur_x; grid_x++) {
      for (int32_t grid_y = grid_ll_y; grid_y <= grid_ur_y; grid_y++) {
        int32_t cluster_idx = grid_x + grid_y * grid_x_size;
        if (static_cast<int32_t>(rv_cluster_list.size()) <= cluster_idx) {
          DRCLOG.error(Loc::current(), "rv_cluster_list.size() <= cluster_idx!");
        }
        rv_cluster_list[cluster_idx].get_drc_env_shape_list().push_back(&drc_env_shape);
      }
    }
  }
  for (DRCShape& drc_result_shape : rv_model.get_drc_result_shape_list()) {
    PlanarRect searched_rect = DRCUTIL.getEnlargedRect(drc_result_shape.get_rect(), expand_size);
    if (!DRCUTIL.isClosedOverlap(searched_rect, bounding_box)) {
      continue;
    }
    searched_rect = DRCUTIL.getRegularRect(searched_rect, bounding_box);
    int32_t grid_ll_x = (searched_rect.get_ll_x() - offset_x) / cluster_size;
    int32_t grid_ll_y = (searched_rect.get_ll_y() - offset_y) / cluster_size;
    int32_t grid_ur_x = (searched_rect.get_ur_x() - offset_x) / cluster_size;
    int32_t grid_ur_y = (searched_rect.get_ur_y() - offset_y) / cluster_size;
    for (int32_t grid_x = grid_ll_x; grid_x <= grid_ur_x; grid_x++) {
      for (int32_t grid_y = grid_ll_y; grid_y <= grid_ur_y; grid_y++) {
        int32_t cluster_idx = grid_x + grid_y * grid_x_size;
        if (static_cast<int32_t>(rv_cluster_list.size()) <= cluster_idx) {
          DRCLOG.error(Loc::current(), "rv_cluster_list.size() <= cluster_idx!");
        }
        rv_cluster_list[cluster_idx].get_drc_result_shape_list().push_back(&drc_result_shape);
      }
    }
  }
  for (RVCluster& rv_cluster : rv_cluster_list) {
    rv_cluster.set_drc_check_type_set(&rv_model.get_drc_check_type_set());
    rv_cluster.set_drc_check_region_list(&rv_model.get_drc_check_region_list());
  }
  for (DRCShape& drc_result_shape : rv_model.get_drc_result_shape_list()) {
    if (drc_result_shape.get_net_idx() < 0) {
      DRCLOG.error(Loc::current(), "The drc_result_shape_list exist idx < 0!");
    }
  }
}

void RuleValidator::loadBalance(RVModel& rv_model, int32_t grid_col_num, int32_t grid_row_num)
{
  std::vector<RVCluster>& rv_cluster_list = rv_model.get_rv_cluster_list();
  int32_t cluster_num = static_cast<int32_t>(rv_cluster_list.size());
  if (cluster_num <= 0 || grid_col_num <= 0 || grid_row_num <= 0) {
    rv_model.set_rv_cluster_group_list({});
    DRCLOG.info(Loc::current(), "loadBalance skipped: no clusters");
    return;
  }

  std::vector<int32_t> shape_count_list;
  shape_count_list.reserve(cluster_num);
  for (RVCluster& rv_cluster : rv_cluster_list) {
    shape_count_list.push_back(static_cast<int32_t>(rv_cluster.get_drc_env_shape_list().size() + rv_cluster.get_drc_result_shape_list().size()));
  }

  int32_t target_group_num = getTargetGroupNum(cluster_num);
  std::vector<std::vector<int32_t>> cluster_group_list =
      buildClusterGroupList(shape_count_list, grid_col_num, grid_row_num, target_group_num);
  rv_model.set_rv_cluster_group_list(cluster_group_list);
  DRCLOG.info(Loc::current(), "loadBalance completed: cluster_num=", cluster_num, ", target_group_num=", target_group_num,
              ", actual_group_num=", static_cast<int32_t>(cluster_group_list.size()));
}

std::vector<std::vector<int32_t>> RuleValidator::buildClusterGroupList(const std::vector<int32_t>& shape_count_list, int32_t grid_col_num,
                                                                        int32_t grid_row_num, int32_t target_group_num)
{
  int32_t cluster_num = static_cast<int32_t>(shape_count_list.size());
  if (cluster_num <= 0 || grid_col_num <= 0 || grid_row_num <= 0) {
    return {};
  }

  target_group_num = std::max(1, std::min(target_group_num, cluster_num));
  int64_t total_shape_count = std::accumulate(shape_count_list.begin(), shape_count_list.end(), int64_t{0});
  double avg_shape_count = static_cast<double>(total_shape_count) / target_group_num;
  std::vector<std::vector<int32_t>> group_list;
  std::vector<std::pair<int32_t, int32_t>> group_info_list;
  group_list.reserve(cluster_num);
  group_info_list.reserve(cluster_num);
  for (int32_t cluster_idx = 0; cluster_idx < cluster_num; cluster_idx++) {
    group_list.push_back({cluster_idx});
    group_info_list.emplace_back(shape_count_list[cluster_idx], cluster_idx);
  }
  mergeToTargetGroupNum(group_list, group_info_list, target_group_num, avg_shape_count, grid_col_num, grid_row_num);

  std::vector<std::vector<int32_t>> cluster_group_list;
  cluster_group_list.reserve(target_group_num);
  for (std::vector<int32_t>& group : group_list) {
    if (!group.empty()) {
      cluster_group_list.push_back(group);
    }
  }
  return cluster_group_list;
}

int32_t RuleValidator::getTargetGroupNum(int32_t cluster_num)
{
  if (cluster_num <= 0) {
    return 0;
  }

  int32_t thread_num = std::max(DRCDM.getConfig().thread_number, 1);
  int32_t target_group_num = std::max(std::max(1, cluster_num / 4), std::min(cluster_num, thread_num * 16));
  int32_t power = 1;
  while (power < target_group_num) {
    power *= 2;
  }
  if (power > 1 && (power - target_group_num) > (target_group_num - power / 2)) {
    power /= 2;
  }
  return std::max(1, std::min(power, cluster_num));
}

void RuleValidator::mergeToTargetGroupNum(std::vector<std::vector<int32_t>>& group_list,
                                          std::vector<std::pair<int32_t, int32_t>>& group_info_list, int32_t target_group_num,
                                          double avg_shape_count, int32_t grid_col_num, int32_t grid_row_num)
{
  int32_t group_num = static_cast<int32_t>(group_list.size());
  target_group_num = std::max(1, std::min(target_group_num, group_num));
  if (group_num <= target_group_num || grid_col_num <= 0 || grid_row_num <= 0) {
    return;
  }

  struct GroupHeapNode
  {
    int32_t shape_count = 0;
    int32_t group_idx = -1;
    int32_t version = 0;
  };
  struct CompareGroupHeapNode
  {
    bool operator()(const GroupHeapNode& lhs, const GroupHeapNode& rhs) const
    {
      return lhs.shape_count == rhs.shape_count ? lhs.group_idx > rhs.group_idx : lhs.shape_count > rhs.shape_count;
    }
  };

  std::vector<int32_t> group_shape_count_list(group_num, 0);
  for (const auto& group_info : group_info_list) {
    if (group_info.second >= 0 && group_info.second < group_num) {
      group_shape_count_list[group_info.second] = group_info.first;
    }
  }
  std::vector<int32_t> cluster_to_group_list(grid_col_num * grid_row_num, -1);
  std::vector<bool> active_group_list(group_num, false);
  std::vector<int32_t> group_version_list(group_num, 0);
  std::priority_queue<GroupHeapNode, std::vector<GroupHeapNode>, CompareGroupHeapNode> group_heap;
  int32_t active_group_num = 0;
  for (int32_t group_idx = 0; group_idx < group_num; group_idx++) {
    active_group_list[group_idx] = true;
    active_group_num++;
    group_heap.push({group_shape_count_list[group_idx], group_idx, group_version_list[group_idx]});
    cluster_to_group_list[group_idx] = group_idx;
  }

  while (active_group_num > target_group_num && !group_heap.empty()) {
    GroupHeapNode current_node = group_heap.top();
    group_heap.pop();
    int32_t group_idx = current_node.group_idx;
    if (group_idx < 0 || group_idx >= group_num || !active_group_list[group_idx] || current_node.version != group_version_list[group_idx]) {
      continue;
    }

    int32_t best_neighbor_group_idx = -1;
    double best_deviation = std::numeric_limits<double>::max();
    int32_t best_neighbor_shape_count = std::numeric_limits<int32_t>::max();
    for (int32_t cluster_idx : group_list[group_idx]) {
      for (int32_t neighbor_idx : getNeighborIdxList(cluster_idx, grid_col_num, grid_row_num)) {
        int32_t neighbor_group_idx = cluster_to_group_list[neighbor_idx];
        if (neighbor_group_idx < 0 || neighbor_group_idx == group_idx || !active_group_list[neighbor_group_idx]) {
          continue;
        }
        int32_t neighbor_shape_count = group_shape_count_list[neighbor_group_idx];
        double deviation = std::abs(static_cast<double>(group_shape_count_list[group_idx] + neighbor_shape_count) - avg_shape_count);
        if (deviation < best_deviation
            || (deviation == best_deviation
                && (neighbor_shape_count < best_neighbor_shape_count || neighbor_group_idx < best_neighbor_group_idx))) {
          best_deviation = deviation;
          best_neighbor_group_idx = neighbor_group_idx;
          best_neighbor_shape_count = neighbor_shape_count;
        }
      }
    }
    if (best_neighbor_group_idx < 0) {
      continue;
    }

    group_list[group_idx].insert(group_list[group_idx].end(), group_list[best_neighbor_group_idx].begin(),
                                 group_list[best_neighbor_group_idx].end());
    for (int32_t cluster_idx : group_list[best_neighbor_group_idx]) {
      cluster_to_group_list[cluster_idx] = group_idx;
    }
    group_list[best_neighbor_group_idx].clear();
    active_group_list[best_neighbor_group_idx] = false;
    active_group_num--;
    group_shape_count_list[group_idx] += group_shape_count_list[best_neighbor_group_idx];
    group_shape_count_list[best_neighbor_group_idx] = 0;
    group_version_list[group_idx]++;
    group_version_list[best_neighbor_group_idx]++;
    group_heap.push({group_shape_count_list[group_idx], group_idx, group_version_list[group_idx]});
  }

  group_info_list.clear();
  for (int32_t group_idx = 0; group_idx < group_num; group_idx++) {
    if (active_group_list[group_idx] && !group_list[group_idx].empty()) {
      group_info_list.emplace_back(group_shape_count_list[group_idx], group_idx);
    }
  }
}

std::vector<int32_t> RuleValidator::getNeighborIdxList(int32_t cluster_idx, int32_t grid_col_num, int32_t grid_row_num)
{
  std::vector<int32_t> neighbor_idx_list;
  if (cluster_idx < 0 || grid_col_num <= 0 || grid_row_num <= 0) {
    return neighbor_idx_list;
  }

  int32_t grid_x = cluster_idx % grid_col_num;
  int32_t grid_y = cluster_idx / grid_col_num;
  if (grid_y < 0 || grid_y >= grid_row_num) {
    return neighbor_idx_list;
  }
  if (grid_x < grid_col_num - 1) {
    neighbor_idx_list.push_back(cluster_idx + 1);
  }
  if (grid_x > 0) {
    neighbor_idx_list.push_back(cluster_idx - 1);
  }
  if (grid_y < grid_row_num - 1) {
    neighbor_idx_list.push_back(cluster_idx + grid_col_num);
  }
  if (grid_y > 0) {
    neighbor_idx_list.push_back(cluster_idx - grid_col_num);
  }
  return neighbor_idx_list;
}

int32_t RuleValidator::getUniqueShapeCount(std::vector<int32_t>& cluster_idx_list, std::vector<RVCluster>& rv_cluster_list)
{
  std::set<DRCShape*> unique_shape_set;
  for (int32_t cluster_idx : cluster_idx_list) {
    if (cluster_idx < 0 || cluster_idx >= static_cast<int32_t>(rv_cluster_list.size())) {
      continue;
    }
    RVCluster& rv_cluster = rv_cluster_list[cluster_idx];
    unique_shape_set.insert(rv_cluster.get_drc_env_shape_list().begin(), rv_cluster.get_drc_env_shape_list().end());
    unique_shape_set.insert(rv_cluster.get_drc_result_shape_list().begin(), rv_cluster.get_drc_result_shape_list().end());
  }
  return static_cast<int32_t>(unique_shape_set.size());
}

void RuleValidator::reportGroupStatistics(RVModel& rv_model)
{
  std::vector<RVCluster>& rv_cluster_list = rv_model.get_rv_cluster_list();
  std::vector<std::vector<int32_t>>& cluster_group_list = rv_model.get_rv_cluster_group_list();
  if (cluster_group_list.empty()) {
    return;
  }

  int64_t total_shape_count = 0;
  for (RVCluster& rv_cluster : rv_cluster_list) {
    total_shape_count += static_cast<int64_t>(rv_cluster.get_drc_env_shape_list().size() + rv_cluster.get_drc_result_shape_list().size());
  }
  int32_t group_num = static_cast<int32_t>(cluster_group_list.size());
  double ideal_avg = static_cast<double>(total_shape_count) / group_num;
  double variance = 0.0;
  int32_t min_count = std::numeric_limits<int32_t>::max();
  int32_t max_count = 0;
  for (const std::vector<int32_t>& cluster_idx_list : cluster_group_list) {
    int32_t group_shape_count = 0;
    for (int32_t cluster_idx : cluster_idx_list) {
      if (cluster_idx >= 0 && cluster_idx < static_cast<int32_t>(rv_cluster_list.size())) {
        RVCluster& rv_cluster = rv_cluster_list[cluster_idx];
        group_shape_count += static_cast<int32_t>(rv_cluster.get_drc_env_shape_list().size() + rv_cluster.get_drc_result_shape_list().size());
      }
    }
    min_count = std::min(min_count, group_shape_count);
    max_count = std::max(max_count, group_shape_count);
    double diff = static_cast<double>(group_shape_count) - ideal_avg;
    variance += diff * diff;
  }
  variance /= group_num;
  double cv = ideal_avg > 0.0 ? (std::sqrt(variance) / ideal_avg) * 100.0 : 0.0;
  DRCLOG.info(Loc::current(), "Load Balance Group Statistics: groups=", group_num, ", total_shapes=", total_shape_count,
              ", min=", min_count, ", max=", max_count, ", cv=", cv, "%");
}

void RuleValidator::exportClusterProfileData(RVModel& rv_model)
{
  const std::string& temp_directory_path = DRCDM.getConfig().rv_temp_directory_path;
  std::vector<RVCluster>& rv_cluster_list = rv_model.get_rv_cluster_list();
  std::vector<std::vector<int32_t>>& cluster_group_list = rv_model.get_rv_cluster_group_list();
  int32_t grid_col_num = rv_model.get_grid_col_num();
  int32_t grid_row_num = rv_model.get_grid_row_num();
  if (rv_cluster_list.empty() || cluster_group_list.empty() || grid_col_num <= 0 || grid_row_num <= 0) {
    return;
  }

  int32_t total_cluster_num = grid_col_num * grid_row_num;
  std::ofstream before_file(temp_directory_path + "cluster_before.csv");
  if (before_file.is_open()) {
    before_file << "cluster_idx,grid_x,grid_y,shape_count\n";
    for (int32_t cluster_idx = 0; cluster_idx < total_cluster_num; cluster_idx++) {
      int32_t shape_count = 0;
      if (cluster_idx < static_cast<int32_t>(rv_cluster_list.size())) {
        RVCluster& rv_cluster = rv_cluster_list[cluster_idx];
        shape_count = static_cast<int32_t>(rv_cluster.get_drc_env_shape_list().size() + rv_cluster.get_drc_result_shape_list().size());
      }
      before_file << cluster_idx << "," << cluster_idx % grid_col_num << "," << cluster_idx / grid_col_num << "," << shape_count << "\n";
    }
  }

  std::vector<int32_t> cluster_to_group_list(total_cluster_num, -1);
  std::vector<int32_t> group_shape_count_list(cluster_group_list.size(), 0);
  for (size_t group_idx = 0; group_idx < cluster_group_list.size(); group_idx++) {
    std::vector<int32_t>& cluster_idx_list = cluster_group_list[group_idx];
    for (int32_t cluster_idx : cluster_idx_list) {
      if (cluster_idx >= 0 && cluster_idx < total_cluster_num) {
        cluster_to_group_list[cluster_idx] = static_cast<int32_t>(group_idx);
      }
    }
    group_shape_count_list[group_idx] = getUniqueShapeCount(cluster_idx_list, rv_cluster_list);
  }

  std::ofstream after_file(temp_directory_path + "cluster_after.csv");
  if (after_file.is_open()) {
    after_file << "cluster_idx,grid_x,grid_y,group_id,group_shape_count\n";
    for (int32_t cluster_idx = 0; cluster_idx < total_cluster_num; cluster_idx++) {
      int32_t group_idx = cluster_to_group_list[cluster_idx];
      int32_t group_shape_count = group_idx >= 0 ? group_shape_count_list[group_idx] : 0;
      after_file << cluster_idx << "," << cluster_idx % grid_col_num << "," << cluster_idx / grid_col_num << "," << group_idx << ","
                 << group_shape_count << "\n";
    }
  }
}

void RuleValidator::verifyRVModel(RVModel& rv_model)
{
  auto monitor = Monitor::create();
  DRCLOG.info(Loc::current(), "Starting...");
  std::vector<RVCluster>& rv_cluster_list = rv_model.get_rv_cluster_list();
  std::vector<std::vector<int32_t>>& cluster_group_list = rv_model.get_rv_cluster_group_list();
  bool output_runtime_profile = shouldOutputLoadBalanceProfile();
  std::vector<double> cluster_runtime_list(output_runtime_profile ? rv_cluster_list.size() : 0, 0.0);
  std::vector<int32_t> cluster_env_shape_count_list(output_runtime_profile ? rv_cluster_list.size() : 0, 0);
  std::vector<int32_t> cluster_result_shape_count_list(output_runtime_profile ? rv_cluster_list.size() : 0, 0);
  auto write_cluster_info = [&]() {
    if (!output_runtime_profile) {
      return;
    }
    std::ofstream cluster_csv_file(DRCDM.getConfig().rv_temp_directory_path + "cluster_info.csv");
    if (!cluster_csv_file.is_open()) {
      DRCLOG.warn(Loc::current(), "Failed to open cluster_info.csv");
      return;
    }
    cluster_csv_file << "cluster_idx,env_shape_count,result_shape_count,runtime_seconds\n";
    for (size_t cluster_idx = 0; cluster_idx < rv_cluster_list.size(); cluster_idx++) {
      cluster_csv_file << cluster_idx << "," << cluster_env_shape_count_list[cluster_idx] << ","
                       << cluster_result_shape_count_list[cluster_idx] << "," << std::fixed << std::setprecision(6)
                       << cluster_runtime_list[cluster_idx] << "\n";
    }
  };
  auto write_group_info = [&](const std::vector<double>& group_runtime_list) {
    if (!output_runtime_profile || cluster_group_list.empty()) {
      return;
    }
    std::ofstream group_csv_file(DRCDM.getConfig().rv_temp_directory_path + "group_info.csv");
    if (!group_csv_file.is_open()) {
      DRCLOG.warn(Loc::current(), "Failed to open group_info.csv");
      return;
    }
    group_csv_file << "group_idx,cluster_count,shape_count,runtime_seconds\n";
    for (size_t group_idx = 0; group_idx < cluster_group_list.size(); group_idx++) {
      int32_t cluster_count = 0;
      int32_t shape_count = 0;
      double runtime = group_runtime_list.empty() ? 0.0 : group_runtime_list[group_idx];
      for (int32_t cluster_idx : cluster_group_list[group_idx]) {
        if (cluster_idx < 0 || cluster_idx >= static_cast<int32_t>(rv_cluster_list.size())) {
          continue;
        }
        cluster_count++;
        shape_count += cluster_env_shape_count_list[cluster_idx] + cluster_result_shape_count_list[cluster_idx];
        if (group_runtime_list.empty()) {
          runtime += cluster_runtime_list[cluster_idx];
        }
      }
      group_csv_file << group_idx << "," << cluster_count << "," << shape_count << "," << std::fixed << std::setprecision(6) << runtime << "\n";
    }
  };
  bool use_group_scheduling = !cluster_group_list.empty();
  if (use_group_scheduling && cluster_group_list.size() == rv_cluster_list.size()) {
    std::vector<bool> visited_cluster_list(rv_cluster_list.size(), false);
    int32_t visited_cluster_num = 0;
    for (const std::vector<int32_t>& cluster_idx_list : cluster_group_list) {
      if (cluster_idx_list.size() != 1) {
        break;
      }
      int32_t cluster_idx = cluster_idx_list.front();
      if (cluster_idx < 0 || cluster_idx >= static_cast<int32_t>(rv_cluster_list.size()) || visited_cluster_list[cluster_idx]) {
        break;
      }
      visited_cluster_list[cluster_idx] = true;
      visited_cluster_num++;
    }
    if (visited_cluster_num == static_cast<int32_t>(rv_cluster_list.size())) {
      use_group_scheduling = false;
      DRCLOG.info(Loc::current(), "loadBalance group scheduling skipped: one cluster per group");
    }
  }

  if (use_group_scheduling) {
    std::vector<double> group_runtime_list(output_runtime_profile ? cluster_group_list.size() : 0, 0.0);
#pragma omp parallel for schedule(dynamic)
    for (int32_t group_idx = 0; group_idx < static_cast<int32_t>(cluster_group_list.size()); group_idx++) {
      std::chrono::high_resolution_clock::time_point group_start_time;
      if (output_runtime_profile) {
        group_start_time = std::chrono::high_resolution_clock::now();
      }
      for (int32_t cluster_idx : cluster_group_list[group_idx]) {
        if (cluster_idx < 0 || cluster_idx >= static_cast<int32_t>(rv_cluster_list.size())) {
          continue;
        }
        RVCluster& rv_cluster = rv_cluster_list[cluster_idx];
        std::chrono::high_resolution_clock::time_point cluster_start_time;
        if (output_runtime_profile) {
          cluster_start_time = std::chrono::high_resolution_clock::now();
        }
        buildRVCluster(rv_cluster);
        if (needVerifying(rv_cluster)) {
          buildViolationList(rv_cluster);
        }
        if (output_runtime_profile) {
          cluster_runtime_list[cluster_idx] = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - cluster_start_time).count();
          cluster_env_shape_count_list[cluster_idx] = static_cast<int32_t>(rv_cluster.get_drc_env_shape_list().size());
          cluster_result_shape_count_list[cluster_idx] = static_cast<int32_t>(rv_cluster.get_drc_result_shape_list().size());
        }
      }
      if (output_runtime_profile) {
        group_runtime_list[group_idx] = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - group_start_time).count();
      }
    }
    write_cluster_info();
    write_group_info(group_runtime_list);
    DRCLOG.info(Loc::current(), "Completed", monitor ? monitor->getStatsInfo() : "");
    return;
  }

#pragma omp parallel for schedule(dynamic)
  for (size_t cluster_idx = 0; cluster_idx < rv_cluster_list.size(); cluster_idx++) {
    RVCluster& rv_cluster = rv_cluster_list[cluster_idx];
    std::chrono::high_resolution_clock::time_point cluster_start_time;
    if (output_runtime_profile) {
      cluster_start_time = std::chrono::high_resolution_clock::now();
    }
    buildRVCluster(rv_cluster);
    if (needVerifying(rv_cluster)) {
      buildViolationList(rv_cluster);
    }
    if (output_runtime_profile) {
      cluster_runtime_list[cluster_idx] = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - cluster_start_time).count();
      cluster_env_shape_count_list[cluster_idx] = static_cast<int32_t>(rv_cluster.get_drc_env_shape_list().size());
      cluster_result_shape_count_list[cluster_idx] = static_cast<int32_t>(rv_cluster.get_drc_result_shape_list().size());
    }
  }
  write_cluster_info();
  write_group_info({});
  DRCLOG.info(Loc::current(), "Completed", monitor ? monitor->getStatsInfo() : "");
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

using MetalShortNetPolysetMap = std::map<int32_t, std::map<int32_t, GTLPolySetInt>>;
using MetalShortObsPolysetMap = std::map<int32_t, GTLPolySetInt>;

void addShapeToLayerData(std::map<int32_t, RVLayerData>& layer_data, DRCShape* drc_shape, bool is_env_shape);
void addShapeToMetalShortData(MetalShortNetPolysetMap& metal_polysets, MetalShortObsPolysetMap& obs_polysets, DRCShape* drc_shape);
void prepareRoutingNet(int32_t net_idx, RVRoutingNet& routing_net, RVLayerData& rv_layer_data,
                       std::vector<std::pair<GTLRectInt, int32_t>>& env_rect_rtree_inputs, bool need_polygon_only);
void buildLayerSpatialIndexes(RVLayerData& rv_layer_data, const std::vector<std::pair<GTLRectInt, int32_t>>& env_rect_rtree_inputs);
void buildMetalShortSpatialIndexes(int32_t layer_idx, RVLayerData& rv_layer_data, MetalShortNetPolysetMap& metal_polysets,
                                   MetalShortObsPolysetMap& obs_polysets);

}  // namespace

void RuleValidator::prepareRVCluster(RVCluster& rv_cluster)
{
  const std::set<ViolationType>& check_type_set = *rv_cluster.get_drc_check_type_set();
  const bool need_polygon_only = check_type_set.size() == 1 && check_type_set.contains(ViolationType::kMinimumArea);
  const bool need_metal_short = needVerifying(rv_cluster, ViolationType::kMetalShort);
  std::map<int32_t, RVLayerData>& layer_data = rv_cluster.get_layer_data();
  layer_data.clear();
  MetalShortNetPolysetMap metal_short_metal_polysets;
  MetalShortObsPolysetMap metal_short_obs_polysets;
  for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
    addShapeToLayerData(layer_data, drc_shape, true);
    if (need_metal_short) {
      addShapeToMetalShortData(metal_short_metal_polysets, metal_short_obs_polysets, drc_shape);
    }
  }
  for (DRCShape* drc_shape : rv_cluster.get_drc_result_shape_list()) {
    addShapeToLayerData(layer_data, drc_shape, false);
    if (need_metal_short) {
      addShapeToMetalShortData(metal_short_metal_polysets, metal_short_obs_polysets, drc_shape);
    }
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
      buildMetalShortSpatialIndexes(layer_entry.first, rv_layer_data, metal_short_metal_polysets, metal_short_obs_polysets);
    }
  }
}

void RuleValidator::verifyRVCluster(RVCluster& rv_cluster)
{
  if (needVerifying(rv_cluster, ViolationType::kAdjacentCutSpacing)) {
    verifyAdjacentCutSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kCornerFillSpacing)) {
    verifyCornerFillSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kCornerSpacing)) {
    verifyCornerSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kCutEOLSpacing)) {
    verifyCutEOLSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kCutShort)) {
    verifyCutShort(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kDifferentLayerCutSpacing)) {
    verifyDifferentLayerCutSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kEnclosure)) {
    verifyEnclosure(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kEnclosureEdge)) {
    verifyEnclosureEdge(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kEnclosureParallel)) {
    verifyEnclosureParallel(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kEndOfLineSpacing)) {
    verifyEndOfLineSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kFloatingPatch)) {
    verifyFloatingPatch(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kJogToJogSpacing)) {
    verifyJogToJogSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kMaximumWidth)) {
    verifyMaximumWidth(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kMaxViaStack)) {
    verifyMaxViaStack(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kMetalShort)) {
    verifyMetalShort(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kMinHole)) {
    verifyMinHole(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kMinimumArea)) {
    verifyMinimumArea(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kMinimumCut)) {
    verifyMinimumCut(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kMinimumWidth)) {
    verifyMinimumWidth(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kMinStep)) {
    verifyMinStep(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kNonsufficientMetalOverlap)) {
    verifyNonsufficientMetalOverlap(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kNotchSpacing)) {
    verifyNotchSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kOffGridOrWrongWay)) {
    verifyOffGridOrWrongWay(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kOutOfDie)) {
    verifyOutOfDie(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kParallelRunLengthSpacing)) {
    verifyParallelRunLengthSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kSameLayerCutSpacing)) {
    verifySameLayerCutSpacing(rv_cluster);
  }
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
    new_violation_list.push_back(violation);
  }
  std::sort(new_violation_list.begin(), new_violation_list.end(), CmpViolation());
  new_violation_list.erase(std::unique(new_violation_list.begin(), new_violation_list.end()), new_violation_list.end());
  rv_cluster.set_violation_list(new_violation_list);
}

void RuleValidator::buildViolationList(RVModel& rv_model)
{
  std::vector<Violation>& violation_list = rv_model.get_violation_list();
  for (RVCluster& rv_cluster : rv_model.get_rv_cluster_list()) {
    for (Violation& violation : rv_cluster.get_violation_list()) {
      violation_list.push_back(violation);
    }
  }
  std::sort(violation_list.begin(), violation_list.end(), CmpViolation());
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

void addShapeToMetalShortData(MetalShortNetPolysetMap& metal_polysets, MetalShortObsPolysetMap& obs_polysets, DRCShape* drc_shape)
{
  if (!drc_shape->get_is_routing()) {
    return;
  }
  GTLRectInt rect = DRCUTIL.convertToGTLRectInt(drc_shape->get_rect());
  if (drc_shape->get_is_obs()) {
    obs_polysets[drc_shape->get_layer_idx()] += rect;
  } else {
    metal_polysets[drc_shape->get_layer_idx()][drc_shape->get_net_idx()] += rect;
  }
}

void prepareRoutingNet(int32_t net_idx, RVRoutingNet& routing_net, RVLayerData& rv_layer_data,
                       std::vector<std::pair<GTLRectInt, int32_t>>& env_rect_rtree_inputs, bool need_polygon_only)
{
  NetPrepareContext prepare_context;
  std::vector<GTLRectInt> env_rect_list = std::move(routing_net.env_rect_list);
  std::vector<GTLRectInt> result_rect_list = std::move(routing_net.result_rect_list);
  bool has_env = !env_rect_list.empty();
  bool has_result = !result_rect_list.empty();

  routing_net.polyset.insert(env_rect_list.begin(), env_rect_list.end());
  routing_net.polyset.insert(result_rect_list.begin(), result_rect_list.end());

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

  // Materialize combined geometry into contiguous layer pools.
  routing_net.polygon_begin = static_cast<int32_t>(rv_layer_data.polygon_pool.size());
  routing_net.max_rect_begin = static_cast<int32_t>(rv_layer_data.max_rect_pool.size());
  routing_net.boundary_begin = static_cast<int32_t>(rv_layer_data.boundary_pool.size());

  std::vector<GTLHolePolyInt> hole_poly_list;
  routing_net.polyset.get(hole_poly_list);
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

    collectBoundaryEdges(polygon_hole_poly, false, polygon_id, rv_layer_data.boundary_pool);
    for (auto iter = polygon_hole_poly.begin_holes(); iter != polygon_hole_poly.end_holes(); iter++) {
      GTLPolyInt gtl_poly = *iter;
      GTLHolePolyInt check_hole_poly;
      check_hole_poly.set(gtl_poly.begin(), gtl_poly.end());
      collectBoundaryEdges(check_hole_poly, true, polygon_id, rv_layer_data.boundary_pool);
    }
    polygon_data.boundary_count = static_cast<int32_t>(rv_layer_data.boundary_pool.size()) - polygon_data.boundary_begin;
  }

  routing_net.polygon_count = static_cast<int32_t>(rv_layer_data.polygon_pool.size()) - routing_net.polygon_begin;
  routing_net.max_rect_count = static_cast<int32_t>(rv_layer_data.max_rect_pool.size()) - routing_net.max_rect_begin;
  routing_net.boundary_count = static_cast<int32_t>(rv_layer_data.boundary_pool.size()) - routing_net.boundary_begin;
}

void buildLayerSpatialIndexes(RVLayerData& rv_layer_data, const std::vector<std::pair<GTLRectInt, int32_t>>& env_rect_rtree_inputs)
{
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
}

void buildMetalShortSpatialIndexes(int32_t layer_idx, RVLayerData& rv_layer_data, MetalShortNetPolysetMap& metal_polysets,
                                   MetalShortObsPolysetMap& obs_polysets)
{
  std::vector<IndexedRect> metal_rtree_inputs;
  auto layer_metal_it = metal_polysets.find(layer_idx);
  if (layer_metal_it != metal_polysets.end()) {
    for (auto& [net_idx, polyset] : layer_metal_it->second) {
      std::vector<GTLRectInt> max_rect_list;
      gtl::get_max_rectangles(max_rect_list, polyset);
      for (const GTLRectInt& max_rect : max_rect_list) {
        metal_rtree_inputs.emplace_back(max_rect, net_idx);
      }
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
