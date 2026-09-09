#include "library/site/storage/SiteStorage.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "library/cell_master/model/CellMasterComponents.h"

namespace eccdb {

LibrarySiteId LibrarySiteStorage::createSite(LibrarySite site)
{
  validateSite(site);
  return createSiteTrusted(std::move(site));
}

LibrarySiteId LibrarySiteStorage::createSiteTrusted(LibrarySite site)
{
  const auto entity = _registry.create();
  try {
    _registry.emplace<LibrarySite>(entity, std::move(site));
    return LibrarySiteId{entity};
  } catch (...) {
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
}

bool LibrarySiteStorage::contains(LibrarySiteId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<LibrarySite>(id.entity());
}

LibrarySiteId LibrarySiteStorage::findSiteById(uint32_t id) const
{
  const auto entity = static_cast<LibraryEntity>(id);
  return _registry.valid(entity) && _registry.all_of<LibrarySite>(entity) ? LibrarySiteId{entity} : LibrarySiteId{};
}

LibrarySiteId LibrarySiteStorage::findSite(std::string_view name) const
{
  const auto view = _registry.view<const LibrarySite>();
  for (const auto entity : view) {
    if (view.get<const LibrarySite>(entity).name == name) {
      return LibrarySiteId{entity};
    }
  }
  return {};
}

std::vector<LibrarySiteId> LibrarySiteStorage::sites() const
{
  std::vector<LibrarySiteId> result;
  const auto view = _registry.view<const LibrarySite>();
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

std::size_t LibrarySiteStorage::siteCount() const
{
  std::size_t count = 0;
  const auto view = _registry.view<const LibrarySite>();
  for (const auto entity : view) {
    static_cast<void>(entity);
    ++count;
  }
  return count;
}

const LibrarySite& LibrarySiteStorage::site(LibrarySiteId id) const
{
  ensureSite(id);
  return _registry.get<const LibrarySite>(id.entity());
}

void LibrarySiteStorage::updateSite(LibrarySiteId id, LibrarySite site)
{
  ensureSite(id);
  validateSite(site, id);
  _registry.replace<LibrarySite>(id.entity(), std::move(site));
}

bool LibrarySiteStorage::destroySite(LibrarySiteId id)
{
  if (!contains(id)) {
    return false;
  }

  const auto masters = _registry.view<const LibraryCellMaster>();
  for (const auto entity : masters) {
    const auto& master = masters.get<const LibraryCellMaster>(entity);
    if (master.site.has_value() && *master.site == id) {
      return false;
    }
  }

  _registry.destroy(id.entity());
  return true;
}

void LibrarySiteStorage::ensureSite(LibrarySiteId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid library site id");
  }
}

void LibrarySiteStorage::validateSite(const LibrarySite& site, LibrarySiteId ignored) const
{
  if (site.name.empty()) {
    throw std::invalid_argument("library site name is required");
  }

  const auto existing = findSite(site.name);
  if (existing && existing != ignored) {
    throw std::invalid_argument("duplicate library site name: " + site.name);
  }
}

}  // namespace eccdb
