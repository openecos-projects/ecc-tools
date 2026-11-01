// SPDX-License-Identifier: MulanPSL-2.0
#include "ObjectCatalog.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
namespace ista::sdc {
namespace {
std::string directionName(PinDirection direction)
{
  switch (direction) {
    case PinDirection::kInput:
      return "in";
    case PinDirection::kOutput:
      return "out";
    case PinDirection::kInout:
      return "inout";
    default:
      return "internal";
  }
}

std::string leafName(std::string name)
{
  const std::size_t separator = name.find_last_of("/:");
  return separator == std::string::npos ? name : name.substr(separator + 1);
}

std::vector<std::string> splitPath(const std::string& name)
{
  std::vector<std::string> path;
  size_t begin = 0;
  do {
    const auto end = name.find('/', begin);
    path.push_back(name.substr(begin, end == std::string::npos ? end : end - begin));
    if (end == std::string::npos)
      break;
    begin = end + 1;
  } while (true);
  return path;
}

std::map<std::string, std::vector<std::string>> cellPaths(const Network& network)
{
  std::map<std::string, const HierarchyPin*> unique;
  for (const auto& [name, pin] : network.hierarchyPins())
    unique.try_emplace(pin.instance, &pin);
  std::vector<const HierarchyPin*> scopes;
  for (const auto& [name, pin] : unique)
    scopes.push_back(pin);
  std::sort(scopes.begin(), scopes.end(),
            [](const auto* a, const auto* b) { return std::tie(a->leaf_begin, a->depth, a->instance) < std::tie(b->leaf_begin, b->depth, b->instance); });
  std::map<std::string, std::vector<std::string>> paths;
  std::vector<const HierarchyPin*> ancestors;
  for (const auto* scope : scopes) {
    while (!ancestors.empty() && (ancestors.back()->depth >= scope->depth || ancestors.back()->leaf_end < scope->leaf_end))
      ancestors.pop_back();
    auto& path = paths[scope->instance];
    const std::string parent = ancestors.empty() ? "" : ancestors.back()->instance;
    if (!parent.empty())
      path = paths.at(parent);
    const auto local = parent.empty() ? scope->instance : scope->instance.substr(parent.size() + 1);
    const int parent_depth = ancestors.empty() ? 0 : ancestors.back()->depth;
    if (scope->depth > parent_depth + 1) {
      // Modules with no ports have no boundary record. Preserve their named
      // scopes between the known ancestors, as in networks without metadata.
      const auto missing = splitPath(local);
      path.insert(path.end(), missing.begin(), missing.end());
    } else
      path.push_back(local);
    ancestors.push_back(scope);
  }
  std::vector<std::pair<size_t, std::string>> leaves;
  for (const auto& [name, index] : network.hierarchyIndices())
    leaves.emplace_back(index, name);
  std::sort(leaves.begin(), leaves.end());
  std::priority_queue<std::pair<int, size_t>> active;
  size_t next = 0;
  for (const auto& [index, name] : leaves) {
    while (next < scopes.size() && scopes[next]->leaf_begin <= index) {
      active.emplace(scopes[next]->depth, next);
      ++next;
    }
    while (!active.empty() && scopes[active.top().second]->leaf_end <= index)
      active.pop();
    auto& path = paths[name];
    const std::string parent = active.empty() ? "" : scopes[active.top().second]->instance;
    if (!parent.empty())
      path = paths.at(parent);
    path.push_back(parent.empty() ? name : name.substr(parent.size() + 1));
  }
  // Networks supplied without an elaboration plan have only textual names.
  for (const auto& [name, instance] : network.instances())
    if (!paths.contains(name))
      paths[name] = splitPath(name);
  return paths;
}

void addObjectMatch(std::vector<ObjectMatch>& matches, std::string object_name, std::string display, std::string object_class,
                    std::map<std::string, std::string> attributes = {})
{
  ObjectMatch match;
  match.object_name = std::move(object_name);
  match.names = {match.object_name, display, leafName(display)};
  attributes["name"] = leafName(display);
  attributes["full_name"] = display;
  attributes["object_class"] = std::move(object_class);
  match.attributes = std::move(attributes);
  matches.push_back(std::move(match));
}
}  // namespace
std::vector<ObjectMatch> collectObjectMatches(Database& database, QueryObjectType type)
{
  std::vector<ObjectMatch> matches;
  LibertyLibrary& library = database.liberty();
  if (type == QueryObjectType::kLibrary || type == QueryObjectType::kAny) {
    std::set<std::string> names(library.get_library_name_list().begin(), library.get_library_name_list().end());
    for (auto& [cell_name, cell] : library.get_cell_map())
      names.insert(cell.get_library_name());
    for (const std::string& name : names)
      addObjectMatch(matches, name, name, "library");
  }
  if (type == QueryObjectType::kLibCell || type == QueryObjectType::kAny) {
    for (auto& [cell_name, cell] : library.get_cell_map()) {
      const std::string full_name = cell.get_library_name() + "/" + cell_name;
      addObjectMatch(matches, full_name, full_name, "lib_cell",
                     {{"area", std::to_string(cell.get_area())}, {"is_sequential", cell.get_is_sequential() ? "true" : "false"}});
    }
  }
  if (type == QueryObjectType::kLibPin || type == QueryObjectType::kAny) {
    for (auto& [cell_name, cell] : library.get_cell_map()) {
      for (auto& [pin_name, pin] : cell.get_port_map()) {
        const std::string full_name = cell.get_library_name() + "/" + cell_name + "/" + pin_name;
        addObjectMatch(matches, full_name, full_name, "lib_pin",
                       {{"direction", pin.get_is_input() && pin.get_is_output() ? "inout"
                                      : pin.get_is_input()                      ? "in"
                                      : pin.get_is_output()                     ? "out"
                                                                                : "internal"},
                        {"is_clock_pin", pin.get_is_clock() ? "true" : "false"}});
        matches.back().names.push_back(cell_name + "/" + pin_name);
      }
    }
  }
  if (type == QueryObjectType::kClock || type == QueryObjectType::kAny) {
    for (auto& [name, clock] : database.sdc().get_clock_map()) {
      addObjectMatch(matches, name, name, "clock",
                     {{"period", std::to_string(clock.get_period())},
                      {"is_generated", clock.get_is_generated() ? "true" : "false"},
                      {"is_propagated", clock.get_is_propagated() ? "true" : "false"}});
    }
  }
  if (type == QueryObjectType::kPort || type == QueryObjectType::kPin || type == QueryObjectType::kAny) {
    for (auto& [name, pin] : database.network().pins()) {
      if ((type == QueryObjectType::kPort && !pin.get_is_port()) || (type == QueryObjectType::kPin && pin.get_is_port()))
        continue;
      const std::string display = pin.get_is_port() ? name : pin.get_instance_name() + "/" + pin.get_pin_name();
      std::map<std::string, std::string> attributes{{"direction", directionName(pin.get_direction())}, {"is_port", pin.get_is_port() ? "true" : "false"}};
      if (!pin.get_is_port()) {
        attributes["pin_name"] = pin.get_pin_name();
        const auto instance = database.network().instances().find(pin.get_instance_name());
        if (instance != database.network().instances().end())
          attributes["ref_name"] = instance->second.get_cell_name();
      }
      addObjectMatch(matches, name, display, pin.get_is_port() ? "port" : "pin", std::move(attributes));
    }
  }
  if (type == QueryObjectType::kPin || type == QueryObjectType::kAny) {
    for (const auto& [name, pin] : database.network().hierarchyPins()) {
      addObjectMatch(matches, name, pin.instance + "/" + pin.port, "pin",
                     {{"direction", directionName(pin.direction)}, {"is_port", "false"}, {"is_hierarchical", "true"}, {"pin_name", pin.port}});
    }
  }
  if (type == QueryObjectType::kCell || type == QueryObjectType::kAny) {
    for (const auto& name : database.network().hierarchyCells())
      addObjectMatch(matches, name, name, "cell", {{"is_hierarchical", "true"}, {"is_sequential", "false"}});
    for (auto& [name, instance] : database.network().instances()) {
      addObjectMatch(matches, name, name, "cell",
                     {{"ref_name", instance.get_cell_name()},
                      {"is_hierarchical", "false"},
                      {"is_sequential", instance.get_is_sequential() ? "true" : "false"},
                      {"is_clock_gating_cell", instance.get_is_clock_gating() ? "true" : "false"}});
    }
  }
  if (type == QueryObjectType::kNet || type == QueryObjectType::kAny) {
    if (!database.network().netSegments().empty()) {
      for (const auto& [name, segment] : database.network().netSegments()) {
        const auto physical = database.network().nets().find(segment.physical_net);
        const auto fanout = physical == database.network().nets().end() ? 0 : physical->second.get_load_pin_list().size();
        addObjectMatch(matches, name, name, "net", {{"fanout", std::to_string(fanout)}});
      }
    } else
      for (auto& [name, net] : database.network().nets()) {
        addObjectMatch(matches, name, name, "net", {{"fanout", std::to_string(net.get_load_pin_list().size())}});
        // Flattening can choose an inner segment as the physical net name.
        // Top-level port declarations still name the same connected net in SDC.
        for (const auto& pin_name : net.get_pin_name_list()) {
          const auto pin = database.network().pins().find(pin_name);
          if (pin != database.network().pins().end() && pin->second.get_is_port() && pin->second.get_net_name() == name && pin_name != name)
            matches.back().names.push_back(pin_name);
        }
      }
  }
  if (type == QueryObjectType::kCell || type == QueryObjectType::kPin || type == QueryObjectType::kNet || type == QueryObjectType::kAny) {
    const auto paths = cellPaths(database.network());
    for (auto& match : matches) {
      const auto& kind = match.attributes.at("object_class");
      if (kind == "cell") {
        if (const auto found = paths.find(match.object_name); found != paths.end())
          match.path = found->second;
      } else if (kind == "net" && database.network().netSegments().contains(match.object_name)) {
        const auto& segment = database.network().netSegments().at(match.object_name);
        if (!segment.scope.empty()) {
          const auto owner = segment.scope.substr(0, segment.scope.size() - 1);
          if (const auto found = paths.find(owner); found != paths.end())
            match.path = found->second;
          else
            match.path = splitPath(owner);
        }
        // The declared local identifier may itself contain '/'. Its spelling
        // must not introduce another hierarchy level.
        match.path.push_back(segment.name.substr(segment.scope.size()));
      } else if (kind == "pin") {
        const auto hierarchy = database.network().hierarchyPins().find(match.object_name);
        const auto& instance = hierarchy != database.network().hierarchyPins().end() ? hierarchy->second.instance
                                                                                     : database.network().pins().at(match.object_name).get_instance_name();
        if (const auto found = paths.find(instance); found != paths.end())
          match.path = found->second;
        match.path.push_back(match.attributes.at("pin_name"));
      }
      if (!match.path.empty()) {
        match.names[2] = match.path.back();
        match.attributes["name"] = match.path.back();
      }
    }
  }
  return matches;
}

QueryObjectType getObjectType(Database& database, const std::string& name)
{
  if (database.network().hierarchyPins().contains(name))
    return QueryObjectType::kPin;
  const auto pin = database.network().pins().find(name);
  if (pin != database.network().pins().end())
    return pin->second.get_is_port() ? QueryObjectType::kPort : QueryObjectType::kPin;
  if (database.sdc().get_clock_map().contains(name))
    return QueryObjectType::kClock;
  if (database.network().instances().contains(name) || database.network().hierarchyCells().contains(name))
    return QueryObjectType::kCell;
  if (database.network().hasNet(name))
    return QueryObjectType::kNet;
  for (auto& [cell_name, cell] : database.liberty().get_cell_map()) {
    const std::string lib_cell = cell.get_library_name() + "/" + cell_name;
    if (name == lib_cell)
      return QueryObjectType::kLibCell;
    if (name.rfind(lib_cell + "/", 0) == 0)
      return QueryObjectType::kLibPin;
  }
  if (std::find(database.liberty().get_library_name_list().begin(), database.liberty().get_library_name_list().end(), name)
      != database.liberty().get_library_name_list().end())
    return QueryObjectType::kLibrary;
  return QueryObjectType::kAny;
}
std::set<std::string> collectRelatedObjects(Database& database, const std::vector<std::string>& object_names, QueryObjectType target,
                                            QueryObjectType source_type, bool leaf)
{
  std::set<std::string> result;
  const auto& network = database.network();
  auto addPinNets = [&](const std::string& name) {
    if (network.netSegments().empty()) {
      if (network.isHierarchyPin(name))
        result.insert(network.checkedHierarchyPin(name).net);
      else if (!network.pins().at(name).get_net_name().empty())
        result.insert(network.pins().at(name).get_net_name());
      return;
    }
    for (const auto& net : network.pinNets(name)) {
      const auto& segment = network.netSegment(net);
      // A boundary pin belongs to its enclosing design when queried from top.
      if (network.isHierarchyPin(name) && segment.scope == network.checkedHierarchyPin(name).instance + "/")
        continue;
      result.insert(net);
    }
  };
  for (const std::string& object_name : object_names) {
    const QueryObjectType source = source_type == QueryObjectType::kAny ? getObjectType(database, object_name) : source_type;
    if (source == target)
      result.insert(object_name);
    if (source == QueryObjectType::kPin && database.network().hierarchyPins().contains(object_name)) {
      const auto& pin = database.network().checkedHierarchyPin(object_name);
      if (target == QueryObjectType::kNet)
        addPinNets(object_name);
      if (target == QueryObjectType::kClock) {
        for (const auto& [name, clock] : database.sdc().get_clock_map()) {
          const auto& sources = clock.get_source_objects();
          if (std::find(sources.begin(), sources.end(), object_name) != sources.end())
            result.insert(name);
        }
      }
      if (target == QueryObjectType::kCell)
        result.insert(pin.instance);
      continue;  // Hierarchical modules have no physical cell/library object.
    }
    if (source == QueryObjectType::kCell && !database.network().instances().contains(object_name)) {
      for (const auto& [name, pin] : database.network().hierarchyPins())
        if (pin.instance == object_name) {
          if (target == QueryObjectType::kPin)
            result.insert(name);
          if (target == QueryObjectType::kNet && !pin.net.empty())
            addPinNets(name);
        }
      continue;
    }
    if (source == QueryObjectType::kCell && target == QueryObjectType::kLibCell) {
      Instance& instance = database.network().instances().at(object_name);
      auto cell = database.liberty().get_cell_map().find(instance.get_cell_name());
      if (cell != database.liberty().get_cell_map().end())
        result.insert(cell->second.get_library_name() + "/" + cell->first);
    }
    if (source == QueryObjectType::kPin && target == QueryObjectType::kLibPin) {
      Pin& pin = database.network().pins().at(object_name);
      auto instance = database.network().instances().find(pin.get_instance_name());
      if (instance != database.network().instances().end()) {
        auto cell = database.liberty().get_cell_map().find(instance->second.get_cell_name());
        if (cell != database.liberty().get_cell_map().end())
          result.insert(cell->second.get_library_name() + "/" + cell->first + "/" + pin.get_pin_name());
      }
    }
    if (source == QueryObjectType::kLibPin && target == QueryObjectType::kLibCell)
      result.insert(object_name.substr(0, object_name.rfind('/')));
    if (source == QueryObjectType::kLibCell && target == QueryObjectType::kLibrary)
      result.insert(object_name.substr(0, object_name.find('/')));
    if (source == QueryObjectType::kLibCell && target == QueryObjectType::kLibPin) {
      const std::size_t slash = object_name.find('/');
      const std::string cell_name = object_name.substr(slash + 1);
      auto cell = database.liberty().get_cell_map().find(cell_name);
      if (cell != database.liberty().get_cell_map().end())
        for (auto& [pin_name, pin] : cell->second.get_port_map())
          result.insert(object_name + "/" + pin_name);
    }
    if (source == QueryObjectType::kPin || source == QueryObjectType::kPort) {
      Pin& pin = database.network().pins().at(object_name);
      if (target == QueryObjectType::kCell && !pin.get_instance_name().empty())
        result.insert(pin.get_instance_name());
      if (target == QueryObjectType::kNet && !pin.get_net_name().empty())
        addPinNets(object_name);
    }
    if (source == QueryObjectType::kCell) {
      for (const std::string& pin_name : database.network().instances().at(object_name).get_pin_name_list()) {
        if (target == QueryObjectType::kPin)
          result.insert(pin_name);
        if (target == QueryObjectType::kNet && database.network().pins().contains(pin_name) && !database.network().pins().at(pin_name).get_net_name().empty()) {
          addPinNets(pin_name);
        }
      }
    }
    if (source == QueryObjectType::kNet) {
      for (const std::string& pin_name : network.netSegmentPins(object_name, leaf)) {
        if (network.isHierarchyPin(pin_name)) {
          if (target == QueryObjectType::kPin)
            result.insert(pin_name);
          if (target == QueryObjectType::kCell)
            result.insert(network.checkedHierarchyPin(pin_name).instance);
          continue;
        }
        if (target == QueryObjectType::kPin && database.network().pins().contains(pin_name) && !database.network().pins().at(pin_name).get_is_port()) {
          result.insert(pin_name);
        }
        if (target == QueryObjectType::kPort && database.network().pins().contains(pin_name) && database.network().pins().at(pin_name).get_is_port()) {
          result.insert(pin_name);
        }
        if (target == QueryObjectType::kCell && database.network().pins().contains(pin_name)
            && !database.network().pins().at(pin_name).get_instance_name().empty()) {
          result.insert(database.network().pins().at(pin_name).get_instance_name());
        }
      }
    }
    if (target == QueryObjectType::kClock && (source == QueryObjectType::kPin || source == QueryObjectType::kPort)) {
      for (auto& [clock_name, clock] : database.sdc().get_clock_map()) {
        if (std::find(clock.get_source_list().begin(), clock.get_source_list().end(), object_name) != clock.get_source_list().end())
          result.insert(clock_name);
      }
    }
  }
  return result;
}
}  // namespace ista::sdc
