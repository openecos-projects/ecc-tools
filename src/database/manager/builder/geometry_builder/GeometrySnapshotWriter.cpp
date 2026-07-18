#include "GeometrySnapshotWriter.h"

#include "GeometrySnapshotSchema.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
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

std::string sanitize_manifest_value(std::string value)
{
  for (char& ch : value) {
    if (ch == '\r' || ch == '\n' || ch == '\t') {
      ch = ' ';
    }
  }
  return value;
}

bool write_manifest(const std::filesystem::path& path, const SnapshotWriteResult& result, const std::string& file_prefix,
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
  file << "meta=" << file_prefix << "/geometry.meta.bin\n";
  file << "shapes=" << file_prefix << "/geometry.shapes.bin\n";
  file << "owners=" << file_prefix << "/geometry.owners.bin\n";
  file << "payload=" << file_prefix << "/geometry.payload.bin\n";
  file << "names=" << file_prefix << "/geometry.names.bin\n";
  file << "name_index=" << file_prefix << "/geometry.name_index.bin\n";
  file << "sidmap=" << file_prefix << "/geometry.sidmap.bin\n";
  file << "delta=" << file_prefix << "/geometry.delta.bin\n";
  file << "view=" << file_prefix << "/geometry.view.bin\n";
  file << "layers=" << file_prefix << "/geometry.layers.txt\n";
  file << "sites=" << file_prefix << "/geometry.sites.txt\n";
  file << "masters=" << file_prefix << "/geometry.masters.txt\n";
  file << "vias=" << file_prefix << "/geometry.vias.txt\n";
  file << "grids=" << file_prefix << "/geometry.grids.txt\n";
  file << "connectivity=" << file_prefix << "/geometry.connectivity.txt\n";
  file << "nets=" << file_prefix << "/geometry.nets.txt\n";
  file << "buses=" << file_prefix << "/geometry.buses.txt\n";
  file << "groups=" << file_prefix << "/geometry.groups.txt\n";
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
                      const std::string& file_prefix, const SnapshotWriteOptions& options)
{
  const std::filesystem::path manifest_path = output_dir / "geometry.manifest";
  const std::filesystem::path temporary_path = output_dir / "geometry.manifest.tmp";
  std::error_code error;
  std::filesystem::remove(temporary_path, error);
  if (!write_manifest(temporary_path, result, file_prefix, options)) {
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

GeometryLayerMetadata fallback_layer_metadata(LayerId layer_id)
{
  GeometryLayerMetadata metadata;
  metadata.layer_id = layer_id;
  metadata.order = layer_id;
  metadata.name = "L" + std::to_string(layer_id);
  return metadata;
}

std::vector<GeometryLayerMetadata> make_layer_metadata(std::span<const ShapeRecord> records,
                                                       std::span<const GeometryLayerMetadata> configured_layers)
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

  for (const ShapeRecord& record : records) {
    if (record.state != ShapeState::kAlive) {
      continue;
    }
    by_layer.try_emplace(record.layer_id, fallback_layer_metadata(record.layer_id));
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
  const std::vector<GeometryLayerMetadata> layers = make_layer_metadata(records, options.layers);
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
  const bool wrote_layers = write_layer_metadata_file(epoch_dir / "geometry.layers.txt", layers);
  const bool wrote_sites = write_site_metadata_file(epoch_dir / "geometry.sites.txt", options.sites);
  const bool wrote_masters = write_master_metadata_file(epoch_dir / "geometry.masters.txt", options.masters);
  const bool wrote_vias = write_via_metadata_file(epoch_dir / "geometry.vias.txt", options.vias);
  const bool wrote_grids = write_grid_metadata_file(epoch_dir / "geometry.grids.txt", options.grids);
  const bool wrote_connectivity =
      write_connectivity_metadata_file(epoch_dir / "geometry.connectivity.txt", options.connectivity);
  const bool wrote_nets = write_net_metadata_file(epoch_dir / "geometry.nets.txt", options.nets);
  const bool wrote_buses = write_bus_metadata_file(epoch_dir / "geometry.buses.txt", options.buses);
  const bool wrote_groups = write_group_metadata_file(epoch_dir / "geometry.groups.txt", options.groups);
  const bool wrote_files = wrote_meta && wrote_shapes && wrote_owners && wrote_payload && wrote_names && wrote_name_index
                           && wrote_sidmap && wrote_delta && wrote_view && wrote_layers && wrote_sites && wrote_masters
                           && wrote_vias && wrote_grids && wrote_connectivity && wrote_nets && wrote_buses && wrote_groups;
  const std::string file_prefix = (std::filesystem::path("epochs") / std::to_string(result.epoch)).generic_string();
  const bool wrote_manifest = wrote_files && publish_manifest(options.output_dir, result, file_prefix, options);

  result.ok = wrote_files && wrote_manifest;
  return result;
}

}  // namespace ecc::geometry
