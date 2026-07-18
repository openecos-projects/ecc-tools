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
#include "CharacterTimingTestCommon.hh"
#include "api/TimingEngine.hh"
#include "gtest/gtest.h"
#include "liberty/Lib.hh"
#include "log/Log.hh"

#include <filesystem>
#include <optional>
#include <regex>
#include <set>
#include <system_error>
#include <string>

using namespace ista;
using namespace ieda;

namespace {

std::string extractNamedBlock(const std::string& text,
                              const std::string& keyword,
                              const std::string& name) {
  const std::string header = keyword + "(\"" + name + "\")";
  const auto pos = text.find(header);
  if (pos == std::string::npos) {
    return {};
  }

  const auto brace_open = text.find('{', pos + header.size());
  if (brace_open == std::string::npos) {
    return {};
  }

  int brace_depth = 0;
  for (size_t cursor = brace_open; cursor < text.size(); ++cursor) {
    if (text[cursor] == '{') {
      ++brace_depth;
    } else if (text[cursor] == '}') {
      --brace_depth;
      if (brace_depth == 0) {
        return text.substr(brace_open + 1, cursor - brace_open - 1);
      }
    }
  }

  return {};
}

std::string extractPinBlock(const std::string& liberty_text,
                            const std::string& pin_name) {
  return extractNamedBlock(liberty_text, "pin", pin_name);
}

std::optional<double> extractFirstTableValue(const std::string& block_text,
                                             const std::string& table_name) {
  const std::regex table_regex(table_name + R"(\s*\([^)]+\)\s*\{([\s\S]*?)\})");
  std::smatch table_match;
  if (!std::regex_search(block_text, table_match, table_regex)) {
    return std::nullopt;
  }

  const std::regex values_regex(R"(values\s*\(\s*\"?\s*([0-9eE+\-.]+))");
  std::smatch values_match;
  const auto table_body = table_match[1].str();
  if (!std::regex_search(table_body, values_match, values_regex)) {
    return std::nullopt;
  }

  return std::stod(values_match[1].str());
}

std::optional<double> extractPinCapacitance(const std::string& pin_block) {
  const std::regex cap_regex(R"(capacitance\s*:\s*([0-9eE+\-.]+)\s*;)");
  std::smatch cap_match;
  if (!std::regex_search(pin_block, cap_match, cap_regex)) {
    return std::nullopt;
  }

  return std::stod(cap_match[1].str());
}

std::optional<std::string> extractPinDirection(const std::string& pin_block) {
  const std::regex direction_regex(R"(direction\s*:\s*([A-Za-z_]+)\s*;)");
  std::smatch direction_match;
  if (!std::regex_search(pin_block, direction_match, direction_regex)) {
    return std::nullopt;
  }

  return direction_match[1].str();
}

std::set<std::string> extractBusTypeReferences(const std::string& liberty_text) {
  std::set<std::string> bus_types;
  const std::regex bus_type_regex(R"(bus_type\s*:\s*([A-Za-z0-9_]+)\s*;)");
  for (std::sregex_iterator iter(liberty_text.begin(), liberty_text.end(),
                                 bus_type_regex),
       end;
       iter != end; ++iter) {
    bus_types.emplace((*iter)[1].str());
  }
  return bus_types;
}

std::unique_ptr<ista::LibTable> makeScalarTable(ista::LibTable::TableType type,
                                                double value) {
  auto table = std::make_unique<ista::LibTable>(type, nullptr);
  table->addTableValue(std::make_unique<ista::LibFloatValue>(value));
  return table;
}

std::unique_ptr<ista::LibArc> makeScalarDelayArc(ista::LibCell* lib_cell,
                                                 const char* src_port,
                                                 const char* snk_port,
                                                 const char* timing_sense,
                                                 double cell_rise_value,
                                                 const char* when = nullptr) {
  auto lib_arc = std::make_unique<ista::LibArc>();
  lib_arc->set_src_port(src_port);
  lib_arc->set_snk_port(snk_port);
  lib_arc->set_timing_type("combinational");
  lib_arc->set_timing_sense(timing_sense);
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

std::unique_ptr<ista::LibArc> makeScalarCheckArc(ista::LibCell* lib_cell,
                                                 const char* src_port,
                                                 const char* snk_port,
                                                 const char* timing_type,
                                                 double rise_value,
                                                 double fall_value) {
  auto lib_arc = std::make_unique<ista::LibArc>();
  lib_arc->set_src_port(src_port);
  lib_arc->set_snk_port(snk_port);
  lib_arc->set_timing_type(timing_type);
  lib_arc->set_timing_sense("non_unate");
  lib_arc->set_owner_cell(lib_cell);

  auto check_model = std::make_unique<ista::LibCheckTableModel>();
  check_model->addTable(
      makeScalarTable(ista::LibTable::TableType::kRiseConstrain, rise_value));
  check_model->addTable(
      makeScalarTable(ista::LibTable::TableType::kFallConstrain, fall_value));
  lib_arc->set_table_model(std::move(check_model));
  return lib_arc;
}

std::optional<double> extractTimingScalarValue(const std::string& pin_block,
                                               const std::string& related_pin,
                                               const std::string& timing_type,
                                               const std::string& table_name) {
  const std::regex timing_regex(
      R"(timing\s*\(\s*\)\s*\{[\s\S]*?related_pin\s*:\s*\")" + related_pin +
      R"(\";\s*[\s\S]*?timing_type\s*:\s*)" + timing_type +
      R"(\s*;\s*[\s\S]*?)" + table_name +
      R"(\s*\(\s*scalar\s*\)\s*\{[\s\S]*?values\s*\(\s*\"?\s*([0-9eE+\-.]+))");
  std::smatch timing_match;
  if (!std::regex_search(pin_block, timing_match, timing_regex)) {
    return std::nullopt;
  }

  return std::stod(timing_match[1].str());
}

std::optional<double> extractTimingTableFirstValue(
    const std::string& pin_block, const std::string& related_pin,
    const std::string& timing_type, const std::string& table_name) {
  const std::regex timing_regex(
      R"(timing\s*\(\s*\)\s*\{[\s\S]*?related_pin\s*:\s*\")" + related_pin +
      R"(\";\s*[\s\S]*?timing_type\s*:\s*)" + timing_type +
      R"(\s*;\s*[\s\S]*?)" + table_name +
      R"(\s*\([^)]+\)\s*\{[\s\S]*?values\s*\(\s*\"?\s*([0-9eE+\-.]+))");
  std::smatch timing_match;
  if (!std::regex_search(pin_block, timing_match, timing_regex)) {
    return std::nullopt;
  }

  return std::stod(timing_match[1].str());
}

std::string extractTimingBlock(const std::string& pin_block,
                               const std::string& related_pin,
                               const std::string& timing_type) {
  size_t search_pos = 0;
  while (true) {
    const auto timing_pos = pin_block.find("timing", search_pos);
    if (timing_pos == std::string::npos) {
      return {};
    }

    const auto brace_open = pin_block.find('{', timing_pos);
    if (brace_open == std::string::npos) {
      return {};
    }

    int brace_depth = 0;
    for (size_t cursor = brace_open; cursor < pin_block.size(); ++cursor) {
      if (pin_block[cursor] == '{') {
        ++brace_depth;
      } else if (pin_block[cursor] == '}') {
        --brace_depth;
        if (brace_depth == 0) {
          const auto block =
              pin_block.substr(brace_open + 1, cursor - brace_open - 1);
          const auto related_pin_text =
              "related_pin        : \"" + related_pin + "\";";
          const auto timing_type_text =
              "timing_type        : " + timing_type + ";";
          if (block.find(related_pin_text) != std::string::npos &&
              block.find(timing_type_text) != std::string::npos) {
            return block;
          }
          search_pos = cursor + 1;
          break;
        }
      }
    }
  }
}

std::filesystem::path writeTempLibertyFile(const std::string& file_name,
                                           const std::string& contents) {
  const auto output_dir = ista::test::defaultOutputRoot() / "writer_temp_libs";
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  EXPECT_FALSE(ec) << "failed to create temp liberty dir: " << output_dir
                   << ", error=" << ec.message();

  const auto file_path = output_dir / file_name;
  std::ofstream output(file_path);
  output << contents;
  output.close();
  return file_path;
}

class LibertyAlignmentTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    char config[] = "test";
    char* argv[] = {config};
    Log::init(argv);
    _generated_lib = ista::test::generateGoldenCaseTimingModel(
        AnalysisMode::kMax, ista::test::goldenCaseMaxLibPath());
    _generated_text = ista::test::readFile(_generated_lib);
    _openroad_text =
        ista::test::readFile(ista::test::openroadGoldenLibPath());
  }

