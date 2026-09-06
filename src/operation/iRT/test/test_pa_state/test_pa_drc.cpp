#include <array>
#include <deque>
#include <iostream>
#include <random>
#include <stdexcept>

#include "DRCEngine.hpp"
#include "PinAccessor.hpp"

namespace {

std::vector<irt::DETask> checked_task_list;
std::vector<const void*> env_storage_list;
std::deque<std::vector<irt::Violation>> violation_response_list;

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

using FixedRectMap = std::map<bool, std::map<int32_t, std::map<int32_t, std::set<irt::EXTLayerRect*>>>>;

void checkFixedInput(irt::PinAccessor& accessor, const FixedRectMap& rect_map, const irt::PAFixedGeometry& geometry,
                     const std::vector<irt::LayerRect>& region_list)
{
  irt::DETask scanned;
  irt::DETask indexed;
  scanned.set_check_region_list(region_list);
  indexed.set_check_region_list(region_list);
  accessor.buildFixedDETask(scanned, rect_map);
  accessor.buildFixedDETask(indexed, geometry);
  require(indexed.get_env_shape_list() == scanned.get_env_shape_list(), "Indexed fixed obstacles differ from the ordered scan");
  require(indexed.get_net_pin_shape_map() == scanned.get_net_pin_shape_map(), "Indexed fixed pin shapes differ from the ordered scan");
  accessor.buildCheckedNetSet(scanned);
  accessor.buildCheckedNetSet(indexed);
  require(indexed.get_need_checked_net_set() == scanned.get_need_checked_net_set(), "Fixed geometry lost an empty checked net");
}

void testFixedGeometry(irt::PinAccessor& accessor)
{
  irt::PABox box;
  irt::PAFixedGeometry& geometry = box.get_fixed_geometry();
  FixedRectMap rect_map;
  std::mt19937 random(37);
  std::vector<irt::EXTLayerRect> rect_list(768);
  for (size_t i = 0; i < rect_list.size(); i++) {
    auto& rect = rect_list[i];
    int32_t x = static_cast<int32_t>(random() % 500) - 250;
    int32_t y = static_cast<int32_t>(random() % 500) - 250;
    rect.set_real_rect(irt::PlanarRect(x, y, x + static_cast<int32_t>(random() % 30), y + static_cast<int32_t>(random() % 30)));
    rect.set_layer_idx(i % 3);
    rect_map[i % 2 == 0][rect.get_layer_idx()][static_cast<int32_t>(i % 11) - 1].insert(&rect);
  }
  rect_list[0].set_real_rect(irt::PlanarRect(0, 0, 10, 10));
  rect_map[false][0][-1].insert(&rect_list[0]);
  rect_map[true][0][20].insert(&rect_list[0]);
  rect_map[true][0][30];
  rect_map[false][7][31];
  geometry.build(rect_map);
  require(geometry.get_built(), "Fixed geometry did not publish its built state");
  require(geometry.get_shape_list().size() == rect_list.size() + 2, "Fixed geometry merged distinct source entries");
  require(geometry.query({}).size() == geometry.get_shape_list().size(), "An unrestricted query omitted fixed geometry");
  checkFixedInput(accessor, rect_map, geometry, {});
  checkFixedInput(accessor, rect_map, geometry, {irt::LayerRect(10, 10, 10, 10, 0)});
  checkFixedInput(accessor, rect_map, geometry, {irt::LayerRect(10, 0, 11, 10, 0), irt::LayerRect(0, 0, 10, 10, 0)});
  checkFixedInput(accessor, rect_map, geometry, {irt::LayerRect(-1000, -1000, 1000, 1000, 9)});
  for (int32_t i = 0; i < 4000; i++) {
    std::vector<irt::LayerRect> region_list;
    for (int32_t j = 0; j <= i % 3; j++) {
      int32_t x = static_cast<int32_t>(random() % 600) - 300;
      int32_t y = static_cast<int32_t>(random() % 600) - 300;
      region_list.emplace_back(x, y, x + static_cast<int32_t>(random() % 80), y + static_cast<int32_t>(random() % 80), random() % 5);
    }
    if (i % 2 == 0) {
      region_list.push_back(region_list.front());
    }
    checkFixedInput(accessor, rect_map, geometry, region_list);
  }
  irt::PAFixedGeometry copied = geometry;
  checkFixedInput(accessor, rect_map, copied, {irt::LayerRect(0, 0, 10, 10, 0)});
  accessor.freePABox(box);
  require(!geometry.get_built(), "Freeing the box retained its geometry view");
  geometry.build({});
  checkFixedInput(accessor, {}, geometry, {});
  require(geometry.query({irt::LayerRect(0, 0, 10, 10, 0)}).empty(), "Rebuilding an empty box retained old geometry");
}

void testFixedGeometryErrors()
{
  bool had_throw_policy = std::getenv("ECC_LOGGER_THROW_ON_ERROR") != nullptr;
  if (!had_throw_policy) {
    require(setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1) == 0, "Cannot enable logger exceptions for rejection tests");
  }
  const std::array<const char*, 5> expected_messages = {"has not been built", "has already been built", "Invalid fixed PA geometry on layer",
                                                        "Invalid fixed PA geometry on layer", "Invalid fixed PA geometry query region"};
  for (size_t test_idx = 0; test_idx < expected_messages.size(); test_idx++) {
    irt::PAFixedGeometry geometry;
    auto rect = makeRect(0);
    bool rejected = false;
    try {
      switch (test_idx) {
        case 0:
          geometry.query({});
          break;
        case 1:
          geometry.build({});
          geometry.build({});
          break;
        case 2:
          geometry.build({{true, {{1, {{-1, {&rect}}}}}}});
          break;
        case 3:
          rect.set_real_rect(irt::PlanarRect(10, 0, 0, 10));
          geometry.build({{true, {{0, {{-1, {&rect}}}}}}});
          break;
        case 4:
          geometry.build({});
          geometry.query({irt::LayerRect(10, 0, 0, 10, 0)});
          break;
      }
    } catch (const std::runtime_error& error) {
      rejected = std::string(error.what()).find(expected_messages[test_idx]) != std::string::npos;
    }
    require(rejected, "Invalid fixed geometry state was not rejected with the expected error");
  }
  if (!had_throw_policy) {
    require(unsetenv("ECC_LOGGER_THROW_ON_ERROR") == 0, "Cannot restore logger error policy");
  }
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
  box.get_fixed_geometry().build({{true, {{0, {{-1, {&obstacle}}, {2, {&pin_shape}}}}}}});
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
  box.get_pa_task_list() = {task0, task1};
  box.get_task_order_list() = {1, 0};
  box.get_curr_result().get_task_result_list().resize(2);
  auto& result0 = box.get_curr_result().get_task_result_list()[0].get_segment_list();
  result0.emplace_back(irt::LayerCoord(0, 0, 1), irt::LayerCoord(10, 0, 1));
  result0.emplace_back(irt::LayerCoord(0, 0, 0), irt::LayerCoord(0, 0, 1));
  auto& result1 = box.get_curr_result().get_task_result_list()[1].get_segment_list();
  result1.emplace_back(irt::LayerCoord(20, 0, 0), irt::LayerCoord(20, 0, 1));
  box.get_curr_result().get_task_result_list()[0].get_patch_list().push_back(makeRect(0));
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

