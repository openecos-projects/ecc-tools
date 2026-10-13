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
 * @file FastSTACondition.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-05
 * @brief Parse Liberty expressions and retain correlations of unknown controls.
 */

#include "FastSTACondition.hh"

#include <cctype>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace icts {
namespace {

enum class BooleanOperation
{
  kConstant,
  kVariable,
  kNot,
  kAnd,
  kOr,
  kXor
};

struct BooleanNode
{
  BooleanOperation operation = BooleanOperation::kConstant;
  std::size_t left = 0U;
  std::size_t right = 0U;
  bool constant = false;
  std::string variable = "";
};

class BooleanExpression
{
 public:
  explicit BooleanExpression(std::string_view expression) : _expression(expression) {}

  auto evaluate(const FastStaCondition::PinValueLookup& lookup, std::string switching_pin = {}) -> FastStaLogicValue
  {
    _switching_pin = std::move(switching_pin);
    const auto root = parse();
    if (!root.has_value()) {
      return FastStaLogicValue::kInvalid;
    }
    std::map<std::string, bool> assignments;
    std::vector<std::string> unknown;
    for (const auto& [name, unused] : _variables) {
      (void) unused;
      if (name == _switching_pin) {
        assignments[name] = false;
        continue;
      }
      const auto value = lookup(name);
      assignments[name] = value.value_or(false);
      if (!value.has_value()) {
        unknown.push_back(name);
      }
    }
    const auto possibilities = enumerate(*root, assignments, unknown);
    if (possibilities == 1U) {
      return FastStaLogicValue::kZero;
    }
    return possibilities == 2U ? FastStaLogicValue::kOne : FastStaLogicValue::kUnknown;
  }

 private:
  auto skipSpace() -> void
  {
    while (_position < _expression.size() && std::isspace(static_cast<unsigned char>(_expression[_position])) != 0) {
      ++_position;
    }
  }

  static auto precedence(char operation) -> unsigned
  {
    switch (operation) {
      case '!':
        return 4U;
      case '&':
        return 3U;
      case '^':
        return 2U;
      case '|':
        return 1U;
      default:
        return 0U;
    }
  }

  auto append(BooleanOperation operation, std::size_t left, std::size_t right = 0U) -> std::size_t
  {
    _nodes.push_back(BooleanNode{.operation = operation, .left = left, .right = right});
    return _nodes.size() - 1U;
  }

  auto reduce(char operation, std::vector<std::size_t>& operands) -> bool
  {
    if (operands.empty()) {
      return false;
    }
    if (operation == '!') {
      operands.back() = append(BooleanOperation::kNot, operands.back());
      return true;
    }
    if (operands.size() < 2U || precedence(operation) == 0U) {
      return false;
    }
    const auto right = operands.back();
    operands.pop_back();
    auto kind = BooleanOperation::kAnd;
    if (operation == '|') {
      kind = BooleanOperation::kOr;
    } else if (operation == '^') {
      kind = BooleanOperation::kXor;
    }
    operands.back() = append(kind, operands.back(), right);
    return true;
  }

