// SPDX-License-Identifier: MulanPSL-2.0
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "Database.hpp"
#include "FanoutChecker.hpp"
#include "network/Network.hpp"
#include "sdc/ExceptionMatcher.hpp"

namespace {
using namespace ista;

void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

void pin(Database& db, const std::string& name, const std::string& instance, const std::string& port, bool external = false)
{
  auto& value = db.network().pins()[name];
  value.set_full_name(name);
  value.set_instance_name(instance);
  value.set_pin_name(port);
  value.set_is_port(external);
  value.set_net_name("n");
}

void testFanout()
{
  LibertyPort default_pin;
  require(default_pin.get_fanout_load() == 1., "missing fanout_load must represent one input load");
  default_pin.set_fanout_load(0.);
  require(default_pin.get_fanout_load() == 0., "explicit zero fanout_load lost");
  Database db;
  pin(db, "in", "", "in", true);
  pin(db, "out", "", "out", true);
  pin(db, "drv:Y", "drv", "Y");
  pin(db, "sink:A", "sink", "A");
  pin(db, "bus:IO", "bus", "IO");
  pin(db, "unknown:A", "unknown", "A");
  pin(db, "unknown:Y", "unknown", "Y");
  pin(db, "disabled:Y", "disabled", "Y");
  db.graph().simValues().setDriverDisabled("disabled:Y", true);
  for (const auto& name : {"drv", "sink", "bus"})
    db.network().instances()[name].set_cell_name(name);
  auto& cells = db.liberty().get_cell_map();
  cells["drv"].get_port_map()["Y"].set_max_fanout(2.0);
  cells["sink"].get_port_map()["A"].set_fanout_load(2.0);
  cells["bus"].get_port_map()["IO"].set_fanout_load(1.5);
  auto& net = db.network().nets()["n"];
  net.set_load_pin_list({"sink:A", "sink:A", "bus:IO", "unknown:A", "out"});
  net.set_driver_pin_list({"in", "drv:Y", "bus:IO", "disabled:Y", "in"});
  net.set_driver_pin("unknown:Y");
  auto& sdc = db.sdc();
  sdc.get_max_fanout() = 4.0;
  sdc.get_port_max_fanout_map()["in"] = 3.0;
  db.fanoutChecks().resize(1);  // Evaluation must not publish into Database.
  const FanoutChecker check(db);
  const auto checks = check.check();
  require(checks.size() == 4, "fanout drivers must be unique and enabled");
  const std::vector<std::string> drivers{"drv:Y", "in", "unknown:Y", "bus:IO"};
  const std::vector<double> loads{4.5, 4.5, 4.5, 3.0};
  const std::vector<double> limits{2.0, 3.0, 4.0, 4.0};
  for (std::size_t i = 0; i < checks.size(); ++i) {
    require(checks[i].get_driver_pin() == drivers[i] && checks[i].get_net_name() == "n", "fanout result order changed");
    require(checks[i].get_fanout_load() == loads[i] && checks[i].get_limit() == limits[i], "fanout load or limit changed");
  }
  require(db.fanoutChecks().size() == 1, "fanout evaluator wrote published results");
  sdc.get_max_fanout().reset();
  require(check.check().size() == 2, "unconstrained drivers or stale checks retained");
  sdc.get_port_max_fanout_map()["in"] = 0.0;
  require(check.check().front().get_driver_pin() == "in" && check.check().front().get_limit() == 0.0, "zero fanout limit treated as unset");
  // Missing library cell and missing library port use the same fallback as a
  // missing instance; this does not replace the library reader's defaulting.
  db.network().instances()["unknown"].set_cell_name("absent");
  require(check.check().front().get_fanout_load() == 4.5, "missing library cell fallback changed");
  db.network().instances()["unknown"].set_cell_name("drv");
  require(check.check().front().get_fanout_load() == 4.5, "missing library port fallback changed");
  Database other;
  require(FanoutChecker(other).check().empty(), "fanout crossed database lifetimes");
}

void testExceptions()
{
  Database db;
  pin(db, "in", "", "in", true);
  pin(db, "out", "", "out", true);
  auto& sdc = db.sdc();
  auto& exceptions = sdc.get_path_exception_list();
  TimingException broad;
  broad.set_type(TimingExceptionType::kMaxDelay);
  broad.set_from_objects({{ExceptionObjectType::kClock, "launch"}});
  broad.set_to_objects({{ExceptionObjectType::kClock, "capture"}});
  broad.set_delay(8.0);
  exceptions.push_back(broad);
  broad.set_delay(6.0);
  exceptions.push_back(broad);
  TimingException concrete = broad;
  concrete.set_from_objects({{ExceptionObjectType::kPort, "in"}});
  concrete.set_to_objects({{ExceptionObjectType::kPort, "out"}});
  concrete.set_delay(7.0);
  exceptions.push_back(concrete);
  TimingException multi = concrete;
  multi.set_type(TimingExceptionType::kMulticycle);
  multi.set_setup_multiplier(3);
  exceptions.push_back(multi);
  TimingException minimum = concrete;
  minimum.set_type(TimingExceptionType::kMinDelay);
  minimum.set_delay(2.0);
  exceptions.push_back(minimum);
  const ExceptionMatcher matcher(sdc, db.network());
  const auto resolve = [&](AnalysisType analysis) {
    const auto state = matcher.initState("in", "launch", TransType::kRise, analysis, TransType::kRise);
    return matcher.resolve("in", "launch", "out", "capture", analysis, TransType::kRise, TransType::kRise, state);
  };
  auto result = resolve(AnalysisType::kMax);
  require(!result.false_path && result.path_delay && result.path_delay->get_delay() == 7.0, "concrete selector priority changed");
  require(result.setup_multicycle && result.setup_multicycle->get_setup_multiplier() == 3, "multicycle match lost");
  require(resolve(AnalysisType::kMin).path_delay->get_delay() == 2.0, "min/max exception analyses mixed");
  exceptions.erase(exceptions.begin() + 2);
  require(resolve(AnalysisType::kMax).path_delay->get_delay() == 6.0, "equal-specificity delay selection changed");
  TimingException cut = concrete;
  cut.set_type(TimingExceptionType::kFalsePath);
  cut.set_setup(false);
  exceptions.push_back(cut);
  require(!resolve(AnalysisType::kMax).false_path && resolve(AnalysisType::kMin).false_path, "hold-only false path lost");
  exceptions.pop_back();
  TimingClockGroup groups;
  groups.set_groups({{"launch"}, {"capture"}});
  sdc.get_clock_group_list().push_back(groups);
  require(resolve(AnalysisType::kMax).false_path, "clock group cut lost");
  sdc.get_clock_group_list().back().set_allow_paths(true);
  require(!resolve(AnalysisType::kMax).false_path, "allow_paths ignored");
}
}  // namespace

int main()
{
  // Deliberately initialize no singleton, Tcl interpreter or path search.
  testFanout();
  testExceptions();
}
