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
 * @file CTSAPILifecycleTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-08-10
 * @brief Public CTS destruction lifecycle and session-ownership tests.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "CTSAPI.hh"
#include "IdbDesign.h"
#include "IdbLayer.h"
#include "Logger.hh"
#include "Utility.hh"
#include "data_manager/DataManager.hh"
#include "data_manager/io/Wrapper.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "evaluation/Evaluation.hh"
#include "evaluation/qor/ClockQORMetricCollector.hh"
#include "idm.h"
#include "instantiation/Instantiation.hh"
#include "module/synthesis/Synthesis.hh"
#include "module/synthesis/realization/ClockTreeRealization.hh"
#include "optimization/Optimization.hh"

namespace icts_test {
namespace {

auto MakeUniqueOutputDir(const std::string& label) -> std::filesystem::path
{
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() / ("icts_lifecycle_" + label + "_" + std::to_string(suffix));
}

void RemoveOutputDir(const std::filesystem::path& output_dir)
{
  std::error_code error_code;
  std::filesystem::remove_all(output_dir, error_code);
}

auto WriteTextFile(const std::filesystem::path& path, const std::string& content) -> bool
{
  std::ofstream stream(path);
  stream << content;
  return stream.good();
}

class ScopedMinimalExternalIdb
{
 public:
  explicit ScopedMinimalExternalIdb(const std::string& label) : _output_dir(MakeUniqueOutputDir(label))
  {
    std::filesystem::create_directories(_output_dir);
    _config_path = _output_dir / "cts_config.json";
    const auto tech_lef_path = _output_dir / "minimal.lef";
    const auto def_path = _output_dir / "minimal.def";
    _work_dir = _output_dir / "cts_work";

    constexpr auto tech_lef = R"lef(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.10 ;
  WIDTH 0.05 ;
  RESISTANCE RPERSQ 0.10 ;
  CAPACITANCE CPERSQDIST 0.001 ;
END M1
END LIBRARY
)lef";
    constexpr auto design_def = R"def(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN icts_lifecycle ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 1000 1000 ) ;
COMPONENTS 0 ;
END COMPONENTS
PINS 0 ;
END PINS
NETS 0 ;
END NETS
END DESIGN
)def";

    dmInst->reset();
    _ready = WriteTextFile(_config_path, "{}") && WriteTextFile(tech_lef_path, tech_lef) && WriteTextFile(def_path, design_def)
             && dmInst->readLef(std::vector<std::string>{tech_lef_path.string()}, true) && dmInst->readDef(def_path.string());
    if (_ready) {
      dmInst->get_config().set_sdc_path("");
    }
  }

  ~ScopedMinimalExternalIdb()
  {
    (void) icts::CTSAPI::destroyCTS();
    dmInst->reset();
    RemoveOutputDir(_output_dir);
  }

  ScopedMinimalExternalIdb(const ScopedMinimalExternalIdb& rhs) = delete;
  ScopedMinimalExternalIdb(ScopedMinimalExternalIdb&& rhs) = delete;
  auto operator=(const ScopedMinimalExternalIdb& rhs) -> ScopedMinimalExternalIdb& = delete;
  auto operator=(ScopedMinimalExternalIdb&& rhs) -> ScopedMinimalExternalIdb& = delete;

  auto ready() const -> bool { return _ready; }
  auto configPath() const -> const std::filesystem::path& { return _config_path; }
  auto workDir() const -> const std::filesystem::path& { return _work_dir; }

 private:
  std::filesystem::path _output_dir;
  std::filesystem::path _config_path;
  std::filesystem::path _work_dir;
  bool _ready = false;
};

class ScopedNativeClockExternalIdb
{
 public:
  explicit ScopedNativeClockExternalIdb(const std::string& label) : _output_dir(MakeUniqueOutputDir(label))
  {
    std::filesystem::create_directories(_output_dir);
    _config_path = _output_dir / "cts_config.json";
    const auto tech_lef_path = _output_dir / "native_tech.lef";
    const auto cell_lef_path = _output_dir / "native_cells.lef";
    const auto liberty_path = _output_dir / "native.lib";
    const auto def_path = _output_dir / "native.def";
    const auto sdc_path = _output_dir / "native.sdc";
    _work_dir = _output_dir / "cts_work";

    constexpr auto config = R"json({
  "root_input_slew": 0.02,
  "max_sink_tran": 1.0,
  "max_fanout": 8,
  "routing_layer": [1],
  "buffer_type": ["BUF_X1"],
  "enable_sink_clustering": false
})json";
    constexpr auto tech_lef = R"lef(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.10 ;
  WIDTH 0.05 ;
  RESISTANCE RPERSQ 0.10 ;
  CAPACITANCE CPERSQDIST 0.001 ;
END M1
END LIBRARY
)lef";
    constexpr auto cell_lef = R"lef(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
MACRO BUF_X1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 1 BY 1 ;
  SYMMETRY X Y ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER M1 ;
      RECT 0.05 0.45 0.15 0.55 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER M1 ;
      RECT 0.85 0.45 0.95 0.55 ;
    END
  END Y
END BUF_X1
MACRO INV_X1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 1 BY 1 ;
  SYMMETRY X Y ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER M1 ;
      RECT 0.05 0.45 0.15 0.55 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER M1 ;
      RECT 0.85 0.45 0.95 0.55 ;
    END
  END Y
END INV_X1
MACRO DFF_X1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 1 BY 1 ;
  SYMMETRY X Y ;
  PIN CLK
    DIRECTION INPUT ;
    USE CLOCK ;
    PORT
      LAYER M1 ;
      RECT 0.05 0.45 0.15 0.55 ;
    END
  END CLK