  static void TearDownTestSuite() {
    TimingEngine::destroyTimingEngine();
    Log::end();
  }

  static std::filesystem::path _generated_lib;
  static std::string _generated_text;
  static std::string _openroad_text;
};

std::filesystem::path LibertyAlignmentTest::_generated_lib;
std::string LibertyAlignmentTest::_generated_text;
std::string LibertyAlignmentTest::_openroad_text;

class LibertyWriterUnitTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    if (!Log::isInit()) {
      char config[] = "test";
      char* argv[] = {config};
      Log::init(argv);
      _owns_log = true;
    }
  }

  static void TearDownTestSuite() {
    if (_owns_log && Log::isInit()) {
      Log::end();
      _owns_log = false;
    }
  }

  static bool _owns_log;
};

bool LibertyWriterUnitTest::_owns_log = false;

TEST_F(LibertyAlignmentTest, no_duplicate_pin_names) {
  EXPECT_EQ(ista::test::countDuplicatePinNames(_generated_text), 0U)
      << "duplicate pin names should be zero for " << _generated_lib;
}

TEST_F(LibertyAlignmentTest, timing_blocks_have_timing_type) {
  EXPECT_EQ(ista::test::countTimingBlocksMissingTimingType(_generated_text), 0U)
      << "all timing() blocks should serialize timing_type";
}

TEST_F(LibertyAlignmentTest, exports_setup_and_hold_arcs) {
  EXPECT_GT(ista::test::countLiteral(_generated_text, "setup_rising"), 0U);
  EXPECT_GT(ista::test::countLiteral(_generated_text, "hold_rising"), 0U);
}

TEST_F(LibertyAlignmentTest, emits_library_header_fields) {
  EXPECT_GT(ista::test::countLiteral(_generated_text, "time_unit"), 0U);
  EXPECT_GT(ista::test::countLiteral(_generated_text, "capacitive_load_unit"),
            0U);
  EXPECT_GT(ista::test::countLiteral(_generated_text, "delay_model"), 0U);
}

TEST_F(LibertyAlignmentTest, writer_uses_library_unit_model_for_header_units) {
  ista::LibLibrary unit_test_lib("writer_unit_model_test");
  unit_test_lib.set_cap_unit(ista::CapacitiveUnit::kPF);
  unit_test_lib.set_time_unit(ista::TimeUnit::kFS);
  unit_test_lib.set_resistance_unit(ista::ResistanceUnit::kOHM);

  const auto output_dir = ista::test::defaultOutputRoot() / "writer_units";
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  ASSERT_FALSE(ec) << "failed to create writer unit test directory: "
                   << output_dir << ", error=" << ec.message();

  const auto output_path = output_dir / "writer_unit_model_test.lib";
  unit_test_lib.printLibertyLibrary(output_path.c_str());
  const auto liberty_text = ista::test::readFile(output_path);

  EXPECT_TRUE(ista::test::containsLiteral(liberty_text,
                                          "capacitive_load_unit (1,pF);"))
      << "expected writer to derive capacitance unit from LibLibrary";
  EXPECT_TRUE(ista::test::containsLiteral(liberty_text,
                                          "time_unit                      : \"1fs\";"))
      << "expected writer to derive time unit from LibLibrary";
  EXPECT_TRUE(ista::test::containsLiteral(
      liberty_text, "pulling_resistance_unit        : \"1ohm\";"))
      << "expected writer to derive resistance unit from LibLibrary";
}

