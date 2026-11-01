// SPDX-License-Identifier: MulanPSL-2.0
#include "ElectricalChecker.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

#include "DelayCalculator.hpp"
#include "graph/TimingStateQuery.hpp"

namespace ista {
namespace {
void restrictLimit(std::optional<double>& limit, std::optional<double> candidate)
{
  if (candidate && (!limit || *candidate < *limit))
    limit = candidate;
}
template <typename Library>
double slewRange(const Library& library, TransType edge)
{
  auto normalize = [](double threshold) { return threshold > 1. ? threshold * .01 : threshold; };
  const double lower = edge == TransType::kRise ? library.get_slew_lower_threshold_pct_rise() : library.get_slew_lower_threshold_pct_fall();
  const double upper = edge == TransType::kRise ? library.get_slew_upper_threshold_pct_rise() : library.get_slew_upper_threshold_pct_fall();
  return (normalize(upper) - normalize(lower)) / library.get_slew_derate_from_library();
}
std::set<std::string> dataClocks(TimingPoint& point, AnalysisType analysis, TransType edge)
{
  std::set<std::string> clocks;
  auto mode = point.get_path_state_map().find(analysis);
  if (mode != point.get_path_state_map().end())
    for (const auto& [source, edges] : mode->second) {
      const auto transition = edges.find(edge);
      if (transition != edges.end())
        for (const auto& [tag, state] : transition->second)
          clocks.insert(state.get_clock_name());
    }
  if (clocks.empty())
    clocks.insert("");
  return clocks;
}
}  // namespace

std::optional<double> ElectricalChecker::limit(ElectricalRule rule, const Pin& pin, const std::string& clock, bool clock_path, TransType edge)
{
  auto& db = _database;
  LibertyCell* cell = nullptr;
  std::string lib_pin;
  const auto instance = db.network().instances().find(pin.get_instance_name());
  if (!pin.get_is_port() && instance != db.network().instances().end()) {
    const auto found = db.liberty().get_cell_map().find(instance->second.get_cell_name());
    if (found != db.liberty().get_cell_map().end()) {
      cell = &found->second;
      lib_pin = cell->get_library_name() + "/" + found->first + "/" + pin.get_pin_name();
    }
  }
  auto result = db.sdc().electricalLimits().limit(rule, pin.get_full_name(), pin.get_is_port(), lib_pin, clock, clock_path, edge);
  if (rule == ElectricalRule::kTransition && result && cell) {
    // SDC transition limits use the main library's trip points. The stored
    // waveform and the cell's Liberty limit use the receiving pin's basis.
    const double main_range = slewRange(db.liberty(), edge), pin_range = slewRange(*cell, edge);
    if (std::isfinite(main_range) && main_range > 0. && std::isfinite(pin_range) && pin_range > 0.)
      *result *= pin_range / main_range;
  }
  if (!db.sdc().electricalLimits().forcedLimit(rule, pin.get_full_name(), pin.get_is_port()))
    if (const auto* port = db.network().libertyPort(db.liberty(), pin))
      restrictLimit(result, rule == ElectricalRule::kTransition ? port->get_max_transition() : port->get_max_capacitance());
  if (pin.get_is_port() && !db.sdc().electricalLimits().forcedLimit(rule, pin.get_full_name(), true))
    for (const auto& [driver_cell, output] : db.sdc().drivingCellPorts(db.liberty(), pin.get_full_name())) {
      auto inherited = rule == ElectricalRule::kTransition ? output->get_max_transition() : output->get_max_capacitance();
      if (inherited && rule == ElectricalRule::kTransition) {
        const double main_range = slewRange(db.liberty(), edge), drive_range = slewRange(*driver_cell, edge);
        if (std::isfinite(main_range) && main_range > 0. && std::isfinite(drive_range) && drive_range > 0.)
          *inherited *= main_range / drive_range;
      }
      restrictLimit(result, inherited);
    }
  return result;
}

std::vector<ElectricalCheck> ElectricalChecker::check()
{
  auto& db = _database;
  std::map<std::pair<ElectricalRule, std::string>, ElectricalCheck> worst;
  auto record = [&](ElectricalRule rule, const Pin& pin, const std::string& clock, bool clock_path, AnalysisType analysis, TransType edge, double actual,
                    std::optional<double> maximum) {
    if (!maximum || !std::isfinite(*maximum) || !std::isfinite(actual))
      return;
    ElectricalCheck check{rule, pin.get_full_name(), pin.get_net_name(), clock, analysis, edge, clock_path, actual, *maximum};
    const auto key = std::make_pair(rule, check.pin);
    const auto previous = worst.find(key);
    if (previous == worst.end() || check.slack() < previous->second.slack())
      worst.insert_or_assign(key, std::move(check));
  };
  // A bidirectional pin has separate driver/receiver waveforms; both compete
  // for the single reported transition slack of the physical pin.
  for (auto& vertex : db.graph().vertices()) {
    const auto& pin = db.network().pins().at(vertex.get_pin_name());
    if (isIdealClockPin(db, pin.get_full_name()))
      continue;
    auto& point = vertex.get_timing_point();
    for (const auto& [analysis, edges] : point.get_data_slew_map())
      for (const auto& [edge, slew] : edges)
        for (const auto& clock : dataClocks(point, analysis, edge))
          record(ElectricalRule::kTransition, pin, clock, false, analysis, edge, slew, limit(ElectricalRule::kTransition, pin, clock, false, edge));
    for (const auto& [clock, state] : point.get_clock_state_map())
      for (const auto& [analysis, edges] : state.physical_slew_map)
        for (const auto& [edge, slew] : edges)
          record(ElectricalRule::kTransition, pin, clock, true, analysis, edge, slew, limit(ElectricalRule::kTransition, pin, clock, true, edge));
  }
  for (const auto& [name, net] : db.network().nets()) {
    std::set<std::string> drivers(net.get_driver_pin_list().begin(), net.get_driver_pin_list().end());
    if (!net.get_driver_pin().empty())
      drivers.insert(net.get_driver_pin());
    for (const auto& driver : drivers) {
      if (db.graph().simValues().isDriverDisabled(driver) || isIdealClockPin(db, driver))
        continue;
      const auto& pin = db.network().pins().at(driver);
      const bool separate_driver = db.graph().driverPoints().contains(driver);
      if (!db.graph().simValues().allowsTransition(driver, TransType::kRise, separate_driver)
          && !db.graph().simValues().allowsTransition(driver, TransType::kFall, separate_driver))
        continue;
      if (!db.graph().points().contains(driver) && !separate_driver)
        continue;
      auto& point = db.graph().driverPoint(driver);
      for (auto analysis : {AnalysisType::kMin, AnalysisType::kMax})
        for (auto edge : {TransType::kRise, TransType::kFall}) {
          const double capacitance = _delay.pinLoad(driver, analysis, edge);
          auto evaluate = [&](const std::string& clock, bool clock_path) {
            auto maximum = limit(ElectricalRule::kCapacitance, pin, clock, clock_path, edge);
            // A limit on a load pin or output port constrains the total net
            // capacitance seen by its driver, including pin and wire loads.
            if (!db.sdc().electricalLimits().forcedLimit(ElectricalRule::kCapacitance, pin.get_full_name(), pin.get_is_port()))
              for (const auto& load : net.get_load_pin_list())
                restrictLimit(maximum, limit(ElectricalRule::kCapacitance, db.network().pins().at(load), clock, clock_path, edge));
            record(ElectricalRule::kCapacitance, pin, clock, clock_path, analysis, edge, capacitance, maximum);
            // A cell inout in receiver mode also reports the external driver's
            // net-capacitance check. Reuse that driver's load, which includes
            // the receiving PAD capacitance; evaluating PAD as a driver would
            // subtract its own capacitance. Unknown OE retains both roles.
            for (const auto& load : net.get_load_pin_list()) {
              const auto& receiver = db.network().pins().at(load);
              if (load == driver || receiver.get_is_port() || receiver.get_direction() != PinDirection::kInout
                  || db.graph().simValues().driverDisabled(load) == false || isIdealClockPin(db, load) || !db.graph().simValues().allowsTransition(load, edge))
                continue;
              const auto forced = db.sdc().electricalLimits().forcedLimit(ElectricalRule::kCapacitance, load, false);
              record(ElectricalRule::kCapacitance, receiver, clock, clock_path, analysis, edge, capacitance, forced ? forced : maximum);
            }
          };
          for (const auto& clock : dataClocks(point, analysis, edge))
            evaluate(clock, false);
          for (const auto& [clock, state] : point.get_clock_state_map())
            evaluate(clock, true);
        }
    }
  }
  std::vector<ElectricalCheck> result;
  for (auto& [key, check] : worst)
    result.push_back(std::move(check));
  std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
    return std::tuple{left.rule, left.slack(), left.pin} < std::tuple{right.rule, right.slack(), right.pin};
  });
  return result;
}
}  // namespace ista
