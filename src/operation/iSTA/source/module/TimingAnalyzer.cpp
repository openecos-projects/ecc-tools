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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "TimingAnalyzer.hpp"

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "ClockChecker.hpp"
#include "ClockPropagator.hpp"
#include "DataPropagator.hpp"
#include "DelayCalculator.hpp"
#include "ElectricalChecker.hpp"
#include "FanoutChecker.hpp"
#include "GraphBuilder.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "PathAnalyzer.hpp"
#include "PathChecker.hpp"
#include "RequiredPropagator.hpp"
#include "StructuralTimingStateBuilder.hpp"

namespace ista {

namespace {

template <typename T>
void mixValue(std::uint64_t& hash, T value)
{
  static_assert(std::is_integral_v<T>);
  using Unsigned = std::make_unsigned_t<T>;
  Unsigned bits = static_cast<Unsigned>(value);
  for (std::size_t index = 0; index < sizeof(T); ++index) {
    hash ^= static_cast<std::uint8_t>(bits & 0xffU);
    hash *= 1099511628211ULL;
    bits >>= 8U;
  }
}

void mixString(std::uint64_t& hash, std::string_view value)
{
  mixValue(hash, value.size());
  for (const unsigned char character : value) {
    hash ^= character;
    hash *= 1099511628211ULL;
  }
}

std::uint64_t structureSignature(const Database& database)
{
  std::uint64_t hash = 1469598103934665603ULL;
  const Network& network = database.network();

  mixString(hash, network.name());
  mixValue(hash, network.pins().size());
  for (const auto& [name, pin] : network.pins()) {
    mixString(hash, name);
    mixValue(hash, static_cast<std::uint8_t>(pin.get_direction()));
    mixString(hash, pin.get_instance_name());
    mixString(hash, pin.get_net_name());
    mixValue(hash, static_cast<std::uint8_t>(pin.get_is_port()));
  }

  mixValue(hash, network.instances().size());
  for (const auto& [name, instance] : network.instances()) {
    mixString(hash, name);
    mixString(hash, instance.get_cell_name());
    mixValue(hash, instance.get_pin_name_list().size());
    for (const auto& pin_name : instance.get_pin_name_list())
      mixString(hash, pin_name);
    mixValue(hash, static_cast<std::uint8_t>(instance.get_is_sequential()));
    mixValue(hash, static_cast<std::uint8_t>(instance.get_is_clock_gating()));
    mixString(hash, instance.get_clock_pin_name());
    mixString(hash, instance.get_output_pin_name());
    mixString(hash, instance.get_data_pin_name());
  }

  mixValue(hash, network.nets().size());
  for (const auto& [name, net] : network.nets()) {
    mixString(hash, name);
    mixString(hash, net.get_driver_pin());
    mixValue(hash, net.get_driver_pin_list().size());
    for (const auto& pin_name : net.get_driver_pin_list())
      mixString(hash, pin_name);
    mixValue(hash, net.get_load_pin_list().size());
    for (const auto& pin_name : net.get_load_pin_list())
      mixString(hash, pin_name);
    mixValue(hash, net.get_pin_name_list().size());
    for (const auto& pin_name : net.get_pin_name_list())
      mixString(hash, pin_name);
  }

  mixValue(hash, network.hierarchyPins().size());
  for (const auto& [name, hierarchy_pin] : network.hierarchyPins()) {
    mixString(hash, name);
    (void) hierarchy_pin;
  }

  return hash;
}

}  // namespace

bool TimingAnalyzer::isStructureCurrent() const
{
  const auto signature = _database.graph().structureSignature();
  return signature != 0 && signature == structureSignature(_database);
}

void TimingAnalyzer::prepareStructure()
{
  auto measurement = _database.timingExecutionStats().measure(TimingPhase::kStructuralPreparation);
  _delay.clear();
  _database.graph() = Graph{};
  GraphBuilder(_database).build();
  _database.graph().setStructureSignature(structureSignature(_database));
  StructuralTimingStateBuilder::build(_database, _delay);
}

void TimingAnalyzer::analyze(bool reuse_structure)
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");
  Database& database = _database;
  _delay.clear();
  if (!reuse_structure) {
    database.graph() = Graph{};
  } else {
    database.graph().clearTiming();
  }
  database.pathGroups().clear();
  database.pathStats() = PathStats{};
  database.fanoutChecks().clear();
  database.electricalChecks().clear();
  database.clockChecks().clear();
  const bool can_reuse_structure = reuse_structure && isStructureCurrent();
  if (!can_reuse_structure) {
    database.graph() = Graph{};
    GraphBuilder(database).build();
    database.graph().setStructureSignature(structureSignature(database));
  }
  _delay.initialize(database.graph());

  database.graph().clearTiming();
  ClockPropagator(database, _delay).propagate();
  DataPropagator(database, _delay).propagate();

  {
    PathChecker check(database, _delay);
    RequiredPropagator(database, check).propagate();
    PathAnalyzer(database, check).analyze();
  }
  database.fanoutChecks() = FanoutChecker(database).check();
  database.electricalChecks() = ElectricalChecker(database, _delay).check();
  database.clockChecks() = ClockChecker(database).check();
  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}
}  // namespace ista
