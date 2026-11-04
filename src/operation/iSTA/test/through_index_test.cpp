// SPDX-License-Identifier: MulanPSL-2.0
#include <iostream>
#include <stdexcept>

#include "Database.hpp"
#include "network/Network.hpp"
#include "sdc/ExceptionMatcher.hpp"

using namespace ista;
void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}

// Independent copy of the original matching algorithm, for differential tests.
std::vector<int32_t> reference(Database& db, std::vector<int32_t> state, const std::string& sink, TransType transition, const std::string& source)
{
  auto& exceptions = db.sdc().get_path_exception_list();
  for (size_t e = 0; e < state.size() && e < exceptions.size(); ++e) {
    if (state[e] < 0)
      continue;
    int previous = std::numeric_limits<int>::min();
    const auto& groups = exceptions[e].get_through_list();
    while (size_t(state[e]) < groups.size()) {
      const auto& group = groups[state[e]];
      const auto& names = group.get_objects();
      auto pin = db.network().pins().find(sink);
      bool direct = names.contains({ExceptionObjectType::kPin, sink})
                    || (pin != db.network().pins().end()
                        && (names.contains({ExceptionObjectType::kCell, pin->second.get_instance_name()})
                            || names.contains({ExceptionObjectType::kNet, pin->second.get_net_name()})));
      int crossing = direct ? std::numeric_limits<int>::max() : std::numeric_limits<int>::min();
      for (const auto& object : names) {
        if (object.type != ExceptionObjectType::kPin)
          continue;
        const auto& name = object.name;
        if (!db.network().hierarchyPins().contains(name))
          continue;
        const auto& boundary = db.network().checkedHierarchyPin(name);
        if (!db.network().crossesHierarchyPin(boundary, source, sink))
          continue;
        int position = db.network().hierarchyCrossingPosition(boundary, source, sink);
        if (position >= previous && (crossing == std::numeric_limits<int>::min() || position < crossing))
          crossing = position;
      }
      if (crossing == std::numeric_limits<int>::min() || (group.get_trans_type() != TransType::kNone && group.get_trans_type() != transition))
        break;
      previous = crossing;
      ++state[e];
    }
  }
  return state;
}