END DFF_X1
END LIBRARY
)lef";
    constexpr auto liberty = R"lib(library (native) {
  time_unit : "1ns";
  capacitive_load_unit (1, pf);
  voltage_unit : "1V";
  current_unit : "1mA";
  pulling_resistance_unit : "1kohm";
  leakage_power_unit : "1nW";
  nom_voltage : 1.0;
  lu_table_template (delay_template) {
    variable_1 : input_net_transition;
    variable_2 : total_output_net_capacitance;
    index_1 ("0.01, 1.0");
    index_2 ("0.01, 1.0");
  }
  power_lut_template (power_template) {
    variable_1 : input_transition_time;
    variable_2 : total_output_net_capacitance;
    index_1 ("0.01, 1.0");
    index_2 ("0.01, 1.0");
  }
  cell (BUF_X1) {
    area : 1.0;
    cell_leakage_power : 0.001;
    pin (A) {
      direction : input;
      capacitance : 0.01;
      max_transition : 1.0;
    }
    pin (Y) {
      direction : output;
      function : "A";
      max_capacitance : 1.0;
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        timing_type : combinational;
        cell_rise (delay_template) {
          index_1 ("0.01, 1.0");
          index_2 ("0.01, 1.0");
          values ("0.01, 0.02", "0.02, 0.03");
        }
        cell_fall (delay_template) {
          index_1 ("0.01, 1.0");
          index_2 ("0.01, 1.0");
          values ("0.01, 0.02", "0.02, 0.03");
        }
        rise_transition (delay_template) {
          index_1 ("0.01, 1.0");
          index_2 ("0.01, 1.0");
          values ("0.02, 0.03", "0.03, 0.04");
        }
        fall_transition (delay_template) {
          index_1 ("0.01, 1.0");
          index_2 ("0.01, 1.0");
          values ("0.02, 0.03", "0.03, 0.04");
        }
      }
      internal_power () {
        related_pin : "A";
        rise_power (power_template) {
          index_1 ("0.01, 1.0");
          index_2 ("0.01, 1.0");
          values ("0.001, 0.002", "0.002, 0.003");
        }
        fall_power (power_template) {
          index_1 ("0.01, 1.0");
          index_2 ("0.01, 1.0");
          values ("0.001, 0.002", "0.002, 0.003");
        }
      }
    }
  }
  cell (INV_X1) {
    area : 1.0;
    pin (A) {
      direction : input;
      capacitance : 0.01;
    }
    pin (Y) {
      direction : output;
      function : "!A";
      max_capacitance : 1.0;
    }
  }
  cell (DFF_X1) {
    area : 1.0;
    pin (CLK) {
      direction : input;
      clock : true;
      capacitance : 0.01;
    }
  }
})lib";
    constexpr auto design_def = R"def(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN icts_native_clock ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 2000 2000 ) ;
COMPONENTS 3 ;
- sink DFF_X1 + PLACED ( 500 500 ) N ;
- boundary_buf BUF_X1 + PLACED ( 500 500 ) N ;
- boundary_inv INV_X1 + PLACED ( 500 500 ) N ;
END COMPONENTS
PINS 1 ;
- clk_in + NET clk_net + DIRECTION INPUT + USE CLOCK
  + LAYER M1 ( 0 0 ) ( 10 10 )
  + PLACED ( 500 500 ) N ;
END PINS
NETS 1 ;
- clk_net ( PIN clk_in ) ( sink CLK ) ( boundary_buf A ) ( boundary_inv A ) + USE CLOCK ;
END NETS
END DESIGN
)def";
    constexpr auto partial_trace_design_def = R"def(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN icts_native_partial_trace ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 2000 2000 ) ;
COMPONENTS 15 ;
- sink DFF_X1 + PLACED ( 500 500 ) N ;
- traced_sink_1 DFF_X1 + PLACED ( 500 500 ) N ;
- traced_sink_2 DFF_X1 + PLACED ( 500 500 ) N ;
- traced_sink_3 DFF_X1 + PLACED ( 500 500 ) N ;
- traced_sink_4 DFF_X1 + PLACED ( 500 500 ) N ;
- traced_sink_5 DFF_X1 + PLACED ( 500 500 ) N ;
- traced_sink_6 DFF_X1 + PLACED ( 500 500 ) N ;
- traced_sink_7 DFF_X1 + PLACED ( 500 500 ) N ;
- traced_sink_8 DFF_X1 + PLACED ( 500 500 ) N ;
- traced_sink_9 DFF_X1 + PLACED ( 500 500 ) N ;
- traced_sink_10 DFF_X1 + PLACED ( 500 500 ) N ;
- traced_sink_11 DFF_X1 + PLACED ( 500 500 ) N ;
- traced_sink_12 DFF_X1 + PLACED ( 500 500 ) N ;
- boundary_buf BUF_X1 + PLACED ( 500 500 ) N ;
- boundary_inv INV_X1 + PLACED ( 500 500 ) N ;
END COMPONENTS
PINS 1 ;
- clk_in + NET clk_net + DIRECTION INPUT + USE CLOCK
  + LAYER M1 ( 0 0 ) ( 10 10 )
  + PLACED ( 500 500 ) N ;
END PINS
NETS 2 ;
- clk_net ( PIN clk_in ) ( boundary_buf A ) ( boundary_inv A ) + USE CLOCK ;
- traced_leaf ( boundary_buf Y )
  ( sink CLK )
  ( traced_sink_1 CLK )
  ( traced_sink_2 CLK )
  ( traced_sink_3 CLK )
  ( traced_sink_4 CLK )
  ( traced_sink_5 CLK )
  ( traced_sink_6 CLK )
  ( traced_sink_7 CLK )
  ( traced_sink_8 CLK )
  ( traced_sink_9 CLK )
  ( traced_sink_10 CLK )
  ( traced_sink_11 CLK )
  ( traced_sink_12 CLK ) + USE CLOCK ;
END NETS
END DESIGN
)def";
    constexpr auto sdc = "create_clock -name native_clk -period 10 [get_nets clk_net]\n";
    const bool partial_trace_fixture = label == "partial_traced_frontier";

    dmInst->reset();
    _ready = WriteTextFile(_config_path, config) && WriteTextFile(tech_lef_path, tech_lef) && WriteTextFile(cell_lef_path, cell_lef)
             && WriteTextFile(liberty_path, liberty) && WriteTextFile(def_path, partial_trace_fixture ? partial_trace_design_def : design_def)
             && WriteTextFile(sdc_path, sdc) && dmInst->readLef(std::vector<std::string>{tech_lef_path.string()}, true)
             && dmInst->readLef(std::vector<std::string>{cell_lef_path.string()}, false) && dmInst->readDef(def_path.string());
    if (!_ready) {
      return;
    }
    auto* idb_layout = dmInst->get_idb_layout();
    auto* routing_layer = idb_layout == nullptr ? nullptr : dynamic_cast<idb::IdbLayerRouting*>(idb_layout->get_layers()->find_layer("M1"));
    if (routing_layer == nullptr) {
      _ready = false;
      return;
    }
    routing_layer->set_edge_capacitance(0.00004);

    auto* idb_design = dmInst->get_idb_design();
    std::vector<std::string> flip_flop_names{"sink"};
    if (partial_trace_fixture) {
      for (std::size_t sink_index = 1U; sink_index < 13U; ++sink_index) {
        flip_flop_names.push_back("traced_sink_" + std::to_string(sink_index));
      }
    }
    for (const auto& flip_flop_name : flip_flop_names) {
      auto* sink = idb_design == nullptr ? nullptr : idb_design->get_instance_list()->find_instance(flip_flop_name);
      if (sink == nullptr) {
        _ready = false;
        return;
      }
      sink->set_as_flip_flop_flag();
    }
    dmInst->get_config().set_lib_paths({liberty_path.string()});
    dmInst->get_config().set_sdc_path(sdc_path.string());
  }

  ~ScopedNativeClockExternalIdb()
  {
    (void) icts::CTSAPI::destroyCTS();
    dmInst->reset();
    RemoveOutputDir(_output_dir);
  }

  ScopedNativeClockExternalIdb(const ScopedNativeClockExternalIdb& rhs) = delete;
  ScopedNativeClockExternalIdb(ScopedNativeClockExternalIdb&& rhs) = delete;
  auto operator=(const ScopedNativeClockExternalIdb& rhs) -> ScopedNativeClockExternalIdb& = delete;
  auto operator=(ScopedNativeClockExternalIdb&& rhs) -> ScopedNativeClockExternalIdb& = delete;

  auto ready() const -> bool { return _ready; }
  auto configPath() const -> const std::filesystem::path& { return _config_path; }
  auto workDir() const -> const std::filesystem::path& { return _work_dir; }
  auto reportDir() const -> std::filesystem::path { return _output_dir / "report"; }

 private:
  std::filesystem::path _output_dir;
  std::filesystem::path _config_path;
  std::filesystem::path _work_dir;
  bool _ready = false;
};

