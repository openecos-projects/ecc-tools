// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <string>

#include "advance/TimingEndpointQualification.hpp"
#include "advance/TransType.hpp"

namespace ista {

class Database;
class TimingCheckArc;
class TimingArc;

bool hasCaptureClockState(Database& database, const std::string& pin_name, const std::string& clock_name, TransType transition);
TimingMaxQualification qualifyMaxOutput(Database& database, const std::string& pin_name);
TimingMaxQualification qualifyMaxCheck(Database& database, TimingCheckArc& check, TimingArc& arc);

}  // namespace ista
