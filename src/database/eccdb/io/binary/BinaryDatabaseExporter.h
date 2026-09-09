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
