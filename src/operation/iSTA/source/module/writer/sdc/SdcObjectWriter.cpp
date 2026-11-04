// SPDX-License-Identifier: MulanPSL-2.0
#include "SdcObjectWriter.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Database.hpp"
#if __has_include(<tcl8.6/tcl.h>)
#include <tcl8.6/tcl.h>
#else
#include <tcl.h>
#endif

namespace ista::sdc {

std::string sdcWord(const std::string& value)
{
  Tcl_DString buffer;
  Tcl_DStringInit(&buffer);
  Tcl_DStringAppendElement(&buffer, value.c_str());
  std::string result(Tcl_DStringValue(&buffer), Tcl_DStringLength(&buffer));
  Tcl_DStringFree(&buffer);
  return result;
}

std::string sdcNumber(double value)
{
  if (!std::isfinite(value))
    throw std::runtime_error("cannot export a non-finite SDC value");
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
  return output.str();
}

std::string sdcNumbers(const std::vector<double>& values)
{
  std::string result;
  for (double value : values) {
    if (!result.empty())
      result += ' ';
    result += sdcNumber(value);
  }
  return sdcWord(result);
}

std::string sdcQuery(const std::string& query, const std::vector<std::string>& names)
{
  std::string patterns;
  for (const auto& name : names) {
    // Protect glob metacharacters as well as Tcl substitutions. There are two
    // list levels: the pattern list, then the command word containing that list.
    std::string literal;
    for (char c : name) {
      if (c == '*' || c == '?' || c == '[' || c == ']' || c == '\\')
        literal += '\\';
      literal += c;
    }
    if (!patterns.empty())
      patterns += ' ';
    patterns += sdcWord(literal);
  }
  return "[" + query + " " + sdcWord(patterns) + "]";
}

namespace {
std::pair<std::string, std::string> objectQuery(Database& database, const std::string& name, bool exception)
{
  if (const auto pin = database.network().pins().find(name); pin != database.network().pins().end()) {
    if (pin->second.get_is_port())
      return {"get_ports", name};
    return {"get_pins", pin->second.get_instance_name() + "/" + pin->second.get_pin_name()};
  }
  if (const auto pin = database.network().hierarchyPins().find(name); pin != database.network().hierarchyPins().end()) {
    return {"get_pins", pin->second.instance + "/" + pin->second.port};
  }
  if (exception) {
    if (database.sdc().get_clock_map().contains(name))
      return {"get_clocks", name};
    if (database.network().instances().contains(name))
      return {"get_cells", name};
    if (database.network().hasNet(name))
      return {"get_nets", name};
  }
  throw std::runtime_error("cannot export SDC object '" + name + "'");
}

std::string objects(Database& database, const std::vector<std::string>& names, bool exception)
{
  std::vector<std::pair<std::string, std::string>> references;
  for (const auto& name : names)
    references.push_back(objectQuery(database, name, exception));
  if (references.empty())
    return "{}";
  const auto& query = references.front().first;
  if (std::all_of(references.begin(), references.end(), [&](const auto& ref) { return ref.first == query; })) {
    std::vector<std::string> external;
    for (const auto& ref : references)
      external.push_back(ref.second);
    return sdcQuery(query, external);
  }
  std::string result = "[list";
  for (const auto& [command, name] : references)
    result += " " + sdcQuery(command, {name});
  return result + "]";
}
}  // namespace

std::string sdcObjects(Database& database, const std::vector<std::string>& names)
{
  return objects(database, names, false);
}
std::string sdcExceptionObjects(Database& database, const ExceptionObjects& objects)
{
  std::map<std::string, std::vector<std::string>> groups;
  for (const auto& object : objects) {
    switch (object.type) {
      case ExceptionObjectType::kPort:
        groups["get_ports"].push_back(object.name);
        break;
      case ExceptionObjectType::kClock:
        groups["get_clocks"].push_back(object.name);
        break;
      case ExceptionObjectType::kCell:
        groups["get_cells"].push_back(object.name);
        break;
      case ExceptionObjectType::kNet:
        groups["get_nets"].push_back(object.name);
        break;
      case ExceptionObjectType::kPin: {
        const auto reference = objectQuery(database, object.name, false);
        groups[reference.first].push_back(reference.second);
        break;
      }
    }
  }
  if (groups.empty())
    return "{}";
  if (groups.size() == 1)
    return sdcQuery(groups.begin()->first, groups.begin()->second);
  std::string result = "[list";
  for (const auto& [query, names] : groups)
    result += " " + sdcQuery(query, names);
  return result + "]";
}

}  // namespace ista::sdc
