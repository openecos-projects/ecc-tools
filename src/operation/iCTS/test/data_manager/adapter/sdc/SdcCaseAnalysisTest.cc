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
 * @file SdcCaseAnalysisTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-10-28
 * @brief Shared-frontend case recording and error-boundary regressions.
 */
#if __has_include(<tcl8.6/tcl.h>)
#include <tcl8.6/tcl.h>
#else
#include <tcl.h>
#endif
#include <gtest/gtest.h>

#include <filesystem>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "DataManager.hpp"
#include "Database.hpp"
#include "SDCClockReader.hh"
#include "SdcCommand.hpp"
#include "TimingConstraints.hh"
#include "sdc/Sdc.hpp"
#include "toolkit/io/TestArtifactIO.hh"

namespace icts_test {
namespace {

class SdcCaseAnalysisTestInterface : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    _directory = toolkit::io::PrepareCleanOutputDir(toolkit::io::ResolveOutputDir() / "sdc_case_analysis"
                                                    / ::testing::UnitTest::GetInstance()->current_test_info()->name());
  }

  auto read(const std::string& script) const -> icts::SdcClockData
  {
    const auto path = _directory / "constraints.sdc";
    EXPECT_TRUE(toolkit::io::WriteTextArtifact(path, script));
    return icts::SdcClockReader(path.string()).readClockData();
  }

  std::filesystem::path _directory;
};

TEST_F(SdcCaseAnalysisTestInterface, RetainsUnboundPortAndPinIdentities)
{
  const auto data = read(
      "create_clock -name clk_sys -period 4 [get_ports clk_i]\n"
      "set_case_analysis 0 [get_ports testmode_i]\n"
      "set_case_analysis 1 [get_pins pad_testmode_i/C]\n");
  ASSERT_TRUE(data.ok()) << ::testing::PrintToString(data.issues);
  const std::vector<icts::SdcCaseAnalysis> expected{
      {0, {{icts::SdcObjectKind::kPort, "testmode_i", true}}},
      {1, {{icts::SdcObjectKind::kPin, "pad_testmode_i/C", true}}},
  };
  EXPECT_EQ(data.case_analyses, expected);
  EXPECT_EQ(data.ignored, std::vector<std::string>{});
  ASSERT_EQ(data.clocks.size(), 1U);
  EXPECT_EQ(data.clocks.front().clock_name, "clk_sys");
  EXPECT_DOUBLE_EQ(data.clocks.front().period_ns, 4.0);
}

TEST_F(SdcCaseAnalysisTestInterface, PreservesTypedCollectionsAndLiteralNames)
{
  const auto data = read(
      "set_case_analysis 1 [list [get_ports clk_i] [list [get_pins pad/mode/C] [get_nets tie_low]]]\n"
      "set_case_analysis 0 [list {bare pin} mode_net]\n");
  ASSERT_TRUE(data.ok()) << ::testing::PrintToString(data.issues);
  const std::vector<icts::SdcCaseAnalysis> expected{
      {1, {{icts::SdcObjectKind::kPort, "clk_i", true}, {icts::SdcObjectKind::kPin, "pad/mode/C", true}, {icts::SdcObjectKind::kNet, "tie_low", true}}},
      {0, {{icts::SdcObjectKind::kUnknown, "bare pin", false}, {icts::SdcObjectKind::kUnknown, "mode_net", false}}},
  };
  EXPECT_EQ(data.case_analyses, expected);
}

TEST_F(SdcCaseAnalysisTestInterface, RecordsExecutedTclControlFlow)
{
  const auto data = read(R"(
set calls 0
proc record_case {pin} {
  global calls
  incr calls
  set_case_analysis 0 [get_pins $pin]
}
foreach pin {pad_a/C pad_b/C} {record_case $pin}
if {$calls != 2} {error {case procedure was not executed exactly twice}}
set_case_analysis [expr {$calls - 1}] [get_ports [format %s testmode_i]]
create_clock -name clk_sys -period 4 [get_ports clk_i]
)");
  ASSERT_TRUE(data.ok()) << ::testing::PrintToString(data.issues);
  const std::vector<icts::SdcCaseAnalysis> expected{
      {0, {{icts::SdcObjectKind::kPin, "pad_a/C", true}}},
      {0, {{icts::SdcObjectKind::kPin, "pad_b/C", true}}},
      {1, {{icts::SdcObjectKind::kPort, "testmode_i", true}}},
  };
  EXPECT_EQ(data.case_analyses, expected);
  ASSERT_EQ(data.clocks.size(), 1U);
  EXPECT_EQ(data.clocks.front().clock_name, "clk_sys");
}

