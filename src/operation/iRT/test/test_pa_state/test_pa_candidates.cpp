#include <array>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>

#include "PinAccessor.hpp"

namespace {

void require(bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void initDatabase()
{
  auto& database = irt::DataManager::getInst().getDatabase();
  database.get_die().set_real_rect(irt::PlanarRect(-250, -250, 250, 250));
  database.get_routing_layer_list().resize(3);
  std::vector<int32_t> tracks;
  for (int32_t coord = -300; coord <= 300; coord += 30) {
    tracks.push_back(coord);
  }
  for (int32_t layer_idx = 0; layer_idx < 3; layer_idx++) {
    auto& layer = database.get_routing_layer_list()[layer_idx];
    layer.set_layer_idx(layer_idx);
    layer.set_min_width(6 + 2 * layer_idx);
    layer.get_track_axis().set_x_grid_list(irt::Utility::makeScaleGridList(tracks));
    layer.get_track_axis().set_y_grid_list(irt::Utility::makeScaleGridList(tracks));
  }
  database.get_layer_via_master_list().resize(2);
  for (int32_t layer_idx = 0; layer_idx < 2; layer_idx++) {
    auto& masters = database.get_layer_via_master_list()[layer_idx];
    masters.resize(3);
    for (int32_t via_idx = 0; via_idx < 3; via_idx++) {
      masters[via_idx].set_via_master_idx(layer_idx, via_idx);
    }
  }
}

void appendSignature(uint64_t& signature, int64_t value)
{
  signature ^= static_cast<uint64_t>(value);
  signature *= UINT64_C(1099511628211);
}

uint64_t testCandidates(irt::PinAccessor& accessor)
{
  uint64_t signature = UINT64_C(14695981039346656037);
  std::mt19937 random(83);
  const std::array<int32_t, 4> grid_list = {1, 2, 3, 5};
  for (int32_t case_idx = 0; case_idx < 1200; case_idx++) {
    int32_t grid = grid_list[case_idx % grid_list.size()];
    irt::DataManager::getInst().getDatabase().set_manufacture_grid(grid);
    irt::PAModel model;
    model.set_pa_com_param(irt::PAComParam(1 + case_idx % 20, 2, 1 + case_idx % 8));
    std::vector<irt::PALegalShape> legal_shapes;
    for (int32_t shape_idx = 0; shape_idx <= case_idx % 6; shape_idx++) {
      int32_t x = static_cast<int32_t>(random() % 550) - 275;
      int32_t y = static_cast<int32_t>(random() % 550) - 275;
      int32_t width = random() % 110;
      int32_t height = random() % 110;
      int32_t layer_idx = random() % 3;
      irt::ViaMasterIdx via_idx;
      if (case_idx % 4 != 0 && (case_idx % 4 != 3 || shape_idx % 2 == 0)) {
        via_idx = irt::ViaMasterIdx(layer_idx == 2 ? 1 : 0, shape_idx % 3);
      }
      legal_shapes.push_back({irt::LayerRect(x, y, x + width, y + height, layer_idx), via_idx});
      if (shape_idx % 2 == 0) {
        legal_shapes.push_back(legal_shapes.back());
      }
    }
    auto points = accessor.getAccessPointList(model, 42, legal_shapes);
    std::set<irt::LayerCoord, irt::CmpLayerCoordByXASC> coords;
    appendSignature(signature, case_idx);
    appendSignature(signature, points.size());
    for (auto& point : points) {
      require(point.get_pin_idx() == 42, "A candidate lost its pin identity");
      require(coords.insert(point.getRealLayerCoord()).second, "Duplicate access point coordinate");
      require(point.get_real_x() % grid == 0 && point.get_real_y() % grid == 0, "Candidate is off manufacturing grid");
      require(point.get_init_cost() == 0 || point.get_init_cost() == 75 || point.get_init_cost() == 150, "Candidate track cost changed");
      appendSignature(signature, point.get_layer_idx());
      appendSignature(signature, point.get_real_x());
      appendSignature(signature, point.get_real_y());
      appendSignature(signature, static_cast<int64_t>(point.get_init_cost()));
      appendSignature(signature, point.get_candidate_via_list().size());
      for (const auto& via_idx : point.get_candidate_via_list()) {
        appendSignature(signature, via_idx.get_below_layer_idx());
        appendSignature(signature, via_idx.get_via_idx());
      }
    }
  }
  return signature;
}

void testCandidateBoundaries(irt::PinAccessor& accessor)
{
  irt::DataManager::getInst().getDatabase().set_manufacture_grid(5);
  irt::PAModel model;
  model.set_pa_com_param(irt::PAComParam(20, 2, 8));
  std::vector<irt::PALegalShape> shapes;
  require(accessor.getAccessPointList(model, 0, shapes).empty(), "Empty geometry produced a candidate");
  shapes = {{irt::LayerRect(1, 1, 4, 4, 0), irt::ViaMasterIdx()}};
  require(accessor.getAccessPointList(model, 0, shapes).empty(), "A shape with no grid point produced a candidate");
  shapes = {{irt::LayerRect(0, 0, 0, 0, 0), irt::ViaMasterIdx()}};
  auto points = accessor.getAccessPointList(model, 0, shapes);
  require(points.size() == 1 && points[0].getRealLayerCoord() == irt::LayerCoord(0, 0, 0), "Point geometry changed");
  require(points[0].get_init_cost() == 0, "An on-track point acquired a penalty");
  shapes = {{irt::LayerRect(0, 0, 0, 0, 0), irt::ViaMasterIdx(0, 2)},
            {irt::LayerRect(0, 0, 0, 0, 0), irt::ViaMasterIdx(0, 0)},
            {irt::LayerRect(0, 0, 0, 0, 0), irt::ViaMasterIdx(0, 2)}};
  points = accessor.getAccessPointList(model, 0, shapes);
  require(points.size() == 1, "Via alternatives were not merged at one coordinate");
  require(points[0].get_candidate_via_list() == std::vector<irt::ViaMasterIdx>({irt::ViaMasterIdx(0, 0), irt::ViaMasterIdx(0, 2)}),
          "Via alternatives lost canonical order or uniqueness");
}

void testIterationParameters(irt::PinAccessor& accessor)
{
  auto parameters = accessor.getPAIterParamList();
  require(parameters.size() == 6, "Iteration count changed");
  for (size_t i = 0; i < parameters.size(); i++) {
    auto& param = parameters[i];
    int32_t multiplier = i < 3 ? 1 : 2;
    require(param.get_prefer_wire_unit() == 1 && param.get_non_prefer_wire_unit() == 2.5 && param.get_via_unit() == 150, "Iteration wire/via costs changed");
    require(
        param.get_fixed_rect_unit() == multiplier * 300 && param.get_routed_rect_unit() == multiplier * 150 && param.get_violation_unit() == multiplier * 300,
        "Iteration environment penalties changed");
    require(param.get_size() == 3 && param.get_offset() == static_cast<int32_t>(i % 3) && param.get_schedule_interval() == 3, "Iteration box tiling changed");
    require(param.get_max_routed_times() == (i == 0 ? 20 : (i < 3 ? 80 : 100)) && param.get_max_candidate_patch_num() == 20,
            "Iteration routing or patch budget changed");
  }
}

void testTargets(irt::PinAccessor& accessor)
{
  auto& database = irt::DataManager::getInst().getDatabase();
  database.set_detection_distance(37);
  const std::array<std::pair<int32_t, int32_t>, 3> bounds = {{{0, 1}, {0, 2}, {1, 2}}};
  const int32_t expected_layer[2][3][3] = {{{0, 1, 1}, {0, 1, 2}, {1, 1, 2}}, {{1, 0, 0}, {1, 2, 1}, {2, 2, 1}}};
  for (int32_t core = 0; core < 2; core++) {
    for (size_t bound_idx = 0; bound_idx < bounds.size(); bound_idx++) {
      auto& config = irt::DataManager::getInst().getConfig();
      config.bottom_routing_layer_idx = bounds[bound_idx].first;
      config.top_routing_layer_idx = bounds[bound_idx].second;
      irt::PAPin pin;
      pin.set_is_core(core != 0);
      std::set<irt::LayerCoord, irt::CmpLayerCoordByXASC> expected;
      for (int32_t layer_idx = 0; layer_idx < 3; layer_idx++) {
        int32_t target_layer = expected_layer[core][bound_idx][layer_idx];
        require(accessor.getTargetLayerIdx(core != 0, layer_idx) == target_layer, "Target layer policy changed");
        for (int32_t offset : {0, 10, 10, 37}) {
          pin.get_access_point_list().emplace_back(0, irt::LayerCoord(offset, -offset, layer_idx));
          auto& layer = database.get_routing_layer_list()[layer_idx];
          auto region = irt::Utility::getEnlargedRect(irt::PlanarCoord(offset, -offset), 37);
          for (int32_t x : irt::Utility::getScaleList(region.get_ll_x(), region.get_ur_x(), layer.getXTrackGridList())) {
            for (int32_t y : irt::Utility::getScaleList(region.get_ll_y(), region.get_ur_y(), layer.getYTrackGridList())) {
              expected.emplace(x, y, target_layer);
            }
          }
        }
      }
      pin.get_target_coord_list().emplace_back(999, 999, 0);
      accessor.buildPinTargetCoordList(pin);
      if (pin.get_target_coord_list() != std::vector<irt::LayerCoord>(expected.begin(), expected.end())) {
        std::cerr << "Target mismatch: core=" << core << " bounds=" << bound_idx << " expected=" << expected.size()
                  << " actual=" << pin.get_target_coord_list().size() << '\n';
        for (const auto& coord : pin.get_target_coord_list()) {
          if (!expected.contains(coord)) {
            std::cerr << "Unexpected target: " << coord.get_x() << ',' << coord.get_y() << ',' << coord.get_layer_idx() << '\n';
          }
        }
      }
      require(pin.get_target_coord_list() == std::vector<irt::LayerCoord>(expected.begin(), expected.end()),
              "Target coordinates differ from the set-based scan");
      accessor.buildPinTargetCoordList(pin);
      require(pin.get_target_coord_list() == std::vector<irt::LayerCoord>(expected.begin(), expected.end()), "Target rebuild retained stale coordinates");
    }
  }
}

void testTargetWindows(irt::PinAccessor& accessor)
{
  auto& database = irt::DataManager::getInst().getDatabase();
  auto& config = irt::DataManager::getInst().getConfig();
  auto saved_axis = database.get_routing_layer_list()[1].get_track_axis();
  std::vector<int32_t> shifted_x;
  std::vector<int32_t> shifted_y;
  for (int32_t coord = -300; coord <= 300; coord += 30) {
    shifted_x.push_back(coord + 10);
    shifted_y.push_back(coord + 15);
  }
  database.get_routing_layer_list()[1].get_track_axis().set_x_grid_list(irt::Utility::makeScaleGridList(shifted_x));
  database.get_routing_layer_list()[1].get_track_axis().set_y_grid_list(irt::Utility::makeScaleGridList(shifted_y));
  const int32_t expected_layer[2][3][3] = {{{0, 1, 1}, {0, 1, 2}, {1, 1, 2}}, {{1, 0, 0}, {1, 2, 1}, {2, 2, 1}}};
  const std::array<std::pair<int32_t, int32_t>, 3> bounds = {{{0, 1}, {0, 2}, {1, 2}}};
  std::mt19937 random(101);
  for (int32_t case_idx = 0; case_idx < 600; case_idx++) {
    int32_t core = case_idx % 2;
    size_t bound_idx = case_idx % bounds.size();
    config.bottom_routing_layer_idx = bounds[bound_idx].first;
    config.top_routing_layer_idx = bounds[bound_idx].second;
    int32_t distance = case_idx % 121;
    database.set_detection_distance(distance);
    irt::PAPin pin;
    pin.set_is_core(core != 0);
    std::set<irt::LayerCoord, irt::CmpLayerCoordByXASC> expected;
    for (int32_t i = 0; i < case_idx % 41; i++) {
      int32_t x = static_cast<int32_t>(random() % 480) - 240;
      int32_t y = static_cast<int32_t>(random() % 480) - 240;
      int32_t layer_idx = random() % 3;
      pin.get_access_point_list().emplace_back(0, irt::LayerCoord(x, y, layer_idx));
      if (i % 3 == 0) {
        pin.get_access_point_list().push_back(pin.get_access_point_list().back());
      }
      auto region = irt::Utility::getEnlargedRect(irt::PlanarCoord(x, y), distance);
      auto& layer = database.get_routing_layer_list()[layer_idx];
      for (int32_t target_x : irt::Utility::getScaleList(region.get_ll_x(), region.get_ur_x(), layer.getXTrackGridList())) {
        for (int32_t target_y : irt::Utility::getScaleList(region.get_ll_y(), region.get_ur_y(), layer.getYTrackGridList())) {
          expected.emplace(target_x, target_y, expected_layer[core][bound_idx][layer_idx]);
        }
      }
    }
    accessor.buildPinTargetCoordList(pin);
    require(pin.get_target_coord_list() == std::vector<irt::LayerCoord>(expected.begin(), expected.end()), "Merged target windows differ from AP scans");
    pin.get_access_point_list().clear();
    accessor.buildPinTargetCoordList(pin);
    require(pin.get_target_coord_list().empty(), "Empty AP refresh retained target coordinates");
  }
  database.get_routing_layer_list()[1].get_track_axis() = saved_axis;
}

irt::EXTLayerRect makeLegalRect(const irt::LayerRect& shape)
{
  irt::EXTLayerRect rect;
  rect.set_real_rect(shape.get_rect());
  rect.set_layer_idx(shape.get_layer_idx());
  return rect;
}

void initLegalDatabase()
{
  auto& database = irt::DataManager::getInst().getDatabase();
  database.get_die().set_real_rect(irt::PlanarRect(0, 0, 600, 600));
  database.set_detection_distance(30);
  std::vector<int32_t> coords;
  for (int32_t coord = 0; coord <= 600; coord += 60) {
    coords.push_back(coord);
  }
  database.get_gcell_axis().set_x_grid_list(irt::Utility::makeScaleGridList(coords));
  database.get_gcell_axis().set_y_grid_list(irt::Utility::makeScaleGridList(coords));
  for (int32_t layer_idx = 0; layer_idx < 3; layer_idx++) {
    auto& layer = database.get_routing_layer_list()[layer_idx];
    layer.set_prefer_direction(layer_idx == 1 ? irt::Direction::kVertical : irt::Direction::kHorizontal);
    layer.set_eol_spacing(6 + layer_idx);
    layer.set_eol_within(3);
    layer.get_prl_spacing_table().get_width_list() = {0, 20};
    layer.get_prl_spacing_table().get_width_parallel_length_map().init(2, 1, 4 + layer_idx);
    layer.get_prl_spacing_table().get_width_parallel_length_map()[1][0] += 3;
    database.get_layer_enclosure_map()[layer_idx] = irt::PlanarRect(-2, -2, 2, 2);
  }
  for (int32_t layer_idx = 0; layer_idx < 2; layer_idx++) {
    for (int32_t via_idx = 0; via_idx < 3; via_idx++) {
      auto& via = database.get_layer_via_master_list()[layer_idx][via_idx];
      via.set_below_enclosure(irt::LayerRect(-2 - via_idx, -3, 2 + via_idx, 3, layer_idx));
      via.set_above_enclosure(irt::LayerRect(-3, -4 - via_idx, 3, 4 + via_idx, layer_idx + 1));
    }
  }
}

uint64_t testLegalShapes(irt::PinAccessor& accessor, int32_t& rejected_num)
{
  initLegalDatabase();
  auto& database = irt::DataManager::getInst().getDatabase();
  auto& trees = database.get_type_layer_fixed_rect_rtree_map();
  std::vector<irt::EXTLayerRect> fixed_shapes(60);
  std::mt19937 random(97);
  uint64_t signature = UINT64_C(14695981039346656037);
  rejected_num = 0;
  for (int32_t case_idx = 0; case_idx < 600; case_idx++) {
    trees = {};
    for (size_t i = 0; i < fixed_shapes.size(); i++) {
      int32_t x = 50 + random() % 470;
      int32_t y = 50 + random() % 470;
      int32_t width = 1 + random() % 70;
      int32_t height = 1 + random() % 70;
      fixed_shapes[i] = makeLegalRect(irt::LayerRect(x, y, x + width, y + height, i % 3));
      bool is_routing = i % 4 != 0;
      int32_t net_idx = static_cast<int32_t>(i % 4) - 1;
      trees[is_routing][i % 3].insert({irt::Utility::convertToBGRectInt(fixed_shapes[i].get_real_rect()), {net_idx, &fixed_shapes[i]}});
    }
    irt::PAPin pin;
    pin.set_is_core(case_idx % 2 == 0);
    for (int32_t i = 0; i <= case_idx % 4; i++) {
      int32_t x = 100 + random() % 350;
      int32_t y = 100 + random() % 350;
      int32_t width = 1 + random() % 90;
      int32_t height = 1 + random() % 90;
      int32_t layer_idx = random() % 3;
      auto shape = makeLegalRect(irt::LayerRect(x, y, x + width, y + height, layer_idx));
      pin.get_routing_shape_list().push_back(shape);
      if (case_idx % 7 == 0) {
        pin.get_routing_shape_list().push_back(shape);
      }
    }
    std::map<int32_t, std::vector<irt::ViaMaster*>> selected;
    if (case_idx % 3 != 0) {
      for (auto& masters : database.get_layer_via_master_list()) {
        for (auto& via : masters) {
          if (case_idx % 3 == 1 && via.get_via_master_idx().get_via_idx() != 0) {
            continue;
          }
          selected[via.get_below_enclosure().get_layer_idx()].push_back(&via);
          selected[via.get_above_enclosure().get_layer_idx()].push_back(&via);
        }
      }
    }
    appendSignature(signature, case_idx);
    std::vector<irt::PALegalShape> shapes;
    try {
      shapes = accessor.getLegalShapeList(0, pin, selected);
    } catch (const std::runtime_error& error) {
      // The shared Boost clipping utility rejects some valid integer inputs on precision loss.
      std::string message(error.what());
      require(message == "Exceeding the error range of a double!" || message == "The segment is oblique!", "Unexpected legal-geometry rejection");
      std::cout << "Rejected legal geometry case: " << case_idx << '\n';
      appendSignature(signature, -1);
      rejected_num++;
      continue;
    }
    appendSignature(signature, shapes.size());
    for (const auto& shape : shapes) {
      appendSignature(signature, shape.shape.get_layer_idx());
      appendSignature(signature, shape.shape.get_ll_x());
      appendSignature(signature, shape.shape.get_ll_y());
      appendSignature(signature, shape.shape.get_ur_x());
      appendSignature(signature, shape.shape.get_ur_y());
      appendSignature(signature, shape.via_master_idx.get_below_layer_idx());
      appendSignature(signature, shape.via_master_idx.get_via_idx());
    }
  }
  trees = {};
  return signature;
}

void testLegalBoundaries(irt::PinAccessor& accessor)
{
  auto& database = irt::DataManager::getInst().getDatabase();
  auto& trees = database.get_type_layer_fixed_rect_rtree_map();
  irt::PAPin pin;
  auto thin_shape = makeLegalRect(irt::LayerRect(100, 100, 102, 102, 0));
  pin.get_routing_shape_list() = {thin_shape};
  auto shapes = accessor.getLegalShapeList(0, pin, {});
  require(shapes.size() == 1 && shapes[0].shape == thin_shape.getRealLayerRect(), "Default candidates lost original-shape fallback");
  std::map<int32_t, std::vector<irt::ViaMaster*>> selected = {{0, {&database.get_layer_via_master_list()[0][0]}}};
  require(accessor.getLegalShapeList(0, pin, selected).empty(), "Via candidates silently used the planar fallback");

  pin.get_routing_shape_list() = {makeLegalRect(irt::LayerRect(200, 200, 230, 230, 0))};
  auto unobstructed = accessor.getLegalShapeList(0, pin, {});
  auto obstacle = makeLegalRect(irt::LayerRect(50, 50, 550, 550, 0));
  trees[true][0].insert({irt::Utility::convertToBGRectInt(obstacle.get_real_rect()), {-1, &obstacle}});
  shapes = accessor.getLegalShapeList(0, pin, {});
  require(shapes.size() == unobstructed.size(), "Fully blocked candidates lost best-effort geometry");
  for (size_t i = 0; i < shapes.size(); i++) {
    require(shapes[i].shape == unobstructed[i].shape, "Fully blocked candidates changed the last nonempty geometry");
  }
  trees = {};
  for (int32_t layer_idx = 1; layer_idx < 3; layer_idx++) {
    pin.get_routing_shape_list().push_back(makeLegalRect(irt::LayerRect(200, 200, 230, 230, layer_idx)));
  }
  pin.set_is_core(false);
  require(accessor.getLegalShapeList(0, pin, {}).front().shape.get_layer_idx() == 1, "Noncore layer priority changed");
  pin.set_is_core(true);
  require(accessor.getLegalShapeList(0, pin, {}).front().shape.get_layer_idx() == 2, "Core layer priority changed");
}

uint64_t testPatchCandidates(irt::PinAccessor& accessor)
{
  initLegalDatabase();
  auto& database = irt::DataManager::getInst().getDatabase();
  uint64_t signature = UINT64_C(14695981039346656037);
  std::mt19937 random(137);
  for (int32_t case_idx = 0; case_idx < 400; case_idx++) {
    database.set_manufacture_grid(1 + case_idx % 5);
    int32_t layer_idx = case_idx % 3;
    auto& layer = database.get_routing_layer_list()[layer_idx];
    layer.set_min_area(600 + static_cast<int32_t>(random() % 1200));
    irt::PABox box;
    irt::PAIterParam param;
    param.set_max_candidate_patch_num(1 + case_idx % 24);
    param.set_fixed_rect_unit(7);
    param.set_routed_rect_unit(3);
    param.set_violation_unit(11);
    box.set_pa_iter_param(&param);
    box.get_box_rect().set_real_rect(database.get_die().get_real_rect());
    irt::PATask task;
    task.set_net_idx(0);
    box.get_patch_state().set_curr_patch_task(&task);
    box.get_patch_state().get_curr_patch_violation().set_violation_shape(makeLegalRect(irt::LayerRect(0, 0, 600, 600, layer_idx)));
    box.get_layer_shadow_map().resize(3);
    auto& shadow = box.get_layer_shadow_map()[layer_idx];
    int32_t x = case_idx % 7 == 0 ? 0 : 280;
    int32_t y = case_idx % 11 == 0 ? 590 : 280;
    if (case_idx % 4 == 0) {
      shadow.addFixedRect(1, database.get_die().get_real_rect());
    } else if (case_idx % 4 == 1) {
      shadow.addFixedRect(1, irt::PlanarRect(x - 10, y - 10, x + 20, y + 30));
      shadow.addRoutedRect(2, irt::PlanarRect(x + 5, y - 20, x + 40, y + 10));
      shadow.addViolation(irt::PlanarRect(x - 20, y + 10, x + 20, y + 20));
    }
    shadow.addFixedRect(0, database.get_die().get_real_rect());
    shadow.buildFixedRectRTree();
    GTLPolySetInt poly_set;
    poly_set += irt::Utility::convertToGTLRectInt(irt::PlanarRect(x, y, x + 10, y + 10));
    if (case_idx % 3 != 0) {
      poly_set += irt::Utility::convertToGTLRectInt(irt::PlanarRect(x, y, x + 30, y + 5));
    }
    if (case_idx % 3 == 2) {
      poly_set += irt::Utility::convertToGTLRectInt(irt::PlanarRect(x + 20, y, x + 30, y + 10));
    }
    std::vector<GTLPolyInt> polygons;
    poly_set.get_polygons(polygons);
    require(polygons.size() == 1, "Patch fixture must contain one connected polygon");
    if (case_idx % 17 == 0) {
      layer.set_min_area(static_cast<int32_t>(gtl::area(polygons.front())));
    }
    auto candidates = accessor.getCandidatePatchList(box, polygons.front());
    require(candidates.size() <= static_cast<size_t>(param.get_max_candidate_patch_num()), "Patch candidate budget exceeded");
    require(candidates.empty() == (case_idx % 17 == 0), "Minimum-area eligibility changed");
    appendSignature(signature, case_idx);
    appendSignature(signature, candidates.size());
    std::set<irt::PlanarRect, irt::CmpPlanarRectByXASC> unique_rects;
    for (auto& candidate : candidates) {
      const auto& rect = candidate.get_patch().get_real_rect();
      require(unique_rects.insert(rect).second, "Duplicate patch candidate");
      require(irt::Utility::isInside(database.get_die().get_real_rect(), rect), "Patch candidate escaped the die");
      appendSignature(signature, rect.get_ll_x());
      appendSignature(signature, rect.get_ll_y());
      appendSignature(signature, rect.get_ur_x());
      appendSignature(signature, rect.get_ur_y());
      appendSignature(signature, candidate.get_patch().get_layer_idx());
      appendSignature(signature, candidate.get_fixed_rect_cost());
      appendSignature(signature, candidate.get_routed_rect_cost());
      appendSignature(signature, candidate.get_violation_cost());
      appendSignature(signature, static_cast<int32_t>(candidate.get_direction()));
      appendSignature(signature, candidate.get_overlap_area());
      const auto& grid = candidate.get_patch().get_grid_rect();
      appendSignature(signature, grid.get_ll_x());
      appendSignature(signature, grid.get_ll_y());
      appendSignature(signature, grid.get_ur_x());
      appendSignature(signature, grid.get_ur_y());
    }
  }
  return signature;
}

void testPatchSampling(irt::PinAccessor& accessor)
{
  require(accessor.getPatchSampleCoordList(-20, 9, 3, 4, true) == std::vector<int32_t>({-20, -8, 4, 9}),
          "Initial sampling lost its exact off-grid endpoint");
  require(accessor.getPatchSampleCoordList(-20, 9, 3, 2, false) == std::vector<int32_t>({-14, -2}), "Refinement did not sample the previous midpoints");
  require(accessor.getPatchSampleCoordList(-20, 9, 3, 1, false) == std::vector<int32_t>({-17, -11, -5, 1}), "Refinement sampled the final position twice");
  require(accessor.getPatchSampleCoordList(5, 5, 1, 1, true) == std::vector<int32_t>({5}), "Single-position sampling changed");
  require(accessor.getPatchSampleCoordList(0, 8, 1, 4, true) == std::vector<int32_t>({0, 4, 8}), "An on-stride endpoint was duplicated");
}

void testLegalBorderRejection(irt::PinAccessor& accessor)
{
  initLegalDatabase();
  irt::PAPin pin;
  pin.get_routing_shape_list().push_back(makeLegalRect(irt::LayerRect(700, 700, 730, 730, 0)));
  bool had_throw_policy = std::getenv("ECC_LOGGER_THROW_ON_ERROR") != nullptr;
  if (!had_throw_policy) {
    require(setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1) == 0, "Cannot enable border rejection exceptions");
  }
  bool rejected = false;
  try {
    accessor.getLegalShapeList(0, pin, {});
  } catch (const std::runtime_error& error) {
    rejected = std::string(error.what()).find("This shape is outside the border!") != std::string::npos;
  }
  if (!had_throw_policy) {
    require(unsetenv("ECC_LOGGER_THROW_ON_ERROR") == 0, "Cannot restore border rejection policy");
  }
  require(rejected, "Real-coordinate legal geometry lost its border validation");
}

uint64_t testRouteTasks(irt::PinAccessor& accessor)
{
  initLegalDatabase();
  auto& database = irt::DataManager::getInst().getDatabase();
  database.set_manufacture_grid(1);
  auto& config = irt::DataManager::getInst().getConfig();
  config.bottom_routing_layer_idx = 0;
  config.top_routing_layer_idx = 2;
  uint64_t signature = UINT64_C(14695981039346656037);
  std::mt19937 random(149);
  for (int32_t case_idx = 0; case_idx < 120; case_idx++) {
    irt::PABox box;
    irt::PAPin pin;
    pin.set_pin_idx(0);
    irt::PAIterParam param = accessor.getPAIterParamList().front();
    box.set_pa_iter_param(&param);
    box.get_box_rect().set_real_rect(irt::PlanarRect(30, 30, 150, 150));
    box.get_fixed_geometry().build({});
    box.get_pa_task_list().resize(1);
    box.get_curr_result().get_task_result_list().resize(1);
    box.get_task_order_list() = {0};
    auto& task = box.get_pa_task_list().front();
    task.set_net_idx(0);
    task.set_task_idx(0);
    task.set_pa_pin(&pin);
    std::vector<irt::PAGroup> groups(2);
    groups.front().set_is_target(false);
    groups.back().set_is_target(true);
    std::array<irt::AccessPoint, 2> points;
    for (size_t i = 0; i < points.size(); i++) {
      int32_t x = 60 + static_cast<int32_t>(random() % 3) * 30;
      int32_t y = 60 + static_cast<int32_t>(random() % 3) * 30;
      points[i] = irt::AccessPoint(0, irt::LayerCoord(x, y, 0));
      points[i].set_init_cost(i * (case_idx % 4) * 75);
      if (case_idx % 2 == 0) {
        points[i].set_candidate_via_list({irt::ViaMasterIdx(0, 1), irt::ViaMasterIdx(0, 2)});
      }
      box.get_net_access_point_map()[0].insert(&points[i]);
    }
    for (auto* point : box.get_net_access_point_map()[0]) {
      groups.front().get_coord_list().push_back(point->getRealLayerCoord());
    }
    for (int32_t i = 0; i < 3; i++) {
      int32_t x = 60 + static_cast<int32_t>(random() % 3) * 30;
      int32_t y = 60 + static_cast<int32_t>(random() % 3) * 30;
      groups.back().get_coord_list().emplace_back(x, y, case_idx % 3);
    }
    if (case_idx % 10 == 0) {
      groups.back().get_coord_list() = {groups.front().get_coord_list().front()};
    }
    task.set_pa_group_list(std::move(groups));
    accessor.buildBoxTrackAxis(box);
    accessor.buildLayerNodeMap(box);
    accessor.buildLayerShadowMap(box);
    accessor.buildPANodeNeighbor(box);
    accessor.buildBoxEnvironment(box);
    for (int32_t reroute = 0; reroute < 2; reroute++) {
      if (reroute != 0) {
        accessor.removeTaskResultFromEnvironment(box, &task);
      }
      task.set_routed_times(reroute);
      accessor.routePATask(box, &task);
      require(task.get_selected_access_point() != nullptr, "Routing did not select an AP");
      const auto coord = task.get_selected_access_point()->getRealLayerCoord();
      appendSignature(signature, coord.get_x());
      appendSignature(signature, coord.get_y());
      appendSignature(signature, coord.get_layer_idx());
      const auto& segments = box.get_curr_result().get_task_result_list()[0].get_segment_list();
      appendSignature(signature, segments.size());
      for (const auto& segment : segments) {
        for (const auto& end : {segment.get_first(), segment.get_second()}) {
          appendSignature(signature, end.get_x());
          appendSignature(signature, end.get_y());
          appendSignature(signature, end.get_layer_idx());
        }
        appendSignature(signature, segment.get_via_master_idx().get_below_layer_idx());
        appendSignature(signature, segment.get_via_master_idx().get_via_idx());
      }
      require(box.get_route_state().get_curr_route_task() == nullptr && box.get_route_state().get_open_queue().top() == nullptr, "Routing retained active state");
      for (auto& nodes : box.get_layer_node_map()) {
        for (int32_t x = 0; x < nodes.get_x_size(); x++) {
          for (int32_t y = 0; y < nodes.get_y_size(); y++) {
            require(nodes[x][y].isNone() && nodes[x][y].get_parent_node() == nullptr && nodes[x][y].get_open_queue_idx() == -1,
                    "Routing retained a visited node or queue index");
          }
        }
      }
    }
    accessor.freePABox(box);
  }
  return signature;
}

}  // namespace