TEST_F(LibertyAlignmentTest,
       writer_converts_internal_values_to_declared_export_units) {
  ista::LibLibrary unit_test_lib("writer_numeric_unit_conversion_test");
  unit_test_lib.set_cap_unit(ista::CapacitiveUnit::kFF);
  unit_test_lib.set_time_unit(ista::TimeUnit::kPS);

  auto lut_template =
      std::make_unique<ista::LibLutTableTemplate>("writer_time_axis_template");
  lut_template->set_template_variable1("input_net_transition");
  auto template_axis = std::make_unique<ista::LibAxis>("index_1");
  std::vector<std::unique_ptr<ista::LibAttrValue>> template_axis_values;
  template_axis_values.emplace_back(std::make_unique<ista::LibFloatValue>(0.5));
  template_axis_values.emplace_back(
      std::make_unique<ista::LibFloatValue>(1.25));
  template_axis->set_axis_values(std::move(template_axis_values));
  lut_template->addAxis(std::move(template_axis));
  auto* lut_template_ptr = lut_template.get();
  unit_test_lib.addLutTemplate(std::move(lut_template));

  auto lib_cell = std::make_unique<ista::LibCell>(
      "writer_numeric_unit_conversion_cell", &unit_test_lib);

  auto input_port = std::make_unique<ista::LibPort>("A");
  input_port->set_port_type(ista::LibPort::LibertyPortType::kInput);
  input_port->set_port_cap(1.5);
  input_port->set_ower_cell(lib_cell.get());
  lib_cell->addLibertyPort(std::move(input_port));

  auto output_port = std::make_unique<ista::LibPort>("Y");
  output_port->set_port_type(ista::LibPort::LibertyPortType::kOutput);
  output_port->set_ower_cell(lib_cell.get());
  lib_cell->addLibertyPort(std::move(output_port));

  auto lib_arc = std::make_unique<ista::LibArc>();
  lib_arc->set_src_port("A");
  lib_arc->set_snk_port("Y");
  lib_arc->set_timing_type("combinational");
  lib_arc->set_timing_sense("positive_unate");
  lib_arc->set_owner_cell(lib_cell.get());

  auto delay_model = std::make_unique<ista::LibDelayTableModel>();
  auto delay_table = std::make_unique<ista::LibTable>(
      ista::LibTable::TableType::kCellRise, lut_template_ptr);
  delay_table->addTableValue(std::make_unique<ista::LibFloatValue>(0.125));
  delay_table->addTableValue(std::make_unique<ista::LibFloatValue>(0.25));
  delay_model->addTable(std::move(delay_table));
  lib_arc->set_table_model(std::move(delay_model));
  lib_cell->addLibertyArc(std::move(lib_arc));
  unit_test_lib.addLibertyCell(std::move(lib_cell));

  const auto output_dir =
      ista::test::defaultOutputRoot() / "writer_numeric_units";
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  ASSERT_FALSE(ec) << "failed to create writer numeric unit test directory: "
                   << output_dir << ", error=" << ec.message();

  const auto output_path =
      output_dir / "writer_numeric_unit_conversion_test.lib";
  unit_test_lib.printLibertyLibrary(output_path.c_str());
  const auto liberty_text = ista::test::readFile(output_path);

  const auto input_pin_block = extractPinBlock(liberty_text, "A");
  ASSERT_FALSE(input_pin_block.empty()) << "expected input pin block";
  EXPECT_NE(input_pin_block.find("capacitance             : 1500.00000000;"),
            std::string::npos)
      << "expected writer to convert internal pF pin capacitance to exported "
         "fF values";
  EXPECT_GT(ista::test::countRegexMatches(
                liberty_text,
                std::regex(R"(index_1\(\"500\.00000000,\s*1250\.00000000\"\))")),
            0U)
      << "expected writer to convert internal ns time axis values to exported "
         "ps values";
  EXPECT_GT(ista::test::countRegexMatches(
                liberty_text,
                std::regex(R"(values\s*\(\"125\.00000000,250\.00000000\"\))")),
            0U)
      << "expected writer to convert internal ns table values to exported ps "
         "values";
}

TEST_F(LibertyWriterUnitTest, writer_emits_all_arcs_from_same_lib_arc_set) {
  ista::LibLibrary lib("writer_multi_arc_export_test");

  auto lib_cell = std::make_unique<ista::LibCell>("multi_arc_cell", &lib);

  auto input_port = std::make_unique<ista::LibPort>("A");
  input_port->set_port_type(ista::LibPort::LibertyPortType::kInput);
  input_port->set_ower_cell(lib_cell.get());
  lib_cell->addLibertyPort(std::move(input_port));

  auto output_port = std::make_unique<ista::LibPort>("Y");
  output_port->set_port_type(ista::LibPort::LibertyPortType::kOutput);
  output_port->set_ower_cell(lib_cell.get());
  lib_cell->addLibertyPort(std::move(output_port));

  lib_cell->addLibertyArc(
      makeScalarDelayArc(lib_cell.get(), "A", "Y", "positive_unate", 0.10));
  lib_cell->addLibertyArc(
      makeScalarDelayArc(lib_cell.get(), "A", "Y", "negative_unate", 0.20));
  lib.addLibertyCell(std::move(lib_cell));

  const auto output_dir =
      ista::test::defaultOutputRoot() / "writer_multi_arc_export";
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  ASSERT_FALSE(ec) << "failed to create writer multi arc test directory: "
                   << output_dir << ", error=" << ec.message();

  const auto output_path = output_dir / "writer_multi_arc_export_test.lib";
  lib.printLibertyLibrary(output_path.c_str());
  const auto liberty_text = ista::test::readFile(output_path);
  const auto output_pin_block = extractPinBlock(liberty_text, "Y");
  ASSERT_FALSE(output_pin_block.empty()) << "expected output pin block";

  EXPECT_EQ(ista::test::countLiteral(output_pin_block, "timing ()"), 2U)
      << "expected every Liberty arc in the arc set to be serialized";
  EXPECT_NE(output_pin_block.find("timing_sense        : positive_unate;"),
            std::string::npos);
  EXPECT_NE(output_pin_block.find("timing_sense        : negative_unate;"),
            std::string::npos);
}

TEST_F(LibertyWriterUnitTest,
       writer_preserves_when_conditions_on_exported_timing_arcs) {
  ista::LibLibrary lib("writer_when_export_test");

  auto lib_cell = std::make_unique<ista::LibCell>("when_arc_cell", &lib);

  auto input_port = std::make_unique<ista::LibPort>("A");
  input_port->set_port_type(ista::LibPort::LibertyPortType::kInput);
  input_port->set_ower_cell(lib_cell.get());
  lib_cell->addLibertyPort(std::move(input_port));

  auto output_port = std::make_unique<ista::LibPort>("Y");
  output_port->set_port_type(ista::LibPort::LibertyPortType::kOutput);
  output_port->set_ower_cell(lib_cell.get());
  lib_cell->addLibertyPort(std::move(output_port));

  lib_cell->addLibertyArc(makeScalarDelayArc(lib_cell.get(), "A", "Y",
                                             "positive_unate", 0.10,
                                             "ENABLE"));
  lib.addLibertyCell(std::move(lib_cell));

  const auto output_dir = ista::test::defaultOutputRoot() / "writer_when_export";
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  ASSERT_FALSE(ec) << "failed to create writer when test directory: "
                   << output_dir << ", error=" << ec.message();

  const auto output_path = output_dir / "writer_when_export_test.lib";
  lib.printLibertyLibrary(output_path.c_str());
  const auto liberty_text = ista::test::readFile(output_path);
  const auto output_pin_block = extractPinBlock(liberty_text, "Y");
  ASSERT_FALSE(output_pin_block.empty()) << "expected output pin block";

  EXPECT_NE(output_pin_block.find("when"), std::string::npos)
      << "expected exported timing block to preserve Liberty when clauses";
  EXPECT_NE(output_pin_block.find("\"ENABLE\""), std::string::npos)
      << "expected exported timing block to keep the original when condition";
}

