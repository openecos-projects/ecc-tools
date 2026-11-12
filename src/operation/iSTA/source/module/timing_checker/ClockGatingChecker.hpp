// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

namespace ista {
class Database;

// Infer zero-margin checks on combinational clock gates after clock propagation.
// These checks belong to the analyzed graph, not the linked Liberty/netlist.
class ClockGatingChecker
{
 public:
  explicit ClockGatingChecker(Database& database) : _database(database) {}
  void infer();

 private:
  Database& _database;
};
}  // namespace ista
