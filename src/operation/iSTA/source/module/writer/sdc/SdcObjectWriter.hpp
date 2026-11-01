// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <set>
#include <string>
#include <vector>

#include "sdc/TimingException.hpp"

namespace ista {
class Database;
namespace sdc {
std::string sdcWord(const std::string& value);
std::string sdcNumber(double value);
std::string sdcNumbers(const std::vector<double>& values);
// query is an SDC query command such as get_ports or get_clocks.
std::string sdcQuery(const std::string& query, const std::vector<std::string>& names);
// Resolve stored internal pin names to external instance/port names.
std::string sdcObjects(Database& database, const std::vector<std::string>& names);
std::string sdcExceptionObjects(Database& database, const ExceptionObjects& names);
}  // namespace sdc
}  // namespace ista
