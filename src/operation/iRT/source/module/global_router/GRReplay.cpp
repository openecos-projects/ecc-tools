#include "DataManager.hpp"
#include "GlobalRouter.hpp"
#include "ReplayCapture.h"
#include "Utility.hpp"

namespace irt {
void GlobalRouter::captureReplay(const std::string& phase) const
{
  ecc::replay::guarded([&] {
    auto objects = nlohmann::json::object();
    auto& database = RTDM.getDatabase();
    auto& axis = database.get_gcell_axis();
    std::map<std::string, size_t> duplicates;
    for (const auto& net : net_list_) {
      auto put = [&](const LayerCoord& a, const LayerCoord& b) {
        const auto first = RTUTIL.getRealRectByGCell(PlanarCoord(a.get_x(), a.get_y()), axis).getMidPoint();
        const auto second = RTUTIL.getRealRectByGCell(PlanarCoord(b.get_x(), b.get_y()), axis).getMidPoint();
        auto x1 = first.get_x(), y1 = first.get_y(), x2 = second.get_x(), y2 = second.get_y();
        nlohmann::json object = {{"kind", "guide"},
                                 {"name", net.get_origin_net()->get_net_name()},
                                 {"layer", database.get_routing_layer_list().at(a.get_layer_idx()).get_layer_name()},
                                 {"bbox", {std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2)}},
                                 {"segment", {{"begin", {x1, y1}}, {"end", {x2, y2}}, {"width", 0}}},
                                 {"target_layer", database.get_routing_layer_list().at(b.get_layer_idx()).get_layer_name()}};
        const auto identity = object.dump();
        objects["guide:" + identity + ":" + std::to_string(duplicates[identity]++)] = std::move(object);
      };
      for (const auto& wire : net.get_tree().wire_list)
        put(wire.first, wire.second);
      for (const auto& via : net.get_tree().via_list)
        put(LayerCoord(via.coord, via.bottom_layer_idx), LayerCoord(via.coord, via.top_layer_idx));
    }
    double demand = 0, supply = 0, overflow = 0;
    for (int layer = grid_graph_.getBottomLayerIdx(); layer <= grid_graph_.getTopLayerIdx(); ++layer) {
      const auto& edges = grid_graph_.getEdgeMap(layer);
      for (int x = 0; x < edges.get_x_size(); ++x)
        for (int y = 0; y < edges.get_y_size(); ++y) {
          const auto& edge = edges[x][y];
          demand += edge.getDemand();
          supply += edge.getUsableSupply();
          overflow += edge.getOverflow();
        }
    }
    ecc::replay::sample(phase, "run routing", std::move(objects), "routing", "algorithm_state",
                        {{"demand", demand}, {"capacity", supply}, {"overflow", overflow}});
  });
}
}  // namespace irt
