// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <mutex>

namespace eccdb::lef_detail {

// SI2 keeps parser callbacks and session state globally. Every direct LEF
// importer must hold this mutex for the complete sequence of input files.
[[nodiscard]] std::mutex& parserMutex();

}  // namespace eccdb::lef_detail
