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
// Runs minimal DRC cases through the production RuleValidator without idb, LEF or DEF.
//
//   idrc_case_runner <case.drc>...          check expectations and invariance oracles
//   idrc_case_runner --print <case.drc>...  print the observed violations (diagnosis only)

#include <stdlib.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "DRCCase.hpp"
#include "DataManager.hpp"
#include "Logger.hpp"
#include "RuleValidator.hpp"

namespace {

using namespace idrc;

struct RunOption
{
  std::string label;
  int32_t thread_number = 1;
  int32_t pitch = -1;
  bool reverse_shape_order = false;
};

void destroyDRC()
{
  RuleValidator::destroyInst();
  DataManager::destroyInst();
}

std::vector<std::string> runCase(const DRCCase& drc_case, const RunOption& option, const std::filesystem::path& temp_root)
{
  Database database = drc_case.database;
  if (option.pitch > 0) {
    for (RoutingLayer& routing_layer : database.get_routing_layer_list()) {
      routing_layer.set_pitch(option.pitch);
    }
  }
  std::vector<DRCShape> env_shape_list = drc_case.env_shape_list;
  std::vector<DRCShape> result_shape_list = drc_case.result_shape_list;
  if (option.reverse_shape_order) {
    std::reverse(env_shape_list.begin(), env_shape_list.end());
    std::reverse(result_shape_list.begin(), result_shape_list.end());
  }
  std::map<std::string, std::any> config_map;
  config_map.insert(std::make_pair("-temp_directory_path", (temp_root / "drc").string()));
  config_map.insert(std::make_pair("-thread_number", option.thread_number));

  DataManager::initInst();
  DRCDM.inputDatabase(config_map, std::move(database));
  RuleValidator::initInst();
  std::vector<Violation> violation_list
      = DRCRV.verify(std::move(env_shape_list), std::move(result_shape_list), drc_case.check_type_set, std::vector<DRCShape>{});
  destroyDRC();
  return formatViolationList(drc_case, violation_list);
}

// Both lists are sorted; reports the multiset difference in both directions.
bool compareLineList(const std::vector<std::string>& expected_list, const std::vector<std::string>& actual_list, const std::string& label,
                     std::vector<std::string>& failure_list)
{
  std::vector<std::string> missing_list;
  std::vector<std::string> unexpected_list;
  std::set_difference(expected_list.begin(), expected_list.end(), actual_list.begin(), actual_list.end(), std::back_inserter(missing_list));
  std::set_difference(actual_list.begin(), actual_list.end(), expected_list.begin(), expected_list.end(), std::back_inserter(unexpected_list));
  if (missing_list.empty() && unexpected_list.empty()) {
    return true;
  }
  failure_list.push_back("[" + label + "]");
  for (const std::string& line : missing_list) {
    failure_list.push_back("  missing:    " + line);
  }
  for (const std::string& line : unexpected_list) {
    failure_list.push_back("  unexpected: " + line);
  }
  return false;
}

bool checkCase(const std::string& case_path, bool print_only, const std::filesystem::path& temp_root)
{
  std::vector<std::string> failure_list;
  int32_t run_num = 0;
  try {
    DRCCase drc_case = parseDRCCase(case_path);
    std::vector<std::string> baseline_list = runCase(drc_case, {"baseline"}, temp_root);
    run_num++;
    if (print_only) {
      std::cout << "# " << case_path << ": observed violations; derive expectations from the rule, not from this output" << std::endl;
      for (const std::string& line : baseline_list) {
        std::cout << "expect " << line << std::endl;
      }
      if (baseline_list.empty()) {
        std::cout << "expect none" << std::endl;
      }
      return true;
    }
    compareLineList(drc_case.expected_violation_list, baseline_list, "expectation", failure_list);
    // The oracles below need no expectation: they compare two runs that must agree by construction.
    std::vector<RunOption> option_list = {{"thread_number=4", 4}, {"reversed shape order", 1, -1, true}};
    for (int32_t pitch : drc_case.vary_pitch_list) {
      option_list.push_back({"pitch=" + std::to_string(pitch), 1, pitch});
    }
    for (const RunOption& option : option_list) {
      compareLineList(baseline_list, runCase(drc_case, option, temp_root), "baseline vs " + option.label, failure_list);
      run_num++;
    }
  } catch (const std::exception& exception) {
    destroyDRC();
    failure_list.push_back(std::string("error: ") + exception.what());
  }
  if (failure_list.empty()) {
    std::cout << "PASS " << case_path << " (" << run_num << " runs)" << std::endl;
    return true;
  }
  std::cout << "FAIL " << case_path << std::endl;
  for (const std::string& failure : failure_list) {
    std::cout << failure << std::endl;
  }
  return false;
}

}  // namespace

int main(int argc, char** argv)
{
  // turn DRCLOG.error() into exceptions so one bad case does not hide the others
  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);

  bool print_only = false;
  std::vector<std::string> case_path_list;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--print") {
      print_only = true;
    } else {
      case_path_list.push_back(arg);
    }
  }
  if (case_path_list.empty()) {
    std::cerr << "usage: " << argv[0] << " [--print] <case.drc>..." << std::endl;
    return 2;
  }

  std::string temp_template = (std::filesystem::temp_directory_path() / "idrc_case_XXXXXX").string();
  if (mkdtemp(temp_template.data()) == nullptr) {
    std::cerr << "can not create a temporary directory" << std::endl;
    return 2;
  }
  std::filesystem::path temp_root(temp_template);

  idrc::Logger::initInst();
  DRCLOG.setQuiet(true);
  int32_t failed_num = 0;
  for (const std::string& case_path : case_path_list) {
    if (!checkCase(case_path, print_only, temp_root)) {
      failed_num++;
    }
  }
  idrc::Logger::destroyInst();
  std::filesystem::remove_all(temp_root);
  return failed_num == 0 ? 0 : 1;
}