TEST_F(LibertyWriterUnitTest,
       writer_round_trips_parsed_time_axes_and_values_without_rescaling) {
  const auto input_path = writeTempLibertyFile(
      "writer_round_trip_source.lib",
      R"(library (round_trip_ps_lib) {
  time_unit : "1ps";
  capacitive_load_unit (1,pf);
  delay_model : table_lookup;
  lu_table_template(delay_template_1d) {
    variable_1 : input_net_transition;
    index_1("2.00000000,3.50000000");
  }
  cell (round_trip_cell) {
    pin ("A") {
      direction : input;
      capacitance : 0.00100000;
    }
    pin ("Y") {
      direction : output;
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        timing_type : combinational;
        when : "ENABLE";
        cell_rise(delay_template_1d) {
          values ("4.00000000,5.50000000");
        }
      }
    }
  }
})");

  ista::Lib lib;
  auto liberty_reader = lib.loadLibertyWithCppParser(input_path.c_str());
  ASSERT_EQ(liberty_reader.linkLib(), 1U);
  auto lib_library = liberty_reader.get_library_builder()->takeLib();
  ASSERT_NE(lib_library, nullptr);

  const auto output_dir =
      ista::test::defaultOutputRoot() / "writer_round_trip_source";
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  ASSERT_FALSE(ec) << "failed to create writer round-trip directory: "
                   << output_dir << ", error=" << ec.message();

  const auto output_path = output_dir / "writer_round_trip_source_export.lib";
  lib_library->printLibertyLibrary(output_path.c_str());
  const auto exported_text = ista::test::readFile(output_path);
  const auto output_pin_block = extractPinBlock(exported_text, "Y");
  ASSERT_FALSE(output_pin_block.empty()) << "expected output pin block";

  EXPECT_NE(output_pin_block.find("\"ENABLE\""), std::string::npos)
      << "expected round-tripped liberty to preserve timing when clauses";
  EXPECT_GT(ista::test::countRegexMatches(
                exported_text,
                std::regex(R"(index_1\(\"2\.00000000,\s*3\.50000000\"\))")),
            0U)
      << "expected parsed time axes to remain in their original liberty units";
  EXPECT_GT(ista::test::countRegexMatches(
                exported_text,
                std::regex(R"(values\s*\(\"4\.00000000,5\.50000000\"\))")),
            0U)
      << "expected parsed timing table values to remain in their original "
         "liberty units";
  EXPECT_EQ(ista::test::countRegexMatches(
                exported_text,
                std::regex(R"(index_1\(\"2000\.00000000,\s*3500\.00000000\"\))")),
            0U);
  EXPECT_EQ(ista::test::countRegexMatches(
                exported_text,
                std::regex(R"(values\s*\(\"4000\.00000000,5500\.00000000\"\))")),
            0U);
}

TEST_F(LibertyWriterUnitTest, writer_round_trips_parsed_clock_pin_flags) {
  const auto input_path = writeTempLibertyFile(
      "writer_round_trip_clock_pin.lib",
      R"(library (round_trip_clock_pin_lib) {
  time_unit : "1ns";
  capacitive_load_unit (1,pf);
  delay_model : table_lookup;
  cell (round_trip_clock_pin_cell) {
    pin ("CLK") {
      direction : input;
      clock : true;
      capacitance : 0.00100000;
    }
    pin ("Y") {
      direction : output;
      function : "CLK";
    }
  }
})");

  ista::Lib lib;
  auto liberty_reader = lib.loadLibertyWithCppParser(input_path.c_str());
  ASSERT_EQ(liberty_reader.linkLib(), 1U);
  auto lib_library = liberty_reader.get_library_builder()->takeLib();
  ASSERT_NE(lib_library, nullptr);

  const auto output_dir =
      ista::test::defaultOutputRoot() / "writer_round_trip_clock_pin";
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  ASSERT_FALSE(ec) << "failed to create writer clock-pin round-trip dir: "
                   << output_dir << ", error=" << ec.message();

  const auto output_path =
      output_dir / "writer_round_trip_clock_pin_export.lib";
  lib_library->printLibertyLibrary(output_path.c_str());
  const auto exported_text = ista::test::readFile(output_path);
  const auto clock_pin_block = extractPinBlock(exported_text, "CLK");
  ASSERT_FALSE(clock_pin_block.empty()) << "expected CLK pin block";

  EXPECT_NE(clock_pin_block.find("clock                   : true;"),
            std::string::npos)
      << "expected round-tripped parsed clock pins to keep clock : true";
}

TEST_F(LibertyWriterUnitTest,
       cpp_parser_preserves_pin_function_expressions_in_memory) {
  const auto input_path = writeTempLibertyFile(
      "writer_round_trip_pin_function.lib",
      R"(library (round_trip_function_lib) {
  time_unit : "1ns";
  capacitive_load_unit (1,pf);
  delay_model : table_lookup;
  cell (round_trip_function_cell) {
    pin ("A") {
      direction : input;
      capacitance : 0.00100000;
    }
    pin ("Y") {
      direction : output;
      function : "A";
    }
  }
})");

  ista::Lib lib;
  auto liberty_reader = lib.loadLibertyWithCppParser(input_path.c_str());
  ASSERT_EQ(liberty_reader.linkLib(), 1U);
  auto lib_library = liberty_reader.get_library_builder()->takeLib();
  ASSERT_NE(lib_library, nullptr);

  ASSERT_EQ(lib_library->get_cells().size(), 1U);
  auto* lib_cell = lib_library->get_cells().front().get();
  ASSERT_NE(lib_cell, nullptr);
  auto* output_port = lib_cell->get_cell_port_or_port_bus("Y");
  ASSERT_NE(output_port, nullptr);

  EXPECT_NE(output_port->get_func_expr(), nullptr);
  EXPECT_EQ(output_port->get_func_expr_str(), "A");
}

TEST_F(LibertyWriterUnitTest,
       writer_returns_cleanly_when_output_file_cannot_be_opened) {
  const auto output_path = ista::test::defaultOutputRoot() /
                           "writer_open_failure" / "missing_parent" /
                           "writer_open_failure_test.lib";
  std::error_code ec;
  std::filesystem::remove_all(output_path.parent_path().parent_path(), ec);
  ASSERT_FALSE(ec) << "failed to clean writer open failure test directory: "
                   << output_path.parent_path().parent_path()
                   << ", error=" << ec.message();
  ASSERT_FALSE(std::filesystem::exists(output_path.parent_path()));

  const auto output_path_str = output_path.string();
  EXPECT_EXIT(
      {
        if (!Log::isInit()) {
          char config[] = "test";
          char* argv[] = {config};
          Log::init(argv);
        }
        ista::LibLibrary lib("writer_open_failure_test");
        lib.printLibertyLibrary(output_path_str.c_str());
        std::_Exit(0);
      },
      ::testing::ExitedWithCode(0), "");

  EXPECT_FALSE(std::filesystem::exists(output_path));
}

