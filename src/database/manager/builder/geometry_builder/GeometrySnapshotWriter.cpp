#include "GeometrySnapshotWriter.h"

#include "GeometrySnapshotSchema.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <span>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace ecc::geometry {
namespace {

struct SnapshotManifestPaths
{
  std::string meta;
  std::string shapes;
  std::string owners;
  std::string payload;
  std::string names;
  std::string name_index;
  std::string sidmap;
  std::string delta;
  std::string view;
  std::string layers;
  std::string sites;
  std::string masters;
  std::string vias;
  std::string grids;
  std::string connectivity;
  std::string nets;
  std::string buses;
  std::string groups;
};

std::vector<char> make_geometry_file_bytes(GeometryFileKind file_kind, uint32_t record_size, uint64_t record_count,
                                           const void* data, uint64_t data_size)
{
  GeometryFileHeader header;
  header.file_kind = file_kind;
  header.record_size = record_size;
  header.record_count = record_count;
  header.payload_size = data_size;

  std::vector<char> bytes(sizeof(header) + static_cast<size_t>(data_size));
  std::memcpy(bytes.data(), &header, sizeof(header));
  if (data_size > 0) {
    std::memcpy(bytes.data() + sizeof(header), data, static_cast<size_t>(data_size));
  }
  return bytes;
}

bool write_file_bytes(const std::filesystem::path& path, const std::vector<char>& bytes)
{
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }

  if (bytes.empty()) {
    return static_cast<bool>(file);
  }
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(file);
}

