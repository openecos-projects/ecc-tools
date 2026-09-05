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

}  // namespace

int main()
{
  irt::DataManager::initInst();
  try {
    initDatabase();
    irt::PinAccessor accessor;
    testCandidateBoundaries(accessor);
    testTargets(accessor);
    uint64_t signature = testCandidates(accessor);
    std::cout << "PA candidate signature: " << signature << '\n';
    // Recorded against e70f14fc3 before restructuring candidate enumeration and selection.
    require(signature == UINT64_C(16264380958811896310), "Candidate ordering, cost, sampling, or via selection changed");
    irt::DataManager::destroyInst();
    irt::Utility::destroyInst();
    std::cout << "PA candidate tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
