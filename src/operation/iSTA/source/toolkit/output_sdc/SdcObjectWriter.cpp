// SPDX-License-Identifier: MulanPSL-2.0
#include "SdcObjectWriter.hpp"

#include "Database.hpp"
#include "SdcCommand.hpp"

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
  if (const auto pin = database.get_pin_map().find(name); pin != database.get_pin_map().end()) {
    if (pin->second.get_is_port())
      return {"get_ports", name};
    return {"get_pins", pin->second.get_instance_name() + "/" + pin->second.get_pin_name()};
  }
  if (const auto pin = database.get_hierarchy_pin_map().find(name); pin != database.get_hierarchy_pin_map().end()) {
    return {"get_pins", pin->second.instance + "/" + pin->second.port};
  }
  if (exception) {
    if (database.get_timing_constraint().get_clock_map().contains(name))
      return {"get_clocks", name};
    if (database.get_instance_map().contains(name))
      return {"get_cells", name};
    if (database.get_net_map().contains(name))
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
std::string sdcExceptionObjects(Database& database, const std::set<std::string>& names)
{
  return objects(database, {names.begin(), names.end()}, true);
}

}  // namespace ista::sdc
