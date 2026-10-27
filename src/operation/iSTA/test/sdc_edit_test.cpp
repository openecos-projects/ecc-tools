// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>
#include <string>
#include <vector>

#include "network/Network.hpp"
#include "sdc/Sdc.hpp"

using namespace ista;

static void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

static TimingClock clock(const char* name)
{
  TimingClock result;
  result.set_clock_name(name);
  result.set_period(10.0);
  result.set_rise_edge(1.0);
  result.set_fall_edge(4.0);
  result.set_waveform({1.0, 4.0});
  return result;
}

static void clocks()
{
  Network network;
  Sdc sdc;
  sdc.defineClock(network, clock("first"), {"a", "b"}, false);
  sdc.defineClock(network, clock("second"), {"b"}, true);
  require(sdc.get_clock_map().at("first").get_source_objects() == std::vector<std::string>{"a", "b"}, "add removed another clock source");
  sdc.defineClock(network, clock("third"), {"b"}, false);
  require(sdc.get_clock_map().at("first").get_source_objects() == std::vector<std::string>{"a"} && !sdc.get_clock_map().contains("second"),
          "replacement did not remove only matching sources");
  sdc.defineClock(network, clock("third"), {"c"}, false);
  require(sdc.get_clock_map().size() == 2 && sdc.get_clock_map().at("third").get_source_objects() == std::vector<std::string>{"c"},
          "same-name replacement retained old sources");

  GeneratedClockDefinition definition;
  definition.master_source = "a";
  definition.targets = {"div"};
  definition.divide_by = 2;
  auto divided = sdc.makeGeneratedClock("divided", definition, "divide");
  require(divided.get_master_clock_name() == "first" && divided.get_period() == 20.0 && divided.get_rise_edge() == 1.0 && divided.get_fall_edge() == 11.0
              && divided.get_comment() == "divide",
          "generated clock computation changed");
  require(sdc.get_clock_map().size() == 2, "clock evaluation published a partial clock");
  sdc.defineClock(network, divided, definition.targets, false);
  definition.master_clock = "divided";
  definition.divide_by.reset();
  definition.multiply_by = 2;
  definition.duty_cycle = 25;
  const auto multiplied = sdc.makeGeneratedClock("multiplied", definition, "");
  require(multiplied.get_period() == 10.0 && multiplied.get_rise_edge() == 0.5 && multiplied.get_fall_edge() == 3.0, "multiply/duty waveform changed");

  auto rejected = [&](const char* name, const GeneratedClockDefinition& invalid, const char* expected) {
    bool failed = false;
    try {
      sdc.makeGeneratedClock(name, invalid, "");
    } catch (const std::invalid_argument& error) {
      failed = std::string(error.what()) == expected;
    }
    require(failed, expected);
    require(sdc.get_clock_map().size() == 3 && sdc.get_clock_map().at("first").get_period() == 10.0, "failed evaluation changed constraints");
  };
  rejected("first", definition, "generated clock dependency cycle");
  definition.master_clock = "";
  rejected("bad", definition, "generated clock master does not exist");
  definition.master_clock = "first";
  definition.multiply_by.reset();
  definition.duty_cycle.reset();
  definition.edges = std::vector<double>{};
  rejected("bad", definition, "-edges requires an odd number of at least three values and matching -edge_shift");
  definition.edges = std::vector<double>{1, 2, 3};
  definition.edge_shifts = std::vector<double>{};
  rejected("bad", definition, "-edges requires an odd number of at least three values and matching -edge_shift");
  definition.edge_shifts = std::vector<double>{0.5, 1.0, 2.0};
  const auto shifted = sdc.makeGeneratedClock("shifted", definition, "");
  require(shifted.get_rise_edge() == 1.5 && shifted.get_fall_edge() == 5.0 && shifted.get_period() == 11.5, "edge transformation changed");
  definition.edges.reset();
  definition.edge_shifts.reset();
  definition.divide_by = 2;
  definition.master_clock.reset();
  sdc.defineClock(network, clock("ambiguous"), {"a"}, true);
  bool ambiguous = false;
  try {
    sdc.makeGeneratedClock("bad", definition, "");
  } catch (const std::invalid_argument& error) {
    ambiguous = std::string(error.what()) == "ambiguous master; specify -master_clock";
  }
  require(ambiguous, "ambiguous master was silently selected");
}

static void exceptions()
{
  Sdc sdc;
  TimingException exception;
  exception.set_from_objects({{ExceptionObjectType::kPort, "a"}, {ExceptionObjectType::kPort, "b"}});
  exception.set_to_objects({{ExceptionObjectType::kPort, "x"}, {ExceptionObjectType::kPort, "y"}});
  TimingExceptionThrough through;
  through.set_objects({{ExceptionObjectType::kPin, "m"}, {ExceptionObjectType::kPin, "n"}});
  exception.set_through_list({through});
  sdc.get_path_exception_list().push_back(exception);
  TimingException filter;
  filter.set_from_objects({{ExceptionObjectType::kPort, "a"}});
  filter.set_to_objects({{ExceptionObjectType::kPort, "x"}});
  through.set_objects({{ExceptionObjectType::kPin, "m"}});
  filter.set_through_list({through});
  filter.set_hold(false);
  filter.set_fall(false);
  sdc.resetPathExceptions(filter);
  const auto& remaining = sdc.get_path_exception_list();
  require(remaining.size() == 9, "exception cross-product subtraction changed");
  require(remaining[0].get_from_objects() == ExceptionObjects{{ExceptionObjectType::kPort, "a"}}
              && remaining[0].get_to_objects() == ExceptionObjects{{ExceptionObjectType::kPort, "x"}} && remaining[0].get_rise() && !remaining[0].get_fall()
              && !remaining[0].get_setup() && remaining[0].get_hold(),
          "rise/hold remainder or ordering changed");
  require(!remaining[1].get_rise() && remaining[1].get_fall() && remaining[1].get_setup() && remaining[1].get_hold(), "fall remainder lost setup or hold");
  require(remaining[2].get_through_list()[0].get_objects() == ExceptionObjects{{ExceptionObjectType::kPin, "n"}}
              && remaining.back().get_from_objects() == ExceptionObjects{{ExceptionObjectType::kPort, "b"}},
          "object expansion ordering changed");
  exception.set_type(TimingExceptionType::kMulticycle);
  exception.set_setup_multiplier(3);
  exception.set_hold_multiplier(0);
  exception.set_from_objects({{ExceptionObjectType::kPort, "a"}});
  exception.set_to_objects({{ExceptionObjectType::kPort, "x"}});
  exception.set_through_list({});
  sdc.get_path_exception_list() = {exception};
  filter = TimingException{};
  filter.set_hold(false);
  sdc.resetPathExceptions(filter);
  require(sdc.get_path_exception_list().size() == 2, "multicycle transition splitting changed");
  for (const auto& item : sdc.get_path_exception_list())
    require(!item.get_setup_multiplier() && item.get_hold_multiplier() == 0 && !item.get_setup() && item.get_hold(), "implicit zero hold removed");
}

int main()
{
  // No Tcl interpreter, DataManager singleton or propagated timing is required.
  clocks();
  exceptions();
}
