// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

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
