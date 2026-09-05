#pragma once

#include <filesystem>
#include <span>

#include "tech/TechStore.h"

namespace eccdb {

// One-shot direct importer from SI2 LEF parser callbacks into the EnTT Tech
// model. Parsing is staged, so parser objects and partial input never become
// persistent database state.
class LefTechImporter
{
 public:
  explicit LefTechImporter(TechStore& database) : _database(database) {}

  void import(const std::filesystem::path& file);
  void import(std::span<const std::filesystem::path> files);

 private:
  TechStore& _database;
  bool _used = false;
};

}  // namespace eccdb
