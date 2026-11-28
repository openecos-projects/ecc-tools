// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <string>

namespace idb {

// PG pins are not signal ports: retain their Liberty identity separately.
struct LibSupplyPin
{
  std::string pg_type;
  std::string voltage_name;
};

}  // namespace idb
