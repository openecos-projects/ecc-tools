// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
#include <vector>

#include "Database.hpp"
#include "check/ClockCheck.hpp"
namespace ista {
// Single-pin clock period and high/low pulse checks; never selects data paths.
class ClockChecker
{
 public:
  explicit ClockChecker(Database& database) : _database(database) {}
  std::vector<ClockCheck> check();

 private:
  Database& _database;
};
}  // namespace ista
