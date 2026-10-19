// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
#include <limits>
#include <string>
#include <string_view>
#include <vector>
namespace ista {
class Database;
class TimingClock;
struct HierarchyPin;
const HierarchyPin& checkedHierarchyPin(Database& database, const std::string& name);
bool crossesHierarchyPin(Database& database, const HierarchyPin& boundary, std::string_view driver, std::string_view load);
int hierarchyCrossingPosition(Database& database, const HierarchyPin& boundary, std::string_view driver, std::string_view load);
std::vector<std::string> hierarchyDrivers(Database& database, const std::string& name);
void setClockSources(Database& database, TimingClock& clock, const std::vector<std::string>& objects);
bool clockAllowsNetArc(Database& database, const std::string& clock, const std::string& driver, const std::string& load,
                       int before = std::numeric_limits<int>::max());
bool matchesClockSource(Database& database, const std::string& source, const std::string& pin);
}  // namespace ista
