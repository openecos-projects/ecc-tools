// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

namespace idb {
class LibCell;
}
namespace ipw {
class TimingCell;
void wrapLibertySupplies(TimingCell& cell, idb::LibCell& raw_cell);
}  // namespace ipw