TEST_F(SdcCaseAnalysisTestInterface, RecordsSourcedConstraintsInTheSameInterpreter)
{
  ASSERT_TRUE(toolkit::io::WriteTextArtifact(_directory / "modes.tcl", "incr source_count\nset_case_analysis $mode [get_ports testmode_i]\n"));
  const auto data = read("set mode 0\nset source_count 0\nsource {" + (_directory / "modes.tcl").string()
                         + "}\nif {$source_count != 1} {error {case source was not evaluated exactly once}}\n"
                           "create_clock -name clk_sys -period 4 [get_ports clk_i]\n");
  ASSERT_TRUE(data.ok()) << ::testing::PrintToString(data.issues);
  const std::vector<icts::SdcCaseAnalysis> expected{{0, {{icts::SdcObjectKind::kPort, "testmode_i", true}}}};
  EXPECT_EQ(data.case_analyses, expected);
  ASSERT_EQ(data.clocks.size(), 1U);
}

TEST_F(SdcCaseAnalysisTestInterface, RejectsInvalidAndUnsupportedValues)
{
  using Code = icts::SdcConstraintStatusCode;
  const std::vector<std::pair<std::string, Code>> cases{
      {"2", Code::kMalformed},        {"invalid", Code::kMalformed}, {"rise", Code::kUnsupported},
      {"rising", Code::kUnsupported}, {"fall", Code::kUnsupported},  {"falling", Code::kUnsupported},
  };
  for (const auto& [value, status] : cases) {
    SCOPED_TRACE(value);
    const auto data = read("set_case_analysis " + value + " [get_ports testmode_i]\n");
    const std::vector<icts::SdcConstraintIssue> expected{{status, "set_case_analysis", "only_constant_zero_or_one_case_values_supported"}};
    EXPECT_EQ(data.status, status);
    EXPECT_EQ(data.issues, expected);
    EXPECT_EQ(data.case_analyses, std::vector<icts::SdcCaseAnalysis>{});
  }
}

TEST_F(SdcCaseAnalysisTestInterface, RejectsMissingAndExtraArguments)
{
  for (const auto& command : {"set_case_analysis 0", "set_case_analysis 0 mode extra"}) {
    SCOPED_TRACE(command);
    const auto data = read(command);
    const std::vector<icts::SdcConstraintIssue> expected{{icts::SdcConstraintStatusCode::kMalformed, "set_case_analysis", "value_and_objects_required"}};
    EXPECT_EQ(data.status, icts::SdcConstraintStatusCode::kMalformed);
    EXPECT_EQ(data.issues, expected);
    EXPECT_EQ(data.case_analyses, std::vector<icts::SdcCaseAnalysis>{});
  }
}

TEST_F(SdcCaseAnalysisTestInterface, RejectsEmptyCollectionsWithoutFabricatingObjects)
{
  for (const auto& command : {"set_case_analysis 0 [get_ports -quiet missing*]", "set_case_analysis 0 {}"}) {
    SCOPED_TRACE(command);
    const auto data = read(command);
    const std::vector<icts::SdcConstraintIssue> expected{{icts::SdcConstraintStatusCode::kUnresolvedReference, "set_case_analysis", "empty_object_collection"}};
    EXPECT_EQ(data.status, icts::SdcConstraintStatusCode::kUnresolvedReference);
    EXPECT_EQ(data.issues, expected);
    EXPECT_EQ(data.case_analyses, std::vector<icts::SdcCaseAnalysis>{});
  }
}

TEST_F(SdcCaseAnalysisTestInterface, RejectsEmptyObjectNames)
{
  const auto data = read("set_case_analysis 0 [list {}]\n");
  const std::vector<icts::SdcConstraintIssue> expected{{icts::SdcConstraintStatusCode::kUnresolvedReference, "set_case_analysis", "empty_object_name"}};
  EXPECT_EQ(data.status, icts::SdcConstraintStatusCode::kUnresolvedReference);
  EXPECT_EQ(data.issues, expected);
  EXPECT_EQ(data.case_analyses, std::vector<icts::SdcCaseAnalysis>{});
}

TEST_F(SdcCaseAnalysisTestInterface, RejectsMalformedCollectionsAtTheTclBoundary)
{
  const auto data = read("set bad [format %c 123]\nset_case_analysis 0 $bad\n");
  const std::vector<icts::SdcConstraintIssue> expected{{icts::SdcConstraintStatusCode::kMalformed, "set_case_analysis", "invalid object collection"}};
  EXPECT_EQ(data.status, icts::SdcConstraintStatusCode::kMalformed);
  EXPECT_EQ(data.issues, expected);
  EXPECT_EQ(data.case_analyses, std::vector<icts::SdcCaseAnalysis>{});
}

