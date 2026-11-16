#include "ReplayCapture.h"
#include "ReplayRouting.hpp"
#include "TARouter.hpp"

namespace irt {
void TARouter::captureReplay(std::string_view phase, int iteration, std::optional<size_t> conflicts_before)
{
  ecc::replay::guarded([&] {
    std::map<int32_t, std::vector<Segment<LayerCoord>>> routes;
    for (const auto& net : ta_net_list_) {
      for (const auto& edge : net.edge_list) {
        if (edge.is_via)
          continue;
        const auto& a = net.node_list[edge.first_node_idx].real_coord;
        const auto& b = net.node_list[edge.second_node_idx].real_coord;
        const auto direction = getEdgeDirection(net, edge);
        if (direction == Direction::kHorizontal && a.get_x() != b.get_x())
          routes[net.net_idx].emplace_back(LayerCoord(a.get_x(), edge.track_coord, edge.layer_idx), LayerCoord(b.get_x(), edge.track_coord, edge.layer_idx));
        else if (direction == Direction::kVertical && a.get_y() != b.get_y())
          routes[net.net_idx].emplace_back(LayerCoord(edge.track_coord, a.get_y(), edge.layer_idx), LayerCoord(edge.track_coord, b.get_y(), edge.layer_idx));
      }
    }
    nlohmann::json metrics = {{"geometry", "selected_tracks"}};
    if (conflicts_before)
      metrics["conflicting_nets_before"] = *conflicts_before;
    captureReplayRouting(phase, routes, {}, iteration, -1, nullptr, std::move(metrics));
  });
}
}  // namespace irt