auto Median(std::array<double, 4> samples) -> double
{
  std::ranges::sort(samples);
  return (samples.at(1U) + samples.at(2U)) / 2.0;
}

struct ExternalDesignFingerprint
{
  std::vector<std::string> insts;
  std::vector<std::string> nets;
  std::vector<std::string> pins;

  auto operator==(const ExternalDesignFingerprint& rhs) const -> bool = default;
};

auto Fingerprint(idb::IdbDesign& design) -> ExternalDesignFingerprint
{
  ExternalDesignFingerprint fingerprint;
  for (auto* inst : design.get_instance_list()->get_instance_list()) {
    if (inst == nullptr) {
      continue;
    }
    auto* coordinate = inst->get_coordinate();
    fingerprint.insts.push_back(inst->get_name() + "@" + std::to_string(coordinate == nullptr ? 0 : coordinate->get_x()) + ","
                                + std::to_string(coordinate == nullptr ? 0 : coordinate->get_y()));
    for (auto* pin : inst->get_pin_list()->get_pin_list()) {
      if (pin == nullptr) {
        continue;
      }
      auto* location = pin->get_location();
      fingerprint.pins.push_back(inst->get_name() + "/" + pin->get_pin_name() + "->" + pin->get_net_name() + "@"
                                 + std::to_string(location == nullptr ? 0 : location->get_x()) + ","
                                 + std::to_string(location == nullptr ? 0 : location->get_y()));
    }
  }
  for (auto* net : design.get_net_list()->get_net_list()) {
    if (net == nullptr) {
      continue;
    }
    std::vector<std::string> connections;
    for (auto* pin : net->get_instance_pin_list()->get_pin_list()) {
      if (pin != nullptr && pin->get_instance() != nullptr) {
        connections.push_back(pin->get_instance()->get_name() + "/" + pin->get_pin_name());
      }
    }
    std::ranges::sort(connections);
    std::string record = net->get_net_name();
    for (const auto& connection : connections) {
      record.append("|").append(connection);
    }
    fingerprint.nets.push_back(std::move(record));
  }
  std::ranges::sort(fingerprint.insts);
  std::ranges::sort(fingerprint.nets);
  std::ranges::sort(fingerprint.pins);
  return fingerprint;
}

void PopulateRepresentativeSession(std::size_t inst_count)
{
  for (std::size_t index = 0U; index < inst_count; ++index) {
    ASSERT_NE(CTSDM.getDesign().makeInst("session_inst_" + std::to_string(index)), nullptr);
  }
}

TEST(CTSAPILifecycleTest, DestroyIsSuccessfulBeforeInitializationAndWhenRepeated)
{
  const auto first = icts::CTSAPI::destroyCTS();
  const auto second = icts::CTSAPI::destroyCTS();

  EXPECT_EQ(first.code, icts::CTSStatusCode::kOk);
  EXPECT_EQ(second.code, icts::CTSStatusCode::kOk);
  EXPECT_TRUE(icts::CTSAPI::lastStatus().ok());
  EXPECT_TRUE(icts::CTSAPI::outputClockTiming().empty());
}

TEST(CTSAPILifecycleTest, FailedInitializationUsesCanonicalCleanupAndLeavesAPIUninitialized)
{
  const auto output_dir = MakeUniqueOutputDir("failed_init");
  const auto status = icts::CTSAPI::init((output_dir / "missing_config.json").string(), output_dir.string());

  EXPECT_EQ(status.code, icts::CTSStatusCode::kConfigError);
  EXPECT_EQ(icts::CTSAPI::runCTS().code, icts::CTSStatusCode::kNotInitialized);
  EXPECT_EQ(icts::CTSAPI::report(output_dir.string()).code, icts::CTSStatusCode::kNotInitialized);
  EXPECT_TRUE(icts::CTSAPI::outputClockTiming().empty());
  EXPECT_TRUE(icts::CTSAPI::destroyCTS().ok());
  EXPECT_TRUE(icts::CTSAPI::destroyCTS().ok());

  RemoveOutputDir(output_dir);
}

TEST(CTSAPILifecycleTest, DestroyReleasesRepresentativeSessionAndFreshOwnerStartsEmpty)
{
  icts::DataManager::initInst();
  PopulateRepresentativeSession(256U);
  ASSERT_NE(CTSDM.getDesign().findInst("session_inst_255"), nullptr);

  EXPECT_TRUE(icts::CTSAPI::destroyCTS().ok());
  EXPECT_TRUE(icts::CTSAPI::outputClockTiming().empty());

  icts::DataManager::initInst();
  EXPECT_EQ(CTSDM.getState(), icts::CTSRunState::kEmpty);
  EXPECT_TRUE(CTSDM.getDesign().get_insts().empty());
  EXPECT_EQ(CTSDM.getDesign().findInst("session_inst_255"), nullptr);
}

TEST(CTSAPILifecycleTest, PublicInitializedSessionCanDestroyAndReinitialize)
{
  const ScopedMinimalExternalIdb external_idb("public_reinit");
  ASSERT_TRUE(external_idb.ready());

  const auto first_init = icts::CTSAPI::init(external_idb.configPath().string(), external_idb.workDir().string());
  ASSERT_EQ(first_init.code, icts::CTSStatusCode::kOk) << first_init.message;
  EXPECT_TRUE(icts::CTSAPI::outputClockTiming().empty());
  EXPECT_EQ(icts::CTSAPI::runCTS().code, icts::CTSStatusCode::kNoOp);

  EXPECT_TRUE(icts::CTSAPI::destroyCTS().ok());
  EXPECT_EQ(icts::CTSAPI::runCTS().code, icts::CTSStatusCode::kNotInitialized);
  EXPECT_EQ(icts::CTSAPI::report(external_idb.workDir().string()).code, icts::CTSStatusCode::kNotInitialized);

  const auto second_init = icts::CTSAPI::init(external_idb.configPath().string(), external_idb.workDir().string());
  ASSERT_EQ(second_init.code, icts::CTSStatusCode::kOk) << second_init.message;
  EXPECT_EQ(icts::CTSAPI::runCTS().code, icts::CTSStatusCode::kNoOp);
  EXPECT_TRUE(icts::CTSAPI::destroyCTS().ok());
  EXPECT_TRUE(icts::CTSAPI::destroyCTS().ok());
}

