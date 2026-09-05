#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "library/LibraryStore.h"
#include "tech/TechStore.h"

namespace eccdb {

struct LefLibraryImportDiagnostic
{
  std::string statement;
  std::size_t occurrence_count = 0;
};

struct LefLibraryImportTiming
{
  uint64_t parser_microseconds = 0;
  uint64_t site_callback_microseconds = 0;
  uint64_t macro_callback_microseconds = 0;
  uint64_t pin_callback_microseconds = 0;
  uint64_t obstruction_callback_microseconds = 0;
  uint64_t geometry_prepare_microseconds = 0;
  uint64_t geometry_write_microseconds = 0;
};

// One-shot importer from SI2 LEF callbacks into one LibraryStore. Parser
// callbacks create EnTT objects while the file is being read; only the
// geometry belonging to the current callback is temporarily materialized.
// Unsupported non-geometric attributes are counted in diagnostics; geometry
// that cannot be represented without data loss is rejected.
class LefLibraryImporter
{
 public:
  LefLibraryImporter(const TechStore& technology, LibraryStore& library) : _technology(technology), _library(library) {}

  void import(const std::filesystem::path& file);
  void import(std::span<const std::filesystem::path> files);

  [[nodiscard]] const std::vector<LefLibraryImportDiagnostic>& diagnostics() const noexcept { return _diagnostics; }
  [[nodiscard]] const LefLibraryImportTiming& timing() const noexcept { return _timing; }

 private:
  const TechStore& _technology;
  LibraryStore& _library;
  std::vector<LefLibraryImportDiagnostic> _diagnostics;
  LefLibraryImportTiming _timing;
  bool _used = false;
};

}  // namespace eccdb
