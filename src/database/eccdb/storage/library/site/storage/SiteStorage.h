#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "library/LibraryRegistry.h"
#include "library/site/model/SiteComponents.h"

namespace eccdb {

class LibrarySiteStorage
{
 public:
  using registry_type = LibraryRegistry::registry_type;

  explicit LibrarySiteStorage(LibraryRegistry& registry) : _registry(registry.registry()) {}

  [[nodiscard]] LibrarySiteId createSite(LibrarySite site);
  [[nodiscard]] LibrarySiteId createSiteTrusted(LibrarySite site);
  [[nodiscard]] bool contains(LibrarySiteId id) const;
  [[nodiscard]] LibrarySiteId findSiteById(uint32_t id) const;
  [[nodiscard]] LibrarySiteId findSite(std::string_view name) const;
  [[nodiscard]] std::vector<LibrarySiteId> sites() const;
  [[nodiscard]] std::size_t siteCount() const;

  [[nodiscard]] const LibrarySite& site(LibrarySiteId id) const;
  void updateSite(LibrarySiteId id, LibrarySite site);
  [[nodiscard]] bool destroySite(LibrarySiteId id);

 private:
  void ensureSite(LibrarySiteId id) const;
  void validateSite(const LibrarySite& site, LibrarySiteId ignored = {}) const;

  registry_type& _registry;
};

}  // namespace eccdb
