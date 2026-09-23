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
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "ObjectQuery.hpp"

#include "PWHeader.hpp"
#include "SdcCommand.hpp"
#include "ObjectFilter.hpp"
#include "frontend/SdcTclCmd.hpp"

namespace ipw::sdc {

namespace {

struct ObjectMatch
{
  std::string object_name;
  std::vector<std::string> names;
  std::map<std::string, std::string> attributes;
};

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

void addObjectMatch(std::vector<ObjectMatch>& matches, std::string object_name, std::string display, std::string object_class,
                           std::map<std::string, std::string> attributes = {})
{
  ObjectMatch match;
  match.object_name = std::move(object_name);
  match.names = {match.object_name};
  if (display != match.object_name) match.names.push_back(display);
  match.names.push_back(leafName(display));
  attributes["name"] = leafName(display);
  attributes["full_name"] = display;
  attributes["object_class"] = std::move(object_class);
  match.attributes = std::move(attributes);
  matches.push_back(std::move(match));
}

bool hasUnescapedBracket(const std::string& pattern)
{
  for (std::size_t index = 0; index < pattern.size(); ++index) {
    if ((pattern[index] == '[' || pattern[index] == ']') && (index == 0 || pattern[index - 1] != '\\')) return true;
  }
  return false;
}

std::string escapeGlobBrackets(const std::string& pattern)
{
  std::string escaped;
  escaped.reserve(pattern.size() + 4);
  for (std::size_t index = 0; index < pattern.size(); ++index) {
    if ((pattern[index] == '[' || pattern[index] == ']') && (index == 0 || pattern[index - 1] != '\\')) escaped.push_back('\\');
    escaped.push_back(pattern[index]);
  }
  return escaped;
}

std::vector<ObjectMatch> collectObjectMatches(Database& database, QueryObjectType type)
{
  std::vector<ObjectMatch> matches;
  TimingLibrary& library = database.get_timing_library();
  if (type == QueryObjectType::kLibrary || type == QueryObjectType::kAny) {
    std::set<std::string> names(library.get_library_name_list().begin(), library.get_library_name_list().end());
    for (auto& [cell_name, cell] : library.get_cell_map()) names.insert(cell.get_library_name());
    for (const std::string& name : names) addObjectMatch(matches, name, name, "library");
  }
  if (type == QueryObjectType::kLibCell || type == QueryObjectType::kAny) {
    for (auto& [cell_name, cell] : library.get_cell_map()) {
      const std::string full_name = cell.get_library_name() + "/" + cell_name;
      addObjectMatch(matches, full_name, full_name, "lib_cell", {{"area", std::to_string(cell.get_area())},
                                                                   {"is_sequential", cell.get_is_sequential() ? "true" : "false"}});
    }
  }
  if (type == QueryObjectType::kLibPin || type == QueryObjectType::kAny) {
    for (auto& [cell_name, cell] : library.get_cell_map()) {
      for (auto& [pin_name, pin] : cell.get_port_map()) {
        const std::string full_name = cell.get_library_name() + "/" + cell_name + "/" + pin_name;
        addObjectMatch(matches, full_name, full_name, "lib_pin", {{"direction", pin.get_is_input() ? "in" : pin.get_is_output() ? "out" : "internal"},
                                                                    {"is_clock_pin", pin.get_is_clock() ? "true" : "false"}});
        matches.back().names.push_back(cell_name + "/" + pin_name);
      }
    }
  }
  if (type == QueryObjectType::kClock || type == QueryObjectType::kAny) {
    for (auto& [name, clock] : database.get_timing_constraint().get_clock_map()) {
      addObjectMatch(matches, name, name, "clock", {{"period", std::to_string(clock.get_period())},
                                                     {"is_generated", clock.get_is_generated() ? "true" : "false"},
                                                     {"is_propagated", clock.get_is_propagated() ? "true" : "false"}});
    }
  }
  if (type == QueryObjectType::kPort || type == QueryObjectType::kPin || type == QueryObjectType::kAny) {
    for (auto& [name, pin] : database.get_pin_map()) {
      if ((type == QueryObjectType::kPort && !pin.get_is_port()) || (type == QueryObjectType::kPin && pin.get_is_port())) continue;
      const std::string display = pin.get_is_port() ? name : pin.get_instance_name() + "/" + pin.get_pin_name();
      std::map<std::string, std::string> attributes{{"direction", directionName(pin.get_direction())},
                                                    {"is_port", pin.get_is_port() ? "true" : "false"}};
      if (!pin.get_is_port()) {
        attributes["pin_name"] = pin.get_pin_name();
        const auto instance = database.get_instance_map().find(pin.get_instance_name());
        if (instance != database.get_instance_map().end()) attributes["ref_name"] = instance->second.get_cell_name();
      }
      addObjectMatch(matches, name, display, pin.get_is_port() ? "port" : "pin", std::move(attributes));
    }
  }
  if (type == QueryObjectType::kCell || type == QueryObjectType::kAny) {
    for (auto& [name, instance] : database.get_instance_map()) {
      addObjectMatch(matches, name, name, "cell", {{"ref_name", instance.get_cell_name()},
                                                   {"is_sequential", instance.get_is_sequential() ? "true" : "false"},
                                                   {"is_clock_gating_cell", instance.get_is_clock_gating() ? "true" : "false"}});
    }
  }
  if (type == QueryObjectType::kNet || type == QueryObjectType::kAny) {
    for (auto& [name, net] : database.get_net_map()) {
      addObjectMatch(matches, name, name, "net", {{"fanout", std::to_string(net.get_load_pin_list().size())}});
    }
  }
  return matches;
}

QueryObjectType getObjectType(Database& database, const std::string& name)
{
  const auto pin = database.get_pin_map().find(name);
  if (pin != database.get_pin_map().end()) return pin->second.get_is_port() ? QueryObjectType::kPort : QueryObjectType::kPin;
  if (database.get_timing_constraint().get_clock_map().contains(name)) return QueryObjectType::kClock;
  if (database.get_instance_map().contains(name)) return QueryObjectType::kCell;
  if (database.get_net_map().contains(name)) return QueryObjectType::kNet;
  for (auto& [cell_name, cell] : database.get_timing_library().get_cell_map()) {
    const std::string lib_cell = cell.get_library_name() + "/" + cell_name;
    if (name == lib_cell) return QueryObjectType::kLibCell;
    if (name.rfind(lib_cell + "/", 0) == 0) return QueryObjectType::kLibPin;
  }
  if (std::find(database.get_timing_library().get_library_name_list().begin(), database.get_timing_library().get_library_name_list().end(), name)
      != database.get_timing_library().get_library_name_list().end()) return QueryObjectType::kLibrary;
  return QueryObjectType::kAny;
}

std::set<std::string> collectRelatedObjects(Database& database, const std::vector<std::string>& objects, QueryObjectType target)
{
  std::set<std::string> result;
  for (const std::string& object_pattern : objects) {
    const std::vector<std::string> object_names = findObjects(database, {object_pattern}, QueryObjectType::kAny);
    for (const std::string& object_name : object_names) {
      const QueryObjectType source = getObjectType(database, object_name);
      if (source == target) result.insert(object_name);
      if (source == QueryObjectType::kCell && target == QueryObjectType::kLibCell) {
        Instance& instance = database.get_instance_map().at(object_name);
        auto cell = database.get_timing_library().get_cell_map().find(instance.get_cell_name());
        if (cell != database.get_timing_library().get_cell_map().end()) result.insert(cell->second.get_library_name() + "/" + cell->first);
      }
      if (source == QueryObjectType::kPin && target == QueryObjectType::kLibPin) {
        Pin& pin = database.get_pin_map().at(object_name);
        auto instance = database.get_instance_map().find(pin.get_instance_name());
        if (instance != database.get_instance_map().end()) {
          auto cell = database.get_timing_library().get_cell_map().find(instance->second.get_cell_name());
          if (cell != database.get_timing_library().get_cell_map().end())
            result.insert(cell->second.get_library_name() + "/" + cell->first + "/" + pin.get_pin_name());
        }
      }
      if (source == QueryObjectType::kLibPin && target == QueryObjectType::kLibCell) result.insert(object_name.substr(0, object_name.rfind('/')));
      if (source == QueryObjectType::kLibCell && target == QueryObjectType::kLibrary) result.insert(object_name.substr(0, object_name.find('/')));
      if (source == QueryObjectType::kLibCell && target == QueryObjectType::kLibPin) {
        const std::size_t slash = object_name.find('/');
        const std::string cell_name = object_name.substr(slash + 1);
        auto cell = database.get_timing_library().get_cell_map().find(cell_name);
        if (cell != database.get_timing_library().get_cell_map().end())
          for (auto& [pin_name, pin] : cell->second.get_port_map()) result.insert(object_name + "/" + pin_name);
      }
      if (source == QueryObjectType::kPin || source == QueryObjectType::kPort) {
        Pin& pin = database.get_pin_map().at(object_name);
        if (target == QueryObjectType::kCell && !pin.get_instance_name().empty()) result.insert(pin.get_instance_name());
        if (target == QueryObjectType::kNet && !pin.get_net_name().empty()) result.insert(pin.get_net_name());
      }
      if (source == QueryObjectType::kCell) {
        for (const std::string& pin_name : database.get_instance_map().at(object_name).get_pin_name_list()) {
          if (target == QueryObjectType::kPin) result.insert(pin_name);
          if (target == QueryObjectType::kNet && database.get_pin_map().contains(pin_name)
              && !database.get_pin_map().at(pin_name).get_net_name().empty()) {
            result.insert(database.get_pin_map().at(pin_name).get_net_name());
          }
        }
      }
      if (source == QueryObjectType::kNet) {
        Net& net = database.get_net_map().at(object_name);
        for (const std::string& pin_name : net.get_pin_name_list()) {
          if (target == QueryObjectType::kPin && database.get_pin_map().contains(pin_name) && !database.get_pin_map().at(pin_name).get_is_port()) {
            result.insert(pin_name);
          }
          if (target == QueryObjectType::kPort && database.get_pin_map().contains(pin_name) && database.get_pin_map().at(pin_name).get_is_port()) {
            result.insert(pin_name);
          }
          if (target == QueryObjectType::kCell && database.get_pin_map().contains(pin_name)
              && !database.get_pin_map().at(pin_name).get_instance_name().empty()) {
            result.insert(database.get_pin_map().at(pin_name).get_instance_name());
          }
        }
      }
      if (target == QueryObjectType::kClock && (source == QueryObjectType::kPin || source == QueryObjectType::kPort)) {
        for (auto& [clock_name, clock] : database.get_timing_constraint().get_clock_map()) {
          if (std::find(clock.get_source_list().begin(), clock.get_source_list().end(), object_name) != clock.get_source_list().end()) result.insert(clock_name);
        }
      }
    }
  }
  return result;
}

bool matchObjectName(std::string actual, std::string pattern, const ObjectQueryOptions& options, bool escape_brackets)
{
  if (options.nocase) {
    std::transform(actual.begin(), actual.end(), actual.begin(), [](unsigned char value) { return std::tolower(value); });
    std::transform(pattern.begin(), pattern.end(), pattern.begin(), [](unsigned char value) { return std::tolower(value); });
  }
  if (options.exact) return actual == pattern;
  if (options.regexp) {
    const std::string expression = "^(?:" + pattern + ")$";
    if (Tcl_RegExpMatch(SdcCommand::getInst().getInterp(), "", expression.c_str()) < 0) throw std::invalid_argument("invalid regular expression: " + pattern);
    return Tcl_RegExpMatch(SdcCommand::getInst().getInterp(), actual.c_str(), expression.c_str()) == 1;
  }
  if (escape_brackets) pattern = escapeGlobBrackets(pattern);
  return Tcl_StringMatch(actual.c_str(), pattern.c_str()) != 0;
}

}  // namespace

std::vector<std::string> parseObjectPatterns(const std::string& text, bool regexp)
{
  // A regular expression may be passed as one raw Tcl word; otherwise parse a Tcl list.
  if (regexp && !text.empty() && text.front() != '{' && text.find('\\') != std::string::npos) {
    return {text};
  }
  int count = 0;
  const char** items = nullptr;
  if (Tcl_SplitList(SdcCommand::getInst().getInterp(), text.c_str(), &count, &items) != TCL_OK) {
    throw std::invalid_argument("invalid object pattern list");
  }
  std::vector<std::string> result(items, items + count);
  Tcl_Free(reinterpret_cast<char*>(items));
  return result;
}

std::vector<std::string> findObjects(Database& database, const std::vector<std::string>& patterns, QueryObjectType type, bool regexp)
{
  ObjectQueryOptions options;
  options.regexp = regexp;
  return findObjects(database, patterns, type, options);
}

std::vector<std::string> findObjects(Database& database, const std::vector<std::string>& patterns, QueryObjectType type, const ObjectQueryOptions& options)
{
  if (options.regexp && options.exact) throw std::invalid_argument("-regexp and -exact are mutually exclusive");
  const std::set<std::string> of_objects = options.of_objects.empty() ? std::set<std::string>{} : collectRelatedObjects(database, options.of_objects, type);
  const std::vector<ObjectMatch> object_matches = collectObjectMatches(database, type);
  std::set<std::string> found;

  auto isEligible = [&](const ObjectMatch& candidate) {
    return (options.of_objects.empty() || of_objects.contains(candidate.object_name))
           && matchesFilter(options.filter, candidate.attributes, options.regexp, options.nocase);
  };
  auto matches = [&](const ObjectMatch& candidate, const std::string& pattern, bool escape_brackets) {
    for (std::size_t index = 0; index < candidate.names.size(); ++index) {
      if (index == 2 && !options.hierarchical) continue;
      if (matchObjectName(candidate.names[index], pattern, options, escape_brackets)) return true;
    }
    return false;
  };

  for (const std::string& pattern : patterns) {
    bool pattern_matched = false;
    for (const ObjectMatch& candidate : object_matches) {
      if (isEligible(candidate) && matches(candidate, pattern, false)) {
        found.insert(candidate.object_name);
        pattern_matched = true;
      }
    }

    // Retry a non-regexp query with literal brackets when the normal glob pass
    // found nothing. This is required for names such as bus[0].
    if (!pattern_matched && !options.regexp && !options.exact && hasUnescapedBracket(pattern)) {
      for (const ObjectMatch& candidate : object_matches) {
        if (isEligible(candidate) && matches(candidate, pattern, true)) {
          found.insert(candidate.object_name);
        }
      }
    }
  }
  return {found.begin(), found.end()};
}

std::vector<std::string> findObjectsByType(Database& database, const std::vector<std::string>& objects, QueryObjectType type)
{
  std::set<std::string> object_names;
  for (const std::string& object : objects) {
    const std::vector<std::string> matches = findObjects(database, {object}, type);
    object_names.insert(matches.begin(), matches.end());
  }
  return {object_names.begin(), object_names.end()};
}

std::vector<std::string> findClockSources(Database& database, const std::vector<std::string>& objects)
{
  std::set<std::string> sources;
  for (const std::string& object : objects) {
    for (const std::string& match : findObjects(database, {object}, QueryObjectType::kAny)) {
      const QueryObjectType type = getObjectType(database, match);
      if (type == QueryObjectType::kPort || type == QueryObjectType::kPin) {
        sources.insert(match);
      } else if (type == QueryObjectType::kNet) {
        Net& net = database.get_net_map().at(match);
        if (!net.get_driver_pin().empty()) sources.insert(net.get_driver_pin());
        sources.insert(net.get_driver_pin_list().begin(), net.get_driver_pin_list().end());
      } else {
        throw std::invalid_argument("clock sources must be ports, pins, or nets: " + object);
      }
    }
  }
  return {sources.begin(), sources.end()};
}

std::set<std::string> findClocks(Database& database, const std::vector<std::string>& objects)
{
  std::set<std::string> clocks;
  for (const std::string& pattern : objects) {
    const std::vector<std::string> matches = findObjects(database, {pattern}, QueryObjectType::kClock);
    if (matches.empty()) {
      throw std::invalid_argument("clock '" + pattern + "' does not exist");
    }
    clocks.insert(matches.begin(), matches.end());
  }
  if (clocks.empty()) {
    throw std::invalid_argument("a non-empty clock collection is required");
  }
  return clocks;
}

std::set<std::string> findExceptionObjects(Database& database, const std::vector<std::string>& objects)
{
  std::set<std::string> result;
  for (const std::string& object : objects) {
    if (database.get_pin_map().contains(object) || database.get_instance_map().contains(object) || database.get_net_map().contains(object)
        || database.get_timing_constraint().get_clock_map().contains(object)) {
      result.insert(object);
      continue;
    }
    const std::vector<std::string> matches = findObjects(database, {object}, QueryObjectType::kAny);
    if (matches.empty()) {
      throw std::invalid_argument("exception object not found: " + object);
    }
    result.insert(matches.begin(), matches.end());
  }
  if (result.empty()) {
    throw std::invalid_argument("empty timing exception collection");
  }
  return result;
}

std::vector<std::string> getPortPinNames(Database& database, const std::vector<std::string>& object_list)
{
  std::vector<std::string> pin_names;
  for (const std::string& object_name : object_list) {
    std::string pin_name = object_name;
    if (!pin_name.empty() && pin_name.front() == '\\') {
      pin_name.erase(pin_name.begin());
    }
    if (pin_name.rfind("[get_ports", 0) == 0) {
      pin_name = pin_name.substr(10);
    }
    if (pin_name.rfind("[get_pins", 0) == 0) {
      pin_name = pin_name.substr(9);
      std::replace(pin_name.begin(), pin_name.end(), '/', ':');
    }
    if (database.get_pin_map().count(pin_name) > 0) {
      pin_names.push_back(pin_name);
      continue;
    }
    if (!pin_name.empty() && pin_name.back() == ']') {
      std::string trimmed_pin_name = pin_name;
      trimmed_pin_name.pop_back();
      if (database.get_pin_map().count(trimmed_pin_name) > 0) {
        pin_names.push_back(trimmed_pin_name);
        continue;
      }
    }
    std::replace(pin_name.begin(), pin_name.end(), '/', ':');
    if (database.get_pin_map().count(pin_name) > 0) {
      pin_names.push_back(pin_name);
    }
  }
  return pin_names;
}

std::vector<std::string> getFullNames(Database& database, const std::string& object_list)
{
  const std::vector<std::string> patterns = parseObjectPatterns(object_list, false);
  if (patterns.empty()) {
    // An empty collection is a valid result of get_ports/get_pins and must
    // remain an empty collection when passed to get_full_name.
    return {};
  }

  const std::vector<ObjectMatch> object_matches = collectObjectMatches(database, QueryObjectType::kAny);
  std::map<std::string, std::string> full_names;
  for (const ObjectMatch& match : object_matches) {
    full_names.emplace(match.object_name, match.attributes.at("full_name"));
  }

  std::vector<std::string> result;
  std::set<std::string> emitted;
  for (const std::string& pattern : patterns) {
    const std::vector<std::string> matches = findObjects(database, {pattern}, QueryObjectType::kAny);
    if (matches.empty()) {
      throw std::invalid_argument("object '" + pattern + "' does not exist");
    }
    for (const std::string& match : matches) {
      const auto full_name = full_names.find(match);
      if (full_name == full_names.end()) {
        throw std::invalid_argument("object '" + match + "' has no full name");
      }
      if (emitted.insert(full_name->second).second) {
        result.push_back(full_name->second);
      }
    }
  }
  return result;
}

TimingPortConstraint& getOrCreatePortConstraint(Database& database, const std::string& port_name)
{
  TimingPortConstraint& port_constraint = database.get_timing_constraint().get_port_constraint_map()[port_name];
  port_constraint.set_port_name(port_name);
  return port_constraint;
}

void addObjectQueryOptions(SdcTclCmd& command, bool hierarchical, bool exact, bool of_objects)
{
  command.addOption(new ecc::TclSwitchOption("-quiet"));
  command.addOption(new ecc::TclSwitchOption("-regexp"));
  command.addOption(new ecc::TclSwitchOption("-nocase"));
  command.addOption(new ecc::TclStringOption("-filter", 0));
  if (hierarchical) command.addOption(new ecc::TclSwitchOption("-hierarchical"));
  if (exact) command.addOption(new ecc::TclSwitchOption("-exact"));
  if (of_objects) command.addOption(new ecc::TclStringListOption("-of_objects", 0));
}

ObjectQueryOptions getObjectQueryOptions(SdcTclCmd& command)
{
  ObjectQueryOptions options;
  for (const auto& [name, target] : std::initializer_list<std::pair<const char*, bool*>>{{"-regexp", &options.regexp},
                                                                                        {"-nocase", &options.nocase},
                                                                                        {"-exact", &options.exact},
                                                                                        {"-hierarchical", &options.hierarchical}}) {
    if (ecc::TclOption* option = command.getOptionOrArg(name); option != nullptr) *target = option->is_set_val();
  }
  if (ecc::TclOption* filter = command.getOptionOrArg("-filter"); filter != nullptr && filter->is_set_val()) options.filter = filter->getStringVal();
  if (ecc::TclOption* objects = command.getOptionOrArg("-of_objects"); objects != nullptr && objects->is_set_val()) {
    options.of_objects = objects->getStringList();
  }
  return options;
}

std::optional<std::string> getObjectQueryError(const ObjectQueryOptions& options, bool has_patterns)
{
  if (options.regexp && options.exact) return "-regexp and -exact are mutually exclusive";
  if (options.nocase && !options.regexp) return "-nocase requires -regexp";
  if (has_patterns && !options.of_objects.empty()) return "patterns and -of_objects are mutually exclusive";
  if (options.hierarchical && !options.of_objects.empty()) return "-hierarchical and -of_objects are mutually exclusive";
  return std::nullopt;
}

}  // namespace ipw::sdc