TEST(CTSAPILifecycleTest, InvalidGraphCommitPreservesCanonicalStateAndStoredSummary)
{
  const ScopedMinimalExternalIdb external_idb("invalid_graph_commit");
  ASSERT_TRUE(external_idb.ready());
  const auto init_status = icts::CTSAPI::init(external_idb.configPath().string(), external_idb.workDir().string());
  ASSERT_EQ(init_status.code, icts::CTSStatusCode::kOk) << init_status.message;
  ASSERT_EQ(CTSDM.getState(), icts::CTSRunState::kInputReady);

  const auto stored_summary_before = CTSDM.getSynthesisSummary();
  auto candidate = CTSDM.cloneDesign();
  ASSERT_NE(candidate, nullptr);
  ASSERT_NE(candidate->makeClock("invalid", "missing_source_net"), nullptr);
  icts::SynthesisTraceSummary candidate_summary;
  candidate_summary.success = true;
  candidate_summary.outcome = icts::SynthesisOutcome::kFinished;
  candidate_summary.total_clocks = 1U;
  candidate_summary.successful_clocks = 1U;
  icts::DataManagerStatus commit_status;

  const auto returned_summary = icts::CommitSynthesisCandidate(CTSDM, std::move(candidate), icts::ClockLayout{}, std::move(candidate_summary), commit_status);

  EXPECT_EQ(commit_status.code, icts::DataManagerStatusCode::kCommitError);
  ASSERT_FALSE(commit_status.graph_issues.empty());
  EXPECT_EQ(commit_status.graph_issues.front().code, icts::ClockGraphIssueCode::kMissingClockSource);
  EXPECT_FALSE(returned_summary.success);
  EXPECT_EQ(returned_summary.outcome, icts::SynthesisOutcome::kFailed);
  EXPECT_EQ(returned_summary.commit_status, "rejected");
  EXPECT_EQ(CTSDM.getState(), icts::CTSRunState::kInputReady);
  EXPECT_EQ(CTSDM.getDesign().findClock("invalid", "missing_source_net"), nullptr);
  const auto& stored_summary_after = CTSDM.getSynthesisSummary();
  EXPECT_EQ(stored_summary_after.commit_status, stored_summary_before.commit_status);
  EXPECT_EQ(stored_summary_after.outcome, stored_summary_before.outcome);
  EXPECT_EQ(stored_summary_after.total_clocks, stored_summary_before.total_clocks);
  EXPECT_EQ(stored_summary_after.inserted_inst_count, stored_summary_before.inserted_inst_count);
  EXPECT_EQ(stored_summary_after.inserted_net_count, stored_summary_before.inserted_net_count);
}

TEST(CTSAPILifecycleTest, NativeNonNoOpFlowConvergesPlannedCommittedWrittenAndEvaluatedCounts)
{
  const ScopedNativeClockExternalIdb external_idb("native_non_no_op");
  ASSERT_TRUE(external_idb.ready());
  const auto init_status = icts::CTSAPI::init(external_idb.configPath().string(), external_idb.workDir().string());
  ASSERT_EQ(init_status.code, icts::CTSStatusCode::kOk) << init_status.message;
  ASSERT_EQ(CTSDM.getState(), icts::CTSRunState::kInputReady);
  ASSERT_EQ(CTSDM.getDesign().get_clocks().size(), 1U);

  auto* input_clock = CTSDM.getDesign().get_clocks().front();
  ASSERT_NE(input_clock, nullptr);
  EXPECT_EQ(input_clock->get_clock_name(), "native_clk");
  ASSERT_EQ(input_clock->get_loads().size(), 3U);
  EXPECT_TRUE(input_clock->get_propagation_arcs().empty());
  auto* boundary_buf = CTSDM.getDesign().findInst("boundary_buf");
  auto* boundary_inv = CTSDM.getDesign().findInst("boundary_inv");
  ASSERT_NE(boundary_buf, nullptr);
  ASSERT_NE(boundary_inv, nullptr);
  EXPECT_TRUE(boundary_buf->is_buffer());
  EXPECT_TRUE(boundary_inv->is_inverter());
  EXPECT_EQ(input_clock->findPropagationArc(boundary_buf), nullptr);
  EXPECT_EQ(input_clock->findPropagationArc(boundary_inv), nullptr);
  ASSERT_NE(input_clock->get_clock_source(), nullptr);
  input_clock->get_clock_source()->set_location(input_clock->get_loads().front()->get_location());
  const std::size_t input_inst_count = CTSDM.getDesign().get_insts().size();
  const std::size_t input_net_count = CTSDM.getDesign().get_nets().size();

  const auto synthesis = icts::Synthesis::run();
  ASSERT_TRUE(synthesis.success) << synthesis.failure_reason;
  EXPECT_EQ(synthesis.outcome, icts::SynthesisOutcome::kFinished);
  EXPECT_EQ(synthesis.commit_status, "committed");
  ASSERT_EQ(CTSDM.getState(), icts::CTSRunState::kSynthesisCommitted);
  EXPECT_GT(synthesis.inserted_inst_count, 0U);
  EXPECT_GT(synthesis.inserted_net_count, 0U);
  EXPECT_EQ(CTSDM.getDesign().get_insts().size(), input_inst_count + synthesis.inserted_inst_count);
  EXPECT_EQ(CTSDM.getDesign().get_nets().size(), input_net_count + synthesis.inserted_net_count);
  auto* committed_clock = CTSDM.getDesign().findClock("native_clk", "clk_net");
  ASSERT_NE(committed_clock, nullptr);
  EXPECT_EQ(committed_clock->findPropagationArc(CTSDM.getDesign().findInst("boundary_buf")), nullptr);
  EXPECT_EQ(committed_clock->findPropagationArc(CTSDM.getDesign().findInst("boundary_inv")), nullptr);
  EXPECT_TRUE(CTSDM.getDesign().rebuildClockDAG());

  CTSDM.getConfig().set_buffer_types({});
  const auto optimization = icts::Optimization::run();
  ASSERT_TRUE(optimization.success) << optimization.reason;
  ASSERT_EQ(CTSDM.getState(), icts::CTSRunState::kOptimizationCommitted);

  const auto instantiation = icts::Instantiation::run();
  ASSERT_TRUE(instantiation.success) << instantiation.failure_reason;
  EXPECT_EQ(instantiation.inserted_inst_count, synthesis.inserted_inst_count);
  EXPECT_EQ(instantiation.inserted_net_count, synthesis.inserted_net_count);
  ASSERT_EQ(CTSDM.getState(), icts::CTSRunState::kInstantiationCommitted);

  const auto evaluation = icts::Evaluation::run();
  ASSERT_TRUE(evaluation.summary.evaluation_ready);
  ASSERT_EQ(CTSDM.getState(), icts::CTSRunState::kEvaluationCommitted);
  EXPECT_EQ(static_cast<std::size_t>(evaluation.output.state.summary.final_clock_buffer_count), synthesis.inserted_inst_count);
  EXPECT_EQ(static_cast<std::size_t>(evaluation.output.state.summary.clock_member_buffer_count), synthesis.inserted_inst_count);

  const auto report_status = icts::CTSAPI::report(external_idb.reportDir().string());
  EXPECT_EQ(report_status.code, icts::CTSStatusCode::kOk) << report_status.message;
  EXPECT_TRUE(std::filesystem::exists(external_idb.reportDir() / "statistics" / "wirelength.rpt"));
}