TEST_F(LibertyWriterUnitTest,
       writer_preserves_distinct_bus_types_for_different_widths) {
  ista::LibLibrary lib("writer_bus_type_width_test");

  auto add_cell_with_bus = [&lib](const char* cell_name, int high_bit) {
    auto lib_cell = std::make_unique<ista::LibCell>(cell_name, &lib);

    for (int bit = 0; bit <= high_bit; ++bit) {
      auto port = std::make_unique<ista::LibPort>(
          ("A[" + std::to_string(bit) + "]").c_str());
      port->set_port_type(ista::LibPort::LibertyPortType::kInput);
      port->set_ower_cell(lib_cell.get());
      lib_cell->addLibertyPort(std::move(port));
    }

    auto output_port = std::make_unique<ista::LibPort>("Y");
    output_port->set_port_type(ista::LibPort::LibertyPortType::kOutput);
    output_port->set_ower_cell(lib_cell.get());
    lib_cell->addLibertyPort(std::move(output_port));

    lib.addLibertyCell(std::move(lib_cell));
  };

  add_cell_with_bus("bus2_cell", 1);
  add_cell_with_bus("bus8_cell", 7);

  const auto output_dir =
      ista::test::defaultOutputRoot() / "writer_bus_type_widths";
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  ASSERT_FALSE(ec) << "failed to create writer bus type test directory: "
                   << output_dir << ", error=" << ec.message();

  const auto output_path = output_dir / "writer_bus_type_width_test.lib";
  lib.printLibertyLibrary(output_path.c_str());
  const auto liberty_text = ista::test::readFile(output_path);
  const auto bus_type_refs = extractBusTypeReferences(liberty_text);

  EXPECT_TRUE(ista::test::containsLiteral(liberty_text, "bit_width : 2;"));
  EXPECT_TRUE(ista::test::containsLiteral(liberty_text, "bit_width : 8;"));
  EXPECT_EQ(bus_type_refs.size(), 2U)
      << "expected different-width buses to reference distinct bus types";
}

TEST_F(LibertyWriterUnitTest,
       writer_emits_all_axis_indices_for_lut_templates) {
  ista::LibLibrary lib("writer_high_dim_template_test");

  auto lut_template =
      std::make_unique<ista::LibLutTableTemplate>("writer_4d_template");
  lut_template->set_template_variable1("input_net_transition");
  lut_template->set_template_variable2("total_output_net_capacitance");
  lut_template->set_template_variable3("time");
  lut_template->set_template_variable4("output_voltage");

  auto add_axis = [&](const char* axis_name, std::initializer_list<double> values) {
    auto axis = std::make_unique<ista::LibAxis>(axis_name);
    std::vector<std::unique_ptr<ista::LibAttrValue>> axis_values;
    for (double value : values) {
      axis_values.emplace_back(std::make_unique<ista::LibFloatValue>(value));
    }
    axis->set_axis_values(std::move(axis_values));
    lut_template->addAxis(std::move(axis));
  };

  add_axis("index_1", {0.1, 0.2});
  add_axis("index_2", {0.3, 0.4});
  add_axis("index_3", {0.5, 0.6});
  add_axis("index_4", {0.7, 0.8});
  lib.addLutTemplate(std::move(lut_template));

  const auto output_dir =
      ista::test::defaultOutputRoot() / "writer_high_dim_template";
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  ASSERT_FALSE(ec) << "failed to create writer high-dim template dir: "
                   << output_dir << ", error=" << ec.message();

  const auto output_path = output_dir / "writer_high_dim_template_test.lib";
  lib.printLibertyLibrary(output_path.c_str());
  const auto liberty_text = ista::test::readFile(output_path);

  EXPECT_NE(liberty_text.find("index_3(\"0.50000000, 0.60000000\");"),
            std::string::npos);
  EXPECT_NE(liberty_text.find("index_4(\"0.70000000, 0.80000000\");"),
            std::string::npos);
}

TEST_F(LibertyWriterUnitTest, writer_emits_recovery_and_removal_checks) {
  ista::LibLibrary lib("writer_async_check_export_test");

  auto lib_cell = std::make_unique<ista::LibCell>("async_check_cell", &lib);

  auto clock_port = std::make_unique<ista::LibPort>("CLK");
  clock_port->set_port_type(ista::LibPort::LibertyPortType::kInput);
  clock_port->set_is_clock(true);
  clock_port->set_ower_cell(lib_cell.get());
  lib_cell->addLibertyPort(std::move(clock_port));

  auto clear_port = std::make_unique<ista::LibPort>("RN");
  clear_port->set_port_type(ista::LibPort::LibertyPortType::kInput);
  clear_port->set_ower_cell(lib_cell.get());
  lib_cell->addLibertyPort(std::move(clear_port));

  lib_cell->addLibertyArc(
      makeScalarCheckArc(lib_cell.get(), "CLK", "RN", "recovery_rising", 0.11,
                         0.12));
  lib_cell->addLibertyArc(
      makeScalarCheckArc(lib_cell.get(), "CLK", "RN", "removal_rising", 0.21,
                         0.22));
  lib.addLibertyCell(std::move(lib_cell));

  const auto output_dir =
      ista::test::defaultOutputRoot() / "writer_async_check_export";
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  ASSERT_FALSE(ec) << "failed to create writer async check test directory: "
                   << output_dir << ", error=" << ec.message();

  const auto output_path = output_dir / "writer_async_check_export_test.lib";
  lib.printLibertyLibrary(output_path.c_str());
  const auto liberty_text = ista::test::readFile(output_path);
  const auto clear_pin_block = extractPinBlock(liberty_text, "RN");
  ASSERT_FALSE(clear_pin_block.empty()) << "expected async pin block";

  EXPECT_NE(clear_pin_block.find("timing_type        : recovery_rising;"),
            std::string::npos);
  EXPECT_NE(clear_pin_block.find("timing_type        : removal_rising;"),
            std::string::npos);
}

