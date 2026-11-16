#include "ReplayRouting.hpp"

#include "DataManager.hpp"
#include "ReplayCapture.h"

namespace irt {
nlohmann::json replayRoutingObjects(const std::map<int32_t, std::vector<Segment<LayerCoord>>>& routes,
                                    const std::map<int32_t, std::vector<EXTLayerRect>>& patches)
{
  auto objects = nlohmann::json::object();
  std::map<std::string, size_t> duplicates;
  auto& database = RTDM.getDatabase();
  auto put = [&](int net, const LayerRect& shape, bool routing, const std::string& kind, const std::string& via_master = "") {
    const auto name = database.get_net_list().at(net).get_net_name();
    const auto layer = routing ? database.get_routing_layer_list().at(shape.get_layer_idx()).get_layer_name()
                               : database.get_cut_layer_list().at(shape.get_layer_idx()).get_layer_name();
    nlohmann::json object
        = {{"kind", kind}, {"name", name}, {"layer", layer}, {"bbox", {shape.get_ll_x(), shape.get_ll_y(), shape.get_ur_x(), shape.get_ur_y()}}};
    if (!via_master.empty())
      object["via_master"] = via_master;
    const auto identity = object.dump();
    objects["rt:" + identity + ":" + std::to_string(duplicates[identity]++)] = std::move(object);
  };
  for (const auto& [net, segments] : routes) {
    for (auto segment : segments) {
      std::string via_master;
      if (segment.hasValidViaMaster()) {
        const auto& index = segment.get_via_master_idx();
        via_master = database.get_layer_via_master_list().at(index.get_below_layer_idx()).at(index.get_via_idx()).get_via_name();
      }
      for (const auto& shape : RTDM.getNetDetailedShapeList(net, segment))
        put(net, shape, shape.get_is_routing(), shape.get_is_routing() ? "wire" : "via", via_master);
    }
  }
  for (const auto& [net, rectangles] : patches)
    for (const auto& shape : rectangles)
      put(net, LayerRect(shape.get_real_rect(), shape.get_layer_idx()), true, "route_patch");
  return objects;
}

void appendReplayRegion(nlohmann::json& objects, const std::string& id, const PlanarRect& area, bool active)
{
  objects["region:" + id] = {{"kind", "route_region"}, {"name", id}, {"active", active},
      {"bbox", {area.get_ll_x(), area.get_ll_y(), area.get_ur_x(), area.get_ur_y()}}};
}

void appendReplayViolations(nlohmann::json& objects, const std::vector<Violation>& violations)
{
  auto& database = RTDM.getDatabase();
  std::map<std::string, size_t> duplicates;
  for (const auto& violation : violations) {
    const auto& shape = violation.get_violation_shape();
    const auto& rect = shape.get_real_rect();
    const auto layer = violation.get_is_routing() ? database.get_routing_layer_list().at(shape.get_layer_idx()).get_layer_name()
                                                  : database.get_cut_layer_list().at(shape.get_layer_idx()).get_layer_name();
    nlohmann::json object = {{"kind", "route_violation"},
                             {"layer", layer},
                             {"name", GetViolationTypeName()(violation.get_violation_type())},
                             {"bbox", {rect.get_ll_x(), rect.get_ll_y(), rect.get_ur_x(), rect.get_ur_y()}},
                             {"net_ids", violation.get_violation_net_set()},
                             {"required_size", violation.get_required_size()},
                             {"is_routing", violation.get_is_routing()},
                             {"units", "DBU"}};
    const auto identity = object.dump();
    objects["violation:" + identity + ":" + std::to_string(duplicates[identity]++)] = std::move(object);
  }
}

void captureReplayRouting(std::string_view phase, const std::map<int32_t, std::vector<Segment<LayerCoord>>>& routes,
                          const std::map<int32_t, std::vector<EXTLayerRect>>& patches, int iteration, int violations,
                          const std::vector<Violation>* violation_shapes, nlohmann::json extras)
{
  ecc::replay::guarded([&] {
    auto objects = replayRoutingObjects(routes, patches);
    if (violation_shapes)
      appendReplayViolations(objects, *violation_shapes);
    auto metrics = extras.is_null() ? nlohmann::json::object() : std::move(extras);
    metrics["iteration"] = iteration;
    metrics["routed_nets"] = routes.size();
    if (violations >= 0)
      metrics["violations"] = violations;
    ecc::replay::sample(std::string(phase), "run routing", std::move(objects), "routing", "algorithm_state", std::move(metrics));
  });
}
}  // namespace irt
