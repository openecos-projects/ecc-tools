// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
#include "Config.hpp"
#include "Database.hpp"

namespace ista {
class ElectricalReportWriter
{
 public:
  ElectricalReportWriter(const Database& database, const Config& config) : _database(database), _config(config) {}
  void write() const;

 private:
  const Database& _database;
  const Config& _config;
};
}  // namespace ista