  ap0.set_candidate_via_list({irt::ViaMasterIdx(0, 0), irt::ViaMasterIdx(0, 1)});
  ap1.set_candidate_via_list(ap0.get_candidate_via_list());
  result0[1].set_via_master_idx(irt::ViaMasterIdx(0, 1));
  accessor.updateAccessPoint(box);
  require(
      box.get_curr_result().get_task_result_list()[0].get_access_point().get_candidate_via_list() == std::vector<irt::ViaMasterIdx>({irt::ViaMasterIdx(0, 1)}),
      "Task result did not retain the selected via master");
  require(box.get_curr_result().get_task_result_list()[1].get_access_point().get_candidate_via_list().empty(),
          "An unselected via candidate was published in the task result");
  require(ap0.get_candidate_via_list().size() == 2, "Publishing a selected via mutated the source candidate list");
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
  box.get_pa_task_list().resize(2);
  irt::PATask& task = box.get_pa_task_list()[0];
  task.set_net_idx(0);
  task.set_task_idx(0);
  box.get_pa_task_list()[1].set_net_idx(0);
  box.get_pa_task_list()[1].set_task_idx(1);
  box.get_task_order_list() = {1, 0};
  box.get_curr_result().get_task_result_list().resize(2);
  box.get_patch_state().set_curr_patch_task(&task);
  irt::EXTLayerRect obstacle = makeRect(0);
  irt::EXTLayerRect pin_shape = makeRect(40);
  irt::EXTLayerRect env_patch = makeRect(40);
  box.get_fixed_geometry().build({{true, {{0, {{-1, {&obstacle}}, {2, {&pin_shape}}}}}}});
  box.get_net_pin_env_patch_map()[3][0].insert(&env_patch);
  box.get_curr_result().get_task_result_list()[0].get_patch_list().push_back(makeRect(40));
  box.get_curr_result().get_task_result_list()[1].get_patch_list().push_back(makeRect(0));
  box.get_patch_state().get_routing_patch_list().push_back(makeRect(0));
  irt::DETask de_task = accessor.buildPatchDETask(box, {irt::ViolationType::kMinimumArea}, {irt::LayerRect(-5, -5, 15, 15, 0)});
  require(de_task.get_env_shape_list().size() == 1, "Patch check lost its obstacle");
  require(de_task.get_net_pin_shape_map().at(2).empty(), "Patch check ignored its spatial region");
  require(de_task.get_net_patch_map().at(3).empty(), "Patch check contains an out-of-region patch");
  require(de_task.get_net_patch_map().at(0)
              == std::vector<irt::EXTLayerRect*>(
                  {&box.get_patch_state().get_routing_patch_list()[0], &box.get_curr_result().get_task_result_list()[1].get_patch_list()[0]}),
          "Patch check did not replace the current task's old patches");
  require(de_task.get_need_checked_net_set() == std::set<int32_t>({0, 2, 3}), "Patch checked-net set changed");
  require(de_task.get_check_type_set() == std::set<irt::ViolationType>({irt::ViolationType::kMinimumArea}), "Patch rule selection changed");
  box.get_patch_state().get_routing_patch_list()[0] = makeRect(40);
  box.get_curr_result().get_task_result_list()[1].get_patch_list()[0] = makeRect(40);
  de_task = accessor.buildPatchDETask(box, {irt::ViolationType::kMinimumArea}, {irt::LayerRect(-5, -5, 15, 15, 0)});
  require(de_task.get_net_patch_map().at(0).empty(), "The immutable fixed view cached a mutable task patch");
}

