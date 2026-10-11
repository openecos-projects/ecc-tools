// ***************************************************************************************
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
/**
 * @file ClockIgnoreLoadTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-10-10
 * @brief Top-level IO pins on a clock net are ignore loads: read, clone, and writeback coverage.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "IdbCellMaster.h"
#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbTerm.h"
#include "data_manager/adapter/sdc/SDCClockReader.hh"
#include "data_manager/design/Clock.hh"
#include "data_manager/design/Design.hh"
#include "data_manager/design/Net.hh"
#include "data_manager/design/Pin.hh"
#include "data_manager/io/Wrapper.hh"

namespace icts_test {
namespace {

using PortSpec = std::pair<std::string, idb::IdbConnectDirection>;

class ClockIgnoreLoadFixtureInterface : public testing::Test
{
 protected:
  ClockIgnoreLoadFixtureInterface() : _idb_design(&_layout) {}

  auto addMaster(const std::string& name, const std::vector<PortSpec>& ports) -> idb::IdbCellMaster*
  {
    auto* master = _layout.get_cell_master_list()->set_cell_master(name);
    EXPECT_NE(master, nullptr);
    if (master == nullptr) {
      return nullptr;
    }
    master->set_type(idb::CellMasterType::kCore);
    for (const auto& [port_name, direction] : ports) {
      auto* term = master->add_term(port_name);
      EXPECT_NE(term, nullptr);
      if (term != nullptr) {
        term->set_direction(direction);
        term->set_type(idb::IdbConnectType::kSignal);
      }
    }
    return master;
  }

  auto addFlipFlop(const std::string& name, idb::IdbCellMaster* master) -> idb::IdbInstance*
  {
    auto* inst = _idb_design.get_instance_list()->add_instance(name);
    EXPECT_NE(inst, nullptr);
    if (inst != nullptr) {
      inst->set_cell_master(master);
      inst->set_coodinate(static_cast<int32_t>(_idb_design.get_instance_list()->get_instance_list().size() * 100), 0, false);
      inst->set_as_flip_flop_flag();
    }
    return inst;
  }

  auto addPort(const std::string& name, idb::IdbConnectDirection direction, int32_t x, int32_t y) -> idb::IdbPin*
  {
    auto* pin = _idb_design.createOrFindIoPin(name);
    EXPECT_NE(pin, nullptr);
    if (pin == nullptr) {
      return nullptr;
    }
    auto* term = pin->set_term();
    term->set_name(name);
    term->set_direction(direction);
    term->set_type(idb::IdbConnectType::kSignal);
    pin->set_average_coordinate(x, y);
    return pin;
  }

  auto connect(const std::string& net_name, const std::vector<idb::IdbPin*>& pins) -> idb::IdbNet*
  {
    auto* net = _idb_design.createOrFindNet(net_name, idb::IdbConnectType::kClock);
    EXPECT_NE(net, nullptr);
    for (auto* pin : pins) {
      EXPECT_NE(pin, nullptr);
      EXPECT_TRUE(_idb_design.connectPinToNet(pin, net));
    }
    return net;
  }

  static auto instPin(idb::IdbInstance* inst, const std::string& name) -> idb::IdbPin* { return inst == nullptr ? nullptr : inst->get_pin_by_term(name); }

  // clk_in drives net "clk" with flip_flop_count sequential sinks plus the given extra top-level ports.
  auto buildPassThroughDesign(std::size_t flip_flop_count, const std::vector<PortSpec>& extra_ports) -> idb::IdbNet*
  {
    auto* dff
        = addMaster("DFF_X1", {{"CLK", idb::IdbConnectDirection::kInput}, {"D", idb::IdbConnectDirection::kInput}, {"Q", idb::IdbConnectDirection::kOutput}});
    std::vector<idb::IdbPin*> pins;
    pins.push_back(addPort("clk_in", idb::IdbConnectDirection::kInput, 0, 0));
    for (std::size_t index = 0; index < flip_flop_count; ++index) {
      auto* flip_flop = addFlipFlop("ff" + std::to_string(index), dff);
      pins.push_back(instPin(flip_flop, "CLK"));
    }
    int32_t port_y = 1000;
    for (const auto& [port_name, direction] : extra_ports) {
      pins.push_back(addPort(port_name, direction, 1000, port_y));
      port_y += 100;
    }
    return connect("clk", pins);
  }

  auto readClock(const icts::ClockTraceClockTarget& target) -> icts::Clock*
  {
    _wrapper.set_idb_layout(&_layout);
    _wrapper.set_idb_design(&_idb_design);
    if (!_wrapper.readTraceClockTargets(_design, {target})) {
      return nullptr;
    }
    const auto clocks = _design.get_clocks();
    return clocks.empty() ? nullptr : clocks.front();
  }

  static auto target(std::vector<std::string> terminal_net_names = {}) -> icts::ClockTraceClockTarget
  {
    icts::ClockTraceClockTarget clock_target;
    clock_target.clock_name = "clk";
    clock_target.clock_net_name = "clk";
    clock_target.terminal_net_names = std::move(terminal_net_names);
    return clock_target;
  }

  static auto sortedPinNames(const std::vector<icts::Pin*>& pins) -> std::vector<std::string>
  {
    std::vector<std::string> names;
    names.reserve(pins.size());
    for (const auto* pin : pins) {
      names.push_back(icts::Design::getPinFullName(pin));
    }
    std::ranges::sort(names);
    return names;
  }

  static auto ioPinNames(idb::IdbNet* net) -> std::vector<std::string>
  {
    std::vector<std::string> names;
    if (net == nullptr || net->get_io_pins() == nullptr) {
      return names;
    }
    for (auto* pin : net->get_io_pins()->get_pin_list()) {
      names.push_back(pin->get_pin_name());
    }
    std::ranges::sort(names);
    return names;
  }

  static auto instPinCount(idb::IdbNet* net) -> std::size_t
  {
    return net == nullptr || net->get_instance_pin_list() == nullptr ? 0U : net->get_instance_pin_list()->get_pin_list().size();
  }

  idb::IdbLayout _layout;
  idb::IdbDesign _idb_design;
  icts::Wrapper _wrapper;
  icts::Design _design;
};

TEST_F(ClockIgnoreLoadFixtureInterface, OutputPortOnClockNetIsAnIgnoreLoadNotASink)
{
  buildPassThroughDesign(3U, {{"adc_clk", idb::IdbConnectDirection::kOutput}});

  auto* clock = readClock(target());
  ASSERT_NE(clock, nullptr);
  ASSERT_NE(clock->get_clock_source(), nullptr);
  EXPECT_EQ(clock->get_clock_source()->get_name(), "clk_in");
  EXPECT_TRUE(clock->get_clock_source()->is_io());
  EXPECT_EQ(sortedPinNames(clock->get_loads()), (std::vector<std::string>{"ff0/CLK", "ff1/CLK", "ff2/CLK"}));

  auto* source_net = clock->get_clock_source_net();
  ASSERT_NE(source_net, nullptr);
  EXPECT_EQ(sortedPinNames(source_net->get_loads()), sortedPinNames(clock->get_loads()));
  ASSERT_EQ(source_net->get_ignore_loads().size(), 1U);
  const auto* ignore_load = source_net->get_ignore_loads().front();
  EXPECT_EQ(ignore_load->get_name(), "adc_clk");
  EXPECT_TRUE(ignore_load->is_io());
  EXPECT_EQ(ignore_load->get_inst(), nullptr);
  EXPECT_EQ(ignore_load->get_net(), source_net);
}

TEST_F(ClockIgnoreLoadFixtureInterface, EveryNonDriverTopLevelPinIsAnIgnoreLoadRegardlessOfDirection)
{
  buildPassThroughDesign(
      2U,
      {{"adc_clk", idb::IdbConnectDirection::kOutput}, {"tri_clk", idb::IdbConnectDirection::kOutputTriState}, {"pad_clk", idb::IdbConnectDirection::kInOut}});

  auto* clock = readClock(target());
  ASSERT_NE(clock, nullptr);
  EXPECT_EQ(clock->get_clock_source()->get_name(), "clk_in");
  EXPECT_EQ(clock->get_loads().size(), 2U);
  ASSERT_NE(clock->get_clock_source_net(), nullptr);
  EXPECT_EQ(sortedPinNames(clock->get_clock_source_net()->get_ignore_loads()), (std::vector<std::string>{"adc_clk", "pad_clk", "tri_clk"}));
}

TEST_F(ClockIgnoreLoadFixtureInterface, TracedTargetKeepsThePortOnTheSourceNetWithoutDuplicates)
{
  buildPassThroughDesign(3U, {{"adc_clk", idb::IdbConnectDirection::kOutput}});

  // The source net is also its own terminal net, so both traced attach points run.
  auto* clock = readClock(target({"clk"}));
  ASSERT_NE(clock, nullptr);
  EXPECT_EQ(clock->get_loads().size(), 3U);
  ASSERT_NE(clock->get_clock_source_net(), nullptr);
  EXPECT_EQ(sortedPinNames(clock->get_clock_source_net()->get_ignore_loads()), (std::vector<std::string>{"adc_clk"}));
  EXPECT_TRUE(clock->get_nets().empty());
}

TEST_F(ClockIgnoreLoadFixtureInterface, PortOnlyClockNetHasNoBalanceLoads)
{
  buildPassThroughDesign(0U, {{"adc_clk", idb::IdbConnectDirection::kOutput}});

  auto* clock = readClock(target());
  ASSERT_NE(clock, nullptr);
  EXPECT_TRUE(clock->get_loads().empty());
  ASSERT_NE(clock->get_clock_source_net(), nullptr);
  EXPECT_EQ(sortedPinNames(clock->get_clock_source_net()->get_ignore_loads()), (std::vector<std::string>{"adc_clk"}));
}

TEST_F(ClockIgnoreLoadFixtureInterface, ClonePreservesIgnoreLoadsAndTheirNet)
{
  buildPassThroughDesign(2U, {{"adc_clk", idb::IdbConnectDirection::kOutput}});
  ASSERT_NE(readClock(target()), nullptr);

  const auto cloned = _design.clone();
  ASSERT_NE(cloned, nullptr);
  ASSERT_EQ(cloned->get_clocks().size(), 1U);
  auto* cloned_net = cloned->get_clocks().front()->get_clock_source_net();
  ASSERT_NE(cloned_net, nullptr);
  ASSERT_EQ(cloned_net->get_ignore_loads().size(), 1U);
  const auto* cloned_ignore_load = cloned_net->get_ignore_loads().front();
  EXPECT_EQ(cloned_ignore_load->get_name(), "adc_clk");
  EXPECT_EQ(cloned_ignore_load->get_net(), cloned_net);
  EXPECT_TRUE(cloned_ignore_load->is_io());
  EXPECT_EQ(cloned->get_clocks().front()->get_loads().size(), 2U);
}

TEST_F(ClockIgnoreLoadFixtureInterface, WritebackKeepsThePortConnectedToTheClockNet)
{
  auto* idb_net = buildPassThroughDesign(3U, {{"adc_clk", idb::IdbConnectDirection::kOutput}});
  auto* clock = readClock(target());
  ASSERT_NE(clock, nullptr);

  const auto summary = _wrapper.writeClocksDetailed(_design, {clock});
  EXPECT_TRUE(summary.success) << summary.reason;
  EXPECT_EQ(ioPinNames(idb_net), (std::vector<std::string>{"adc_clk", "clk_in"}));
  EXPECT_EQ(instPinCount(idb_net), 3U);
  auto* port = _idb_design.get_io_pin_list()->find_pin("adc_clk");
  ASSERT_NE(port, nullptr);
  EXPECT_EQ(port->get_net(), idb_net);
}

TEST_F(ClockIgnoreLoadFixtureInterface, WritebackRejectsADroppedPreexistingPinAndRestoresIdb)
{
  auto* idb_net = buildPassThroughDesign(3U, {{"adc_clk", idb::IdbConnectDirection::kOutput}});
  auto* clock = readClock(target());
  ASSERT_NE(clock, nullptr);
  ASSERT_NE(clock->get_clock_source_net(), nullptr);

  // Simulate a model that lost the port: the writer must refuse rather than disconnect it.
  clock->get_clock_source_net()->set_ignore_loads({});
  const auto summary = _wrapper.writeClocksDetailed(_design, {clock});
  EXPECT_FALSE(summary.success);
  EXPECT_EQ(summary.reason, "clock_tree_preexisting_pin_dropped:clk:adc_clk");
  EXPECT_EQ(summary.failed_net, "clk");
  EXPECT_EQ(summary.failed_clock, "clk");
  EXPECT_TRUE(summary.idb_clock_tree_restored);
  EXPECT_EQ(ioPinNames(idb_net), (std::vector<std::string>{"adc_clk", "clk_in"}));
  EXPECT_EQ(instPinCount(idb_net), 3U);
}

}  // namespace
}  // namespace icts_test