void addPin(Database& db, const std::string& name, const std::string& instance, const std::string& net, PinDirection direction, size_t order)
{
  auto& pin = db.network().pins()[name];
  pin.set_instance_name(instance);
  pin.set_net_name(net);
  pin.set_direction(direction);
  db.network().hierarchyIndices()[instance] = order;
}
TimingExceptionThrough group(std::set<std::string> names, TransType transition = TransType::kNone)
{
  TimingExceptionThrough result;
  ExceptionObjects objects;
  for (const auto& name : names)
    objects.insert({name == "inside" ? ExceptionObjectType::kCell : name == "bus" ? ExceptionObjectType::kNet : ExceptionObjectType::kPin, name});
  result.set_objects(objects);
  result.set_trans_type(transition);
  return result;
}
void addException(Database& db, std::vector<TimingExceptionThrough> groups)
{
  TimingException exception;
  exception.set_type(TimingExceptionType::kFalsePath);
  exception.set_setup(false);
  exception.set_hold(true);
  exception.set_through_list(groups);
  db.sdc().get_path_exception_list().push_back(exception);
}
int main()
{
  try {
    Database db;
    addPin(db, "outside:Y", "outside", "bus", PinDirection::kOutput, 0);
    addPin(db, "inside:A", "inside", "bus", PinDirection::kInput, 15);
    addPin(db, "inside:Y", "inside", "bus", PinDirection::kOutput, 15);
    addPin(db, "outside:A", "outside", "bus", PinDirection::kInput, 0);
    std::set<std::string> large;
    for (int i = 0; i < 3624; ++i) {
      const std::string name = "boundary_" + std::to_string(i);
      HierarchyPin boundary;
      boundary.net = "unrelated_" + std::to_string(i);
      boundary.direction = PinDirection::kInput;
      boundary.leaf_begin = 10;
      boundary.leaf_end = 20;
      boundary.depth = 1;
      db.network().hierarchyPins()[name] = boundary;
      large.insert(name);
    }
    for (int depth : {1, 2}) {
      HierarchyPin boundary;
      boundary.net = "bus";
      boundary.direction = PinDirection::kInout;
      boundary.leaf_begin = 10;
      boundary.leaf_end = 20;
      boundary.depth = depth;
      db.network().hierarchyPins()["depth" + std::to_string(depth)] = boundary;
    }
    large.insert("depth1");
    addException(db, {group(large)});
    addException(db, {group({"depth1"}), group({"depth2"})});
    addException(db, {group({"depth2"}), group({"depth1"})});
    addException(db, {group({"inside:A", "depth2"}, TransType::kRise)});
    addException(db, {group({"inside", "bus"})});
    addException(db, {group({"depth1"}), group({"depth1"})});
    addException(db, {});
    ExceptionThroughIndex index;
    index.rebuild(db.sdc(), db.network());
    require(index.candidates(0, 0, "bus").size() == 1, "large through group did not select only same-net boundaries");
    require(index.candidates(0, 0, "absent").empty(), "unrelated net yielded candidates");
    for (auto direction : {PinDirection::kInput, PinDirection::kOutput, PinDirection::kInout}) {
      db.network().hierarchyPins().at("depth1").direction = direction;
      db.network().hierarchyPins().at("depth2").direction = direction;
      index.rebuild(db.sdc(), db.network());
      for (const auto& source : {"outside:Y", "inside:Y", "inside:A", "missing", ""}) {
        for (const auto& sink : {"outside:A", "inside:A", "outside:Y", "missing"}) {
          for (auto transition : {TransType::kRise, TransType::kFall}) {
            for (auto analysis : {AnalysisType::kMax, AnalysisType::kMin}) {
              auto state = ExceptionMatcher(db.sdc(), db.network()).initState("outside:Y", "", TransType::kRise, analysis, TransType::kRise);
              if (analysis == AnalysisType::kMax)
                require(state[0] == -1, "hold-only exception active in setup");
              for (int step = 0; step < 3; ++step) {
                auto expected = reference(db, state, sink, transition, source);
                state = ExceptionMatcher(db.sdc(), db.network()).advanceState(index, state, sink, transition, source);
                require(state == expected, "indexed matching changed through semantics");
              }
            }
          }
        }
      }
    }
    auto hold = ExceptionMatcher(db.sdc(), db.network()).initState("outside:Y", "", TransType::kRise, AnalysisType::kMin, TransType::kRise);
    auto entering = ExceptionMatcher(db.sdc(), db.network()).advanceState(index, hold, "inside:A", TransType::kRise, "outside:Y");
    require(entering[1] == 2 && entering[2] == 1, "nested input boundaries consumed out of order");
    auto leaving = ExceptionMatcher(db.sdc(), db.network()).advanceState(index, hold, "outside:A", TransType::kRise, "inside:Y");
    require(leaving[1] == 1 && leaving[2] == 2, "nested output boundaries consumed out of order");
    // Refresh must see edited constraints and changed hierarchy net assignments.
    db.network().hierarchyPins().at("depth1").net = "replacement";
    db.sdc().get_path_exception_list().resize(1);
    index.rebuild(db.sdc(), db.network());
    require(index.candidates(0, 0, "bus").empty(), "stale boundary net after refresh");
    require(index.candidates(0, 0, "replacement").size() == 1, "changed boundary net missing after refresh");
    db.sdc().get_path_exception_list().clear();
    index.rebuild(db.sdc(), db.network());
    require(index.candidates(0, 0, "replacement").empty(), "stale constraints after refresh");
    std::cout << "THROUGH_INDEX_PASSED\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