void testFixedOverlapPoly(irt::PinAccessor& accessor)
{
  irt::PABox box;
  irt::PAPin pin;
  pin.set_pin_idx(0);
  box.get_pa_task_list().resize(1);
  irt::PATask& task = box.get_pa_task_list()[0];
  task.set_net_idx(7);
  task.set_task_idx(0);
  task.set_pa_pin(&pin);
  box.get_curr_result().get_task_result_list().resize(1);
  box.get_patch_state().set_curr_patch_task(&task);
  auto pin_rect = makeRect(0);
  auto other_net_rect = makeRect(5);
  auto cut_rect = makeRect(7);
  auto other_layer_rect = makeRect(9);
  other_layer_rect.set_layer_idx(1);
  box.get_fixed_geometry().build(
      {{false, {{0, {{7, {&cut_rect}}}}}}, {true, {{0, {{7, {&pin_rect}}, {8, {&other_net_rect}}}}, {1, {{7, {&other_layer_rect}}}}}}});
  irt::Violation violation;
  violation.set_violation_shape(makeRect(5));
  auto poly = accessor.getViolationOverlapPoly(box, violation);
  require(gtl::area(poly) == 100, "Overlap polygon included a different net, layer, or cut shape");
  violation.set_violation_shape(makeRect(11));
  poly = accessor.getViolationOverlapPoly(box, violation);
  require(gtl::area(poly) == 0, "Raw fixed geometry was expanded like a cost shadow");
  require(box.get_net_pin_env_result_map().empty() && box.get_net_pin_env_patch_map().empty(), "Reading an absent pin mutated the environment");
  box.get_net_pin_env_result_map()[7][1];
  box.get_net_pin_env_patch_map()[7][1];
  accessor.getViolationOverlapPoly(box, violation);
  require(box.get_net_pin_env_result_map().at(7).size() == 1 && box.get_net_pin_env_patch_map().at(7).size() == 1,
          "Reading an absent pin inserted an empty environment row");
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
  irt::PAIterParam param;
  param.set_fixed_rect_unit(3);
  param.set_routed_rect_unit(5);
  param.set_violation_unit(7);
  box.set_pa_iter_param(&param);
  irt::EXTLayerRect fixed_rect = makeRect(40);
  fixed_rect.set_layer_idx(1);
  box.get_fixed_geometry().build({{true, {{1, {{-1, {&fixed_rect}}}}}}});
  accessor.buildBoxEnvironment(box);
  irt::EXTLayerRect fixed_query = makeRect(40);
  require(accessor.getFixedRectCost(box, 8, fixed_query) == 0, "Fixed shadow lookup crossed routing layers");
  fixed_query.set_layer_idx(1);
  require(accessor.getFixedRectCost(box, 8, fixed_query) == 6, "Fixed shadow lookup lost a PRL/EOL shape or its weight");
  irt::EXTLayerRect patch0 = makeRect(10);
  irt::EXTLayerRect patch1 = makeRect(15);
  accessor.updateRoutedRectToEnvironment(box, irt::ChangeType::kAdd, 7, patch0, true);
  accessor.updateRoutedRectToEnvironment(box, irt::ChangeType::kAdd, 7, patch1, true);
  double routed_cost = accessor.getRoutedRectCost(box, 8, patch0);
  require(routed_cost > 0 && accessor.getRoutedRectCost(box, 7, patch0) == 0, "Routed shadow lookup lost occupancy or same-net exemption");
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
  require(accessor.getRoutedRectCost(box, 8, patch0) == routed_cost, "Repeated projection was charged twice");
  accessor.updateRoutedRectToEnvironment(box, irt::ChangeType::kDel, 7, patch0, true);
  require(accessor.getRoutedRectCost(box, 8, patch0) == routed_cost, "Removing a repeated projection lost its cost");
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
  require(box.get_layer_shadow_map()[0].get_routed_rect_rtree().empty(), "Shadow projection left stale index entries");
  require(accessor.getRoutedRectCost(box, 8, patch0) == 0, "Balanced projection updates left a nonzero cost");
}

void testPathOutcome(irt::PinAccessor& accessor)
{
  irt::PANode start;
  irt::PANode end;
  start.set_coord(0, 0);
  start.set_layer_idx(0);
  end.set_coord(10, 0);
  end.set_layer_idx(0);
  irt::PATask task;
  task.set_net_idx(0);
  irt::PABox box;
  irt::PAIterParam param;
  param.set_prefer_wire_unit(1);
  param.set_non_prefer_wire_unit(2);
  box.set_pa_iter_param(&param);
  box.get_route_state().set_curr_route_task(&task);
  box.get_route_state().get_source_node_list() = {&start};
  box.get_route_state().get_target_node_list() = {&end};
  box.get_route_state().set_path_head_node(&end);
  require(!accessor.routeSinglePath(box), "An unreachable target was reported as connected");
  require(box.get_route_state().get_path_head_node() == nullptr && !accessor.reachEnd(box),
          "Exhausted search retained a successful endpoint");
  accessor.resetSinglePath(box);
  require(start.isNone() && start.get_open_queue_idx() == -1, "Failed search left stale node state");

  start.setNeighborNode(irt::Orientation::kEast, &end);
  end.setNeighborNode(irt::Orientation::kWest, &start);
  require(accessor.routeSinglePath(box), "A connected target was not reached");
  require(box.get_route_state().get_path_head_node() == &end && accessor.reachEnd(box),
          "Successful search did not publish its endpoint");
  auto segments = accessor.getRoutingSegmentListByNode(&end);
  require(segments.size() == 1, "Successful search lost its segment");
  accessor.resetSinglePath(box);

  box.get_route_state().get_target_node_list() = {&start};
  require(accessor.routeSinglePath(box), "A coincident source and target was not reached");
  require(accessor.getRoutingSegmentListByNode(&start).empty(), "A coincident source and target produced a segment");
  accessor.resetSinglePath(box);
  box.get_route_state().get_open_queue().release();
}