TEST(CTSAPILifecycleTest, NativeCharacterizationProducerPublishesExactBufferPairsToTimingAndPower)
{
  const ScopedNativeClockExternalIdb external_idb("native_characterization_pairs");
  ASSERT_TRUE(external_idb.ready());
  const auto init_status = icts::CTSAPI::init(external_idb.configPath().string(), external_idb.workDir().string());
  ASSERT_EQ(init_status.code, icts::CTSStatusCode::kOk) << init_status.message;

  auto& fast_sta = CTSDM.getFastSTA();
  auto& wrapper = CTSDM.getWrapper();
  ASSERT_TRUE(wrapper.queryBufferPorts("BUF_X1").has_value());
  ASSERT_TRUE(wrapper.queryCharInputPinCap("BUF_X1").has_value());
  ASSERT_TRUE(wrapper.queryCellOutPinCapLimit("BUF_X1").has_value());
  ASSERT_TRUE(wrapper.queryCellInPinSlewLimit("BUF_X1").has_value());
  ASSERT_TRUE(wrapper.queryCellAreaUm2("BUF_X1").has_value());
  const auto context_build = fast_sta.buildCharContext(icts::FastStaCharTopologySpec{
      .wrapper = &wrapper,
      .source_cell_master = "BUF_X1",
      .sink_cell_master = "BUF_X1",
      .buffer_cell_masters = {"BUF_X1"},
      .wire_segments_um = {0.25, 0.25},
      .dbu_per_um = CTSDM.getWrapper().queryDbUnit(),
      .routing_layer = 1,
      .wire_width_um = std::nullopt,
      .clock_period_ns = 10.0,
      .root_input_slew_ns = 0.02,
  });
  if (!context_build.context_id.has_value()) {
    ADD_FAILURE() << context_build.failure_reason;
    return;
  }
  const auto context_id = *context_build.context_id;
  ASSERT_TRUE(fast_sta.setCharLoad(context_id, 0.02));

  const auto sample = fast_sta.runCharSample(context_id, 0.02);
  EXPECT_TRUE(sample.valid);
  EXPECT_GT(sample.delay_ns, 0.0);
  EXPECT_GT(sample.output_slew_ns, 0.0);
  EXPECT_GT(sample.power_w, 0.0);
  EXPECT_GT(sample.source_boundary_net_switch_power_w, 0.0);
  EXPECT_TRUE(fast_sta.eraseCharContext(context_id));
}

