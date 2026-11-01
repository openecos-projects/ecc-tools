// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "binary/BinaryPayload.h"

#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "binary/BinaryArchive.h"
#include "binary/BinarySchema.h"
#include "tech/TechStore.h"

namespace eccdb::binary_detail {
void writeTechPayload(std::ostream& output, const TechStore& database)
{
  BinaryOutputArchive archive(output);
  writeRegistrySnapshot<TechEntity>(database.techRegistry().registry(), archive, TechBinaryComponents{});
  writeRegistrySnapshot<GeometryEntity>(database.geometryRegistry().registry(), archive, DomainGeometryBinaryComponents{});
  archive.flush();
}

void writeLibraryPayload(std::ostream& output, const LibraryStore& database)
{
  BinaryOutputArchive archive(output);
  writeRegistrySnapshot<LibraryEntity>(database.libraryRegistry().registry(), archive, LibraryBinaryComponents{});
  writeRegistrySnapshot<GeometryEntity>(database.geometryRegistry().registry(), archive, LibraryGeometryBinaryComponents{});
  archive.flush();
}

void writeDesignPayload(std::ostream& output, const DesignStore& database)
{
  BinaryOutputArchive archive(output);
  auto entity_mode = ActiveDesignEntitySchema::mode;
  archive(entity_mode);
  writeRegistrySnapshot<DesignEntity>(database.designRegistry().registry(), archive, DesignBinaryComponents{});
  writeRegistrySnapshot<GeometryEntity>(database.geometryRegistry().registry(), archive, DesignGeometryBinaryComponents{});
  archive.flush();
}

}  // namespace eccdb::binary_detail
