// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <filesystem>

namespace eccdb {

class DesignStore;
class LibraryStore;
class TechStore;

class BinaryDatabaseExporter
{
 public:
  static void saveTech(const std::filesystem::path& path, const TechStore& database);
  static void saveLibrary(const std::filesystem::path& path, const LibraryStore& library);
  static void saveDesign(const std::filesystem::path& path, const DesignStore& design);
};

}  // namespace eccdb