void testRouteTaskContract(irt::PinAccessor& accessor)
{
  bool had_throw_policy = std::getenv("ECC_LOGGER_THROW_ON_ERROR") != nullptr;
  if (!had_throw_policy) {
    require(setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1) == 0, "Cannot enable route contract exceptions");
  }
  for (int32_t case_idx = 0; case_idx < 7; case_idx++) {
    irt::PABox box;
    irt::PATask task;
    std::vector<irt::PAGroup> groups(case_idx < 3 ? case_idx : 2);
    if (case_idx == 2) {
      groups.resize(3);
    }
    for (size_t i = 0; i < groups.size(); i++) {
      groups[i].set_is_target(i != 0);
      groups[i].get_coord_list().emplace_back(10, 10, 0);
    }
    if (case_idx == 3) {
      groups.front().set_is_target(true);
    } else if (case_idx == 4) {
      groups.back().set_is_target(false);
    } else if (case_idx == 5) {
      groups.front().get_coord_list().clear();
    } else if (case_idx == 6) {
      groups.back().get_coord_list().clear();
    }
    task.set_pa_group_list(std::move(groups));
    bool rejected = false;
    try {
      accessor.initSingleRouteTask(box, &task);
    } catch (const std::runtime_error& error) {
      rejected = std::string(error.what()).find("one nonempty source group and one nonempty target group") != std::string::npos;
    }
    require(rejected, "Invalid PA source/target contract was not rejected at the routing boundary");
    require(box.get_route_state().get_curr_route_task() == nullptr && box.get_net_access_point_map().empty(),
            "Rejecting an invalid task mutated routing state or environment");
  }
  if (!had_throw_policy) {
    require(unsetenv("ECC_LOGGER_THROW_ON_ERROR") == 0, "Cannot restore route contract exception policy");
  }
}

void testTaskIdentity(irt::PinAccessor& accessor)
{
  irt::PABox box;
  irt::PAIterParam param;
  param.set_max_routed_times(2);
  box.set_pa_iter_param(&param);
  box.get_box_rect().set_real_rect(irt::PlanarRect(0, 0, 100, 100));
  box.get_pa_task_list().resize(3);
  box.get_curr_result().get_task_result_list().resize(3);
  box.get_task_order_list() = {2, 1, 0};
  std::array<irt::PAPin, 3> pins;
  std::vector<irt::PAGroup> groups(2);
  groups[0].get_coord_list().emplace_back(10, 10, 0);
  groups[1].get_coord_list().emplace_back(10, 10, 1);
  for (int32_t i = 0; i < 3; i++) {
    pins[i].set_pin_idx(i);
    box.get_pa_task_list()[i].set_net_idx(0);
    box.get_pa_task_list()[i].set_task_idx(i);
    box.get_pa_task_list()[i].set_pa_pin(&pins[i]);
    box.get_pa_task_list()[i].set_pa_group_list(groups);
    box.get_pa_task_list()[i].set_bounding_box(irt::PlanarRect(10, 10, 10, 10));
    box.get_curr_result().get_task_result_list()[i].get_patch_list().push_back(makeRect(0));
  }
  box.get_pa_task_list()[2].set_routed_times(2);
  require(
      !irt::CmpPATask()(&box.get_pa_task_list()[0], &box.get_pa_task_list()[1]) && !irt::CmpPATask()(&box.get_pa_task_list()[1], &box.get_pa_task_list()[0]),
      "The identity test must use equal-priority tasks");
  irt::Violation violation;
  violation.set_violation_shape(makeRect(0));
  violation.set_violation_net_set({0});
  box.get_curr_result().get_route_violation_list() = {violation, violation};
  irt::ScaleGrid grid;
  grid.set_step_length(10);
  irt::DataManager::getInst().getDatabase().get_routing_layer_list()[0].get_track_axis().get_x_grid_list() = {grid};
  std::vector<int32_t> schedule;
  irt::PATask* first_task = &box.get_pa_task_list()[0];
  accessor.updateTaskSchedule(box, schedule, 0);
  require(schedule == std::vector<int32_t>({1, 0}), "Distinct equal-priority tasks were merged, repeated, or exceeded the route limit");
  accessor.updateTaskSchedule(box, schedule, 1);
  require(box.get_task_order_list() == std::vector<int32_t>({0, 1, 2}), "Alternating schedule order changed");
  require(&box.get_pa_task_list()[0] == first_task && first_task->get_task_idx() == 0, "Rescheduling moved task identity or storage");
}

