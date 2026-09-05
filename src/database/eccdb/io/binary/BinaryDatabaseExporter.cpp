#include "binary/BinaryDatabaseExporter.h"

#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "binary/BinaryFormat.h"
#include "binary/BinaryPayload.h"
#include "binary/BinarySchema.h"
#include "tech/TechStore.h"

namespace eccdb {
void BinaryDatabaseExporter::saveTech(const std::filesystem::path& path, const TechStore& database)
{
  binary_detail::BinaryFileHeader header;
  header.kind = binary_detail::BinaryDatabaseKind::kTech;
  header.schema_version = binary_detail::kTechSchemaVersion;
  binary_detail::writeBinaryFile(path, header,
                                 [&database](std::ostream& output) { binary_detail::writeTechPayload(output, database); });
}

void BinaryDatabaseExporter::saveLibrary(const std::filesystem::path& path, const LibraryStore& library)
{
  binary_detail::BinaryFileHeader header;
  header.kind = binary_detail::BinaryDatabaseKind::kLibrary;
  header.schema_version = binary_detail::kLibrarySchemaVersion;
  binary_detail::writeBinaryFile(path, header,
                                 [&library](std::ostream& output) { binary_detail::writeLibraryPayload(output, library); });
}

void BinaryDatabaseExporter::saveDesign(const std::filesystem::path& path, const DesignStore& design)
{
  binary_detail::BinaryFileHeader header;
  header.kind = binary_detail::BinaryDatabaseKind::kDesign;
  header.schema_version = binary_detail::kDesignSchemaVersion;
  binary_detail::writeBinaryFile(path, header,
                                 [&design](std::ostream& output) { binary_detail::writeDesignPayload(output, design); });
}

}  // namespace eccdb
