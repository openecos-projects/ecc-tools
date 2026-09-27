// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You may obtain a copy of the License at:
// http://license.coscl.org.cn/MulanPSL2
//
// ***************************************************************************************
#include "ObjectFilter.hpp"

#include "SdcCommand.hpp"

#include <cmath>
#include <cstdlib>
#include <string_view>

#if __has_include(<tcl8.6/tcl.h>)
#include <tcl8.6/tcl.h>
#else
#include <tcl.h>
#endif

namespace ista::sdc {

namespace {

std::string trimFilterText(std::string text)
{
  const std::size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const std::size_t last = text.find_last_not_of(" \t\r\n");
  text = text.substr(first, last - first + 1);
  while (text.size() >= 2 && ((text.front() == '{' && text.back() == '}') || (text.front() == '(' && text.back() == ')'))) {
    int depth = 0;
    bool encloses_all = true;
    for (std::size_t index = 0; index < text.size(); ++index) {
      if (text[index] == text.front()) ++depth;
      if (text[index] == text.back()) --depth;
      if (depth == 0 && index + 1 < text.size()) {
        encloses_all = false;
        break;
      }
    }
    if (!encloses_all) break;
    text = trimFilterText(text.substr(1, text.size() - 2));
  }
  if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') || (text.front() == '\'' && text.back() == '\''))) {
    text = text.substr(1, text.size() - 2);
  }
  return text;
}

std::size_t findFilterOperator(const std::string& expression, std::string_view operation)
{
  int parentheses = 0;
  int braces = 0;
  char quote = '\0';
  for (std::size_t index = 0; index + operation.size() <= expression.size(); ++index) {
    const char value = expression[index];
    if (quote != '\0') {
      if (value == quote && (index == 0 || expression[index - 1] != '\\')) quote = '\0';
      continue;
    }
    if (value == '"' || value == '\'') {
      quote = value;
      continue;
    }
    if (value == '(') ++parentheses;
    if (value == ')') --parentheses;
    if (value == '{') ++braces;
    if (value == '}') --braces;
    if (parentheses == 0 && braces == 0 && expression.compare(index, operation.size(), operation) == 0) return index;
  }
  return std::string::npos;
}

bool parseFilterNumber(const std::string& text, double& value)
{
  char* end = nullptr;
  value = std::strtod(text.c_str(), &end);
  return end != text.c_str() && *end == '\0' && std::isfinite(value);
}

bool evaluateFilterExpression(const std::string& raw_expression, const std::map<std::string, std::string>& attributes, bool regexp, bool nocase)
{
  const std::string expression = trimFilterText(raw_expression);
  if (expression.empty()) return true;
  for (std::string_view operation : {std::string_view("||"), std::string_view("&&")}) {
    const std::size_t position = findFilterOperator(expression, operation);
    if (position != std::string::npos) {
      const bool left = evaluateFilterExpression(expression.substr(0, position), attributes, regexp, nocase);
      const bool right = evaluateFilterExpression(expression.substr(position + operation.size()), attributes, regexp, nocase);
      return operation == "||" ? left || right : left && right;
    }
  }
  if (expression.front() == '!') return !evaluateFilterExpression(expression.substr(1), attributes, regexp, nocase);

  for (std::string_view operation : {std::string_view("!~"), std::string_view("=~"), std::string_view("!="), std::string_view("=="),
                                     std::string_view(">="), std::string_view("<="), std::string_view(">"), std::string_view("<")}) {
    const std::size_t position = findFilterOperator(expression, operation);
    if (position == std::string::npos) continue;
    const std::string attribute = trimFilterText(expression.substr(0, position));
    const std::string expected = trimFilterText(expression.substr(position + operation.size()));
    const auto actual_iter = attributes.find(attribute);
    if (actual_iter == attributes.end()) throw std::invalid_argument("unknown filter attribute: " + attribute);
    const std::string& actual = actual_iter->second;
    if (operation == "=~" || operation == "!~") {
      bool match = false;
      if (regexp) {
        const std::string pattern = std::string(nocase ? "(?i)" : "") + "^(?:" + expected + ")$";
        if (Tcl_RegExpMatch(SdcCommand::getInst().getInterp(), "", pattern.c_str()) < 0) {
          throw std::invalid_argument("invalid filter regular expression: " + expected);
        }
        match = Tcl_RegExpMatch(SdcCommand::getInst().getInterp(), actual.c_str(), pattern.c_str()) == 1;
      } else if (nocase) {
        std::string folded_actual = actual;
        std::string folded_expected = expected;
        std::transform(folded_actual.begin(), folded_actual.end(), folded_actual.begin(), [](unsigned char value) { return std::tolower(value); });
        std::transform(folded_expected.begin(), folded_expected.end(), folded_expected.begin(), [](unsigned char value) { return std::tolower(value); });
        match = Tcl_StringMatch(folded_actual.c_str(), folded_expected.c_str()) != 0;
      } else {
        match = Tcl_StringMatch(actual.c_str(), expected.c_str()) != 0;
      }
      return operation == "=~" ? match : !match;
    }
    if (operation == "==" || operation == "!=") {
      const bool match = actual == expected;
      return operation == "==" ? match : !match;
    }
    double left = 0.0;
    double right = 0.0;
    if (!parseFilterNumber(actual, left) || !parseFilterNumber(expected, right)) {
      throw std::invalid_argument("numeric filter comparison requires numbers: " + expression);
    }
    if (operation == ">=") return left >= right;
    if (operation == "<=") return left <= right;
    if (operation == ">") return left > right;
    return left < right;
  }
  const auto attribute = attributes.find(expression);
  return attribute != attributes.end() && attribute->second != "false" && attribute->second != "0" && !attribute->second.empty();
}

}  // namespace

bool matchesFilter(const std::string& expression, const std::map<std::string, std::string>& attributes, bool regexp, bool nocase)
{
  return evaluateFilterExpression(expression, attributes, regexp, nocase);
}

}  // namespace ista::sdc
