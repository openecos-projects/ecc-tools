#include "binary/BinaryPayload.h"

#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "binary/BinaryArchive.h"
#include "binary/BinarySchema.h"
#include "tech/TechStore.h"

#include <cstddef>
#include <type_traits>

namespace eccdb::binary_detail {
namespace {

template <typename Value>
inline constexpr bool kByteCompatibleRoutingRecord
    = ByteCompatibleArchiveValue<Value> && std::is_aggregate_v<Value>;

static_assert(kByteCompatibleRoutingRecord<DesignRoutingPointRecord>);
static_assert(kByteCompatibleRoutingRecord<DesignRoutingPointExtraEntry>);
static_assert(kByteCompatibleRoutingRecord<DesignRoutingViaRecord>);
static_assert(kByteCompatibleRoutingRecord<DesignRoutingViaExtraEntry>);
static_assert(kByteCompatibleRoutingRecord<DesignRoutingPathRecord>);
static_assert(kByteCompatibleRoutingRecord<DesignWireRectangle>);

static_assert(sizeof(DesignRoutingPointRecord) == 8u);
static_assert(sizeof(DesignRoutingPointExtraEntry) == 12u);
static_assert(sizeof(DesignRoutingViaRecord) == 16u);
static_assert(sizeof(DesignRoutingViaExtraEntry) == 32u);
static_assert(sizeof(DesignRoutingPathRecord) == 16u);
static_assert(sizeof(DesignWireRectangle) == 20u);

static_assert(offsetof(DesignRoutingPointRecord, position) == 0u);
static_assert(offsetof(DesignRoutingPointExtraEntry, point_index) == 0u);
static_assert(offsetof(DesignRoutingPointExtraEntry, flags) == 4u);
static_assert(offsetof(DesignRoutingPointExtraEntry, extension) == 8u);
static_assert(offsetof(DesignRoutingViaRecord, point_index) == 0u);
static_assert(offsetof(DesignRoutingViaRecord, meta) == 4u);
static_assert(offsetof(DesignRoutingViaRecord, reference) == 8u);
static_assert(offsetof(DesignRoutingPathRecord, meta) == 0u);
static_assert(offsetof(DesignRoutingPathRecord, point_end) == 4u);
static_assert(offsetof(DesignRoutingPathRecord, via_end) == 8u);
static_assert(offsetof(DesignRoutingPathRecord, rectangle_end) == 12u);
static_assert(offsetof(DesignWireRectangle, point_index) == 0u);
static_assert(offsetof(DesignWireRectangle, delta) == 4u);

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

  const auto routing = database.routingStorage().serializedRoutingPool();
  archive.writeSequence(routing.routing_layers);
  archive.writeByteCompatibleSequence(routing.paths);
  archive.writeByteCompatibleSequence(routing.points);
  archive.writeByteCompatibleSequence(routing.point_extras);
  archive.writeByteCompatibleSequence(routing.vias);
  archive.writeByteCompatibleSequence(routing.via_extras);
  archive.writeByteCompatibleSequence(routing.rectangles);
  archive.writeSequence(routing.path_extras);
  archive.flush();
}

}  // namespace eccdb::binary_detail
