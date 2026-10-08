// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

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