bool file_bytes_equal(const std::filesystem::path& path, const std::vector<char>& bytes)
{
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }

  std::vector<char> existing((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return existing == bytes;
}

std::unordered_map<std::string, std::string> read_manifest_values(const std::filesystem::path& path)
{
  std::unordered_map<std::string, std::string> values;
  std::ifstream file(path);
  if (!file) {
    return values;
  }

  std::string line;
  while (std::getline(file, line)) {
    const size_t equals = line.find('=');
    if (equals == std::string::npos) {
      continue;
    }
    values.emplace(line.substr(0, equals), line.substr(equals + 1));
  }
  return values;
}

std::filesystem::path resolve_manifest_path(const std::filesystem::path& output_dir, const std::string& value)
{
  const std::filesystem::path path(value);
  return path.is_absolute() ? path : output_dir / path;
}

bool write_or_reuse_file(const std::filesystem::path& output_dir, const std::filesystem::path& epoch_dir,
                         const std::string& file_prefix,
                         const std::unordered_map<std::string, std::string>& previous_manifest, const std::string& key,
                         const std::string& filename, const std::vector<char>& bytes, std::string& manifest_value,
                         SnapshotWriteResult& result)
{
  const auto previous_iter = previous_manifest.find(key);
  if (previous_iter != previous_manifest.end() && !previous_iter->second.empty()) {
    const std::filesystem::path previous_path = resolve_manifest_path(output_dir, previous_iter->second);
    if (file_bytes_equal(previous_path, bytes)) {
      manifest_value = previous_iter->second;
      ++result.reused_side_file_count;
      return true;
    }
  }

  const std::filesystem::path path = epoch_dir / filename;
  if (!write_file_bytes(path, bytes)) {
    return false;
  }
  manifest_value = (std::filesystem::path(file_prefix) / filename).generic_string();
  ++result.written_side_file_count;
  return true;
}

std::string sanitize_manifest_value(std::string value)
{
  for (char& ch : value) {
    if (ch == '\r' || ch == '\n' || ch == '\t') {
      ch = ' ';
    }
  }
  return value;
}

bool write_manifest(const std::filesystem::path& path, const SnapshotWriteResult& result, const SnapshotManifestPaths& paths,
                    const SnapshotWriteOptions& options)
{
  std::ofstream file(path);
  if (!file) {
    return false;
  }

  file << "schema_version=" << kGeometrySchemaVersion << '\n';
  file << "active_epoch=" << result.epoch << '\n';
  if (!options.design_name.empty()) {
    file << "design_name=" << sanitize_manifest_value(options.design_name) << '\n';
  }
  if (!options.design_version.empty()) {
    file << "design_version=" << sanitize_manifest_value(options.design_version) << '\n';
  }
  if (options.dbu_per_micron > 0) {
    file << "dbu_per_micron=" << options.dbu_per_micron << '\n';
  }
  if (options.manufacture_grid >= 0) {
    file << "manufacture_grid=" << options.manufacture_grid << '\n';
  }
  file << "shape_count=" << result.shape_count << '\n';
  file << "owner_count=" << result.owner_count << '\n';
  file << "payload_size=" << result.payload_size << '\n';
  file << "dirty_lod_tile_count=" << result.dirty_lod_tile_count << '\n';
  file << "dirty_lod_rebuild_candidate_count=" << result.dirty_lod_rebuild_candidate_count << '\n';
  file << "written_side_file_count=" << result.written_side_file_count << '\n';
  file << "reused_side_file_count=" << result.reused_side_file_count << '\n';
  file << "meta=" << paths.meta << '\n';
  file << "shapes=" << paths.shapes << '\n';
  file << "owners=" << paths.owners << '\n';
  file << "payload=" << paths.payload << '\n';
  file << "names=" << paths.names << '\n';
  file << "name_index=" << paths.name_index << '\n';
  file << "sidmap=" << paths.sidmap << '\n';
  file << "delta=" << paths.delta << '\n';
  file << "view=" << paths.view << '\n';
  file << "layers=" << paths.layers << '\n';
  file << "sites=" << paths.sites << '\n';
  file << "masters=" << paths.masters << '\n';
  file << "vias=" << paths.vias << '\n';
  file << "grids=" << paths.grids << '\n';
  file << "connectivity=" << paths.connectivity << '\n';
  file << "nets=" << paths.nets << '\n';
  file << "buses=" << paths.buses << '\n';
  file << "groups=" << paths.groups << '\n';
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
                      const SnapshotManifestPaths& paths, const SnapshotWriteOptions& options)
{
  const std::filesystem::path manifest_path = output_dir / "geometry.manifest";
  const std::filesystem::path temporary_path = output_dir / "geometry.manifest.tmp";
  std::error_code error;
  std::filesystem::remove(temporary_path, error);
  if (!write_manifest(temporary_path, result, paths, options)) {
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

std::string sanitize_layer_text(const std::string& value, const std::string& fallback)
{
  std::string sanitized = value.empty() ? fallback : value;
  for (char& ch : sanitized) {
    if (ch == '\t' || ch == '\r' || ch == '\n') {
      ch = ' ';
    }
  }
  return sanitized;
}

std::string join_metadata_names(const std::vector<std::string>& names)
{
  std::string joined;
  for (const std::string& name : names) {
    const std::string sanitized = sanitize_layer_text(name, "");
    if (sanitized.empty()) {
      continue;
    }
    if (!joined.empty()) {
      joined += ",";
    }
    joined += sanitized;
  }
  return joined;
}

std::vector<GeometryLayerMetadata> make_layer_metadata(std::span<const GeometryLayerMetadata> configured_layers)
{
  std::map<LayerId, GeometryLayerMetadata> by_layer;
  for (const GeometryLayerMetadata& metadata : configured_layers) {
    GeometryLayerMetadata normalized = metadata;
    normalized.name = sanitize_layer_text(normalized.name, "L" + std::to_string(normalized.layer_id));
    normalized.type = sanitize_layer_text(normalized.type, "unknown");
    normalized.direction = sanitize_layer_text(normalized.direction, "unknown");
    normalized.enclosure_below = sanitize_layer_text(normalized.enclosure_below, "");
    normalized.enclosure_above = sanitize_layer_text(normalized.enclosure_above, "");
    by_layer[metadata.layer_id] = normalized;
  }

  std::vector<GeometryLayerMetadata> layers;
  layers.reserve(by_layer.size());
  for (const auto& [layer_id, metadata] : by_layer) {
    layers.push_back(metadata);
  }
  std::sort(layers.begin(), layers.end(), [](const GeometryLayerMetadata& lhs, const GeometryLayerMetadata& rhs) {
    if (lhs.order != rhs.order) {
      return lhs.order < rhs.order;
    }
    return lhs.layer_id < rhs.layer_id;
  });
  return layers;
}

bool write_layer_metadata_file(const std::filesystem::path& path, std::span<const GeometryLayerMetadata> layers)
{
  std::ofstream file(path);
  if (!file) {
    return false;
  }

  file << "layer_id\torder\ttype\tdirection\twidth\tpitch_x\tpitch_y\tname\tmin_spacing\tmin_area\tmin_step\tcut_spacing\t"
          "enclosure_below\tenclosure_above\tlef58_rule_count\n";
  for (const GeometryLayerMetadata& layer : layers) {
    file << layer.layer_id << '\t' << layer.order << '\t' << sanitize_layer_text(layer.type, "unknown") << '\t'
         << sanitize_layer_text(layer.direction, "unknown") << '\t' << layer.width << '\t' << layer.pitch_x << '\t'
         << layer.pitch_y << '\t' << sanitize_layer_text(layer.name, "L" + std::to_string(layer.layer_id)) << '\t'
         << layer.min_spacing << '\t' << layer.min_area << '\t' << layer.min_step << '\t' << layer.cut_spacing << '\t'
         << sanitize_layer_text(layer.enclosure_below, "") << '\t' << sanitize_layer_text(layer.enclosure_above, "") << '\t'
         << layer.lef58_rule_count << '\n';
  }

  return static_cast<bool>(file);
}

bool write_site_metadata_file(const std::filesystem::path& path, std::span<const GeometrySiteMetadata> sites)
{
  std::ofstream file(path);
  if (!file) {
    return false;
  }

  file << "name\tclass\tsymmetry\torient\twidth\theight\tis_overlap\n";
  for (const GeometrySiteMetadata& site : sites) {
    file << sanitize_layer_text(site.name, "") << '\t' << sanitize_layer_text(site.site_class, "unknown") << '\t'
         << sanitize_layer_text(site.symmetry, "") << '\t' << sanitize_layer_text(site.orient, "") << '\t' << site.width
         << '\t' << site.height << '\t' << (site.is_overlap ? 1 : 0) << '\n';
  }

  return static_cast<bool>(file);
}

bool write_master_metadata_file(const std::filesystem::path& path, std::span<const GeometryMasterMetadata> masters)
{
  std::ofstream file(path);
  if (!file) {
    return false;
  }

  file << "name\ttype\tsite\tsymmetry\torigin_x\torigin_y\twidth\theight\tterm_count\tobs_count\n";
  for (const GeometryMasterMetadata& master : masters) {
    file << sanitize_layer_text(master.name, "") << '\t' << sanitize_layer_text(master.master_type, "unknown") << '\t'
         << sanitize_layer_text(master.site, "") << '\t' << sanitize_layer_text(master.symmetry, "") << '\t'
         << master.origin_x << '\t' << master.origin_y << '\t' << master.width << '\t' << master.height << '\t'
         << master.term_count << '\t' << master.obs_count << '\n';
  }

  return static_cast<bool>(file);
}

bool write_via_metadata_file(const std::filesystem::path& path, std::span<const GeometryViaMetadata> vias)
{
  std::ofstream file(path);
  if (!file) {
    return false;
  }

  file << "name\tmaster\ttype\trule\tbottom\tcut\ttop\tcut_width\tcut_height\tcut_spacing_x\tcut_spacing_y"
          "\tenclosure_bottom_x\tenclosure_bottom_y\tenclosure_top_x\tenclosure_top_y\trows\tcols\tdefault\n";
  for (const GeometryViaMetadata& via : vias) {
    file << sanitize_layer_text(via.name, "") << '\t' << sanitize_layer_text(via.master_name, "") << '\t'
         << sanitize_layer_text(via.via_type, "unknown") << '\t' << sanitize_layer_text(via.rule_name, "") << '\t'
         << sanitize_layer_text(via.bottom_layer, "") << '\t' << sanitize_layer_text(via.cut_layer, "") << '\t'
         << sanitize_layer_text(via.top_layer, "") << '\t' << via.cut_width << '\t' << via.cut_height << '\t'
         << via.cut_spacing_x << '\t' << via.cut_spacing_y << '\t' << via.enclosure_bottom_x << '\t'
         << via.enclosure_bottom_y << '\t' << via.enclosure_top_x << '\t' << via.enclosure_top_y << '\t'
         << via.rows << '\t' << via.cols << '\t' << (via.is_default ? 1 : 0) << '\n';
  }

  return static_cast<bool>(file);
}

bool write_grid_metadata_file(const std::filesystem::path& path, std::span<const GeometryGridMetadata> grids)
{
  std::ofstream file(path);
  if (!file) {
    return false;
  }

  file << "type\tindex\tdirection\tstart\tstep\tcount\twidth\tlayers\n";
  for (const GeometryGridMetadata& grid : grids) {
    file << sanitize_layer_text(grid.grid_type, "unknown") << '\t' << grid.index << '\t'
         << sanitize_layer_text(grid.direction, "unknown") << '\t' << grid.start << '\t' << grid.step << '\t'
         << grid.count << '\t' << grid.width << '\t' << join_metadata_names(grid.layer_names) << '\n';
  }

  return static_cast<bool>(file);
}

bool write_connectivity_metadata_file(const std::filesystem::path& path,
                                      std::span<const GeometryConnectivityMetadata> connectivity)
{
  std::ofstream file(path);
  if (!file) {
    return false;
  }

  file << "net\tkind\tendpoint_type\tinstance\tpin\tmaster\n";
  for (const GeometryConnectivityMetadata& endpoint : connectivity) {
    file << sanitize_layer_text(endpoint.net_name, "") << '\t' << sanitize_layer_text(endpoint.net_kind, "other") << '\t'
         << sanitize_layer_text(endpoint.endpoint_type, "unknown") << '\t'
         << sanitize_layer_text(endpoint.instance_name, "") << '\t' << sanitize_layer_text(endpoint.pin_name, "") << '\t'
         << sanitize_layer_text(endpoint.master_name, "") << '\n';
  }

  return static_cast<bool>(file);
}

bool write_net_metadata_file(const std::filesystem::path& path, std::span<const GeometryNetMetadata> nets)
{
  std::ofstream file(path);
  if (!file) {
    return false;
  }

  file << "name\tkind\n";
  for (const GeometryNetMetadata& net : nets) {
    file << sanitize_layer_text(net.name, "") << '\t' << sanitize_layer_text(net.kind, "other") << '\n';
  }

  return static_cast<bool>(file);
}

bool write_bus_metadata_file(const std::filesystem::path& path, std::span<const GeometryBusMetadata> buses)
{
  std::ofstream file(path);
  if (!file) {
    return false;
  }

  file << "name\ttype\tleft\tright\tnet_count\tpin_count\tnets\tpins\n";
  for (const GeometryBusMetadata& bus : buses) {
    file << sanitize_layer_text(bus.name, "") << '\t' << sanitize_layer_text(bus.bus_type, "unknown") << '\t' << bus.left
         << '\t' << bus.right << '\t' << bus.net_count << '\t' << bus.pin_count << '\t'
         << join_metadata_names(bus.net_names) << '\t' << join_metadata_names(bus.pin_names) << '\n';
  }

  return static_cast<bool>(file);
}

bool write_group_metadata_file(const std::filesystem::path& path, std::span<const GeometryGroupMetadata> groups)
{
  std::ofstream file(path);
  if (!file) {
    return false;
  }

  file << "name\tregion\tinstance_count\tinstances\n";
  for (const GeometryGroupMetadata& group : groups) {
    file << sanitize_layer_text(group.name, "") << '\t' << sanitize_layer_text(group.region_name, "") << '\t'
         << group.instance_count << '\t' << join_metadata_names(group.instance_names) << '\n';
  }

  return static_cast<bool>(file);
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
  const std::filesystem::path& output_dir = options.output_dir;

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

  result.dirty_lod_tile_count = static_cast<uint64_t>(store.dirty_lod_tile_count());
  store.rebuild_dirty_lod_tiles();
  result.dirty_lod_rebuild_candidate_count = static_cast<uint64_t>(store.last_dirty_lod_rebuild_candidate_count());

  const std::span<const ShapeRecord> records = store.records();
  const std::span<const OwnerRef> owners = store.owners();
  const std::span<const std::byte> payloads = store.payloads();
  const std::span<const GeometryNameRecord> name_records = store.name_records();
  const std::span<const std::byte> name_payloads = store.name_payloads();
  const std::span<const GeometryDeltaEvent> delta_events = store.delta_events();
  const std::vector<GeometrySidMapRecord> sidmap_records = make_sidmap_records(records, owners);
  const std::vector<GeometryTileSummary> lod_summaries = store.lod_summaries();
  const std::vector<GeometryViewTileRecord> view_records = make_view_tile_records(lod_summaries);
  // The physical-layer catalog is defined exclusively by IDB-derived options.layers.
  // Geometry records may use the internal layout layer or legacy unknown layers,
  // but must not manufacture technology entries for the viewer controls.
  const std::vector<GeometryLayerMetadata> layers = make_layer_metadata(options.layers);
  result.layer_count = static_cast<uint64_t>(layers.size());
  result.site_count = static_cast<uint64_t>(options.sites.size());
  result.master_count = static_cast<uint64_t>(options.masters.size());
  result.via_count = static_cast<uint64_t>(options.vias.size());
  result.grid_count = static_cast<uint64_t>(options.grids.size());
  result.connectivity_count = static_cast<uint64_t>(options.connectivity.size());
  result.net_count = static_cast<uint64_t>(options.nets.size());
  result.bus_count = static_cast<uint64_t>(options.buses.size());
  result.group_count = static_cast<uint64_t>(options.groups.size());

  GeometryMetaRecord meta;
  meta.shape_count = result.shape_count;
  meta.owner_count = result.owner_count;
  meta.payload_size = result.payload_size;
  meta.name_record_count = static_cast<uint64_t>(name_records.size());
  meta.name_payload_size = static_cast<uint64_t>(name_payloads.size());
  meta.next_shape_id = next_shape_id_from_records(records);

  const std::string file_prefix = (std::filesystem::path("epochs") / std::to_string(result.epoch)).generic_string();
  const std::unordered_map<std::string, std::string> previous_manifest =
      read_manifest_values(options.output_dir / "geometry.manifest");
  SnapshotManifestPaths paths;

  const bool wrote_meta = write_or_reuse_file(output_dir, epoch_dir, file_prefix, previous_manifest, "meta",
                                              "geometry.meta.bin",
                                              make_geometry_file_bytes(GeometryFileKind::kMeta, sizeof(GeometryMetaRecord), 1, &meta,
                                                                       sizeof(meta)),
                                              paths.meta, result);
  const bool wrote_shapes =
      write_or_reuse_file(output_dir, epoch_dir, file_prefix, previous_manifest, "shapes", "geometry.shapes.bin",
                          make_geometry_file_bytes(GeometryFileKind::kShapes, sizeof(ShapeRecord), result.shape_count,
                                                   records.data(), records.size_bytes()),
                          paths.shapes, result);
  const bool wrote_owners =
      write_or_reuse_file(output_dir, epoch_dir, file_prefix, previous_manifest, "owners", "geometry.owners.bin",
                          make_geometry_file_bytes(GeometryFileKind::kOwners, sizeof(OwnerRef), result.owner_count,
                                                   owners.data(), owners.size_bytes()),
                          paths.owners, result);
  const bool wrote_payload = write_or_reuse_file(output_dir, epoch_dir, file_prefix, previous_manifest, "payload",
                                                 "geometry.payload.bin",
                                                 make_geometry_file_bytes(GeometryFileKind::kPayload, 1, result.payload_size,
                                                                          payloads.data(), payloads.size_bytes()),
                                                 paths.payload, result);
  const bool wrote_names =
      write_or_reuse_file(output_dir, epoch_dir, file_prefix, previous_manifest, "names", "geometry.names.bin",
                          make_geometry_file_bytes(GeometryFileKind::kNames, 1, name_payloads.size(), name_payloads.data(),
                                                   name_payloads.size_bytes()),
                          paths.names, result);
  const bool wrote_name_index =
      write_or_reuse_file(output_dir, epoch_dir, file_prefix, previous_manifest, "name_index", "geometry.name_index.bin",
                          make_geometry_file_bytes(GeometryFileKind::kNameIndex, sizeof(GeometryNameRecord),
                                                   name_records.size(), name_records.data(), name_records.size_bytes()),
                          paths.name_index, result);
  const bool wrote_sidmap =
      write_or_reuse_file(output_dir, epoch_dir, file_prefix, previous_manifest, "sidmap", "geometry.sidmap.bin",
                          make_geometry_file_bytes(GeometryFileKind::kSidMap, sizeof(GeometrySidMapRecord),
                                                   sidmap_records.size(), sidmap_records.data(),
                                                   static_cast<uint64_t>(sidmap_records.size() * sizeof(GeometrySidMapRecord))),
                          paths.sidmap, result);
  const bool wrote_delta =
      write_or_reuse_file(output_dir, epoch_dir, file_prefix, previous_manifest, "delta", "geometry.delta.bin",
                          make_geometry_file_bytes(GeometryFileKind::kDelta, sizeof(GeometryDeltaEvent), delta_events.size(),
                                                   delta_events.data(), delta_events.size_bytes()),
                          paths.delta, result);
  const bool wrote_view =
      write_or_reuse_file(output_dir, epoch_dir, file_prefix, previous_manifest, "view", "geometry.view.bin",
                          make_geometry_file_bytes(GeometryFileKind::kView, sizeof(GeometryViewTileRecord),
                                                   view_records.size(), view_records.data(),
                                                   static_cast<uint64_t>(view_records.size() * sizeof(GeometryViewTileRecord))),
                          paths.view, result);

  paths.layers = (std::filesystem::path(file_prefix) / "geometry.layers.txt").generic_string();
  paths.sites = (std::filesystem::path(file_prefix) / "geometry.sites.txt").generic_string();
  paths.masters = (std::filesystem::path(file_prefix) / "geometry.masters.txt").generic_string();
  paths.vias = (std::filesystem::path(file_prefix) / "geometry.vias.txt").generic_string();
  paths.grids = (std::filesystem::path(file_prefix) / "geometry.grids.txt").generic_string();
  paths.connectivity = (std::filesystem::path(file_prefix) / "geometry.connectivity.txt").generic_string();
  paths.nets = (std::filesystem::path(file_prefix) / "geometry.nets.txt").generic_string();
  paths.buses = (std::filesystem::path(file_prefix) / "geometry.buses.txt").generic_string();
  paths.groups = (std::filesystem::path(file_prefix) / "geometry.groups.txt").generic_string();

  const bool wrote_layers = write_layer_metadata_file(output_dir / paths.layers, layers);
  const bool wrote_sites = write_site_metadata_file(output_dir / paths.sites, options.sites);
  const bool wrote_masters = write_master_metadata_file(output_dir / paths.masters, options.masters);
  const bool wrote_vias = write_via_metadata_file(output_dir / paths.vias, options.vias);
  const bool wrote_grids = write_grid_metadata_file(output_dir / paths.grids, options.grids);
  const bool wrote_connectivity =
      write_connectivity_metadata_file(output_dir / paths.connectivity, options.connectivity);
  const bool wrote_nets = write_net_metadata_file(output_dir / paths.nets, options.nets);
  const bool wrote_buses = write_bus_metadata_file(output_dir / paths.buses, options.buses);
  const bool wrote_groups = write_group_metadata_file(output_dir / paths.groups, options.groups);
  result.written_side_file_count +=
      static_cast<uint64_t>(wrote_layers) + static_cast<uint64_t>(wrote_sites) + static_cast<uint64_t>(wrote_masters)
      + static_cast<uint64_t>(wrote_vias) + static_cast<uint64_t>(wrote_grids) + static_cast<uint64_t>(wrote_connectivity)
      + static_cast<uint64_t>(wrote_nets) + static_cast<uint64_t>(wrote_buses) + static_cast<uint64_t>(wrote_groups);
  const bool wrote_files = wrote_meta && wrote_shapes && wrote_owners && wrote_payload && wrote_names && wrote_name_index
                           && wrote_sidmap && wrote_delta && wrote_view && wrote_layers && wrote_sites && wrote_masters
                           && wrote_vias && wrote_grids && wrote_connectivity && wrote_nets && wrote_buses && wrote_groups;
  const bool wrote_manifest = wrote_files && publish_manifest(options.output_dir, result, paths, options);

  result.ok = wrote_files && wrote_manifest;
  return result;
}

}  // namespace ecc::geometry
