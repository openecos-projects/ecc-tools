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
 * @file ClockTreeWritebackInstAttributesTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-10-20
 * @brief The clock writeback must not edit placement attributes the input DEF already decided.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "IdbCellMaster.h"
#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbGeometry.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbPins.h"
#include "IdbTerm.h"
#include "data_manager/adapter/sdc/SDCClockReader.hh"
#include "data_manager/design/Clock.hh"
#include "data_manager/design/Design.hh"
#include "data_manager/design/Inst.hh"
#include "data_manager/io/Wrapper.hh"

namespace idb {
class IdbNet;
}

namespace icts_test {
namespace {

using PortSpec = std::pair<std::string, idb::IdbConnectDirection>;

// A clock path that already exists in the input DEF is made of real cells (an I/O pad, a clock
// mux, clock buffers) and the clock tree is only built *around* them. Their orientation,
// placement status, instance type and coordinate are floorplan decisions the writeback has no
// business rewriting, yet it used to place every instance of the clock model with a hard-coded N
// orientation and PLACED status, which silently un-rotated and un-fixed those cells in the
// written DEF.
class ClockTreeWritebackInstAttributesFixtureInterface : public testing::Test
{
 protected:
  ClockTreeWritebackInstAttributesFixtureInterface() : _idb_design(&_layout) {}

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

  auto addInstance(const std::string& name, idb::IdbCellMaster* master, int32_t x, int32_t y) -> idb::IdbInstance*
  {
    auto* inst = _idb_design.get_instance_list()->add_instance(name);
    EXPECT_NE(inst, nullptr);
    if (inst != nullptr) {
      inst->set_cell_master(master);
      inst->set_coodinate(x, y, false);
    }
    return inst;
  }

  // A clock source has to be a top-level port for the trace to resolve a driver. The term is held
  // in a local because every set_term() call hands back a fresh term.
  auto addClockPort(const std::string& name, int32_t x, int32_t y) -> idb::IdbPin*
  {
    auto* pin = _idb_design.createOrFindIoPin(name);
    EXPECT_NE(pin, nullptr);
    if (pin == nullptr) {
      return nullptr;
    }
    auto* term = pin->set_term();
    term->set_name(name);
    term->set_direction(idb::IdbConnectDirection::kInput);
    term->set_type(idb::IdbConnectType::kSignal);
    pin->set_average_coordinate(x, y);
    return pin;
  }

  static auto instPin(idb::IdbInstance* inst, const std::string& name) -> idb::IdbPin* { return inst == nullptr ? nullptr : inst->get_pin_by_term(name); }

  auto connect(const std::string& name, const std::vector<idb::IdbPin*>& pins) -> idb::IdbNet*
  {
    auto* net = _idb_design.createOrFindNet(name, idb::IdbConnectType::kClock);
    EXPECT_NE(net, nullptr);
    for (auto* pin : pins) {
      EXPECT_TRUE(_idb_design.connectPinToNet(pin, net));
    }
    return net;
  }

  // clk_in/clk and clk_alt fall into the clock mux, whose output then clocks two flip-flops. The
  // mux already exists in the DEF, placed by the floorplan: it is exactly the cell the writeback
  // used to relocate and un-mirror.
  auto buildPlacedClockPathDesign() -> void
  {
    auto* mux_master
        = addMaster("MUX2_X1", {{"A1", idb::IdbConnectDirection::kInput}, {"A2", idb::IdbConnectDirection::kInput}, {"Z", idb::IdbConnectDirection::kOutput}});
    auto* sink_master = addMaster("DFF_X1", {{"CLK", idb::IdbConnectDirection::kInput}});

    auto* primary_port = addClockPort("clk_in", 0, 0);
    auto* alternate_port = addClockPort("clk_alt", 0, 100);
    _mux = addInstance("i_mux", mux_master, 400, 60);
    ASSERT_NE(primary_port, nullptr);
    ASSERT_NE(alternate_port, nullptr);
    ASSERT_NE(_mux, nullptr);

    _mux->set_status(idb::IdbPlacementStatus::kFixed);
    _mux->set_orient(idb::IdbOrient::kW_R90);
    _mux->set_type(idb::IdbInstanceType::kNetlist);

    auto* sink_a = addInstance("ff_a", sink_master, 1200, 0);
    auto* sink_b = addInstance("ff_b", sink_master, 1200, 600);
    ASSERT_NE(sink_a, nullptr);
    ASSERT_NE(sink_b, nullptr);
    for (auto* sink : {sink_a, sink_b}) {
      sink->set_as_flip_flop_flag();
      sink->set_status(idb::IdbPlacementStatus::kPlaced);
      sink->set_orient(idb::IdbOrient::kFS_MX);
    }

    connect("clk", {primary_port, instPin(_mux, "A1")});
    connect("clk_alt", {alternate_port, instPin(_mux, "A2")});
    connect("mux_out", {instPin(_mux, "Z"), instPin(sink_a, "CLK"), instPin(sink_b, "CLK")});
  }

