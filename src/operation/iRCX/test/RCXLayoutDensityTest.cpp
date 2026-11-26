#include <cmath>
#include <iostream>

#include "LayoutMetal.hpp"
#include "DensityGeometry.hpp"
#include "ProcessReader.hpp"

int main()
{
  using namespace ircx;
  LayoutData layout;
  layout.set_dbu_per_micron(1000);
  layout.get_routing_layer_map()[0] = RoutingLayer{};
  layout.get_routing_layer_map()[2] = RoutingLayer{};
  layout.get_net_list().resize(1);
  auto& net = layout.get_net_list()[0];
  Segment wire;
  wire.set_layer_idx(0);
  wire.set_shape(GTLRectInt(0, 0, 1000, 1000));
  net.get_segment_list().push_back(wire);
  Patch patch;
  patch.set_layer_idx(0);
  patch.set_shape(GTLRectInt(1000, 0, 2000, 1000));
  net.get_patch_list().push_back(patch);
  Pin pin;
  pin.get_layer_shape_list().emplace_back(0, GTLRectInt(0, 1000, 1000, 2000));
  net.get_pin_list().push_back(pin);
  Via via;
  via.set_bottom_layer_shape(LayerShape(0, GTLRectInt(1000, 1000, 2000, 2000)));
  via.set_top_layer_shape(LayerShape(2, GTLRectInt(0, 0, 1000, 1000)));
  via.set_cut_layer_shape(LayerShape(1, GTLRectInt(0, 0, 500, 500)));
  net.get_via_list().push_back(via);
  layout.get_special_net().get_segment_list().push_back(wire);  // Duplicate physical metal.
  patch.set_shape(GTLRectInt(2000, 0, 3000, 1000));
  layout.get_special_net().get_patch_list().push_back(patch);
  layout.get_density_metal_shapes().emplace_back(0, GTLRectInt(2000, 1000, 3000, 2000));

  auto shapes = collectLayoutMetal(layout, 0.9);
  if (!shapes || shapes->size() != 2 || shapes->contains(1)) {
    std::cerr << "FAIL: collect routing metal and exclude cuts\n";
    return 1;
  }
  auto density = MetalDensity::build(shapes->at(0));
  auto upper = MetalDensity::build(shapes->at(2));
  if (!density || !upper)
    return 1;
  auto fraction = density->density(1.35, 0.9, 3.6);
  auto upper_fraction = upper->density(0.45, 0.45, 1.8);
  if (!fraction || std::abs(*fraction - 0.375) > 1e-12 || !upper_fraction || std::abs(*upper_fraction - 0.25) > 1e-12) {
    std::cerr << "FAIL: all metal categories contribute once with one scale conversion\n";
    return 1;
  }
  if (collectLayoutMetal(layout, 0)) {
    std::cerr << "FAIL: invalid scale accepted\n";
    return 1;
  }
  layout.set_dbu_per_micron(0);
  if (collectLayoutMetal(layout, 1)) {
    std::cerr << "FAIL: invalid layout unit accepted\n";
    return 1;
  }
  for (const bool custom_window : {false, true}) {
    const auto process = parseProcess(std::string("CONDUCTOR M1 {THICKNESS=.1 RPSQ=.12 ")
        + (custom_window ? "DENSITY_BOX_WEIGHTING_FACTOR {(20,1)} " : "")
        + R"(POLYNOMIAL_BASED_THICKNESS_VARIATION {DENSITY_POLYNOMIAL_ORDERS {1} WIDTH_POLYNOMIAL_ORDERS {0}
              POLYNOMIAL_COEFFICIENTS {1} DENSITY_BOUNDS_VS_WIDTH {(.1,0,1)}}})");
    if (!process) {
      std::cerr << "FAIL: density window model did not parse\n";
      return 1;
    }
    const std::vector<DensityRect> rectangles{{0,-1,10,1}};
    auto metal = MetalDensity::build(rectangles);
    if (!metal)
      return 1;
    const WireGeometryInput input{&process.technology->conductors[0], 0, 2, {{0,10}}};
    const std::vector<EffectiveWireSegment> snapshot{{0,0,10,{2,.1}}};
    const auto profile = finalizeDensityGeometry(input, snapshot, true, &*metal, nullptr, ProcessEffectType::kResistance);
    if (!profile || profile->empty()) {
      std::cerr << "FAIL: density window finalization failed\n";
      return 1;
    }
    for (const auto& segment : *profile) {
      const double expected = custom_window ? .105 : .1008;
      if (std::abs(segment.geometry.thickness - expected) > 1e-12) {
        std::cerr << "FAIL: density finalization ignored the configured physical window\n";
        return 1;
      }
    }
  }
  return 0;
}