void testTaskResultFlow(irt::PinAccessor& accessor)
{
  irt::PAModel model;
  model.get_pa_net_list().resize(2);
  irt::PANet& net = model.get_pa_net_list()[1];
  net.set_net_idx(1);
  net.get_pa_pin_list().resize(4);
  irt::PABox box;
  box.set_pa_box_id(irt::PABoxId(0, 0));
  box.get_box_rect().set_real_rect(irt::PlanarRect(0, 0, 100, 100));
  box.set_initial_routing(false);
  irt::PAIterParam param;
  param.set_max_routed_times(2);
  box.set_pa_iter_param(&param);
  for (int32_t i = 0; i < 3; i++) {
    irt::PAPin& pin = net.get_pa_pin_list()[i];
    pin.set_pin_idx(i);
    pin.set_owner_pa_box_id(irt::PABoxId(0, 0));
    pin.set_access_point(irt::AccessPoint(i, irt::LayerCoord(10, 10, 0)));
    pin.get_access_point().set_candidate_via_list({irt::ViaMasterIdx(0, i)});
    if (i < 2) {
      pin.get_target_coord_list().emplace_back(10, 10, 1);
    }
    box.get_net_access_point_map()[1].insert(&pin.get_access_point());
    box.get_net_pin_own_result_map()[1][i].emplace_back(irt::LayerCoord(10, 10, 0), irt::LayerCoord(10, 10, 1));
    box.get_net_pin_own_result_map()[1][i][0].set_via_master_idx(irt::ViaMasterIdx(0, i));
    box.get_net_pin_own_patch_map()[1][i].push_back(makeRect(i * 20));
  }
  irt::PAPin& neighbor_pin = net.get_pa_pin_list()[3];
  neighbor_pin.set_pin_idx(3);
  neighbor_pin.set_owner_pa_box_id(irt::PABoxId(1, 0));
  neighbor_pin.set_access_point(irt::AccessPoint(3, irt::LayerCoord(10, 10, 0)));
  neighbor_pin.get_target_coord_list().emplace_back(10, 10, 1);
  box.get_net_access_point_map()[1].insert(&neighbor_pin.get_access_point());
  const auto* old_segments = box.get_net_pin_own_result_map()[1][0].data();
  accessor.initPATaskList(model, box);
  require(box.get_curr_result().get_task_result_list().empty() && box.get_net_pin_own_result_map().at(1).size() == 3,
          "Task definition imported owner results before its explicit import phase");
  accessor.initPATaskResult(box);
  require(box.get_pa_task_list().size() == 2 && box.get_curr_result().get_task_result_list().size() == 2, "Task and result table sizes disagree");
  require(box.get_curr_result().get_task_result_list()[0].get_segment_list().data() == old_segments, "Importing owner results copied segment storage");
  require(box.get_net_pin_own_result_map().at(1).size() == 1 && box.get_net_pin_own_result_map().at(1).contains(2), "Untasked owner result was lost");
  require(box.get_net_pin_env_result_map().at(1).at(2).contains(&box.get_net_pin_own_result_map().at(1).at(2)[0]),
          "Untasked result is absent from environment");
  require(box.get_net_pin_env_patch_map().at(1).at(2).contains(&box.get_net_pin_own_patch_map().at(1).at(2)[0]), "Untasked patch is absent from environment");
  box.get_task_order_list() = {1, 0};
  accessor.routePABox(box);
  require(!box.get_best_result().get_valid(), "An empty routing schedule created an unnecessary snapshot");
  require(box.get_curr_result().get_task_result_list()[0].get_segment_list().data() == old_segments,
          "An empty routing schedule replaced the imported result");
  accessor.updateBestResult(box);
  box.get_curr_result().get_task_result_list()[0].get_segment_list().clear();
  box.get_curr_result().get_task_result_list()[0].get_patch_list().clear();
  box.get_curr_result().get_task_result_list()[0].set_access_point(irt::AccessPoint());
  accessor.selectBestResult(box);
  require(!box.get_best_result().get_valid() && box.get_curr_result().get_task_result_list()[0].get_segment_list().size() == 1,
          "Best result selection did not restore the current result and consume the snapshot");
  require(box.get_net_pin_own_result_map().at(1).size() == 1 && box.get_net_pin_own_patch_map().at(1).size() == 1,
          "Best result selection published task results before the explicit publication phase");
  accessor.uploadPABoxResult(box);
  for (int32_t i = 0; i < 3; i++) {
    require(box.get_net_pin_own_result_map().at(1).at(i)[0].get_via_master_idx() == irt::ViaMasterIdx(0, i),
            "Best result restoration changed pin/via ownership");
    require(box.get_net_pin_own_patch_map().at(1).at(i)[0].get_real_rect() == makeRect(i * 20).get_real_rect(),
            "Best result restoration changed patch ownership");
    require(net.get_pa_pin_list()[i].get_access_point().get_candidate_via_list() == std::vector<irt::ViaMasterIdx>({irt::ViaMasterIdx(0, i)}),
            "Best result restoration lost an unchanged access point or via selection");
  }
  accessor.freePABox(box);
  require(box.get_pa_task_list().empty() && box.get_task_order_list().empty() && box.get_curr_result().get_task_result_list().empty(),
          "Box cleanup retained task storage");
  require(box.get_net_pin_own_result_map().at(1).size() == 3, "Box cleanup erased unpublished owner results");
}

