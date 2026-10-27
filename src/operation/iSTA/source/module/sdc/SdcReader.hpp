// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
namespace ista {
class Sdc;
// The command interpreter belongs to the active STA session. Pass that session's
// Sdc; this function does not create an interpreter for an independent database.
void readSdc(Sdc& constraints);
}  // namespace ista
