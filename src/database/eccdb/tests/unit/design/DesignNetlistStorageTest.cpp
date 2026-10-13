// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

#include "DesignTestFixture.h"
#include "geometry/DomainGeometryComponents.h"

namespace eccdb {
namespace {

struct InstanceTermTestMarker
{
  int value = 0;
};

TEST_F(DesignStorageTest, BlockPinsHaveIndependentIdentityAndOwnedLifecycle)
{
  auto& netlist = design.netlistStorage();
  const auto terminal = netlist.createIoPin(
      DesignBlockTerm{.name = "IN"},
      {{.flags = DesignIoPinPortFlag::kExplicit,
        .rectangles = {{.layer = layer, .rectangle = {0, 0, 20, 20}}}},
       {.flags = DesignIoPinPortFlag::kExplicit,
        .rectangles = {{.layer = layer, .rectangle = {100, 0, 120, 20}}}}});
  const auto ids = netlist.blockPins(terminal);
  ASSERT_EQ(ids.size(), 2u);
  const auto first = ids[0];
  const auto second = ids[1];
  EXPECT_NE(first.entity(), terminal.entity());
  EXPECT_NE(first, second);
  EXPECT_TRUE(design.designRegistry().registry().all_of<DesignBlockPin>(first.entity()));
  EXPECT_EQ(netlist.blockPin(first).term, terminal);
  EXPECT_EQ(netlist.blockPin(second).term, terminal);
  const auto first_shape = netlist.blockPin(first).rectangles.front();
  const auto original_second_shape = netlist.blockPin(second).rectangles.front();
  EXPECT_EQ(design.geometryRegistry().shape(first_shape).owner, designBlockPinOwner(first));
  EXPECT_EQ(netlist.blockPinRectangle(first_shape).rectangle, (Rect{0, 0, 20, 20}));

  const auto& original_second = netlist.blockPin(second);
  auto changed = DesignBlockPinInput{.flags = original_second.flags,
                                     .placement_status = original_second.placement_status,
                                     .origin = original_second.origin,
                                     .orientation = original_second.orientation,
                                     .rectangles = {{.layer = layer, .rectangle = {200, 0, 220, 20}}}};
  netlist.updateBlockPin(second, changed);
  EXPECT_EQ(netlist.blockPinRectangle(netlist.blockPin(first).rectangles.front()).rectangle, (Rect{0, 0, 20, 20}));
  const auto updated_second_shape = netlist.blockPin(second).rectangles.front();
  EXPECT_EQ(netlist.blockPinRectangle(updated_second_shape).rectangle, (Rect{200, 0, 220, 20}));
  EXPECT_FALSE(design.geometryRegistry().contains(original_second_shape));

  auto metadata = netlist.ioPin(terminal);
  metadata.direction = DesignIoPinDirection::kInput;
  netlist.updateIoPin(terminal, metadata);
  EXPECT_EQ(netlist.blockPins(terminal)[1], second);
  EXPECT_TRUE(netlist.destroyBlockPin(first));
  EXPECT_FALSE(netlist.contains(first));
  EXPECT_FALSE(design.geometryRegistry().contains(first_shape));
  ASSERT_EQ(netlist.blockPins(terminal).size(), 1u);
  EXPECT_EQ(netlist.blockPins(terminal).front(), second);
  const auto replacement = netlist.createBlockPin(terminal, changed);
  EXPECT_NE(replacement, first);
  EXPECT_FALSE(netlist.contains(first));
  EXPECT_EQ(netlist.blockPin(replacement).term, terminal);
  EXPECT_EQ(netlist.blockPinCount(), 2u);
  EXPECT_TRUE(netlist.destroyIoPin(terminal));
  EXPECT_FALSE(netlist.contains(second));
  EXPECT_FALSE(netlist.contains(replacement));
  EXPECT_FALSE(design.geometryRegistry().contains(updated_second_shape));
  EXPECT_EQ(netlist.blockPinCount(), 0u);
}

TEST_F(DesignStorageTest, AppendingExplicitBlockPinsUsesGeometricOwnerCapacity)
{
  auto& netlist = design.netlistStorage();
  auto& registry = design.designRegistry().registry();
  const auto term = netlist.createBlockTerm(DesignBlockTerm{.name = "MULTI_PORT"});
  std::vector<DesignBlockPinId> created;
  created.reserve(17);

  for (int32_t index = 0; index < 17; ++index) {
    const auto& before = registry.get<const DesignBlockTerm>(term.entity()).pins;
    const auto old_size = before.size();
    const auto old_capacity = before.capacity();
    const auto id = netlist.createBlockPin(
        term,
        DesignBlockPinInput{.flags = DesignIoPinPortFlag::kExplicit,
                            .rectangles = {{.layer = layer, .rectangle = {index * 20, 0, index * 20 + 10, 10}}}});
    created.push_back(id);

    const auto& pins = registry.get<const DesignBlockTerm>(term.entity()).pins;
    ASSERT_EQ(pins.size(), old_size + 1u);
    EXPECT_EQ(pins.back(), id);
    if (old_size == old_capacity) {
      EXPECT_GE(pins.capacity(), old_capacity == 0u ? 1u : old_capacity * 2u);
    } else {
      EXPECT_EQ(pins.capacity(), old_capacity);
    }
  }

  const std::vector<DesignBlockPinId> ports{netlist.blockPins(term).begin(), netlist.blockPins(term).end()};
  EXPECT_EQ(ports, created);
  for (const auto id : created) {
    EXPECT_TRUE(netlist.contains(id));
    EXPECT_EQ(netlist.blockPin(id).term, term);
  }
}

TEST_F(DesignStorageTest, RollsBackBlockTermWhenALaterBlockPinIsInvalid)
{
  auto& netlist = design.netlistStorage();
  const auto term_count = netlist.ioPinCount();
  const auto pin_count = netlist.blockPinCount();
  EXPECT_THROW(
      (void) netlist.createBlockTerm(
          DesignBlockTerm{.name = "IN"},
          {{.flags = DesignIoPinPortFlag::kExplicit,
            .rectangles = {{.layer = layer, .rectangle = {0, 0, 20, 20}}}},
           {.flags = DesignIoPinPortFlag::kExplicit,
            .rectangles = {{.layer = {}, .rectangle = {100, 0, 120, 20}}}}}),
      std::invalid_argument);
  EXPECT_FALSE(netlist.findBlockTerm("IN"));
  EXPECT_EQ(netlist.ioPinCount(), term_count);
  EXPECT_EQ(netlist.blockPinCount(), pin_count);
  EXPECT_NO_THROW(netlist.rebuildNameIndexes());

  const auto replacement = netlist.createBlockTerm(DesignBlockTerm{.name = "IN"});
  EXPECT_EQ(netlist.findBlockTerm("IN"), replacement);
  EXPECT_TRUE(netlist.blockPins(replacement).empty());
}

TEST_F(DesignStorageTest, MaterializesInstancePinsAndConnectsBothPinKinds)
{
  auto& netlist = design.netlistStorage();
  const auto instance = netlist.createInstance(createInstance("u1", Point{200, 400}));
  const auto signal = netlist.createNet(DesignNet{.name = "n1", .use = DesignSignalUse::kSignal});
  const auto power = netlist.createSpecialNet(DesignNet{.name = "VDD", .use = DesignSignalUse::kPower});
  const auto io = netlist.createIoPin(DesignIoPin{.name = "IN",
                                                  .direction = DesignIoPinDirection::kInput,
                                                  .use = DesignSignalUse::kSignal},
                                                  {{.flags = DesignIoPinPortFlag::kHasPlacement,
                                                             .placement_status = DesignPlacementStatus::kFixed,
                                                             .origin = {0, 500},
                                                             .rectangles = {{.layer = layer, .rectangle = {-10, -10, 10, 10}}}}});

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

TEST_F(DesignStorageTest, ReconnectsBothPinKindsAfterExplicitDisconnect)
{
  auto& netlist = design.netlistStorage();
  auto& registry = design.designRegistry().registry();
  const auto instance = netlist.createInstance(createInstance("u1"));
  const auto pin = netlist.findInstancePin(instance, "A");
  const auto io = netlist.createIoPin(DesignIoPin{.name = "IN"});
  const auto signal = netlist.createNet(DesignNet{.name = "n1"});
  const auto next_signal = netlist.createNet(DesignNet{.name = "n2"});
  const auto power = netlist.createSpecialNet(DesignNet{.name = "VDD"});
  const auto next_power = netlist.createSpecialNet(DesignNet{.name = "VDD2"});

  for (const auto [original, replacement] : {std::pair{signal, next_signal}, std::pair{power, next_power}}) {
    netlist.connect(pin, original);
    netlist.connect(io, original);
    ASSERT_EQ(netlist.instancePins(original).size(), 1u);
    EXPECT_EQ(netlist.instancePins(original).front(), pin);
    ASSERT_EQ(netlist.ioPins(original).size(), 1u);
    EXPECT_EQ(netlist.ioPins(original).front(), io);
    EXPECT_FALSE(registry.all_of<DesignNetInstancePins>(replacement.entity()));
    EXPECT_FALSE(registry.all_of<DesignNetIoPins>(replacement.entity()));

    netlist.disconnect(pin, original);
    netlist.disconnect(io, original);
    EXPECT_TRUE(netlist.instancePins(original).empty());
    EXPECT_TRUE(netlist.ioPins(original).empty());
    netlist.connect(pin, replacement);
    netlist.connect(io, replacement);
    ASSERT_EQ(netlist.instancePins(replacement).size(), 1u);
    EXPECT_EQ(netlist.instancePins(replacement).front(), pin);
    ASSERT_EQ(netlist.ioPins(replacement).size(), 1u);
    EXPECT_EQ(netlist.ioPins(replacement).front(), io);
  }

  EXPECT_EQ(netlist.instancePin(pin).net, next_signal);
  EXPECT_EQ(netlist.instancePin(pin).special_net, next_power);
  EXPECT_EQ(netlist.ioPin(io).net, next_signal);
  EXPECT_EQ(netlist.ioPin(io).special_net, next_power);
}

TEST_F(DesignStorageTest, StoresIndependentInstanceTermEntitiesAndInvalidatesDeletedIds)
{
  auto& netlist = design.netlistStorage();
  auto& registry = design.designRegistry().registry();
  const auto first = netlist.createInstance(createInstance("first"));
  const auto first_pin = netlist.findInstancePin(first, "A");
  const auto& terms = registry.get<const DesignInstanceTerms>(first.entity());
  ASSERT_EQ(terms.values.size(), 2u);
  ASSERT_EQ(terms.names.size(), 2u);
  EXPECT_EQ(terms.values.front(), first_pin);
  const auto named = terms.names.find("A");
  ASSERT_NE(named, terms.names.end());
  EXPECT_EQ(named->second, first_pin);
  ASSERT_TRUE(registry.valid(first_pin.entity()));
  ASSERT_TRUE(registry.all_of<DesignInstanceTerm>(first_pin.entity()));
  const auto* pin = &registry.get<const DesignInstanceTerm>(first_pin.entity());
  ASSERT_NE(pin, nullptr);
  EXPECT_EQ(pin, &netlist.instanceTerm(first_pin));
  EXPECT_EQ(pin, netlist.findInstanceTermData(first, "A"));
  EXPECT_EQ(pin->instance, first);
  EXPECT_EQ(pin->master_term, input_term);
  EXPECT_EQ(pin->instance_ptr, &netlist.instance(first));
  EXPECT_EQ(pin->master_term_ptr, &library.masterTermStorage().masterTerm(input_term));
  EXPECT_EQ(netlist.instancePin(first_pin).instance, first);
  EXPECT_EQ(netlist.instancePin(first_pin).master_term, input_term);
  registry.emplace<InstanceTermTestMarker>(first_pin.entity(), InstanceTermTestMarker{.value = 7});
  EXPECT_EQ(registry.get<const InstanceTermTestMarker>(first_pin.entity()).value, 7);

  const auto survivor = netlist.createInstance(createInstance("survivor"));
  const auto survivor_pin = netlist.findInstancePin(survivor, "A");
  const auto signal = netlist.createNet(DesignNet{.name = "survivor_signal"});
  netlist.connect(survivor_pin, signal);
  ASSERT_TRUE(netlist.destroyInstance(first));
  EXPECT_FALSE(netlist.contains(first_pin));
  EXPECT_FALSE(registry.valid(first_pin.entity()));
  EXPECT_FALSE(registry.all_of<DesignInstanceTerm>(first_pin.entity()));
  EXPECT_FALSE(registry.all_of<InstanceTermTestMarker>(first_pin.entity()));
  EXPECT_THROW((void) netlist.instancePin(first_pin), std::out_of_range);
  EXPECT_TRUE(netlist.contains(survivor_pin));
  EXPECT_EQ(netlist.findInstancePin(survivor, "A"), survivor_pin);
  EXPECT_EQ(netlist.instancePin(survivor_pin).instance, survivor);
  EXPECT_EQ(netlist.instancePin(survivor_pin).master_term, input_term);
  EXPECT_EQ(netlist.instancePin(survivor_pin).net, signal);
  EXPECT_EQ(netlist.instancePin(survivor_pin).instance_ptr, &netlist.instance(survivor));
  EXPECT_EQ(netlist.instancePin(survivor_pin).master_term_ptr, &library.masterTermStorage().masterTerm(input_term));
  ASSERT_EQ(netlist.instancePins(signal).size(), 1u);
  EXPECT_EQ(netlist.instancePins(signal).front(), survivor_pin);
  const auto replacement = netlist.createInstance(createInstance("replacement"));
  const auto replacement_pin = netlist.findInstancePin(replacement, "A");
  EXPECT_NE(first_pin, replacement_pin);
  const auto replacement_terms = netlist.instanceTerms(replacement);
  const auto reused_slot = std::find_if(replacement_terms.begin(), replacement_terms.end(), [first_pin](const auto id) {
    return entt::to_entity(id.entity()) == entt::to_entity(first_pin.entity());
  });
  ASSERT_NE(reused_slot, replacement_terms.end());
  EXPECT_NE(*reused_slot, first_pin);
  EXPECT_FALSE(registry.valid(first_pin.entity()));
  EXPECT_FALSE(registry.all_of<DesignInstanceTerm>(first_pin.entity()));

  netlist.rebuildNameIndexes();
  netlist.rebuildLibraryCaches();
  EXPECT_FALSE(netlist.contains(first_pin));
  EXPECT_EQ(netlist.findInstancePin(survivor, "A"), survivor_pin);
  EXPECT_EQ(netlist.instancePin(survivor_pin).instance, survivor);
  EXPECT_EQ(netlist.instancePin(survivor_pin).master_term, input_term);
  EXPECT_EQ(netlist.instancePin(survivor_pin).net, signal);
  EXPECT_EQ(netlist.instancePin(survivor_pin).instance_ptr, &netlist.instance(survivor));
  EXPECT_EQ(netlist.instancePin(survivor_pin).master_term_ptr, &library.masterTermStorage().masterTerm(input_term));
  ASSERT_EQ(netlist.instancePins(signal).size(), 1u);
  EXPECT_EQ(netlist.instancePins(signal).front(), survivor_pin);
  EXPECT_EQ(netlist.instancePinCount(), 4u);
}

TEST_F(DesignStorageTest, ExposesInstanceTermsThroughRegistryViewsWithOptionalComponents)
{
  auto& netlist = design.netlistStorage();
  auto& registry = design.designRegistry().registry();
  const auto first = netlist.createInstance(createInstance("first"));
  const auto second = netlist.createInstance(createInstance("second"));
  const auto first_input = netlist.findInstanceTerm(first, "A");
  const auto first_output = netlist.findInstanceTerm(first, "Y");
  const auto second_input = netlist.findInstanceTerm(second, "A");
  const auto second_output = netlist.findInstanceTerm(second, "Y");
  registry.emplace<InstanceTermTestMarker>(second_input.entity(), InstanceTermTestMarker{.value = 23});

  std::vector<DesignInstanceTermId> all_terms;
  const auto all_terms_view = registry.view<const DesignInstanceTerm>();
  for (const auto entity : all_terms_view) {
    const DesignInstanceTermId id{entity};
    all_terms.push_back(id);
    EXPECT_EQ(&registry.get<const DesignInstanceTerm>(entity), &netlist.instanceTerm(id));
  }
  EXPECT_EQ(all_terms.size(), 4u);
  for (const auto id : std::array{first_input, first_output, second_input, second_output}) {
    EXPECT_NE(std::find(all_terms.begin(), all_terms.end(), id), all_terms.end());
  }

  std::vector<DesignInstanceTermId> marked_terms;
  const auto marked_terms_view = registry.view<const DesignInstanceTerm, const InstanceTermTestMarker>();
  for (const auto entity : marked_terms_view) {
    marked_terms.emplace_back(entity);
    EXPECT_EQ(registry.get<const InstanceTermTestMarker>(entity).value, 23);
  }
  EXPECT_EQ(marked_terms, (std::vector<DesignInstanceTermId>{second_input}));
}

TEST_F(DesignStorageTest, RollsBackIndependentTermsWhenMasterHasDuplicateTermName)
{
  auto& netlist = design.netlistStorage();
  auto& registry = design.designRegistry().registry();
  auto& malformed_master = library.libraryRegistry().registry().get<LibraryCellMaster>(master.entity());
  malformed_master.terms.push_back(input_term);
  const auto term_count = [&registry] {
    std::size_t count = 0;
    for (const auto entity : registry.view<const DesignInstanceTerm>()) {
      static_cast<void>(entity);
      ++count;
    }
    return count;
  };

  EXPECT_EQ(term_count(), 0u);
  EXPECT_THROW((void) netlist.createInstance(createInstance("malformed")), std::logic_error);
  EXPECT_EQ(term_count(), 0u);
  EXPECT_EQ(netlist.instanceCount(), 0u);
  EXPECT_FALSE(netlist.findInstance("malformed"));
  EXPECT_NO_THROW(netlist.rebuildNameIndexes());
}

TEST_F(DesignStorageTest, RejectsBrokenNetlistReferencesAndManagedFieldUpdates)
{
  auto& netlist = design.netlistStorage();
  const auto instance = netlist.createInstance(createInstance("u1"));
  const auto net = netlist.createNet(DesignNet{.name = "n1"});

  EXPECT_THROW((void) netlist.createInstance(createInstance("u1")), std::invalid_argument);
  EXPECT_THROW((void) netlist.createNet(DesignNet{.name = "n1"}), std::invalid_argument);

  auto replacement = createInstance("u1_new");
  const auto other_master = library.cellMasterStorage().createCellMaster(LibraryCellMaster{.name = "BUF"});
  replacement.master = other_master;
  EXPECT_THROW(netlist.updateInstance(instance, replacement), std::invalid_argument);

  replacement.master = master;
  netlist.updateInstance(instance, replacement);
  EXPECT_EQ(netlist.findInstance("u1"), DesignInstanceId{});
  EXPECT_EQ(netlist.findInstance("u1_new"), instance);
  EXPECT_TRUE(netlist.findInstancePin(instance, "A"));

  EXPECT_TRUE(netlist.destroyNet(net));
  EXPECT_TRUE(netlist.destroyInstance(instance));
}

TEST_F(DesignStorageTest, FindsInstancePinsByIndexedMasterTermNameAfterMasterTermFinalization)
{
  auto& netlist = design.netlistStorage();
  library.masterTermStorage().updateMasterTerm(
      output_term, LibraryMasterTerm{.name = "Z", .direction = LibraryMasterTermDirection::kOutput});
  const auto instance = netlist.createInstance(createInstance("u1"));

  const auto a = netlist.findInstancePin(instance, input_term);
  ASSERT_TRUE(a);
  EXPECT_EQ(netlist.instancePin(a).master_term, input_term);
  EXPECT_FALSE(netlist.findInstancePin(instance, "Y"));
  const auto z = netlist.findInstancePin(instance, "Z");
  ASSERT_TRUE(z);
  EXPECT_EQ(netlist.instancePin(z).master_term, output_term);
}

TEST_F(DesignStorageTest, RebuildsInstanceTermNameCacheWithoutMovingTermPayload)
{
  auto& netlist = design.netlistStorage();
  const auto& registry = design.designRegistry().registry();
  const auto instance = netlist.createInstance(createInstance("u1"));
  const auto& initial_terms = registry.get<const DesignInstanceTerms>(instance.entity());
  ASSERT_EQ(initial_terms.values.size(), 2u);
  ASSERT_EQ(initial_terms.names.size(), 2u);
  EXPECT_NE(initial_terms.names.find("Y"), initial_terms.names.end());
  const auto output_pin = netlist.findInstancePin(instance, "Y");
  ASSERT_TRUE(output_pin);
  const auto* const original_term = &registry.get<const DesignInstanceTerm>(output_pin.entity());

  library.masterTermStorage().updateMasterTerm(
      output_term, LibraryMasterTerm{.name = "Z", .direction = LibraryMasterTermDirection::kOutput});
  EXPECT_EQ(netlist.findInstancePin(instance, "Y"), output_pin);
  EXPECT_FALSE(netlist.findInstancePin(instance, "Z"));
  netlist.rebuildLibraryCaches();
  const auto& rebound_terms = registry.get<const DesignInstanceTerms>(instance.entity());
  EXPECT_EQ(rebound_terms.names.find("Y"), rebound_terms.names.end());
  EXPECT_NE(rebound_terms.names.find("Z"), rebound_terms.names.end());
  EXPECT_EQ(&registry.get<const DesignInstanceTerm>(output_pin.entity()), original_term);
  EXPECT_EQ(netlist.findInstanceTermData(instance, "Z"), &netlist.instanceTerm(output_pin));
  EXPECT_EQ(netlist.instancePin(output_pin).master_term_ptr, &library.masterTermStorage().masterTerm(output_term));
  EXPECT_EQ(netlist.findInstancePin(instance, "Z"), output_pin);
}

TEST_F(DesignStorageTest, FindsPinOrdinalsAfterMasterTermsAreFinalized)
{
  auto& netlist = design.netlistStorage();
  auto& terms = library.masterTermStorage();
  const auto added = terms.createMasterTerm(
      master, LibraryMasterTerm{.name = "NEW", .direction = LibraryMasterTermDirection::kInput});
  ASSERT_TRUE(terms.destroyMasterTerm(input_term));
  const auto instance = netlist.createInstance(createInstance("u1"));
  const auto output_pin = netlist.findInstancePin(instance, "Y");
  const auto added_pin = netlist.findInstancePin(instance, "NEW");
  ASSERT_TRUE(output_pin);
  ASSERT_TRUE(added_pin);
  EXPECT_FALSE(netlist.findInstancePin(instance, "A"));
  EXPECT_FALSE(netlist.findInstancePin(instance, "MISSING"));
  EXPECT_EQ(netlist.instancePin(output_pin).master_term, output_term);
  EXPECT_EQ(netlist.instancePin(added_pin).master_term, added);
  ASSERT_EQ(netlist.instancePins(instance).size(), 2u);
  EXPECT_EQ(netlist.instancePins(instance)[0], output_pin);
  EXPECT_EQ(netlist.instancePins(instance)[1], added_pin);
}

TEST_F(DesignStorageTest, ReturnsNoPinForMasterWithoutTerms)
{
  auto& netlist = design.netlistStorage();
  const auto empty_master = library.cellMasterStorage().createCellMaster(LibraryCellMaster{.name = "EMPTY"});
  const auto instance = netlist.createInstance(DesignInstance{.name = "empty", .master = empty_master});
  EXPECT_TRUE(netlist.instancePins(instance).empty());
  EXPECT_FALSE(netlist.findInstancePin(instance, "A"));
}

TEST_F(DesignStorageTest, KeepsInstancePinPointersAndRelationsAcrossOtherInstanceDeletionAndUpdate)
{
  auto& netlist = design.netlistStorage();
  const auto& registry = design.designRegistry().registry();
  const auto first = netlist.createInstance(createInstance("first"));
  const auto second = netlist.createInstance(createInstance("second"));
  const auto pin = netlist.findInstancePin(second, "A");
  const auto net = netlist.createNet(DesignNet{.name = "n1"});
  ASSERT_TRUE(pin);
  netlist.connect(pin, net);
  const auto* cached_pin = netlist.findInstanceTermData(second, "A");
  ASSERT_NE(cached_pin, nullptr);
  const auto* const registry_pin = &registry.get<const DesignInstanceTerm>(pin.entity());
  EXPECT_EQ(cached_pin, &netlist.instanceTerm(pin));
  EXPECT_EQ(cached_pin, registry_pin);
  const auto* cached_instance = cached_pin->instance_ptr;
  const auto* cached_term = cached_pin->master_term_ptr;
  ASSERT_EQ(cached_instance, &netlist.instance(second));
  ASSERT_EQ(cached_term, &library.masterTermStorage().masterTerm(input_term));
  EXPECT_EQ(netlist.instancePin(pin).instance, second);
  EXPECT_EQ(netlist.instancePin(pin).master_term, input_term);
  EXPECT_EQ(netlist.instancePin(pin).net, net);
  EXPECT_EQ(netlist.findInstancePin(second, "A"), pin);

  ASSERT_TRUE(netlist.destroyInstance(first));
  EXPECT_EQ(netlist.instanceCount(), 1u);
  EXPECT_EQ(netlist.findInstancePin(second, "A"), pin);
  EXPECT_EQ(&netlist.instancePin(pin), cached_pin);
  EXPECT_EQ(&registry.get<const DesignInstanceTerm>(pin.entity()), registry_pin);
  EXPECT_EQ(netlist.instancePin(pin).instance_ptr, cached_instance);
  EXPECT_EQ(netlist.instancePin(pin).master_term_ptr, cached_term);
  EXPECT_EQ(netlist.instancePin(pin).instance, second);
  EXPECT_EQ(netlist.instancePin(pin).master_term, input_term);
  EXPECT_EQ(netlist.instancePin(pin).net, net);
  ASSERT_EQ(netlist.instancePins(net).size(), 1u);
  EXPECT_EQ(netlist.instancePins(net).front(), pin);

  netlist.updateInstance(second, createInstance("second_renamed"));
  EXPECT_EQ(netlist.findInstance("second_renamed"), second);
  EXPECT_EQ(netlist.findInstancePin(second, "A"), pin);
  EXPECT_EQ(&netlist.instancePin(pin), cached_pin);
  EXPECT_EQ(&registry.get<const DesignInstanceTerm>(pin.entity()), registry_pin);
  EXPECT_EQ(netlist.instancePin(pin).instance_ptr, cached_instance);
  EXPECT_EQ(netlist.instancePin(pin).master_term_ptr, cached_term);
  EXPECT_EQ(cached_instance->name, "second_renamed");
  EXPECT_EQ(netlist.instancePin(pin).instance, second);
  EXPECT_EQ(netlist.instancePin(pin).master_term, input_term);
  EXPECT_EQ(netlist.instancePin(pin).net, net);
  ASSERT_EQ(netlist.instancePins(net).size(), 1u);
  EXPECT_EQ(netlist.instancePins(net).front(), pin);
}

TEST_F(DesignStorageTest, FindsInstancePinsAfterRemovingMasterTermBeforeInstanceCreation)
{
  auto& netlist = design.netlistStorage();
  ASSERT_TRUE(library.masterTermStorage().destroyMasterTerm(input_term));
  const auto instance = netlist.createInstance(createInstance("u1"));

  EXPECT_FALSE(netlist.findInstancePin(instance, "A"));
  const auto y = netlist.findInstancePin(instance, "Y");
  ASSERT_TRUE(y);
  EXPECT_EQ(netlist.instancePin(y).master_term, output_term);
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
  const auto region = constraints.createRegion(
      DesignRegionInput{.name = "FENCE", .type = DesignRegionType::kFence, .rectangles = {{0, 0, 1000, 1000}}});

  auto named = createInstance("named");
  named.flags
      = DesignInstanceFlag::kHasRegion | DesignInstanceFlag::kHasHalo | DesignInstanceFlag::kHaloSoft | DesignInstanceFlag::kHasRouteHalo;
  named.region = region;
  named.halo = DesignInstanceHalo{.left = 10, .bottom = 20, .right = 30, .top = 40};
  named.route_halo = DesignInstanceRouteHalo{.distance = 50, .min_layer = routing_layer, .max_layer = routing_layer};
  const auto named_id = design.netlistStorage().createInstance(named);

  auto bounded = createInstance("bounded");
  bounded.flags = DesignInstanceFlag::kHasRegionBounds;
  const std::array<Rect, 2> region_bounds{{{100, 200, 300, 400}, {500, 600, 700, 800}}};
  const auto bounded_id = design.netlistStorage().createInstance(bounded, region_bounds);

  const auto net = design.netlistStorage().createNet(
      DesignNet{.name = "wide_net", .flags = DesignNetFlag::kHasNonDefaultRule, .non_default_rule = non_default_rule});

  const auto& stored_named = design.netlistStorage().instance(named_id);
  EXPECT_EQ(stored_named.region, region);
  EXPECT_EQ(stored_named.halo.left, 10);
  EXPECT_EQ(stored_named.route_halo.distance, 50);
  const auto stored_bounds = design.netlistStorage().instance(bounded_id).region_bounds;
  ASSERT_EQ(stored_bounds.size(), 2u);
  EXPECT_EQ(design.geometryRegistry().shape(stored_bounds.front()).owner, designInstanceOwner(bounded_id));
  EXPECT_EQ(design.geometryRegistry().get<const GeometryRectangle>(stored_bounds.front()).rectangle, region_bounds.front());
  EXPECT_EQ(design.netlistStorage().net(net).non_default_rule, non_default_rule);
  EXPECT_FALSE(constraints.destroyRegion(region));

  EXPECT_TRUE(design.netlistStorage().destroyNet(net));
  EXPECT_TRUE(design.netlistStorage().destroyInstance(named_id));
  EXPECT_TRUE(design.netlistStorage().destroyInstance(bounded_id));
  EXPECT_FALSE(design.geometryRegistry().contains(stored_bounds.front()));
  EXPECT_FALSE(design.geometryRegistry().contains(stored_bounds.back()));
  EXPECT_TRUE(constraints.destroyRegion(region));
}

TEST_F(DesignStorageTest, PreservesZeroDistanceRouteHaloAndRejectsNegativeDistance)
{
  auto instance = createInstance("zero_route_halo");
  instance.flags = DesignInstanceFlag::kHasRouteHalo;
  instance.route_halo = {.distance = 0, .min_layer = routing_layer, .max_layer = routing_layer};
  const auto id = design.netlistStorage().createInstance(instance);
  const auto& stored = design.netlistStorage().instance(id);
  EXPECT_NE(stored.flags & DesignInstanceFlag::kHasRouteHalo, 0u);
  EXPECT_EQ(stored.route_halo.distance, 0);
  EXPECT_EQ(stored.route_halo.min_layer, routing_layer);
  EXPECT_EQ(stored.route_halo.max_layer, routing_layer);

  instance.name = "negative_route_halo";
  instance.route_halo.distance = -1;
  EXPECT_THROW((void) design.netlistStorage().createInstance(instance), std::invalid_argument);
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
