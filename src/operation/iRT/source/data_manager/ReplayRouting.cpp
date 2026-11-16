#include "ReplayRouting.hpp"

#include "DataManager.hpp"
#include "ReplayCapture.h"

namespace irt {
void captureReplayRouting(const std::string& phase, const std::map<int32_t, std::vector<Segment<LayerCoord>>>& routes,
                          const std::map<int32_t, std::vector<EXTLayerRect>>& patches, int iteration, int violations)
{
  ecc::replay::guarded([&] {
    auto objects = nlohmann::json::object();
    std::map<std::string, size_t> duplicates;
    auto& database = RTDM.getDatabase();
    auto put = [&](int net, const LayerRect& shape, bool routing, const std::string& kind) {
      const auto name = database.get_net_list().at(net).get_net_name();
      const auto layer = routing ? database.get_routing_layer_list().at(shape.get_layer_idx()).get_layer_name()
                                 : database.get_cut_layer_list().at(shape.get_layer_idx()).get_layer_name();
      nlohmann::json object
          = {{"kind", kind}, {"name", name}, {"layer", layer}, {"bbox", {shape.get_ll_x(), shape.get_ll_y(), shape.get_ur_x(), shape.get_ur_y()}}};
      const auto identity = object.dump();
      objects["rt:" + identity + ":" + std::to_string(duplicates[identity]++)] = std::move(object);
    };
    for (const auto& [net, segments] : routes) {
      for (auto segment : segments) {
        for (const auto& shape : RTDM.getNetDetailedShapeList(net, segment))
          put(net, shape, shape.get_is_routing(), shape.get_is_routing() ? "wire" : "via");
      }
    }
    for (const auto& [net, rectangles] : patches)
      for (const auto& shape : rectangles)
        put(net, LayerRect(shape.get_real_rect(), shape.get_layer_idx()), true, "route_patch");
    nlohmann::json metrics = {{"iteration", iteration}, {"routed_nets", routes.size()}};
    if (violations >= 0)
      metrics["violations"] = violations;
    ecc::replay::sample(phase, "run routing", std::move(objects), "routing", "algorithm_state", std::move(metrics));
  });
}
}  // namespace irt