TEST(CTSAPILifecycleTest, SameClockPartialTracedTopologySynthesizesOnlyExplicitFrontierAndRepeatsSafely)
{
  const ScopedNativeClockExternalIdb external_idb("partial_traced_frontier");
  ASSERT_TRUE(external_idb.ready());
  const auto init_status = icts::CTSAPI::init(external_idb.configPath().string(), external_idb.workDir().string());
  ASSERT_EQ(init_status.code, icts::CTSStatusCode::kOk) << init_status.message;

  auto& input_design = CTSDM.getDesign();
  auto* input_clock = input_design.findClock("native_clk", "clk_net");
  auto* input_buffer = input_design.findInst("boundary_buf");
  auto* input_pin = input_design.findPin("boundary_buf/A");
  auto* output_pin = input_design.findPin("boundary_buf/Y");
  auto* boundary_pin = input_design.findPin("boundary_inv/A");
  auto* sink_pin = input_design.findPin("sink/CLK");
  auto* leaf_net = input_design.findNet("traced_leaf");
  ASSERT_NE(input_clock, nullptr);
  ASSERT_NE(input_buffer, nullptr);
  ASSERT_NE(input_pin, nullptr);
  ASSERT_NE(output_pin, nullptr);
  ASSERT_NE(boundary_pin, nullptr);
  ASSERT_NE(sink_pin, nullptr);
  ASSERT_NE(leaf_net, nullptr);
  auto* source_net = input_clock->get_clock_source_net();
  ASSERT_NE(source_net, nullptr);
  ASSERT_NE(input_clock->get_clock_source(), nullptr);
  input_clock->get_clock_source()->set_location(boundary_pin->get_location());
  const auto* input_arc = input_clock->findPropagationArc(input_buffer);
  ASSERT_NE(input_arc, nullptr);
  EXPECT_EQ(input_arc->input_pin, input_pin);
  EXPECT_EQ(input_arc->output_pin, output_pin);
  EXPECT_EQ(input_arc->origin, icts::ClockPropagationOrigin::kTracedInput);
  EXPECT_EQ(input_clock->findPropagationArc(boundary_pin->get_inst()), nullptr);
  ASSERT_EQ(source_net->get_loads().size(), 2U);
  ASSERT_EQ(leaf_net->get_loads().size(), 13U);
  ASSERT_EQ(input_clock->get_loads().size(), 14U);
  const auto input_inst_count = input_design.get_insts().size();
  const auto input_net_count = input_design.get_nets().size();
  const auto input_frontier = icts::ClockTreeRealization::deriveSynthesisFrontier(*input_clock);
  ASSERT_TRUE(input_frontier.hasTracedTopology());
  ASSERT_EQ(input_frontier.top_level_traced_inputs, std::vector<icts::Pin*>({input_pin}));
  ASSERT_EQ(input_frontier.uncovered_terminal_loads, std::vector<icts::Pin*>({boundary_pin}));
  EXPECT_EQ(input_frontier.pins.size(), 2U);
  ASSERT_TRUE(input_design.rebuildClockDAG());

  const auto synthesis = icts::Synthesis::run();
  ASSERT_TRUE(synthesis.success) << synthesis.failure_reason;
  EXPECT_EQ(synthesis.outcome, icts::SynthesisOutcome::kFinished);
  EXPECT_EQ(synthesis.commit_status, "committed");
  EXPECT_EQ(synthesis.total_clocks, 1U);
  EXPECT_EQ(synthesis.successful_clocks, 1U);
  EXPECT_EQ(synthesis.regular_sinks, 14U);
  EXPECT_EQ(synthesis.hard_macro_sinks, 0U);
  EXPECT_EQ(synthesis.total_sink_domains, 1U);
  EXPECT_GT(synthesis.inserted_inst_count, 0U);
  EXPECT_GT(synthesis.inserted_net_count, 0U);

  auto& committed_design = CTSDM.getDesign();
  auto* committed_clock = committed_design.findClock("native_clk", "clk_net");
  auto* committed_buffer = committed_design.findInst("boundary_buf");
  auto* committed_leaf_net = committed_design.findNet("traced_leaf");
  auto* committed_sink_pin = committed_design.findPin("sink/CLK");
  auto* committed_boundary_pin = committed_design.findPin("boundary_inv/A");
  ASSERT_NE(committed_clock, nullptr);
  ASSERT_NE(committed_buffer, nullptr);
  ASSERT_NE(committed_leaf_net, nullptr);
  ASSERT_NE(committed_sink_pin, nullptr);
  ASSERT_NE(committed_boundary_pin, nullptr);
  const auto* committed_arc = committed_clock->findPropagationArc(committed_buffer);
  ASSERT_NE(committed_arc, nullptr);
  EXPECT_EQ(committed_arc->origin, icts::ClockPropagationOrigin::kTracedInput);
  EXPECT_EQ(committed_arc->path_buffer_weight, 1);
  EXPECT_EQ(committed_arc->input_pin->get_name(), "A");
  EXPECT_EQ(committed_arc->output_pin->get_name(), "Y");
  EXPECT_EQ(committed_leaf_net->get_driver(), committed_arc->output_pin);
  ASSERT_EQ(committed_leaf_net->get_loads().size(), 13U);
  EXPECT_NE(std::ranges::find(committed_leaf_net->get_loads(), committed_sink_pin), committed_leaf_net->get_loads().end());
  EXPECT_EQ(committed_sink_pin->get_net(), committed_leaf_net);
  for (std::size_t sink_index = 1U; sink_index < 13U; ++sink_index) {
    auto* traced_sink_pin = committed_design.findPin("traced_sink_" + std::to_string(sink_index) + "/CLK");
    ASSERT_NE(traced_sink_pin, nullptr);
    EXPECT_EQ(traced_sink_pin->get_net(), committed_leaf_net);
    EXPECT_NE(std::ranges::find(committed_clock->get_loads(), traced_sink_pin), committed_clock->get_loads().end());
  }
  std::size_t synthesized_arc_count = 0U;
  for (const auto& arc : committed_clock->get_propagation_arcs()) {
    synthesized_arc_count += static_cast<std::size_t>(arc.origin == icts::ClockPropagationOrigin::kSynthesized);
  }
  EXPECT_EQ(synthesized_arc_count, synthesis.inserted_inst_count);
  EXPECT_EQ(committed_clock->get_loads().size(), 14U);
  EXPECT_NE(std::ranges::find(committed_clock->get_loads(), committed_sink_pin), committed_clock->get_loads().end());
  EXPECT_NE(std::ranges::find(committed_clock->get_loads(), committed_boundary_pin), committed_clock->get_loads().end());
  EXPECT_EQ(std::ranges::find(committed_clock->get_loads(), committed_arc->input_pin), committed_clock->get_loads().end());
  ASSERT_TRUE(committed_design.rebuildClockDAG());
  EXPECT_GT(committed_design.get_clock_dag().reachableNets(committed_clock).size(), 2U);
  const auto path_stats = committed_design.get_clock_dag().pathBufferStats(committed_clock);
  EXPECT_TRUE(path_stats.available);
  EXPECT_EQ(path_stats.min_buffer_count, 2);
  EXPECT_EQ(path_stats.max_buffer_count, 2);
  EXPECT_EQ(path_stats.ff_sink_terminal_count, 13U);
  EXPECT_EQ(committed_clock->findPropagationArc(committed_design.findInst("boundary_inv")), nullptr);
  EXPECT_EQ(CTSDM.getClockLayout().findInst(0U, "boundary_buf"), nullptr);
  const auto* boundary_layout = CTSDM.getClockLayout().findInst(0U, "boundary_inv");
  const auto* traced_sink_layout = CTSDM.getClockLayout().findInst(0U, "traced_sink_1");
  ASSERT_NE(boundary_layout, nullptr);
  ASSERT_NE(traced_sink_layout, nullptr);
  EXPECT_EQ(boundary_layout->role, icts::LayoutInstRole::kClockLoad);
  EXPECT_EQ(traced_sink_layout->role, icts::LayoutInstRole::kClockLoad);
  EXPECT_EQ(committed_design.get_insts().size(), input_inst_count + synthesis.inserted_inst_count);
  EXPECT_EQ(committed_design.get_nets().size(), input_net_count + synthesis.inserted_net_count);

  const auto first_inst_count = committed_design.get_insts().size();
  const auto first_net_count = committed_design.get_nets().size();
  const auto repeated_synthesis = icts::Synthesis::run();
  ASSERT_TRUE(repeated_synthesis.success) << repeated_synthesis.failure_reason;
  EXPECT_EQ(repeated_synthesis.commit_status, "committed");
  EXPECT_EQ(CTSDM.getState(), icts::CTSRunState::kSynthesisCommitted);
  EXPECT_EQ(repeated_synthesis.inserted_inst_count, synthesis.inserted_inst_count);
  EXPECT_EQ(repeated_synthesis.inserted_net_count, synthesis.inserted_net_count);
  EXPECT_EQ(CTSDM.getDesign().get_insts().size(), first_inst_count);
  EXPECT_EQ(CTSDM.getDesign().get_nets().size(), first_net_count);
  auto* repeated_clock = CTSDM.getDesign().findClock("native_clk", "clk_net");
  ASSERT_NE(repeated_clock, nullptr);
  ASSERT_TRUE(CTSDM.getDesign().rebuildClockDAG());
  const auto* repeated_traced_arc = repeated_clock->findPropagationArc(CTSDM.getDesign().findInst("boundary_buf"));
  ASSERT_NE(repeated_traced_arc, nullptr);
  EXPECT_EQ(repeated_traced_arc->origin, icts::ClockPropagationOrigin::kTracedInput);
  EXPECT_EQ(repeated_traced_arc->path_buffer_weight, 1);
  const auto exact_input_cap_pf = CTSDM.getWrapper().queryPinCapacitance(repeated_traced_arc->input_pin);
  ASSERT_TRUE(exact_input_cap_pf.has_value());
  EXPECT_DOUBLE_EQ(exact_input_cap_pf.value_or(-1.0), 0.01);
  EXPECT_FALSE(CTSDM.getWrapper().queryPinCapacitance(repeated_traced_arc->output_pin).has_value());
  icts::Qor exact_arc_statistics;
  const auto exact_arc_metric_status
      = icts::qor_evaluation::AccumulateInstStatistics(CTSDM.getWrapper(), *repeated_traced_arc->inst, repeated_traced_arc, exact_arc_statistics);
  EXPECT_TRUE(exact_arc_metric_status.ok());
  const auto exact_buffer_stats = exact_arc_statistics.cell_stats.find("Buffer");
  ASSERT_NE(exact_buffer_stats, exact_arc_statistics.cell_stats.end());
  ASSERT_TRUE(exact_buffer_stats->second.total_cap_pf.has_value());
  EXPECT_DOUBLE_EQ(exact_buffer_stats->second.total_cap_pf.value_or(-1.0), 0.01);
  icts::Qor rejected_arc_statistics;
  const auto missing_arc_metric_status
      = icts::qor_evaluation::AccumulateInstStatistics(CTSDM.getWrapper(), *repeated_traced_arc->inst, nullptr, rejected_arc_statistics);
  EXPECT_EQ(missing_arc_metric_status.issue, icts::qor_evaluation::ClockCellMetricIssue::kMissingPropagationArc);
  auto mismatched_arc = *repeated_traced_arc;
  mismatched_arc.inst = CTSDM.getDesign().findInst("boundary_inv");
  const auto mismatched_arc_metric_status
      = icts::qor_evaluation::AccumulateInstStatistics(CTSDM.getWrapper(), *repeated_traced_arc->inst, &mismatched_arc, rejected_arc_statistics);
  EXPECT_EQ(mismatched_arc_metric_status.issue, icts::qor_evaluation::ClockCellMetricIssue::kPropagationInstMismatch);
  auto* repeated_leaf_net = CTSDM.getDesign().findNet("traced_leaf");
  auto* repeated_sink_pin = CTSDM.getDesign().findPin("sink/CLK");
  ASSERT_NE(repeated_leaf_net, nullptr);
  ASSERT_NE(repeated_sink_pin, nullptr);
  EXPECT_EQ(repeated_leaf_net->get_driver(), repeated_traced_arc->output_pin);
  ASSERT_EQ(repeated_leaf_net->get_loads().size(), 13U);
  EXPECT_NE(std::ranges::find(repeated_leaf_net->get_loads(), repeated_sink_pin), repeated_leaf_net->get_loads().end());
  for (std::size_t sink_index = 1U; sink_index < 13U; ++sink_index) {
    auto* traced_sink_pin = CTSDM.getDesign().findPin("traced_sink_" + std::to_string(sink_index) + "/CLK");
    ASSERT_NE(traced_sink_pin, nullptr);
    EXPECT_EQ(traced_sink_pin->get_net(), repeated_leaf_net);
  }

  CTSDM.getConfig().set_buffer_types({});
  const auto optimization = icts::Optimization::run();
  ASSERT_TRUE(optimization.success) << optimization.reason;
  EXPECT_EQ(CTSDM.getState(), icts::CTSRunState::kOptimizationCommitted);
  const auto instantiation = icts::Instantiation::run();
  ASSERT_TRUE(instantiation.success) << instantiation.failure_reason;
  EXPECT_EQ(instantiation.inserted_inst_count, repeated_synthesis.inserted_inst_count);
  EXPECT_EQ(instantiation.inserted_net_count, repeated_synthesis.inserted_net_count);
  EXPECT_EQ(CTSDM.getState(), icts::CTSRunState::kInstantiationCommitted);
  const auto evaluation = icts::Evaluation::run();
  ASSERT_TRUE(evaluation.summary.evaluation_ready);
  EXPECT_EQ(CTSDM.getState(), icts::CTSRunState::kEvaluationCommitted);
  const auto expected_buffer_count = static_cast<int32_t>(repeated_synthesis.inserted_inst_count + 1U);
  EXPECT_EQ(evaluation.output.state.summary.qor_metric_status, "available");
  EXPECT_EQ(evaluation.output.state.summary.final_clock_buffer_count, expected_buffer_count);
  EXPECT_EQ(evaluation.output.state.summary.clock_member_buffer_count, expected_buffer_count);
  const auto buffer_stats = evaluation.output.state.statistics.cell_stats.find("Buffer");
  ASSERT_NE(buffer_stats, evaluation.output.state.statistics.cell_stats.end());
  EXPECT_EQ(buffer_stats->second.count, static_cast<std::size_t>(expected_buffer_count));
  ASSERT_TRUE(buffer_stats->second.total_cap_pf.has_value());
  EXPECT_NEAR(buffer_stats->second.total_cap_pf.value_or(-1.0), 0.01 * static_cast<double>(expected_buffer_count), 1e-12);
  EXPECT_EQ(evaluation.output.state.summary.path_depth_metric_status, "available");
  EXPECT_EQ(evaluation.output.state.summary.clock_path_min_buffer, 2);
  EXPECT_EQ(evaluation.output.state.summary.clock_path_max_buffer, 2);
}

