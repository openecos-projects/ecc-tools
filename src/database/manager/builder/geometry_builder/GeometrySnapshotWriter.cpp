#include "GeometrySnapshotWriter.h"

#include "GeometrySnapshotSchema.h"

#include <chrono>
#include <fstream>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace ecc::geometry {
namespace {

bool write_bytes(std::ofstream& file, const void* data, uint64_t size)
{
  if (size == 0) {
    return static_cast<bool>(file);
  }

  file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
  return static_cast<bool>(file);
}

bool write_file(const std::filesystem::path& path, GeometryFileKind file_kind, uint32_t record_size, uint64_t record_count,
                const void* data, uint64_t data_size)
{
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }

  GeometryFileHeader header;
  header.file_kind = file_kind;
  header.record_size = record_size;
  header.record_count = record_count;
  header.payload_size = data_size;

  return write_bytes(file, &header, sizeof(header)) && write_bytes(file, data, data_size);
}

bool write_manifest(const std::filesystem::path& path, const SnapshotWriteResult& result, const std::string& file_prefix)
{
  std::ofstream file(path);
  if (!file) {
    return false;
  }

  file << "schema_version=" << kGeometrySchemaVersion << '\n';
  file << "active_epoch=" << result.epoch << '\n';
  file << "shape_count=" << result.shape_count << '\n';
  file << "owner_count=" << result.owner_count << '\n';
  file << "payload_size=" << result.payload_size << '\n';
  file << "meta=" << file_prefix << "/geometry.meta.bin\n";
  file << "shapes=" << file_prefix << "/geometry.shapes.bin\n";
  file << "owners=" << file_prefix << "/geometry.owners.bin\n";
  file << "payload=" << file_prefix << "/geometry.payload.bin\n";
  file << "names=" << file_prefix << "/geometry.names.bin\n";
  file << "name_index=" << file_prefix << "/geometry.name_index.bin\n";
  file << "sidmap=" << file_prefix << "/geometry.sidmap.bin\n";
  file << "delta=" << file_prefix << "/geometry.delta.bin\n";
  file << "view=" << file_prefix << "/geometry.view.bin\n";
  return static_cast<bool>(file);
}

uint64_t create_epoch_directory(const std::filesystem::path& output_dir, std::filesystem::path& epoch_dir)
{
  const std::filesystem::path epochs_dir = output_dir / "epochs";
  std::error_code error;
  std::filesystem::create_directories(epochs_dir, error);
  if (error) {
    return 0;
  }

  uint64_t epoch = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
  while (epoch != 0) {
    epoch_dir = epochs_dir / std::to_string(epoch);
    error.clear();
    if (std::filesystem::create_directory(epoch_dir, error)) {
      return epoch;
    }
    if (error) {
      return 0;
    }
    ++epoch;
  }
  return 0;
}

bool publish_manifest(const std::filesystem::path& output_dir, const SnapshotWriteResult& result,
                      const std::string& file_prefix)
{
  const std::filesystem::path manifest_path = output_dir / "geometry.manifest";
  const std::filesystem::path temporary_path = output_dir / "geometry.manifest.tmp";
  std::error_code error;
  std::filesystem::remove(temporary_path, error);
  if (!write_manifest(temporary_path, result, file_prefix)) {
    return false;
  }

  error.clear();
  std::filesystem::rename(temporary_path, manifest_path, error);
  return !error;
}

ShapeId next_shape_id_from_records(std::span<const ShapeRecord> records)
{
  ShapeId next_shape_id = 1;
  for (const ShapeRecord& record : records) {
    if (record.id >= next_shape_id) {
      next_shape_id = record.id + 1;
    }
  }

  return next_shape_id;
}

std::vector<GeometrySidMapRecord> make_sidmap_records(std::span<const ShapeRecord> records, std::span<const OwnerRef> owners)
{
  std::vector<GeometrySidMapRecord> sidmap_records;
  sidmap_records.reserve(records.size());

  for (const ShapeRecord& record : records) {
    if (record.owner_index >= owners.size()) {
      continue;
    }

    GeometrySidMapRecord sidmap_record;
    sidmap_record.shape_id = record.id;
    sidmap_record.owner = owners[record.owner_index];
    sidmap_records.push_back(sidmap_record);
  }

  return sidmap_records;
}

std::vector<GeometryViewTileRecord> make_view_tile_records(std::span<const GeometryTileSummary> summaries)
{
  std::vector<GeometryViewTileRecord> view_records;
  view_records.reserve(summaries.size());
  for (const GeometryTileSummary& summary : summaries) {
    GeometryViewTileRecord record;
    record.lod_level = summary.lod_level;
    record.layer_id = summary.layer_id;
    record.tile_x = summary.tile_x;
    record.tile_y = summary.tile_y;
    record.shape_count = summary.shape_count;
    record.bbox = normalize(summary.bbox);
    view_records.push_back(record);
  }

  return view_records;
}

}  // namespace

