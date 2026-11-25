// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>

#include "Database.hpp"

#include "clock_id_helper.hpp"

using namespace ista;
void require(bool ok, const char* why)
{
  if (!ok)
    throw std::runtime_error(why);
}
int main()
{
  Database db;
  auto& receiver = db.graph().points()["io"];
  auto& driver = db.graph().driverPoints()["io"];
  receiver.setDefaultClock(clockId(db, "a"));
  driver.setDefaultClock(clockId(db, "a"));
  require(receiver.findDefaultClockState() == nullptr, "query fabricated missing default clock");
  require(receiver.get_clock_state_map().empty(), "query inserted clock state");
  auto& a = receiver.get_clock_state(clockId(db, "a"));
  a.arrival_map.set(AnalysisType::kMax, TransType::kRise, 2.0);
  a.slew_map.set(AnalysisType::kMin, TransType::kFall, 0.0);
  a.physical_slew_map.set(AnalysisType::kMin, TransType::kFall, 3.0);
  require(receiver.findDefaultClockState()->arrival(AnalysisType::kMax, TransType::kRise) == 2.0, "default clock detached from named clock");
  require(a.slew(AnalysisType::kMin, TransType::kFall) == 0.0 && !a.slew(AnalysisType::kMax, TransType::kFall),
          "missing slew became valid zero or min/max mixed");
  require(a.physical_slew_map.get(AnalysisType::kMin, TransType::kFall).value() == 3.0, "physical and effective slew mixed");
  a.arrival_map.set(AnalysisType::kMax, TransType::kRise, 4.0);
  require(receiver.findDefaultClockState()->arrival(AnalysisType::kMax, TransType::kRise) == 4.0, "named clock update left stale default state");
  receiver.setLaunchClock(clockId(db, "other_launch"));
  require(receiver.defaultClock() == clockId(db, "a") && receiver.findDefaultClockState()->arrival(AnalysisType::kMax, TransType::kRise) == 4.0,
          "data launch summary changed reference clock selection");
  require(driver.findDefaultClockState() == nullptr, "inout driver inherited receiver state");
  receiver.get_clock_state(clockId(db, "b")).arrival_map.set(AnalysisType::kMin, TransType::kFall, 7.0);
  receiver.setDefaultClock(clockId(db, "b"));
  require(receiver.findDefaultClockState()->arrival(AnalysisType::kMin, TransType::kFall) == 7.0
              && !receiver.findDefaultClockState()->arrival(AnalysisType::kMax, TransType::kRise),
          "default selection mixed clocks or transitions");
  auto& trace = receiver.get_clock_state(clockId(db, "trace"));
  trace.is_source_path_only = true;
  trace.physical_arrival_map.set(AnalysisType::kMax, TransType::kRise, 9.0);
  receiver.setDefaultClock(clockId(db, "trace"));
  require(receiver.findDefaultClockState() == nullptr && !receiver.has_clock_state(clockId(db, "trace")) && receiver.find_clock_state(clockId(db, "trace")) != nullptr,
          "source trace became an effective clock");
  receiver.setDefaultClock(clockId(db, "a"));
  Database copy = db;
  receiver.clearClockStates();
  require(receiver.findDefaultClockState() == nullptr, "default selection survived clear");
  require(copy.graph().points().at("io").findDefaultClockState()->arrival(AnalysisType::kMax, TransType::kRise) == 4.0,
          "database copy retained original clock state");
}
