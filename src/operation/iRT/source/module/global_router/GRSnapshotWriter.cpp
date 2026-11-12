// ***************************************************************************************
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
#include "GRSnapshotWriter.hpp"

#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "DataManager.hpp"
#include "Net.hpp"
#include "Pin.hpp"
#include "json.hpp"

namespace irt {

namespace {

constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;
constexpr uint32_t kHeaderSize = 64;
constexpr uint32_t kSectionGCellAxis = 1;
constexpr uint32_t kSectionLayerEdges = 2;
constexpr uint32_t kSectionNetIndex = 3;
constexpr uint32_t kSectionNetNames = 4;
constexpr uint32_t kSectionNetRouteChunk = 5;
constexpr uint32_t kCodecRaw = 0;
constexpr uint32_t kCodecZlib = 1;
constexpr size_t kCompressionThreshold = 4 * 1024;
constexpr size_t kRouteChunkTargetSize = 1024 * 1024;
constexpr size_t kRouteChunkNetLimit = 1024;
constexpr size_t kNetIndexRecordSize = 56;

struct MetricRange
{
  double min = std::numeric_limits<double>::infinity();
  double max = -std::numeric_limits<double>::infinity();

  void add(double value)
  {
    min = std::min(min, value);
    max = std::max(max, value);
  }

