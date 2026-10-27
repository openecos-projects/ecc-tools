// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <vector>

#include "check/FanoutCheck.hpp"
#include "network/Network.hpp"

namespace ista {
class Database;
class Sdc;
class LibertyLibrary;
class SimValues;

// Computes electrical checks independently of path search. The caller owns
// result publication; inputs must remain unchanged for the duration of check().
class FanoutChecker
{
 public:
  explicit FanoutChecker(const Database& database) : _database(database) {}
  std::vector<FanoutCheck> check() const;

 private:
  const Database& _database;
};
}  // namespace ista
