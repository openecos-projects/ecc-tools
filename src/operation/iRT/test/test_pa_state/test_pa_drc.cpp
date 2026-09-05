#include <iostream>
#include <stdexcept>

#include "DRCEngine.hpp"
#include "PinAccessor.hpp"

namespace {

std::vector<irt::DETask> checked_task_list;
std::vector<const void*> env_storage_list;

void require(bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

irt::EXTLayerRect makeRect(int32_t x)
{
  irt::EXTLayerRect rect;
  rect.set_real_rect(irt::PlanarRect(x, 0, x + 10, 10));
  rect.set_layer_idx(0);
  return rect;
}

void checkSharedEnvironment()
{
  require(checked_task_list.size() == 2, "Both route views must be checked");
  auto& full = checked_task_list[0];
  auto& ap = checked_task_list[1];
  require(full.get_env_shape_list() == ap.get_env_shape_list(), "Route views disagree on fixed obstacles");
  require(full.get_net_pin_shape_map() == ap.get_net_pin_shape_map(), "Route views disagree on fixed pin shapes");
  require(env_storage_list[0] == env_storage_list[1], "Fixed environment storage was copied instead of transferred");
  require(!full.get_skip_single_net_violation() && ap.get_skip_single_net_violation(), "Route view filtering changed");
  require(ap.get_net_patch_map().empty(), "AP-via-only contains a patch");
  require(full.get_top_name() == ap.get_top_name(), "Route view names disagree");
  require(full.get_check_region_list() == ap.get_check_region_list(), "Route view regions disagree");
  checked_task_list.clear();
  env_storage_list.clear();
}

void testBoxViews(irt::PinAccessor& accessor)
{
  irt::PABox box;
  irt::EXTLayerRect obstacle = makeRect(0);
  irt::EXTLayerRect pin_shape = makeRect(20);
  irt::EXTLayerRect env_patch = makeRect(40);
  box.get_type_layer_net_fixed_rect_map()[true][0][-1].insert(&obstacle);
  box.get_type_layer_net_fixed_rect_map()[true][0][2].insert(&pin_shape);
  irt::Segment<irt::LayerCoord> env_segment(irt::LayerCoord(30, 0, 0), irt::LayerCoord(30, 0, 1));
  box.get_net_pin_env_result_map()[1][0].insert(&env_segment);
  box.get_net_pin_env_patch_map()[3][0].insert(&env_patch);
  irt::PAPin pin0;
  irt::PAPin pin1;
  irt::AccessPoint ap0(0, irt::LayerCoord(0, 0, 0));
  irt::AccessPoint ap1(1, irt::LayerCoord(20, 0, 0));
  irt::PATask task0;
  irt::PATask task1;
  task0.set_net_idx(0);
  task0.set_task_idx(0);
  task0.set_pa_pin(&pin0);
  task0.set_selected_access_point(&ap0);
  task1.set_net_idx(0);
  task1.set_task_idx(1);
  task1.set_pa_pin(&pin1);
  task1.set_selected_access_point(&ap1);
  box.get_pa_task_list() = {&task1, &task0};
  auto& result0 = box.get_curr_result().get_net_task_result_map()[0][0];
  result0.emplace_back(irt::LayerCoord(0, 0, 1), irt::LayerCoord(10, 0, 1));
  result0.emplace_back(irt::LayerCoord(0, 0, 0), irt::LayerCoord(0, 0, 1));
  auto& result1 = box.get_curr_result().get_net_task_result_map()[0][1];
  result1.emplace_back(irt::LayerCoord(20, 0, 0), irt::LayerCoord(20, 0, 1));
  box.get_curr_result().get_net_task_patch_map()[0][0].push_back(makeRect(0));
  accessor.getRouteViolationList(box);
  require(checked_task_list.size() == 2, "Box did not check both views");
  require(checked_task_list[0].get_net_result_map().at(0).size() == 3, "Full box lost current result segments");
  require(checked_task_list[0].get_net_result_map().at(1).front() == &env_segment, "Full box lost a neighbor result");
  require(checked_task_list[0].get_need_checked_net_set() == std::set<int32_t>({0, 1, 2, 3}), "Full box checked-net set changed");
  require(checked_task_list[1].get_need_checked_net_set() == std::set<int32_t>({0, 2}), "AP box checked-net set changed");
  require(checked_task_list[1].get_net_result_map().size() == 1, "AP box contains a neighbor result");
  require(checked_task_list[1].get_net_result_map().at(0) == std::vector<irt::Segment<irt::LayerCoord>*>({&result0[1], &result1[0]}),
          "AP box selection or task-ID order changed");
  checkSharedEnvironment();
}

void testModelViews(irt::PinAccessor& accessor)
{
  irt::PAModel model;
  model.get_pa_net_list().resize(4);
  for (int32_t i = 0; i < 4; i++) {
    model.get_pa_net_list()[i].set_net_idx(i);
    model.get_pa_net_list()[i].get_pa_pin_list().resize(1);
    model.get_pa_net_list()[i].get_pa_pin_list()[0].set_access_point(irt::AccessPoint(0, irt::LayerCoord(0, 0, 0)));
  }
  model.get_pa_box_map().init(1, 1);
  irt::PABox& box = model.get_pa_box_map()[0][0];
  box.get_box_rect().set_real_rect(irt::PlanarRect(0, 0, 100, 100));
  auto& result = box.get_net_pin_own_result_map()[0][0];
  result.emplace_back(irt::LayerCoord(0, 0, 0), irt::LayerCoord(0, 0, 1));
  result.emplace_back(irt::LayerCoord(0, 0, 1), irt::LayerCoord(10, 0, 1));
  box.get_net_pin_own_patch_map()[3][0].push_back(makeRect(0));
  irt::EXTLayerRect obstacle = makeRect(0);
  irt::EXTLayerRect pin_shape = makeRect(20);
  auto& rtree = irt::DataManager::getInst().getDatabase().get_type_layer_fixed_rect_rtree_map()[true][0];
  rtree.insert({irt::Utility::convertToBGRectInt(obstacle.get_real_rect()), {-1, &obstacle}});
  rtree.insert({irt::Utility::convertToBGRectInt(pin_shape.get_real_rect()), {2, &pin_shape}});
  accessor.getFullRouteViolationList(model);
  require(checked_task_list[0].get_need_checked_net_set() == std::set<int32_t>({0, 1, 2, 3}), "Full model lost checked nets");
  require(checked_task_list[1].get_need_checked_net_set() == checked_task_list[0].get_need_checked_net_set(), "AP model lost checked nets");
  require(checked_task_list[0].get_net_result_map().at(0).size() == 2, "Full model lost a segment");
  require(checked_task_list[1].get_net_result_map().at(0) == std::vector<irt::Segment<irt::LayerCoord>*>({&result[0]}), "AP model contains a non-AP segment");
  checkSharedEnvironment();

  for (irt::Segment<irt::LayerCoord>& segment : result) {
    box.get_net_pin_env_result_map()[0][0].insert(&segment);
  }
  box.get_net_pin_env_patch_map()[3][0].insert(&box.get_net_pin_own_patch_map()[3][0][0]);
  irt::DataManager::getInst().getDatabase().get_routing_layer_list().resize(2);
  for (int32_t i = 0; i < 2; i++) {
    irt::DataManager::getInst().getDatabase().get_routing_layer_list()[i].set_layer_idx(i);
  }
  accessor.getDirtyRouteViolationList(model, box);
  require(checked_task_list[0].get_need_checked_net_set() == std::set<int32_t>({0, 2, 3}), "Dirty full view checked-net set changed");
  require(checked_task_list[1].get_need_checked_net_set() == std::set<int32_t>({0, 2}), "Dirty AP view includes patch-only nets");
  require(checked_task_list[0].get_check_region_list().size() == 2, "Dirty view lost check layers");
  require(checked_task_list[1].get_net_result_map().at(0) == std::vector<irt::Segment<irt::LayerCoord>*>({&result[0]}), "Dirty AP selection changed");
  checkSharedEnvironment();
  rtree.clear();
}

void testPatchInput(irt::PinAccessor& accessor)
{
  irt::PABox box;
  irt::PATask task;
  task.set_net_idx(0);
  task.set_task_idx(0);
  box.get_pa_task_list().push_back(&task);
  box.get_patch_state().set_curr_patch_task(&task);
  irt::EXTLayerRect obstacle = makeRect(0);
  irt::EXTLayerRect pin_shape = makeRect(40);
  irt::EXTLayerRect env_patch = makeRect(40);
  box.get_type_layer_net_fixed_rect_map()[true][0][-1].insert(&obstacle);
  box.get_type_layer_net_fixed_rect_map()[true][0][2].insert(&pin_shape);
  box.get_net_pin_env_patch_map()[3][0].insert(&env_patch);
  box.get_curr_result().get_net_task_patch_map()[0][0].push_back(makeRect(40));
  box.get_curr_result().get_net_task_patch_map()[0][1].push_back(makeRect(0));
  box.get_patch_state().get_routing_patch_list().push_back(makeRect(0));
  irt::DETask de_task = accessor.buildPatchDETask(box, {irt::ViolationType::kMinimumArea}, {irt::LayerRect(-5, -5, 15, 15, 0)});
  require(de_task.get_env_shape_list().size() == 1, "Patch check lost its obstacle");
  require(de_task.get_net_pin_shape_map().at(2).empty(), "Patch check ignored its spatial region");
  require(de_task.get_net_patch_map().at(3).empty(), "Patch check contains an out-of-region patch");
  require(
      de_task.get_net_patch_map().at(0)
          == std::vector<irt::EXTLayerRect*>({&box.get_patch_state().get_routing_patch_list()[0], &box.get_curr_result().get_net_task_patch_map()[0][1][0]}),
      "Patch check did not replace the current task's old patches");
  require(de_task.get_need_checked_net_set() == std::set<int32_t>({0, 2, 3}), "Patch checked-net set changed");
  require(de_task.get_check_type_set() == std::set<irt::ViolationType>({irt::ViolationType::kMinimumArea}), "Patch rule selection changed");
}

void testEnvironmentUpdates(irt::PinAccessor& accessor)
{
  auto& database = irt::DataManager::getInst().getDatabase();
  std::vector<int32_t> coords = {0, 10, 20, 30, 40, 50, 60};
  irt::PABox box;
  box.get_box_rect().set_real_rect(irt::PlanarRect(0, 0, 60, 60));
  box.get_box_track_axis().set_x_grid_list(irt::Utility::makeScaleGridList(coords));
  box.get_box_track_axis().set_y_grid_list(irt::Utility::makeScaleGridList(coords));
  irt::DataManager::getInst().getConfig().bottom_routing_layer_idx = 0;
  irt::DataManager::getInst().getConfig().top_routing_layer_idx = 1;
  database.get_routing_layer_list().resize(2);
  for (int32_t layer_idx = 0; layer_idx < 2; layer_idx++) {
    auto& layer = database.get_routing_layer_list()[layer_idx];
    layer.set_layer_idx(layer_idx);
    layer.set_min_width(4);
    layer.set_prefer_direction(layer_idx == 0 ? irt::Direction::kHorizontal : irt::Direction::kVertical);
    layer.set_eol_spacing(4);
    layer.set_eol_ete(5);
    layer.set_eol_within(2);
    layer.get_prl_spacing_table().get_width_list() = {0};
    layer.get_prl_spacing_table().get_width_parallel_length_map().init(1, 1, 3);
    database.get_layer_enclosure_map()[layer_idx] = irt::PlanarRect(-2, -2, 2, 2);
    box.get_layer_axis_map()[layer_idx] = {std::set<int32_t>(coords.begin(), coords.end()), std::set<int32_t>(coords.begin(), coords.end())};
  }
  accessor.buildLayerNodeMap(box);
  accessor.buildLayerShadowMap(box);
  accessor.buildPANodeNeighbor(box);
  irt::EXTLayerRect patch0 = makeRect(10);
  irt::EXTLayerRect patch1 = makeRect(15);
  accessor.updateRoutedRectToEnvironment(box, irt::ChangeType::kAdd, 7, patch0, true);
  accessor.updateRoutedRectToEnvironment(box, irt::ChangeType::kAdd, 7, patch1, true);
  std::vector<irt::PANode::OrientNetCountMap> node_counts;
  size_t occupied_num = 0;
  for (auto& node_map : box.get_layer_node_map()) {
    for (int32_t x = 0; x < node_map.get_x_size(); x++) {
      for (int32_t y = 0; y < node_map.get_y_size(); y++) {
        node_counts.push_back(node_map[x][y].get_orient_routed_rect_map());
        occupied_num += !node_counts.back().empty();
      }
    }
  }
  require(occupied_num > 0, "Environment projection did not touch any graph nodes");
  auto shadow_counts = box.get_layer_shadow_map()[0].get_net_routed_rect_map();
  accessor.updateRoutedRectToEnvironment(box, irt::ChangeType::kAdd, 7, patch0, true);
  accessor.updateRoutedRectToEnvironment(box, irt::ChangeType::kDel, 7, patch0, true);
  size_t node_idx = 0;
  for (auto& node_map : box.get_layer_node_map()) {
    for (int32_t x = 0; x < node_map.get_x_size(); x++) {
      for (int32_t y = 0; y < node_map.get_y_size(); y++) {
        require(node_map[x][y].get_orient_routed_rect_map() == node_counts[node_idx++], "Graph projection add/remove is not balanced");
      }
    }
  }
  require(box.get_layer_shadow_map()[0].get_net_routed_rect_map() == shadow_counts, "Shadow projection add/remove is not balanced");
  accessor.updateRoutedRectToEnvironment(box, irt::ChangeType::kDel, 7, patch0, true);
  accessor.updateRoutedRectToEnvironment(box, irt::ChangeType::kDel, 7, patch1, true);
  for (auto& node_map : box.get_layer_node_map()) {
    for (int32_t x = 0; x < node_map.get_x_size(); x++) {
      for (int32_t y = 0; y < node_map.get_y_size(); y++) {
        require(node_map[x][y].get_orient_routed_rect_map().empty(), "Graph projection left stale contributions");
      }
    }
  }
  require(box.get_layer_shadow_map()[0].get_net_routed_rect_map().empty(), "Shadow projection left stale contributions");
}

}  // namespace

extern "C" std::vector<irt::Violation> __wrap__ZN3irt9DRCEngine16getViolationListERNS_6DETaskE(irt::DRCEngine*, irt::DETask& de_task)
{
  env_storage_list.push_back(de_task.get_env_shape_list().data());
  checked_task_list.push_back(de_task);
  return {};
}

int main()
{
  irt::DataManager::initInst();
  irt::DRCEngine::initInst();
  try {
    irt::PinAccessor accessor;
    testBoxViews(accessor);
    testModelViews(accessor);
    testPatchInput(accessor);
    testEnvironmentUpdates(accessor);
    irt::DRCEngine::destroyInst();
    irt::DataManager::destroyInst();
    irt::Utility::destroyInst();
    std::cout << "PA DRC input tests passed\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