TEST(CTSAPILifecycleTest, FullyCoveredTracedTopologyRemainsZeroInsertionAcrossRepeatedSynthesis)
{
  const ScopedNativeClockExternalIdb external_idb("fully_covered_traced");
  ASSERT_TRUE(external_idb.ready());
  const auto init_status = icts::CTSAPI::init(external_idb.configPath().string(), external_idb.workDir().string());
  ASSERT_EQ(init_status.code, icts::CTSStatusCode::kOk) << init_status.message;

  auto& input_design = CTSDM.getDesign();
  auto* input_clock = input_design.findClock("native_clk", "clk_net");
  auto* input_buffer = input_design.findInst("boundary_buf");
  auto* input_pin = input_design.findPin("boundary_buf/A");
  auto* boundary_pin = input_design.findPin("boundary_inv/A");
  auto* sink_pin = input_design.findPin("sink/CLK");
  ASSERT_NE(input_clock, nullptr);
  ASSERT_NE(input_buffer, nullptr);
  ASSERT_NE(input_pin, nullptr);
  ASSERT_NE(boundary_pin, nullptr);
  ASSERT_NE(sink_pin, nullptr);
  auto* source_net = input_clock->get_clock_source_net();
  ASSERT_NE(source_net, nullptr);
  auto* output_pin = input_design.makePin("Y");
  ASSERT_NE(output_pin, nullptr);
  output_pin->set_type(icts::PinType::kOut);
  output_pin->set_location(input_buffer->get_location());
  output_pin->set_inst(input_buffer);
  input_buffer->add_pin(output_pin);
  ASSERT_TRUE(input_design.indexPin(output_pin));
  auto* leaf_net = input_design.makeNet("fully_covered_leaf");
  ASSERT_NE(leaf_net, nullptr);
  source_net->set_loads({input_pin});
  input_pin->set_net(source_net);
  boundary_pin->set_net(nullptr);
  leaf_net->set_driver(output_pin);
  leaf_net->set_loads({sink_pin});
  output_pin->set_net(leaf_net);
  sink_pin->set_net(leaf_net);
  input_clock->set_loads({sink_pin});
  input_clock->add_net(leaf_net);
  ASSERT_TRUE(input_clock
                  ->addPropagationArc({.inst = input_buffer,
                                       .input_pin = input_pin,
                                       .output_pin = output_pin,
                                       .kind = icts::ClockPropagationKind::kBuffer,
                                       .origin = icts::ClockPropagationOrigin::kTracedInput,
                                       .path_buffer_weight = 1})
                  .ok());
  ASSERT_TRUE(input_design.rebuildClockDAG());
  const auto input_inst_count = input_design.get_insts().size();
  const auto input_net_count = input_design.get_nets().size();

  const auto first = icts::Synthesis::run();
  ASSERT_TRUE(first.success) << first.failure_reason;
  EXPECT_EQ(first.inserted_inst_count, 0U);
  EXPECT_EQ(first.inserted_net_count, 0U);
  EXPECT_EQ(first.commit_status, "committed");
  const auto second = icts::Synthesis::run();
  ASSERT_TRUE(second.success) << second.failure_reason;
  EXPECT_EQ(second.inserted_inst_count, 0U);
  EXPECT_EQ(second.inserted_net_count, 0U);
  EXPECT_EQ(second.commit_status, "committed");
  EXPECT_EQ(CTSDM.getDesign().get_insts().size(), input_inst_count);
  EXPECT_EQ(CTSDM.getDesign().get_nets().size(), input_net_count);
  auto* committed_clock = CTSDM.getDesign().findClock("native_clk", "clk_net");
  ASSERT_NE(committed_clock, nullptr);
  ASSERT_EQ(committed_clock->get_propagation_arcs().size(), 1U);
  EXPECT_EQ(committed_clock->get_propagation_arcs().front().origin, icts::ClockPropagationOrigin::kTracedInput);
  EXPECT_NE(CTSDM.getDesign().findNet("fully_covered_leaf"), nullptr);
  ASSERT_TRUE(CTSDM.getDesign().rebuildClockDAG());
}

