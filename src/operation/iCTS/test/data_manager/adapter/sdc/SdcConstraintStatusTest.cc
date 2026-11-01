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
 * @file SdcConstraintStatusTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-10-11
 * @brief Boundary tests for the SDC subset reader.
 *
 * An SDC command iCTS does not recognize must not leave a discarded issue's
 * status behind: the clock model iCTS consumes never reads that command or its
 * arguments, so neither may turn the whole constraint file into a rejection.
 * Real inputs such as `pulp/croc` reach this through `source` plus the shell
 * variables it defines, referenced later by commands iCTS does not read.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "SDCClockReader.hh"
#include "TimingConstraints.hh"

namespace icts_test {
namespace {

auto WriteTempSdc(const std::string& name, const std::string& text) -> std::string
{
  const auto path = std::string("/tmp/icts_sdc_status_") + name + ".sdc";
  std::ofstream stream(path);
  stream << text;
  stream.close();
  return path;
}

class SdcConstraintStatusFixtureInterface : public ::testing::Test
{
 protected:
  static auto readSdc(const std::string& name, const std::string& text) -> icts::SdcClockData
  {
    const auto path = WriteTempSdc(name, text);
    auto data = icts::SdcClockReader(path).readClockData();
    std::remove(path.c_str());
    return data;
  }
};

TEST_F(SdcConstraintStatusFixtureInterface, UnrecognizedCommandWithUnresolvedArgumentKeepsStatusOk)
{
  // `source` is outside iCTS's remit, and so is the variable it defines here. Both
  // must collapse to one non-fatal note instead of rejecting the clock model.
  const auto data = readSdc("unsupported_unresolved",
                            "source src/instances.tcl\n"
                            "set_max_delay 3.0 -from $JTAG_ASYNC_REQ_START -to $JTAG_ASYNC_REQ_END\n");
  EXPECT_TRUE(data.ok());
  ASSERT_FALSE(data.ignored.empty());
  EXPECT_TRUE(data.clocks.empty());
}

TEST_F(SdcConstraintStatusFixtureInterface, UnrecognizedCommandAloneKeepsStatusOk)
{
  const auto data = readSdc("unsupported_alone", "set_max_fanout 16 [current_design]\n");
  EXPECT_TRUE(data.ok());
  ASSERT_FALSE(data.ignored.empty());
}

TEST_F(SdcConstraintStatusFixtureInterface, RecognizedCommandWithUnresolvedArgumentStaysFatal)
{
  // The dispatcher is the boundary: what it recognizes, it owns, and its failure
  // is fatal. Rewinding must not soften this.
  const auto data = readSdc("recognized_unresolved", "set_propagated_clock $UNDEFINED_CLOCK\n");
  EXPECT_FALSE(data.ok());
  ASSERT_FALSE(data.issues.empty());
}

TEST_F(SdcConstraintStatusFixtureInterface, DefaultEccUnitsOutsideClockScopeStayIgnored)
{
  const auto data = readSdc("ecc_default_units",
                            "set_units -time ns -resistance kOhm -capacitance pF -voltage V -current mA\n"
                            "create_clock -name i_clk -period 20 [get_ports i_clk]\n"
                            "set_max_transition 3.4 -clock_path [get_clocks i_clk]\n"
                            "set_timing_derate -late 1.10 -clock -delay_corner [all_delay_corners]\n");
  EXPECT_TRUE(data.ok()) << (data.diagnostics.empty() ? "" : data.diagnostics.front());
  ASSERT_EQ(data.clocks.size(), 1U);
  EXPECT_EQ(data.clocks.front().clock_name, "i_clk");
  EXPECT_DOUBLE_EQ(data.clocks.front().period_ns, 20.0);
  EXPECT_FALSE(data.ignored.empty());
}

TEST_F(SdcConstraintStatusFixtureInterface, RecognizedCommandsKeepStatusOk)
{
  const auto data = readSdc("recognized_ok", "create_clock -name clk_sys -period 5 [get_ports clk_i]\n");
  EXPECT_TRUE(data.ok());
  ASSERT_EQ(data.clocks.size(), 1U);
  EXPECT_EQ(data.clocks.front().clock_name, "clk_sys");
}

}  // namespace
}  // namespace icts_test