TEST_F(LibertyAlignmentTest, preserves_reference_library_header_metadata) {
  EXPECT_TRUE(ista::test::containsLiteral(_generated_text, "comment"))
      << "expected generated Liberty header to preserve source comment metadata";
  EXPECT_TRUE(ista::test::containsLiteral(_generated_text, "simulation"))
      << "expected generated Liberty header to preserve source simulation metadata";
  EXPECT_TRUE(
      ista::test::containsLiteral(_generated_text, "leakage_power_unit"))
      << "expected generated Liberty header to preserve source leakage power unit";
  EXPECT_TRUE(ista::test::containsLiteral(_generated_text, "current_unit"))
      << "expected generated Liberty header to preserve source current unit";
  EXPECT_TRUE(ista::test::containsLiteral(_generated_text, "voltage_unit"))
      << "expected generated Liberty header to preserve source voltage unit";
  EXPECT_TRUE(ista::test::containsLiteral(_generated_text, "library_features"))
      << "expected generated Liberty header to preserve source library features";
  EXPECT_TRUE(ista::test::containsLiteral(_generated_text, "nom_process"))
      << "expected generated Liberty header to preserve source nominal process";
  EXPECT_TRUE(ista::test::containsLiteral(_generated_text, "nom_temperature"))
      << "expected generated Liberty header to preserve source nominal temperature";
}

TEST_F(LibertyAlignmentTest, preserves_output_fall_threshold_percent) {
  EXPECT_GT(ista::test::countRegexMatches(
                _generated_text,
                std::regex(R"(output_threshold_pct_fall\s*:\s*50\b)")),
            0U)
      << "expected output_threshold_pct_fall to remain 50% in generated "
         "Liberty header";
}

TEST_F(LibertyAlignmentTest, emits_bus_and_type_definitions) {
  EXPECT_GT(ista::test::countLiteral(_generated_text, "bus(\""), 0U);
  EXPECT_GT(ista::test::countLiteral(_generated_text, "bus_type"), 0U);
  EXPECT_GT(ista::test::countLiteral(_generated_text, "type (\""), 0U);
}

TEST_F(LibertyAlignmentTest, emits_macro_and_clock_metadata) {
  EXPECT_TRUE(
      ista::test::containsLiteral(_generated_text, "is_macro_cell : true"));
  EXPECT_GT(ista::test::countRegexMatches(
                _generated_text, std::regex(R"(clock\s*:\s*true)")),
            0U);
}

TEST_F(LibertyAlignmentTest, emits_power_and_ground_pins_from_design_context) {
  EXPECT_TRUE(ista::test::containsLiteral(_generated_text, "pin(\"VDD\")"))
      << "expected exported Liberty to retain VDD block pin";
  EXPECT_TRUE(ista::test::containsLiteral(_generated_text, "pin(\"VSS\")"))
      << "expected exported Liberty to retain VSS block pin";
}

TEST_F(LibertyAlignmentTest,
       power_and_ground_pin_directions_match_openroad_reference) {
  for (const char* pin_name : {"VDD", "VSS"}) {
    const auto openroad_pin_block = extractPinBlock(_openroad_text, pin_name);
    ASSERT_FALSE(openroad_pin_block.empty())
        << "expected OpenROAD pin block for " << pin_name;
    const auto generated_pin_block = extractPinBlock(_generated_text, pin_name);
    ASSERT_FALSE(generated_pin_block.empty())
        << "expected generated pin block for " << pin_name;

    const auto openroad_direction = extractPinDirection(openroad_pin_block);
    const auto generated_direction = extractPinDirection(generated_pin_block);
    ASSERT_TRUE(openroad_direction.has_value())
        << "expected OpenROAD direction for " << pin_name;
    ASSERT_TRUE(generated_direction.has_value())
        << "expected generated direction for " << pin_name;

    EXPECT_EQ(*generated_direction, *openroad_direction)
        << "expected generated PG pin direction to match OpenROAD for "
        << pin_name;
  }
}

TEST_F(LibertyAlignmentTest, emits_clock_tree_path_arcs) {
  EXPECT_GT(ista::test::countLiteral(_generated_text, "max_clock_tree_path"),
            0U);
  EXPECT_GT(ista::test::countLiteral(_generated_text, "min_clock_tree_path"),
            0U);
}

TEST_F(LibertyAlignmentTest, emits_lut_templates_and_indices) {
  EXPECT_GT(ista::test::countLiteral(_generated_text, "lu_table_template"), 0U);
  EXPECT_GT(ista::test::countLiteral(_generated_text, "index_1"), 0U);
}

TEST_F(LibertyAlignmentTest, does_not_fall_back_to_timing_cluster_tables) {
  EXPECT_EQ(ista::test::countLiteral(_generated_text, "timing_cluster"), 0U);
}

TEST_F(LibertyAlignmentTest, emits_pin_capacitance_attributes) {
  EXPECT_GT(
      ista::test::countRegexMatches(_generated_text,
                                    std::regex(R"(capacitance\s*:\s*[0-9eE+.\-]+)")),
      0U);
}

TEST_F(LibertyAlignmentTest, emits_port_checks_for_csb2cmac_a_req_pvld) {
  const auto pin_block = extractPinBlock(_generated_text, "csb2cmac_a_req_pvld");
  ASSERT_FALSE(pin_block.empty()) << "expected csb2cmac_a_req_pvld pin block";
  EXPECT_NE(pin_block.find("setup_rising"), std::string::npos)
      << "expected setup_rising on csb2cmac_a_req_pvld";
  EXPECT_NE(pin_block.find("hold_rising"), std::string::npos)
      << "expected hold_rising on csb2cmac_a_req_pvld";
}

TEST_F(LibertyAlignmentTest,
       compat_control_inputs_match_openroad_setup_hold_presence) {
  for (const char* pin_name : {"direct_reset_", "dla_reset_rstn", "test_mode"}) {
    const auto openroad_pin_block = extractPinBlock(_openroad_text, pin_name);
    ASSERT_FALSE(openroad_pin_block.empty())
        << "expected OpenROAD pin block for " << pin_name;
    const auto generated_pin_block = extractPinBlock(_generated_text, pin_name);
    ASSERT_FALSE(generated_pin_block.empty())
        << "expected generated pin block for " << pin_name;

    const bool openroad_has_setup_rising =
        openroad_pin_block.find("setup_rising") != std::string::npos;
    const bool openroad_has_hold_rising =
        openroad_pin_block.find("hold_rising") != std::string::npos;
    const bool openroad_has_setup_falling =
        openroad_pin_block.find("setup_falling") != std::string::npos;
    const bool openroad_has_hold_falling =
        openroad_pin_block.find("hold_falling") != std::string::npos;

    EXPECT_EQ(generated_pin_block.find("setup_rising") != std::string::npos,
              openroad_has_setup_rising)
        << "generated setup_rising presence should match OpenROAD for "
        << pin_name;
    EXPECT_EQ(generated_pin_block.find("hold_rising") != std::string::npos,
              openroad_has_hold_rising)
        << "generated hold_rising presence should match OpenROAD for "
        << pin_name;
    EXPECT_EQ(generated_pin_block.find("setup_falling") != std::string::npos,
              openroad_has_setup_falling)
        << "generated setup_falling presence should match OpenROAD for "
        << pin_name;
    EXPECT_EQ(generated_pin_block.find("hold_falling") != std::string::npos,
              openroad_has_hold_falling)
        << "generated hold_falling presence should match OpenROAD for "
        << pin_name;
  }
}

