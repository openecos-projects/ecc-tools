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
 * @file NativeStaConstraintsTest.cc
 * @brief Import and snapshot the actual loaded native Network/Liberty/SDC session.
 */
#include <algorithm>
#include <any>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <system_error>
#include <vector>

#include "Config.hh"
#include "ConstraintBudgets.hh"
#include "DataManager.hpp"
#include "Database.hpp"
#include "IdbCellMaster.h"
#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbPins.h"
#include "IdbTerm.h"
#include "IdbUnits.h"
#include "IstaConstraintReader.hh"
#include "STAInterface.hpp"
#include "TimingConstraints.hh"
#include "def_service.h"
#include "dm_config.h"
#include "graph/Graph.hpp"
#include "gtest/gtest.h"
#include "idm.h"
#include "lef_service.h"
#include "liberty/LibertyCell.hpp"
#include "liberty/LibertyLibrary.hpp"
#include "network/Network.hpp"

namespace icts_test {
namespace {

constexpr const char* kLibrary = R"lib(
library(L) {
  time_unit : "1ns";
  capacitive_load_unit(1, pf);
  nom_process : 1;
  nom_voltage : 1;
  nom_temperature : 25;
  cell(BUF) {
    area : 1;
    pin(A) { direction : input; capacitance : 0.01; max_transition : 0.3; }
    pin(Y) { direction : output; function : "A"; max_capacitance : 0.2; }
  }
}
)lib";

class NativeStaConstraintsTestInterface : public testing::Test
{
 protected:
  void SetUp() override
  {
    ASSERT_FALSE(ista::DataManager::isInitialized());
    auto directory = (std::filesystem::temp_directory_path() / "icts-native-constraints-XXXXXX").string();
    ASSERT_NE(mkdtemp(directory.data()), nullptr);
    _directory = directory;
    dmInst->set_idb_lef_service(&_lef);
    dmInst->set_idb_def_service(&_def);
    dmInst->get_config().set_thread_number(1);
    auto* layout = _lef.get_layout();
    layout->get_units()->set_microns_dbu(1000);
    auto* design = _def.get_design();
    design->set_design_name("top");
    auto* master = layout->get_cell_master_list()->set_cell_master("BUF");
    master->set_width(1000U);
    master->set_height(1000U);
    for (const auto* name : {"A", "Y"}) {
      auto* term = master->add_term(name);
      term->set_direction(std::string(name) == "A" ? idb::IdbConnectDirection::kInput : idb::IdbConnectDirection::kOutput);
      term->set_type(idb::IdbConnectType::kSignal);
    }
    auto* inst = design->get_instance_list()->add_instance("u0");
    inst->set_cell_master(master);
    for (const auto* name : {"clk", "out", "mode", "extra"}) {
      auto* port = design->get_io_pin_list()->add_pin_list(name);
      port->set_as_io();
      auto* term = port->set_term();
      term->set_direction(std::string(name) == "out" ? idb::IdbConnectDirection::kOutput : idb::IdbConnectDirection::kInput);
      term->set_type(idb::IdbConnectType::kSignal);
      auto* net = design->createOrFindNet(name, idb::IdbConnectType::kSignal);
      ASSERT_TRUE(design->connectPinToNet(port, net));
      if (std::string(name) == "clk") {
        ASSERT_TRUE(design->connectPinToNet(inst->get_pin_by_term("A"), net));
      } else if (std::string(name) == "out") {
        ASSERT_TRUE(design->connectPinToNet(inst->get_pin_by_term("Y"), net));
      }
    }
    ASSERT_TRUE(loadLibrary(kLibrary));
  }

  void TearDown() override
  {
    if (ista::DataManager::isInitialized()) {
      ista::STAInterface::getInst().destroySTA();
    }
    dmInst->set_idb_lef_service(_saved_lef);
    dmInst->set_idb_def_service(_saved_def);
    dmInst->get_config() = _saved_config;
    static_cast<void>(dmInst->readLib({}));
    std::error_code error;
    std::filesystem::remove_all(_directory, error);
  }

  auto loadLibrary(const std::string& contents) -> bool
  {
    const auto path = (_directory / "test.lib").string();
    std::ofstream(path) << contents;
    dmInst->get_config().set_lib_paths({path});
    return dmInst->readLib({path});
  }

