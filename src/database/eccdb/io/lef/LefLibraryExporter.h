#pragma once

#include <filesystem>
#include <iosfwd>

namespace eccdb {

class LibraryStore;
class TechStore;

// Deterministic LEF writer for one LibraryStore. Technology references are
// resolved through the supplied TechStore and are emitted by name.
class LefLibraryExporter
{
 public:
  static void write(std::ostream& output, const TechStore& technology, const LibraryStore& library);
  static void write(const std::filesystem::path& path, const TechStore& technology, const LibraryStore& library);
};

}  // namespace eccdb
