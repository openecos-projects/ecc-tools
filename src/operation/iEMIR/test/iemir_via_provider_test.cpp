// SPDX-License-Identifier: MulanPSL-2.0
// LEF CUT resistance inheritance and VIA/VIARULE precedence.
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "EMIRInterface.hpp"
#include "IdbLayer.h"
#include "IdbPins.h"
#include "IdbViaMaster.h"
#include "IdbViaRule.h"
#include "IdbVias.h"
#include "PowerNet.hpp"
#include "idm.h"

namespace {
void requireResistance(iemir::PowerNet& net, double expected)
{
  auto& vias = net.get_via_list();
  if (vias.empty() || std::abs(vias.back().get_resistance() - expected) > 1e-12) {
    throw std::runtime_error("incorrect VIA resistance inheritance");
  }
}
}

int main(int argc, char** argv)
{
  try {
    if (argc != 2 || !dmInst->readLef({argv[1]}, true)) {
      throw std::runtime_error("CUT resistance LEF import failed");
    }
    auto* parsed_cut = dynamic_cast<idb::IdbLayerCut*>(dmInst->get_idb_layout()->get_layers()->find_layer("V1"));
    if (parsed_cut == nullptr || parsed_cut->get_resistance() != 12.0) {
      throw std::runtime_error("LEF reader lost CUT RESISTANCE");
    }
    idb::IdbLayerRouting bottom;
    bottom.set_type(idb::IdbLayerType::kLayerRouting);
    bottom.set_id(0);
    idb::IdbLayerRouting top;
    top.set_type(idb::IdbLayerType::kLayerRouting);
    top.set_id(1);
    idb::IdbLayerCut cut;
    cut.set_type(idb::IdbLayerType::kLayerCut);
    cut.set_resistance(parsed_cut->get_resistance());
    idb::IdbVia via;
    auto* master = via.get_instance();
    master->get_bottom_layer_shape()->set_layer(&bottom);
    master->get_top_layer_shape()->set_layer(&top);
    master->get_cut_layer_shape()->set_layer(&cut);
    master->get_bottom_layer_shape()->add_rect(-4, -2, 3, 2);
    master->get_top_layer_shape()->add_rect(-5, -3, 4, 3);
    master->get_cut_layer_shape()->add_rect(-3, -1, -1, 1);
    master->get_cut_layer_shape()->add_rect(1, -1, 3, 1);
    via.set_coordinate(10, 20);
    iemir::PowerNet net;
    EMIRI.wrapPowerVia(net, &via);
    requireResistance(net, 6.0);  // 12 ohms/cut, two parallel cuts.
    const auto& first_via = net.get_via_list().back();
    if (first_via.get_bottom_rect_list().size() != 1u || first_via.get_top_rect_list().size() != 1u
        || first_via.get_bottom_rect_list().front().ll_x != 6 || first_via.get_bottom_rect_list().front().ll_y != 18
        || first_via.get_bottom_rect_list().front().ur_x != 13 || first_via.get_bottom_rect_list().front().ur_y != 22
        || first_via.get_top_rect_list().front().ll_x != 5 || first_via.get_top_rect_list().front().ll_y != 17
        || first_via.get_top_rect_list().front().ur_x != 14 || first_via.get_top_rect_list().front().ur_y != 23) {
      throw std::runtime_error("legacy provider lost placed VIA metal rectangles");
    }

    idb::IdbViaRuleGenerate rule;
    rule.set_resistance_per_cut(8.0);
    master->get_master_generate()->set_rule_generate(&rule);
    EMIRI.wrapPowerVia(net, &via);
    requireResistance(net, 4.0);

    master->set_resistance(3.0);
    EMIRI.wrapPowerVia(net, &via);
    requireResistance(net, 3.0);  // VIA resistance is already a whole-VIA value.
    // Rule pointers are borrowed from the technology DB.
    master->get_master_generate()->set_rule_generate(nullptr);

    idb::IdbPin pin;
    pin.set_as_io();
    pin.set_pin_name("VDD");
    auto* term = pin.set_term();
    term->set_has_port(true);
    term->set_placement_status_place();
    auto* port0 = term->add_port();
    port0->set_coordinate(100, 200);
    port0->set_orient(idb::IdbOrient::kN_R0);
    port0->set_placement_status_place();
    auto* metal0 = port0->add_layer_shape();
    metal0->set_layer(&bottom);
    metal0->add_rect(0, 0, 10, 4);
    metal0->add_rect(3, -5, 4, 5);
    auto* metal1 = port0->add_layer_shape();
    metal1->set_layer(&top);
    metal1->add_rect(0, 0, 10, 4);
    auto* cuts = port0->add_layer_shape();
    cuts->set_layer(&cut);
    cuts->add_rect(1, 1, 2, 2);
    auto* port1 = term->add_port();
    port1->set_coordinate(100, 200);
    port1->set_orient(idb::IdbOrient::kN_R0);
    port1->set_placement_status_place();
    auto* other_metal = port1->add_layer_shape();
    other_metal->set_layer(&bottom);
    other_metal->add_rect(20, 0, 30, 4);
    pin.set_port_layer_shape();
    iemir::PowerNet pin_net;
    EMIRI.wrapPowerPin(pin_net, &pin, true);
    auto& physical_pins = pin_net.get_pin_list();
    if (physical_pins.size() != 4 || !physical_pins[0].has_bounds()
        || physical_pins[0].get_ll_x() != 100 || physical_pins[0].get_ll_y() != 200
        || physical_pins[1].get_ll_y() != 195 || !physical_pins[0].has_port_id()
        || physical_pins[0].get_port_id() != physical_pins[1].get_port_id()
        || physical_pins[0].get_port_id() != physical_pins[2].get_port_id()
        || physical_pins[0].get_port_id() == physical_pins[3].get_port_id()) {
      throw std::runtime_error("legacy provider lost individual pin bounds or PORT membership");
    }
    iemir::EMIRInterface::destroyInst();
    std::cout << "PASS: CUT inheritance, parallel cuts, VIA precedence, physical pin bounds and PORT identity\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