  void finish()
  {
    if (!std::isfinite(min) || !std::isfinite(max)) {
      min = 0.0;
      max = 0.0;
    }
  }
};

struct LayerMetricRanges
{
  MetricRange supply;
  MetricRange fixed_demand;
  MetricRange routing_demand;
  MetricRange usable_supply;
  MetricRange overflow;
  MetricRange reserved_supply;
};

struct ExportLayer
{
  uint8_t ordinal = 0;
  int32_t layer_index = -1;
  std::string name;
  bool is_horizontal = false;
  int32_t edge_x_size = 0;
  int32_t edge_y_size = 0;
  uint64_t edge_count = 0;
  uint64_t edge_base = 0;
  std::string edges_section;
  LayerMetricRanges metric_ranges;
};

struct ExportNetRecord
{
  uint32_t source_net_idx = 0;
  uint32_t name_offset = 0;
  uint32_t name_length = 0;
  int32_t bbox_llx = 0;
  int32_t bbox_lly = 0;
  int32_t bbox_urx = 0;
  int32_t bbox_ury = 0;
  uint32_t route_chunk_ordinal = 0;
  uint32_t route_offset = 0;
  uint32_t planar_edge_count = 0;
  uint32_t via_count = 0;
  uint32_t pin_count = 0;
  uint32_t flags = 0;
};

struct RouteChunkInfo
{
  uint32_t ordinal = 0;
  std::string path;
  uint32_t net_ordinal_begin = 0;
  uint32_t net_ordinal_end = 0;
};

struct RouteChunkPayload
{
  uint32_t net_ordinal_begin = 0;
  uint32_t record_count = 0;
  std::vector<uint8_t> payload;
};

struct SnapshotStats
{
  uint64_t planar_route_edge_reference_count = 0;
  uint64_t via_stack_count = 0;
  uint64_t pin_count = 0;
};

uint32_t checkedUint32(size_t value, const char* field)
{
  if (value > std::numeric_limits<uint32_t>::max()) {
    throw std::runtime_error(std::string(field) + " exceeds uint32");
  }
  return static_cast<uint32_t>(value);
}

uint32_t checkedUint32(double value, const char* field)
{
  if (!std::isfinite(value) || value < 0.0 || value > std::numeric_limits<uint32_t>::max() || std::floor(value) != value) {
    throw std::runtime_error(std::string(field) + " is not a non-negative uint32 integer");
  }
  return static_cast<uint32_t>(value);
}

float checkedFloat(double value, const char* field)
{
  if (!std::isfinite(value) || value < 0.0 || value > std::numeric_limits<float>::max()) {
    throw std::runtime_error(std::string(field) + " is not a finite non-negative float32 value");
  }
  return static_cast<float>(value);
}

void checkFiniteNonNegative(double value, const char* field)
{
  if (!std::isfinite(value) || value < 0.0) {
    throw std::runtime_error(std::string(field) + " is not a finite non-negative value");
  }
}

void appendU8(std::vector<uint8_t>& data, uint8_t value)
{
  data.push_back(value);
}

void appendU16(std::vector<uint8_t>& data, uint16_t value)
{
  data.push_back(static_cast<uint8_t>(value & 0xff));
  data.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void appendU32(std::vector<uint8_t>& data, uint32_t value)
{
  data.push_back(static_cast<uint8_t>(value & 0xff));
  data.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
  data.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
  data.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

void appendI32(std::vector<uint8_t>& data, int32_t value)
{
  appendU32(data, static_cast<uint32_t>(value));
}

void appendU64(std::vector<uint8_t>& data, uint64_t value)
{
  for (int32_t shift = 0; shift < 64; shift += 8) {
    data.push_back(static_cast<uint8_t>((value >> shift) & 0xff));
  }
}

void appendUvarint(std::vector<uint8_t>& data, uint32_t value)
{
  while (value >= 0x80) {
    data.push_back(static_cast<uint8_t>((value & 0x7f) | 0x80));
    value >>= 7;
  }
  data.push_back(static_cast<uint8_t>(value));
}

void setU32At(std::vector<uint8_t>& data, size_t offset, uint32_t value)
{
  if (offset + 4 > data.size()) {
    throw std::runtime_error("uint32 write exceeds buffer");
  }
  data[offset] = static_cast<uint8_t>(value & 0xff);
  data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
  data[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xff);
  data[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

void setFloatAt(std::vector<uint8_t>& data, size_t offset, float value)
{
  setU32At(data, offset, std::bit_cast<uint32_t>(value));
}

std::array<uint32_t, 256> makeCrc32cTable()
{
  std::array<uint32_t, 256> table = {};
  for (uint32_t index = 0; index < table.size(); index++) {
    uint32_t crc = index;
    for (int32_t bit = 0; bit < 8; bit++) {
      crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0x82f63b78U : crc >> 1U;
    }
    table[index] = crc;
  }
  return table;
}

uint32_t crc32c(const uint8_t* data, size_t size)
{
  static const std::array<uint32_t, 256> table = makeCrc32cTable();
  uint32_t crc = 0xffffffffU;
  for (size_t i = 0; i < size; i++) {
    crc = table[(crc ^ data[i]) & 0xffU] ^ (crc >> 8U);
  }
  return crc ^ 0xffffffffU;
}

std::vector<uint8_t> compressZlib(const std::vector<uint8_t>& payload)
{
  if (payload.empty()) {
    return {};
  }
  if (payload.size() > std::numeric_limits<uLong>::max()) {
    throw std::runtime_error("GR snapshot section is too large for zlib");
  }
  uLongf compressed_size = compressBound(static_cast<uLong>(payload.size()));
  std::vector<uint8_t> compressed(compressed_size);
  int32_t result = compress2(compressed.data(), &compressed_size, payload.data(), static_cast<uLong>(payload.size()), Z_DEFAULT_COMPRESSION);
  if (result != Z_OK) {
    throw std::runtime_error(std::string("Failed to compress GR snapshot section: ") + zError(result));
  }
  compressed.resize(compressed_size);
  return compressed;
}

std::vector<uint8_t> buildSectionHeader(uint32_t section_type, uint32_t codec, uint64_t element_count, uint64_t uncompressed_size, uint64_t stored_size,
                                        uint32_t payload_crc32c)
{
  std::vector<uint8_t> header;
  header.reserve(kHeaderSize);
  header.push_back('G');
  header.push_back('R');
  header.push_back('V');
  header.push_back('S');
  appendU16(header, kVersionMajor);
  appendU16(header, kVersionMinor);
  appendU32(header, kHeaderSize);
  appendU32(header, section_type);
  appendU32(header, codec);
  appendU32(header, 0);
  appendU64(header, element_count);
  appendU64(header, uncompressed_size);
  appendU64(header, stored_size);
  appendU32(header, payload_crc32c);
  appendU32(header, 0);
  appendU32(header, 0);
  if (header.size() != 60) {
    throw std::runtime_error("Invalid GRVS header prefix size");
  }
  appendU32(header, crc32c(header.data(), header.size()));
  if (header.size() != kHeaderSize) {
    throw std::runtime_error("Invalid GRVS header size");
  }
  return header;
}

void writeSectionFile(const std::filesystem::path& staging_path, const std::string& relative_path, uint32_t section_type, uint64_t element_count,
                      const std::vector<uint8_t>& payload)
{
  uint32_t codec = kCodecRaw;
  std::vector<uint8_t> stored_payload = payload;
  if (payload.size() > kCompressionThreshold) {
    codec = kCodecZlib;
    stored_payload = compressZlib(payload);
  }
  std::vector<uint8_t> header
      = buildSectionHeader(section_type, codec, element_count, payload.size(), stored_payload.size(), crc32c(payload.data(), payload.size()));
  std::filesystem::path file_path = staging_path / relative_path;
  std::filesystem::create_directories(file_path.parent_path());
  std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    throw std::runtime_error("Failed to open GR snapshot section: " + file_path.string());
  }
  output.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
  if (!stored_payload.empty()) {
    output.write(reinterpret_cast<const char*>(stored_payload.data()), static_cast<std::streamsize>(stored_payload.size()));
  }
  output.close();
  if (!output) {
    throw std::runtime_error("Failed to write GR snapshot section: " + file_path.string());
  }
}

void validateStage(const std::string& stage)
{
  if (stage.empty()) {
    throw std::runtime_error("GR snapshot stage is empty");
  }
  for (char character : stage) {
    bool is_valid = ('a' <= character && character <= 'z') || ('A' <= character && character <= 'Z') || ('0' <= character && character <= '9')
                    || character == '_' || character == '-' || character == '.';
    if (!is_valid) {
      throw std::runtime_error("GR snapshot stage contains an invalid path character");
    }
  }
}

std::string makeUtcTimestamp()
{
  std::time_t timestamp = std::time(nullptr);
  std::tm utc_time = {};
  gmtime_r(&timestamp, &utc_time);
  char buffer[32] = {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc_time);
  return std::string(buffer);
}

std::vector<ExportLayer> buildExportLayerList(const GRGridGraph& grid_graph)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  int32_t bottom_layer_idx = grid_graph.getBottomLayerIdx();
  int32_t top_layer_idx = grid_graph.getTopLayerIdx();
  if (bottom_layer_idx < 0 || top_layer_idx < bottom_layer_idx || top_layer_idx >= static_cast<int32_t>(routing_layer_list.size())) {
    throw std::runtime_error("Invalid GR routing layer range");
  }
  size_t layer_count = static_cast<size_t>(top_layer_idx - bottom_layer_idx + 1);
  if (layer_count > 256) {
    throw std::runtime_error("GR snapshot supports at most 256 routing layers");
  }

  std::vector<ExportLayer> layer_list;
  layer_list.reserve(layer_count);
  uint64_t edge_base = 0;
  for (int32_t layer_idx = bottom_layer_idx; layer_idx <= top_layer_idx; layer_idx++) {
    ExportLayer layer;
    layer.ordinal = static_cast<uint8_t>(layer_list.size());
    layer.layer_index = layer_idx;
    layer.name = routing_layer_list[layer_idx].get_layer_name();
    layer.is_horizontal = grid_graph.isHorizontalLayer(layer_idx);
    layer.edge_x_size = layer.is_horizontal ? std::max(0, grid_graph.getXSize() - 1) : grid_graph.getXSize();
    layer.edge_y_size = layer.is_horizontal ? grid_graph.getYSize() : std::max(0, grid_graph.getYSize() - 1);
    layer.edge_count = static_cast<uint64_t>(layer.edge_x_size) * static_cast<uint64_t>(layer.edge_y_size);
    layer.edge_base = edge_base;
    layer.edges_section = "sections/layer_" + std::to_string(layer_idx) + "_edges.grs";
    if (layer.edge_base + layer.edge_count > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1ULL) {
      throw std::runtime_error("GR global edge id exceeds uint32");
    }
    edge_base += layer.edge_count;
    layer_list.push_back(std::move(layer));
  }
  return layer_list;
}

void writeAxisPayload(const std::vector<ScaleGrid>& grid_list, int32_t expected_size, const char* axis_name)
{
  int64_t cell_count = 0;
  for (const ScaleGrid& grid : grid_list) {
    if (grid.get_start_line() < 0 || grid.get_step_length() <= 0 || grid.get_step_num() <= 0
        || grid.get_end_line() != grid.get_start_line() + grid.get_step_num() * grid.get_step_length()) {
      throw std::runtime_error(std::string("Invalid GCell ") + axis_name + " axis run");
    }
    cell_count += grid.get_step_num();
  }
  if (cell_count != expected_size) {
    throw std::runtime_error(std::string("GCell ") + axis_name + " axis size does not match GR grid size");
  }
}

void exportAxis(const std::filesystem::path& staging_path, const std::string& relative_path, const std::vector<ScaleGrid>& grid_list, int32_t expected_size,
                const char* axis_name)
{
  writeAxisPayload(grid_list, expected_size, axis_name);
  std::vector<uint8_t> payload;
  payload.reserve(grid_list.size() * 16);
  for (const ScaleGrid& grid : grid_list) {
    appendI32(payload, grid.get_start_line());
    appendI32(payload, grid.get_step_length());
    appendI32(payload, grid.get_step_num());
    appendI32(payload, grid.get_end_line());
  }
  writeSectionFile(staging_path, relative_path, kSectionGCellAxis, grid_list.size(), payload);
}

LayerMetricRanges exportLayerEdges(const std::filesystem::path& staging_path, const GRGridGraph& grid_graph, ExportLayer& layer)
{
  if (layer.edge_count > std::numeric_limits<size_t>::max() / 12) {
    throw std::runtime_error("GR layer edge payload exceeds addressable size");
  }
  const GridMap<GRWireEdge>& edge_map = grid_graph.getEdgeMap(layer.layer_index);
  if (edge_map.get_x_size() != layer.edge_x_size || edge_map.get_y_size() != layer.edge_y_size) {
    throw std::runtime_error("GR layer edge map size does not match layer metadata");
  }
  std::vector<uint8_t> payload(static_cast<size_t>(layer.edge_count) * 12);
  size_t local_index = 0;
  for (int32_t y = 0; y < layer.edge_y_size; y++) {
    for (int32_t x = 0; x < layer.edge_x_size; x++) {
      const GRWireEdge& edge = edge_map[x][y];
      uint32_t supply = checkedUint32(edge.supply, "GR edge supply");
      float fixed_demand = checkedFloat(edge.getFixedDemand(), "GR edge fixed demand");
      float reserved_supply = checkedFloat(edge.reserved_supply, "GR edge reserved supply");
      float routing_demand = checkedFloat(edge.getDemand(), "GR edge routing demand");
      double usable_supply = std::max(0.0, static_cast<double>(supply) - static_cast<double>(fixed_demand));
      double overflow = std::max(0.0, static_cast<double>(routing_demand) - usable_supply);

      setU32At(payload, local_index * 4, supply);
      setFloatAt(payload, static_cast<size_t>(layer.edge_count) * 4 + local_index * 4, fixed_demand);
      setFloatAt(payload, static_cast<size_t>(layer.edge_count) * 8 + local_index * 4, routing_demand);

      layer.metric_ranges.supply.add(static_cast<double>(supply));
      layer.metric_ranges.fixed_demand.add(static_cast<double>(fixed_demand));
      layer.metric_ranges.routing_demand.add(static_cast<double>(routing_demand));
      layer.metric_ranges.usable_supply.add(usable_supply);
      layer.metric_ranges.overflow.add(overflow);
      layer.metric_ranges.reserved_supply.add(static_cast<double>(reserved_supply));
      local_index++;
    }
  }
  if (local_index != layer.edge_count) {
    throw std::runtime_error("GR layer edge count mismatch");
  }
  layer.metric_ranges.supply.finish();
  layer.metric_ranges.fixed_demand.finish();
  layer.metric_ranges.routing_demand.finish();
  layer.metric_ranges.usable_supply.finish();
  layer.metric_ranges.overflow.finish();
  layer.metric_ranges.reserved_supply.finish();
  writeSectionFile(staging_path, layer.edges_section, kSectionLayerEdges, layer.edge_count, payload);
  return layer.metric_ranges;
}

uint8_t getLayerOrdinal(const std::vector<ExportLayer>& layer_list, int32_t layer_index)
{
  for (const ExportLayer& layer : layer_list) {
    if (layer.layer_index == layer_index) {
      return layer.ordinal;
    }
  }
  throw std::runtime_error("GR route references a routing layer outside the active layer range");
}

const ExportLayer& getExportLayer(const std::vector<ExportLayer>& layer_list, int32_t layer_index)
{
  for (const ExportLayer& layer : layer_list) {
    if (layer.layer_index == layer_index) {
      return layer;
    }
  }
  throw std::runtime_error("GR edge references a routing layer outside the active layer range");
}

uint32_t getGlobalEdgeId(const std::vector<ExportLayer>& layer_list, const GRGridGraph& grid_graph, const GRWireEdgeId& edge_id)
{
  if (!grid_graph.hasWireEdge(edge_id)) {
    throw std::runtime_error("GR route references an invalid wire edge");
  }
  const ExportLayer& layer = getExportLayer(layer_list, edge_id.layer_idx);
  uint64_t local_index = 0;
  if (layer.is_horizontal) {
    local_index = static_cast<uint64_t>(edge_id.y) * static_cast<uint64_t>(layer.edge_x_size) + static_cast<uint64_t>(edge_id.x);
  } else {
    local_index = static_cast<uint64_t>(edge_id.y) * static_cast<uint64_t>(layer.edge_x_size) + static_cast<uint64_t>(edge_id.x);
  }
  uint64_t global_edge_id = layer.edge_base + local_index;
  if (global_edge_id > std::numeric_limits<uint32_t>::max()) {
    throw std::runtime_error("GR global edge id exceeds uint32");
  }
  return static_cast<uint32_t>(global_edge_id);
}

void prepareNetRecords(const std::vector<GRNet>& net_list, std::vector<ExportNetRecord>& net_record_list, std::vector<uint8_t>& name_payload,
                       SnapshotStats& stats)
{
  net_record_list.reserve(net_list.size());
  std::set<int32_t> source_net_idx_set;
  for (const GRNet& gr_net : net_list) {
    Net* net = gr_net.get_origin_net();
    if (net == nullptr || net->get_net_idx() < 0) {
      throw std::runtime_error("GR net has an invalid source net index");
    }
    if (!source_net_idx_set.insert(net->get_net_idx()).second) {
      throw std::runtime_error("GR net list contains a duplicate source net index");
    }
    std::string& net_name = net->get_net_name();
    ExportNetRecord record;
    record.source_net_idx = static_cast<uint32_t>(net->get_net_idx());
    record.name_offset = checkedUint32(name_payload.size(), "GR net name offset");
    record.name_length = checkedUint32(net_name.size(), "GR net name length");
    name_payload.insert(name_payload.end(), net_name.begin(), net_name.end());
    const PlanarRect& bbox = gr_net.get_bbox().get_grid_rect();
    record.bbox_llx = bbox.get_ll_x();
    record.bbox_lly = bbox.get_ll_y();
    record.bbox_urx = bbox.get_ur_x();
    record.bbox_ury = bbox.get_ur_y();
    record.via_count = checkedUint32(gr_net.get_tree().via_list.size(), "GR net via count");
    record.pin_count = checkedUint32(gr_net.get_pin_list().size(), "GR net pin count");
    record.flags = gr_net.isClock() ? 1U : 0U;
    net_record_list.push_back(record);
    stats.via_stack_count += gr_net.get_tree().via_list.size();
    stats.pin_count += gr_net.get_pin_list().size();
  }
  checkedUint32(name_payload.size(), "GR net name table size");
  checkedUint32(net_list.size(), "GR net count");
}

void appendUint32Set(std::vector<uint8_t>& payload, const std::vector<uint32_t>& edge_id_list)
{
  uint32_t previous = 0;
  for (size_t i = 0; i < edge_id_list.size(); i++) {
    uint32_t value = i == 0 ? edge_id_list[i] : edge_id_list[i] - previous;
    appendUvarint(payload, value);
    previous = edge_id_list[i];
  }
}

std::vector<uint8_t> buildRouteRecord(const GRGridGraph& grid_graph, const std::vector<ExportLayer>& layer_list, const GRNet& gr_net, uint32_t flags,
                                      uint32_t& planar_edge_count, SnapshotStats& stats)
{
  const GRTree& tree = gr_net.get_tree();
  std::vector<uint32_t> edge_id_list;
  for (const GRWireSegment& wire_segment : tree.wire_list) {
    GRWireEdgeIter iterator(grid_graph, wire_segment.first, wire_segment.second);
    if (!iterator.isLegal()) {
      throw std::runtime_error("GR net contains an illegal planar wire segment");
    }
    for (; iterator.isValid(); iterator.next()) {
      edge_id_list.push_back(getGlobalEdgeId(layer_list, grid_graph, iterator.getEdgeId()));
    }
  }
  std::sort(edge_id_list.begin(), edge_id_list.end());
  edge_id_list.erase(std::unique(edge_id_list.begin(), edge_id_list.end()), edge_id_list.end());

  std::vector<uint8_t> record;
  appendU32(record, 0);
  appendU32(record, static_cast<uint32_t>(gr_net.get_origin_net()->get_net_idx()));
  planar_edge_count = checkedUint32(edge_id_list.size(), "GR net planar edge count");
  appendU32(record, planar_edge_count);
  appendU32(record, checkedUint32(tree.via_list.size(), "GR net via count"));
  appendU32(record, checkedUint32(gr_net.get_pin_list().size(), "GR net pin count"));
  appendU32(record, flags);
  appendU32(record, 0);
  appendUint32Set(record, edge_id_list);

  for (const GRViaStack& via_stack : tree.via_list) {
    if (via_stack.coord.get_x() < 0 || via_stack.coord.get_y() < 0 || via_stack.coord.get_x() >= grid_graph.getXSize()
        || via_stack.coord.get_y() >= grid_graph.getYSize() || via_stack.bottom_layer_idx >= via_stack.top_layer_idx) {
      throw std::runtime_error("GR net contains an invalid via stack");
    }
    uint8_t bottom_ordinal = getLayerOrdinal(layer_list, via_stack.bottom_layer_idx);
    uint8_t top_ordinal = getLayerOrdinal(layer_list, via_stack.top_layer_idx);
    if (bottom_ordinal >= top_ordinal) {
      throw std::runtime_error("GR via stack layer order is invalid");
    }
    appendUvarint(record, static_cast<uint32_t>(via_stack.coord.get_x()));
    appendUvarint(record, static_cast<uint32_t>(via_stack.coord.get_y()));
    appendU8(record, bottom_ordinal);
    appendU8(record, top_ordinal);
    appendU16(record, 0);
  }

  for (const GRPin& gr_pin : gr_net.get_pin_list()) {
    Pin* pin = gr_pin.pin;
    if (pin == nullptr || pin->get_pin_idx() < 0 || gr_pin.grid_coord.get_x() < 0 || gr_pin.grid_coord.get_y() < 0
        || gr_pin.grid_coord.get_x() >= grid_graph.getXSize() || gr_pin.grid_coord.get_y() >= grid_graph.getYSize()) {
      throw std::runtime_error("GR net contains an invalid pin");
    }
    uint8_t layer_ordinal = getLayerOrdinal(layer_list, gr_pin.layer_idx);
    uint8_t pin_flags = 0;
    pin_flags |= pin->get_is_core() ? 0x01U : 0U;
    pin_flags |= pin->get_is_macro() ? 0x02U : 0U;
    pin_flags |= pin->get_is_pad() ? 0x04U : 0U;
    pin_flags |= pin->get_is_driven() ? 0x08U : 0U;
    appendUvarint(record, static_cast<uint32_t>(gr_pin.grid_coord.get_x()));
    appendUvarint(record, static_cast<uint32_t>(gr_pin.grid_coord.get_y()));
    appendU8(record, layer_ordinal);
    appendU8(record, pin_flags);
    appendU32(record, static_cast<uint32_t>(pin->get_pin_idx()));
  }
  while ((record.size() % 4) != 0) {
    appendU8(record, 0);
  }
  setU32At(record, 0, checkedUint32(record.size(), "GR net route record size"));
  stats.planar_route_edge_reference_count += edge_id_list.size();
  return record;
}

RouteChunkInfo flushRouteChunk(const std::filesystem::path& staging_path, uint32_t ordinal, RouteChunkPayload& chunk, uint32_t net_ordinal_end)
{
  if (chunk.payload.size() > std::numeric_limits<uint32_t>::max()) {
    throw std::runtime_error("GR route chunk exceeds uint32 offset range");
  }
  std::ostringstream path_stream;
  path_stream << "net_routes/route_" << std::setw(6) << std::setfill('0') << ordinal << ".grs";
  RouteChunkInfo info;
  info.ordinal = ordinal;
  info.path = path_stream.str();
  info.net_ordinal_begin = chunk.net_ordinal_begin;
  info.net_ordinal_end = net_ordinal_end;
  writeSectionFile(staging_path, info.path, kSectionNetRouteChunk, chunk.record_count, chunk.payload);
  chunk = RouteChunkPayload();
  return info;
}

void exportNetRoutes(const std::filesystem::path& staging_path, const GRGridGraph& grid_graph, const std::vector<ExportLayer>& layer_list,
                     const std::vector<GRNet>& net_list, std::vector<ExportNetRecord>& net_record_list, std::vector<RouteChunkInfo>& route_chunk_list,
                     SnapshotStats& stats)
{
  RouteChunkPayload current_chunk;
  for (size_t net_ordinal = 0; net_ordinal < net_list.size(); net_ordinal++) {
    uint32_t planar_edge_count = 0;
    std::vector<uint8_t> route_record
        = buildRouteRecord(grid_graph, layer_list, net_list[net_ordinal], net_record_list[net_ordinal].flags, planar_edge_count, stats);
    bool should_flush = current_chunk.record_count > 0
                        && (current_chunk.record_count >= kRouteChunkNetLimit || current_chunk.payload.size() + route_record.size() > kRouteChunkTargetSize);
    if (should_flush) {
      route_chunk_list.push_back(flushRouteChunk(staging_path, checkedUint32(route_chunk_list.size(), "GR route chunk ordinal"), current_chunk,
                                                 checkedUint32(net_ordinal, "GR net ordinal")));
    }
    if (current_chunk.record_count == 0) {
      current_chunk.net_ordinal_begin = checkedUint32(net_ordinal, "GR net ordinal");
    }
    net_record_list[net_ordinal].route_chunk_ordinal = checkedUint32(route_chunk_list.size(), "GR net route chunk ordinal");
    net_record_list[net_ordinal].route_offset = checkedUint32(current_chunk.payload.size(), "GR net route offset");
    net_record_list[net_ordinal].planar_edge_count = planar_edge_count;
    current_chunk.payload.insert(current_chunk.payload.end(), route_record.begin(), route_record.end());
    current_chunk.record_count++;
    if (current_chunk.payload.size() > std::numeric_limits<uint32_t>::max()) {
      throw std::runtime_error("GR route chunk exceeds uint32 offset range");
    }
  }
  if (current_chunk.record_count > 0) {
    route_chunk_list.push_back(flushRouteChunk(staging_path, checkedUint32(route_chunk_list.size(), "GR route chunk ordinal"), current_chunk,
                                               checkedUint32(net_list.size(), "GR net ordinal")));
  }
}

void writeNetNames(const std::filesystem::path& staging_path, const std::vector<uint8_t>& name_payload)
{
  writeSectionFile(staging_path, "sections/net_names.grs", kSectionNetNames, name_payload.size(), name_payload);
}

void writeNetIndex(const std::filesystem::path& staging_path, const std::vector<ExportNetRecord>& net_record_list)
{
  std::vector<uint8_t> payload;
  payload.reserve(net_record_list.size() * kNetIndexRecordSize);
  for (const ExportNetRecord& record : net_record_list) {
    appendU32(payload, record.source_net_idx);
    appendU32(payload, record.name_offset);
    appendU32(payload, record.name_length);
    appendI32(payload, record.bbox_llx);
    appendI32(payload, record.bbox_lly);
    appendI32(payload, record.bbox_urx);
    appendI32(payload, record.bbox_ury);
    appendU32(payload, record.route_chunk_ordinal);
    appendU32(payload, record.route_offset);
    appendU32(payload, record.planar_edge_count);
    appendU32(payload, record.via_count);
    appendU32(payload, record.pin_count);
    appendU32(payload, record.flags);
    appendU32(payload, 0);
  }
  if (payload.size() != net_record_list.size() * kNetIndexRecordSize) {
    throw std::runtime_error("GR net index record size mismatch");
  }
  writeSectionFile(staging_path, "sections/net_index.grs", kSectionNetIndex, net_record_list.size(), payload);
}

nlohmann::json makeMetricRangeJson(const MetricRange& range)
{
  return nlohmann::json::array({range.min, range.max});
}

void writeManifest(const std::filesystem::path& staging_path, const std::string& stage, const GRGridGraph& grid_graph, const GRParams& params,
                   const std::vector<ExportLayer>& layer_list, const std::vector<RouteChunkInfo>& route_chunk_list, size_t net_count,
                   const SnapshotStats& stats)
{
  Database& database = RTDM.getDatabase();
  Die& die = database.get_die();
  if (database.get_micron_dbu() <= 0) {
    throw std::runtime_error("Invalid database micron_dbu for GR snapshot");
  }
  if (die.get_real_ur_x() <= die.get_real_ll_x() || die.get_real_ur_y() <= die.get_real_ll_y()) {
    throw std::runtime_error("Invalid die rectangle for GR snapshot");
  }
  if (grid_graph.getXSize() <= 0 || grid_graph.getYSize() <= 0) {
    throw std::runtime_error("Invalid GR grid size for snapshot");
  }
  checkFiniteNonNegative(params.unit_wire_cost, "GR unit wire cost");
  checkFiniteNonNegative(params.initial_cost_weight, "GR initial cost weight");
  checkFiniteNonNegative(params.cost_weight, "GR cost weight");
  checkFiniteNonNegative(params.max_cost_weight, "GR max cost weight");
  checkFiniteNonNegative(params.congestion_hotspot_ratio, "GR congestion hotspot ratio");
  checkFiniteNonNegative(params.via_base_cost, "GR via base cost");
  checkFiniteNonNegative(params.via_demand, "GR via demand");
  checkFiniteNonNegative(params.max_detour_length_ratio, "GR max detour length ratio");
  checkFiniteNonNegative(params.cleanup_detour_ratio, "GR cleanup detour ratio");
  checkFiniteNonNegative(params.cleanup_min_excess_length, "GR cleanup minimum excess length");
  checkFiniteNonNegative(params.cleanup_accept_length_ratio, "GR cleanup accept length ratio");
  checkFiniteNonNegative(params.trial_smooth_neighbor_ratio, "GR trial smooth neighbor ratio");
  checkFiniteNonNegative(params.trial_reserve_start_ratio, "GR trial reserve start ratio");
  if (params.congestion_hotspot_ratio > 1.0 || params.max_detour_offset < 0 || params.max_shift_round < 0
      || params.cleanup_accept_length_ratio > 1.0 || params.cleanup_max_net_num < 0 || params.trial_smooth_radius < 0
      || params.trial_smooth_neighbor_ratio > 1.0 || params.trial_reserve_start_ratio >= 1.0) {
    throw std::runtime_error("Invalid GR parameters for snapshot");
  }
  nlohmann::json manifest;
  manifest["format"] = "ecc-gr-snapshot";
  manifest["format_version"] = {{"major", kVersionMajor}, {"minor", kVersionMinor}};
  manifest["snapshot_id"] = stage;
  manifest["stage"] = stage;
  manifest["created_at_utc"] = makeUtcTimestamp();
  manifest["producer"] = {{"tool", "ecc-tools"}, {"component", "iRT.GlobalRouter"}, {"call_site", "GlobalRouter::debugExport"}};
  manifest["design"] = {{"name", database.get_design_name()},
                        {"micron_dbu", database.get_micron_dbu()},
                        {"die_real", {{"llx", die.get_real_ll_x()}, {"lly", die.get_real_ll_y()}, {"urx", die.get_real_ur_x()}, {"ury", die.get_real_ur_y()}}}};
  manifest["grid"] = {{"node_model", "implicit_gcell_center"},
                      {"x_size", grid_graph.getXSize()},
                      {"y_size", grid_graph.getYSize()},
                      {"x_axis_section", "sections/gcell_x_axis.grs"},
                      {"y_axis_section", "sections/gcell_y_axis.grs"}};
  manifest["routing_layers"] = nlohmann::json::array();
  for (const ExportLayer& layer : layer_list) {
    nlohmann::json layer_json;
    layer_json["ordinal"] = layer.ordinal;
    layer_json["layer_index"] = layer.layer_index;
    layer_json["name"] = layer.name;
    layer_json["direction"] = layer.is_horizontal ? "horizontal" : "vertical";
    layer_json["edge_x_size"] = layer.edge_x_size;
    layer_json["edge_y_size"] = layer.edge_y_size;
    layer_json["edge_count"] = layer.edge_count;
    layer_json["edge_base"] = layer.edge_base;
    layer_json["edges_section"] = layer.edges_section;
    layer_json["metric_ranges"] = {{"supply", makeMetricRangeJson(layer.metric_ranges.supply)},
                                   {"fixed_demand", makeMetricRangeJson(layer.metric_ranges.fixed_demand)},
                                   {"routing_demand", makeMetricRangeJson(layer.metric_ranges.routing_demand)},
                                   {"usable_supply", makeMetricRangeJson(layer.metric_ranges.usable_supply)},
                                   {"overflow", makeMetricRangeJson(layer.metric_ranges.overflow)},
                                   {"reserved_supply", makeMetricRangeJson(layer.metric_ranges.reserved_supply)}};
    manifest["routing_layers"].push_back(std::move(layer_json));
  }
  uint64_t total_edge_count = layer_list.back().edge_base + layer_list.back().edge_count;
  manifest["edges"] = {{"global_id_type", "uint32"}, {"total_edge_count", total_edge_count}};
  manifest["nets"] = {{"count", net_count},
                      {"index_section", "sections/net_index.grs"},
                      {"name_table_section", "sections/net_names.grs"},
                      {"route_record_alignment", 4},
                      {"route_chunks", nlohmann::json::array()}};
  for (const RouteChunkInfo& chunk : route_chunk_list) {
    manifest["nets"]["route_chunks"].push_back(
        {{"ordinal", chunk.ordinal}, {"path", chunk.path}, {"net_ordinal_begin", chunk.net_ordinal_begin}, {"net_ordinal_end", chunk.net_ordinal_end}});
  }
  manifest["gr_params"] = {{"unit_wire_cost", params.unit_wire_cost},
                           {"initial_cost_weight", params.initial_cost_weight},
                           {"cost_weight", params.cost_weight},
                           {"max_cost_weight", params.max_cost_weight},
                           {"congestion_hotspot_ratio", params.congestion_hotspot_ratio},
                           {"via_base_cost", params.via_base_cost},
                           {"via_demand", params.via_demand},
                           {"max_detour_offset", params.max_detour_offset},
                           {"max_detour_length_ratio", params.max_detour_length_ratio},
                           {"max_shift_round", params.max_shift_round},
                           {"cleanup_detour_ratio", params.cleanup_detour_ratio},
                           {"cleanup_min_excess_length", params.cleanup_min_excess_length},
                           {"cleanup_accept_length_ratio", params.cleanup_accept_length_ratio},
                           {"cleanup_max_net_num", params.cleanup_max_net_num},
                           {"enable_supply_trial", params.enable_supply_trial},
                           {"trial_ghost_failed_net", params.trial_ghost_failed_net},
                           {"trial_smooth_radius", params.trial_smooth_radius},
                           {"trial_smooth_neighbor_ratio", params.trial_smooth_neighbor_ratio},
                           {"trial_reserve_start_ratio", params.trial_reserve_start_ratio},
                           {"trial_keep_last_track", params.trial_keep_last_track}};
  manifest["stats"] = {{"routing_layer_count", layer_list.size()},
                       {"net_count", net_count},
                       {"planar_route_edge_reference_count", stats.planar_route_edge_reference_count},
                       {"via_stack_count", stats.via_stack_count},
                       {"pin_count", stats.pin_count}};

  std::filesystem::path manifest_path = staging_path / "manifest.json";
  std::ofstream output(manifest_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    throw std::runtime_error("Failed to open GR snapshot manifest");
  }
  output << manifest.dump(2) << '\n';
  output.close();
  if (!output) {
    throw std::runtime_error("Failed to write GR snapshot manifest");
  }
}

}  // namespace

GRSnapshotWriter::GRSnapshotWriter(const GRGridGraph& grid_graph, const std::vector<GRNet>& net_list, const GRParams& params)
    : grid_graph_(grid_graph), net_list_(net_list), params_(params)
{
}

void GRSnapshotWriter::write(const std::string& snapshot_path, const std::string& stage) const
{
  validateStage(stage);
  std::filesystem::path final_path(snapshot_path);
  if (final_path.filename().empty()) {
    throw std::runtime_error("GR snapshot path has no directory name");
  }
  if (std::filesystem::exists(final_path)) {
    throw std::runtime_error("GR snapshot path already exists: " + final_path.string());
  }
  std::filesystem::path parent_path = final_path.parent_path();
  if (parent_path.empty()) {
    parent_path = std::filesystem::current_path();
  }
  std::filesystem::create_directories(parent_path);
  std::filesystem::path staging_path = parent_path / ("." + final_path.filename().string() + ".tmp-" + std::to_string(static_cast<long long>(getpid())));
  if (!std::filesystem::create_directory(staging_path)) {
    throw std::runtime_error("Failed to create GR snapshot staging directory: " + staging_path.string());
  }

  try {
    std::filesystem::create_directories(staging_path / "sections");
    std::filesystem::create_directories(staging_path / "net_routes");
    std::vector<ExportLayer> layer_list = buildExportLayerList(grid_graph_);
    ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
    exportAxis(staging_path, "sections/gcell_x_axis.grs", gcell_axis.get_x_grid_list(), grid_graph_.getXSize(), "x");
    exportAxis(staging_path, "sections/gcell_y_axis.grs", gcell_axis.get_y_grid_list(), grid_graph_.getYSize(), "y");
    for (ExportLayer& layer : layer_list) {
      exportLayerEdges(staging_path, grid_graph_, layer);
    }

    std::vector<uint8_t> name_payload;
    std::vector<ExportNetRecord> net_record_list;
    std::vector<RouteChunkInfo> route_chunk_list;
    SnapshotStats stats;
    prepareNetRecords(net_list_, net_record_list, name_payload, stats);
    exportNetRoutes(staging_path, grid_graph_, layer_list, net_list_, net_record_list, route_chunk_list, stats);
    writeNetNames(staging_path, name_payload);
    writeNetIndex(staging_path, net_record_list);
    writeManifest(staging_path, stage, grid_graph_, params_, layer_list, route_chunk_list, net_record_list.size(), stats);
    std::filesystem::rename(staging_path, final_path);
  } catch (...) {
    std::error_code system_error;
    std::filesystem::remove_all(staging_path, system_error);
    throw;
  }
}

}  // namespace irt
