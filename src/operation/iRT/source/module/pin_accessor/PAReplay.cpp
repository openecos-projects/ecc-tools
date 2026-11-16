#include "PinAccessor.hpp"
#include "ReplayCapture.h"
#include "ReplayRouting.hpp"

namespace irt {
void PinAccessor::captureReplay(PAModel& model, std::string_view phase)
{
  ecc::replay::guarded([&] {
    std::map<int32_t, std::vector<Segment<LayerCoord>>> routes;
    std::map<int32_t, std::vector<EXTLayerRect>> patches;
    for (const auto& [net, pins] : model.get_net_pin_access_result_map())
      for (const auto& [pin, segments] : pins)
        routes[net].insert(routes[net].end(), segments.begin(), segments.end());
    for (const auto& [net, pins] : model.get_net_pin_access_patch_map())
      for (const auto& [pin, rectangles] : pins)
        patches[net].insert(patches[net].end(), rectangles.begin(), rectangles.end());
    auto objects = replayRoutingObjects(routes, patches);
    appendReplayViolations(objects, model.get_route_violation_list());
    auto& boxes = model.get_pa_box_map();
    for (int x = 0; x < boxes.get_x_size(); ++x)
      for (int y = 0; y < boxes.get_y_size(); ++y)
        appendReplayRegion(objects, "PA:" + std::to_string(x) + ":" + std::to_string(y), boxes[x][y].get_box_rect().get_real_rect(), false);
    size_t selected = 0, candidates = 0, total_candidates = 0;
    auto& layers = RTDM.getDatabase().get_routing_layer_list();
    size_t unmapped = 0;
    for (auto& net : model.get_pa_net_list()) {
      for (auto& pin : net.get_pa_pin_list()) {
        if (phase == "pin_access_candidates" || phase == "pin_access_partition") {
          for (const auto& candidate : pin.get_access_point_list()) {
            ++total_candidates;
            // Bound candidate preview; selected points and routing remain complete.
            if (candidates >= 100000)
              continue;
            const auto layer = candidate.get_layer_idx();
            if (layer < 0 || static_cast<size_t>(layer) >= layers.size())
              continue;
            const auto x = candidate.get_real_x(), y = candidate.get_real_y();
            objects["candidate:" + std::to_string(candidates++)]
                = {{"kind", "access_candidate"}, {"name", pin.get_pin_name()}, {"layer", layers[layer].get_layer_name()}, {"bbox", {x, y, x, y}}};
          }
          continue;
        }
        auto& point = pin.get_access_point();
        const auto layer = point.get_layer_idx();
        if (layer < 0 || static_cast<size_t>(layer) >= layers.size()) {
          ++unmapped;
          continue;
        }
        const auto x = point.get_real_x();
        const auto y = point.get_real_y();
        const auto key = "access:" + std::to_string(net.get_net_idx()) + ":" + std::to_string(pin.get_pin_idx());
        ++selected;
        objects[key] = {{"kind", "access_point"},
                        {"name", pin.get_pin_name()},
                        {"net", RTDM.getDatabase().get_net_list().at(net.get_net_idx()).get_net_name()},
                        {"layer", layers[layer].get_layer_name()},
                        {"bbox", {x, y, x, y}}};
      }
    }
    auto metrics = nlohmann::json{{"access_points", selected},
                                  {"unmapped_pins", unmapped},
                                  {"iteration", model.get_iter()},
                                  {"candidate_points", candidates},
                                  {"total_candidate_points", total_candidates},
                                  {"violations", model.get_route_violation_list().size()}};
    ecc::replay::sample(std::string(phase), "run routing", std::move(objects), "routing", "algorithm_state", std::move(metrics));
  });
}
}  // namespace irt
