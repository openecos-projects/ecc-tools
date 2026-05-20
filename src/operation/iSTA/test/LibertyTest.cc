// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

// #include <gperftools/heap-profiler.h>

#include <limits>
#include <string_view>

#include "api/TimingEngine.hh"
#include "gtest/gtest.h"
#include "liberty/Lib.hh"
#include "log/Log.hh"
#include "string/Str.hh"

using ieda::Log;
using ista::Lib;

using namespace ista;

namespace {

std::unique_ptr<ista::LibTable> makeScalarTable(ista::LibTable::TableType type,
                                                double value) {
  auto table = std::make_unique<ista::LibTable>(type, nullptr);
  table->addTableValue(std::make_unique<ista::LibFloatValue>(value));
  return table;
}

std::unique_ptr<ista::LibArc> makeScalarDelayArcWithWhen(
    ista::LibCell* lib_cell, const char* src_port, const char* snk_port,
    double cell_rise_value, const char* when = nullptr) {
  auto lib_arc = std::make_unique<ista::LibArc>();
  lib_arc->set_src_port(src_port);
  lib_arc->set_snk_port(snk_port);
  lib_arc->set_timing_type("combinational");
  lib_arc->set_timing_sense("positive_unate");
  if (when) {
    lib_arc->set_when(when);
  }
  lib_arc->set_owner_cell(lib_cell);

  auto delay_model = std::make_unique<ista::LibDelayTableModel>();
  delay_model->addTable(
      makeScalarTable(ista::LibTable::TableType::kCellRise, cell_rise_value));
  lib_arc->set_table_model(std::move(delay_model));
  return lib_arc;
}

std::unique_ptr<ista::LibArc> makeScalarDelayArcWithWhenAndSlew(
    ista::LibCell* lib_cell, const char* src_port, const char* snk_port,
    double cell_rise_value, double rise_transition_value,
    const char* when = nullptr) {
  auto lib_arc =
      makeScalarDelayArcWithWhen(lib_cell, src_port, snk_port, cell_rise_value,
                                 when);
  auto* delay_model =
      dynamic_cast<ista::LibDelayTableModel*>(lib_arc->get_table_model());
  EXPECT_NE(delay_model, nullptr);
  delay_model->addTable(makeScalarTable(ista::LibTable::TableType::kRiseTransition,
                                        rise_transition_value));
  return lib_arc;
}

class LibertyTest : public testing::Test {
  void SetUp() {
    char config[] = "test";
    char* argv[] = {config};
    Log::init(argv);
  }
  void TearDown() { Log::end(); }
};

TEST_F(LibertyTest, cpp_reader) {
  const char* lib_path =
      "/home/ieda/ssta-data/lib/lib/tcbn28hpcplusbwp30p140ulvtssg0p81v125c.lib";
  Lib lib;
  lib.loadLibertyWithCppParser(lib_path);
}

TEST_F(LibertyTest, cpp_expr_builder) {
  LibertyExprBuilder expr_builder("(!((A1 A2)+(B1 B2)))");
  expr_builder.execute();
  auto* func_expr = expr_builder.get_result_expr();
  LOG_FATAL_IF(!func_expr) << "func_expr is nullptr";
}

TEST_F(LibertyTest, cpp_expr_builder_backslack) {
  LibertyExprBuilder expr_builder(R"((!CEN & ! \
                                   WEN & !( \
                                (BWEN[0]) & \
                                (BWEN[1]) & \
                                (BWEN[2]) & \
                                (BWEN[3]) & \
                                (BWEN[4]) & \
                                (BWEN[5]) & \
                                (BWEN[6]) & \
                                (BWEN[7]) & \
                                (BWEN[8]) & \
                                (BWEN[9]) & \
                                (BWEN[10]) & \
                                (BWEN[11]) & \
                                (BWEN[12]) & \
                                (BWEN[13]) & \
                                (BWEN[14]) & \
                                (BWEN[15]) & \
                                (BWEN[16]) & \
                                (BWEN[17]) & \
                                (BWEN[18]) & \
                                (BWEN[19]) & \
                                (BWEN[20]) & \
                                (BWEN[21]) & \
                                (BWEN[22]) & \
                                (BWEN[23]) & \
                                (BWEN[24]) & \
                                (BWEN[25]) & \
                                (BWEN[26]) & \
                                (BWEN[27]) & \
                                (BWEN[28]) & \
                                (BWEN[29]) & \
                                (BWEN[30]) & \
                                (BWEN[31])) \
                                ) \
                                 )");
  expr_builder.execute();
  auto* func_expr = expr_builder.get_result_expr();
  LOG_FATAL_IF(!func_expr) << "func_expr is nullptr";
}

TEST_F(LibertyTest, print_liberty_library_json) {
  const char* lib_path =
      "/home/taosimin/nangate45/lib/NangateOpenCellLibrary_typical.lib";
  Lib lib;
  auto liberty_reader = lib.loadLibertyWithCppParser(lib_path);
  liberty_reader.linkLib();
  auto lib_library = liberty_reader.get_library_builder()->takeLib();
  // lib_library->findCell()->get_cell_arcs();
  // const char* json_file_names_n45 =
  //     "/home/longshuaiying/lib_lef/"
  //     "NangateOpenCellLibrary_typical.json";
  // lib_library->printLibertyLibraryJson(json_file_names_n45);
}

TEST_F(LibertyTest, cpp_reader_converts_ff_pin_cap_ranges_to_internal_pf) {
  const char* lib_path =
      "/home/zhaoxueyan/code/write-lib_back/benchmark/iccad24-benchmark/ASAP7/"
      "lib/asap7sc7p5t_SEQ_RVT_FF_nldm_201020.lib";

  Lib lib;
  auto liberty_reader = lib.loadLibertyWithCppParser(lib_path);
  liberty_reader.linkLib();
  auto lib_library = liberty_reader.get_library_builder()->takeLib();

  ASSERT_NE(lib_library, nullptr);
  EXPECT_EQ(lib_library->get_cap_unit(), CapacitiveUnit::kFF);

  auto* lib_cell = lib_library->findCell("ASYNC_DFFHx1_ASAP7_75t_R");
  ASSERT_NE(lib_cell, nullptr);

  auto* data_port = lib_cell->get_cell_port_or_port_bus("D");
  ASSERT_NE(data_port, nullptr);

  EXPECT_NEAR(data_port->get_port_cap(), 0.000621396, 1e-9);

  const auto max_rise_cap =
      data_port->get_port_cap(AnalysisMode::kMax, TransType::kRise);
  const auto min_rise_cap =
      data_port->get_port_cap(AnalysisMode::kMin, TransType::kRise);
  const auto max_fall_cap =
      data_port->get_port_cap(AnalysisMode::kMax, TransType::kFall);
  const auto min_fall_cap =
      data_port->get_port_cap(AnalysisMode::kMin, TransType::kFall);

  ASSERT_TRUE(max_rise_cap.has_value());
  ASSERT_TRUE(min_rise_cap.has_value());
  ASSERT_TRUE(max_fall_cap.has_value());
  ASSERT_TRUE(min_fall_cap.has_value());

  EXPECT_NEAR(*max_rise_cap, 0.000619712, 1e-9);
  EXPECT_NEAR(*min_rise_cap, 0.000553479, 1e-9);
  EXPECT_NEAR(*max_fall_cap, 0.000621396, 1e-9);
  EXPECT_NEAR(*min_fall_cap, 0.000554061, 1e-9);
}

TEST_F(LibertyTest,
       cpp_reader_same_sense_arc_sets_prefer_declared_default_arc) {
  const char* lib_path =
      "/home/zhaoxueyan/code/write-lib_back/benchmark/iccad24-benchmark/ASAP7/"
      "lib/asap7sc7p5t_AO_RVT_FF_nldm_201020.lib";

  Lib lib;
  auto liberty_reader = lib.loadLibertyWithCppParser(lib_path);
  liberty_reader.linkLib();
  auto lib_library = liberty_reader.get_library_builder()->takeLib();

  ASSERT_NE(lib_library, nullptr);
  auto* lib_cell = lib_library->findCell("AOI22xp33_ASAP7_75t_R");
  ASSERT_NE(lib_cell, nullptr);

  LibArcSet* b2_to_y_arc_set = nullptr;
  for (auto& cell_arc_set : lib_cell->get_cell_arcs()) {
    auto* front_arc = cell_arc_set->front();
    if (!front_arc) {
      continue;
    }
    if (std::string_view(front_arc->get_src_port()) == "B2" &&
        std::string_view(front_arc->get_snk_port()) == "Y") {
      b2_to_y_arc_set = cell_arc_set.get();
      break;
    }
  }

  ASSERT_NE(b2_to_y_arc_set, nullptr);
  ASSERT_GT(b2_to_y_arc_set->get_arcs().size(), 1U);
  ASSERT_FALSE(b2_to_y_arc_set->get_arcs().front()->get_when().empty());
  ASSERT_TRUE(b2_to_y_arc_set->get_arcs().back()->get_when().empty());

  constexpr double kInputSlew = 0.015;
  constexpr double kLoad = 0.36;

  auto* default_arc = b2_to_y_arc_set->get_arcs().back().get();
  ASSERT_NE(default_arc, nullptr);

  const auto default_delay =
      default_arc->getDelayOrConstrainCheckNs(TransType::kRise, kInputSlew,
                                              kLoad);
  const auto default_slew =
      default_arc->getSlewNs(TransType::kRise, kInputSlew, kLoad);

  double aggregated_min_delay = std::numeric_limits<double>::max();
  double aggregated_min_slew = std::numeric_limits<double>::max();
  for (auto& lib_arc_holder : b2_to_y_arc_set->get_arcs()) {
    auto* lib_arc = lib_arc_holder.get();
    ASSERT_NE(lib_arc, nullptr);
    aggregated_min_delay = std::min(
        aggregated_min_delay,
        lib_arc->getDelayOrConstrainCheckNs(TransType::kRise, kInputSlew,
                                            kLoad));
    aggregated_min_slew = std::min(
        aggregated_min_slew,
        lib_arc->getSlewNs(TransType::kRise, kInputSlew, kLoad));
  }

  const auto arc_set_delay = b2_to_y_arc_set->getDelayOrConstrainCheckNs(
      TransType::kFall, TransType::kRise, kInputSlew, kLoad);
  const auto arc_set_slew =
      b2_to_y_arc_set->getSlewNs(TransType::kFall, TransType::kRise,
                                 kInputSlew, kLoad);

  ASSERT_FALSE(arc_set_delay.empty());
  ASSERT_FALSE(arc_set_slew.empty());

  EXPECT_DOUBLE_EQ(arc_set_delay.back(), default_delay);
  EXPECT_DOUBLE_EQ(arc_set_slew.back(), default_slew);
  EXPECT_LT(aggregated_min_delay, default_delay);
  EXPECT_LT(aggregated_min_slew, default_slew);
}

TEST_F(LibertyTest,
       same_sense_arc_sets_without_fallback_keep_all_matching_arcs) {
  ista::LibLibrary lib("same_sense_without_fallback");
  auto lib_cell = std::make_unique<ista::LibCell>("same_sense_cell", &lib);
  auto* lib_cell_ptr = lib_cell.get();
  lib.addLibertyCell(std::move(lib_cell));

  ista::LibArcSet arc_set;
  arc_set.addLibertyArc(
      makeScalarDelayArcWithWhen(lib_cell_ptr, "A", "Y", 0.10, "COND0"));
  arc_set.addLibertyArc(
      makeScalarDelayArcWithWhen(lib_cell_ptr, "A", "Y", 0.20, "COND1"));

  const auto values = arc_set.getDelayOrConstrainCheckNs(
      TransType::kRise, TransType::kRise, 0.01, 0.36);

  ASSERT_EQ(values.size(), 2U);
  EXPECT_DOUBLE_EQ(values[0], 0.20);
  EXPECT_DOUBLE_EQ(values[1], 0.10);
}

TEST_F(LibertyTest,
       same_sense_arc_sets_prefer_declared_default_arc_regardless_of_order) {
  ista::LibLibrary lib("same_sense_fallback_any_order");
  auto lib_cell = std::make_unique<ista::LibCell>("same_sense_cell", &lib);
  auto* lib_cell_ptr = lib_cell.get();
  lib.addLibertyCell(std::move(lib_cell));

  ista::LibArcSet arc_set;
  arc_set.addLibertyArc(makeScalarDelayArcWithWhenAndSlew(
      lib_cell_ptr, "A", "Y", 0.10, 0.30));
  arc_set.addLibertyArc(makeScalarDelayArcWithWhenAndSlew(
      lib_cell_ptr, "A", "Y", 0.20, 0.40, "COND1"));
  arc_set.addLibertyArc(makeScalarDelayArcWithWhenAndSlew(
      lib_cell_ptr, "A", "Y", 0.25, 0.45, "COND2"));

  const auto delay_values = arc_set.getDelayOrConstrainCheckNs(
      TransType::kRise, TransType::kRise, 0.01, 0.36);
  const auto slew_values =
      arc_set.getSlewNs(TransType::kRise, TransType::kRise, 0.01, 0.36);

  ASSERT_EQ(delay_values.size(), 1U);
  EXPECT_DOUBLE_EQ(delay_values.front(), 0.10);
  ASSERT_EQ(slew_values.size(), 1U);
  EXPECT_DOUBLE_EQ(slew_values.front(), 0.30);
}

}  // namespace
