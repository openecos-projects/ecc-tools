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

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ObjectFilter.hpp"
#include "network/Network.hpp"
#include "query/SdcObject.hpp"

#if __has_include(<tcl8.6/tcl.h>)
#include <tcl8.6/tcl.h>
#else
#include <tcl.h>
#endif

namespace ista::sdc {
QueryObjectType queryObjectType(Tcl_Obj* object)
{
  switch (SdcObject::getType(object)) {
    case SdcObjectType::kPort:
      return QueryObjectType::kPort;
    case SdcObjectType::kNet:
      return QueryObjectType::kNet;
    case SdcObjectType::kPin:
      return QueryObjectType::kPin;
    case SdcObjectType::kCell:
      return QueryObjectType::kCell;
    case SdcObjectType::kClock:
      return QueryObjectType::kClock;
    case SdcObjectType::kLibrary:
      return QueryObjectType::kLibrary;
    case SdcObjectType::kLibCell:
      return QueryObjectType::kLibCell;
    case SdcObjectType::kLibPin:
      return QueryObjectType::kLibPin;
    default:
      return QueryObjectType::kAny;
  }
}

namespace {

// Resolve canonical identity only. Display-name aliases and wildcard matching
// belong to textual queries, never to an object returned by a previous query.
std::string objectFullName(Database& database, Tcl_Obj* object)
{
  const std::string name = Tcl_GetString(object);
  const auto type = SdcObject::getType(object);
  const auto& network = database.network();
  switch (type) {
    case SdcObjectType::kPort:
    case SdcObjectType::kPin: {
      const auto pin = network.pins().find(name);
      if (pin != network.pins().end() && pin->second.get_is_port() == (type == SdcObjectType::kPort))
        return type == SdcObjectType::kPort ? name : pin->second.get_instance_name() + "/" + pin->second.get_pin_name();
      if (type == SdcObjectType::kPin) {
        const auto boundary = network.hierarchyPins().find(name);
        if (boundary != network.hierarchyPins().end())
          return boundary->second.instance + "/" + boundary->second.port;
      }
      break;
    }
    case SdcObjectType::kClock:
      if (database.sdc().get_clock_map().contains(name))
        return name;
      break;
    case SdcObjectType::kCell:
      if (network.instances().contains(name) || network.hierarchyCells().contains(name))
        return name;
      break;
    case SdcObjectType::kNet:
      if (network.hasNet(name)) {
        network.physicalNet(name);
        return name;
      }
      break;
    case SdcObjectType::kDesign:
      if (network.name() == name)
        return name;
      break;
    case SdcObjectType::kDelayCorner:
      if (name == "default")
        return name;
      break;
    case SdcObjectType::kLibrary:
    case SdcObjectType::kLibCell:
    case SdcObjectType::kLibPin:
      for (const auto& candidate : collectObjectMatches(database, queryObjectType(object)))
        if (candidate.object_name == name)
          return candidate.attributes.at("full_name");
      break;
    case SdcObjectType::kNone:
      throw std::invalid_argument("expected an object collection, got text: " + name);
  }
  throw std::invalid_argument("object no longer exists: " + name);
}

bool hasUnescapedBracket(const std::string& pattern)
{
  for (std::size_t index = 0; index < pattern.size(); ++index) {
    if ((pattern[index] == '[' || pattern[index] == ']') && (index == 0 || pattern[index - 1] != '\\'))
      return true;
  }
  return false;
}

std::string escapeGlobBrackets(const std::string& pattern)
{
  std::string escaped;
  escaped.reserve(pattern.size() + 4);
  for (std::size_t index = 0; index < pattern.size(); ++index) {
    if ((pattern[index] == '[' || pattern[index] == ']') && (index == 0 || pattern[index - 1] != '\\'))
      escaped.push_back('\\');
    escaped.push_back(pattern[index]);
  }
  return escaped;
}

std::set<std::string> resolveRelatedObjects(Tcl_Interp* interp, Database& database, const std::vector<std::pair<QueryObjectType, std::string>>& objects,
                                            QueryObjectType target, bool leaf)
{
  std::set<std::string> result;
  for (const auto& [source, pattern] : objects) {
    const auto names = source != QueryObjectType::kAny || getObjectType(database, pattern) != QueryObjectType::kAny
                           ? std::vector<std::string>{pattern}
                           : findObjects(interp, database, {pattern}, QueryObjectType::kAny);
    const auto related = collectRelatedObjects(database, names, target, source, leaf);
    result.insert(related.begin(), related.end());
  }
  return result;
}

bool matchObjectName(Tcl_Interp* interp, std::string actual, std::string pattern, const ObjectQueryOptions& options, bool escape_brackets)
{
  if (options.nocase && !options.regexp) {
    std::transform(actual.begin(), actual.end(), actual.begin(), [](unsigned char value) { return std::tolower(value); });
    std::transform(pattern.begin(), pattern.end(), pattern.begin(), [](unsigned char value) { return std::tolower(value); });
  }
  if (options.exact)
    return actual == pattern;
  if (options.regexp) {
    const std::string expression = (options.nocase ? "(?i)^(?:" : "^(?:") + pattern + ")$";
    if (Tcl_RegExpMatch(interp, "", expression.c_str()) < 0)
      throw std::invalid_argument("invalid regular expression: " + pattern);
    return Tcl_RegExpMatch(interp, actual.c_str(), expression.c_str()) == 1;
  }
  if (escape_brackets)
    pattern = escapeGlobBrackets(pattern);
  return Tcl_StringMatch(actual.c_str(), pattern.c_str()) != 0;
}

}  // namespace

std::vector<std::string> parseObjectPatterns(Tcl_Interp* interp, const std::string& text, bool regexp)
{
  // A regular expression may be passed as one raw Tcl word; otherwise parse a Tcl list.
  if (regexp && !text.empty() && text.front() != '{' && text.find('\\') != std::string::npos) {
    return {text};
  }
  int count = 0;
  const char** items = nullptr;
  if (Tcl_SplitList(interp, text.c_str(), &count, &items) != TCL_OK) {
    throw std::invalid_argument("invalid object pattern list");
  }
  std::vector<std::string> result(items, items + count);
  Tcl_Free(reinterpret_cast<char*>(items));
  return result;
}

std::vector<std::string> findObjects(Tcl_Interp* interp, Database& database, const std::vector<std::string>& patterns, QueryObjectType type, bool regexp)
{
  ObjectQueryOptions options;
  options.regexp = regexp;
  return findObjects(interp, database, patterns, type, options);
}

std::vector<std::string> findObjects(Tcl_Interp* interp, Database& database, const std::vector<std::string>& patterns, QueryObjectType type,
                                     const ObjectQueryOptions& options)
{
  if (options.regexp && options.exact)
    throw std::invalid_argument("-regexp and -exact are mutually exclusive");
  // Exported SDCs contain thousands of exact scalar/bus queries. Avoid
  // rebuilding a multi-million-pin display catalogue for each one. Numeric
  // Tcl character classes retain their normal-match-then-literal fallback.
  if (!options.regexp && !options.nocase && !options.hierarchical && options.filter.empty() && !options.has_of_objects
      && (type == QueryObjectType::kPin || type == QueryObjectType::kPort || type == QueryObjectType::kCell || type == QueryObjectType::kClock)) {
    auto lookup = [&](std::string name, std::set<std::string>& out) {
      if (type == QueryObjectType::kPin) {
        auto insert = [&](const std::string& key) {
          if (database.network().hierarchyPins().contains(key))
            out.insert(key);
          else if (database.network().pins().contains(key) && !database.network().pins().at(key).get_is_port())
            out.insert(key);
        };
        insert(name);
        // Either instance or escaped port identifiers can contain '/'. Check
        // every split, preserving all matches if display names are ambiguous.
        for (size_t slash = name.find('/'); slash != std::string::npos; slash = name.find('/', slash + 1)) {
          name[slash] = ':';
          insert(name);
          name[slash] = '/';
        }
      } else if (type == QueryObjectType::kPort) {
        if (database.network().pins().contains(name) && database.network().pins().at(name).get_is_port())
          out.insert(name);
      } else if (type == QueryObjectType::kCell && (database.network().instances().contains(name) || database.network().hierarchyCells().contains(name)))
        out.insert(name);
      else if (type == QueryObjectType::kClock && database.sdc().get_clock_map().contains(name))
        out.insert(name);
    };
    std::set<std::string> exact;
    bool complete = true;
    for (const auto& pattern : patterns) {
      if (!options.exact && pattern.find_first_of("*?\\") != std::string::npos) {
        complete = false;
        break;
      }
      std::vector<std::string> candidates{std::string{}};
      for (size_t i = 0; i < pattern.size() && complete; ++i) {
        if (pattern[i] != '[' || options.exact) {
          for (auto& name : candidates)
            name += pattern[i];
          continue;
        }
        const auto end = pattern.find(']', i + 1);
        if (end == std::string::npos || end == i + 1
            || !std::all_of(pattern.begin() + i + 1, pattern.begin() + end, [](char c) { return c >= '0' && c <= '9'; })
            || candidates.size() * (end - i - 1) > 256) {
          complete = false;
          break;
        }
        std::vector<std::string> expanded;
        for (const auto& name : candidates)
          for (size_t j = i + 1; j < end; ++j)
            expanded.push_back(name + pattern[j]);
        candidates = std::move(expanded);
        i = end;
      }
      if (!complete)
        break;
      std::set<std::string> matches;
      for (const auto& candidate : candidates)
        lookup(candidate, matches);
      if (matches.empty())
        lookup(pattern, matches);
      exact.insert(matches.begin(), matches.end());
    }
    if (complete)
      return {exact.begin(), exact.end()};
  }
  const std::set<std::string> of_objects
      = options.of_objects.empty() ? std::set<std::string>{} : resolveRelatedObjects(interp, database, options.of_objects, type, options.leaf);
  const std::vector<ObjectMatch> object_matches = collectObjectMatches(database, type);
  std::set<std::string> found;

  auto isEligible = [&](const ObjectMatch& candidate) {
    return (!options.has_of_objects || of_objects.contains(candidate.object_name))
           && matchesFilter(interp, options.filter, candidate.attributes, options.regexp, options.nocase);
  };
  auto matches = [&](const ObjectMatch& candidate, const std::string& pattern, bool escape_brackets) {
    if (options.has_of_objects)
      return true;  // No patterns are permitted with -of_objects.
    if (!options.regexp && !options.exact && !candidate.path.empty()) {
      if (pattern == candidate.object_name)
        return true;
      std::vector<std::string> parts;
      size_t begin = 0;
      do {
        const auto end = pattern.find('/', begin);
        parts.push_back(pattern.substr(begin, end == std::string::npos ? end : end - begin));
        if (end == std::string::npos)
          break;
        begin = end + 1;
      } while (true);
      auto path = candidate.path;
      if (candidate.attributes.at("object_class") == "pin" && path.size() >= 2) {
        if (!options.hierarchical && parts.size() == 1 && pattern != "*")
          return false;
        // At each search level a pin is named cell/pin, not just pin.
        // That last separator is not another design hierarchy level.
        auto pin = std::move(path.back());
        path.pop_back();
        path.back() += "/" + pin;
        if (parts.size() >= 2) {
          auto pin_pattern = std::move(parts.back());
          parts.pop_back();
          parts.back() += "/" + pin_pattern;
        }
      }
      if (options.hierarchical && parts.size() == 1)
        return matchObjectName(interp, path.back(), parts.back(), options, escape_brackets);
      if (parts.size() != path.size())
        return false;
      for (size_t i = 0; i < parts.size(); ++i)
        if (!matchObjectName(interp, path[i], parts[i], options, escape_brackets))
          return false;
      return true;
    }
    for (std::size_t index = 0; index < candidate.names.size(); ++index) {
      if (index == 2 && !options.hierarchical)
        continue;
      const auto& kind = candidate.attributes.at("object_class");
      if (!options.regexp && !options.exact && (kind == "cell" || kind == "pin" || kind == "net")) {
        // A glob matches one hierarchy level. Explicit path components still
        // select deeper objects; hierarchical queries also try the local name.
        const auto& name = candidate.names[index];
        if (std::count(name.begin(), name.end(), '/') != std::count(pattern.begin(), pattern.end(), '/'))
          continue;
      }
      if (matchObjectName(interp, candidate.names[index], pattern, options, escape_brackets))
        return true;
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

bool deferObjectBinding(Tcl_Interp* interp, const Database& database)
{
  const auto& network = database.network();
  return SdcObject::allowsUnboundObjects(interp) && network.name().empty() && network.pins().empty() && network.nets().empty() && network.instances().empty()
         && !network.hasHierarchyPins();
}

std::vector<std::string> findQueryObjects(Tcl_Interp* interp, Database& database, Tcl_Obj* objects, QueryObjectType type, const ObjectQueryOptions& options,
                                          const std::string& default_pattern)
{
  if (objects && deferObjectBinding(interp, database) && !options.regexp && options.filter.empty() && !options.has_of_objects
      && (type == QueryObjectType::kPort || type == QueryObjectType::kPin || type == QueryObjectType::kNet)) {
    std::set<std::string> names;
    for (auto* item : SdcObject::collection(objects)) {
      if (SdcObject::getType(item) != SdcObjectType::kNone && queryObjectType(item) != type)
        throw std::invalid_argument("wrong object type in query: " + std::string(Tcl_GetString(item)));
      const std::string name = Tcl_GetString(item);
      // Without a design there is no universe in which to expand a wildcard.
      if (!name.empty() && (options.exact || name.find_first_of("*?") == std::string::npos))
        names.insert(name);
    }
    return {names.begin(), names.end()};
  }
  if (!objects)
    return findObjects(interp, database, {default_pattern}, type, options);
  // Save the original word before Tcl list conversion: a raw regexp such as
  // {\\D+} must not lose its backslash during list parsing.
  const std::string text = Tcl_GetString(objects);
  const auto items = SdcObject::collection(objects);
  const bool typed = std::any_of(items.begin(), items.end(), [](Tcl_Obj* item) { return SdcObject::getType(item) != SdcObjectType::kNone; });
  if (!typed)
    return findObjects(interp, database, parseObjectPatterns(interp, text, options.regexp), type, options);

  std::vector<std::string> patterns;
  std::set<std::string> found;
  for (Tcl_Obj* item : items) {
    if (SdcObject::getType(item) == SdcObjectType::kNone) {
      patterns.emplace_back(Tcl_GetString(item));
      continue;
    }
    if (queryObjectType(item) != type)
      throw std::invalid_argument("wrong object type in query: " + std::string(Tcl_GetString(item)));
    objectFullName(database, item);  // Validate identity without reinterpreting its spelling.
    found.insert(Tcl_GetString(item));
  }
  if (!options.filter.empty()) {
    const auto catalog = collectObjectMatches(database, type);
    std::set<std::string> eligible;
    for (const auto& candidate : catalog)
      if (found.contains(candidate.object_name) && matchesFilter(interp, options.filter, candidate.attributes, options.regexp, options.nocase))
        eligible.insert(candidate.object_name);
    found = std::move(eligible);
  }
  if (!patterns.empty()) {
    const auto matches = findObjects(interp, database, patterns, type, options);
    found.insert(matches.begin(), matches.end());
  }
  return {found.begin(), found.end()};
}

std::vector<std::string> findObjectsByType(Tcl_Interp* interp, Database& database, const std::vector<std::string>& objects, QueryObjectType type)
{
  std::set<std::string> object_names;
  for (const std::string& object : objects) {
    const std::vector<std::string> matches = findObjects(interp, database, {object}, type);
    object_names.insert(matches.begin(), matches.end());
  }
  return {object_names.begin(), object_names.end()};
}

std::vector<std::string> findClockSources(Tcl_Interp* interp, Database& database, const std::vector<std::string>& objects)
{
  std::set<std::string> sources;
  for (const std::string& object : objects) {
    if (deferObjectBinding(interp, database) && !object.empty() && object.find_first_of("*?") == std::string::npos) {
      sources.insert(object);
      continue;
    }
    const auto direct_type = getObjectType(database, object);
    const auto matches
        = direct_type == QueryObjectType::kAny ? findObjects(interp, database, {object}, QueryObjectType::kAny) : std::vector<std::string>{object};
    for (const std::string& match : matches) {
      const QueryObjectType type = getObjectType(database, match);
      if (type == QueryObjectType::kPort || type == QueryObjectType::kPin) {
        if (database.network().hierarchyPins().contains(match))
          database.network().checkedHierarchyPin(match);
        sources.insert(match);
      } else if (type == QueryObjectType::kNet) {
        Net& net = database.network().nets().at(database.network().physicalNet(match));
        if (!net.get_driver_pin().empty())
          sources.insert(net.get_driver_pin());
        sources.insert(net.get_driver_pin_list().begin(), net.get_driver_pin_list().end());
      } else {
        throw std::invalid_argument("clock sources must be ports, pins, or nets: " + object);
      }
    }
  }
  return {sources.begin(), sources.end()};
}

std::set<std::string> findClocks(Tcl_Interp* interp, Database& database, const std::vector<std::string>& objects)
{
  std::set<std::string> clocks;
  for (const std::string& pattern : objects) {
    const std::vector<std::string> matches = findObjects(interp, database, {pattern}, QueryObjectType::kClock);
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

std::set<std::string> findClocksFromObjects(Tcl_Interp* interp, Database& database, const std::vector<std::string>& objects)
{
  std::set<std::string> result;
  for (const std::string& object : objects) {
    const std::vector<std::string> direct_clocks = findObjects(interp, database, {object}, QueryObjectType::kClock);
    if (!direct_clocks.empty()) {
      result.insert(direct_clocks.begin(), direct_clocks.end());
      continue;
    }
    for (const std::string& source : findClockSources(interp, database, {object})) {
      for (auto& [clock_name, clock] : database.sdc().get_clock_map()) {
        if (std::find(clock.get_source_objects().begin(), clock.get_source_objects().end(), source) != clock.get_source_objects().end()) {
          result.insert(clock_name);
        }
      }
    }
  }
  if (result.empty()) {
    throw std::invalid_argument("objects resolved to an empty clock collection");
  }
  return result;
}

ExceptionObjects findExceptionObjects(Tcl_Interp* interp, Database& database, const std::vector<Tcl_Obj*>& objects)
{
  ExceptionObjects result;
  const std::vector<std::pair<QueryObjectType, ExceptionObjectType>> kinds{{QueryObjectType::kClock, ExceptionObjectType::kClock},
                                                                           {QueryObjectType::kPort, ExceptionObjectType::kPort},
                                                                           {QueryObjectType::kPin, ExceptionObjectType::kPin},
                                                                           {QueryObjectType::kCell, ExceptionObjectType::kCell},
                                                                           {QueryObjectType::kNet, ExceptionObjectType::kNet}};
  for (auto* object : objects) {
    const auto type = queryObjectType(object);
    const std::string name = Tcl_GetString(object);
    bool found = false;
    for (const auto& [query, kind] : kinds) {
      if (SdcObject::getType(object) != SdcObjectType::kNone && type != query)
        continue;
      std::vector<std::string> names;
      if (SdcObject::getType(object) != SdcObjectType::kNone) {
        // A typed object is already canonical. Reinterpreting spelling aliases
        // here can expand one escaped pin into a different physical pin.
        const auto pin = database.network().pins().find(name);
        const bool exists = query == QueryObjectType::kClock ? database.sdc().get_clock_map().contains(name)
                            : query == QueryObjectType::kNet ? database.network().hasNet(name)
                            : query == QueryObjectType::kCell
                                ? database.network().instances().contains(name) || database.network().hierarchyCells().contains(name)
                            : query == QueryObjectType::kPort
                                ? pin != database.network().pins().end() && pin->second.get_is_port()
                                : database.network().isHierarchyPin(name) || (pin != database.network().pins().end() && !pin->second.get_is_port());
        if (exists)
          names.push_back(name);
      } else
        names = findObjects(interp, database, {name}, query);
      for (const auto& target : names) {
        if (kind == ExceptionObjectType::kPin && database.network().isHierarchyPin(target))
          database.network().checkedHierarchyPin(target);
        result.insert({kind, target});
        found = true;
      }
    }
    if (!found)
      throw std::invalid_argument("invalid timing exception object: " + name);
  }
  if (result.empty())
    throw std::invalid_argument("empty timing exception collection");
  return result;
}

std::vector<std::string> resolvePinTargets(Tcl_Interp* interp, Database& database, const std::vector<Tcl_Obj*>& objects, bool ports_only, bool inputs_only)
{
  std::set<std::string> targets;
  for (Tcl_Obj* object : objects) {
    const auto type = SdcObject::getType(object);
    const std::string name = Tcl_GetString(object);
    if (type != SdcObjectType::kNone && type != SdcObjectType::kPort && (ports_only || type != SdcObjectType::kPin))
      throw std::invalid_argument("constraint requires " + std::string(ports_only ? "ports: " : "pins or ports: ") + name);
    std::vector<std::string> names;
    if (type != SdcObjectType::kNone) {
      const auto pin = database.network().pins().find(name);
      if (pin == database.network().pins().end() || pin->second.get_is_port() != (type == SdcObjectType::kPort))
        throw std::invalid_argument("pin or port no longer exists: " + name);
      names.push_back(name);
    } else {
      names = findObjects(interp, database, {name}, QueryObjectType::kPort);
      if (!ports_only) {
        const auto pins = findObjects(interp, database, {name}, QueryObjectType::kPin);
        names.insert(names.end(), pins.begin(), pins.end());
      }
    }
    if (names.empty())
      throw std::invalid_argument("pin or port not found: " + name);
    for (const auto& target : names) {
      const auto pin = database.network().pins().find(target);
      if (pin == database.network().pins().end())
        throw std::invalid_argument("constraint requires leaf pins or top-level ports: " + target);
      if (inputs_only && pin->second.get_direction() != PinDirection::kInput && pin->second.get_direction() != PinDirection::kInout)
        throw std::invalid_argument("constraint requires input ports: " + target);
      targets.insert(target);
    }
  }
  if (targets.empty())
    throw std::invalid_argument("constraint requires a non-empty pin or port collection");
  return {targets.begin(), targets.end()};
}

std::vector<std::pair<QueryObjectType, std::string>> resolveClockTargets(Tcl_Interp* interp, Database& database, const std::vector<Tcl_Obj*>& objects,
                                                                         bool clocks_only)
{
  std::set<std::pair<QueryObjectType, std::string>> result;
  for (auto* object : objects) {
    const auto type = SdcObject::getType(object);
    const std::string name = Tcl_GetString(object);
    if (type == SdcObjectType::kClock) {
      if (!database.sdc().get_clock_map().contains(name))
        throw std::invalid_argument("clock no longer exists: " + name);
      result.emplace(QueryObjectType::kClock, name);
    } else if (type == SdcObjectType::kNone) {
      const auto clocks = findObjects(interp, database, {name}, QueryObjectType::kClock);
      for (const auto& clock : clocks)
        result.emplace(QueryObjectType::kClock, clock);
      if (!clocks.empty())
        continue;
      if (clocks_only)
        throw std::invalid_argument("clock not found: " + name);
      for (const auto& pin : resolvePinTargets(interp, database, {object}))
        result.emplace(database.network().pins().at(pin).get_is_port() ? QueryObjectType::kPort : QueryObjectType::kPin, pin);
    } else {
      if (clocks_only)
        throw std::invalid_argument("constraint requires clock objects: " + name);
      for (const auto& pin : resolvePinTargets(interp, database, {object}))
        result.emplace(database.network().pins().at(pin).get_is_port() ? QueryObjectType::kPort : QueryObjectType::kPin, pin);
    }
  }
  if (result.empty())
    throw std::invalid_argument("constraint requires a non-empty clock, pin or port collection");
  return {result.begin(), result.end()};
}

std::string findClock(Tcl_Interp* interp, Database& database, Tcl_Obj* object)
{
  if (!object)
    throw std::invalid_argument("clock is required");
  // A scalar name may contain spaces; a typed collection retains its kind.
  if (SdcObject::getType(object) == SdcObjectType::kNone && database.sdc().get_clock_map().contains(Tcl_GetString(object)))
    return Tcl_GetString(object);
  const auto clocks = findClocks(interp, database, SdcObject::collection(object));
  if (clocks.size() != 1)
    throw std::invalid_argument("exactly one clock is required");
  return *clocks.begin();
}

std::set<std::string> findClocks(Tcl_Interp* interp, Database& database, const std::vector<Tcl_Obj*>& objects)
{
  std::set<std::string> names;
  for (const auto& [kind, name] : resolveClockTargets(interp, database, objects, true))
    names.insert(name);
  return names;
}

std::vector<std::string> findClockSources(Tcl_Interp* interp, Database& database, const std::vector<Tcl_Obj*>& objects)
{
  std::set<std::string> names;
  for (auto* object : objects) {
    const auto type = SdcObject::getType(object);
    const std::string name = Tcl_GetString(object);
    if (deferObjectBinding(interp, database) && (type == SdcObjectType::kPort || type == SdcObjectType::kPin || type == SdcObjectType::kNet)) {
      names.insert(name);
    } else if (type == SdcObjectType::kNone) {
      const auto sources = findClockSources(interp, database, std::vector<std::string>{name});
      names.insert(sources.begin(), sources.end());
    } else if (type == SdcObjectType::kPin && database.network().isHierarchyPin(name)) {
      database.network().checkedHierarchyPin(name);
      names.insert(name);
    } else if (type == SdcObjectType::kPort || type == SdcObjectType::kPin) {
      const auto targets = resolvePinTargets(interp, database, {object});
      names.insert(targets.begin(), targets.end());
    } else if (type == SdcObjectType::kNet) {
      const auto net = database.network().nets().find(database.network().physicalNet(name));
      if (net == database.network().nets().end())
        throw std::invalid_argument("clock source net no longer exists: " + name);
      for (const auto& pin_name : net->second.get_pin_name_list()) {
        const auto& pin = database.network().pins().at(pin_name);
        if (pin.get_direction() == PinDirection::kInout
            || (pin.get_is_port() ? pin.get_direction() == PinDirection::kInput : pin.get_direction() == PinDirection::kOutput))
          names.insert(pin_name);
      }
    } else
      throw std::invalid_argument("clock sources require pins, ports or nets: " + name);
  }
  return {names.begin(), names.end()};
}

std::vector<ClockSourceKind> findClockSourceKinds(Tcl_Interp* interp, Database& database, const std::vector<Tcl_Obj*>& objects,
                                                  const std::vector<std::string>& sources)
{
  std::map<std::string, ClockSourceKind> kinds;
  for (auto* object : objects) {
    for (const auto& source : findClockSources(interp, database, std::vector<Tcl_Obj*>{object})) {
      const auto type = deferObjectBinding(interp, database) ? queryObjectType(object) : getObjectType(database, source);
      const auto kind = type == QueryObjectType::kPin ? ClockSourceKind::kPin : type == QueryObjectType::kNet ? ClockSourceKind::kNet : ClockSourceKind::kPort;
      if (const auto [it, inserted] = kinds.emplace(source, kind); !inserted && it->second != kind)
        throw std::invalid_argument("ambiguous clock source identity: " + source);
    }
  }
  std::vector<ClockSourceKind> result;
  for (const auto& source : sources)
    result.push_back(kinds.at(source));
  return result;
}

std::vector<std::string> getFullNames(Tcl_Interp*, Database& database, const std::vector<Tcl_Obj*>& objects)
{
  std::vector<std::string> result;
  result.reserve(objects.size());
  for (Tcl_Obj* object : objects)
    result.push_back(objectFullName(database, object));
  return result;
}

TimingPortConstraint& getOrCreatePortConstraint(Database& database, const std::string& port_name)
{
  TimingPortConstraint& port_constraint = database.sdc().get_port_constraint_map()[port_name];
  port_constraint.set_port_name(port_name);
  return port_constraint;
}

}  // namespace ista::sdc
