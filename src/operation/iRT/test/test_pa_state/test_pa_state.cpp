#include <iostream>
#include <random>
#include <stdexcept>

#include "PABox.hpp"

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

  irt::PAPin pin;
  box.get_curr_result().get_net_task_result_map()[7][0].emplace_back(irt::LayerCoord(0, 0, 0), irt::LayerCoord(10, 0, 0));
  box.get_curr_result().get_net_task_patch_map()[7][0].emplace_back();
  box.get_curr_result().get_pin_access_point_map()[&pin] = irt::AccessPoint(0, irt::LayerCoord(0, 0, 0));
  box.get_curr_result().get_route_violation_list().emplace_back();
  box.get_best_result() = box.get_curr_result();
  box.get_best_result().set_valid(true);
  box.get_curr_result() = irt::PABoxResult();
  require(box.get_best_result().get_net_task_result_map().at(7).at(0).size() == 1, "Snapshot aliases the current segments");
  box.get_curr_result() = std::move(box.get_best_result());
  require(box.get_curr_result().get_net_task_patch_map().at(7).at(0).size() == 1, "Restoring a result lost its patch");
  require(box.get_curr_result().get_pin_access_point_map().size() == 1, "Restoring a result lost its access point");
  require(box.get_curr_result().get_route_violation_list().size() == 1, "Restoring a result lost its violations");
  require(box.get_best_result().get_net_task_result_map().empty(), "Restoring a result copied instead of moving its segments");
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
  shadow.delRoutedRect(7, rect);
  require(shadow.get_net_routed_rect_map().at(7).at(rect) == 1, "Removing one shadow erased another task's contribution");
  shadow.delRoutedRect(7, rect);
  require(!shadow.get_net_routed_rect_map().contains(7), "The empty routed shadow net was retained");
  require(shadow.get_net_routed_rect_map().at(8).at(rect) == 1, "Removing one net changed another net's shadow");
  shadow.delRoutedRect(8, rect);
  require(shadow.get_net_routed_rect_map().empty(), "Balanced updates left shadow contributions behind");
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

int main()
{
  try {
    testResultSnapshot();
    testWorkspaceReset();
    testNodeContributions();
    testRandomNodeContributions();
    testShadowContributions();
    std::cout << "PA state tests passed\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