void testEmptyModelSnapshot(irt::PinAccessor& accessor)
{
  irt::PAModel model;
  require(!model.get_best_result_valid(), "A new model already has a best result");
  accessor.updateBestResult(model);
  require(model.get_best_result_valid(), "An empty model result cannot be snapshotted");
  irt::Violation violation;
  violation.set_violation_type(irt::ViolationType::kMinimumArea);
  model.get_route_violation_list().push_back(violation);
  accessor.updateBestResult(model);
  require(model.get_best_route_violation_list().empty(), "A worse result replaced a valid empty best result");
}

void testViaEnvironmentOwnership(irt::PinAccessor& accessor)
{
  auto& database = irt::DataManager::getInst().getDatabase();
  database.get_layer_via_master_list().resize(1);
  database.get_layer_via_master_list()[0].resize(1);
  auto& via = database.get_layer_via_master_list()[0][0];
  via.set_via_master_idx(0, 0);
  via.set_below_enclosure(irt::LayerRect(-2, -2, 2, 2, 0));
  via.set_above_enclosure(irt::LayerRect(-2, -2, 2, 2, 1));
  via.set_cut_layer_idx(0);
  via.get_cut_shape_list() = {irt::PlanarRect(-1, -1, 1, 1)};
  irt::Segment<irt::LayerCoord> segment(irt::LayerCoord(10, 10, 0), irt::LayerCoord(10, 10, 1));
  segment.set_via_master_idx(irt::ViaMasterIdx(0, 0));
  irt::EXTLayerRect patch;
  patch.set_real_rect(irt::PlanarRect(8, 8, 12, 12));
  patch.set_layer_idx(0);
  irt::PAModel model;
  model.get_pa_net_list().resize(2);
  auto& net = model.get_pa_net_list()[0];
  net.set_net_idx(0);
  net.get_pa_pin_list().resize(1);
  auto& pin = net.get_pa_pin_list()[0];
  pin.set_pin_idx(0);
  pin.set_owner_pa_box_id(irt::PABoxId(0, 0));
  pin.set_access_point(irt::AccessPoint(0, irt::LayerCoord(10, 10, 0)));
  pin.get_target_coord_list().emplace_back(10, 10, 1);
  irt::PABox box;
  box.set_pa_box_id(irt::PABoxId(0, 0));
  box.get_box_rect().set_real_rect(irt::PlanarRect(0, 0, 100, 100));
  box.get_net_access_point_map()[0].insert(&pin.get_access_point());
  box.get_net_pin_own_result_map()[1][0] = {segment};
  box.get_net_pin_own_patch_map()[1][0] = {patch};
  box.get_fixed_geometry().build({});
  accessor.initPATaskList(model, box);
  accessor.initPATaskResult(box);
  require(box.get_net_pin_env_result_map().at(1).at(0).contains(&box.get_net_pin_own_result_map().at(1).at(0)[0]),
          "Untasked owner segment is missing from the environment");
  // Two 4x4 enclosures and one 4x4 patch contribute area + 1 each; cuts do not contribute.
  require(accessor.getViaMasterCost(box, 0, segment) == 51, "Untasked owner geometry was charged more than once");
  auto neighbor_segment = segment;
  auto neighbor_patch = patch;
  box.get_net_pin_env_result_map()[2][0].insert(&neighbor_segment);
  box.get_net_pin_env_patch_map()[2][0].insert(&neighbor_patch);
  require(accessor.getViaMasterCost(box, 0, segment) == 102, "Distinct coincident neighbor geometry was merged");
  box.get_net_pin_env_result_map()[0][1].insert(&segment);
  box.get_net_pin_env_patch_map()[0][1].insert(&patch);
  require(accessor.getViaMasterCost(box, 0, segment) == 102, "Same-net environment changed via cost");
  box.get_pa_task_list().resize(2);
  box.get_pa_task_list()[1].set_task_idx(1);
  box.get_pa_task_list()[1].set_net_idx(3);
  box.get_curr_result().get_task_result_list().resize(2);
  box.get_curr_result().get_task_result_list()[1].get_segment_list() = {segment};
  box.get_curr_result().get_task_result_list()[1].get_patch_list() = {patch};
  require(accessor.getViaMasterCost(box, 0, segment) == 153, "Current task results are absent from via cost");
}

irt::Violation makePatchViolation(int32_t x, irt::ViolationType type, const std::set<int32_t>& nets)
{
  irt::Violation violation;
  violation.set_violation_shape(makeRect(x));
  violation.set_violation_type(type);
  violation.set_violation_net_set(nets);
  return violation;
}

