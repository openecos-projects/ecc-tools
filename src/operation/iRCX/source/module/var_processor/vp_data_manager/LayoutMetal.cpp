#include "LayoutMetal.hpp"

#include <cmath>

namespace ircx {
ProcessResult<LayerMetalShapes> collectLayoutMetal(LayoutData& layout, double scale)
{
  if (!std::isfinite(scale) || scale <= 0 || layout.get_dbu_per_micron() <= 0)
    return std::string("invalid metal density layout scale or database unit");
  const double unit = scale / layout.get_dbu_per_micron();
  LayerMetalShapes shapes;
  auto append = [&](int32_t layer, const GTLRectInt& rectangle) {
    if (!layout.get_routing_layer_map().contains(layer))
      return;
    shapes[layer].push_back({gtl::xl(rectangle) * unit, gtl::yl(rectangle) * unit, gtl::xh(rectangle) * unit, gtl::yh(rectangle) * unit});
  };
  auto append_net = [&](Net& net) {
    for (auto& wire : net.get_segment_list())
      append(wire.get_layer_idx(), wire.get_shape());
    for (auto& patch : net.get_patch_list())
      append(patch.get_layer_idx(), patch.get_shape());
    for (auto& pin : net.get_pin_list())
      for (const auto& shape : pin.get_layer_shape_list())
        append(shape.get_layer_idx(), shape.get_shape());
    for (auto& via : net.get_via_list()) {
      for (const auto* shape : {&via.get_bottom_layer_shape(), &via.get_top_layer_shape()})
        append(shape->get_layer_idx(), shape->get_shape());
    }
  };
  for (auto& net : layout.get_net_list())
    append_net(net);
  append_net(layout.get_special_net());
  for (const auto& shape : layout.get_density_metal_shapes())
    append(shape.get_layer_idx(), shape.get_shape());
  return shapes;
}
}  // namespace ircx
