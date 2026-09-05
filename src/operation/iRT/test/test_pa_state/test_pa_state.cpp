#include <iostream>
#include <random>
#include <stdexcept>

#include "PABox.hpp"
#include "PAPatch.hpp"

namespace {

void require(bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void testNodeContributions()
{
  for (irt::Orientation orientation : irt::PANode::kOrientationList) {
    irt::PANode node;
    node.addRoutedRectNet(orientation, 7);
    node.addRoutedRectNet(orientation, 7);
    node.delRoutedRectNet(orientation, 7);
    require(node.getRoutedRectCost(8, orientation, 10) == 10, "Removing one contribution erased another task's occupancy");
    require(node.getRoutedRectCost(7, orientation, 10) == 0, "A net must not block itself");
    node.addRoutedRectNet(orientation, 8);
    require(node.getRoutedRectCost(7, orientation, 10) == 10, "Another net's occupancy was lost");
    node.delRoutedRectNet(orientation, 7);
    require(node.getRoutedRectCost(8, orientation, 10) == 0, "The direction did not return to a single net");
    node.delRoutedRectNet(orientation, 8);
    require(node.getRoutedRectCost(9, orientation, 10) == 0, "The direction did not become empty");

    node.addFixedRectNet(orientation, -1);
    node.addFixedRectNet(orientation, -1);
    node.delFixedRectNet(orientation, -1);
    require(!node.hasFixedRectOrient(orientation), "Pin exemption must remove the fixed obstacle membership");
  }
}

void testResultSnapshot()
{
  irt::PABox box;
  require(!box.get_best_result().get_valid(), "A new box must not have a best result");
  box.get_best_result() = box.get_curr_result();
  box.get_best_result().set_valid(true);
  require(box.get_best_result().get_valid(), "An empty result is still a valid snapshot");

  box.get_curr_result().get_task_result_list().resize(1);
  box.get_curr_result().get_task_result_list()[0].get_segment_list().emplace_back(irt::LayerCoord(0, 0, 0), irt::LayerCoord(10, 0, 0));
  box.get_curr_result().get_task_result_list()[0].get_patch_list().emplace_back();
  box.get_curr_result().get_task_result_list()[0].set_access_point(irt::AccessPoint(0, irt::LayerCoord(0, 0, 0)));
  box.get_curr_result().get_route_violation_list().emplace_back();
  box.get_best_result() = box.get_curr_result();
  box.get_best_result().set_valid(true);
  box.get_curr_result() = irt::PABoxResult();
  require(box.get_best_result().get_task_result_list().at(0).get_segment_list().size() == 1, "Snapshot aliases the current segments");
  box.get_curr_result() = std::move(box.get_best_result());
  require(box.get_curr_result().get_task_result_list().at(0).get_patch_list().size() == 1, "Restoring a result lost its patch");
  require(box.get_curr_result().get_task_result_list().at(0).get_access_point().getRealLayerCoord() == irt::LayerCoord(0, 0, 0),
          "Restoring a result lost its access point");
  require(box.get_curr_result().get_route_violation_list().size() == 1, "Restoring a result lost its violations");
  require(box.get_best_result().get_task_result_list().empty(), "Restoring a result copied instead of moving its segments");
}

void testTaskSortKey()
{
  irt::PATask task0;
  irt::PATask task1;
  std::vector<irt::PAGroup> groups(2);
  groups[0].get_coord_list() = {irt::LayerCoord(20, 0, 1), irt::LayerCoord(0, 0, 0)};
  groups[1].get_coord_list() = {irt::LayerCoord(10, 0, 1)};
  task0.set_pa_group_list(groups);
  std::vector<irt::LayerCoord> sorted_coords = {irt::LayerCoord(0, 0, 0), irt::LayerCoord(10, 0, 1), irt::LayerCoord(20, 0, 1)};
  require(task0.get_sort_coord_list() == sorted_coords, "Task sort key does not match canonical coordinates");
  std::reverse(groups[0].get_coord_list().begin(), groups[0].get_coord_list().end());
  task1.set_pa_group_list(groups);
  require(!irt::CmpPATask()(&task0, &task1) && !irt::CmpPATask()(&task1, &task0), "Group coordinate order changed task priority");
  groups[1].get_coord_list()[0].set_x(5);
  task1.set_pa_group_list(groups);
  require(irt::CmpPATask()(&task1, &task0), "Replacing task groups did not refresh the cached sort key");
  task1.set_pa_group_list({});
  require(task1.get_sort_coord_list().empty(), "Replacing task groups retained old coordinates");
}

void testRandomNodeContributions()
{
  std::mt19937 generator(20260906);
  irt::PANode node;
  std::array<std::array<int32_t, 8>, 6> counts{};
  for (int32_t i = 0; i < 20000; i++) {
    size_t direction_idx = generator() % counts.size();
    size_t net_idx = generator() % counts[direction_idx].size();
    irt::Orientation orientation = irt::PANode::kOrientationList[direction_idx];
    if (counts[direction_idx][net_idx] == 0 || generator() % 2 == 0) {
      node.addRoutedRectNet(orientation, net_idx);
      counts[direction_idx][net_idx]++;
    } else {
      node.delRoutedRectNet(orientation, net_idx);
      counts[direction_idx][net_idx]--;
    }
    for (size_t query_net = 0; query_net < counts[direction_idx].size(); query_net++) {
      bool blocked = false;
      for (size_t other_net = 0; other_net < counts[direction_idx].size(); other_net++) {
        blocked |= other_net != query_net && counts[direction_idx][other_net] > 0;
      }
      require(node.getRoutedRectCost(query_net, orientation, 10) == (blocked ? 10 : 0), "Random contribution updates changed the node cost incorrectly");
    }
  }
  for (size_t direction_idx = 0; direction_idx < counts.size(); direction_idx++) {
    for (size_t net_idx = 0; net_idx < counts[direction_idx].size(); net_idx++) {
      for (int32_t i = 0; i < counts[direction_idx][net_idx]; i++) {
        node.delRoutedRectNet(irt::PANode::kOrientationList[direction_idx], net_idx);
      }
    }
  }
  require(node.get_orient_routed_rect_map().empty(), "Balanced updates left routed contributions behind");
}

void testShadowContributions()
{
  irt::PAShadow shadow;
  irt::PlanarRect rect(0, 0, 10, 10);
  shadow.addRoutedRect(7, rect);
  shadow.addRoutedRect(7, rect);
  shadow.addRoutedRect(8, rect);
  require(shadow.get_routed_rect_rtree().size() == 2, "Repeated contributions must share one index entry");
  require(shadow.getRoutedRectCost(9, rect, 10) == 20, "Shadow cost must count unique net/rectangle entries");
  require(shadow.getRoutedRectCost(7, rect, 10) == 10, "A shadow must not block its own net");
  shadow.delRoutedRect(7, rect);
  require(shadow.get_net_routed_rect_map().at(7).at(rect) == 1, "Removing one shadow erased another task's contribution");
  shadow.delRoutedRect(7, rect);
  require(!shadow.get_net_routed_rect_map().contains(7), "The empty routed shadow net was retained");
  require(shadow.get_net_routed_rect_map().at(8).at(rect) == 1, "Removing one net changed another net's shadow");
  shadow.delRoutedRect(8, rect);
  require(shadow.get_net_routed_rect_map().empty(), "Balanced updates left shadow contributions behind");
  require(shadow.get_routed_rect_rtree().empty(), "Balanced updates left stale spatial entries");
}

using ShadowRectMap = std::map<int32_t, std::map<irt::PlanarRect, int32_t, irt::CmpPlanarRectByXASC>>;

double getReferenceCost(const ShadowRectMap& rect_map, int32_t net_idx, const irt::PlanarRect& query, double unit)
{
  double cost = 0;
  for (const auto& [other_net_idx, rect_counts] : rect_map) {
    if (other_net_idx == net_idx) {
      continue;
    }
    for (const auto& [rect, count] : rect_counts) {
      if (count > 0 && irt::Utility::isOpenOverlap(query, rect)) {
        cost += unit;
      }
    }
  }
  return cost;
}

void testShadowBoundaries()
{
  irt::PAShadow shadow;
  irt::PlanarRect rect(0, 0, 10, 10);
  shadow.addFixedRect(-1, rect);
  shadow.addFixedRect(-1, rect);
  shadow.addFixedRect(7, rect);
  shadow.buildFixedRectRTree();
  shadow.addRoutedRect(7, rect);
  shadow.addViolation(rect);
  shadow.addViolation(rect);
  require(shadow.get_fixed_rect_rtree().size() == 2, "Fixed shadows were not deduplicated by net and rectangle");
  require(shadow.getFixedRectCost(7, rect, 3) == 3, "Fixed shadows lost the same-net exemption or obstacle");
  require(shadow.getFixedRectCost(8, rect, 3) == 6, "Fixed shadows merged different nets");
  require(shadow.getViolationCost(rect, 5) == 5, "Duplicate violation rectangles were charged twice");
  for (const irt::PlanarRect& query : {irt::PlanarRect(10, 2, 15, 8), irt::PlanarRect(10, 10, 15, 15), irt::PlanarRect(20, 20, 30, 30)}) {
    require(shadow.getFixedRectCost(8, query, 3) == 0, "Touching or disjoint fixed shadows were charged");
    require(shadow.getRoutedRectCost(8, query, 3) == 0, "Touching or disjoint routed shadows were charged");
    require(shadow.getViolationCost(query, 5) == 0, "Touching or disjoint violations were charged");
  }
  for (const irt::PlanarRect& query : {irt::PlanarRect(9, 2, 15, 8), irt::PlanarRect(5, 2, 5, 8), irt::PlanarRect(5, 5, 5, 5)}) {
    require(shadow.getRoutedRectCost(8, query, 3) == 3, "Interior overlap semantics changed");
  }
  shadow.clearViolation();
  require(shadow.getViolationCost(rect, 5) == 0, "Clearing violations left stale costs");
  irt::PAShadow empty;
  empty.buildFixedRectRTree();
  require(empty.getFixedRectCost(8, rect, 3) == 0 && empty.getRoutedRectCost(8, rect, 3) == 0, "An empty index has a nonzero cost");
}

void testRandomShadowQueries()
{
  std::mt19937 generator(20260907);
  irt::PAShadow shadow;
  ShadowRectMap fixed_rect_map;
  ShadowRectMap routed_rect_map;
  std::vector<irt::PlanarRect> rect_list;
  for (int32_t i = 0; i < 256; i++) {
    int32_t x = static_cast<int32_t>(generator() % 200) - 100;
    int32_t y = static_cast<int32_t>(generator() % 200) - 100;
    rect_list.emplace_back(x, y, x + static_cast<int32_t>(generator() % 40), y + static_cast<int32_t>(generator() % 40));
    int32_t net_idx = static_cast<int32_t>(generator() % 9) - 1;
    shadow.addFixedRect(net_idx, rect_list.back());
    shadow.addFixedRect(net_idx, rect_list.back());
    fixed_rect_map[net_idx][rect_list.back()] += 2;
  }
  shadow.buildFixedRectRTree();
  for (int32_t i = 0; i < 20000; i++) {
    const irt::PlanarRect& rect = rect_list[generator() % rect_list.size()];
    int32_t net_idx = generator() % 8;
    int32_t& count = routed_rect_map[net_idx][rect];
    if (count == 0 || generator() % 2 == 0) {
      count++;
      shadow.addRoutedRect(net_idx, rect);
    } else {
      count--;
      shadow.delRoutedRect(net_idx, rect);
    }
    for (int32_t query_idx = 0; query_idx < 4; query_idx++) {
      const irt::PlanarRect& query = rect_list[generator() % rect_list.size()];
      int32_t query_net_idx = generator() % 8;
      require(shadow.getFixedRectCost(query_net_idx, query, 0.125) == getReferenceCost(fixed_rect_map, query_net_idx, query, 0.125),
              "Indexed fixed cost differs from full scanning");
      require(shadow.getRoutedRectCost(query_net_idx, query, 3.5) == getReferenceCost(routed_rect_map, query_net_idx, query, 3.5),
              "Indexed routed cost differs from full scanning after updates");
    }
  }
  std::vector<irt::PAPatch> indexed_patch_list;
  std::vector<irt::PAPatch> reference_patch_list;
  irt::Direction direction = irt::Direction::kHorizontal;
  for (const irt::PlanarRect& rect : rect_list) {
    irt::PAPatch patch(rect, 0);
    patch.set_direction(rect.getRectDirection(direction));
    patch.set_fixed_rect_cost(shadow.getFixedRectCost(0, rect, 10));
    patch.set_routed_rect_cost(shadow.getRoutedRectCost(0, rect, 2));
    indexed_patch_list.push_back(patch);
    patch.set_fixed_rect_cost(getReferenceCost(fixed_rect_map, 0, rect, 10));
    patch.set_routed_rect_cost(getReferenceCost(routed_rect_map, 0, rect, 2));
    reference_patch_list.push_back(patch);
  }
  auto cmp_patch = [&direction](const irt::PAPatch& a, const irt::PAPatch& b) { return irt::CmpPAPatch()(a, b, direction); };
  std::ranges::sort(indexed_patch_list, cmp_patch);
  std::ranges::sort(reference_patch_list, cmp_patch);
  for (size_t i = 0; i < indexed_patch_list.size(); i++) {
    require(indexed_patch_list[i].get_patch().get_real_rect() == reference_patch_list[i].get_patch().get_real_rect(), "Indexed costs changed candidate order");
  }
  for (const auto& [net_idx, rect_counts] : routed_rect_map) {
    for (const auto& [rect, count] : rect_counts) {
      for (int32_t i = 0; i < count; i++) {
        shadow.delRoutedRect(net_idx, rect);
      }
    }
  }
  require(shadow.get_routed_rect_rtree().empty() && shadow.get_net_routed_rect_map().empty(), "Random balanced updates left stale entries");
}

void benchmarkShadow()
{
  using Clock = std::chrono::steady_clock;
  for (int32_t rect_num : {16, 128, 1024, 4096}) {
    std::vector<irt::PlanarRect> rect_list;
    for (int32_t i = 0; i < rect_num; i++) {
      int32_t x = i % 64 * 32;
      int32_t y = i / 64 * 32;
      rect_list.emplace_back(x, y, x + 12, y + 12);
    }
    auto begin = Clock::now();
    irt::PAShadow shadow;
    for (int32_t i = 0; i < rect_num; i++) {
      shadow.addFixedRect(i % 8, rect_list[i]);
      shadow.addRoutedRect(i % 8, rect_list[i]);
    }
    shadow.buildFixedRectRTree();
    auto indexed_built = Clock::now();
    double indexed_cost = 0;
    for (int32_t i = 0; i < 4096; i++) {
      indexed_cost += shadow.getFixedRectCost(0, rect_list[i % rect_num], 1);
      indexed_cost += shadow.getRoutedRectCost(0, rect_list[i % rect_num], 1);
    }
    auto indexed_queried = Clock::now();
    for (int32_t i = 0; i < rect_num; i++) {
      shadow.delRoutedRect(i % 8, rect_list[i]);
      shadow.addRoutedRect(i % 8, rect_list[i]);
    }
    auto indexed_updated = Clock::now();
    ShadowRectMap fixed_rect_map;
    ShadowRectMap routed_rect_map;
    for (int32_t i = 0; i < rect_num; i++) {
      fixed_rect_map[i % 8][rect_list[i]]++;
      routed_rect_map[i % 8][rect_list[i]]++;
    }
    auto reference_built = Clock::now();
    double reference_cost = 0;
    for (int32_t i = 0; i < 4096; i++) {
      reference_cost += getReferenceCost(fixed_rect_map, 0, rect_list[i % rect_num], 1);
      reference_cost += getReferenceCost(routed_rect_map, 0, rect_list[i % rect_num], 1);
    }
    auto reference_queried = Clock::now();
    for (int32_t i = 0; i < rect_num; i++) {
      routed_rect_map[i % 8].erase(rect_list[i]);
      routed_rect_map[i % 8][rect_list[i]]++;
    }
    auto reference_updated = Clock::now();
    require(indexed_cost == reference_cost && indexed_cost > 0, "Shadow benchmark cost mismatch");
    require(shadow.get_net_routed_rect_map() == routed_rect_map, "Shadow benchmark update mismatch");
    std::cout << "rects=" << rect_num << " indexed_us(build/query/update)=" << std::chrono::duration<double, std::micro>(indexed_built - begin).count() << '/'
              << std::chrono::duration<double, std::micro>(indexed_queried - indexed_built).count() << '/'
              << std::chrono::duration<double, std::micro>(indexed_updated - indexed_queried).count()
              << " scan_us(build/query/update)=" << std::chrono::duration<double, std::micro>(reference_built - indexed_updated).count() << '/'
              << std::chrono::duration<double, std::micro>(reference_queried - reference_built).count() << '/'
              << std::chrono::duration<double, std::micro>(reference_updated - reference_queried).count() << '\n';
  }
}

void testWorkspaceReset()
{
  irt::PABox box;
  irt::PATask task;
  irt::PANode node;
  box.get_route_state().set_curr_route_task(&task);
  box.get_route_state().get_open_queue().push(&node);
  box.get_route_state().get_open_queue().release();
  require(node.get_open_queue_idx() == -1, "Releasing a queue left a stale node index");
  box.get_route_state() = irt::PARouteState();
  require(box.get_route_state().get_curr_route_task() == nullptr, "Route state retained a task");
  require(box.get_route_state().get_end_node_list_idx() == -1, "Route state retained an end index");
  box.get_patch_state().set_curr_patch_task(&task);
  box.get_patch_state().get_routing_patch_list().emplace_back();
  box.get_patch_state().get_tried_fix_violation_set().emplace();
  box.get_patch_state() = irt::PAPatchState();
  require(box.get_patch_state().get_curr_patch_task() == nullptr, "Patch state retained a task");
  require(box.get_patch_state().get_routing_patch_list().empty(), "Patch state retained a patch");
  require(box.get_patch_state().get_tried_fix_violation_set().empty(), "Patch state retained attempted violations");
}

}  // namespace

int main(int argc, char** argv)
{
  try {
    if (argc == 2 && std::string(argv[1]) == "--benchmark") {
      benchmarkShadow();
      return 0;
    }
    require(argc == 1, "Expected no arguments or --benchmark");
    testResultSnapshot();
    testTaskSortKey();
    testWorkspaceReset();
    testNodeContributions();
    testRandomNodeContributions();
    testShadowContributions();
    testShadowBoundaries();
    testRandomShadowQueries();
    std::cout << "PA state tests passed\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
