// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "context/DatabaseContext.h"

#include <algorithm>

namespace eccdb {

DatabaseContext::DatabaseContext(DesignStore& design, TechStore* technology, LibraryStore* library) noexcept
    : _design(&design), _technology(technology), _library(library)
{
}

DatabaseContext::DatabaseContext(TechStore& technology, LibraryStore& library, DesignStore& design) noexcept
    : DatabaseContext(design, &technology, &library)
{
}

void DatabaseContext::addObserver(DatabaseObserver& observer)
{
  if (std::find(_observers.begin(), _observers.end(), &observer) == _observers.end()) {
    _observers.push_back(&observer);
  }
}

void DatabaseContext::removeObserver(DatabaseObserver& observer) noexcept
{
  std::erase(_observers, &observer);
}

void DatabaseContext::notifyBefore(const DatabaseChange& change) noexcept
{
  for (auto* observer : _observers) observer->beforeChange(change);
}

void DatabaseContext::notifyAfter(const DatabaseChange& change) noexcept
{
  ++_revision;
  for (auto* observer : _observers) observer->afterChange(change);
}

}  // namespace eccdb