void testPatchImprovement(irt::PinAccessor& accessor)
{
  irt::PABox box;
  box.get_box_rect().set_real_rect(irt::PlanarRect(0, 0, 100, 100));
  auto area = makePatchViolation(10, irt::ViolationType::kMinimumArea, {0});
  auto other_net_area = makePatchViolation(20, irt::ViolationType::kMinimumArea, {1});
  auto outside = makePatchViolation(200, irt::ViolationType::kMinimumArea, {0});
  auto shorting = makePatchViolation(10, irt::ViolationType::kMetalShort, {0, 1});
  auto spacing = makePatchViolation(10, irt::ViolationType::kParallelRunLengthSpacing, {0, 1});
  auto inter_net_area = makePatchViolation(10, irt::ViolationType::kMinimumArea, {0, 1});
  require(accessor.isBoxMinAreaViolation(box, other_net_area), "Box minimum-area eligibility unexpectedly filters by net");
  require(!accessor.isBoxMinAreaViolation(box, outside) && !accessor.isBoxMinAreaViolation(box, shorting), "Patch eligibility changed");
  require(accessor.isPatchImprovement(box, {}, {}), "Empty comparison changed");
  require(accessor.isPatchImprovement(box, {area}, {}), "Solved minimum-area violation was rejected");
  require(!accessor.isPatchImprovement(box, {area}, {area}), "Unchanged minimum-area violation was accepted");
  require(accessor.isPatchImprovement(box, {area, other_net_area}, {other_net_area}), "Partial area improvement was mistaken for requiring full resolution");
  require(!accessor.isPatchImprovement(box, {area}, {outside}), "A new environment violation was accepted");
  require(!accessor.isPatchImprovement(box, {area}, {shorting}), "A new rule violation was accepted");
  require(accessor.isPatchImprovement(box, {area, shorting}, {shorting}), "Unchanged environment violation blocked area improvement");
  require(!accessor.isPatchImprovement(box, {area, shorting}, {spacing}), "Environment counts were compared across rule types");
  require(!accessor.isPatchImprovement(box, {area, other_net_area}, {inter_net_area}), "A new inter-net violation was accepted");
  require(accessor.isPatchImprovement(box, {area, inter_net_area}, {inter_net_area}), "Unchanged inter-net count blocked area improvement");
  require(!accessor.isPatchImprovement(box, {}, {area}), "A new minimum-area violation was accepted");
}

void testPatchSelection(irt::PinAccessor& accessor)
{
  irt::DataManager::getInst().getDatabase().set_detection_distance(20);
  irt::PABox box;
  box.get_box_rect().set_real_rect(irt::PlanarRect(0, 0, 100, 100));
  box.get_fixed_geometry().build({});
  box.get_pa_task_list().resize(1);
  box.get_pa_task_list()[0].set_net_idx(0);
  box.get_pa_task_list()[0].set_task_idx(0);
  box.get_curr_result().get_task_result_list().resize(1);
  box.get_patch_state().set_curr_patch_task(&box.get_pa_task_list()[0]);
  auto area = makePatchViolation(10, irt::ViolationType::kMinimumArea, {0});
  box.get_patch_state().set_curr_patch_violation(area);
  box.get_patch_state().get_routing_patch_list() = {makeRect(10)};
  auto* accepted_storage = box.get_patch_state().get_routing_patch_list().data();
  checked_task_list.clear();
  env_storage_list.clear();
  std::vector<irt::PAPatch> candidates;
  auto selection = accessor.selectPatch(box, candidates);
  require(selection.type == irt::PAPatchSelectionType::kNone && selection.candidate_idx == -1, "Empty candidate selection changed");
  candidates.emplace_back(makeRect(10).get_real_rect(), 0);
  selection = accessor.selectPatch(box, candidates);
  require(selection.type == irt::PAPatchSelectionType::kSingleCandidate && selection.candidate_idx == 0 && checked_task_list.empty(),
          "Single-candidate policy unexpectedly invokes DRC");
  candidates.emplace_back(makeRect(20).get_real_rect(), 0);
  for (int32_t scenario = 0; scenario < 3; scenario++) {
    checked_task_list.clear();
    env_storage_list.clear();
    candidates[0].get_patch() = makeRect(scenario == 2 ? 200 : 10);
    violation_response_list = {{area}, {area}, scenario == 1 ? std::vector<irt::Violation>{area} : std::vector<irt::Violation>{}};
    selection = accessor.selectPatch(box, candidates);
    require(violation_response_list.empty() && checked_task_list.size() == 3, "Patch selection changed the check sequence");
    require(selection.type == (scenario == 1 ? irt::PAPatchSelectionType::kBestEffort : irt::PAPatchSelectionType::kImproved)
                && selection.candidate_idx == (scenario == 1 ? 0 : 1),
            "Patch selection lost first-improvement or best-effort policy");
    require(box.get_patch_state().get_routing_patch_list().size() == 1 && box.get_patch_state().get_routing_patch_list().data() == accepted_storage,
            "Selection mutated accepted-patch storage while DRC borrowed it");
    const auto& origin = checked_task_list[0].get_net_patch_map().at(0);
    const auto& first = checked_task_list[1].get_net_patch_map().at(0);
    const auto& second = checked_task_list[2].get_net_patch_map().at(0);
    require(origin == std::vector<irt::EXTLayerRect*>({accepted_storage}), "Origin check contains a candidate");
    require(first.size() == (scenario == 2 ? 1 : 2), "Candidate spatial filtering changed");
    require(second == std::vector<irt::EXTLayerRect*>({accepted_storage, &candidates[1].get_patch()}), "A rejected candidate leaked into the next check");
  }
  checked_task_list.clear();
  env_storage_list.clear();
  violation_response_list = {{area}, {}};
  candidates[0].get_patch() = makeRect(10);
  selection = accessor.selectPatch(box, candidates);
  require(selection.type == irt::PAPatchSelectionType::kImproved && selection.candidate_idx == 0 && violation_response_list.empty()
              && checked_task_list.size() == 2,
          "First improvement did not stop checking later candidates");
  checked_task_list.clear();
  env_storage_list.clear();
}

