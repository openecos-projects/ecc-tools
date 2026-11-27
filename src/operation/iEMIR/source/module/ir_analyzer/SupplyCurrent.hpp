// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include "Database.hpp"
#include "IAModel.hpp"

namespace iemir {

double graphSourceVoltage(PowerGraph& graph, std::map<uint64_t, InstancePower>& powers);
void distributeNodeCurrent(IAModel& model, const std::vector<std::size_t>& nodes, const std::map<std::size_t, double>& weights, double current);
void buildSupplyCurrent(PowerGraph& graph, uint64_t instance_id, InstancePower& power, IAModel& model);
void reportSupplyCoverage(Database& database, const std::string& path, bool allow_unmapped_pg_pins = false);

}  // namespace iemir