  auto parse() -> std::optional<std::size_t>
  {
    std::vector<char> operators;
    std::vector<std::size_t> operands;
    bool expect_operand = true;
    while (true) {
      skipSpace();
      if (_position == _expression.size()) {
        break;
      }
      const auto token = _expression[_position];
      if (expect_operand) {
        if (token == '!' || token == '~' || token == '(') {
          operators.push_back(token == '~' ? '!' : token);
          ++_position;
          continue;
        }
      } else {
        if (token == '\'') {
          operands.back() = append(BooleanOperation::kNot, operands.back());
          ++_position;
          continue;
        }
        if (token == ')') {
          while (!operators.empty() && operators.back() != '(') {
            if (!reduce(operators.back(), operands)) {
              return std::nullopt;
            }
            operators.pop_back();
          }
          if (operators.empty()) {
            return std::nullopt;
          }
          operators.pop_back();
          ++_position;
          continue;
        }
        // Adjacent operands use Liberty's implicit AND at the same precedence as '&'.
        auto operation = '&';
        if (token == '|' || token == '+') {
          operation = '|';
        } else if (token == '^') {
          operation = '^';
        }
        while (!operators.empty() && precedence(operators.back()) >= precedence(operation)) {
          if (!reduce(operators.back(), operands)) {
            return std::nullopt;
          }
          operators.pop_back();
        }
        operators.push_back(operation);
        expect_operand = true;
        if (token == '&' || token == '*' || token == '|' || token == '+' || token == '^') {
          ++_position;
        }
        continue;
      }
      const auto begin = _position;
      while (_position < _expression.size()) {
        const auto character = _expression[_position];
        if (std::isspace(static_cast<unsigned char>(character)) != 0 || std::string_view("!~'&*|+^()").find(character) != std::string_view::npos) {
          break;
        }
        ++_position;
      }
      if (begin == _position) {
        return std::nullopt;
      }
      const auto name = std::string(_expression.substr(begin, _position - begin));
      if (name == "0" || name == "1") {
        _nodes.push_back(BooleanNode{.constant = name == "1"});
      } else {
        _variables.emplace(name, false);
        _nodes.push_back(BooleanNode{.operation = BooleanOperation::kVariable, .variable = name});
      }
      operands.push_back(_nodes.size() - 1U);
      expect_operand = false;
    }
    if (expect_operand) {
      return std::nullopt;
    }
    while (!operators.empty()) {
      if (!reduce(operators.back(), operands)) {
        return std::nullopt;
      }
      operators.pop_back();
    }
    return operands.size() == 1U ? std::optional<std::size_t>{operands.back()} : std::nullopt;
  }

  auto value(std::size_t node_id, const std::map<std::string, bool>& assignments) const -> bool
  {
    // Parsing appends each operator after its operands, so one forward pass evaluates the tree.
    std::vector<bool> values(_nodes.size());
    for (std::size_t index = 0U; index < _nodes.size(); ++index) {
      const auto& node = _nodes.at(index);
      switch (node.operation) {
        case BooleanOperation::kConstant:
          values.at(index) = node.constant;
          break;
        case BooleanOperation::kVariable:
          values.at(index) = assignments.at(node.variable);
          break;
        case BooleanOperation::kNot:
          values.at(index) = !values.at(node.left);
          break;
        case BooleanOperation::kAnd:
          values.at(index) = values.at(node.left) && values.at(node.right);
          break;
        case BooleanOperation::kOr:
          values.at(index) = values.at(node.left) || values.at(node.right);
          break;
        case BooleanOperation::kXor:
          values.at(index) = values.at(node.left) != values.at(node.right);
          break;
      }
    }
    return values.at(node_id);
  }

  auto enumerate(std::size_t root, std::map<std::string, bool>& assignments, const std::vector<std::string>& unknown) const -> unsigned
  {
    unsigned possibilities = 0U;
    while (true) {
      if (!_switching_pin.empty()) {
        assignments[_switching_pin] = false;
        const auto zero = value(root, assignments);
        assignments[_switching_pin] = true;
        possibilities |= zero != value(root, assignments) ? 2U : 1U;
      } else {
        possibilities |= value(root, assignments) ? 2U : 1U;
      }
      if (possibilities == 3U) {
        return possibilities;
      }
      bool next_assignment = false;
      for (auto index = unknown.size(); index > 0U; --index) {
        auto& assigned = assignments.at(unknown.at(index - 1U));
        assigned = !assigned;
        if (assigned) {
          next_assignment = true;
          break;
        }
      }
      if (!next_assignment) {
        return possibilities;
      }
    }
  }

  std::string_view _expression;
  std::size_t _position = 0U;
  std::vector<BooleanNode> _nodes;
  std::map<std::string, bool> _variables;
  std::string _switching_pin;
};

}  // namespace

auto FastStaCondition::evaluate(const std::string& expression, const PinValueLookup& lookup) -> FastStaLogicValue
{
  return expression.empty() ? FastStaLogicValue::kOne : BooleanExpression(expression).evaluate(lookup);
}

auto FastStaCondition::isSensitized(const std::string& expression, const std::string& switching_pin, const PinValueLookup& lookup) -> FastStaLogicValue
{
  return expression.empty() || switching_pin.empty() ? FastStaLogicValue::kInvalid : BooleanExpression(expression).evaluate(lookup, switching_pin);
}

}  // namespace icts
