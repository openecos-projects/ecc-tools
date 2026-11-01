// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <mutex>

namespace eccdb::def_detail {

// SI2 DEF callbacks and parser session state are process-global.
[[nodiscard]] std::mutex& parserMutex();

}  // namespace eccdb::def_detail
