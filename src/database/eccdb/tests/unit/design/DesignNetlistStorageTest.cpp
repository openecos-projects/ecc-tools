// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <stdexcept>

#include "DesignTestFixture.h"

namespace eccdb {
namespace {

TEST_F(DesignStorageTest, MaterializesInstancePinsAndConnectsBothPinKinds)
{
  auto& netlist = design.netlistStorage();
  const auto instance = netlist.createInstance(createInstance("u1", Point{200, 400}));
  const auto signal = netlist.createNet(DesignNet{.name = "n1", .use = DesignSignalUse::kSignal});
  const auto power = netlist.createSpecialNet(DesignNet{.name = "VDD", .use = DesignSignalUse::kPower});
  const auto io = netlist.createIoPin(DesignIoPin{.name = "IN",
                                                  .direction = DesignIoPinDirection::kInput,
                                                  .use = DesignSignalUse::kSignal,
                                                  .ports = {{.flags = DesignIoPinPortFlag::kHasPlacement,
                                                             .placement_status = DesignPlacementStatus::kFixed,
                                                             .origin = {0, 500},
                                                             .rectangles = {{.layer = layer, .rectangle = {-10, -10, 10, 10}}}}}});

  ASSERT_EQ(netlist.instancePins(instance).size(), 2u);
  const auto a = netlist.findInstancePin(instance, input_term);
  const auto y = netlist.findInstancePin(instance, "Y");
  ASSERT_TRUE(a);
  ASSERT_TRUE(y);
  EXPECT_EQ(netlist.instancePin(a).instance, instance);
  EXPECT_EQ(netlist.instancePin(y).master_term, output_term);

  netlist.connect(a, signal);
  netlist.connect(io, signal);
  netlist.connect(y, power);
  netlist.connect(a, power);
  netlist.connect(io, power);

  ASSERT_EQ(netlist.instancePins(signal).size(), 1u);
  EXPECT_EQ(netlist.instancePins(signal).front(), a);
  ASSERT_EQ(netlist.ioPins(signal).size(), 1u);
  EXPECT_EQ(netlist.ioPins(signal).front(), io);
  const auto power_pins = netlist.instancePins(power);
  ASSERT_EQ(power_pins.size(), 2u);
  EXPECT_NE(std::find(power_pins.begin(), power_pins.end(), a), power_pins.end());
  EXPECT_NE(std::find(power_pins.begin(), power_pins.end(), y), power_pins.end());
  ASSERT_EQ(netlist.ioPins(power).size(), 1u);
  EXPECT_EQ(netlist.ioPins(power).front(), io);
  EXPECT_EQ(netlist.instancePin(a).net, signal);
  EXPECT_EQ(netlist.instancePin(a).special_net, power);
  EXPECT_EQ(netlist.ioPin(io).net, signal);
  EXPECT_EQ(netlist.ioPin(io).special_net, power);
  EXPECT_TRUE(netlist.isSpecialNet(power));
  EXPECT_FALSE(netlist.isSpecialNet(signal));
  EXPECT_EQ(netlist.regularNets().size(), 1u);
  EXPECT_EQ(netlist.specialNets().size(), 1u);
  EXPECT_TRUE(netlist.referencesMaster(master));
  EXPECT_TRUE(netlist.referencesMasterTerm(input_term));
  EXPECT_EQ(netlist.instanceCount(), 1u);
  EXPECT_EQ(netlist.instancePinCount(), 2u);
  EXPECT_EQ(netlist.ioPinCount(), 1u);
  EXPECT_EQ(netlist.netCount(), 2u);

  std::vector<DesignInstancePinId> visited_instance_pins;
  netlist.forEachInstancePin([&](DesignInstancePinId id, const DesignInstancePin& pin) {
    visited_instance_pins.push_back(id);
    EXPECT_EQ(pin.instance, instance);
  });
  EXPECT_EQ(visited_instance_pins.size(), 2u);
  EXPECT_NE(std::find(visited_instance_pins.begin(), visited_instance_pins.end(), a), visited_instance_pins.end());
  EXPECT_NE(std::find(visited_instance_pins.begin(), visited_instance_pins.end(), y), visited_instance_pins.end());

  std::vector<DesignIoPinId> visited_io_pins;
  netlist.forEachIoPin([&](DesignIoPinId id, const DesignIoPin& pin) {
    visited_io_pins.push_back(id);
    EXPECT_EQ(pin.name, "IN");
  });
  EXPECT_EQ(visited_io_pins, (std::vector<DesignIoPinId>{io}));

  std::vector<DesignInstanceId> visited_instances;
  netlist.forEachInstance([&](DesignInstanceId id, const DesignInstance& value, std::span<const DesignInstancePinId> pins) {
    visited_instances.push_back(id);
    EXPECT_EQ(value.name, "u1");
    EXPECT_EQ(pins.size(), 2u);
  });
  EXPECT_EQ(visited_instances.size(), 1u);
  EXPECT_EQ(visited_instances.front(), instance);

  std::vector<DesignNetId> visited_regular_nets;
  netlist.forEachRegularNet([&](DesignNetId id, const DesignNet& value) {
    visited_regular_nets.push_back(id);
    EXPECT_EQ(value.name, "n1");
  });
  EXPECT_EQ(visited_regular_nets, (std::vector<DesignNetId>{signal}));

  std::vector<DesignNetId> visited_special_nets;
  netlist.forEachSpecialNet([&](DesignNetId id, const DesignNet& value) {
    visited_special_nets.push_back(id);
    EXPECT_EQ(value.name, "VDD");
  });
  EXPECT_EQ(visited_special_nets, (std::vector<DesignNetId>{power}));

  EXPECT_FALSE(netlist.destroyNet(signal));
  EXPECT_FALSE(netlist.destroyInstance(instance));
  EXPECT_FALSE(netlist.destroyIoPin(io));

  netlist.disconnect(a, signal);
  netlist.disconnect(io, signal);
  EXPECT_EQ(netlist.instancePin(a).special_net, power);
  EXPECT_EQ(netlist.ioPin(io).special_net, power);
  netlist.disconnect(a);
  netlist.disconnect(io);
  netlist.disconnect(y);
  EXPECT_TRUE(netlist.destroyNet(signal));
  EXPECT_TRUE(netlist.destroyNet(power));
  EXPECT_TRUE(netlist.destroyIoPin(io));
  EXPECT_TRUE(netlist.destroyInstance(instance));
  EXPECT_EQ(netlist.instancePinCount(), 0u);
}

TEST_F(DesignStorageTest, RejectsBrokenNetlistReferencesAndManagedFieldUpdates)
{
  auto& netlist = design.netlistStorage();
  const auto instance = netlist.createInstance(createInstance("u1"));
  const auto net = netlist.createNet(DesignNet{.name = "n1"});

  EXPECT_THROW((void) netlist.createInstance(createInstance("u1")), std::invalid_argument);
  EXPECT_THROW((void) netlist.createNet(DesignNet{.name = "n1"}), std::invalid_argument);
  EXPECT_THROW(netlist.connect(DesignInstancePinId{}, net), std::out_of_range);

  auto replacement = createInstance("u1_new");
  const auto other_master = library.cellMasterStorage().createCellMaster(LibraryCellMaster{.name = "BUF"});
  replacement.master = other_master;
  EXPECT_THROW(netlist.updateInstance(instance, replacement), std::invalid_argument);

  replacement.master = master;
  netlist.updateInstance(instance, replacement);
  EXPECT_EQ(netlist.findInstance("u1"), DesignInstanceId{});
  EXPECT_EQ(netlist.findInstance("u1_new"), instance);

  EXPECT_TRUE(netlist.destroyNet(net));
  EXPECT_TRUE(netlist.destroyInstance(instance));
}

TEST_F(DesignStorageTest, RejectsInvalidNetlistEnums)
{
  auto& netlist = design.netlistStorage();

  auto instance = createInstance("u1");
  instance.placement_status = static_cast<DesignPlacementStatus>(255);
  EXPECT_THROW((void) netlist.createInstance(instance), std::invalid_argument);

  auto io = DesignIoPin{.name = "IN"};
  io.direction = static_cast<DesignIoPinDirection>(255);
  EXPECT_THROW((void) netlist.createIoPin(io), std::invalid_argument);

  auto net = DesignNet{.name = "n1"};
  net.source = static_cast<DesignNetSource>(255);
  EXPECT_THROW((void) netlist.createNet(net), std::invalid_argument);
}

TEST_F(DesignStorageTest, StoresComponentConstraintsAndNetNonDefaultRule)
{
  auto& constraints = design.constraintStorage();
  const auto region
      = constraints.createRegion(DesignRegion{.name = "FENCE", .type = DesignRegionType::kFence, .rectangles = {{0, 0, 1000, 1000}}});

  auto named = createInstance("named");
  named.flags
      = DesignInstanceFlag::kHasRegion | DesignInstanceFlag::kHasHalo | DesignInstanceFlag::kHaloSoft | DesignInstanceFlag::kHasRouteHalo;
  named.region = region;
  named.halo = DesignInstanceHalo{.left = 10, .bottom = 20, .right = 30, .top = 40};
  named.route_halo = DesignInstanceRouteHalo{.distance = 50, .min_layer = routing_layer, .max_layer = routing_layer};
  const auto named_id = design.netlistStorage().createInstance(named);

  auto bounded = createInstance("bounded");
  bounded.flags = DesignInstanceFlag::kHasRegionBounds;
  bounded.region_bounds = {{100, 200, 300, 400}, {500, 600, 700, 800}};
  const auto bounded_id = design.netlistStorage().createInstance(bounded);

  const auto net = design.netlistStorage().createNet(
      DesignNet{.name = "wide_net", .flags = DesignNetFlag::kHasNonDefaultRule, .non_default_rule = non_default_rule});

  const auto& stored_named = design.netlistStorage().instance(named_id);
  EXPECT_EQ(stored_named.region, region);
  EXPECT_EQ(stored_named.halo.left, 10);
  EXPECT_EQ(stored_named.route_halo.distance, 50);
  EXPECT_EQ(design.netlistStorage().instance(bounded_id).region_bounds.size(), 2u);
  EXPECT_EQ(design.netlistStorage().net(net).non_default_rule, non_default_rule);
  EXPECT_FALSE(constraints.destroyRegion(region));

  EXPECT_TRUE(design.netlistStorage().destroyNet(net));
  EXPECT_TRUE(design.netlistStorage().destroyInstance(named_id));
  EXPECT_TRUE(design.netlistStorage().destroyInstance(bounded_id));
  EXPECT_TRUE(constraints.destroyRegion(region));
}

TEST_F(DesignStorageTest, RejectsInconsistentComponentConstraintAndNetNdrFields)
{
  auto instance = createInstance("u1");
  instance.flags = DesignInstanceFlag::kHaloSoft;
  EXPECT_THROW((void) design.netlistStorage().createInstance(instance), std::invalid_argument);

  instance = createInstance("u2");
  instance.flags = DesignInstanceFlag::kHasRegionBounds;
  EXPECT_THROW((void) design.netlistStorage().createInstance(instance), std::invalid_argument);

  instance = createInstance("u3");
  instance.flags = DesignInstanceFlag::kHasRouteHalo;
  instance.route_halo.distance = 10;
  instance.route_halo.min_layer = routing_layer;
  EXPECT_THROW((void) design.netlistStorage().createInstance(instance), std::invalid_argument);

  EXPECT_THROW((void) design.netlistStorage().createNet(DesignNet{.name = "broken", .flags = DesignNetFlag::kHasNonDefaultRule}),
               std::invalid_argument);
}

TEST_F(DesignStorageTest, StoresAndValidatesOptionalNetOptions)
{
  auto& netlist = design.netlistStorage();
  const auto regular = netlist.createNet(DesignNet{.name = "signal"});
  const auto special = netlist.createSpecialNet(DesignNet{.name = "VDD"});

  netlist.setNetOptions(regular,
                        DesignNetOptions{.flags = DesignNetOptionsFlag::kHasOriginal | DesignNetOptionsFlag::kHasPattern
                                                  | DesignNetOptionsFlag::kHasEstimatedCapacitance | DesignNetOptionsFlag::kHasFrequency
                                                  | DesignNetOptionsFlag::kHasXTalk | DesignNetOptionsFlag::kHasStyle,
                                         .original = "source_signal",
                                         .pattern = DesignNetPattern::kSteiner,
                                         .estimated_capacitance = 1.25,
                                         .frequency = 2.5,
                                         .xtalk = 4,
                                         .style = 0});
  netlist.setNetOptions(
      special,
      DesignNetOptions{
          .flags = DesignNetOptionsFlag::kHasVoltage,
          .voltage = 900,
          .spacing_rules
          = {{.layer = routing_layer, .spacing = 12, .flags = DesignNetSpacingRuleFlag::kHasRange, .range_left = 20, .range_right = 40}}});

  const auto* regular_options = netlist.netOptions(regular);
  ASSERT_NE(regular_options, nullptr);
  EXPECT_EQ(regular_options->pattern, DesignNetPattern::kSteiner);
  EXPECT_EQ(regular_options->style, 0);
  const auto* special_options = netlist.netOptions(special);
  ASSERT_NE(special_options, nullptr);
  EXPECT_EQ(special_options->voltage, 900);
  ASSERT_EQ(special_options->spacing_rules.size(), 1u);
  EXPECT_EQ(special_options->spacing_rules[0].range_right, 40);

  EXPECT_THROW(netlist.setNetOptions(regular, DesignNetOptions{.flags = DesignNetOptionsFlag::kHasVoltage, .voltage = 900}),
               std::invalid_argument);
  EXPECT_THROW(netlist.setNetOptions(special, DesignNetOptions{.flags = DesignNetOptionsFlag::kHasXTalk, .xtalk = 1}),
               std::invalid_argument);
  EXPECT_THROW(netlist.setNetOptions(special, DesignNetOptions{.spacing_rules = {{.layer = routing_layer,
                                                                                  .spacing = 10,
                                                                                  .flags = DesignNetSpacingRuleFlag::kHasRange,
                                                                                  .range_left = 40,
                                                                                  .range_right = 20}}}),
               std::invalid_argument);

  EXPECT_TRUE(netlist.destroyNet(regular));
  EXPECT_TRUE(netlist.destroyNet(special));
}

}  // namespace
}  // namespace eccdb
