// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You may use this software according to the terms and conditions of the Mulan PSL v2.
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
 * @file LibertyStatementTerminatorTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-11-05
 * @brief Regression test for a simple attribute that omits its ';' terminator.
 */

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "CppLibertyAST.hh"
#include "CppLibertyDriver.hh"

namespace liberty_test {
namespace {

std::string libraryWith(bool terminate_area)
{
  const std::string terminator = terminate_area ? ";" : "";
  return "library (test_lib) {\n"
         "  delay_model : table_lookup;\n"
         "  time_unit : \"1ns\";\n"
         "  capacitive_load_unit (1,pf);\n"
         "  cell (FIRST) {\n"
         "    area : 1.0" + terminator + "\n"
         "    pin (A) {\n"
         "      direction : input;\n"
         "    }\n"
         "  }\n"
         "  cell (SECOND) {\n"
         "    area : 2.0" + terminator + "\n"
         "    pin (Y) {\n"
         "      direction : output;\n"
         "    }\n"
         "  }\n"
         "}\n";
}

/**
 * A missing ';' must end the statement, not swallow the next one.
 *
 * ASAP7's INVBUF and SEQ libraries omit the terminator on 42 `area :` lines. The
 * value scanner used to read on until the next ';', absorbing the following
 * `pin`/`pg_pin` group (or an entire next cell) into the value string; the reader
 * then saw a truncated cell and silently dropped it, so the 202-cell merged
 * library exposed 53 cells and every ASAP7 CTS run failed on a missing pin model.
 */
TEST(LibertyStatementTerminatorTest, UnterminatedAttributeKeepsTheFollowingStatements)
{
  std::istringstream stream(libraryWith(false));
  liberty::LibertyDriver driver;
  ASSERT_TRUE(driver.parse(stream, "<inline>"));

  auto* library = driver.getParseResult();
  ASSERT_NE(library, nullptr);
  ASSERT_EQ(library->getChildGroups().size(), 2U);

  const char* const names[] = {"FIRST", "SECOND"};
  for (std::size_t index = 0; index < 2U; ++index) {
    auto& cell = library->getChildGroups().at(index);
    EXPECT_STREQ(cell->getFirstParamName(), names[index]);
    // The attribute before the unterminated one and the group after it must both
    // survive: that is exactly what the value scan used to consume.
    auto* area = cell->findAttribute("area");
    ASSERT_NE(area, nullptr) << names[index] << " lost its area";
    // The value must stay a number and stay attached to this cell: the old scan
    // turned it into "1pin(A){" and the reader dropped the cell.
    ASSERT_TRUE(area->getFirstValue()->isFloat()) << names[index] << " area became a string value";
    EXPECT_DOUBLE_EQ(area->getFirstValue()->asFloat(), static_cast<double>(index) + 1.0);
    ASSERT_EQ(cell->getChildGroups().size(), 1U) << names[index] << " lost its pin group";
    EXPECT_STREQ(cell->getChildGroups().front()->getGroupType(), "pin");
  }
}

TEST(LibertyStatementTerminatorTest, TerminatedAndUnterminatedLibrariesParseTheSame)
{
  std::istringstream terminated(libraryWith(true));
  std::istringstream unterminated(libraryWith(false));
  liberty::LibertyDriver terminated_driver;
  liberty::LibertyDriver unterminated_driver;
  ASSERT_TRUE(terminated_driver.parse(terminated, "<inline>"));
  ASSERT_TRUE(unterminated_driver.parse(unterminated, "<inline>"));

  auto* expected = terminated_driver.getParseResult();
  auto* actual = unterminated_driver.getParseResult();
  ASSERT_NE(expected, nullptr);
  ASSERT_NE(actual, nullptr);
  ASSERT_EQ(expected->getChildGroups().size(), actual->getChildGroups().size());
  for (std::size_t index = 0; index < expected->getChildGroups().size(); ++index) {
    auto& expected_cell = expected->getChildGroups().at(index);
    auto& actual_cell = actual->getChildGroups().at(index);
    EXPECT_STREQ(expected_cell->getFirstParamName(), actual_cell->getFirstParamName());
    EXPECT_EQ(expected_cell->getChildGroups().size(), actual_cell->getChildGroups().size());
    EXPECT_EQ(expected_cell->getAttributes().size(), actual_cell->getAttributes().size());
  }
}

/**
 * A backslash continuation keeps the value on the same logical line.
 *
 * The statement-boundary recovery above keys off a skipped newline, so a value
 * that legitimately continues onto the next line must not look like a new
 * statement. Both spellings of `area : 1.0 extra;` have to agree.
 */
TEST(LibertyStatementTerminatorTest, BackslashContinuationIsNotAStatementBoundary)
{
  const std::string one_line
      = "library (t) {\n"
        "  cell (C) {\n"
        "    area : 1.0 extra;\n"
        "    pin (A) { direction : input; }\n"
        "  }\n"
        "}\n";
  const std::string continued
      = "library (t) {\n"
        "  cell (C) {\n"
        "    area : 1.0 \\\n"
        "      extra;\n"
        "    pin (A) { direction : input; }\n"
        "  }\n"
        "}\n";

  std::istringstream one_line_stream(one_line);
  std::istringstream continued_stream(continued);
  liberty::LibertyDriver one_line_driver;
  liberty::LibertyDriver continued_driver;
  ASSERT_TRUE(one_line_driver.parse(one_line_stream, "<inline>"));
  ASSERT_TRUE(continued_driver.parse(continued_stream, "<inline>"));

  auto* expected = one_line_driver.getParseResult();
  auto* actual = continued_driver.getParseResult();
  ASSERT_NE(expected, nullptr);
  ASSERT_NE(actual, nullptr);
  ASSERT_EQ(expected->getChildGroups().size(), 1U);
  ASSERT_EQ(actual->getChildGroups().size(), 1U);

  auto& expected_cell = expected->getChildGroups().front();
  auto& actual_cell = actual->getChildGroups().front();
  auto* expected_area = expected_cell->findAttribute("area");
  auto* actual_area = actual_cell->findAttribute("area");
  ASSERT_NE(expected_area, nullptr);
  ASSERT_NE(actual_area, nullptr);
  ASSERT_FALSE(expected_area->getFirstValue()->isFloat());
  ASSERT_FALSE(actual_area->getFirstValue()->isFloat()) << "the continuation was read as a statement boundary";
  EXPECT_STREQ(expected_area->getFirstValue()->asString(), actual_area->getFirstValue()->asString());
  EXPECT_EQ(expected_cell->getChildGroups().size(), actual_cell->getChildGroups().size());
}

}  // namespace
}  // namespace liberty_test

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
