#include "PinAccessor.hpp"
#include "ReplayCapture.h"

namespace irt {
void PinAccessor::captureReplay(PAModel& model, const std::string& phase)
{
  ecc::replay::guarded([&] {
    auto objects = nlohmann::json::object();
    auto& layers = RTDM.getDatabase().get_routing_layer_list();
    size_t unmapped = 0;
    for (auto& net : model.get_pa_net_list()) {
      for (auto& pin : net.get_pa_pin_list()) {
        auto& point = pin.get_access_point();
        const auto layer = point.get_layer_idx();
        if (layer < 0 || static_cast<size_t>(layer) >= layers.size()) {
          ++unmapped;
          continue;
        }
        const auto x = point.get_real_x();
        const auto y = point.get_real_y();
        const auto key = "access:" + std::to_string(net.get_net_idx()) + ":" + std::to_string(pin.get_pin_idx());
        objects[key] = {{"kind", "access_point"},
                        {"name", pin.get_pin_name()},
                        {"net", RTDM.getDatabase().get_net_list().at(net.get_net_idx()).get_net_name()},
                        {"layer", layers[layer].get_layer_name()},
                        {"bbox", {x, y, x, y}}};
      }
    }
    auto metrics = nlohmann::json{{"access_points", objects.size()}, {"unmapped_pins", unmapped}};
    ecc::replay::sample(phase, "run routing", std::move(objects), "access", "algorithm_state", std::move(metrics));
  });
}
}  // namespace irt
