// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <filesystem>
#include <iosfwd>

namespace eccdb {

class TechStore;

// Deterministic LEF writer for the technology data represented by
// TechStore. It writes a canonical supported subset, rather than preserving
// source whitespace or cross-category statement order.
class LefTechExporter
{
 public:
  static void write(std::ostream& output, const TechStore& database);
  static void write(const std::filesystem::path& path, const TechStore& database);
};

}  // namespace eccdb
