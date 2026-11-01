// SPDX-License-Identifier: MulanPSL-2.0
#include <omp.h>
#include <unistd.h>

#include <any>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

#include "DataManager.hpp"
#include "Logger.hpp"

using namespace ista;

void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}

int main()
{
  Logger::initInst();
  DataManager::initInst();
  Config& config = STADM.getConfig();
  std::map<std::string, std::any> options;
  STADM.readConfig(options);
  require(options.empty(), "default lookup mutated input");
  require(config.temp_directory_path == "./sta_temp_directory" && config.thread_number == 128, "default execution config");
  require(config.path_report_number == 1 && config.endpoint_path_report_number == 1 && !config.is_path_report_number_specified, "default report limits");
  require(config.output_timing_reports == 1 && config.output_timing_features == 1 && config.timing_path_limit == 20 && config.min_slew_degradation == 0
              && config.timing_corner.empty(),
          "default export config");
  options = {{"-nworst", int32_t{4}}, {"-thread_number", int32_t{0}}};
  STADM.readConfig(options);
  require(config.path_report_number == 4 && !config.is_path_report_number_specified, "nworst did not raise implicit max_paths");
  require(config.thread_number == 0 && omp_get_max_threads() == 1, "OpenMP clamp changed stored thread count");
  options = {{"-max_paths", int32_t{2}},
             {"-max_path", int32_t{3}},
             {"-path_report_number", int32_t{5}},
             {"-nworst", int32_t{8}},
             {"-slack_lesser_than", 0.25},
             {"-slack_greater_than", -0.75},
             {"-temp_directory_path", std::string{"custom"}},
             {"-timing_corner", std::string{"slow"}},
             {"-thread_number", int32_t{2}},
             {"-output_timing_reports", int32_t{0}},
             {"-output_timing_features", int32_t{0}},
             {"-timing_path_limit", int32_t{9}},
             {"-min_slew_degradation", int32_t{1}}};
  STADM.readConfig(options);
  require(config.path_report_number == 5 && config.endpoint_path_report_number == 8 && config.is_path_report_number_specified,
          "alias precedence or explicit limit changed");
  require(config.has_timing_report_slack_lesser_than && config.has_timing_report_slack_greater_than && config.timing_report_slack_lesser_than == 0.25
              && config.timing_report_slack_greater_than == -0.75,
          "slack bounds changed");
  require(config.temp_directory_path == "custom" && config.timing_corner == "slow" && omp_get_max_threads() == 2 && config.output_timing_reports == 0
              && config.output_timing_features == 0 && config.timing_path_limit == 9 && config.min_slew_degradation == 1,
          "explicit config changed");
  options.clear();
  STADM.readConfig(options);
  require(!config.has_timing_report_slack_lesser_than && !config.has_timing_report_slack_greater_than && config.timing_report_slack_lesser_than == 0.25
              && config.timing_report_slack_greater_than == -0.75,
          "repeat import must disable bounds while retaining inactive values");
  options = {{"-thread_number", std::string{"wrong type"}}};
  bool rejected = false;
  try {
    STADM.readConfig(options);
  } catch (const std::bad_any_cast&) {
    rejected = true;
  }
  require(rejected, "wrong config type accepted");
  // The caller chooses an output root; only STA-owned subdirectories may be
  // refreshed. A design or unrelated note in the parent must survive init.
  const auto directory = std::filesystem::temp_directory_path() / ("ista-output-owner-" + std::to_string(getpid()));
  std::filesystem::create_directories(directory / "inputs");
  std::ofstream(directory / "inputs/design.sdc") << "create_clock -period 10 clk\n";
  std::ofstream(directory / "notes.txt") << "keep";
  std::filesystem::create_directories(directory / "timing_reporter");
  std::ofstream(directory / "timing_reporter/timing_max.rpt") << "previous run";
  options = {{"-temp_directory_path", directory.string()}};
  STADM.readConfig(options);
  STADM.buildDesign();
  const bool preserved = std::filesystem::is_regular_file(directory / "inputs/design.sdc") && std::filesystem::is_regular_file(directory / "notes.txt");
  const bool refreshed = std::filesystem::is_directory(directory / "timing_reporter") && !std::filesystem::exists(directory / "timing_reporter/timing_max.rpt");
  Logger::getInst().closeLogFileStream();
  std::filesystem::remove_all(directory);
  require(preserved, "STA initialization removed unrelated files from the output root");
  require(refreshed, "STA-owned report directory retained stale results");
  DataManager::destroyInst();
  Logger::destroyInst();
}