TEST(CTSAPILifecycleTest, DestroyPreservesBorrowedExternalIdbObjectsAndConnectivity)
{
  icts::DataManager::initInst();
  idb::IdbDesign external_design;
  auto* driver_inst = external_design.get_instance_list()->add_instance("cts_driver");
  auto* load_inst = external_design.get_instance_list()->add_instance("cts_load");
  ASSERT_NE(driver_inst, nullptr);
  ASSERT_NE(load_inst, nullptr);
  driver_inst->set_coodinate(100, 200, false);
  load_inst->set_coodinate(300, 400, false);
  auto* driver_pin = driver_inst->addPin("Y");
  auto* load_pin = load_inst->addPin("A");
  ASSERT_NE(driver_pin, nullptr);
  ASSERT_NE(load_pin, nullptr);
  driver_pin->set_location(110, 210);
  load_pin->set_location(310, 410);

  auto* clock_net = external_design.get_net_list()->add_net("cts_clock_net", idb::IdbConnectType::kClock);
  ASSERT_NE(clock_net, nullptr);
  clock_net->add_instance_pin(driver_pin);
  clock_net->add_instance_pin(load_pin);
  driver_pin->set_net(clock_net);
  driver_pin->set_net_name(clock_net->get_net_name());
  load_pin->set_net(clock_net);
  load_pin->set_net_name(clock_net->get_net_name());

  CTSDM.getWrapper().set_idb_design(&external_design);
  const auto before = Fingerprint(external_design);

  EXPECT_TRUE(icts::CTSAPI::destroyCTS().ok());

  const auto after = Fingerprint(external_design);
  EXPECT_EQ(after, before);
  ASSERT_EQ(after.insts.size(), 2U);
  ASSERT_EQ(after.nets.size(), 1U);
  ASSERT_EQ(after.pins.size(), 2U);
}

TEST(CTSAPILifecycleTest, CompletionIsLoggedBeforeLoggerDestruction)
{
  icts::Logger::initInst();
  icts::DataManager::initInst();
  const auto output_dir = MakeUniqueOutputDir("log_order");
  std::filesystem::create_directories(output_dir);
  const auto log_path = output_dir / "cts.log";
  CTSLOG.openLogFileStream(log_path.string());
  PopulateRepresentativeSession(32U);

  ASSERT_TRUE(icts::CTSAPI::destroyCTS().ok());

  std::ifstream log_file(log_path);
  const std::string log_text((std::istreambuf_iterator<char>(log_file)), std::istreambuf_iterator<char>());
  const auto start_pos = log_text.find("Starting CTS destruction");
  const auto completion_pos = log_text.find("Completed CTS destruction");
  EXPECT_NE(start_pos, std::string::npos);
  EXPECT_NE(completion_pos, std::string::npos);
  EXPECT_LT(start_pos, completion_pos);

  RemoveOutputDir(output_dir);
}

TEST(CTSAPILifecycleTest, TenCyclesRemainWithinApprovedPostDestroyRssThreshold)
{
#ifdef __linux__
  const ScopedMinimalExternalIdb external_idb("ten_cycles");
  ASSERT_TRUE(external_idb.ready());
  std::array<double, 10> post_destroy_rss_mb{};
  for (std::size_t cycle = 0U; cycle < post_destroy_rss_mb.size(); ++cycle) {
    const auto init_status = icts::CTSAPI::init(external_idb.configPath().string(), external_idb.workDir().string());
    ASSERT_EQ(init_status.code, icts::CTSStatusCode::kOk) << "cycle " << cycle << ": " << init_status.message;
    EXPECT_TRUE(icts::CTSAPI::outputClockTiming().empty());
    EXPECT_EQ(icts::CTSAPI::runCTS().code, icts::CTSStatusCode::kNoOp);
    PopulateRepresentativeSession(4096U);
    ASSERT_TRUE(icts::CTSAPI::destroyCTS().ok());
    ASSERT_TRUE(icts::CTSAPI::destroyCTS().ok());
    const auto rss_mb = icts::Utility::currentRssMb();
    ASSERT_TRUE(rss_mb.has_value());
    post_destroy_rss_mb.at(cycle) = rss_mb.value_or(0.0);
  }

  const std::array<double, 4> early_samples = {post_destroy_rss_mb.at(1U), post_destroy_rss_mb.at(2U), post_destroy_rss_mb.at(3U), post_destroy_rss_mb.at(4U)};
  const std::array<double, 4> late_samples = {post_destroy_rss_mb.at(6U), post_destroy_rss_mb.at(7U), post_destroy_rss_mb.at(8U), post_destroy_rss_mb.at(9U)};
  const double early_median_mb = Median(early_samples);
  const double late_median_mb = Median(late_samples);
  constexpr double absolute_tolerance_mb = 16.0;
  const double allowed_growth_mb = std::max(absolute_tolerance_mb, early_median_mb * 0.05);
  EXPECT_LE(late_median_mb, early_median_mb + allowed_growth_mb);

  bool sustained_growth = true;
  for (std::size_t index = 2U; index < post_destroy_rss_mb.size(); ++index) {
    sustained_growth = sustained_growth && post_destroy_rss_mb.at(index) > post_destroy_rss_mb.at(index - 1U);
  }
  EXPECT_FALSE(sustained_growth);
#else
  GTEST_SKIP() << "Linux /proc RSS sampling is unavailable.";
#endif
}

}  // namespace
}  // namespace icts_test