SnapshotWriteResult GeometrySnapshotWriter::write(GeometryStore& store, const SnapshotWriteOptions& options) const
{
  SnapshotWriteResult result;
  result.shape_count = static_cast<uint64_t>(store.records().size());
  result.owner_count = static_cast<uint64_t>(store.owners().size());
  result.payload_size = static_cast<uint64_t>(store.payloads().size());
  result.delta_count = static_cast<uint64_t>(store.delta_events().size());
  result.manifest_path = options.output_dir / "geometry.manifest";

  std::error_code error;
  std::filesystem::create_directories(options.output_dir, error);
  if (error) {
    return result;
  }

  std::filesystem::path epoch_dir;
  result.epoch = create_epoch_directory(options.output_dir, epoch_dir);
  if (result.epoch == 0) {
    return result;
  }

  store.rebuild_dirty_lod_tiles();

  const std::span<const ShapeRecord> records = store.records();
  const std::span<const OwnerRef> owners = store.owners();
  const std::span<const std::byte> payloads = store.payloads();
  const std::span<const GeometryNameRecord> name_records = store.name_records();
  const std::span<const std::byte> name_payloads = store.name_payloads();
  const std::span<const GeometryDeltaEvent> delta_events = store.delta_events();
  const std::vector<GeometrySidMapRecord> sidmap_records = make_sidmap_records(records, owners);
  const std::vector<GeometryTileSummary> lod_summaries = store.lod_summaries();
  const std::vector<GeometryViewTileRecord> view_records = make_view_tile_records(lod_summaries);

  GeometryMetaRecord meta;
  meta.shape_count = result.shape_count;
  meta.owner_count = result.owner_count;
  meta.payload_size = result.payload_size;
  meta.name_record_count = static_cast<uint64_t>(name_records.size());
  meta.name_payload_size = static_cast<uint64_t>(name_payloads.size());
  meta.next_shape_id = next_shape_id_from_records(records);

  const bool wrote_meta =
      write_file(epoch_dir / "geometry.meta.bin", GeometryFileKind::kMeta, sizeof(GeometryMetaRecord), 1, &meta,
                 sizeof(meta));
  const bool wrote_shapes =
      write_file(epoch_dir / "geometry.shapes.bin", GeometryFileKind::kShapes, sizeof(ShapeRecord),
                 result.shape_count, records.data(), records.size_bytes());
  const bool wrote_owners = write_file(epoch_dir / "geometry.owners.bin", GeometryFileKind::kOwners,
                                       sizeof(OwnerRef), result.owner_count, owners.data(), owners.size_bytes());
  const bool wrote_payload = write_file(epoch_dir / "geometry.payload.bin", GeometryFileKind::kPayload, 1,
                                        result.payload_size, payloads.data(), payloads.size_bytes());
  const bool wrote_names = write_file(epoch_dir / "geometry.names.bin", GeometryFileKind::kNames, 1,
                                      name_payloads.size(), name_payloads.data(), name_payloads.size_bytes());
  const bool wrote_name_index =
      write_file(epoch_dir / "geometry.name_index.bin", GeometryFileKind::kNameIndex, sizeof(GeometryNameRecord),
                 name_records.size(), name_records.data(), name_records.size_bytes());
  const bool wrote_sidmap =
      write_file(epoch_dir / "geometry.sidmap.bin", GeometryFileKind::kSidMap, sizeof(GeometrySidMapRecord),
                 sidmap_records.size(), sidmap_records.data(),
                 static_cast<uint64_t>(sidmap_records.size() * sizeof(GeometrySidMapRecord)));
  const bool wrote_delta =
      write_file(epoch_dir / "geometry.delta.bin", GeometryFileKind::kDelta, sizeof(GeometryDeltaEvent),
                 delta_events.size(), delta_events.data(), delta_events.size_bytes());
  const bool wrote_view =
      write_file(epoch_dir / "geometry.view.bin", GeometryFileKind::kView, sizeof(GeometryViewTileRecord),
                 view_records.size(), view_records.data(),
                 static_cast<uint64_t>(view_records.size() * sizeof(GeometryViewTileRecord)));
  const bool wrote_files = wrote_meta && wrote_shapes && wrote_owners && wrote_payload && wrote_names && wrote_name_index
                           && wrote_sidmap && wrote_delta && wrote_view;
  const std::string file_prefix = (std::filesystem::path("epochs") / std::to_string(result.epoch)).generic_string();
  const bool wrote_manifest = wrote_files && publish_manifest(options.output_dir, result, file_prefix);

  result.ok = wrote_files && wrote_manifest;
  return result;
}

}  // namespace ecc::geometry
