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
 * @file SDCClockEvaluator.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief SDC clock command evaluation dispatch.
 */

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "SDCClockParser.hh"

namespace icts::sdc_reader {
namespace {

enum class EvaluationStage
{
  kCommandStart,
  kCommandName,
  kNextArgument,
  kArgumentValue,
  kWordStart,
  kWordValue,
  kTextScan,
  kTextValue,
  kScriptStart,
  kNextCommand,
  kScriptValue,
};

struct EvaluationFrame
{
  EvaluationStage stage = EvaluationStage::kCommandStart;
  std::string text = "";
  bool braced = false;
  std::vector<ParsedWord> words;
  std::vector<std::string> commands;
  std::vector<SdcValue> args;
  std::string command_name = "";
  std::string expanded = "";
  SdcValue value{};
  std::size_t index = 0U;
  std::size_t literal_start = 0U;
  std::size_t issue_count = 0U;
};

}  // namespace

auto SdcSubsetEvaluator::substituteVariablesToString(const std::string& text) -> std::string
{
  std::string result;
  for (std::size_t index = 0U; index < text.size();) {
    if (text[index] == '\\' && index + 1U < text.size()) {
      const char escaped = text[index + 1U];
      if (escaped == 'n') {
        result += '\n';
      } else if (escaped == 't') {
        result += '\t';
      } else {
        result += escaped;
      }
      index += 2U;
      continue;
    }
    if (text[index] != '$') {
      result += text[index++];
      continue;
    }
    const auto [var_name, end_pos] = parseVariableName(text, index);
    if (var_name.empty()) {
      result += text[index++];
      continue;
    }
    if (const auto iter = _variables.find(var_name); iter != _variables.end()) {
      result += ValueToString(iter->second);
    } else {
      reportIssue(SdcConstraintStatusCode::kUnresolvedReference, "substitution", "unresolved_variable:" + var_name);
    }
    index = end_pos;
  }
  return result;
}

auto SdcSubsetEvaluator::evaluateCommand(const std::string& command) -> SdcValue
{
  // Each frame resumes only after its child completes, preserving substitution
  // order and typed collection values without recursive command evaluation.
  std::vector<EvaluationFrame> stack{{.text = command, .words = {}, .commands = {}, .args = {}}};
  SdcValue result;
  while (!stack.empty()) {
    auto& frame = stack.back();
    switch (frame.stage) {
      case EvaluationStage::kCommandStart:
        frame.issue_count = _data.issues.size();
        frame.words = parseWords(frame.text);
        if (frame.words.empty()) {
          result = {};
          stack.pop_back();
          break;
        }
        frame.stage = EvaluationStage::kCommandName;
        stack.push_back({.stage = EvaluationStage::kWordStart,
                         .text = frame.words.front().text,
                         .braced = frame.words.front().braced,
                         .words = {},
                         .commands = {},
                         .args = {}});
        break;
      case EvaluationStage::kCommandName:
        frame.command_name = ValueToString(result);
        frame.index = frame.command_name == "set" ? 2U : 1U;
        frame.stage = EvaluationStage::kNextArgument;
        break;
      case EvaluationStage::kArgumentValue:
        frame.args.push_back(std::exchange(result, {}));
        ++frame.index;
        frame.stage = EvaluationStage::kNextArgument;
        break;
      case EvaluationStage::kNextArgument:
        if (frame.index < frame.words.size()) {
          const auto& word = frame.words[frame.index];
          if (frame.command_name == "expr" && word.braced) {
            frame.args.push_back(MakeStringValue(substituteVariablesToString(word.text)));
            ++frame.index;
          } else {
            frame.stage = EvaluationStage::kArgumentValue;
            stack.push_back({.stage = EvaluationStage::kWordStart, .text = word.text, .braced = word.braced, .words = {}, .commands = {}, .args = {}});
          }
          break;
        }
        if (frame.command_name == "set") {
          result = evaluateSet(frame.words, frame.args);
        } else if (frame.command_name == "expr") {
          result = evaluateExpr(frame.args);
        } else {
          result = _data.issues.size() == frame.issue_count ? evaluateCommandArgs(frame.command_name, frame.args) : SdcValue{};
        }
        stack.pop_back();
        break;
      case EvaluationStage::kWordStart: {
        if (frame.braced) {
          result = MakeStringValue(frame.text);
          stack.pop_back();
          break;
        }
        const auto clean_text = Trim(frame.text);
        if (!clean_text.empty() && clean_text.front() == '$') {
          const auto [var_name, end_pos] = parseVariableName(clean_text, 0U);
          if (!var_name.empty() && end_pos == clean_text.size()) {
            if (const auto iter = _variables.find(var_name); iter != _variables.end()) {
              result = iter->second;
            } else {
              reportIssue(SdcConstraintStatusCode::kUnresolvedReference, "substitution", "unresolved_variable:" + var_name);
              result = {};
            }
            stack.pop_back();
            break;
          }
        }
        if (clean_text.size() >= 2U && clean_text.front() == '[' && matchingBracketPos(clean_text, 0U) == clean_text.size() - 1U) {
          frame.stage = EvaluationStage::kWordValue;
          stack.push_back(
              {.stage = EvaluationStage::kScriptStart, .text = clean_text.substr(1U, clean_text.size() - 2U), .words = {}, .commands = {}, .args = {}});
        } else {
          frame.stage = EvaluationStage::kTextScan;
        }
        break;
      }
      case EvaluationStage::kWordValue:
        stack.pop_back();
        break;
      case EvaluationStage::kTextValue:
        frame.expanded += ValueToString(result);
        frame.stage = EvaluationStage::kTextScan;
        break;
      case EvaluationStage::kTextScan: {
        bool suspended = false;
        while (frame.index < frame.text.size()) {
          if (frame.text[frame.index] == '\\' && frame.index + 1U < frame.text.size()) {
            frame.index += 2U;
            continue;
          }
          if (frame.text[frame.index] != '[') {
            ++frame.index;
            continue;
          }
          const auto close = matchingBracketPos(frame.text, frame.index);
          if (close == std::string::npos) {
            reportIssue(SdcConstraintStatusCode::kMalformed, "substitution", "unterminated_command_substitution");
            result = MakeStringValue({});
            stack.pop_back();
          } else {
            frame.expanded += substituteVariablesToString(frame.text.substr(frame.literal_start, frame.index - frame.literal_start));
            auto script = frame.text.substr(frame.index + 1U, close - frame.index - 1U);
            frame.index = close + 1U;
            frame.literal_start = frame.index;
            frame.stage = EvaluationStage::kTextValue;
            stack.push_back({.stage = EvaluationStage::kScriptStart, .text = std::move(script), .words = {}, .commands = {}, .args = {}});
          }
          suspended = true;
          break;
        }
        if (!suspended) {
          frame.expanded += substituteVariablesToString(frame.text.substr(frame.literal_start));
          result = MakeStringValue(std::move(frame.expanded));
          stack.pop_back();
        }
        break;
      }
      case EvaluationStage::kScriptStart:
        frame.commands = splitCommands(frame.text);
        frame.stage = EvaluationStage::kNextCommand;
        break;
      case EvaluationStage::kScriptValue:
        frame.value = std::exchange(result, {});
        ++frame.index;
        frame.stage = EvaluationStage::kNextCommand;
        break;
      case EvaluationStage::kNextCommand:
        if (frame.index < frame.commands.size()) {
          frame.stage = EvaluationStage::kScriptValue;
          stack.push_back({.text = frame.commands[frame.index], .words = {}, .commands = {}, .args = {}});
        } else {
          result = std::move(frame.value);
          stack.pop_back();
        }
        break;
    }
  }
  return result;
}

auto SdcSubsetEvaluator::evaluateCommandArgs(const std::string& command_name, const std::vector<SdcValue>& args) -> SdcValue
{
  if (command_name == "list" || command_name == "concat") {
    SdcValue value;
    for (const auto& arg : args) {
      value.objects.insert(value.objects.end(), arg.objects.begin(), arg.objects.end());
      for (const auto& string : arg.strings) {
        const auto items = command_name == "list" ? std::vector<std::string>{string} : SplitListText(string);
        for (const auto& item : items) {
          value.objects.push_back({SdcObjectKind::kUnknown, item, false});
        }
      }
    }
    return value;
  }
  if (command_name == "set_units") {
    evaluateSetUnits(args);
    return {};
  }
  if (command_name == "get_ports") {
    return evaluateCollection(SdcObjectKind::kPort, args);
  }
  if (command_name == "get_pins") {
    return evaluateCollection(SdcObjectKind::kPin, args);
  }
  if (command_name == "get_nets") {
    return evaluateCollection(SdcObjectKind::kNet, args);
  }
  if (command_name == "get_clocks") {
    return evaluateGetClocks(args);
  }
  if (command_name == "all_clocks") {
    if (!args.empty()) {
      reportIssue(SdcConstraintStatusCode::kUnsupported, command_name, "arguments_not_supported");
      return {};
    }
    return evaluateAllClocks();
  }
  if (command_name == "create_clock") {
    return evaluateCreateClock(args);
  }
  if (command_name == "create_generated_clock") {
    return evaluateCreateGeneratedClock(args);
  }
  if (command_name == "set_case_analysis") {
    evaluateSetCaseAnalysis(args);
    return {};
  }
  if (command_name == "set_false_path") {
    evaluatePathException(command_name, args, SdcExceptionKind::kFalsePath);
  } else if (command_name == "set_multicycle_path") {
    evaluatePathException(command_name, args, SdcExceptionKind::kMulticyclePath);
  } else if (command_name == "set_min_delay" || command_name == "set_max_delay") {
    evaluatePathException(command_name, args, command_name == "set_min_delay" ? SdcExceptionKind::kMinDelay : SdcExceptionKind::kMaxDelay);
  } else if (command_name == "set_clock_groups") {
    evaluateClockGroups(args);
  } else if (command_name == "set_clock_latency") {
    evaluateClockLatency(args);
  } else if (command_name == "set_clock_uncertainty") {
    evaluateClockUncertainty(args);
  } else if (command_name == "set_clock_transition") {
    evaluateClockTransition(args);
  } else if (command_name == "set_input_delay" || command_name == "set_output_delay") {
    evaluateIODelay(command_name, args, command_name == "set_input_delay");
  } else if (command_name == "set_input_transition") {
    evaluateInputTransition(args);
  } else if (command_name == "set_load") {
    evaluateLoad(args);
  } else if (command_name == "set_propagated_clock") {
    if (args.size() != 1U) {
      reportIssue(SdcConstraintStatusCode::kMalformed, command_name, "clock_list_required");
    } else {
      std::vector<SdcObjectRef> clocks;
      if (readRefs(command_name, args.front(), SdcObjectKind::kClock, clocks)) {
        _data.propagated_clocks.insert(_data.propagated_clocks.end(), clocks.begin(), clocks.end());
      }
    }
  } else if (!command_name.empty()) {
    reportIssue(SdcConstraintStatusCode::kUnsupported, command_name, "unsupported_sdc_command");
  }
  return {};
}

}  // namespace icts::sdc_reader
