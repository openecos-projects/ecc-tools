#pragma once

#include <filesystem>
#include <memory>

namespace eccdb {

class DesignStore;
class LibraryStore;
class TechStore;

class BinaryDatabaseImporter
{
 public:
  [[nodiscard]] static std::unique_ptr<TechStore> loadTech(const std::filesystem::path& path);
  [[nodiscard]] static std::unique_ptr<LibraryStore> loadLibrary(const std::filesystem::path& path,
                                                                    const TechStore& technology);
  [[nodiscard]] static std::unique_ptr<DesignStore> loadDesign(const std::filesystem::path& path, const TechStore& technology,
                                                                  const LibraryStore& library);
};

}  // namespace eccdb