void testBoxPublicationFlow(irt::PinAccessor& accessor)
{
  irt::PAModel model;
  irt::PAPin pin;
  pin.set_pin_idx(2);
  irt::PABox box;
  box.set_initial_routing(false);
  box.get_pa_task_list().resize(1);
  box.get_pa_task_list()[0].set_net_idx(1);
  box.get_pa_task_list()[0].set_task_idx(0);
  box.get_pa_task_list()[0].set_pa_pin(&pin);
  box.get_task_order_list() = {0};
  box.get_curr_result().get_task_result_list().resize(1);
  auto& result = box.get_curr_result().get_task_result_list()[0];
  result.get_segment_list().emplace_back(irt::LayerCoord(10, 10, 0), irt::LayerCoord(20, 10, 0));
  result.get_patch_list() = {makeRect(10)};
  result.set_access_point(irt::AccessPoint(2, irt::LayerCoord(10, 10, 0)));
  const auto* imported_segment_data = result.get_segment_list().data();
  const auto* imported_patch_data = result.get_patch_list().data();
  accessor.routePABox(model, box);
  require(box.get_route_violation_list().empty(), "Unchanged box acquired a violation");
  require(!box.get_dirty() && box.get_pa_task_list().empty() && box.get_curr_result().get_task_result_list().empty(),
          "Unchanged box was marked dirty or retained temporary task state");
  require(box.get_net_pin_own_patch_map().at(1).at(2).size() == 1 && pin.get_access_point().getRealLayerCoord() == irt::LayerCoord(10, 10, 0),
          "Box publication lost its imported result or access point");
  require(box.get_net_pin_own_result_map().at(1).at(2).data() == imported_segment_data
              && box.get_net_pin_own_patch_map().at(1).at(2).data() == imported_patch_data,
          "Publishing an unchanged box copied its imported geometry");
  auto violation = makePatchViolation(10, irt::ViolationType::kMinimumArea, {1});
  box.get_curr_result().get_route_violation_list() = {violation};
  const auto* imported_violation_data = box.get_curr_result().get_route_violation_list().data();
  accessor.routePABox(model, box);
  require(box.get_route_violation_list() == std::vector<irt::Violation>({violation}) && box.get_dirty(),
          "A taskless box lost its violation or dirty status during publication");
  require(box.get_route_violation_list().data() == imported_violation_data, "Publishing a taskless box copied its violations");
  require(box.get_curr_result().get_route_violation_list().empty(), "Box publication retained temporary violations");
}

void testBoxViolationPublication(irt::PinAccessor& accessor)
{
  irt::PAModel model;
  model.get_pa_box_map().init(3, 1);
  auto existing_violation = makePatchViolation(10, irt::ViolationType::kMinimumArea, {1});
  auto routed_violation = makePatchViolation(20, irt::ViolationType::kMinimumArea, {2});
  auto inactive_violation = makePatchViolation(30, irt::ViolationType::kMinimumArea, {3});
  model.get_route_violation_list() = {existing_violation};
  model.get_pa_box_map()[0][0].get_route_violation_list() = {existing_violation, routed_violation};
  model.get_pa_box_map()[1][0].get_route_violation_list() = {inactive_violation};
  model.get_pa_box_map()[2][0].get_route_violation_list() = {routed_violation};

  accessor.updateRouteViolation(model, {irt::PABoxId(2, 0), irt::PABoxId(0, 0)});
  std::set<irt::Violation, irt::CmpViolation> expected_violation_set = {existing_violation, routed_violation};
  require(model.get_route_violation_list() == std::vector<irt::Violation>(expected_violation_set.begin(), expected_violation_set.end()),
          "Box violation publication lost existing violations, retained duplicates or included an inactive box");
  require(model.get_pa_box_map()[0][0].get_route_violation_list().empty() && model.get_pa_box_map()[2][0].get_route_violation_list().empty(),
          "Box violation publication retained consumed violations");
  require(model.get_pa_box_map()[1][0].get_route_violation_list() == std::vector<irt::Violation>({inactive_violation}),
          "Box violation publication consumed an inactive box");
}

}  // namespace

extern "C" std::vector<irt::Violation> __wrap__ZN3irt9DRCEngine16getViolationListERNS_6DETaskE(irt::DRCEngine*, irt::DETask& de_task)
{
  env_storage_list.push_back(de_task.get_env_shape_list().data());
  checked_task_list.push_back(de_task);
  if (!violation_response_list.empty()) {
    auto violations = std::move(violation_response_list.front());
    violation_response_list.pop_front();
    return violations;
  }
  return {};
}

int main()
{
  irt::DataManager::initInst();
  irt::DRCEngine::initInst();
  try {
    irt::PinAccessor accessor;
    testFixedGeometry(accessor);
    testFixedGeometryErrors();
    testBoxViews(accessor);
    testModelViews(accessor);
    testPatchInput(accessor);
    testFixedOverlapPoly(accessor);
    testEnvironmentUpdates(accessor);
    testPathOutcome(accessor);
    testRouteTaskContract(accessor);
    testTaskIdentity(accessor);
    testTaskResultFlow(accessor);
    testEmptyModelSnapshot(accessor);
    testViaEnvironmentOwnership(accessor);
    testPatchImprovement(accessor);
    testPatchSelection(accessor);
    testBoxPublicationFlow(accessor);
    testBoxViolationPublication(accessor);
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
