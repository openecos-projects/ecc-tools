// SPDX-License-Identifier: MulanPSL-2.0
//
// P0 of the run_sta execution-side refactor: lock clock-name ordering at the
// TimingPoint clock-state boundary before _clock_state_map stops being a
// std::map<std::string, ClockState>.
//
// Today the per-point clock map iterates name-lexicographic regardless of SDC
// definition order, and ClockNetworkBuilder's orderSources walks the clock
// table in name order, so the default clock of a point is the name-first
// clock reaching it. The planned ClockId-keyed map iterates in id (= SDC
// definition) order instead, so name order must be reconstructed explicitly
// at order-sensitive sites. Clocks here are defined in reverse name order on
// purpose.
#include <stdexcept>
#include <string>
#include <vector>

#include "Database.hpp"
#include "DelayCalculator.hpp"
#include "TimingAnalyzer.hpp"

using namespace ista;

namespace {

void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

}  // namespace

int main()
{
  Database db;
  Config config;
  for (const auto* name : {"in", "out"}) {
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_pin_name(name);
    pin.set_is_port(true);
    pin.set_net_name("wire");
    pin.set_direction(std::string(name) == "in" ? PinDirection::kInput : PinDirection::kOutput);
  }
  auto& net = db.network().nets()["wire"];
  net.set_net_name("wire");
  net.set_pin_name_list({"in", "out"});
  // SDC definition order is deliberately the reverse of name order.
  for (const auto* name : {"zclk", "mclk", "aclk"}) {
    auto& clock = db.sdc().get_clock_map()[name];
    clock.set_clock_name(name);
    clock.set_period(10);
    clock.set_rise_edge(0);
    clock.set_fall_edge(5);
    clock.set_source_list({"in"});
  }
  db.sdc().get_port_constraint_map()["in"].set_input_transition(.05);
  DelayCalculator delay(db, config);
  TimingAnalyzer(db, delay).analyze();

  // markClock skips output ports with no fanout by design ("no internal data
  // fanout to analyze"), so the observable point here is the source pin "in".
  auto& point = db.graph().points()["in"];
  std::vector<std::string> iterated;
  for (const auto& [clock_name, state] : point.get_clock_state_map())
    iterated.push_back(clock_name);
  std::string actual;
  for (const auto& name : iterated)
    actual += name + ",";
  require((iterated == std::vector<std::string>{"aclk", "mclk", "zclk"}),
          (std::string("per-point clock state iteration left name order: ") + actual).c_str());

  // The default clock is the name-first clock reaching the point, not the
  // first-defined one.
  require(point.defaultClock() == "aclk", "default clock followed SDC definition order instead of name order");
  require(point.findDefaultClockState() != nullptr, "default clock lost its clock state");

  // Clock-state lookup by every name works and returns distinct states.
  for (const auto* name : {"zclk", "mclk", "aclk"})
    require(point.has_clock_state(name), "clock state missing after propagation");
}
