// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <any>
#include <map>
#include <string>

namespace eccdb {
class DatabaseContext;
}

namespace imj {

// Run iMJ's shared metal-fill planner directly against EccDB geometry and
// commit the resulting rectangles as design Fill objects.
void insertMetalEccdb(eccdb::DatabaseContext& context, std::map<std::string, std::any>& config);

}  // namespace imj
