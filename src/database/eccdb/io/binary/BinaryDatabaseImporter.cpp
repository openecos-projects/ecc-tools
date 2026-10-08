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

#include "binary/BinaryDatabaseImporter.h"

#include <stdexcept>
#include <utility>

#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "binary/BinaryArchive.h"
#include "binary/BinaryFormat.h"
#include "binary/BinarySchema.h"
#include "tech/TechStore.h"

namespace eccdb {
std::unique_ptr<TechStore> BinaryDatabaseImporter::loadTech(const std::filesystem::path& path)
{
  std::unique_ptr<TechStore> result;
  binary_detail::readBinaryFile(
      path, binary_detail::BinaryDatabaseKind::kTech, binary_detail::kTechSchemaVersion,
      [&result](std::istream& input, const binary_detail::BinaryFileHeader&) {
        binary_detail::BinaryInputArchive archive(input);
        result = std::make_unique<TechStore>();
        result->resetForBinaryLoad();
        binary_detail::readRegistrySnapshot<TechEntity>(result->techRegistry().registry(), archive, binary_detail::TechBinaryComponents{});
        binary_detail::readRegistrySnapshot<GeometryEntity>(result->geometryRegistry().registry(), archive,
                                                             binary_detail::DomainGeometryBinaryComponents{});
        result->bindBinaryLoadedRoot();
      });
  return result;
}

std::unique_ptr<LibraryStore> BinaryDatabaseImporter::loadLibrary(const std::filesystem::path& path,
                                                                     const TechStore& technology)
{
  std::unique_ptr<LibraryStore> result;
  binary_detail::readBinaryFile(
      path, binary_detail::BinaryDatabaseKind::kLibrary, binary_detail::kLibrarySchemaVersion,
      [&result, &technology](std::istream& input, const binary_detail::BinaryFileHeader&) {
        binary_detail::BinaryInputArchive archive(input);
        result = std::make_unique<LibraryStore>(technology.techRegistry());
        binary_detail::readRegistrySnapshot<LibraryEntity>(result->libraryRegistry().registry(), archive,
                                                           binary_detail::LibraryBinaryComponents{});
        binary_detail::readRegistrySnapshot<GeometryEntity>(result->geometryRegistry().registry(), archive,
                                                             binary_detail::LibraryGeometryBinaryComponents{});
        result->validateRestoredGeometry();
        result->masterTermStorage().rebuildNameIndexes();
      });
  return result;
}

std::unique_ptr<DesignStore> BinaryDatabaseImporter::loadDesign(const std::filesystem::path& path, const TechStore& technology,
                                                                   const LibraryStore& library)
{
  std::unique_ptr<DesignStore> result;
  binary_detail::readBinaryFile(
      path, binary_detail::BinaryDatabaseKind::kDesign, binary_detail::kDesignSchemaVersion,
      [&result, &technology, &library](std::istream& input, const binary_detail::BinaryFileHeader&) {
        binary_detail::BinaryInputArchive archive(input);
        DesignEntityMode entity_mode{};
        archive(entity_mode);
        if (entity_mode != ActiveDesignEntitySchema::mode) {
          throw std::runtime_error("design binary entity mode does not match this build");
        }
        result = std::make_unique<DesignStore>(technology.techRegistry(), library.libraryRegistry(),
                                               &technology.geometryRegistry());
        result->resetForBinaryLoad();
        binary_detail::readRegistrySnapshot<DesignEntity>(result->designRegistry().registry(), archive,
                                                          binary_detail::DesignBinaryComponents{});
        binary_detail::readRegistrySnapshot<GeometryEntity>(result->geometryRegistry().registry(), archive,
                                                             binary_detail::DesignGeometryBinaryComponents{});
        result->bindBinaryLoadedRoot();
      });
  return result;
}

}  // namespace eccdb
