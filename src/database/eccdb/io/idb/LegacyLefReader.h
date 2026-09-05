#pragma once

#include <memory>
#include <string>
#include <vector>

#include "lef_service.h"

namespace eccdb {

// Minimal legacy LEF loading path used by adapter differential tests. It
// deliberately excludes DEF, Verilog, GDS, JSON, and the full IdbBuilder.
class LegacyLefReader
{
 public:
  LegacyLefReader();
  ~LegacyLefReader();

  LegacyLefReader(const LegacyLefReader&) = delete;
  LegacyLefReader& operator=(const LegacyLefReader&) = delete;

  ::idb::IdbLefService* buildLef(std::vector<std::string>& files, bool technology_file = false);
  void updateLefData();

 private:
  void normalizeMacroOrigins();

  std::unique_ptr<::idb::IdbLefService> _service;
};

}  // namespace eccdb