int main()
{
  irt::DataManager::initInst();
  try {
    initDatabase();
    irt::PinAccessor accessor;
    testIterationParameters(accessor);
    testCandidateBoundaries(accessor);
    testTargets(accessor);
    testTargetWindows(accessor);
    uint64_t signature = testCandidates(accessor);
    std::cout << "PA candidate signature: " << signature << '\n';
    // Recorded against e70f14fc3 before restructuring candidate enumeration and selection.
    require(signature == UINT64_C(16264380958811896310), "Candidate ordering, cost, sampling, or via selection changed");
    bool had_throw_policy = std::getenv("ECC_LOGGER_THROW_ON_ERROR") != nullptr;
    if (!had_throw_policy) {
      require(setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1) == 0, "Cannot enable logger exceptions for geometry rejection tests");
    }
    int32_t rejected_num = 0;
    uint64_t legal_signature = testLegalShapes(accessor, rejected_num);
    if (!had_throw_policy) {
      require(unsetenv("ECC_LOGGER_THROW_ON_ERROR") == 0, "Cannot restore logger error policy");
    }
    std::cout << "PA legal geometry signature: " << legal_signature << ", rejected: " << rejected_num << '\n';
    // Recorded against a18f572df before narrowing queries and restructuring the geometry pipeline.
    require(legal_signature == UINT64_C(14584988952947507999) && rejected_num == 2,
            "Legal geometry ordering, enclosure, obstacle policy, or precision rejection changed");
    testLegalBoundaries(accessor);
    testLegalBorderRejection(accessor);
    testPatchSampling(accessor);
    uint64_t patch_signature = testPatchCandidates(accessor);
    uint64_t route_signature = testRouteTasks(accessor);
    std::cout << "PA patch candidate signature: " << patch_signature << '\n';
    std::cout << "PA route task signature: " << route_signature << '\n';
    // Recorded against 6a1f36ff5 before simplifying patch generation and two-group routing.
    require(patch_signature == UINT64_C(12375491706622377300), "Patch geometry, sampling, order, or costs changed");
    require(route_signature == UINT64_C(18226129794881124865), "Task AP selection, segment order, or via identity changed");
    irt::DataManager::destroyInst();
    irt::Utility::destroyInst();
    std::cout << "PA candidate tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
