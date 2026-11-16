#include "DetailedRouter.hpp"
#include "ReplayCapture.h"
#include "ReplayRouting.hpp"

namespace irt {
void DetailedRouter::captureReplay(DRModel& model, std::string_view phase, ReplayState state, const std::vector<DRBoxId>* active_boxes)
{
  ecc::replay::guarded([&] {
    auto& result = model.get_curr_result();
    if (state == ReplayState::kModel) {
      const auto* violations = &result.get_route_violation_list();
      captureReplayRouting(phase, result.get_net_detailed_result_map(), result.get_net_detailed_patch_map(), model.get_iter(),
                           static_cast<int>(violations->size()), violations, {{"violation_evaluation", "current"}});
      return;
    }
    // splitNetResult transfers ownership to boxes. Include ALL owners, including
    // untouched boxes, without moving data or changing the router's model.
    auto routes = result.get_net_detailed_result_map();
    auto patches = result.get_net_detailed_patch_map();
    auto& boxes = model.get_dr_box_map();
    auto regions = nlohmann::json::object();
    std::set<std::pair<int, int>> active;
    if (active_boxes)
      for (const auto& id : *active_boxes)
        active.emplace(id.get_x(), id.get_y());
    for (int x = 0; x < boxes.get_x_size(); ++x)
      for (int y = 0; y < boxes.get_y_size(); ++y) {
        appendReplayRegion(regions, "DR:" + std::to_string(x) + ":" + std::to_string(y), boxes[x][y].get_box_rect().get_real_rect(), active.contains({x, y}));
        const auto& box = boxes[x][y].get_curr_result();
        for (const auto& [net, segments] : box.get_net_own_result_map())
          routes[net].insert(routes[net].end(), segments.begin(), segments.end());
        for (const auto& [net, rectangles] : box.get_net_own_patch_map())
          patches[net].insert(patches[net].end(), rectangles.begin(), rectangles.end());
      }
    auto objects = replayRoutingObjects(routes, patches);
    objects.update(regions);
    ecc::replay::sample(std::string(phase), "run routing", std::move(objects), "routing", "algorithm_state",
                        {{"iteration", model.get_iter()},
                         {"active_boxes", active.size()},
                         {"boxes", regions.size()},
                         {"violation_evaluation", "pending"},
                         {"geometry", "box_owner_union"}});
  });
}
void DetailedRouter::captureReplayRegions(DRModel& model, std::string_view phase, const std::vector<PlanarRect>& boxes, const std::vector<size_t>* active)
{
  ecc::replay::guarded([&] {
    const auto& result = model.get_curr_result();
    auto objects = replayRoutingObjects(result.get_net_detailed_result_map(), result.get_net_detailed_patch_map());
    for (size_t i = 0; i < boxes.size(); ++i)
      appendReplayRegion(objects, "repair:" + std::to_string(i), boxes[i], !active || std::find(active->begin(), active->end(), i) != active->end());
    ecc::replay::sample(std::string(phase), "run routing", std::move(objects), "routing", "algorithm_state",
                        {{"iteration", model.get_iter()}, {"active_boxes", active ? active->size() : boxes.size()}, {"violation_evaluation", "pending"}});
  });
}
}  // namespace irt
