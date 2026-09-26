#include "binary/BinaryPayload.h"

#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "binary/BinaryArchive.h"
#include "binary/BinarySchema.h"
#include "tech/TechStore.h"

namespace eccdb::binary_detail {
namespace {

void writeGeometry(BinaryOutputArchive& archive, const GeometryPool& pool)
{
  archive(pool.options().polygon_mode);
  archive.writeSequence(pool.serializedEntries());
  archive.writeSequence(pool.serializedRectangles());
  archive.writeSequence(pool.serializedPoints());
  archive.writeSequence(pool.serializedPolygons());
}

}  // namespace

void writeTechPayload(std::ostream& output, const TechStore& database)
{
  BinaryOutputArchive archive(output);
  writeGeometry(archive, database.geometryPool());
  writeRegistrySnapshot<TechEntity>(database.techRegistry().registry(), archive, TechBinaryComponents{});
  archive.flush();
}

void writeLibraryPayload(std::ostream& output, const LibraryStore& database)
{
  BinaryOutputArchive archive(output);
  writeGeometry(archive, database.geometryPool());
  writeRegistrySnapshot<LibraryEntity>(database.libraryRegistry().registry(), archive, LibraryBinaryComponents{});
  archive.flush();
}

void writeDesignPayload(std::ostream& output, const DesignStore& database)
{
  BinaryOutputArchive archive(output);
  auto entity_mode = ActiveDesignEntitySchema::mode;
  archive(entity_mode);
  writeRegistrySnapshot<DesignEntity>(database.designRegistry().registry(), archive, DesignBinaryComponents{});
  archive.flush();
}

}  // namespace eccdb::binary_detail
