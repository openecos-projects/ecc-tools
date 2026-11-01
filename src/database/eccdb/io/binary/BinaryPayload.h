// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <iosfwd>

namespace eccdb {

class DesignStore;
class LibraryStore;
class TechStore;

namespace binary_detail {

void writeTechPayload(std::ostream& output, const TechStore& database);
void writeLibraryPayload(std::ostream& output, const LibraryStore& database);
void writeDesignPayload(std::ostream& output, const DesignStore& database);

}  // namespace binary_detail
}  // namespace eccdb