TEST_F(LibertyAlignmentTest, clocked_outputs_use_top_level_clock_related_pin) {
  for (const char* pin_name : {"cmac_a2csb_resp_valid", "mac2accu_data0[0]"}) {
    const auto pin_block = extractPinBlock(_generated_text, pin_name);
    ASSERT_FALSE(pin_block.empty()) << "expected " << pin_name << " pin block";
    EXPECT_NE(pin_block.find("timing_type        : rising_edge;"),
              std::string::npos)
        << "expected clocked output pin " << pin_name
        << " to keep rising_edge timing_type";
    EXPECT_NE(pin_block.find("related_pin        : \"nvdla_core_clk\";"),
              std::string::npos)
        << "expected clocked output pin " << pin_name
        << " to reference the top-level clock pin";
    EXPECT_EQ(pin_block.find(":CLK"), std::string::npos)
        << "did not expect internal sequential clock pins to leak into "
           "related_pin for "
        << pin_name;
  }
}

TEST_F(LibertyAlignmentTest,
       rising_edge_outputs_do_not_fall_back_to_scalar_fall_tables) {
  const auto pin_block = extractPinBlock(_generated_text, "cmac_a2csb_resp_valid");
  ASSERT_FALSE(pin_block.empty()) << "expected cmac_a2csb_resp_valid pin block";
  const auto timing_block =
      extractTimingBlock(pin_block, "nvdla_core_clk", "rising_edge");
  ASSERT_FALSE(timing_block.empty())
      << "expected rising_edge timing block for cmac_a2csb_resp_valid";
  EXPECT_GT(ista::test::countRegexMatches(
                timing_block,
                std::regex(R"(cell_fall\s*\(\s*(?!scalar\b)[^)]+\))")),
            0U)
      << "expected non-scalar cell_fall table on cmac_a2csb_resp_valid";
  EXPECT_GT(ista::test::countRegexMatches(
                timing_block,
                std::regex(R"(fall_transition\s*\(\s*(?!scalar\b)[^)]+\))")),
            0U)
      << "expected non-scalar fall_transition table on cmac_a2csb_resp_valid";
  EXPECT_EQ(ista::test::countRegexMatches(
                timing_block, std::regex(R"(cell_fall\s*\(\s*scalar\s*\))")),
            0U)
      << "did not expect scalar cell_fall fallback on cmac_a2csb_resp_valid";
  EXPECT_EQ(ista::test::countRegexMatches(
                timing_block,
                std::regex(R"(fall_transition\s*\(\s*scalar\s*\))")),
            0U)
      << "did not expect scalar fall_transition fallback on cmac_a2csb_resp_valid";
}

TEST_F(LibertyAlignmentTest,
       setup_hold_constraint_values_track_openroad_reference_scale) {
  const auto openroad_pin_block = extractPinBlock(_openroad_text, "sc2mac_dat_pd[7]");
  ASSERT_FALSE(openroad_pin_block.empty())
      << "expected OpenROAD pin block for sc2mac_dat_pd[7]";
  const auto generated_pin_block = extractPinBlock(_generated_text, "sc2mac_dat_pd[7]");
  ASSERT_FALSE(generated_pin_block.empty())
      << "expected generated pin block for sc2mac_dat_pd[7]";

  const auto openroad_hold_rise = extractTimingScalarValue(
      openroad_pin_block, "nvdla_core_clk", "hold_rising", "rise_constraint");
  const auto generated_hold_rise = extractTimingScalarValue(
      generated_pin_block, "nvdla_core_clk", "hold_rising", "rise_constraint");
  const auto openroad_hold_fall = extractTimingScalarValue(
      openroad_pin_block, "nvdla_core_clk", "hold_rising", "fall_constraint");
  const auto generated_hold_fall = extractTimingScalarValue(
      generated_pin_block, "nvdla_core_clk", "hold_rising", "fall_constraint");
  const auto openroad_setup_rise = extractTimingScalarValue(
      openroad_pin_block, "nvdla_core_clk", "setup_rising", "rise_constraint");
  const auto generated_setup_rise = extractTimingScalarValue(
      generated_pin_block, "nvdla_core_clk", "setup_rising", "rise_constraint");
  const auto openroad_setup_fall = extractTimingScalarValue(
      openroad_pin_block, "nvdla_core_clk", "setup_rising", "fall_constraint");
  const auto generated_setup_fall = extractTimingScalarValue(
      generated_pin_block, "nvdla_core_clk", "setup_rising", "fall_constraint");

  ASSERT_TRUE(openroad_hold_rise.has_value());
  ASSERT_TRUE(generated_hold_rise.has_value());
  ASSERT_TRUE(openroad_hold_fall.has_value());
  ASSERT_TRUE(generated_hold_fall.has_value());
  ASSERT_TRUE(openroad_setup_rise.has_value());
  ASSERT_TRUE(generated_setup_rise.has_value());
  ASSERT_TRUE(openroad_setup_fall.has_value());
  ASSERT_TRUE(generated_setup_fall.has_value());

  EXPECT_NEAR(*generated_hold_rise, *openroad_hold_rise,
              std::max(5.0, std::abs(*openroad_hold_rise) * 0.20));
  EXPECT_NEAR(*generated_hold_fall, *openroad_hold_fall,
              std::max(5.0, std::abs(*openroad_hold_fall) * 0.20));
  EXPECT_NEAR(*generated_setup_rise, *openroad_setup_rise,
              std::max(5.0, std::abs(*openroad_setup_rise) * 0.20));
  EXPECT_NEAR(*generated_setup_fall, *openroad_setup_fall,
              std::max(5.0, std::abs(*openroad_setup_fall) * 0.20));
}

TEST_F(LibertyAlignmentTest,
       setup_hold_constraint_sources_are_dumped_for_all_scalar_entries) {
  const auto source_path = ista::test::goldenCaseMaxCheckSourcePath();
  ASSERT_TRUE(std::filesystem::exists(source_path))
      << "expected check-source sidecar at " << source_path;

  const auto source_text = ista::test::readFile(source_path);
  EXPECT_TRUE(ista::test::containsLiteral(
      source_text,
      "port\trelated_pin\ttiming_type\tinput_trans\tconstraint_ns\t"
      "source_tag\tcheck_margin_source\tlocal_seq_match_status\t"
      "local_seq_binding_status\tpreserved_seq_match_status\t"
      "exported_check_pair_binding_status"));

  const std::regex kSc2macSourceLine(
      R"(sc2mac_dat_pd\[7\]\s+nvdla_core_clk\s+(setup_rising|hold_rising)\s+(rise|fall)\s+[^\s]+\s+[^\s]+\s+[^\s]+\s+[^\s]+\s+[^\s]+\s+[^\s]+\s+[^\s]+)");
  EXPECT_EQ(ista::test::countRegexMatches(source_text, kSc2macSourceLine), 4U)
      << "expected setup/hold rise/fall source rows for sc2mac_dat_pd[7]";
}

