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
 * @file SDCClockParser.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief SDC clock subset parser declarations.
 */

#pragma once

#include <cstddef>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "SDCClockReader.hh"

namespace icts::sdc_reader {

struct ParsedWord
{
  std::string text = "";
  bool braced = false;
};

struct SdcValue
{
  std::vector<std::string> strings;
  std::vector<SdcObjectRef> objects;
};

struct SdcCommandOptions
{
  std::unordered_map<std::string, SdcValue> values;
  std::vector<std::pair<std::string, SdcValue>> ordered_values;
  std::unordered_set<std::string> flags;
  std::vector<SdcValue> positional;
};

// What the reader had reported before a command and its arguments were evaluated.
// An unrecognized command rewinds to this watermark so that everything its subtree
// reported collapses into one non-fatal note.
struct SdcReportWatermark
{
  std::size_t issues = 0U;
  std::size_t diagnostics = 0U;
  std::size_t ignored = 0U;
};

class ArithmeticParser
{
 public:
  explicit ArithmeticParser(std::string expression);

  auto parse(double& value) -> bool;

 private:
  static auto isOperator(char token) -> bool;
  static auto precedence(char token) -> int;
  static auto applyTopOperator(std::vector<double>& values, std::vector<char>& operators) -> bool;
  static auto applyUntilOpen(std::vector<double>& values, std::vector<char>& operators) -> bool;

  auto parseNumber(double& value) -> bool;
  auto skipSpaces() -> void;
  auto matchWord(std::string_view word) const -> bool;

  std::string _expression;
  std::size_t _pos = 0U;
};

class SdcSubsetEvaluator
{
 public:
  explicit SdcSubsetEvaluator(SdcUnits units = {}) : _default_units(units) {}
  auto readFile(const std::string& sdc_path) -> SdcClockData;
  static auto resolveGeneratedClockData(SdcClockData& data) -> void;

 private:
  auto splitCommands(const std::string& text) -> std::vector<std::string>;
  auto parseWords(const std::string& command) -> std::vector<ParsedWord>;
  static auto parseBalanced(const std::string& text, std::size_t open_pos, char open_ch, char close_ch) -> std::pair<std::string, std::size_t>;
  static auto matchingBracketPos(const std::string& text, std::size_t open_pos) -> std::size_t;
  static auto parseVariableName(const std::string& text, std::size_t dollar_pos) -> std::pair<std::string, std::size_t>;

  auto substituteVariablesToString(const std::string& text) -> std::string;
  auto evaluateCommand(const std::string& command) -> SdcValue;
  auto evaluateCommandArgs(const std::string& command, const std::vector<SdcValue>& args, const SdcReportWatermark& watermark) -> SdcValue;

  auto evaluateSet(const std::vector<ParsedWord>& words, const std::vector<SdcValue>& args) -> SdcValue;
  auto evaluateExpr(const std::vector<SdcValue>& args) -> SdcValue;
  auto evaluateSetUnits(const std::vector<SdcValue>& args) -> void;
  auto evaluateCollection(SdcObjectKind kind, const std::vector<SdcValue>& args) -> SdcValue;
  auto evaluateGetClocks(const std::vector<SdcValue>& args) -> SdcValue;
  auto evaluateAllClocks() -> SdcValue;
  auto evaluateCreateClock(const std::vector<SdcValue>& args) -> SdcValue;
  auto evaluateCreateGeneratedClock(const std::vector<SdcValue>& args) -> SdcValue;
  auto evaluateSetCaseAnalysis(const std::vector<SdcValue>& args) -> void;
  auto evaluateClockTransition(const std::vector<SdcValue>& args) -> void;

  auto reportIssue(SdcConstraintStatusCode code, const std::string& command, const std::string& detail) -> void;
  // Record an SDC construct iCTS does not read. These never make `SdcClockData::ok()`
  // false: the clock model iCTS consumes is unaffected by a command it never looks at.
  auto reportIgnored(const std::string& command, const std::string& detail) -> void;
  auto parseOptions(const std::string& command, const std::vector<SdcValue>& args, std::initializer_list<std::string_view> value_options,
                    std::initializer_list<std::string_view> flag_options, std::initializer_list<std::string_view> repeated_options = {})
      -> std::optional<SdcCommandOptions>;
  auto readRefs(const std::string& command, const SdcValue& value, SdcObjectKind default_kind, std::vector<SdcObjectRef>& refs) -> bool;
  auto readNumber(const std::string& command, const SdcValue& value, double& number) -> bool;
  auto scaleNumber(const std::string& command, double& number, double scale) -> bool;
  auto readScalarObjects(const std::string& command, const SdcCommandOptions& options, SdcObjectKind default_kind, double& number,
                         std::vector<SdcObjectRef>& objects) -> bool;
  auto resolveGeneratedClocks() -> void;
  auto storeClock(SdcClockDecl clock) -> void;

  SdcUnits _default_units;
  double _time_unit_ns = 1.0;
  double _capacitance_unit_pf = 1.0;
  SdcClockData _data;
  std::unordered_map<std::string, SdcValue> _variables;
};

auto Trim(const std::string& text) -> std::string;
auto IsOption(const std::string& text) -> bool;
auto JoinStrings(const std::vector<std::string>& values) -> std::string;
auto SplitListText(const std::string& text) -> std::vector<std::string>;
auto ValueToString(const SdcValue& value) -> std::string;
auto MakeStringValue(std::string text) -> SdcValue;
auto MakeObjectValue(SdcObjectKind kind, const std::vector<std::string>& patterns) -> SdcValue;
auto AppendRefsFromValue(std::vector<SdcObjectRef>& refs, const SdcValue& value, SdcObjectKind default_kind) -> void;
auto ParseDoubleValue(const std::string& text, double& value) -> bool;
auto ParseIntValue(const std::string& text, int& value) -> bool;
auto TimeUnitToNs(const std::string& unit) -> double;
auto CapacitanceUnitToPf(const std::string& unit) -> double;
auto OptionTransition(const SdcCommandOptions& options) -> SdcTransition;
auto SelectBothUnlessOne(const SdcCommandOptions& options, const std::string& first, const std::string& second, bool& selected_first, bool& selected_second)
    -> void;
auto ObjectPatternMatches(const std::string& pattern, const std::string& name) -> bool;
auto PrimarySourceExpression(const SdcClockDecl& clock) -> std::string;

}  // namespace icts::sdc_reader
