#include "GeometrySnapshotReader.h"

#include "GeometrySnapshotSchema.h"

#include <cstddef>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ecc::geometry {
namespace {

std::unordered_map<std::string, std::string> read_manifest_values(const std::filesystem::path& path)
{
  std::unordered_map<std::string, std::string> values;
  std::ifstream file(path);
  std::string line;

  while (std::getline(file, line)) {
    const std::string::size_type delimiter = line.find('=');
    if (delimiter == std::string::npos) {
      continue;
    }

    values[line.substr(0, delimiter)] = line.substr(delimiter + 1);
  }

  return values;
}

bool read_header(std::ifstream& file, GeometryFileKind expected_kind, uint32_t expected_record_size, GeometryFileHeader& header)
{
  file.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (!file) {
    return false;
  }

  return header.magic == kGeometryFileMagic && header.schema_version == kGeometrySchemaVersion
         && header.header_size == sizeof(GeometryFileHeader) && header.file_kind == expected_kind
         && header.record_size == expected_record_size;
}

template <typename Record>
bool read_record_file(const std::filesystem::path& path, GeometryFileKind expected_kind, std::vector<Record>& records,
                      uint64_t& byte_size)
{
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }

  GeometryFileHeader header;
  if (!read_header(file, expected_kind, sizeof(Record), header)) {
    return false;
  }

  const uint64_t expected_size = header.record_count * sizeof(Record);
  if (header.payload_size != expected_size) {
    return false;
  }

  records.resize(static_cast<size_t>(header.record_count));
  file.read(reinterpret_cast<char*>(records.data()), static_cast<std::streamsize>(expected_size));
  if (!file && expected_size != 0) {
    return false;
  }

  byte_size = expected_size;
  return true;
}

bool read_payload_file(const std::filesystem::path& path, GeometryFileKind expected_kind, std::vector<std::byte>& payloads,
                       uint64_t& byte_size)
{
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }

  GeometryFileHeader header;
  if (!read_header(file, expected_kind, 1, header)) {
    return false;
  }

  if (header.record_count != header.payload_size) {
    return false;
  }

  payloads.resize(static_cast<size_t>(header.payload_size));
  file.read(reinterpret_cast<char*>(payloads.data()), static_cast<std::streamsize>(header.payload_size));
  if (!file && header.payload_size != 0) {
    return false;
  }

  byte_size = header.payload_size;
  return true;
}

bool validate_meta_records(const std::vector<GeometryMetaRecord>& meta_records, uint64_t shape_count, uint64_t owner_count,
                           uint64_t payload_size, uint64_t name_record_count, uint64_t name_payload_size)
{
  if (meta_records.size() != 1) {
    return false;
  }

  const GeometryMetaRecord& meta = meta_records[0];
  return meta.shape_count == shape_count && meta.owner_count == owner_count && meta.payload_size == payload_size
         && meta.name_record_count == name_record_count && meta.name_payload_size == name_payload_size
         && meta.next_shape_id != 0;
}

bool validate_sidmap_records(const std::vector<GeometrySidMapRecord>& sidmap_records, const std::vector<ShapeRecord>& records)
{
  if (sidmap_records.size() != records.size()) {
    return false;
  }

  for (size_t i = 0; i < records.size(); ++i) {
    if (sidmap_records[i].shape_id != records[i].id) {
      return false;
    }
  }

  return true;
}

std::filesystem::path manifest_file_path(const std::filesystem::path& manifest_path,
                                         const std::unordered_map<std::string, std::string>& values,
                                         const std::string& key, const std::string& fallback)
{
  const auto iter = values.find(key);
  const std::string& filename = iter == values.end() ? fallback : iter->second;
  return manifest_path.parent_path() / filename;
}

}  // namespace

SnapshotReadResult GeometrySnapshotReader::read(const SnapshotReadOptions& options, GeometryStore& store) const
{
  SnapshotReadResult result;
  result.manifest_path = options.manifest_path;

  const std::unordered_map<std::string, std::string> manifest_values = read_manifest_values(options.manifest_path);
  if (manifest_values.empty()) {
    return result;
  }

  std::vector<ShapeRecord> records;
  std::vector<OwnerRef> owners;
  std::vector<std::byte> payloads;
  std::vector<GeometryNameRecord> name_records;
  std::vector<std::byte> name_payloads;
  std::vector<GeometryMetaRecord> meta_records;
  std::vector<GeometrySidMapRecord> sidmap_records;
  uint64_t meta_byte_size = 0;
  uint64_t shapes_byte_size = 0;
  uint64_t owners_byte_size = 0;
  uint64_t sidmap_byte_size = 0;
  uint64_t name_index_byte_size = 0;
  uint64_t name_payload_size = 0;

  const bool read_meta =
      read_record_file(manifest_file_path(options.manifest_path, manifest_values, "meta", "geometry.meta.bin"),
                       GeometryFileKind::kMeta, meta_records, meta_byte_size);
  const bool read_shapes =
      read_record_file(manifest_file_path(options.manifest_path, manifest_values, "shapes", "geometry.shapes.bin"),
                       GeometryFileKind::kShapes, records, shapes_byte_size);
  const bool read_owners =
      read_record_file(manifest_file_path(options.manifest_path, manifest_values, "owners", "geometry.owners.bin"),
                       GeometryFileKind::kOwners, owners, owners_byte_size);
  const bool read_payload =
      read_payload_file(manifest_file_path(options.manifest_path, manifest_values, "payload", "geometry.payload.bin"),
                        GeometryFileKind::kPayload, payloads, result.payload_size);
  const bool read_sidmap =
      read_record_file(manifest_file_path(options.manifest_path, manifest_values, "sidmap", "geometry.sidmap.bin"),
                       GeometryFileKind::kSidMap, sidmap_records, sidmap_byte_size);

  if (!read_meta || !read_shapes || !read_owners || !read_payload || !read_sidmap) {
    return result;
  }

  const bool has_name_files = manifest_values.contains("names") || manifest_values.contains("name_index");
  if (has_name_files) {
    const bool read_names =
        read_payload_file(manifest_file_path(options.manifest_path, manifest_values, "names", "geometry.names.bin"),
                          GeometryFileKind::kNames, name_payloads, name_payload_size);
    const bool read_name_index =
        read_record_file(manifest_file_path(options.manifest_path, manifest_values, "name_index", "geometry.name_index.bin"),
                         GeometryFileKind::kNameIndex, name_records, name_index_byte_size);

    if (!read_names || !read_name_index) {
      return result;
    }
  }

  if (!validate_meta_records(meta_records, records.size(), owners.size(), payloads.size(), name_records.size(), name_payloads.size())
      || !validate_sidmap_records(sidmap_records, records)) {
    return result;
  }

  result.shape_count = static_cast<uint64_t>(records.size());
  result.owner_count = static_cast<uint64_t>(owners.size());
  result.ok = store.replace_snapshot(std::move(records), std::move(owners), std::move(payloads), std::move(name_records),
                                     std::move(name_payloads));
  return result;
}

}  // namespace ecc::geometry