TEST_F(SdcCaseAnalysisTestInterface, DoesNotAppendPartialConstraintsWithInvalidObjectKinds)
{
  const auto data = read(
      "create_clock -name clk_sys -period 4 [get_ports clk_i]\n"
      "set_case_analysis 0 [list [get_ports testmode_i] [get_clocks clk_sys]]\n");
  const std::vector<icts::SdcConstraintIssue> expected{{icts::SdcConstraintStatusCode::kMalformed, "set_case_analysis", "expected_pin_port_or_net_collection"}};
  EXPECT_EQ(data.status, icts::SdcConstraintStatusCode::kMalformed);
  EXPECT_EQ(data.issues, expected);
  EXPECT_EQ(data.case_analyses, std::vector<icts::SdcCaseAnalysis>{});
}

TEST_F(SdcCaseAnalysisTestInterface, DoesNotHideCaseErrorsInsideControlFlow)
{
  const auto data = read(
      "foreach pin {pad/C} {set_case_analysis rise [get_pins $pin]}\n"
      "create_clock -name clk_sys -period 4 [get_ports clk_i]\n");
  const std::vector<icts::SdcConstraintIssue> expected{
      {icts::SdcConstraintStatusCode::kUnsupported, "set_case_analysis", "only_constant_zero_or_one_case_values_supported"}};
  EXPECT_EQ(data.status, icts::SdcConstraintStatusCode::kUnsupported);
  EXPECT_EQ(data.issues, expected);
  EXPECT_EQ(data.case_analyses, std::vector<icts::SdcCaseAnalysis>{});
}

TEST_F(SdcCaseAnalysisTestInterface, DoesNotRelaxNormalStaOrLeakItsDatabase)
{
  const auto data = read("set_case_analysis 0 [get_ports testmode_i]\n");
  ASSERT_TRUE(data.ok());
  ASSERT_FALSE(ista::DataManager::isInitialized());
  ista::DataManager::initInst();
  struct OwnedStaDatabase
  {
    ~OwnedStaDatabase() { ista::DataManager::destroyInst(); }
  } owned_database;
  ista::SdcCommand interpreter;
  ista::sdc::registerSdcCommands(interpreter);
  EXPECT_EQ(interpreter.evalString("set_case_analysis 0 testmode_i"), TCL_ERROR);
  EXPECT_TRUE(ista::DataManager::getInst().getDatabase().sdc().get_case_analysis_map().empty());
}

TEST_F(SdcCaseAnalysisTestInterface, PreservesExistingNumericConstantForms)
{
  const std::vector<std::pair<std::string, int>> cases{{"+1", 1}, {"01", 1}, {"-0", 0}, {" 0 ", 0}, {"00", 0}};
  for (const auto& [text, value] : cases) {
    SCOPED_TRACE(text);
    const auto data = read("set_case_analysis {" + text + "} [get_ports testmode_i]");
    ASSERT_TRUE(data.ok());
    const std::vector<icts::SdcCaseAnalysis> expected{{value, {{icts::SdcObjectKind::kPort, "testmode_i", true}}}};
    EXPECT_EQ(data.case_analyses, expected);
  }
}

TEST_F(SdcCaseAnalysisTestInterface, DoesNotHideCollectionErrorsInsideControlFlow)
{
  const auto data = read(R"(
set bad [format %c 123]
foreach pin {pad/C} {set_case_analysis 0 $bad}
create_clock -name clk_sys -period 4 [get_ports clk_i]
)");
  icts::SdcClockData expected;
  expected.diagnostics = {"set_case_analysis:invalid object collection"};
  expected.status = icts::SdcConstraintStatusCode::kMalformed;
  expected.issues = {{icts::SdcConstraintStatusCode::kMalformed, "set_case_analysis", "invalid object collection"}};
  EXPECT_EQ(data, expected);
}

TEST_F(SdcCaseAnalysisTestInterface, DoesNotHideCollectionErrorsCaughtByTcl)
{
  const auto data = read(R"(
set bad [format %c 123]
catch {set_case_analysis 0 $bad}
create_clock -name clk_sys -period 4 [get_ports clk_i]
)");
  icts::SdcClockData expected;
  expected.diagnostics = {"set_case_analysis:invalid object collection"};
  expected.status = icts::SdcConstraintStatusCode::kMalformed;
  expected.issues = {{icts::SdcConstraintStatusCode::kMalformed, "set_case_analysis", "invalid object collection"}};
  EXPECT_EQ(data, expected);
}

TEST_F(SdcCaseAnalysisTestInterface, DoesNotRetainCasesAcrossReaderSessions)
{
  const auto first = read("set_case_analysis 0 [get_ports mode_a]");
  const auto second = read("set_case_analysis 1 [get_ports mode_b]");
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  const std::vector<icts::SdcCaseAnalysis> first_expected{{0, {{icts::SdcObjectKind::kPort, "mode_a", true}}}};
  const std::vector<icts::SdcCaseAnalysis> second_expected{{1, {{icts::SdcObjectKind::kPort, "mode_b", true}}}};
  EXPECT_EQ(first.case_analyses, first_expected);
  EXPECT_EQ(second.case_analyses, second_expected);
}

}  // namespace
}  // namespace icts_test
