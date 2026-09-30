// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "DrtTrace.h"

namespace odb {
class dbNet;
}

namespace drt_trace {

// Decode the ordinary routed wire representation used by the trace schema.
std::vector<Shape> readOdbWire(odb::dbNet* net);

}  // namespace drt_trace