TEST_F(LibertyAlignmentTest,
       full_sta_preserved_seq_data_covers_all_sc2mac_port_clock_pairs) {
  const auto source_path = ista::test::goldenCaseMaxCheckSourcePath();
  ASSERT_TRUE(std::filesystem::exists(source_path))
      << "expected check-source sidecar at " << source_path;

  const auto source_text = ista::test::readFile(source_path);
  const std::regex kMissingPreservedSeqRow(
      R"(sc2mac_dat_pd\[7\]\s+nvdla_core_clk\s+(setup_rising|hold_rising)\s+(rise|fall)\s+[^\s]+\s+[^\s]+\s+[^\s]+\s+[^\s]+\s+[^\s]+\s+missing\s+[^\s]+)");
  EXPECT_EQ(ista::test::countRegexMatches(source_text, kMissingPreservedSeqRow),
            0U)
      << "expected full-STA preserved seq data to cover all exported "
         "sc2mac_dat_pd[7] port-clock scalar entries";
}

TEST_F(LibertyAlignmentTest,
       rising_edge_output_delay_values_track_openroad_reference_scale) {
  const auto openroad_pin_block =
      extractPinBlock(_openroad_text, "cmac_a2csb_resp_valid");
  ASSERT_FALSE(openroad_pin_block.empty())
      << "expected OpenROAD pin block for cmac_a2csb_resp_valid";
  const auto generated_pin_block =
      extractPinBlock(_generated_text, "cmac_a2csb_resp_valid");
  ASSERT_FALSE(generated_pin_block.empty())
      << "expected generated pin block for cmac_a2csb_resp_valid";

  const auto openroad_first_cell_rise =
      extractFirstTableValue(openroad_pin_block, "cell_rise");
  const auto generated_first_cell_rise =
      extractFirstTableValue(generated_pin_block, "cell_rise");
  const auto openroad_first_rise_transition =
      extractFirstTableValue(openroad_pin_block, "rise_transition");
  const auto generated_first_rise_transition =
      extractFirstTableValue(generated_pin_block, "rise_transition");
  ASSERT_TRUE(openroad_first_cell_rise.has_value())
      << "expected OpenROAD cell_rise values for cmac_a2csb_resp_valid";
  ASSERT_TRUE(generated_first_cell_rise.has_value())
      << "expected generated cell_rise values for cmac_a2csb_resp_valid";
  ASSERT_TRUE(openroad_first_rise_transition.has_value())
      << "expected OpenROAD rise_transition values for cmac_a2csb_resp_valid";
  ASSERT_TRUE(generated_first_rise_transition.has_value())
      << "expected generated rise_transition values for cmac_a2csb_resp_valid";

  EXPECT_NEAR(*generated_first_cell_rise, *openroad_first_cell_rise,
              std::max(5.0, *openroad_first_cell_rise * 0.10))
      << "expected generated first cell_rise sample to stay on the same "
         "boundary-delay scale as OpenROAD";
  EXPECT_NEAR(*generated_first_rise_transition, *openroad_first_rise_transition,
              std::max(5.0, *openroad_first_rise_transition * 0.10))
      << "expected generated first rise_transition sample to stay on the same "
         "boundary-slew scale as OpenROAD";
}

TEST_F(LibertyAlignmentTest,
       rising_edge_clk2out_tables_track_openroad_within_same_timing_block) {
  const auto openroad_pin_block =
      extractPinBlock(_openroad_text, "cmac_a2csb_resp_valid");
  ASSERT_FALSE(openroad_pin_block.empty())
      << "expected OpenROAD pin block for cmac_a2csb_resp_valid";
  const auto generated_pin_block =
      extractPinBlock(_generated_text, "cmac_a2csb_resp_valid");
  ASSERT_FALSE(generated_pin_block.empty())
      << "expected generated pin block for cmac_a2csb_resp_valid";

  const auto openroad_cell_rise = extractTimingTableFirstValue(
      openroad_pin_block, "nvdla_core_clk", "rising_edge", "cell_rise");
  const auto generated_cell_rise = extractTimingTableFirstValue(
      generated_pin_block, "nvdla_core_clk", "rising_edge", "cell_rise");
  const auto openroad_rise_transition = extractTimingTableFirstValue(
      openroad_pin_block, "nvdla_core_clk", "rising_edge", "rise_transition");
  const auto generated_rise_transition =
      extractTimingTableFirstValue(generated_pin_block, "nvdla_core_clk",
                                   "rising_edge", "rise_transition");

  ASSERT_TRUE(openroad_cell_rise.has_value());
  ASSERT_TRUE(generated_cell_rise.has_value());
  ASSERT_TRUE(openroad_rise_transition.has_value());
  ASSERT_TRUE(generated_rise_transition.has_value());

  EXPECT_NEAR(*generated_cell_rise, *openroad_cell_rise,
              std::max(5.0, *openroad_cell_rise * 0.10));
  EXPECT_NEAR(*generated_rise_transition, *openroad_rise_transition,
              std::max(5.0, *openroad_rise_transition * 0.10));
}

TEST_F(LibertyAlignmentTest, input_pin_capacitance_tracks_openroad_reference) {
  const auto openroad_pin_block =
      extractPinBlock(_openroad_text, "csb2cmac_a_req_pvld");
  ASSERT_FALSE(openroad_pin_block.empty())
      << "expected OpenROAD pin block for csb2cmac_a_req_pvld";
  const auto generated_pin_block =
      extractPinBlock(_generated_text, "csb2cmac_a_req_pvld");
  ASSERT_FALSE(generated_pin_block.empty())
      << "expected generated pin block for csb2cmac_a_req_pvld";

  const auto openroad_cap = extractPinCapacitance(openroad_pin_block);
  const auto generated_cap = extractPinCapacitance(generated_pin_block);
  ASSERT_TRUE(openroad_cap.has_value())
      << "expected OpenROAD capacitance for csb2cmac_a_req_pvld";
  ASSERT_TRUE(generated_cap.has_value())
      << "expected generated capacitance for csb2cmac_a_req_pvld";

  EXPECT_NEAR(*generated_cap, *openroad_cap, std::max(0.5, *openroad_cap * 0.10))
      << "expected generated pin capacitance to stay on the same scale as "
         "OpenROAD";
}

}  // namespace
