// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

namespace ista {

class Database;
class Config;
struct TimingData;

void exportMinTimingData(TimingData& snapshot, Database& database, const Config& config);
void exportMinTimingModel(TimingData& snapshot, Database& database, const Config& config);

}  // namespace ista
