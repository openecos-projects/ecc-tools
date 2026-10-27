// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <stdexcept>
#include <string>

#include "Config.hpp"
#include "Database.hpp"
#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "TimingAnalyzer.hpp"

using namespace ista;

static void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

static void makeDesign(Database& database, double input_delay, double output_delay)
{
  auto& network = database.network();
  for (const auto* name : {"in", "out"}) {
    auto& pin = network.pins()[name];
    pin.set_full_name(name);
    pin.set_pin_name(name);
    pin.set_is_port(true);
    pin.set_direction(std::string(name) == "in" ? PinDirection::kInput : PinDirection::kOutput);
    pin.set_net_name("wire");
  }
  auto& net = network.nets()["wire"];
  net.set_net_name("wire");
  net.get_pin_name_list() = {"in", "out"};
  auto& clock = database.sdc().get_clock_map()["clock"];
  clock.set_clock_name("clock");
  clock.set_period(10.0);
  clock.set_fall_edge(5.0);
  std::vector<TimingIoDelay> input_delays, output_delays;
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    TimingIoDelay delay;
    delay.set_clock_name("clock");
    delay.set_trans_type(edge);
    delay.set_delay(input_delay);
    input_delays.push_back(delay);
    delay.set_delay(output_delay);
    output_delays.push_back(delay);
  }
  database.sdc().get_port_constraint_map()["in"].set_input_delays(input_delays, false);
  database.sdc().get_port_constraint_map()["out"].set_output_delays(output_delays, false);
}

static void check(Database& database, double arrival, double required)
{
  require(database.graph().arcs().size() == 1, "analysis did not rebuild one wire arc");
  if (database.graph().endpoints().size() != 1)
    throw std::runtime_error("expected one endpoint, found " + std::to_string(database.graph().endpoints().size()));
  const auto& endpoint = database.graph().points().at("out");
  require(std::abs(endpoint.get_arrival() - arrival) < 1E-9, "analysis read another database or skipped data propagation");
  require(std::abs(endpoint.get_required() - required) < 1E-9, "required propagation skipped output constraint");
  bool found = false;
  for (const auto& group : database.pathGroups()) {
    for (const auto& [name, paths] : group.endpointPaths()) {
      for (const auto& path : paths) {
        if (path.get_analysis_type() == AnalysisType::kMax) {
          require(std::abs(path.get_slack() - (required - arrival)) < 1E-9, "published path lost timing result");
          found = true;
        }
      }
    }
  }
  require(found, "analysis did not publish max path");
}

int main()
{
  Logger::initInst();
  Database first, second;
  Config config;
  makeDesign(first, 1.0, 2.0);
  makeDesign(second, 3.0, 4.0);
  DelayCalculator first_delay(first, config), second_delay(second, config);
  TimingAnalyzer first_analysis(first, first_delay), second_analysis(second, second_delay);
  first_analysis.analyze();
  second_analysis.analyze();
  check(first, 1.0, 8.0);
  check(second, 3.0, 6.0);
  const auto groups = first.pathGroups().size();
  auto updated = first.sdc().get_port_constraint_map().at("in").get_input_delay_list();
  for (auto& delay : updated)
    delay.set_delay(2.0);
  first.sdc().get_port_constraint_map().at("in").set_input_delays(updated, false);
  first_analysis.analyze();
  check(first, 2.0, 8.0);
  check(second, 3.0, 6.0);
  require(first.pathGroups().size() == groups, "repeat analysis appended stale path groups");
  Logger::destroyInst();
}
