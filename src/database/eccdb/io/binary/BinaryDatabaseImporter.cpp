#include "binary/BinaryDatabaseImporter.h"

#include <stdexcept>
#include <utility>
#include <vector>

#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "binary/BinaryArchive.h"
#include "binary/BinaryFormat.h"
#include "binary/BinarySchema.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

struct SerializedGeometry
{
  GeometryPoolOptions options;
  std::vector<GeometryPoolEntry> entries;
  std::vector<Rect> rectangles;
  std::vector<Point> points;
  std::vector<GeometryPoolPolygon> polygons;
};

SerializedGeometry readGeometry(binary_detail::BinaryInputArchive& archive)
{
  SerializedGeometry geometry;
  archive(geometry.options.polygon_mode, geometry.entries, geometry.rectangles, geometry.points, geometry.polygons);
  return geometry;
}

}  // namespace

std::unique_ptr<TechStore> BinaryDatabaseImporter::loadTech(const std::filesystem::path& path)
{
  std::unique_ptr<TechStore> result;
  binary_detail::readBinaryFile(
      path, binary_detail::BinaryDatabaseKind::kTech, binary_detail::kTechSchemaVersion,
      [&result](std::istream& input, const binary_detail::BinaryFileHeader&) {
        binary_detail::BinaryInputArchive archive(input);
        auto geometry = readGeometry(archive);
        result = std::make_unique<TechStore>(TechStoreOptions{.geometry = geometry.options});
        result->resetForBinaryLoad(geometry.options);
        result->geometryPool().restoreSerialized(geometry.options, std::move(geometry.entries), std::move(geometry.rectangles),
                                                 std::move(geometry.points), std::move(geometry.polygons));
        binary_detail::readRegistrySnapshot<TechEntity>(result->techRegistry().registry(), archive, binary_detail::TechBinaryComponents{});
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
        auto geometry = readGeometry(archive);
        result = std::make_unique<LibraryStore>(technology.techRegistry(), LibraryStoreOptions{.geometry = geometry.options});
        result->geometryPool().restoreSerialized(geometry.options, std::move(geometry.entries), std::move(geometry.rectangles),
                                                 std::move(geometry.points), std::move(geometry.polygons));
        binary_detail::readRegistrySnapshot<LibraryEntity>(result->libraryRegistry().registry(), archive,
                                                           binary_detail::LibraryBinaryComponents{});
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
        result = std::make_unique<DesignStore>(technology.techRegistry(), library.libraryRegistry());
        result->resetForBinaryLoad();
        binary_detail::readRegistrySnapshot<DesignEntity>(result->designRegistry().registry(), archive,
                                                          binary_detail::DesignBinaryComponents{});
        DesignRoutingPoolData routing;
        archive(routing.routing_layers);
        archive.readByteCompatibleSequence(routing.paths);
        archive.readByteCompatibleSequence(routing.points);
        archive.readByteCompatibleSequence(routing.point_extras);
        archive.readByteCompatibleSequence(routing.vias);
        archive.readByteCompatibleSequence(routing.via_extras);
        archive.readByteCompatibleSequence(routing.rectangles);
        archive(routing.path_extras);
        result->routingStorage().restoreSerializedRoutingPool(std::move(routing));
        result->bindBinaryLoadedRoot();
      });
  return result;
}

}  // namespace eccdb
