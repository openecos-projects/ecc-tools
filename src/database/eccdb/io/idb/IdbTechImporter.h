#pragma once

#include <string_view>
#include <unordered_map>

#include "tech/TechStore.h"

namespace idb {
class IdbLayer;
class IdbLayout;
}  // namespace idb

namespace eccdb {

// One-shot adapter from the legacy LEF/iDB layout into the EnTT Tech model.
// Source pointers are used only while importing; no legacy pointer or ID is
// written into a persistent component.
class IdbTechImporter
{
 public:
  explicit IdbTechImporter(TechStore& database) : _database(database) {}

  void import(::idb::IdbLayout& source);

  [[nodiscard]] TechLayerId layerId(const ::idb::IdbLayer* source) const;
  [[nodiscard]] TechViaMasterId viaMasterId(std::string_view name) const;

 private:
  TechStore& _database;
  std::unordered_map<const ::idb::IdbLayer*, TechLayerId> _layer_ids;
  bool _imported = false;
};

}  // namespace eccdb
