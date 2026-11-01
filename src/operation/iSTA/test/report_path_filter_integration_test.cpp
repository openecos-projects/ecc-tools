// SPDX-License-Identifier: MulanPSL-2.0
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "Config.hpp"
#include "Database.hpp"
#include "Logger.hpp"
#include "QorSummaryBuilder.hpp"
#include "ReportPathSelector.hpp"
#include "TimingReporter.hpp"

namespace {
using namespace ista;

void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

void checkReport(const std::string& directory, std::vector<double> slacks, bool has_less, bool has_greater, int max_paths, int nworst,
                 const std::vector<bool>& expected, const char* expected_header)
{
  Database database;
  database.pathGroups().clear();
  database.network().pins().clear();
  Config config;
  config.tr_temp_directory_path = directory + "/";
  config.output_timing_reports = 1;
  config.output_timing_features = 0;
  config.path_report_number = max_paths;
  config.endpoint_path_report_number = nworst;
  config.has_timing_report_slack_lesser_than = has_less;
  config.timing_report_slack_lesser_than = 1.0;
  config.has_timing_report_slack_greater_than = has_greater;
  config.timing_report_slack_greater_than = 0.0;
  database.network().pins()["in"].set_is_port(true);
  PathGroup group;
  group.setName("group");
  for (std::size_t i = 0; i < slacks.size(); ++i) {
    const auto end = "out" + std::to_string(i);
    database.network().pins()[end].set_is_port(true);
    TimingPath path;
    path.set_start_point("in");
    path.set_end_point(end);
    path.set_analysis_type(AnalysisType::kMax);
    path.set_slack(slacks[i]);
    group.endpointPaths()[end].push_back(path);
  }
  database.pathGroups().push_back(group);
  TimingReporter(database, config).report();
  std::ifstream file(directory + "/timing_max_in2out.rpt");
  require(file.is_open(), "timing report was not produced");
  const std::string report{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  require(report.find(expected_header) != std::string::npos, "reported slack filter disagrees with configuration");
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const bool present = report.find("Endpoint: out" + std::to_string(i) + " ") != std::string::npos;
    require(present == expected[i], "report endpoint selection disagrees with slack filter");
  }
}

void checkSelectionOrderAndLimits(const std::string& directory)
{
  Database database;
  database.pathGroups().clear();
  database.network().pins().clear();
  Config config;
  config.tr_temp_directory_path = directory + "/";
  config.output_timing_reports = 1;
  config.output_timing_features = 0;
  config.has_timing_report_slack_lesser_than = false;
  config.has_timing_report_slack_greater_than = true;
  config.timing_report_slack_greater_than = -3.5;
  config.path_report_number = 2;
  config.endpoint_path_report_number = 1;

  struct Candidate
  {
    const char* start;
    const char* end;
    double slack;
  };
  PathGroup group;
  group.setName("group");
  for (const auto& candidate : {Candidate{"filtered", "out", -4.0}, Candidate{"worst", "out", -3.0}, Candidate{"same_endpoint", "out", -2.0},
                                Candidate{"next", "other_out", -1.0}, Candidate{"beyond_max_paths", "last_out", -0.5}}) {
    database.network().pins()[candidate.start].set_is_port(true);
    database.network().pins()[candidate.end].set_is_port(true);
    TimingPath path;
    path.set_start_point(candidate.start);
    path.set_end_point(candidate.end);
    path.set_analysis_type(AnalysisType::kMax);
    path.set_slack(candidate.slack);
    group.endpointPaths()[candidate.end].push_back(path);
  }
  database.pathGroups().push_back(group);
  ReportPathSelector selector(database, config);
  const auto summary = QorSummaryBuilder(database).build(selector);
  require(summary.setup_wns_map.at("group") == -4.0 && summary.total_setup_tns == -5.5 && summary.total_setup_nvp == 3,
          "QoR incorrectly used text slack filters or path quotas instead of each endpoint's worst path");
  TimingReporter(database, config).report();
  std::ifstream file(directory + "/timing_max_in2out.rpt");
  require(file.is_open(), "ranked timing report was not produced");
  const std::string report{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  const auto worst = report.find("Startpoint: worst ");
  const auto next = report.find("Startpoint: next ");
  require(worst != std::string::npos && next != std::string::npos && worst < next,
          "report no longer selects the two worst paths from distinct endpoints in slack order");
  for (const auto* excluded : {"filtered", "same_endpoint", "beyond_max_paths"}) {
    require(report.find(std::string("Startpoint: ") + excluded + " ") == std::string::npos, "filter, endpoint quota, or global path limit changed");
  }
}
}  // namespace

int main()
{
  char pattern[] = "/tmp/ista-report-filter-XXXXXX";
  const char* directory = mkdtemp(pattern);
  require(directory != nullptr, "could not create temporary report directory");
  Logger::initInst();
  try {
    checkReport(directory, {-0.5, 0.0, 0.5}, false, false, 3, 1, {true, false, false}, "SlackLesserThan : 0.0000000000");
    checkReport(directory, {-0.5, 0.0, 0.5}, true, false, 3, 1, {true, true, true}, "SlackLesserThan : 1.0000000000");
    checkReport(directory, {-0.5, 0.0, 0.5}, false, true, 3, 1, {false, false, true}, "SlackLesserThan : infinity");
    checkReport(directory, {0.5}, false, false, 1, 1, {true}, "SlackLesserThan : infinity");
    checkReport(directory, {0.5}, false, false, 1, 2, {false}, "SlackLesserThan : 0.0000000000");
    checkSelectionOrderAndLimits(directory);
  } catch (...) {
    Logger::destroyInst();
    std::filesystem::remove_all(directory);
    throw;
  }
  Logger::destroyInst();
  std::filesystem::remove_all(directory);
}
