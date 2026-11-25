// SPDX-License-Identifier: MulanPSL-2.0
// Fixture helper for the ClockId-keyed clock-state API: resolve a clock name
// to its id in the SDC clock table, registering a minimal fixture clock when
// the name is not a defined clock yet.
#pragma once

#include <string_view>

#include "Database.hpp"

#include "graph/TimingStateQuery.hpp"

namespace ista {

inline ClockId clockId(Database& db, std::string_view name)
{
  auto& clocks = db.sdc().get_clock_map();
  if (const auto id = clocks.id(name))
    return *id;
  clocks[name].set_clock_name(std::string(name));
  return clocks.id(name).value();
}

// TimingPathState identity fields are interned ids now.
inline ecc::symbol::Id<PinTag> pinId(Database& db, std::string_view name)
{
  return db.network().pins().id(name).value();
}
inline LaunchClockId launchClockId(Database& db, std::string_view name)
{
  return LaunchClockId{db.clockNameSymbols().intern(name)};
}
inline StartPointId startPointId(Database& db, std::string_view name)
{
  return StartPointId{db.startPointSymbols().intern(name)};
}

}  // namespace ista