  // The clock model only owns a pre-existing cell when the trace proved the transition through
  // it, so the test supplies the same propagation evidence the SDC trace hands over in a real
  // run instead of hand-building the model.
  auto readTraceClock() -> icts::Clock*
  {
    icts::ClockTraceClockTarget clock_target;
    clock_target.clock_name = "clk";
    clock_target.clock_net_name = "clk";
    clock_target.terminal_net_names = {"mux_out"};
    clock_target.propagation_steps = {{.clock_name = "clk",
                                       .inst_name = "i_mux",
                                       .input_pin_name = "A1",
                                       .output_pin_name = "Z",
                                       .input_net_name = "clk",
                                       .output_net_name = "mux_out",
                                       .kind = icts::ClockTracePropagationKind::kClockMux,
                                       .ownership_reason = "sdc_reachable_owned_transition"}};

    _wrapper.set_idb_layout(&_layout);
    _wrapper.set_idb_design(&_idb_design);
    if (!_wrapper.readTraceClockTargets(_design, {clock_target})) {
      return nullptr;
    }
    const auto clocks = _design.get_clocks();
    return clocks.empty() ? nullptr : clocks.front();
  }

  // The regression these tests guard only reproduces when the clock model really owns the
  // pre-existing cell, so that is asserted rather than assumed.
  static auto ownsInst(const icts::Clock& clock, const std::string& inst_name) -> bool
  {
    for (const auto* inst : clock.get_insts()) {
      if (inst != nullptr && inst->get_name() == inst_name) {
        return true;
      }
    }
    return false;
  }

  idb::IdbLayout _layout;
  idb::IdbDesign _idb_design;
  icts::Wrapper _wrapper;
  icts::Design _design;
  idb::IdbInstance* _mux = nullptr;
};

TEST_F(ClockTreeWritebackInstAttributesFixtureInterface, WritebackKeepsAFixedClockPathInstAttribute)
{
  buildPlacedClockPathDesign();
  auto* clock = readTraceClock();
  ASSERT_NE(clock, nullptr);
  ASSERT_TRUE(ownsInst(*clock, "i_mux")) << "the clock mux must be part of the clock model for this test to guard anything";

  const auto summary = _wrapper.writeClocksDetailed(_design, {clock});
  ASSERT_TRUE(summary.success) << summary.reason;

  auto* mux = _idb_design.get_instance_list()->find_instance("i_mux");
  ASSERT_NE(mux, nullptr);
  EXPECT_EQ(mux->get_status(), idb::IdbPlacementStatus::kFixed) << "a fixed cell must stay fixed";
  EXPECT_EQ(mux->get_orient(), idb::IdbOrient::kW_R90) << "the writeback must not rotate a placed cell";
  EXPECT_EQ(mux->get_type(), idb::IdbInstanceType::kNetlist) << "the writeback must not relabel an input cell";
  ASSERT_NE(mux->get_coordinate(), nullptr);
  EXPECT_EQ(mux->get_coordinate()->get_x(), 400) << "a fixed cell keeps the coordinate the floorplan gave it";
  EXPECT_EQ(mux->get_coordinate()->get_y(), 60);
}

TEST_F(ClockTreeWritebackInstAttributesFixtureInterface, WritebackKeepsAPlacedClockPathInstAttribute)
{
  buildPlacedClockPathDesign();
  ASSERT_NE(_mux, nullptr);
  _mux->set_status(idb::IdbPlacementStatus::kPlaced);
  _mux->set_orient(idb::IdbOrient::kFS_MX);

  auto* clock = readTraceClock();
  ASSERT_NE(clock, nullptr);
  ASSERT_TRUE(ownsInst(*clock, "i_mux"));

  const auto summary = _wrapper.writeClocksDetailed(_design, {clock});
  ASSERT_TRUE(summary.success) << summary.reason;

  auto* mux = _idb_design.get_instance_list()->find_instance("i_mux");
  ASSERT_NE(mux, nullptr);
  EXPECT_EQ(mux->get_status(), idb::IdbPlacementStatus::kPlaced);
  EXPECT_EQ(mux->get_orient(), idb::IdbOrient::kFS_MX) << "a mirrored clock-path cell must stay mirrored";
  EXPECT_EQ(mux->get_type(), idb::IdbInstanceType::kNetlist);
  ASSERT_NE(mux->get_coordinate(), nullptr);
  EXPECT_EQ(mux->get_coordinate()->get_x(), 400) << "a cell the clock path already owns keeps its placement";
  EXPECT_EQ(mux->get_coordinate()->get_y(), 60);
}

TEST_F(ClockTreeWritebackInstAttributesFixtureInterface, WritebackPlacesAPreexistingUnplacedClockPathInst)
{
  buildPlacedClockPathDesign();
  ASSERT_NE(_mux, nullptr);
  _mux->set_status(idb::IdbPlacementStatus::kUnplaced);
  _mux->set_orient(idb::IdbOrient::kNone);

  auto* clock = readTraceClock();
  ASSERT_NE(clock, nullptr);
  ASSERT_TRUE(ownsInst(*clock, "i_mux"));

  const auto summary = _wrapper.writeClocksDetailed(_design, {clock});
  ASSERT_TRUE(summary.success) << summary.reason;

  auto* mux = _idb_design.get_instance_list()->find_instance("i_mux");
  ASSERT_NE(mux, nullptr);
  // A cell the input left unplaced still has to carry the clock, so the writeback resolves its
  // placement rather than leaving a dangling object in the written DEF.
  EXPECT_EQ(mux->get_status(), idb::IdbPlacementStatus::kPlaced);
  EXPECT_EQ(mux->get_orient(), idb::IdbOrient::kN_R0) << "an unoriented cell gets the single legal fallback";
}

}  // namespace
}  // namespace icts_test
