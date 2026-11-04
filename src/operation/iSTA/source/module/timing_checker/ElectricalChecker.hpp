// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
#include <vector>

#include "Database.hpp"

namespace ista {
class DelayCalculator;
class ElectricalChecker
{
 public:
  ElectricalChecker(Database& database, DelayCalculator& delay) : _database(database), _delay(delay) {}
  std::vector<ElectricalCheck> check();

 private:
  std::optional<double> limit(ElectricalRule rule, const Pin& pin, const std::string& clock, bool clock_path, TransType edge);
  Database& _database;
  DelayCalculator& _delay;
};
}  // namespace ista
