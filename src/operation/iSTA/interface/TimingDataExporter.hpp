// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include "TimingData.hpp"

namespace ista {

class Database;
class Config;

class TimingDataExporter
{
 public:
  static TimingData exportData(Database& database, const Config& config);
};

}  // namespace ista