  auto read(const std::string& commands) -> icts::SdcClockData
  {
    const auto path = (_directory / "test.sdc").string();
    std::ofstream(path) << commands;
    dmInst->get_config().get_sdc_path() = path;
    const auto generation = dmInst->get_liberty_generation();
    auto result = icts::IstaConstraintReader::read(_directory.string());
    EXPECT_FALSE(ista::DataManager::isInitialized());
    EXPECT_FALSE(ista::STAInterface::getInst().isInitialized());
    EXPECT_FALSE(ista::STAInterface::getInst().isModelReady());
    EXPECT_FALSE(ista::STAInterface::getInst().isTimingReady());
    EXPECT_EQ(dmInst->get_liberty_generation(), generation);
    return result;
  }

  idb::IdbLefService* _saved_lef = dmInst->get_idb_lef_service();
  idb::IdbDefService* _saved_def = dmInst->get_idb_def_service();
  idm::DataConfig _saved_config = dmInst->get_config();
  idb::IdbLefService _lef;
  idb::IdbDefService _def{_lef.get_layout()};
  std::filesystem::path _directory;
};

TEST_F(NativeStaConstraintsTestInterface, CurrentDesignAndLiteralDesignNameAreNativeEquivalent)
{
  const auto current = read("set_max_fanout 8 [current_design]\nset_max_transition 0.5 [current_design]\nset_max_capacitance 0.4 [current_design]\n");
  const auto literal = read("set_max_fanout 8 top\nset_max_transition 0.5 top\nset_max_capacitance 0.4 top\n");
  ASSERT_TRUE(current.ok());
  ASSERT_TRUE(literal.ok());
  ASSERT_EQ(current.electrical_limits.size(), 3U);
  EXPECT_EQ(current.electrical_limits, literal.electrical_limits);
  icts::Config config;
  config.set_max_fanout(2U);
  config.set_max_buf_tran(0.1);
  config.set_max_sink_tran(0.1);
  config.set_max_cap(0.1);
  const auto budgets = icts::ApplyConstraintBudgets(literal.electrical_limits, config);
  EXPECT_EQ(config.get_max_fanout(), 8U);
  EXPECT_DOUBLE_EQ(config.get_max_buf_tran(), 0.5);
  EXPECT_DOUBLE_EQ(config.get_max_sink_tran(), 0.5);
  EXPECT_DOUBLE_EQ(config.get_max_cap(), 0.4);
  EXPECT_TRUE(std::ranges::all_of(budgets, [](const auto& row) -> bool { return row.from_sdc; }));
}

TEST_F(NativeStaConstraintsTestInterface, TclControlFlowAndSourcedFilesStayNative)
{
  std::ofstream(_directory / "child.sdc") << "set_max_fanout 7 [current_design]\n";
  const auto data = read("set target [current_design]\nif {[get_object_name $target] eq {top}} {source {" + (_directory / "child.sdc").string()
                         + "}}\ncreate_clock -name C -period 10 [get_ports clk]\nset_input_delay -clock C 1 [get_ports mode]\n");
  ASSERT_TRUE(data.ok());
  ASSERT_EQ(data.electrical_limits.size(), 1U);
  EXPECT_DOUBLE_EQ(data.electrical_limits.front().value, 7.0);
  ASSERT_EQ(data.clocks.size(), 1U);
  EXPECT_EQ(data.clocks.front().targets.front().name, "clk");
}

TEST_F(NativeStaConstraintsTestInterface, ScopedLimitsKeepIdentitySelectorsAndForce)
{
  const auto data = read(
      "create_clock -name C -period 10 [get_ports clk]\n"
      "set_max_fanout 3 [get_ports clk]\n"
      "set_max_transition 0.4 [current_design]\n"
      "set_max_transition -force 0.2 [get_ports mode]\n"
      "set_max_capacitance -force 0.1 [get_pins u0/Y]\n"
      "set_max_transition 0.15 [get_lib_pins BUF/A]\n"
      "set_max_transition -clock_path -rise 0.25 [get_clocks C]\n");
  ASSERT_TRUE(data.ok()) << (data.diagnostics.empty() ? "" : data.diagnostics.front());
  EXPECT_EQ(data.electrical_limits.size(), 6U);
  const auto find = [&](icts::SdcElectricalScope scope) -> std::vector<icts::SdcElectricalLimit>::const_iterator {
    return std::ranges::find(data.electrical_limits, scope, &icts::SdcElectricalLimit::scope);
  };
  const auto pin = find(icts::SdcElectricalScope::kPin);
  ASSERT_NE(pin, data.electrical_limits.end());
  EXPECT_EQ(pin->object_name, "u0/Y");
  EXPECT_TRUE(pin->force);
  const auto clock = find(icts::SdcElectricalScope::kClock);
  ASSERT_NE(clock, data.electrical_limits.end());
  EXPECT_EQ(clock->object_name, "C");
  EXPECT_TRUE(clock->clock_path);
  EXPECT_EQ(clock->transition, icts::SdcTransition::kRise);
  const auto lib_pin = find(icts::SdcElectricalScope::kLibPin);
  ASSERT_NE(lib_pin, data.electrical_limits.end());
  EXPECT_EQ(lib_pin->object_name, "L/BUF/A");
  icts::Config config;
  const auto rows = icts::ApplyConstraintBudgets(data.electrical_limits, config);
  EXPECT_EQ(config.get_max_fanout(), 32U);
  EXPECT_DOUBLE_EQ(config.get_sdc_max_transition_ns(), 0.4);
  EXPECT_FALSE(config.has_sdc_max_cap());
  EXPECT_FALSE(data.ignored.empty());
}

TEST_F(NativeStaConstraintsTestInterface, CaseTransitionsAreNotCoercedToConstants)
{
  const auto data = read("set_case_analysis rise [get_ports mode]\nset_case_analysis fall [get_pins u0/A]\nset_case_analysis 0 [get_ports extra]\n");
  ASSERT_TRUE(data.ok());
  ASSERT_EQ(data.case_analyses.size(), 3U);
  const auto find = [&](const std::string& name) -> std::vector<icts::SdcCaseAnalysis>::const_iterator {
    return std::ranges::find_if(data.case_analyses, [&](const auto& entry) -> bool { return entry.objects.front().name == name; });
  };
  ASSERT_NE(find("mode"), data.case_analyses.end());
  ASSERT_NE(find("u0/A"), data.case_analyses.end());
  ASSERT_NE(find("extra"), data.case_analyses.end());
  EXPECT_EQ(find("mode")->value, icts::SdcCaseValue::kRise);
  EXPECT_EQ(find("u0/A")->value, icts::SdcCaseValue::kFall);
  EXPECT_EQ(find("extra")->value, icts::SdcCaseValue::kZero);
  EXPECT_EQ(find("mode")->objects.front().kind, icts::SdcObjectKind::kPort);
  EXPECT_EQ(find("u0/A")->objects.front().kind, icts::SdcObjectKind::kPin);
}

TEST_F(NativeStaConstraintsTestInterface, GeneratedClockSnapshotOwnsNativeWaveformsAndDefinitions)
{
  const auto data = read(
      "create_clock -name C -period 10 -waveform {1 4} [get_ports clk]\n"
      "create_generated_clock -name G -source [get_ports clk] -master_clock C -edges {1 3 5} -edge_shift {0.1 0.2 0.3} [get_pins u0/Y]\n"
      "set_propagated_clock [get_clocks {C G}]\nset_clock_transition -rise -max 0.12 [get_clocks G]\n");
  ASSERT_TRUE(data.ok());
  ASSERT_EQ(data.clocks.size(), 2U);
  const auto generated = std::ranges::find(data.clocks, std::string("G"), &icts::SdcClockDecl::clock_name);
  ASSERT_NE(generated, data.clocks.end());
  EXPECT_EQ(generated->master_clock_name, "C");
  EXPECT_EQ(generated->generated_edges, (std::vector<double>{1, 3, 5}));
  EXPECT_EQ(generated->generated_edge_shifts_ns, (std::vector<double>{0.1, 0.2, 0.3}));
  EXPECT_TRUE(generated->waveform_resolved);
  EXPECT_NEAR(generated->period_ns, 20.2, 1e-12);
  EXPECT_EQ(data.propagated_clocks.size(), 2U);
  ASSERT_EQ(data.clock_transitions.size(), 1U);
  EXPECT_EQ(data.clock_transitions.front().transition, icts::SdcTransition::kRise);
  EXPECT_FALSE(data.clock_transitions.front().min);
  EXPECT_TRUE(data.clock_transitions.front().max);
}

TEST_F(NativeStaConstraintsTestInterface, GeneratedTransformFlagsRemainNativeFacts)
{
  const auto data = read(
      "create_clock -name C -period 10 [get_ports clk]\n"
      "create_generated_clock -name D -source [get_ports clk] -master_clock C -divide_by 2 -preinvert -add [get_pins u0/Y]\n"
      "create_generated_clock -name M -source [get_ports clk] -master_clock C -multiply_by 2 -duty_cycle 25 -invert -add [get_pins u0/Y]\n"
      "create_generated_clock -name B -source [get_ports clk] -master_clock C -combinational -add [get_pins u0/Y]\n");
  ASSERT_TRUE(data.ok());
  const auto find = [&](const std::string& name) -> std::vector<icts::SdcClockDecl>::const_iterator {
    return std::ranges::find(data.clocks, name, &icts::SdcClockDecl::clock_name);
  };
  const auto divide = find("D");
  const auto multiply = find("M");
  const auto combinational = find("B");
  ASSERT_NE(divide, data.clocks.end());
  ASSERT_NE(multiply, data.clocks.end());
  ASSERT_NE(combinational, data.clocks.end());
  EXPECT_TRUE(divide->divide_by_explicit);
  EXPECT_FALSE(divide->multiply_by_explicit);
  EXPECT_TRUE(divide->preinvert);
  EXPECT_DOUBLE_EQ(divide->divide_by, 2.0);
  EXPECT_EQ(divide->targets.front().name, "u0/Y");
  EXPECT_EQ(divide->targets.front().kind, icts::SdcObjectKind::kPin);
  EXPECT_TRUE(multiply->multiply_by_explicit);
  EXPECT_TRUE(multiply->invert);
  EXPECT_TRUE(multiply->add);
  EXPECT_EQ(multiply->duty_cycle_percent, std::optional<double>{25.0});
  EXPECT_TRUE(combinational->combinational);
  EXPECT_TRUE(combinational->waveform_resolved);
}

TEST_F(NativeStaConstraintsTestInterface, NativeLibertyUnitsNormalizeExactlyOnce)
{
  std::string library = kLibrary;
  library.replace(library.find("1ns"), 3U, "1ps");
  library.replace(library.find("(1, pf)"), 7U, "(1, ff)");
  ASSERT_TRUE(loadLibrary(library));
  const auto data = read("create_clock -name C -period 1000 [get_ports clk]\nset_max_transition 200 top\nset_max_capacitance 300 top\n");
  ASSERT_TRUE(data.ok());
  ASSERT_EQ(data.clocks.size(), 1U);
  EXPECT_DOUBLE_EQ(data.clocks.front().period_ns, 1.0);
  icts::Config config;
  static_cast<void>(icts::ApplyConstraintBudgets(data.electrical_limits, config));
  EXPECT_DOUBLE_EQ(config.get_sdc_max_transition_ns(), 0.2);
  EXPECT_DOUBLE_EQ(config.get_sdc_max_cap_pf(), 0.3);
}

TEST_F(NativeStaConstraintsTestInterface, NativeErrorsRejectAllPartialFactsAndRecover)
{
  for (const auto* command : {"unknown_sdc_command", "set_max_fanout -1 top", "set_max_transition 1 missing_design", "set_max_capacitance bad top"}) {
    SCOPED_TRACE(command);
    const auto invalid = read(std::string("create_clock -name C -period 10 [get_ports clk]\n") + command + "\n");
    EXPECT_FALSE(invalid.ok());
    EXPECT_FALSE(invalid.diagnostics.empty());
    EXPECT_TRUE(invalid.clocks.empty());
    EXPECT_TRUE(invalid.electrical_limits.empty());
    EXPECT_TRUE(read("set_max_fanout 8 top\n").ok());
  }
}

TEST_F(NativeStaConstraintsTestInterface, NeverDestroysAnotherOwnersSession)
{
  ista::DataManager::initInst();
  auto& network = ista::DataManager::getInst().getDatabase().network();
  network.setName("external_owner");
  const auto data = icts::IstaConstraintReader::read(_directory.string());
  EXPECT_FALSE(data.ok());
  EXPECT_TRUE(ista::DataManager::isInitialized());
  EXPECT_EQ(network.name(), "external_owner");
  ASSERT_FALSE(data.diagnostics.empty());
  EXPECT_EQ(data.diagnostics.front(), "native_sta_import:ista_session_in_use");
}

TEST_F(NativeStaConstraintsTestInterface, FailedNativeInitializationCanBeCleanedBeforeCalculatorExists)
{
  const auto sdc = (_directory / "valid.sdc").string();
  std::ofstream(sdc) << "set_max_fanout 8 top\n";
  dmInst->get_config().get_sdc_path() = sdc;
  const auto obstruction = (_directory / "obstruction").string();
  std::ofstream(obstruction) << "not a directory";
  const auto failed = icts::IstaConstraintReader::read(obstruction);
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(ista::DataManager::isInitialized());
  EXPECT_FALSE(ista::STAInterface::getInst().isInitialized());
  EXPECT_TRUE(read("set_max_fanout 8 top\n").ok());
}

TEST_F(NativeStaConstraintsTestInterface, NativeNetworkErrorsReturnStatusWithoutExiting)
{
  auto* term = _def.get_design()->get_io_pin_list()->find_pin("clk")->get_term();
  term->set_direction(idb::IdbConnectDirection::kNone);
  const auto failed = read("set_max_fanout 8 top\n");
  EXPECT_FALSE(failed.ok());
  EXPECT_TRUE(failed.clocks.empty());
  term->set_direction(idb::IdbConnectDirection::kInput);
  EXPECT_TRUE(read("set_max_fanout 8 top\n").ok());
}

TEST_F(NativeStaConstraintsTestInterface, ConstraintInitializationLinksButDoesNotBuildTimingGraph)
{
  const auto path = (_directory / "model.sdc").string();
  std::ofstream(path) << "create_clock -name C -period 10 [get_ports clk]\n";
  dmInst->get_config().get_sdc_path() = path;
  auto& sta = ista::STAInterface::getInst();
  std::string diagnostic;
  ASSERT_TRUE(sta.tryInitConstraints({{"-temp_directory_path", _directory.string()}, {"-thread_number", int32_t{1}}}, diagnostic)) << diagnostic;
  auto& database = ista::DataManager::getInst().getDatabase();
  EXPECT_EQ(database.network().name(), "top");
  EXPECT_TRUE(database.liberty().get_cell_map().contains("BUF"));
  EXPECT_TRUE(database.graph().vertices().empty());
  EXPECT_FALSE(sta.isModelReady());
  EXPECT_FALSE(sta.isTimingReady());
  const auto snapshot = icts::IstaConstraintReader::snapshot(database);
  sta.destroySTA();
  ASSERT_EQ(snapshot.clocks.size(), 1U);
  EXPECT_EQ(snapshot.clocks.front().clock_name, "C");
  EXPECT_EQ(snapshot.clocks.front().targets.front().name, "clk");
}

TEST_F(NativeStaConstraintsTestInterface, RejectsAStaleSharedLibertyGenerationWithoutCreatingSession)
{
  const auto path = (_directory / "stale.sdc").string();
  std::ofstream(path) << "set_max_fanout 8 top\n";
  dmInst->get_config().get_sdc_path() = path;
  const auto generation = dmInst->get_liberty_generation();
  dmInst->get_config().set_lib_paths({(_directory / "missing.lib").string()});
  const auto data = icts::IstaConstraintReader::read(_directory.string());
  EXPECT_FALSE(data.ok());
  EXPECT_FALSE(ista::DataManager::isInitialized());
  EXPECT_TRUE(data.electrical_limits.empty());
  EXPECT_EQ(dmInst->get_liberty_generation(), generation);
  ASSERT_FALSE(data.diagnostics.empty());
  EXPECT_EQ(data.diagnostics.front(), "native_sta_import:stale_liberty_generation");
}

TEST_F(NativeStaConstraintsTestInterface, RepeatedDesignConstraintsKeepTheNativeLastValue)
{
  const auto data = read(
      "set_max_fanout 2 top\nset_max_fanout 8 [current_design]\n"
      "set_max_transition 0.1 top\nset_max_transition 0.5 top\n"
      "set_max_capacitance 0.1 top\nset_max_capacitance 0.6 top\n");
  ASSERT_TRUE(data.ok());
  icts::Config config;
  static_cast<void>(icts::ApplyConstraintBudgets(data.electrical_limits, config));
  EXPECT_EQ(config.get_max_fanout(), 8U);
  EXPECT_DOUBLE_EQ(config.get_max_buf_tran(), 0.5);
  EXPECT_DOUBLE_EQ(config.get_max_cap(), 0.6);
}

TEST_F(NativeStaConstraintsTestInterface, MissingFileDoesNotCreateSession)
{
  dmInst->get_config().get_sdc_path() = (_directory / "missing.sdc").string();
  const auto data = icts::IstaConstraintReader::read(_directory.string());
  EXPECT_FALSE(data.ok());
  EXPECT_FALSE(ista::DataManager::isInitialized());
  EXPECT_TRUE(data.clocks.empty());
}

}  // namespace
}  // namespace icts_test
